// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_EASYMETRIC_RENDER_H_INCLUDED
#define APP_COMMANDS_EASYMETRIC_RENDER_H_INCLUDED

#include "app/commands/easymetric_model.h"
#include "doc/image.h"
#include "doc/image_ref.h"

namespace app { namespace easymetric {

struct Render3DResult {
  doc::ImageRef diffImg;
  doc::ImageRef normImg;
  doc::ImageRef deptImg;
  int outW = 0;
  int outH = 0;
  float cxScreen = 0.0f;
  float cyScreen = 0.0f;
};

Render3DResult compute3DPreviewImages(const SixSurfaceData& data,
                                      int yawDeg,
                                      int pitchDeg,
                                      int pivX,
                                      int pivY,
                                      int pivZ,
                                      int renderStyle = 0,
                                      bool addOutline = true,
                                      bool allowCutouts = true);

}} // namespace app::easymetric

#endif
