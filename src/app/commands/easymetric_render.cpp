// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/commands/easymetric_render.h"
#include "app/commands/easymetric_volume.h"

#include <algorithm>
#include <cmath>

namespace app { namespace easymetric {

Render3DResult compute3DPreviewImages(const SixSurfaceData& data,
                                      int yawDeg,
                                      int pitchDeg,
                                      int pivX,
                                      int pivY,
                                      int pivZ,
                                      int renderStyle,
                                      bool addOutline,
                                      bool allowCutouts)
{
  Render3DResult res;
  const float halfX =
    std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown] }) * 0.5f;
  const float halfY =
    std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight] }) *
    0.5f;
  const float halfZ =
    std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown] }) * 0.5f;
  const int maxDim = std::max({ int(halfX * 2.0f), int(halfY * 2.0f), int(halfZ * 2.0f), 24 });
  const int outW = maxDim * 2 + 16;
  const int outH = maxDim * 2 + 16;

  res.outW = outW;
  res.outH = outH;

  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
  const float yaw = yawDeg * kDegToRad;
  const float pitch = pitchDeg * kDegToRad;
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);

  const float rxX = cy, rxY = 0.0f, rzX = -sy;
  const float ryX = sy * sp, ryY = cp, ryZ = cy * sp;
  const float vdX = sy * cp, vdY = -sp, vdZ = cy * cp;

  const float pivOffX = ((pivX - 50) * 0.01f) * (halfX * 2.0f);
  const float pivOffY = ((pivY - 50) * 0.01f) * (halfY * 2.0f);
  const float pivOffZ = ((pivZ - 50) * 0.01f) * (halfZ * 2.0f);

  const float cxScreen = outW * 0.5f;
  const float cyScreen = outH * 0.5f;
  res.cxScreen = cxScreen;
  res.cyScreen = cyScreen;

  res.diffImg.reset(doc::Image::create(doc::IMAGE_RGB, outW, outH));
  res.normImg.reset(doc::Image::create(doc::IMAGE_RGB, outW, outH));
  res.deptImg.reset(doc::Image::create(doc::IMAGE_RGB, outW, outH));
  res.diffImg->clear(0);
  res.normImg->clear(0);
  res.deptImg->clear(0);

  SculptedVoxelVolume vol = buildSculptedVolume(data, pivX, pivY, pivZ);

  std::vector<int> faceMap(outW * outH, -1);
  const float maxDepthRange = float(maxDim) * 1.732f;
  const float Lx = -0.45f, Ly = -0.60f, Lz = 0.66f;

  const float camDist = maxDepthRange * 2.5f;
  const float nearPlane = 1.0f;
  const float farPlane = camDist * 2.0f + maxDepthRange;

  for (int py = 0; py < outH; ++py) {
    const float camY = (py - cyScreen);
    uint32_t* __restrict diffRow = reinterpret_cast<uint32_t*>(res.diffImg->getPixelAddress(0, py));
    uint32_t* __restrict normRow = reinterpret_cast<uint32_t*>(res.normImg->getPixelAddress(0, py));
    uint32_t* __restrict deptRow = reinterpret_cast<uint32_t*>(res.deptImg->getPixelAddress(0, py));

    for (int px = 0; px < outW; ++px) {
      const float camX = (px - cxScreen);
      const float ox = (camX * rxX + camY * ryX + pivOffX) - camDist * vdX;
      const float oy = (camX * rxY + camY * ryY + pivOffY) - camDist * vdY;
      const float oz = (camX * rzX + camY * ryZ + pivOffZ) - camDist * vdZ;

      float t0 = -1e9f, t1 = 1e9f;
      auto clipAxis = [&](float orig, float dir, float bMin, float bMax) -> bool {
        if (std::fabs(dir) > 1e-5f) {
          float invD = 1.0f / dir;
          float ta = (bMin - orig) * invD;
          float tb = (bMax - orig) * invD;
          if (ta > tb)
            std::swap(ta, tb);
          if (ta > t0)
            t0 = ta;
          if (tb < t1)
            t1 = tb;
          return t0 <= t1;
        }
        return (orig >= bMin && orig <= bMax);
      };

      if (!clipAxis(ox, vdX, -halfX, halfX) || !clipAxis(oy, vdY, -halfY, halfY) ||
          !clipAxis(oz, vdZ, -halfZ, halfZ) || t1 < nearPlane || t0 > farPlane || t0 > t1) {
        continue;
      }

      const float stepSize = std::min({ (2.0f * halfX) / float(vol.vx),
                                        (2.0f * halfY) / float(vol.vy),
                                        (2.0f * halfZ) / float(vol.vz) }) *
                             0.45f;
      float tStart = std::max(nearPlane, t0);
      float tEnd = std::min(farPlane, t1);
      float tHit = -1.0f;
      int hitVx = -1, hitVy = -1, hitVz = -1;

      for (float t = tStart; t <= tEnd; t += stepSize) {
        const float pxW = ox + t * vdX;
        const float pyW = oy + t * vdY;
        const float pzW = oz + t * vdZ;

        int vx = int((pxW + halfX) / (2.0f * halfX) * float(vol.vx));
        int vy = int((pyW + halfY) / (2.0f * halfY) * float(vol.vy));
        int vz = int((pzW + halfZ) / (2.0f * halfZ) * float(vol.vz));

        if (vol.isSolid(vx, vy, vz)) {
          float ta = t - stepSize;
          float tb = t;
          for (int b = 0; b < 3; ++b) {
            float tm = (ta + tb) * 0.5f;
            int mx = int((ox + tm * vdX + halfX) / (2.0f * halfX) * float(vol.vx));
            int my = int((oy + tm * vdY + halfY) / (2.0f * halfY) * float(vol.vy));
            int mz = int((oz + tm * vdZ + halfZ) / (2.0f * halfZ) * float(vol.vz));
            if (vol.isSolid(mx, my, mz)) {
              tb = tm;
              vx = mx;
              vy = my;
              vz = mz;
            }
            else {
              ta = tm;
            }
          }
          tHit = tb;
          hitVx = vx;
          hitVy = vy;
          hitVz = vz;
          break;
        }
      }

      if (tHit < 0.0f)
        continue;

      float gX = float(vol.isSolid(hitVx - 1, hitVy, hitVz)) -
                 float(vol.isSolid(hitVx + 1, hitVy, hitVz));
      float gY = float(vol.isSolid(hitVx, hitVy - 1, hitVz)) -
                 float(vol.isSolid(hitVx, hitVy + 1, hitVz));
      float gZ = float(vol.isSolid(hitVx, hitVy, hitVz - 1)) -
                 float(vol.isSolid(hitVx, hitVy, hitVz + 1));
      float len = std::sqrt(gX * gX + gY * gY + gZ * gZ);
      if (len > 1e-4f) {
        gX /= len;
        gY /= len;
        gZ /= len;
      }
      else {
        gX = (ox + tHit * vdX) / halfX;
        gY = (oy + tHit * vdY) / halfY;
        gZ = (oz + tHit * vdZ) / halfZ;
        len = std::sqrt(gX * gX + gY * gY + gZ * gZ);
        if (len > 1e-4f) {
          gX /= len;
          gY /= len;
          gZ /= len;
        }
        else {
          gX = 0.0f;
          gY = 0.0f;
          gZ = 1.0f;
        }
      }

      int face = kFaceFront;
      if (std::fabs(gZ) >= std::fabs(gX) && std::fabs(gZ) >= std::fabs(gY)) {
        face = (gZ > 0.0f ? kFaceFront : kFaceBack);
      }
      else if (std::fabs(gY) >= std::fabs(gX) && std::fabs(gY) >= std::fabs(gZ)) {
        face = (gY < 0.0f ? kFaceTop : kFaceDown);
      }
      else {
        face = (gX > 0.0f ? kFaceRight : kFaceLeft);
      }

      const float hx = ox + tHit * vdX;
      const float hy = oy + tHit * vdY;
      const float hz = oz + tHit * vdZ;

      const int fw = data.w[face];
      const int fh = data.h[face];
      int u = 0, v = 0;
      switch (face) {
        case kFaceFront:
          u = std::clamp(int((hx + halfX) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceBack:
          u = std::clamp(int((halfX - hx) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceLeft:
          u = std::clamp(int((hz + halfZ) / (2.0f * halfZ) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceRight:
          u = std::clamp(int((halfZ - hz) / (2.0f * halfZ) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceTop:
          u = std::clamp(int((hx + halfX) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((hz + halfZ) / (2.0f * halfZ) * fh), 0, fh - 1);
          break;
        case kFaceDown:
          u = std::clamp(int((hx + halfX) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((halfZ - hz) / (2.0f * halfZ) * fh), 0, fh - 1);
          break;
      }

      const uint32_t texCol = data.sampleSymmetric(face, u, v);
      const uint8_t a = doc::rgba_geta(texCol);
      if (a <= 16 && renderStyle != kRenderColoredFaces) {
        if (allowCutouts)
          continue;
      }

      const float dC = data.sampleDepthNorm(face, u, v);
      const float dL = data.sampleDepthNorm(face, u - 1, v);
      const float dR = data.sampleDepthNorm(face, u + 1, v);
      const float dU = data.sampleDepthNorm(face, u, v - 1);
      const float dD = data.sampleDepthNorm(face, u, v + 1);
      const float gradScale = data.hasCustomDepthLayer ? 1.45f : 0.65f;
      const float du = (dL - dR) * gradScale;
      const float dv = (dD - dU) * gradScale;

      float cnx = gX * rxX + gY * ryX + gZ * rzX + du;
      float cny = -(gX * ryX + gY * ryY + gZ * ryZ) + dv;
      float cnz = std::fabs(gX * vdX + gY * vdY + gZ * vdZ) + 0.15f;
      const float nLen = std::sqrt(cnx * cnx + cny * cny + cnz * cnz);
      if (nLen > 1e-4f) {
        cnx /= nLen;
        cny /= nLen;
        cnz /= nLen;
      }

      const uint8_t nr = clampByte((cnx * 0.5f + 0.5f) * 255.0f);
      const uint8_t ng = clampByte((cny * 0.5f + 0.5f) * 255.0f);
      const uint8_t nb = clampByte((cnz * 0.5f + 0.5f) * 255.0f);
      const uint32_t normPixel = doc::rgba(nr, ng, nb, 255);
      normRow[px] = normPixel;

      const float tRel = tHit - camDist;
      const float normZ = std::clamp((tRel + maxDepthRange * 0.5f) / maxDepthRange, 0.0f, 1.0f);
      const float faceRelief = (dC - 0.5f) * (data.hasCustomDepthLayer ? 0.35f : 0.14f);
      const uint8_t dByte = clampByte(35.0f + std::clamp(normZ + faceRelief, 0.0f, 1.0f) * 215.0f);
      const uint32_t deptPixel = doc::rgba(dByte, dByte, dByte, 255);
      deptRow[px] = deptPixel;

      const float ndotl = std::max(0.0f, cnx * Lx + (-cny) * Ly + cnz * Lz);
      const float shade = std::clamp(0.84f + ndotl * 0.16f, 0.84f, 1.0f);

      switch (renderStyle) {
        case kRenderTexturedFlat:
          diffRow[px] =
            doc::rgba(doc::rgba_getr(texCol), doc::rgba_getg(texCol), doc::rgba_getb(texCol), 255);
          break;
        case kRenderColoredFaces: {
          const auto& spec = kFaceSpecs[face];
          const float fShade = std::clamp(0.75f + ndotl * 0.25f, 0.75f, 1.0f);
          diffRow[px] = doc::rgba(clampByte(spec.r * fShade),
                                  clampByte(spec.g * fShade),
                                  clampByte(spec.b * fShade),
                                  255);
          break;
        }
        case kRenderNormalShaded: diffRow[px] = normPixel; break;
        case kRenderDepthShaded:  diffRow[px] = deptPixel; break;
        case kRenderWireframeTex: {
          bool isVoxelEdge =
            (hitVx == 0 || hitVy == 0 || hitVz == 0 || hitVx == vol.vx - 1 || hitVy == vol.vy - 1 ||
             hitVz == vol.vz - 1 || !vol.isSolid(hitVx - 1, hitVy, hitVz) ||
             !vol.isSolid(hitVx + 1, hitVy, hitVz) || !vol.isSolid(hitVx, hitVy - 1, hitVz) ||
             !vol.isSolid(hitVx, hitVy + 1, hitVz) || !vol.isSolid(hitVx, hitVy, hitVz - 1) ||
             !vol.isSolid(hitVx, hitVy, hitVz + 1));
          if (isVoxelEdge) {
            diffRow[px] = doc::rgba(14, 165, 233, 255);
          }
          else {
            diffRow[px] = doc::rgba(clampByte(doc::rgba_getr(texCol) * shade * 0.75f),
                                    clampByte(doc::rgba_getg(texCol) * shade * 0.75f),
                                    clampByte(doc::rgba_getb(texCol) * shade * 0.75f),
                                    255);
          }
          break;
        }
        case kRenderTexturedLit:
        default:
          diffRow[px] = doc::rgba(clampByte(doc::rgba_getr(texCol) * shade),
                                  clampByte(doc::rgba_getg(texCol) * shade),
                                  clampByte(doc::rgba_getb(texCol) * shade),
                                  255);
          break;
      }
      faceMap[py * outW + px] = face;
    }
  }

  if (addOutline) {
    const uint32_t outlineCol = doc::rgba(15, 23, 42, 255);
    const uint32_t outNormCol = doc::rgba(128, 128, 255, 255);
    const uint32_t outDeptCol = doc::rgba(28, 28, 28, 255);

    for (int y = 1; y < outH - 1; ++y) {
      uint32_t* __restrict diffRow = reinterpret_cast<uint32_t*>(
        res.diffImg->getPixelAddress(0, y));
      uint32_t* __restrict normRow = reinterpret_cast<uint32_t*>(
        res.normImg->getPixelAddress(0, y));
      uint32_t* __restrict deptRow = reinterpret_cast<uint32_t*>(
        res.deptImg->getPixelAddress(0, y));

      for (int x = 1; x < outW - 1; ++x) {
        const int idx = y * outW + x;
        const int f = faceMap[idx];
        if (f < 0) {
          if (faceMap[idx - 1] >= 0 || faceMap[idx + 1] >= 0 || faceMap[idx - outW] >= 0 ||
              faceMap[idx + outW] >= 0) {
            diffRow[x] = outlineCol;
            normRow[x] = outNormCol;
            deptRow[x] = outDeptCol;
          }
        }
        else if (f == kFaceTop && (faceMap[idx + outW] >= 0 && faceMap[idx + outW] != kFaceTop)) {
          const uint32_t cur = diffRow[x];
          diffRow[x] = doc::rgba(clampByte(doc::rgba_getr(cur) * 0.68f + 253.0f * 0.32f),
                                 clampByte(doc::rgba_getg(cur) * 0.68f + 230.0f * 0.32f),
                                 clampByte(doc::rgba_getb(cur) * 0.68f + 138.0f * 0.32f),
                                 255);
        }
      }
    }
  }

  return res;
}

}} // namespace app::easymetric
