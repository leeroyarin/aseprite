// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/commands/easymetric_model.h"

#include <cmath>

namespace app { namespace easymetric {

uint32_t starterSurfacePixel(int face, int u, int v, int W, int H)
{
  const bool isBorder = (u == 0 || v == 0 || u == W - 1 || v == H - 1);
  const bool isInnerBorder = (u == 1 || v == 1 || u == W - 2 || v == H - 2);
  const bool isCorner = ((u <= 2 && v <= 2) || (u >= W - 3 && v <= 2) || (u <= 2 && v >= H - 3) ||
                         (u >= W - 3 && v >= H - 3));
  if (isCorner)
    return doc::rgba(251, 191, 36, 255);
  if (isBorder)
    return doc::rgba(30, 41, 59, 255);
  if (isInnerBorder) {
    const auto& spec = kFaceSpecs[face];
    return doc::rgba(spec.r / 2 + 30, spec.g / 2 + 30, spec.b / 2 + 30, 255);
  }

  const float cx = (W - 1) * 0.5f;
  const float cy = (H - 1) * 0.5f;
  const float dx = u - cx;
  const float dy = v - cy;
  const float dist = std::sqrt(dx * dx + dy * dy);
  const int minDim = std::min(W, H);

  switch (face) {
    case kFaceFront: {
      if (v == 3 && u >= 4 && u <= W - 5)
        return doc::rgba(0, 229, 255, 255);
      if (dist <= 2.3f)
        return doc::rgba(148, 163, 184, 255);
      if (dist <= minDim * 0.30f)
        return doc::rgba(15, 23, 42, 255);
      if (dist <= minDim * 0.36f)
        return doc::rgba(71, 85, 105, 255);
      return (((u + v) & 1) == 0) ? doc::rgba(30, 41, 59, 255) : doc::rgba(51, 65, 85, 255);
    }
    case kFaceBack: {
      if (v >= 4 && v <= 8 && (v & 1) == 0 && u >= 5 && u <= W - 6)
        return doc::rgba(15, 23, 42, 255);
      if (dist <= 2.5f)
        return doc::rgba(239, 68, 68, 255);
      return doc::rgba(45, 55, 72, 255);
    }
    case kFaceLeft:
    case kFaceRight: {
      if (v >= H / 2 - 2 && v <= H / 2 + 2 && u >= 5 && u <= W - 6)
        return doc::rgba(28, 25, 23, 255);
      return (((u + v) % 3) == 0) ? doc::rgba(93, 64, 55, 255) : doc::rgba(78, 52, 46, 255);
    }
    case kFaceTop: {
      if (v >= H / 2 - 1 && v <= H / 2 + 1 && u >= 4 && u <= W - 5)
        return doc::rgba(15, 23, 42, 255);
      if (v >= 3 && v <= 4 && u >= 4 && u <= W - 5 && (u % 3 == 0))
        return doc::rgba(253, 224, 71, 255);
      return doc::rgba(109, 76, 65, 255);
    }
    case kFaceDown:
    default:        {
      if ((u >= 3 && u <= 5 && v >= 3 && v <= 5) ||
          (u >= W - 6 && u <= W - 4 && v >= 3 && v <= 5) ||
          (u >= 3 && u <= 5 && v >= H - 6 && v <= H - 4) ||
          (u >= W - 6 && u <= W - 4 && v >= H - 6 && v <= H - 4)) {
        return doc::rgba(15, 23, 42, 255);
      }
      return doc::rgba(51, 65, 85, 255);
    }
  }
}

}} // namespace app::easymetric
