// Aseprite
// Copyright (C) 2018-2025  Igara Studio S.A.
// Copyright (C) 2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/cmd/set_layer_blend_mode.h"
#include "app/cmd/set_layer_flags.h"
#include "app/cmd/set_layer_name.h"
#include "app/cmd/set_layer_opacity.h"
#include "app/cmd/set_layer_tileset.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/pref/preferences.h"
#include "app/file/file.h"
#include "app/modules/gui.h"
#include "app/script/blend_mode.h"
#include "app/script/docobj.h"
#include "app/script/engine.h"
#include "app/script/luacpp.h"
#include "app/script/userdata.h"
#include "app/tx.h"
#include "app/ui/timeline/timeline.h"
#include "base/fs.h"
#include "base/string.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/layer_tilemap.h"
#include "doc/sprite.h"
#include "doc/tileset.h"
#include "doc/tilesets.h"
#include "render/dithering.h"
#include "render/quantization.h"
#include "render/render.h"

namespace app { namespace script {

using namespace doc;

namespace {

// Returns false if we are outside a transaction, so you have to
// change the flag manually.
bool set_layer_flags_with_undo(Layer* layer, const bool enableFlag, const LayerFlags flag)
{
  ASSERT(layer->sprite());
  Doc* doc = static_cast<Doc*>(layer->sprite()->document());
  ASSERT(doc);

  // With undo when we are inside a transaction
  if (doc->transaction()) {
    Tx tx(doc);
    LayerFlags newFlags = layer->flags();
    if (enableFlag)
      newFlags |= flag;
    else
      newFlags &= ~flag;

    tx(new cmd::SetLayerFlags(layer, newFlags));
    tx.commit();
    return true;
  }

  // Without undo
  return false;
}

int Layer_eq(lua_State* L)
{
  const auto a = may_get_docobj<Layer>(L, 1);
  const auto b = may_get_docobj<Layer>(L, 2);
  lua_pushboolean(L, (!a && !b) || (a && b && a->id() == b->id()));
  return 1;
}

int Layer_cel(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  auto cel = layer->cel(get_frame_number_from_arg(L, 2));
  if (cel)
    push_docobj<Cel>(L, cel);
  else
    lua_pushnil(L);
  return 1;
}

int Layer_get_id(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushinteger(L, layer->id());
  return 1;
}

int Layer_get_sprite(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  push_docobj<Sprite>(L, layer->sprite());
  return 1;
}

int Layer_get_parent(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->parent() == layer->sprite()->root())
    push_docobj<Sprite>(L, layer->sprite());
  else
    push_docobj<Layer>(L, layer->parent());
  return 1;
}

int Layer_get_layers(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isGroup())
    push_group_layers(L, layer);
  else
    lua_pushnil(L);
  return 1;
}

int Layer_get_stackIndex(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const auto& layers = layer->parent()->layers();
  auto it = std::find(layers.begin(), layers.end(), layer);
  ASSERT(it != layers.end());
  if (it != layers.end())
    lua_pushinteger(L, it - layers.begin() + 1);
  else
    lua_pushnil(L);
  return 1;
}

int Layer_get_previous(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (auto previous = layer->getPrevious())
    push_docobj<Layer>(L, previous);
  else
    lua_pushnil(L);
  return 1;
}

int Layer_get_next(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (auto next = layer->getNext())
    push_docobj<Layer>(L, next);
  else
    lua_pushnil(L);
  return 1;
}

int Layer_get_name(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushstring(L, layer->name().c_str());
  return 1;
}

int Layer_get_opacity(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isImage() ||
      (layer->isGroup() && app::Preferences::instance().experimental.composeGroups())) {
    lua_pushinteger(L, static_cast<LayerImage*>(layer)->opacity());
    return 1;
  }
  else
    return 0;
}

int Layer_get_blendMode(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isImage() ||
      (layer->isGroup() && app::Preferences::instance().experimental.composeGroups())) {
    lua_pushinteger(
      L,
      int(base::convert_to<app::script::BlendMode>(static_cast<LayerImage*>(layer)->blendMode())));
    return 1;
  }
  else
    return 0;
}

