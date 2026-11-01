// Aseprite
// Copyright (C) 2021-2024  Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/cmd/add_cel.h"
#include "app/cmd/add_layer.h"
#include "app/cmd/add_tileset.h"
#include "app/cmd/background_from_layer.h"
#include "app/cmd/copy_cel.h"
#include "app/cmd/layer_from_background.h"
#include "app/cmd/move_layer.h"
#include "app/cmd/remove_layer.h"
#include "app/commands/command.h"
#include "app/commands/new_params.h"
#include "app/console.h"
#include "app/context_access.h"
#include "app/doc.h"
#include "app/doc_range.h"
#include "app/file/file.h"
#include "app/file_selector.h"
#include "app/i18n/strings.h"
#include "app/modules/gui.h"
#include "app/smart_object_manager.h"
#include "app/tx.h"
#include "app/ui/editor/editor.h"
#include "app/ui/tileset_selector.h"
#include "app/util/cel_ops.h"
#include "base/fs.h"
#include "doc/grid.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/layer_tilemap.h"
#include "doc/palette.h"
#include "doc/selected_layers.h"
#include "doc/sprite.h"
#include "doc/tileset.h"
#include "doc/tilesets.h"

#ifdef ENABLE_SCRIPTING
  #include "app/script/luacpp.h"
#endif

#include "tileset_selector_window.xml.h"

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace app {

enum class ConvertLayerParam {
  None,
  Background,
  Layer,
  Tilemap,
  SmartObject,
  Group,
  SeparateSmartObjects
};

template<>
void Param<ConvertLayerParam>::fromString(const std::string& value)
{
  if (value == "background")
    setValue(ConvertLayerParam::Background);
  else if (value == "layer")
    setValue(ConvertLayerParam::Layer);
  else if (value == "tilemap")
    setValue(ConvertLayerParam::Tilemap);
  else if (value == "smartobject" || value == "smart_object")
    setValue(ConvertLayerParam::SmartObject);
  else if (value == "group")
    setValue(ConvertLayerParam::Group);
  else if (value == "separatesmartobjects" || value == "separate_smart_objects")
    setValue(ConvertLayerParam::SeparateSmartObjects);
  else
    setValue(ConvertLayerParam::None);
}

#ifdef ENABLE_SCRIPTING
template<>
void Param<ConvertLayerParam>::fromLua(lua_State* L, int index)
{
  fromString(lua_tostring(L, index));
}
#endif // ENABLE_SCRIPTING

struct ConvertLayerParams : public NewParams {
  Param<bool> ui{ this, true, "ui" };
  Param<ConvertLayerParam> to{ this, ConvertLayerParam::None, "to" };
  Param<std::string> filename{ this, std::string(), "filename" };
  Param<std::string> folder{ this, std::string(), "folder" };
};

static std::string sanitizeFilename(const std::string& name)
{
  std::string result;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|') {
      result.push_back('_');
    }
    else {
      result.push_back(c);
    }
  }
  if (result.empty())
    result = "layer";
  return result;
}

