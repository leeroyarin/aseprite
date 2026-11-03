// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_EASYMETRIC_MODEL_H_INCLUDED
#define APP_COMMANDS_EASYMETRIC_MODEL_H_INCLUDED

#include "doc/color.h"
#include "gfx/rect.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace app { namespace easymetric {

enum FaceId : int {
  kFaceTop = 0,
  kFaceDown = 1,
  kFaceFront = 2,
  kFaceBack = 3,
  kFaceRight = 4,
  kFaceLeft = 5,
  kFaceCount = 6
};

enum RenderStyle : int {
  kRenderTexturedLit = 0,
  kRenderTexturedFlat = 1,
  kRenderColoredFaces = 2,
  kRenderNormalShaded = 3,
  kRenderDepthShaded = 4,
  kRenderWireframeTex = 5
};

struct FaceSpec {
  const char* sliceName;
  const char* displayName;
  int gridCol;
  int gridRow;
  uint8_t r, g, b;
  int defaultYaw;
  int defaultPitch;
};

// 4-Column 2-Row layout matching reference UI:
// Col 0: Top (row 0),    Bottom (row 1)
// Col 1: Front (row 0),  Back (row 1)
// Col 2: Right (row 0),  Left (row 1)
// Col 3: Preview (row 0), Fields (row 1)
constexpr std::array<FaceSpec, kFaceCount> kFaceSpecs = {
  {
   { "TOP", "Top", 0, 0, 168, 85, 247, 45, 60 },
   { "DOWN", "Bottom", 0, 1, 244, 63, 94, 45, -60 },
   { "FRONT", "Front", 1, 0, 0, 229, 255, 0, 0 },
   { "BACK", "Back", 1, 1, 59, 130, 246, 180, 0 },
   { "RIGHT", "Right", 2, 0, 245, 158, 11, -90, 0 },
   { "LEFT", "Left", 2, 1, 16, 185, 129, 90, 0 },
   }
};

inline uint8_t clampByte(float v)
{
  if (v <= 0.0f)
    return 0;
  if (v >= 255.0f)
    return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

inline float pixelLuma(uint32_t c)
{
  if (doc::rgba_geta(c) <= 16)
    return 0.0f;
  return (0.299f * doc::rgba_getr(c) + 0.587f * doc::rgba_getg(c) + 0.114f * doc::rgba_getb(c)) *
         (1.0f / 255.0f);
}

uint32_t starterSurfacePixel(int face, int u, int v, int W, int H);

struct SixSurfaceData {
  int S = 24;
  std::array<int, kFaceCount> w = { 24, 24, 24, 24, 24, 24 };
  std::array<int, kFaceCount> h = { 24, 24, 24, 24, 24, 24 };
  std::array<std::vector<uint32_t>, kFaceCount> pixels;
  std::array<std::vector<uint8_t>, kFaceCount> depthPixels;
  std::array<std::vector<uint8_t>, kFaceCount> baseAlpha;
  std::array<bool, kFaceCount> hasBaseLayer = { false, false, false, false, false, false };
  std::array<gfx::Rect, kFaceCount> bounds;
  std::array<bool, 3> colLinked = { false, false, false };
  bool hasSlices = false;
  bool hasCustomDepthLayer = false;

  void init(int faceSize)
  {
    S = std::max(8, faceSize);
    for (int f = 0; f < kFaceCount; ++f) {
      w[f] = S;
      h[f] = S;
      pixels[f].assign(S * S, 0);
      depthPixels[f].assign(S * S, 128);
      baseAlpha[f].assign(S * S, 0);
    }
    hasBaseLayer.fill(false);
    colLinked.fill(false);
  }

  void setFaceSize(int face, int fw, int fh)
  {
    fw = std::clamp(fw, 4, 256);
    fh = std::clamp(fh, 4, 256);
    w[face] = fw;
    h[face] = fh;
    pixels[face].assign(fw * fh, 0);
    depthPixels[face].assign(fw * fh, 128);
    baseAlpha[face].assign(fw * fh, 0);
    S = std::max(S, std::max(fw, fh));
  }

  void propagateDimensionLink(int srcFace, int newW, int newH)
  {
    newW = std::clamp(newW, 4, 256);
    newH = std::clamp(newH, 4, 256);

    int X = w[kFaceFront];
    int Y = h[kFaceFront];
    int Z = w[kFaceRight];

    switch (srcFace) {
      case kFaceTop:
      case kFaceDown:
        X = newW;
        Z = newH;
        break;
      case kFaceFront:
      case kFaceBack:
        X = newW;
        Y = newH;
        break;
      case kFaceRight:
      case kFaceLeft:
        Z = newW;
        Y = newH;
        break;
    }

    setFaceSize(kFaceTop, X, Z);
    setFaceSize(kFaceDown, X, Z);
    setFaceSize(kFaceFront, X, Y);
    setFaceSize(kFaceBack, X, Y);
    setFaceSize(kFaceRight, Z, Y);
    setFaceSize(kFaceLeft, Z, Y);
  }

  uint32_t sample(int face, int u, int v) const
  {
    const int fw = w[face];
    const int fh = h[face];
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    return pixels[face][v * fw + u];
  }

  float sampleDepthNorm(int face, int u, int v) const
  {
    const int fw = w[face];
    const int fh = h[face];
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    if (hasCustomDepthLayer) {
      return float(depthPixels[face][v * fw + u]) * (1.0f / 255.0f);
    }
    return pixelLuma(pixels[face][v * fw + u]);
  }

  bool hasPixels(int face) const
  {
    if (face < 0 || face >= kFaceCount)
      return false;
    const auto& px = pixels[face];
    for (uint32_t c : px) {
      if (doc::rgba_geta(c) > 16)
        return true;
    }
    return false;
  }

  int symmetricFace(int face) const
  {
    if (hasPixels(face))
      return face;
    switch (face) {
      case kFaceTop:
        if (hasPixels(kFaceDown))
          return kFaceDown;
        break;
      case kFaceDown:
        if (hasPixels(kFaceTop))
          return kFaceTop;
        break;
      case kFaceFront:
        if (hasPixels(kFaceBack))
          return kFaceBack;
        break;
      case kFaceBack:
        if (hasPixels(kFaceFront))
          return kFaceFront;
        break;
      case kFaceRight:
        if (hasPixels(kFaceLeft))
          return kFaceLeft;
        break;
      case kFaceLeft:
        if (hasPixels(kFaceRight))
          return kFaceRight;
        break;
    }
    return face;
  }

  uint32_t sampleSymmetric(int face, int u, int v) const
  {
    const int sf = symmetricFace(face);
    return sample(sf, u, v);
  }

  bool isBaseSolid(int face, int u, int v) const
  {
    if (face < 0 || face >= kFaceCount)
      return false;
    const int fw = w[face];
    const int fh = h[face];
    if (fw <= 0 || fh <= 0 || baseAlpha[face].empty())
      return false;
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    return baseAlpha[face][v * fw + u] > 16;
  }

  bool isFaceSolid(int face, int u, int v) const
  {
    if (hasBaseLayer[face]) {
      return isBaseSolid(face, u, v);
    }
    return doc::rgba_geta(sampleSymmetric(face, u, v)) > 16;
  }
};

}} // namespace app::easymetric

#endif