int Layer_get_isImage(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isImage());
  return 1;
}

int Layer_get_isGroup(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isGroup());
  return 1;
}

int Layer_get_isTilemap(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isTilemap());
  return 1;
}

int Layer_get_isTransparent(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isTransparent());
  return 1;
}

int Layer_get_isBackground(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isBackground());
  return 1;
}

int Layer_get_isEditable(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isEditable());
  return 1;
}

int Layer_get_isVisible(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isVisible());
  return 1;
}

int Layer_get_isContinuous(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isContinuous());
  return 1;
}

int Layer_get_isCollapsed(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isCollapsed());
  return 1;
}

int Layer_get_isExpanded(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isExpanded());
  return 1;
}

int Layer_get_isReference(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isReference());
  return 1;
}

int Layer_get_isSmartObject(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_pushboolean(L, layer->isSmartObject());
  return 1;
}

void notify_layer_update(Layer* layer)
{
  if (layer && layer->sprite() && layer->sprite()->document()) {
    Doc* doc = static_cast<Doc*>(layer->sprite()->document());
    doc->notifyGeneralUpdate();
  }
}

gfx::PointF get_point_f(lua_State* L, int index, const gfx::PointF& def = gfx::PointF(0.0, 0.0))
{
  gfx::PointF pt = def;
  if (lua_istable(L, index)) {
    int type = lua_getfield(L, index, "x");
    if (VALID_LUATYPE(type) && !lua_isnil(L, -1)) {
      pt.x = lua_tonumber(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, index, "y");
      if (!lua_isnil(L, -1))
        pt.y = lua_tonumber(L, -1);
      lua_pop(L, 1);
    }
    else {
      lua_pop(L, 1);
      lua_geti(L, index, 1);
      if (!lua_isnil(L, -1))
        pt.x = lua_tonumber(L, -1);
      lua_pop(L, 1);
      lua_geti(L, index, 2);
      if (!lua_isnil(L, -1))
        pt.y = lua_tonumber(L, -1);
      lua_pop(L, 1);
    }
  }
  else if (lua_isnumber(L, index)) {
    pt.x = lua_tonumber(L, index);
    if (lua_isnumber(L, index + 1))
      pt.y = lua_tonumber(L, index + 1);
    else
      pt.y = pt.x;
  }
  return pt;
}

void push_point_f(lua_State* L, const gfx::PointF& pt)
{
  lua_createtable(L, 2, 2);
  lua_pushnumber(L, pt.x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, pt.y);
  lua_setfield(L, -2, "y");
  lua_pushnumber(L, pt.x);
  lua_rawseti(L, -2, 1);
  lua_pushnumber(L, pt.y);
  lua_rawseti(L, -2, 2);
}

int Layer_get_scale(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    push_point_f(L, sm->scale());
  }
  else {
    push_point_f(L, gfx::PointF(1.0, 1.0));
  }
  return 1;
}

int Layer_set_scale(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    gfx::PointF sc = get_point_f(L, 2, sm->scale());
    sm->setScale(sc);
    sm->renderAllCels();
    notify_layer_update(layer);
  }
  return 0;
}

int Layer_get_angle(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject())
    lua_pushnumber(L, static_cast<LayerSmartObject*>(layer)->angle());
  else
    lua_pushnumber(L, 0.0);
  return 1;
}

int Layer_set_angle(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    sm->setAngle(lua_tonumber(L, 2));
    sm->renderAllCels();
    notify_layer_update(layer);
  }
  return 0;
}

int Layer_get_flipH(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject())
    lua_pushboolean(L, static_cast<LayerSmartObject*>(layer)->flipH());
  else
    lua_pushboolean(L, false);
  return 1;
}

int Layer_set_flipH(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    sm->setFlipH(lua_toboolean(L, 2));
    sm->renderAllCels();
    notify_layer_update(layer);
  }
  return 0;
}

int Layer_get_flipV(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject())
    lua_pushboolean(L, static_cast<LayerSmartObject*>(layer)->flipV());
  else
    lua_pushboolean(L, false);
  return 1;
}

