// Aseprite Document Library
// Copyright (c) 2020-2023  Igara Studio S.A.
// Copyright (c) 2001-2018 David Capello
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "doc/algorithm/rotsprite.h"

#include "doc/algorithm/rotate.h"
#include "doc/blend_funcs.h"
#include "doc/image.h"
#include "doc/primitives.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace doc { namespace algorithm {

// Fast scanline EPX/Scale2x implementation using direct row pointers for
// byte-aligned pixel formats, falling back to pixel helpers for bitmaps.
template<typename ImageTraits>
static void image_scale2x_tpl(Image* dst, const Image* src, int src_w, int src_h)
{
  if (src_w <= 0 || src_h <= 0)
    return;

  if constexpr (ImageTraits::pixels_per_byte == 0) {
    using pixel_t = typename ImageTraits::pixel_t;

    for (int y = 0; y < src_h; ++y) {
      const pixel_t* rowP = reinterpret_cast<const pixel_t*>(src->getPixelAddress(0, y));
      const pixel_t* rowA = (y > 0) ? reinterpret_cast<const pixel_t*>(src->getPixelAddress(0, y - 1)) : rowP;
      const pixel_t* rowD =
        (y + 1 < src_h) ? reinterpret_cast<const pixel_t*>(src->getPixelAddress(0, y + 1)) : rowP;

      pixel_t* dstRow0 = reinterpret_cast<pixel_t*>(dst->getPixelAddress(0, y * 2));
      pixel_t* dstRow1 = reinterpret_cast<pixel_t*>(dst->getPixelAddress(0, y * 2 + 1));

      if (src_w == 1) {
        const pixel_t P = rowP[0];
        dstRow0[0] = P;
        dstRow0[1] = P;
        dstRow1[0] = P;
        dstRow1[1] = P;
        continue;
      }

      // Left edge (x = 0, C == P)
      {
        const pixel_t P = rowP[0];
        const pixel_t A = rowA[0];
        const pixel_t B = rowP[1];
        const pixel_t C = P;
        const pixel_t D = rowD[0];
        dstRow0[0] = P;
        dstRow0[1] = (A == B && A != C && B != D ? B : P);
        dstRow1[0] = P;
        dstRow1[1] = (B == D && B != A && D != C ? D : P);
        dstRow0 += 2;
        dstRow1 += 2;
      }

      // Interior pixels (x = 1 .. src_w - 2) without bounds checks
      for (int x = 1; x < src_w - 1; ++x) {
        const pixel_t P = rowP[x];
        const pixel_t A = rowA[x];
        const pixel_t B = rowP[x + 1];
        const pixel_t C = rowP[x - 1];
        const pixel_t D = rowD[x];

        dstRow0[0] = (C == A && C != D && A != B ? A : P);
        dstRow0[1] = (A == B && A != C && B != D ? B : P);
        dstRow1[0] = (D == C && D != B && C != A ? C : P);
        dstRow1[1] = (B == D && B != A && D != C ? D : P);
        dstRow0 += 2;
        dstRow1 += 2;
      }

      // Right edge (x = src_w - 1, B == P)
      {
        const int x = src_w - 1;
        const pixel_t P = rowP[x];
        const pixel_t A = rowA[x];
        const pixel_t B = P;
        const pixel_t C = rowP[x - 1];
        const pixel_t D = rowD[x];
        dstRow0[0] = (C == A && C != D && A != B ? A : P);
        dstRow0[1] = P;
        dstRow1[0] = (D == C && D != B && C != A ? C : P);
        dstRow1[1] = P;
      }
    }
  }
  else {
    LockImageBits<ImageTraits> dstBits(dst, gfx::Rect(0, 0, src_w * 2, src_h * 2));
    auto dstIt = dstBits.begin();
    auto dstIt2 = dstIt;

    for (int y = 0; y < src_h; ++y) {
      dstIt2 += src_w * 2;
      for (int x = 0; x < src_w; ++x) {
        const color_t P = get_pixel_fast<ImageTraits>(src, x, y);
        const color_t A = (y > 0 ? get_pixel_fast<ImageTraits>(src, x, y - 1) : P);
        const color_t B = (x < src_w - 1 ? get_pixel_fast<ImageTraits>(src, x + 1, y) : P);
        const color_t C = (x > 0 ? get_pixel_fast<ImageTraits>(src, x - 1, y) : P);
        const color_t D = (y < src_h - 1 ? get_pixel_fast<ImageTraits>(src, x, y + 1) : P);

        *dstIt = (C == A && C != D && A != B ? A : P);
        ++dstIt;
        *dstIt = (A == B && A != C && B != D ? B : P);
        ++dstIt;

        *dstIt2 = (D == C && D != B && C != A ? C : P);
        ++dstIt2;
        *dstIt2 = (B == D && B != A && D != C ? D : P);
        ++dstIt2;
      }
      dstIt += src_w * 2;
    }
  }
}