static void copyLayerTree(const Layer* sourceLayer, Doc* destDoc, Layer* destParent)
{
  std::unique_ptr<Layer> destLayer(sourceLayer->clone(destDoc->sprite()));

  LayerFlags dstFlags = sourceLayer->flags();
  if (((int)dstFlags & (int)LayerFlags::Background) == (int)LayerFlags::Background &&
      (destDoc->sprite()->backgroundLayer())) {
    dstFlags = (LayerFlags)((int)dstFlags & ~(int)(LayerFlags::BackgroundLayerFlags));
  }

  destLayer->setName(sourceLayer->name());
  destLayer->setFlags(dstFlags);
  destLayer->setUserData(sourceLayer->userData());
  destLayer->setBlendMode(sourceLayer->blendMode());
  destLayer->setOpacity(sourceLayer->opacity());

  if (sourceLayer->isGroup() && destLayer->isGroup()) {
    destParent->addLayer(destLayer.get());
    Layer* newGroup = destLayer.release();
    for (const Layer* child : static_cast<const LayerGroup*>(sourceLayer)->layers()) {
      copyLayerTree(child, destDoc, newGroup);
    }
    return;
  }
  else if (sourceLayer->type() == destLayer->type()) {
    std::map<doc::ObjectId, doc::Cel*> linked;
    CelConstIterator it = sourceLayer->getCelBegin();
    const CelConstIterator end = sourceLayer->getCelEnd();

    for (; it != end; ++it) {
      const Cel* sourceCel = *it;
      if (sourceCel->frame() > destLayer->sprite()->lastFrame())
        break;

      std::unique_ptr<Cel> newCel;
      auto itLinked = linked.find(sourceCel->data()->id());
      if (itLinked != linked.end()) {
        newCel.reset(Cel::MakeLink(sourceCel->frame(), itLinked->second));
        newCel->copyNonsharedPropertiesFrom(sourceCel);
      }
      else {
        newCel.reset(create_cel_copy(nullptr,
                                     sourceCel,
                                     destLayer->sprite(),
                                     destLayer.get(),
                                     sourceCel->frame()));
        linked.insert(std::make_pair(sourceCel->data()->id(), newCel.get()));
      }

      destLayer->addCel(newCel.release());
    }
  }

  destParent->addLayer(destLayer.release());
}

static std::unique_ptr<Doc> createDetachedDocFromLayers(Doc* hostDoc,
                                                        const LayerList& layersToCopy,
                                                        const std::string& filepath)
{
  Sprite* hostSprite = hostDoc->sprite();
  auto newSprite = std::make_unique<Sprite>(hostSprite->spec(), hostSprite->palette(0)->size());
  newSprite->setUserData(hostSprite->userData());
  newSprite->setTotalFrames(hostSprite->totalFrames());

  for (frame_t f = 0; f < hostSprite->totalFrames(); ++f)
    newSprite->setFrameDuration(f, hostSprite->frameDuration(f));

  for (const Palette* pal : hostSprite->getPalettes())
    newSprite->setPalette(pal, true);

  if (hostSprite->hasTilesets()) {
    for (Tileset* tileset : *hostSprite->tilesets()) {
      auto tilesetCopy = new Tileset(newSprite.get(), tileset);
      newSprite->tilesets()->add(tilesetCopy);
    }
  }

  auto newDoc = std::make_unique<Doc>(newSprite.get());
  newSprite.release();
  newDoc->setFilename(filepath);

  for (const Layer* src : layersToCopy) {
    copyLayerTree(src, newDoc.get(), newDoc->sprite()->root());
  }

  return newDoc;
}

static void unpackSourceLayers(Tx& tx,
                               const LayerGroup* srcParent,
                               LayerGroup* dstParent,
                               Sprite* dstSprite,
                               int dx,
                               int dy)
{
  for (Layer* srcChild : srcParent->layers()) {
    if (srcChild->isGroup()) {
      auto childGroup = new LayerGroup(dstSprite);
      childGroup->setName(srcChild->name());
      childGroup->setFlags(srcChild->flags() & ~LayerFlags::SmartObject);
      childGroup->setUserData(srcChild->userData());
      tx(new cmd::AddLayer(dstParent, childGroup, dstParent->lastLayer()));
      unpackSourceLayers(tx, static_cast<const LayerGroup*>(srcChild), childGroup, dstSprite, dx, dy);
    }
    else if (srcChild->isImage()) {
      auto childLayer = new LayerImage(dstSprite);
      childLayer->setName(srcChild->name());
      childLayer->setFlags(srcChild->flags() & ~(LayerFlags::SmartObject | LayerFlags::Background));
      childLayer->setUserData(srcChild->userData());
      childLayer->setBlendMode(srcChild->blendMode());
      childLayer->setOpacity(srcChild->opacity());
      tx(new cmd::AddLayer(dstParent, childLayer, dstParent->lastLayer()));

      std::map<doc::ObjectId, doc::Cel*> linkedCels;
      CelList srcCels;
      srcChild->getCels(srcCels);
      for (Cel* srcCel : srcCels) {
        frame_t frame = srcCel->frame();
        if (frame > dstSprite->lastFrame())
          break;

        Cel* linkedSrcCel = srcCel->link();
        if (linkedSrcCel) {
          auto it = linkedCels.find(linkedSrcCel->id());
          if (it != linkedCels.end()) {
            tx(new cmd::CopyCel(childLayer, linkedSrcCel->frame(), childLayer, frame, true));
            continue;
          }
        }

        Cel* newCel = create_cel_copy(tx, srcCel, dstSprite, childLayer, frame);
        newCel->setPosition(newCel->x() + dx, newCel->y() + dy);
        tx(new cmd::AddCel(childLayer, newCel));
        linkedCels[srcCel->id()] = newCel;
      }
    }
  }
}

