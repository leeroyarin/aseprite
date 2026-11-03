// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_EASYMETRIC_VOLUME_H_INCLUDED
#define APP_COMMANDS_EASYMETRIC_VOLUME_H_INCLUDED

#include "app/commands/easymetric_model.h"

namespace app { namespace easymetric {

struct SculptedVoxelVolume {
  int vx = 24;
  int vy = 24;
  int vz = 24;
  std::vector<uint8_t> solid;

  bool isSolid(int x, int y, int z) const
  {
    if (x < 0 || x >= vx || y < 0 || y >= vy || z < 0 || z >= vz)
      return false;
    return solid[(y * vz + z) * vx + x] != 0;
  }

  void setSolid(int x, int y, int z, bool val)
  {
    if (x >= 0 && x < vx && y >= 0 && y < vy && z >= 0 && z < vz)
      solid[(y * vz + z) * vx + x] = (val ? 1 : 0);
  }
};

SculptedVoxelVolume buildSculptedVolume(const SixSurfaceData& data, int pivX, int pivY, int pivZ);

}} // namespace app::easymetric

#endif