static void image_scale2x(Image* dst, const Image* src, int src_w, int src_h)
{
  switch (src->pixelFormat()) {
    case IMAGE_RGB:       image_scale2x_tpl<RgbTraits>(dst, src, src_w, src_h); break;
    case IMAGE_GRAYSCALE: image_scale2x_tpl<GrayscaleTraits>(dst, src, src_w, src_h); break;
    case IMAGE_INDEXED:   image_scale2x_tpl<IndexedTraits>(dst, src, src_w, src_h); break;
    case IMAGE_BITMAP:    image_scale2x_tpl<BitmapTraits>(dst, src, src_w, src_h); break;
  }
}

template<typename PixelT, typename WriteFunc>
static void downsample_block_center_tpl(Image* bmp,
                                        const Image* bmp_copy,
                                        int xmin,
                                        int ymin,
                                        int rot_width,
                                        int rot_height,
                                        int scale,
                                        WriteFunc writePixel)
{
  const int xStart = std::max(0, xmin);
  const int yStart = std::max(0, ymin);
  const int xEnd = std::min(bmp->width(), xmin + rot_width);
  const int yEnd = std::min(bmp->height(), ymin + rot_height);
  if (xStart >= xEnd || yStart >= yEnd)
    return;

  const int half = scale >> 1;
  const int maxSx = bmp_copy->width() - 1;
  const int maxSy = bmp_copy->height() - 1;

  for (int dy = yStart; dy < yEnd; ++dy) {
    const int sy = std::min(maxSy, (dy - ymin) * scale + half);
    const PixelT* srcRow = reinterpret_cast<const PixelT*>(bmp_copy->getPixelAddress(0, sy));
    PixelT* dstRow = reinterpret_cast<PixelT*>(bmp->getPixelAddress(0, dy));

    int sx = (xStart - xmin) * scale + half;
    for (int dx = xStart; dx < xEnd; ++dx, sx += scale) {
      const PixelT c = srcRow[std::min(maxSx, sx)];
      writePixel(dstRow[dx], c);
    }
  }
}

void rotsprite_image(Image* bmp,
                     const Image* spr,
                     const Image* mask,
                     int x1,
                     int y1,
                     int x2,
                     int y2,
                     int x3,
                     int y3,
                     int x4,
                     int y4)
{
  rotsprite_image(bmp,
                  spr,
                  mask,
                  static_cast<double>(x1),
                  static_cast<double>(y1),
                  static_cast<double>(x2),
                  static_cast<double>(y2),
                  static_cast<double>(x3),
                  static_cast<double>(y3),
                  static_cast<double>(x4),
                  static_cast<double>(y4));
}