class ConvertLayerCommand : public CommandWithNewParams<ConvertLayerParams> {
public:
  ConvertLayerCommand();

private:
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
  std::string onGetFriendlyName() const override;

  void copyCels(Tx& tx, Layer* srcLayer, Layer* newLayer);
};

ConvertLayerCommand::ConvertLayerCommand()
  : CommandWithNewParams<ConvertLayerParams>(CommandId::ConvertLayer())
{
}

bool ConvertLayerCommand::onEnabled(Context* ctx)
{
  if (!ctx->checkFlags(ContextFlags::ActiveDocumentIsWritable | ContextFlags::HasActiveSprite |
                       ContextFlags::HasActiveLayer))
    return false;

  const Site& site = ctx->activeSite();
  const DocRange& range = site.range();
  Layer* activeLayer = site.layer();
  bool hasMultiLayers = (range.enabled() && range.selectedLayers().size() > 1);

  switch (params().to()) {
    case ConvertLayerParam::Background:
      if (!ctx->checkFlags(ContextFlags::ActiveLayerIsVisible | ContextFlags::ActiveLayerIsEditable))
        return false;
      if (ctx->checkFlags(ContextFlags::ActiveLayerIsSmartObject) ||
          ctx->checkFlags(ContextFlags::ActiveLayerIsReference))
        return false;
      return !ctx->checkFlags(ContextFlags::HasBackgroundLayer) &&
             ctx->checkFlags(ContextFlags::ActiveLayerIsImage) &&
             !ctx->checkFlags(ContextFlags::ActiveLayerIsTilemap);

    case ConvertLayerParam::Layer:
      if (!ctx->checkFlags(ContextFlags::ActiveLayerIsVisible | ContextFlags::ActiveLayerIsEditable))
        return false;
      if (ctx->checkFlags(ContextFlags::ActiveLayerIsSmartObject) ||
          ctx->checkFlags(ContextFlags::ActiveLayerIsReference))
        return false;
      return ctx->checkFlags(ContextFlags::ActiveLayerIsImage | ContextFlags::ActiveLayerIsBackground) ||
             ctx->checkFlags(ContextFlags::ActiveLayerIsTilemap);

    case ConvertLayerParam::Tilemap:
      if (!ctx->checkFlags(ContextFlags::ActiveLayerIsVisible | ContextFlags::ActiveLayerIsEditable))
        return false;
      if (ctx->checkFlags(ContextFlags::ActiveLayerIsSmartObject) ||
          ctx->checkFlags(ContextFlags::ActiveLayerIsReference))
        return false;
      return ctx->checkFlags(ContextFlags::ActiveLayerIsImage) &&
             !ctx->checkFlags(ContextFlags::ActiveLayerIsTilemap) &&
             !ctx->checkFlags(ContextFlags::ActiveLayerIsBackground);

    case ConvertLayerParam::Group:
      // Allow if active layer is Smart Object (to unpack) OR if multiple layers are selected
      if (activeLayer && activeLayer->isSmartObject())
        return true;
      if (hasMultiLayers)
        return true;
      return false;

    case ConvertLayerParam::SmartObject:
      // Allow if active layer or selected layers can be converted (not already a single smart object/reference layer)
      if (hasMultiLayers)
        return true;
      if (activeLayer) {
        if (activeLayer->isSmartObject() || activeLayer->isReference())
          return false;
        return true;
      }
      return false;

    case ConvertLayerParam::SeparateSmartObjects:
      return hasMultiLayers;

    default:
      return false;
  }
}