int Layer_set_flipV(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    sm->setFlipV(lua_toboolean(L, 2));
    sm->renderAllCels();
    notify_layer_update(layer);
  }
  return 0;
}

int Layer_get_pivot(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    push_point_f(L, sm->pivot());
  }
  else {
    push_point_f(L, gfx::PointF(0.5, 0.5));
  }
  return 1;
}

int Layer_set_pivot(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    gfx::PointF p = get_point_f(L, 2, sm->pivot());
    sm->setPivot(p);
    sm->renderAllCels();
    notify_layer_update(layer);
  }
  return 0;
}

int Layer_get_smartObjectFilepath(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject())
    lua_pushstring(L, static_cast<LayerSmartObject*>(layer)->filepath().c_str());
  else
    lua_pushnil(L);
  return 1;
}

int Layer_set_smartObjectFilepath(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return luaL_error(L, "Cannot set smartObjectFilepath on non-smart layer");

  const char* str = luaL_checkstring(L, 2);
  if (!str || !*str)
    return luaL_error(L, "Filepath cannot be empty");

  std::string newPath = base::get_canonical_path(str);
  if (!base::is_file(newPath))
    newPath = str;

  // Prevent linking document to itself
  Doc* doc = (layer->sprite() ? static_cast<Doc*>(layer->sprite()->document()) : nullptr);
  if (doc && !doc->filename().empty()) {
    std::string hostCanon = base::get_canonical_path(doc->filename());
    std::string targetCanon = base::get_canonical_path(str);
    if (hostCanon == targetCanon || hostCanon == newPath) {
      return luaL_error(L, "Cannot link smart object to its own host document");
    }
  }

  auto sm = static_cast<LayerSmartObject*>(layer);
  if (sm->filepath() != newPath) {
    sm->setFilepath(newPath);
    sm->reloadSource(true);
    notify_layer_update(layer);
  }
  return 0;
}

int Layer_get_smartObjectLastModified(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isSmartObject())
    lua_pushinteger(L, static_cast<LayerSmartObject*>(layer)->lastModifiedTime());
  else
    lua_pushinteger(L, 0);
  return 1;
}

int Layer_set_smartObjectLastModified(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return luaL_error(L, "Cannot set smartObjectLastModified on non-smart layer");

  uint64_t t = static_cast<uint64_t>(luaL_checkinteger(L, 2));
  auto sm = static_cast<LayerSmartObject*>(layer);
  sm->setLastModifiedTime(t);
  return 0;
}

int Layer_get_smartObjectTimelineMapping(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return lua_pushnil(L), 1;

  auto sm = static_cast<LayerSmartObject*>(layer);
  switch (sm->timelineMapping()) {
    case TimelineMapping::Hold:  lua_pushstring(L, "hold"); break;
    case TimelineMapping::Fixed: lua_pushstring(L, "fixed"); break;
    case TimelineMapping::Loop:
    default:                     lua_pushstring(L, "loop"); break;
  }
  return 1;
}

int Layer_set_smartObjectTimelineMapping(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return luaL_error(L, "Cannot set smartObjectTimelineMapping on non-smart layer");

  const char* str = luaL_checkstring(L, 2);
  auto sm = static_cast<LayerSmartObject*>(layer);
  if (base::utf8_icmp(str, "hold") == 0)
    sm->setTimelineMapping(TimelineMapping::Hold);
  else if (base::utf8_icmp(str, "fixed") == 0)
    sm->setTimelineMapping(TimelineMapping::Fixed);
  else
    sm->setTimelineMapping(TimelineMapping::Loop);

  notify_layer_update(layer);
  return 0;
}

int Layer_refreshSmartObject(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "Layer is not a smart object");
    return 2;
  }

  auto sm = static_cast<LayerSmartObject*>(layer);
  bool force = false;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
    force = lua_toboolean(L, 2);
  }

  bool updated = sm->reloadSource(force);
  if (updated) {
    notify_layer_update(layer);
    lua_pushboolean(L, true);
    return 1;
  }
  else {
    lua_pushboolean(L, false);
    if (!base::is_file(sm->filepath()))
      lua_pushstring(L, "Linked file missing on disk");
    else
      lua_pushstring(L, "No changes detected on disk");
    return 2;
  }
}

