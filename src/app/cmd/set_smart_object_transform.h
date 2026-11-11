// Aseprite
// Copyright (C) 2026  Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_CMD_SET_SMART_OBJECT_TRANSFORM_H_INCLUDED
#define APP_CMD_SET_SMART_OBJECT_TRANSFORM_H_INCLUDED
#pragma once

#include "app/cmd.h"
#include "app/cmd/with_layer.h"
#include "gfx/point.h"

namespace doc {
class LayerSmartObject;
}

namespace app { namespace cmd {

class SetSmartObjectTransform : public Cmd,
                                public WithLayer {
public:
  SetSmartObjectTransform(doc::LayerSmartObject* layer,
                          const gfx::PointF& pos,
                          const gfx::PointF& scale,
                          double angle,
                          bool flipH,
                          bool flipV,
                          const gfx::PointF& pivot);

protected:
  void onExecute() override;
  void onUndo() override;
  void onFireNotifications() override;
  size_t onMemSize() const override { return sizeof(*this); }

private:
  void apply(const gfx::PointF& pos,
             const gfx::PointF& scale,
             double angle,
             bool flipH,
             bool flipV,
             const gfx::PointF& pivot);

  gfx::PointF m_oldPos;
  gfx::PointF m_oldScale;
  double m_oldAngle;
  bool m_oldFlipH;
  bool m_oldFlipV;
  gfx::PointF m_oldPivot;

  gfx::PointF m_newPos;
  gfx::PointF m_newScale;
  double m_newAngle;
  bool m_newFlipH;
  bool m_newFlipV;
  gfx::PointF m_newPivot;
};

}} // namespace app::cmd

#endif
