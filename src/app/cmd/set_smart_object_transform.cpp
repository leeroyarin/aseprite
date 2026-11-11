// Aseprite
// Copyright (C) 2026  Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/cmd/set_smart_object_transform.h"

#include "app/doc.h"
#include "app/doc_event.h"
#include "doc/cel.h"
#include "doc/cel_data.h"
#include "doc/layer_smart_object.h"
#include "doc/sprite.h"

namespace app { namespace cmd {

SetSmartObjectTransform::SetSmartObjectTransform(doc::LayerSmartObject* layer,
                                                 const gfx::PointF& pos,
                                                 const gfx::PointF& scale,
                                                 double angle,
                                                 bool flipH,
                                                 bool flipV,
                                                 const gfx::PointF& pivot)
  : WithLayer(layer)
  , m_oldPos(layer->position())
  , m_oldScale(layer->scale())
  , m_oldAngle(layer->angle())
  , m_oldFlipH(layer->flipH())
  , m_oldFlipV(layer->flipV())
  , m_oldPivot(layer->pivot())
  , m_newPos(pos)
  , m_newScale(scale)
  , m_newAngle(angle)
  , m_newFlipH(flipH)
  , m_newFlipV(flipV)
  , m_newPivot(pivot)
{
}

void SetSmartObjectTransform::onExecute()
{
  apply(m_newPos, m_newScale, m_newAngle, m_newFlipH, m_newFlipV, m_newPivot);
}

void SetSmartObjectTransform::onUndo()
{
  apply(m_oldPos, m_oldScale, m_oldAngle, m_oldFlipH, m_oldFlipV, m_oldPivot);
}

void SetSmartObjectTransform::apply(const gfx::PointF& pos,
                                    const gfx::PointF& scale,
                                    double angle,
                                    bool flipH,
                                    bool flipV,
                                    const gfx::PointF& pivot)
{
  auto sm = static_cast<doc::LayerSmartObject*>(layer());
  if (!sm->linkedDoc() && !sm->filepath().empty()) {
    sm->reloadSource(false);
  }
  sm->setTransform(pos, scale, angle, flipH, flipV, pivot);
  sm->incrementVersion();
  for (doc::Cel* c : sm->cels()) {
    if (c) {
      c->incrementVersion();
      if (c->data())
        c->data()->incrementVersion();
    }
  }
}

void SetSmartObjectTransform::onFireNotifications()
{
  doc::Layer* lay = this->layer();
  Doc* doc = static_cast<Doc*>(lay->sprite()->document());
  DocEvent ev(doc);
  ev.sprite(lay->sprite());
  ev.layer(lay);
  ev.withUserData(lay);
  doc->notify_observers<DocEvent&>(&DocObserver::onUserDataChange, ev);
  doc->notify_observers<DocEvent&>(&DocObserver::onGeneralUpdate, ev);
}

}} // namespace app::cmd