int Layer_get_linkStatus(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject()) {
    lua_pushnil(L);
    return 1;
  }
  auto sm = static_cast<LayerSmartObject*>(layer);
  switch (sm->linkStatus()) {
    case SmartObjectStatus::Linked:
      lua_pushstring(L, "Linked");
      break;
    case SmartObjectStatus::Outdated:
      lua_pushstring(L, "Outdated");
      break;
    case SmartObjectStatus::Missing:
      lua_pushstring(L, "Missing");
      break;
    case SmartObjectStatus::Broken:
    default:
      lua_pushstring(L, "Broken");
      break;
  }
  return 1;
}

int Layer_get_isDiskFileNewer(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject()) {
    lua_pushboolean(L, false);
    return 1;
  }
  auto sm = static_cast<LayerSmartObject*>(layer);
  lua_pushboolean(L, sm->isDiskFileNewer());
  return 1;
}

int Layer_get_linkedFrameCount(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject()) {
    lua_pushinteger(L, 0);
    return 1;
  }
  auto sm = static_cast<LayerSmartObject*>(layer);
  lua_pushinteger(L, sm->linkedFrameCount());
  return 1;
}

int Layer_get_timelineMapping(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return lua_pushnil(L), 1;
  auto sm = static_cast<LayerSmartObject*>(layer);
  switch (sm->timelineMapping()) {
    case TimelineMapping::Hold:
      lua_pushstring(L, "Hold");
      break;
    case TimelineMapping::Fixed:
      lua_pushstring(L, "Fixed");
      break;
    case TimelineMapping::Loop:
    default:
      lua_pushstring(L, "Loop");
      break;
  }
  return 1;
}

int Layer_set_timelineMapping(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;
  auto sm = static_cast<LayerSmartObject*>(layer);
  const char* str = lua_tostring(L, 2);
  if (!str)
    return 0;

  if (base::utf8_icmp(str, "Hold") == 0)
    sm->setTimelineMapping(TimelineMapping::Hold);
  else if (base::utf8_icmp(str, "Fixed") == 0)
    sm->setTimelineMapping(TimelineMapping::Fixed);
  else
    sm->setTimelineMapping(TimelineMapping::Loop);

  notify_layer_update(layer);
  return 0;
}

int Layer_get_transform(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  lua_newtable(L);
  if (layer->isSmartObject()) {
    auto sm = static_cast<LayerSmartObject*>(layer);
    lua_pushnumber(L, sm->position().x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, sm->position().y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, sm->scale().x);
    lua_setfield(L, -2, "scaleX");
    lua_pushnumber(L, sm->scale().y);
    lua_setfield(L, -2, "scaleY");
    lua_pushnumber(L, sm->angle());
    lua_setfield(L, -2, "angle");
    lua_pushboolean(L, sm->flipH());
    lua_setfield(L, -2, "flipH");
    lua_pushboolean(L, sm->flipV());
    lua_setfield(L, -2, "flipV");
    lua_pushnumber(L, sm->pivot().x);
    lua_setfield(L, -2, "pivotX");
    lua_pushnumber(L, sm->pivot().y);
    lua_setfield(L, -2, "pivotY");
  }
  else {
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "scaleX");
    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "scaleY");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "angle");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "flipH");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "flipV");
    lua_pushnumber(L, 0.5);
    lua_setfield(L, -2, "pivotX");
    lua_pushnumber(L, 0.5);
    lua_setfield(L, -2, "pivotY");
  }
  return 1;
}