void ConvertLayerCommand::onExecute(Context* ctx)
{
  Site site = ctx->activeSite();
  Sprite* sprite = site.sprite();
  Layer* srcLayer = site.layer();

  if (!sprite || !srcLayer)
    return;

  if ((params().to() == ConvertLayerParam::Background ||
       params().to() == ConvertLayerParam::Layer ||
       params().to() == ConvertLayerParam::Tilemap) &&
      (srcLayer->isSmartObject() || srcLayer->isReference()))
    return;

  // Default options to convert a layer to a tilemap
  std::string tilesetName;
  int baseIndex = 1;
  tile_flags matchFlags = 0;
  Grid grid0 = site.grid();
  grid0.origin(gfx::Point(0, 0));

  if (params().to() == ConvertLayerParam::Tilemap && ctx->isUIAvailable() && params().ui() &&
      // Background or Transparent Layer -> Tilemap
      (srcLayer->isImage() && (srcLayer->isBackground() || srcLayer->isTransparent()))) {
    TilesetSelector::Info tilesetInfo;
    tilesetInfo.allowNewTileset = true;
    tilesetInfo.allowExistentTileset = false;
    tilesetInfo.grid = grid0;

    gen::TilesetSelectorWindow window;
    TilesetSelector tilesetSel(sprite, tilesetInfo);
    window.tilesetOptions()->addChild(&tilesetSel);
    window.openWindowInForeground();
    if (window.closer() != window.ok())
      return;

    // Save "advanced" options
    tilesetSel.saveAdvancedPreferences();

    tilesetInfo = tilesetSel.getInfo();
    tilesetName = tilesetInfo.name;
    grid0 = tilesetInfo.grid;
    baseIndex = tilesetInfo.baseIndex;
    matchFlags = tilesetInfo.matchFlags;
  }

  const DocRange& range = site.range();
  bool hasMultiLayers = (range.enabled() && range.selectedLayers().size() > 1);

  ContextWriter writer(ctx);
  Doc* document(writer.document());

  switch (params().to()) {
    case ConvertLayerParam::Background: {
      Tx tx(writer, friendlyName());
      if (srcLayer->isTransparent()) {
        ASSERT(srcLayer->isImage());
        tx(new cmd::BackgroundFromLayer(static_cast<LayerImage*>(srcLayer)));
      }
      else if (srcLayer->isTilemap()) {
        auto newLayer = new LayerImage(sprite);
        newLayer->configureAsBackground();
        newLayer->setName(Strings::commands_NewFile_BackgroundLayer());
        newLayer->setContinuous(srcLayer->isContinuous());
        newLayer->setUserData(srcLayer->userData());
        tx(new cmd::AddLayer(srcLayer->parent(), newLayer, srcLayer));

        CelList srcCels;
        srcLayer->getCels(srcCels);
        for (Cel* srcCel : srcCels)
          create_cel_copy(tx, srcCel, sprite, newLayer, srcCel->frame());

        tx(new cmd::RemoveLayer(srcLayer));
      }
      tx.commit();
      break;
    }

    case ConvertLayerParam::Layer: {
      Tx tx(writer, friendlyName());
      if (srcLayer->isBackground()) {
        tx(new cmd::LayerFromBackground(srcLayer));
      }
      else if (srcLayer->isTilemap()) {
        auto newLayer = new LayerImage(sprite);
        newLayer->setName(srcLayer->name());
        newLayer->setContinuous(srcLayer->isContinuous());
        newLayer->setBlendMode(static_cast<LayerImage*>(srcLayer)->blendMode());
        newLayer->setOpacity(static_cast<LayerImage*>(srcLayer)->opacity());
        newLayer->setUserData(srcLayer->userData());
        tx(new cmd::AddLayer(srcLayer->parent(), newLayer, srcLayer));

        copyCels(tx, srcLayer, newLayer);

        tx(new cmd::RemoveLayer(srcLayer));
      }
      tx.commit();
      break;
    }

    case ConvertLayerParam::Tilemap: {
      Tx tx(writer, friendlyName());
      if (srcLayer->isImage() && (srcLayer->isBackground() || srcLayer->isTransparent())) {
        auto tileset = new Tileset(sprite, grid0, 1);
        tileset->setName(tilesetName);
        tileset->setBaseIndex(baseIndex);
        tileset->setMatchFlags(matchFlags);

        auto addTileset = new cmd::AddTileset(sprite, tileset);
        tx(addTileset);
        tileset_index tsi = addTileset->tilesetIndex();

        auto newLayer = new LayerTilemap(sprite, tsi);
        newLayer->setName(srcLayer->name());
        newLayer->setContinuous(srcLayer->isContinuous());
        newLayer->setBlendMode(static_cast<LayerImage*>(srcLayer)->blendMode());
        newLayer->setOpacity(static_cast<LayerImage*>(srcLayer)->opacity());
        newLayer->setUserData(srcLayer->userData());
        tx(new cmd::AddLayer(srcLayer->parent(), newLayer, srcLayer));

        copyCels(tx, srcLayer, newLayer);

        tx(new cmd::RemoveLayer(srcLayer));
      }
      tx.commit();
      break;
    }

    case ConvertLayerParam::Group: {
      if (!hasMultiLayers && srcLayer->isSmartObject()) {
        auto smartLayer = static_cast<LayerSmartObject*>(srcLayer);
        Document* srcDoc = smartLayer->linkedDoc();
        if (!srcDoc) {
          smartLayer->reloadSource(false);
          srcDoc = smartLayer->linkedDoc();
        }
        if (!srcDoc || !srcDoc->sprite()) {
          Console(ctx).printf("Cannot convert to group: linked file could not be loaded: %s\n",
                              smartLayer->filepath().c_str());
          return;
        }

        Sprite* srcSprite = srcDoc->sprite();
        Layer* parent = smartLayer->parent();
        auto newGroup = new LayerGroup(sprite);
        newGroup->setName(smartLayer->name());
        newGroup->setFlags(smartLayer->flags() & ~LayerFlags::SmartObject);
        newGroup->setUserData(smartLayer->userData());

        int dx = int(std::round(smartLayer->position().x));
        int dy = int(std::round(smartLayer->position().y));

        {
          Tx tx(writer, friendlyName());
          tx(new cmd::AddLayer(parent, newGroup, smartLayer));

          unpackSourceLayers(tx, srcSprite->root(), newGroup, sprite, dx, dy);

          tx(new cmd::RemoveLayer(smartLayer));
          tx.commit();
        }

        ctx->setActiveLayer(newGroup);
        if (ctx->isUIAvailable()) {
          if (auto editor = Editor::activeEditor())
            editor->setLayer(newGroup);
        }
      }
      else if (hasMultiLayers) {
        LayerList selList = range.selectedLayers().toBrowsableLayerList();
        if (selList.size() > 1) {
          Layer* topLayer = selList.back();
          Layer* parent = topLayer->parent();

          auto newGroup = new LayerGroup(sprite);
          newGroup->setName("Group");

          {
            Tx tx(writer, friendlyName());
            tx(new cmd::AddLayer(parent, newGroup, topLayer));

            for (Layer* child : selList) {
              tx(new cmd::MoveLayer(child, newGroup, newGroup->lastLayer()));
            }
            tx.commit();
          }

          ctx->setActiveLayer(newGroup);
          if (ctx->isUIAvailable()) {
            if (auto editor = Editor::activeEditor())
              editor->setLayer(newGroup);
          }
        }
      }
      break;
    }

    case ConvertLayerParam::SmartObject: {
      LayerList selList;
      if (hasMultiLayers) {
        selList = range.selectedLayers().toBrowsableLayerList();
      }
      else if (srcLayer && !srcLayer->isSmartObject() && !srcLayer->isReference()) {
        selList.push_back(srcLayer);
      }
      if (selList.empty())
        return;

      std::string filename;
      if (params().filename.isSet() && !params().filename().empty()) {
        filename = params().filename();
      }
      else if (params().ui() && ctx->isUIAvailable()) {
        std::string initialDir;
        if (!document->filename().empty()) {
          initialDir = base::get_file_path(document->filename());
          if (!initialDir.empty() && !base::is_path_separator(initialDir.back()))
            initialDir.push_back(base::path_separator);
        }
        std::string initialFilename;
        if (selList.size() == 1)
          initialFilename = initialDir + sanitizeFilename(selList.front()->name()) + ".aseprite";
        else
          initialFilename = initialDir + "SmartObject.aseprite";

        base::paths files;
        if (!app::show_file_selector(
              Strings::commands_ConvertLayer_SmartObject(),
              initialFilename,
              { "ase", "aseprite" },
              FileSelectorType::Save,
              files)) {
          return;
        }
        if (files.empty())
          return;
        filename = files.front();
      }
      else {
        std::string baseName;
        if (selList.size() == 1)
          baseName = sanitizeFilename(selList.front()->name());
        else
          baseName = "smart_object";
        filename = base::join_path(base::get_temp_path(),
                                   baseName + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".aseprite");
      }

      if (base::get_file_extension(filename).empty())
        filename += ".aseprite";

      std::string dir = base::get_file_path(filename);
      if (!dir.empty() && !base::is_directory(dir))
        base::make_all_directories(dir);

      auto newDoc = createDetachedDocFromLayers(document, selList, filename);
      if (save_document(ctx, newDoc.get()) != 0) {
        Console(ctx).printf("Failed to save smart object: %s\n", filename.c_str());
        return;
      }

      Layer* topLayer = selList.back();
      Layer* parent = topLayer->parent();

      std::string smartName;
      if (selList.size() == 1)
        smartName = selList.front()->name();
      else
        smartName = base::get_file_title(filename);

      auto smartLayer = new LayerSmartObject(sprite, filename);
      smartLayer->setName(smartName);
      smartLayer->setPosition(0, 0);
      smartLayer->setSourceSize(newDoc->sprite()->size());
      smartLayer->setLastModifiedTime(LayerSmartObject::fileModificationEpoch(filename));
      smartLayer->setLinkedDoc(newDoc.release());
      smartLayer->syncPropertiesToUserData();

      {
        Tx tx(writer, friendlyName());
        tx(new cmd::AddLayer(parent, smartLayer, topLayer));
        for (Layer* l : selList) {
          tx(new cmd::RemoveLayer(l));
        }
        smartLayer->syncFrames(sprite->totalFrames());
        tx.commit();
      }

      SmartObjectManager::instance()->trackDocument(document);

      ctx->setActiveLayer(smartLayer);
      if (ctx->isUIAvailable()) {
        if (auto editor = Editor::activeEditor())
          editor->setLayer(smartLayer);
      }
      break;
    }

    case ConvertLayerParam::SeparateSmartObjects: {
      LayerList selList = range.selectedLayers().toBrowsableLayerList();
      if (selList.size() < 2)
        return;

      std::string folder;
      if (params().folder.isSet() && !params().folder().empty()) {
        folder = params().folder();
      }
      else if (params().ui() && ctx->isUIAvailable()) {
        std::string initialDir;
        if (!document->filename().empty())
          initialDir = base::get_file_path(document->filename());
        else
          initialDir = base::get_temp_path();

        base::paths files;
        if (app::show_file_selector(
              Strings::commands_ConvertLayer_SeparateSmartObjects(),
              initialDir,
              { "ase", "aseprite" },
              FileSelectorType::Save,
              files) && !files.empty()) {
          folder = base::get_file_path(files.front());
        }
        else {
          return;
        }
      }
      else {
        if (!document->filename().empty())
          folder = base::get_file_path(document->filename());
        else
          folder = base::get_temp_path();
      }

      if (!folder.empty() && !base::is_directory(folder))
        base::make_all_directories(folder);

      struct Entry {
        Layer* originalLayer;
        std::string filepath;
        std::unique_ptr<Doc> detachedDoc;
        LayerSmartObject* smartLayer;
      };

      std::vector<Entry> entries;
      std::set<std::string> usedPaths;

      for (Layer* src : selList) {
        if (src->isSmartObject())
          continue;

        std::string sanitized = sanitizeFilename(src->name());
        std::string targetPath = base::join_path(folder, sanitized + ".aseprite");
        int counter = 2;
        while (usedPaths.count(targetPath) || base::is_file(targetPath)) {
          targetPath = base::join_path(folder, sanitized + "_" + std::to_string(counter++) + ".aseprite");
        }
        usedPaths.insert(targetPath);

        auto detachedDoc = createDetachedDocFromLayers(document, { src }, targetPath);
        if (save_document(ctx, detachedDoc.get()) != 0) {
          Console(ctx).printf("Failed to save separate smart object: %s\n", targetPath.c_str());
          return;
        }

        auto smartLayer = new LayerSmartObject(sprite, targetPath);
        smartLayer->setName(src->name());
        smartLayer->setPosition(0, 0);
        smartLayer->setSourceSize(detachedDoc->sprite()->size());
        smartLayer->setLastModifiedTime(LayerSmartObject::fileModificationEpoch(targetPath));
        smartLayer->setLinkedDoc(detachedDoc.get());
        smartLayer->syncPropertiesToUserData();

        entries.push_back({ src, targetPath, std::move(detachedDoc), smartLayer });
      }

      if (entries.empty())
        return;

      {
        Tx tx(writer, friendlyName());
        for (auto& entry : entries) {
          tx(new cmd::AddLayer(entry.originalLayer->parent(), entry.smartLayer, entry.originalLayer));
          tx(new cmd::RemoveLayer(entry.originalLayer));
          entry.smartLayer->syncFrames(sprite->totalFrames());
          entry.detachedDoc.release();
        }
        tx.commit();
      }

      SmartObjectManager::instance()->trackDocument(document);

      ctx->setActiveLayer(entries.back().smartLayer);
      if (ctx->isUIAvailable()) {
        if (auto editor = Editor::activeEditor())
          editor->setLayer(entries.back().smartLayer);
      }
      break;
    }

    default:
      break;
  }

  update_screen_for_document(document);
}

