// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/commands/easymetric_volume.h"

namespace app { namespace easymetric {

SculptedVoxelVolume buildSculptedVolume(const SixSurfaceData& data, int pivX, int pivY, int pivZ)
{
  SculptedVoxelVolume vol;
  const int dimX = std::max(
    { data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 });
  const int dimY = std::max(
    { data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 });
  const int dimZ = std::max(
    { data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 });

  vol.vx = std::clamp(dimX, 8, 64);
  vol.vy = std::clamp(dimY, 8, 64);
  vol.vz = std::clamp(dimZ, 8, 64);
  vol.solid.assign(vol.vx * vol.vy * vol.vz, 0);

  const bool hasTop = data.hasPixels(kFaceTop);
  const bool hasDown = data.hasPixels(kFaceDown);
  const bool hasFront = data.hasPixels(kFaceFront);
  const bool hasBack = data.hasPixels(kFaceBack);
  const bool hasRight = data.hasPixels(kFaceRight);
  const bool hasLeft = data.hasPixels(kFaceLeft);

  const bool activeTopDown = (hasTop || hasDown);
  const bool activeFrontBack = (hasFront || hasBack);
  const bool activeRightLeft = (hasRight || hasLeft);

  if (!activeTopDown && !activeFrontBack && !activeRightLeft) {
    vol.solid.assign(vol.vx * vol.vy * vol.vz, 1);
    return vol;
  }

  const int pivIndexX = std::clamp(int(pivX * 0.01f * (vol.vx - 1)), 0, vol.vx - 1);
  const int pivIndexY = std::clamp(int(pivY * 0.01f * (vol.vy - 1)), 0, vol.vy - 1);
  const int pivIndexZ = std::clamp(int(pivZ * 0.01f * (vol.vz - 1)), 0, vol.vz - 1);

  const bool onlyTopDown = activeTopDown && !activeFrontBack && !activeRightLeft;
  const bool onlyFrontBack = activeFrontBack && !activeTopDown && !activeRightLeft;
  const bool onlyRightLeft = activeRightLeft && !activeTopDown && !activeFrontBack;

  const int slabYMin = std::max(0, pivIndexY - 1);
  const int slabYMax = std::min(vol.vy - 1, pivIndexY + 1);

  const int slabZMin = std::max(0, pivIndexZ - 1);
  const int slabZMax = std::min(vol.vz - 1, pivIndexZ + 1);

  const int slabXMin = std::max(0, pivIndexX - 1);
  const int slabXMax = std::min(vol.vx - 1, pivIndexX + 1);

  for (int y = 0; y < vol.vy; ++y) {
    if (onlyTopDown && (y < slabYMin || y > slabYMax))
      continue;

    for (int z = 0; z < vol.vz; ++z) {
      if (onlyFrontBack && (z < slabZMin || z > slabZMax))
        continue;

      for (int x = 0; x < vol.vx; ++x) {
        if (onlyRightLeft && (x < slabXMin || x > slabXMax))
          continue;

        bool solid = true;

        if (activeTopDown) {
          const int uTop = std::clamp(int((float(x) + 0.5f) / float(vol.vx) * data.w[kFaceTop]),
                                      0,
                                      data.w[kFaceTop] - 1);
          const int vTop = std::clamp(int((float(z) + 0.5f) / float(vol.vz) * data.h[kFaceTop]),
                                      0,
                                      data.h[kFaceTop] - 1);
          bool topSolid = data.isFaceSolid(kFaceTop, uTop, vTop);

          if (!data.colLinked[0] && hasDown) {
            const int uDown = std::clamp(int((float(x) + 0.5f) / float(vol.vx) * data.w[kFaceDown]),
                                         0,
                                         data.w[kFaceDown] - 1);
            const int vDown = std::clamp(
              int((float(vol.vz - 1 - z) + 0.5f) / float(vol.vz) * data.h[kFaceDown]),
              0,
              data.h[kFaceDown] - 1);
            bool downSolid = data.isFaceSolid(kFaceDown, uDown, vDown);
            topSolid = (hasTop ? (topSolid || downSolid) : downSolid);
          }
          if (!topSolid) {
            solid = false;
          }
        }

        if (solid && activeFrontBack) {
          const int uFront = std::clamp(int((float(x) + 0.5f) / float(vol.vx) * data.w[kFaceFront]),
                                        0,
                                        data.w[kFaceFront] - 1);
          const int vFront = std::clamp(int((float(y) + 0.5f) / float(vol.vy) * data.h[kFaceFront]),
                                        0,
                                        data.h[kFaceFront] - 1);
          bool frontSolid = data.isFaceSolid(kFaceFront, uFront, vFront);

          if (!data.colLinked[1] && hasBack) {
            const int uBack = std::clamp(
              int((float(vol.vx - 1 - x) + 0.5f) / float(vol.vx) * data.w[kFaceBack]),
              0,
              data.w[kFaceBack] - 1);
            const int vBack = std::clamp(int((float(y) + 0.5f) / float(vol.vy) * data.h[kFaceBack]),
                                         0,
                                         data.h[kFaceBack] - 1);
            bool backSolid = data.isFaceSolid(kFaceBack, uBack, vBack);
            frontSolid = (hasFront ? (frontSolid || backSolid) : backSolid);
          }
          if (!frontSolid) {
            solid = false;
          }
        }

        if (solid && activeRightLeft) {
          const int uRight = std::clamp(
            int((float(vol.vz - 1 - z) + 0.5f) / float(vol.vz) * data.w[kFaceRight]),
            0,
            data.w[kFaceRight] - 1);
          const int vRight = std::clamp(int((float(y) + 0.5f) / float(vol.vy) * data.h[kFaceRight]),
                                        0,
                                        data.h[kFaceRight] - 1);
          bool sideSolid = data.isFaceSolid(kFaceRight, uRight, vRight);

          if (!data.colLinked[2] && hasLeft) {
            const int uLeft = std::clamp(int((float(z) + 0.5f) / float(vol.vz) * data.w[kFaceLeft]),
                                         0,
                                         data.w[kFaceLeft] - 1);
            const int vLeft = std::clamp(int((float(y) + 0.5f) / float(vol.vy) * data.h[kFaceLeft]),
                                         0,
                                         data.h[kFaceLeft] - 1);
            bool leftSolid = data.isFaceSolid(kFaceLeft, uLeft, vLeft);
            sideSolid = (hasRight ? (sideSolid || leftSolid) : leftSolid);
          }
          if (!sideSolid) {
            solid = false;
          }
        }

        if (solid) {
          vol.setSolid(x, y, z, true);
        }
      }
    }
  }

  return vol;
}

}} // namespace app::easymetric