int Layer_setTransform(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  const int top = lua_gettop(L);

  gfx::PointF pos = sm->position();
  gfx::PointF scale = sm->scale();
  double angle = sm->angle();
  bool flipH = sm->flipH();
  bool flipV = sm->flipV();
  gfx::PointF pivot = sm->pivot();

  if (top >= 5 && (lua_isboolean(L, 5) || (top >= 6 && lua_isboolean(L, 6)))) {
    // Form: (scaleX, scaleY, angle, flipH, flipV, [pivot])
    if (!lua_isnil(L, 2)) scale.x = lua_tonumber(L, 2);
    if (top >= 3 && !lua_isnil(L, 3)) scale.y = lua_tonumber(L, 3);
    if (top >= 4 && !lua_isnil(L, 4)) angle = lua_tonumber(L, 4);
    if (top >= 5 && !lua_isnil(L, 5)) flipH = lua_toboolean(L, 5);
    if (top >= 6 && !lua_isnil(L, 6)) flipV = lua_toboolean(L, 6);
    if (top >= 7 && !lua_isnil(L, 7)) {
      if (lua_istable(L, 7)) {
        pivot = get_point_f(L, 7, pivot);
      }
      else {
        pivot.x = lua_tonumber(L, 7);
        if (top >= 8 && !lua_isnil(L, 8))
          pivot.y = lua_tonumber(L, 8);
      }
    }
  }
  else {
    // Form: (x, y, scaleX, scaleY, angle, flipH, flipV, [pivotX, pivotY] / [pivotTable])
    if (!lua_isnil(L, 2)) pos.x = lua_tonumber(L, 2);
    if (top >= 3 && !lua_isnil(L, 3)) pos.y = lua_tonumber(L, 3);
    if (top >= 4 && !lua_isnil(L, 4)) scale.x = lua_tonumber(L, 4);
    if (top >= 5 && !lua_isnil(L, 5)) scale.y = lua_tonumber(L, 5);
    if (top >= 6 && !lua_isnil(L, 6)) angle = lua_tonumber(L, 6);
    if (top >= 7 && !lua_isnil(L, 7)) flipH = lua_toboolean(L, 7);
    if (top >= 8 && !lua_isnil(L, 8)) flipV = lua_toboolean(L, 8);
    if (top >= 9 && !lua_isnil(L, 9)) {
      if (lua_istable(L, 9)) {
        pivot = get_point_f(L, 9, pivot);
      }
      else {
        pivot.x = lua_tonumber(L, 9);
        if (top >= 10 && !lua_isnil(L, 10))
          pivot.y = lua_tonumber(L, 10);
      }
    }
  }

  sm->setTransform(pos, scale, angle, flipH, flipV, pivot);
  notify_layer_update(layer);
  return 0;
}

int Layer_resetTransform(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  sm->resetTransform();
  notify_layer_update(layer);
  return 0;
}

int Layer_translate(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  double dx = lua_tonumber(L, 2);
  double dy = lua_tonumber(L, 3);
  sm->setPosition(sm->position().x + dx, sm->position().y + dy);
  sm->renderAllCels();
  notify_layer_update(layer);
  return 0;
}

int Layer_renderCel(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  frame_t hf = (lua_gettop(L) >= 2 ? get_frame_number_from_arg(L, 2) : frame_t(0));
  frame_t lf = (lua_gettop(L) >= 3 ? get_frame_number_from_arg(L, 3) : sm->mapTimelineFrame(hf));
  bool res = sm->renderCel(hf, lf);
  notify_layer_update(layer);
  lua_pushboolean(L, res);
  return 1;
}

int Layer_renderAllCels(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  sm->renderAllCels();
  notify_layer_update(layer);
  return 0;
}

int Layer_syncTimeline(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  if (lua_isstring(L, 2)) {
    const char* str = lua_tostring(L, 2);
    if (base::utf8_icmp(str, "Hold") == 0)
      sm->setTimelineMapping(TimelineMapping::Hold);
    else if (base::utf8_icmp(str, "Fixed") == 0)
      sm->setTimelineMapping(TimelineMapping::Fixed);
    else
      sm->setTimelineMapping(TimelineMapping::Loop);
  }

  sm->syncFrames();
  notify_layer_update(layer);
  lua_pushinteger(L, sm->cels().size());
  return 1;
}