void ConvertLayerCommand::copyCels(Tx& tx, Layer* srcLayer, Layer* newLayer)
{
  std::map<doc::ObjectId, doc::Cel*> linkedCels;

  CelList srcCels;
  srcLayer->getCels(srcCels);
  for (Cel* srcCel : srcCels) {
    frame_t frame = srcCel->frame();

    // Keep linked cels in the new layer
    Cel* linkedSrcCel = srcCel->link();
    if (linkedSrcCel) {
      auto it = linkedCels.find(linkedSrcCel->id());
      if (it != linkedCels.end()) {
        tx(new cmd::CopyCel(newLayer, linkedSrcCel->frame(), newLayer, frame, true));
        continue;
      }
    }

    Cel* newCel = create_cel_copy(tx, srcCel, srcLayer->sprite(), newLayer, frame);
    tx(new cmd::AddCel(newLayer, newCel));

    linkedCels[srcCel->id()] = newCel;
  }
}

std::string ConvertLayerCommand::onGetFriendlyName() const
{
  switch (params().to()) {
    case ConvertLayerParam::Background:           return Strings::commands_ConvertLayer_Background(); break;
    case ConvertLayerParam::Layer:                return Strings::commands_ConvertLayer_Layer(); break;
    case ConvertLayerParam::Tilemap:              return Strings::commands_ConvertLayer_Tilemap(); break;
    case ConvertLayerParam::SmartObject:          return Strings::commands_ConvertLayer_SmartObject(); break;
    case ConvertLayerParam::Group:                return Strings::commands_ConvertLayer_Group(); break;
    case ConvertLayerParam::SeparateSmartObjects: return Strings::commands_ConvertLayer_SeparateSmartObjects(); break;
    default:                                      return Strings::commands_ConvertLayer();
  }
}

Command* CommandFactory::createConvertLayerCommand()
{
  return new ConvertLayerCommand;
}

} // namespace app
