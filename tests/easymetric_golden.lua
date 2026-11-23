-- =============================================================================
-- Easymetric render goldens: prints one line per case with a hash of the baked
-- Iso_Diffuse / Iso_Depth images and of each face's uv. Used to prove that
-- refactors (objects, composite preview) leave single-object output unchanged.
--   build\bin\aseprite.exe -b --script tests\easymetric_golden.lua
-- Compare with tests/easymetric_golden.txt (python tests/check_golden.py).
-- =============================================================================
local function layer(spr, name) for _, l in ipairs(spr.layers) do if l.name == name then return l end end end

local function imgHash(img)
  local h = 2166136261
  for it in img:pixels() do
    h = ((h ~ it()) * 16777619) & 0xffffffff
  end
  return string.format("%08x", h)
end

local function stateFaces(spr)
  app.activeSprite = spr
  local tmp = app.fs.joinPath(app.fs.tempPath, "easymetric_golden_state.json")
  app.command.EasymetricStudio{ action = "state", out = tmp }
  local f = io.open(tmp, "r"); local st = json.decode(f:read("a")); f:close(); os.remove(tmp)
  local parts = {}
  for _, n in ipairs({ "TOP", "DOWN", "FRONT", "BACK", "RIGHT", "LEFT" }) do
    local uv = st.faces[n].uv
    parts[#parts + 1] = n .. "=" .. table.concat(uv, ",")
  end
  return table.concat(parts, " ")
end

local function bake(spr, yaw, pitch)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "bake3D", yaw = yaw, pitch = pitch }
  return imgHash(layer(spr, "Iso_Diffuse"):cel(1).image) .. "/" .. imgHash(layer(spr, "Iso_Depth"):cel(1).image)
end

local function report(name, spr)
  print(string.format("GOLDEN %s faces[%s] bake45=%s bake0=%s bake180=%s", name, stateFaces(spr),
    bake(spr, 45, 30), bake(spr, 0, 0), bake(spr, 180, 0)))
end

-- Case 1: default setup, a painted cross on Front and a block on Top.
do
  local spr = Sprite(72, 60)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "setupSurfaces" }
  local cel = layer(spr, "Surface_Base"):cel(1)
  local img = Image(spr.width, spr.height, spr.colorMode)
  img:drawImage(cel.image, cel.position)
  local c1, c2 = Color{ r = 200, g = 60, b = 40 }, Color{ r = 40, g = 90, b = 200 }
  for v = 5, 24 do for u = 8, 15 do img:drawPixel(24 + u, v, c1) end end
  for v = 12, 16 do for u = 2, 21 do img:drawPixel(24 + u, v, c2) end end
  for v = 4, 19 do for u = 4, 19 do img:drawPixel(u, v, c1) end end
  cel.image = img
  cel.position = Point(0, 0)
  report("cross", spr)
  spr:close()
end

-- Case 2: the block layout from block_layout_test (22x21x22 in 64x128x64 cells).
do
  local spr = Sprite(192, 256)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "setupSurfaces" }
  app.command.EasymetricStudio{ action = "blockLayout", width = 22, height = 21, depth = 22,
                                cellW = 64, cellH = 128, cellD = 64 }
  report("block", spr)
  spr:close()
end

-- Case 3: depth sculpting (Surface_DEPTH present).
do
  local spr = Sprite(72, 60)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "setupSurfaces" }
  app.command.EasymetricStudio{ action = "blockLayout", width = 12, height = 14, depth = 10 }
  app.command.EasymetricStudio{ action = "generateDepthFromBase" }
  app.command.EasymetricStudio{ action = "depthDab", face = "front", u = 6, v = 7, radius = 2, mode = "plus" }
  report("sculpt", spr)
  spr:close()
end