int Layer_mapTimelineFrame(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (!layer->isSmartObject())
    return 0;

  auto sm = static_cast<LayerSmartObject*>(layer);
  frame_t hf = get_frame_number_from_arg(L, 2);
  frame_t lf = sm->mapTimelineFrame(hf);
  lua_pushinteger(L, lf + 1); // 1-based for Lua
  return 1;
}

int Layer_get_cels(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  push_cels(L, layer);
  return 1;
}

int Layer_get_tileset(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isTilemap())
    push_tileset(L, static_cast<doc::LayerTilemap*>(layer)->tileset());
  else
    lua_pushnil(L);
  return 1;
}

int Layer_get_uuid(lua_State* L)
{
  auto* layer = get_docobj<Layer>(L, 1);
  push_obj(L, layer->uuid());
  return 1;
}

int Layer_set_name(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const char* name = lua_tostring(L, 2);
  if (name) {
    Tx tx(layer->sprite());
    tx(new cmd::SetLayerName(layer, name));
    tx.commit();
  }
  return 0;
}

int Layer_set_opacity(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const int opacity = lua_tointeger(L, 2);
  if (layer->isImage() || layer->isGroup()) {
    Tx tx(layer->sprite());
    tx(new cmd::SetLayerOpacity(static_cast<LayerImage*>(layer), opacity));
    tx.commit();
  }
  return 0;
}

int Layer_set_blendMode(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  auto blendMode = app::script::BlendMode(lua_tointeger(L, 2));
  if (layer->isImage() || layer->isGroup()) {
    Tx tx(layer->sprite());
    tx(new cmd::SetLayerBlendMode(static_cast<LayerImage*>(layer),
                                  base::convert_to<doc::BlendMode>(blendMode)));
    tx.commit();
  }
  return 0;
}

int Layer_set_stackIndex(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const auto& layers = layer->parent()->layers();
  int newStackIndex = lua_tointeger(L, 2);
  int stackIndex = 1;
  auto parent = layer->parent();

  // First we need to know this layer stackIndex because we'll use
  auto it = std::find(layers.begin(), layers.end(), layer);
  ASSERT(it != layers.end());
  if (it != layers.end())
    stackIndex = it - layers.begin() + 1;

  Layer* beforeThis = nullptr;
  if (newStackIndex > stackIndex) {
    ++newStackIndex;
  }

  if (newStackIndex - 1 < int(parent->layers().size())) {
    beforeThis = parent->layers()[std::clamp(newStackIndex - 1, 0, (int)parent->layers().size())];
  }
  else {
    beforeThis = nullptr;
  }

  // Do nothing
  if (beforeThis == layer)
    return 0;

  Doc* doc = static_cast<Doc*>(layer->sprite()->document());
  Tx tx(doc);
  DocApi(doc, tx).restackLayerBefore(layer, parent, beforeThis);
  tx.commit();
  return 0;
}

int Layer_set_isEditable(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const bool newState = lua_toboolean(L, 2);

  if (!set_layer_flags_with_undo(layer, newState, LayerFlags::Editable))
    layer->setEditable(newState);

  return 0;
}

int Layer_set_isVisible(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const bool newState = lua_toboolean(L, 2);

  if (!set_layer_flags_with_undo(layer, newState, LayerFlags::Visible)) {
    Doc* doc = static_cast<Doc*>(layer->sprite()->document());
    doc->setLayerVisibilityWithNotifications(layer, newState);
  }

  return 0;
}

int Layer_set_isContinuous(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const bool newState = lua_toboolean(L, 2);

  if (!set_layer_flags_with_undo(layer, newState, LayerFlags::Continuous))
    layer->setContinuous(newState);

  return 0;
}

int Layer_set_isCollapsed(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const bool newState = lua_toboolean(L, 2);

  if (!set_layer_flags_with_undo(layer, newState, LayerFlags::Collapsed))
    layer->setCollapsed(newState);

  // TODO the timeline should be listening this kind of changes
  if (auto* timeline = App::instance()->timeline())
    timeline->refresh();

  return 0;
}