void rotsprite_image(Image* bmp,
                     const Image* spr,
                     const Image* mask,
                     double x1,
                     double y1,
                     double x2,
                     double y2,
                     double x3,
                     double y3,
                     double x4,
                     double y4)
{
  thread_local ImageBufferPtr buf[3];

  for (int i = 0; i < 3; ++i) {
    if (!buf[i])
      buf[i].reset(new ImageBuffer(1));
  }

  const int xmin = static_cast<int>(std::floor(std::min({ x1, x2, x3, x4 })));
  const int xmax = static_cast<int>(std::ceil(std::max({ x1, x2, x3, x4 })));
  const int ymin = static_cast<int>(std::floor(std::min({ y1, y2, y3, y4 })));
  const int ymax = static_cast<int>(std::ceil(std::max({ y1, y2, y3, y4 })));
  const int rot_width = xmax - xmin;
  const int rot_height = ymax - ymin;

  if (rot_width <= 0 || rot_height <= 0 || spr->width() <= 0 || spr->height() <= 0)
    return;

  // Choose maximum Scale2x passes (up to 8x) while bounding buffer side to <= 4096px
  // to avoid massive allocations / bad_alloc fallbacks on very large or zoomed sprites.
  const int maxDim = std::max({ rot_width, rot_height, spr->width(), spr->height() });
  int passes = 3;
  if (maxDim * 8 > 4096)
    passes = (maxDim * 4 <= 4096) ? 2 : 1;
  const int scale = 1 << passes;

  std::unique_ptr<Image> bmp_copy(
    Image::create(bmp->pixelFormat(), rot_width * scale, rot_height * scale, buf[0]));

  // Zero-copy ping-pong Scale2x directly from spr -> 2x -> 4x -> 8x
  std::unique_ptr<Image> stepImgs[2];
  const Image* curSrc = spr;
  int activeBufIdx = 1;
  int curW = spr->width();
  int curH = spr->height();

  for (int i = 0; i < passes; ++i) {
    const int slot = (i & 1);
    const int nextW = curW * 2;
    const int nextH = curH * 2;
    stepImgs[slot].reset(Image::create(spr->pixelFormat(), nextW, nextH, buf[1 + slot]));
    stepImgs[slot]->setMaskColor(spr->maskColor());
    image_scale2x(stepImgs[slot].get(), curSrc, curW, curH);
    curSrc = stepImgs[slot].get();
    curW = nextW;
    curH = nextH;
    activeBufIdx = 1 + slot;
  }

  ImageBufferPtr msk_buf;
  std::unique_ptr<Image> msk_copy;
  if (mask) {
    // Reuse whichever ping-pong buffer is not holding curSrc when passes == 2,
    // or dedicated buffer when both slots are live or passes != 2.
    ImageBufferPtr* mskBufPtr = &msk_buf;
    if (passes < 3) {
      const int freeIdx = (activeBufIdx == 1) ? 2 : 1;
      stepImgs[(freeIdx - 1)].reset();
      mskBufPtr = &buf[freeIdx];
    }
    else {
      msk_buf.reset(new ImageBuffer(1));
    }
    msk_copy.reset(
      Image::create(IMAGE_BITMAP, mask->width() * scale, mask->height() * scale, *mskBufPtr));
    clear_image(msk_copy.get(), 0);
    scale_image(msk_copy.get(),
                mask,
                0,
                0,
                msk_copy->width(),
                msk_copy->height(),
                0,
                0,
                mask->width(),
                mask->height());
  }

  const color_t maskColor = spr->maskColor();
  bmp_copy->setMaskColor(maskColor);
  clear_image(bmp_copy.get(), maskColor);

  const int sx1 = static_cast<int>(std::round((x1 - xmin) * scale));
  const int sy1 = static_cast<int>(std::round((y1 - ymin) * scale));
  const int sx2 = static_cast<int>(std::round((x2 - xmin) * scale));
  const int sy2 = static_cast<int>(std::round((y2 - ymin) * scale));
  const int sx4 = static_cast<int>(std::round((x4 - xmin) * scale));
  const int sy4 = static_cast<int>(std::round((y4 - ymin) * scale));
  const int sx3 = sx2 + sx4 - sx1;
  const int sy3 = sy2 + sy4 - sy1;

  parallelogram(bmp_copy.get(),
                curSrc,
                msk_copy.get(),
                sx1,
                sy1,
                sx2,
                sy2,
                sx3,
                sy3,
                sx4,
                sy4);

  switch (bmp->pixelFormat()) {
    case IMAGE_RGB:
      downsample_block_center_tpl<uint32_t>(
        bmp,
        bmp_copy.get(),
        xmin,
        ymin,
        rot_width,
        rot_height,
        scale,
        [maskColor](uint32_t& dst, uint32_t c) {
          if (rgba_geta(c) != 0 &&
              (rgba_geta(maskColor) == 0 || (c & rgba_rgb_mask) != (maskColor & rgba_rgb_mask))) {
            dst = (dst == maskColor) ? c : rgba_blender_normal(dst, c);
          }
        });
      break;

    case IMAGE_GRAYSCALE:
      downsample_block_center_tpl<uint16_t>(
        bmp,
        bmp_copy.get(),
        xmin,
        ymin,
        rot_width,
        rot_height,
        scale,
        [maskColor](uint16_t& dst, uint16_t c) {
          if (graya_geta(c) != 0 &&
              (graya_geta(maskColor) == 0 || (c & graya_v_mask) != (maskColor & graya_v_mask))) {
            dst = (dst == maskColor) ? c : graya_blender_normal(dst, c);
          }
        });
      break;

    case IMAGE_INDEXED:
      downsample_block_center_tpl<uint8_t>(
        bmp,
        bmp_copy.get(),
        xmin,
        ymin,
        rot_width,
        rot_height,
        scale,
        [maskColor](uint8_t& dst, uint8_t c) {
          if (c != maskColor)
            dst = c;
        });
      break;

    case IMAGE_BITMAP:
      scale_image(bmp,
                  bmp_copy.get(),
                  std::max(0, xmin),
                  std::max(0, ymin),
                  std::clamp(rot_width, 0, std::max(0, bmp->width() - std::max(0, xmin))),
                  std::clamp(rot_height, 0, std::max(0, bmp->height() - std::max(0, ymin))),
                  0,
                  0,
                  bmp_copy->width(),
                  bmp_copy->height());
      break;
  }
}

}} // namespace doc::algorithm