int Layer_set_isExpanded(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  const bool newState = !lua_toboolean(L, 2);

  if (!set_layer_flags_with_undo(layer, newState, LayerFlags::Collapsed))
    layer->setCollapsed(newState);

  // TODO the timeline should be listening this kind of changes
  if (auto* timeline = App::instance()->timeline())
    timeline->refresh();

  return 0;
}

int Layer_set_parent(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  LayerGroup* parent = nullptr;
  if (auto sprite = may_get_docobj<Sprite>(L, 2)) {
    parent = sprite->root();
  }
  else if (auto parentLayer = may_get_docobj<Layer>(L, 2)) {
    if (parentLayer->isGroup())
      parent = static_cast<LayerGroup*>(parentLayer);
    else
      return luaL_error(L, "the given parent is not a layer group or sprite");
  }

  if (!parent)
    return luaL_error(L, "parent cannot be nil");
  else if (parent == layer)
    return luaL_error(L, "the parent of a layer cannot be the layer itself");

  // TODO Why? should we be able to do this? It would require some hard work:
  // 1. convert color modes
  // 2. multiple transactions for both modified sprites?
  if (parent->sprite() != layer->sprite())
    return luaL_error(L, "you cannot move layers between sprites");

  if (parent) {
    Doc* doc = static_cast<Doc*>(layer->sprite()->document());
    Tx tx(doc);
    DocApi(doc, tx).restackLayerAfter(layer, parent, parent->lastLayer());
    tx.commit();
  }
  return 0;
}

int Layer_set_tileset(lua_State* L)
{
  auto layer = get_docobj<Layer>(L, 1);
  if (layer->isTilemap()) {
    auto tilemap = static_cast<doc::LayerTilemap*>(layer);
    doc::tileset_index tsi = tilemap->tilesetIndex();

    if (auto tileset = may_get_docobj<Tileset>(L, 2))
      tsi = layer->sprite()->tilesets()->getIndex(tileset);
    else if (lua_isinteger(L, 2))
      tsi = lua_tointeger(L, 2);

    if (tsi != tilemap->tilesetIndex()) {
      Tx tx(layer->sprite());
      tx(new cmd::SetLayerTileset(tilemap, tsi));
      tx.commit();
    }
  }
  return 0;
}

const luaL_Reg Layer_methods[] = {
  { "__eq",             Layer_eq               },
  { "cel",              Layer_cel              },
  { "setTransform",     Layer_setTransform     },
  { "resetTransform",   Layer_resetTransform   },
  { "translate",        Layer_translate        },
  { "renderCel",        Layer_renderCel        },
  { "renderAllCels",    Layer_renderAllCels    },
  { "syncTimeline",     Layer_syncTimeline     },
  { "mapTimelineFrame",   Layer_mapTimelineFrame   },
  { "refreshSmartObject", Layer_refreshSmartObject },
  { nullptr,              nullptr                  }
};

const Property Layer_properties[] = {
  { "id",               Layer_get_id,                   nullptr                        },
  { "sprite",           Layer_get_sprite,               nullptr                        },
  { "parent",           Layer_get_parent,               Layer_set_parent               },
  { "layers",           Layer_get_layers,               nullptr                        },
  { "stackIndex",       Layer_get_stackIndex,           Layer_set_stackIndex           },
  { "previous",         Layer_get_previous,             nullptr                        },
  { "next",             Layer_get_next,                 nullptr                        },
  { "name",             Layer_get_name,                 Layer_set_name                 },
  { "opacity",          Layer_get_opacity,              Layer_set_opacity              },
  { "blendMode",        Layer_get_blendMode,            Layer_set_blendMode            },
  { "isImage",          Layer_get_isImage,              nullptr                        },
  { "isGroup",          Layer_get_isGroup,              nullptr                        },
  { "isTilemap",        Layer_get_isTilemap,            nullptr                        },
  { "isTransparent",    Layer_get_isTransparent,        nullptr                        },
  { "isBackground",     Layer_get_isBackground,         nullptr                        },
  { "isEditable",       Layer_get_isEditable,           Layer_set_isEditable           },
  { "isVisible",        Layer_get_isVisible,            Layer_set_isVisible            },
  { "isContinuous",     Layer_get_isContinuous,         Layer_set_isContinuous         },
  { "isCollapsed",      Layer_get_isCollapsed,          Layer_set_isCollapsed          },
  { "isExpanded",       Layer_get_isExpanded,           Layer_set_isExpanded           },
  { "isReference",      Layer_get_isReference,          nullptr                        },
  { "isSmartObject",    Layer_get_isSmartObject,        nullptr                        },
  { "scale",            Layer_get_scale,                Layer_set_scale                },
  { "angle",            Layer_get_angle,                Layer_set_angle                },
  { "flipH",            Layer_get_flipH,                Layer_set_flipH                },
  { "flipV",            Layer_get_flipV,                Layer_set_flipV                },
  { "pivot",            Layer_get_pivot,                Layer_set_pivot                },
  { "smartObjectTimelineMapping", Layer_get_smartObjectTimelineMapping, Layer_set_smartObjectTimelineMapping },
  { "timelineMapping",  Layer_get_timelineMapping,      Layer_set_timelineMapping      },
  { "linkedFrameCount", Layer_get_linkedFrameCount,     nullptr                        },
  { "smartObjectFilepath", Layer_get_smartObjectFilepath, Layer_set_smartObjectFilepath },
  { "sourcePath",       Layer_get_smartObjectFilepath,  Layer_set_smartObjectFilepath  },
  { "smartObjectLastModified", Layer_get_smartObjectLastModified, Layer_set_smartObjectLastModified },
  { "linkStatus",       Layer_get_linkStatus,           nullptr                        },
  { "isDiskFileNewer",  Layer_get_isDiskFileNewer,      nullptr                        },
  { "transform",        Layer_get_transform,            nullptr                        },
  { "cels",             Layer_get_cels,                 nullptr                        },
  { "color",            UserData_get_color<Layer>,      UserData_set_color<Layer>      },
  { "data",             UserData_get_text<Layer>,       UserData_set_text<Layer>       },
  { "properties",       UserData_get_properties<Layer>, UserData_set_properties<Layer> },
  { "tileset",          Layer_get_tileset,              Layer_set_tileset              },
  { "uuid",             Layer_get_uuid,                 nullptr                        },
  { nullptr,            nullptr,                        nullptr                        }
};

} // anonymous namespace

DEF_MTNAME(Layer);

void register_layer_class(lua_State* L)
{
  using Layer = doc::Layer;
  REG_CLASS(L, Layer);
  REG_CLASS_PROPERTIES(L, Layer);

  static bool s_smartHooksInitialized = false;
  if (!s_smartHooksInitialized) {
    s_smartHooksInitialized = true;
    doc::LayerSmartObject::setSourceDocLoader([](const std::string& path) -> doc::Document* {
      return load_document(nullptr, path);
    });
    doc::LayerSmartObject::setCompositeRenderer([](doc::Document* srcDoc, frame_t linkedFrame, const doc::Sprite* hostSprite) -> doc::ImageRef {
      if (!srcDoc || !srcDoc->sprite())
        return nullptr;
      doc::Sprite* spr = srcDoc->sprite();
      render::Render r;
      r.setNewBlend(true);
      r.setBgOptions(render::BgOptions::MakeNone());
      doc::ImageRef img(doc::Image::create(spr->pixelFormat(), spr->width(), spr->height()));
      doc::clear_image(img.get(), spr->transparentColor());
      r.renderSprite(img.get(), spr, linkedFrame);

      if (hostSprite && (hostSprite->pixelFormat() != spr->pixelFormat() ||
                         hostSprite->pixelFormat() == IMAGE_INDEXED)) {
        doc::ImageRef converted(
          render::convert_pixel_format(img.get(),
                                       nullptr,
                                       hostSprite->pixelFormat(),
                                       render::Dithering(),
                                       hostSprite->rgbMap(linkedFrame),
                                       spr->palette(linkedFrame),
                                       (spr->backgroundLayer() ? true : false),
                                       hostSprite->transparentColor()));
        if (converted)
          img = converted;
      }
      return img;
    });
  }
}

}} // namespace app::script
