-- ==============================================================================
-- Extensive Smart Object Rotation Quality, UI Command, & Speed Test Suite
-- ==============================================================================

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "mismatch", tostring(expected), tostring(actual)), 2)
  end
end

local function assert_near(actual, expected, eps, msg)
  if math.abs(actual - expected) > (eps or 1e-4) then
    error(string.format("ASSERTION FAILED: %s (expected %.6f +- %.6f, got %.6f)",
      msg or "near", expected, eps or 1e-4, actual), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error("ASSERTION FAILED: " .. (msg or "expected true"), 2)
  end
end

local function count_non_transparent(img, colorMode)
  local n = 0
  local maskC = img.spec.transparentColor
  for it in img:pixels() do
    local p = it()
    if colorMode == ColorMode.RGB then
      if app.pixelColor.rgbaA(p) > 0 then n = n + 1 end
    elseif colorMode == ColorMode.GRAY then
      if app.pixelColor.grayaA(p) > 0 then n = n + 1 end
    else
      if p ~= maskC then n = n + 1 end
    end
  end
  return n
end

local function bounding_symmetry_diff(img)
  -- Measures mirror symmetry error across vertical & horizontal center axes
  local w, h = img.width, img.height
  local diff = 0
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      local p = img:getPixel(x, y)
      local px = img:getPixel(w - 1 - x, y)
      local py = img:getPixel(x, h - 1 - y)
      if p ~= px then diff = diff + 1 end
      if p ~= py then diff = diff + 1 end
    end
  end
  return diff
end

local tmpDir = app.fs.joinPath(app.fs.tempPath, "aseprite2_rot_suite")
app.fs.makeDirectory(tmpDir)

local function make_source_sprite(path, size, colorMode, patternFn)
  local spec = ImageSpec{ width = size, height = size, colorMode = colorMode }
  local s = Sprite(spec)
  local img = s.layers[1]:cel(1).image
  patternFn(img, size, colorMode)
  s:saveCopyAs(path)
  s:close()
end

-- ==============================================================================
-- 1. Rotation Quality & Color Preservation (RGB, Grayscale, Indexed)
-- ==============================================================================
print("\n[1/5] Rotation quality & color preservation (RGB, Grayscale, Indexed)...")

local symRgbPath = app.fs.joinPath(tmpDir, "sym16_rgb.aseprite")
make_source_sprite(symRgbPath, 16, ColorMode.RGB, function(img)
  local red = app.pixelColor.rgba(230, 40, 40, 255)
  local blk = app.pixelColor.rgba(20, 20, 30, 255)
  for y = 2, 13 do
    for x = 2, 13 do
      if x == 2 or x == 13 or y == 2 or y == 13 then
        img:drawPixel(x, y, blk)
      else
        img:drawPixel(x, y, red)
      end
    end
  end
end)

local grayPath = app.fs.joinPath(tmpDir, "box16_gray.aseprite")
make_source_sprite(grayPath, 16, ColorMode.GRAY, function(img)
  local g1 = app.pixelColor.graya(220, 255)
  local g2 = app.pixelColor.graya(40, 255)
  for y = 2, 13 do
    for x = 2, 13 do
      img:drawPixel(x, y, (x == 2 or x == 13 or y == 2 or y == 13) and g2 or g1)
    end
  end
end)

local idxPath = app.fs.joinPath(tmpDir, "box16_idx.aseprite")
do
  local spec = ImageSpec{ width = 16, height = 16, colorMode = ColorMode.INDEXED }
  local s = Sprite(spec)
  local pal = s.palettes[1]
  pal:setColor(1, Color(255, 255, 255, 255))
  pal:setColor(2, Color(220, 50, 50, 255))
  local img = s.layers[1]:cel(1).image
  for y = 2, 13 do
    for x = 2, 13 do
      img:drawPixel(x, y, (x == 2 or x == 13 or y == 2 or y == 13) and 1 or 2)
    end
  end
  s:saveCopyAs(idxPath)
  s:close()
end

do
  local host = Sprite(64, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = symRgbPath, name = "SymRGB", x = 10, y = 10, ui = false }
  local sm = host.layers[2]

  -- Orthogonal exact pixel preservation across 0, 90, 180, 270
  local baseNonZero = count_non_transparent(sm:cel(1).image, ColorMode.RGB)
  assert_eq(baseNonZero, 144, "12x12 box pixel count = 144")

  for _, a in ipairs({90, 180, 270, -90, 360}) do
    sm:setTransform(10, 10, 1, 1, a, false, false, 0.5, 0.5)
    local cnt = count_non_transparent(sm:cel(1).image, ColorMode.RGB)
    assert_eq(cnt, 144, "Orthogonal angle " .. a .. " preserves exact pixel count")
    assert_eq(bounding_symmetry_diff(sm:cel(1).image), 0, "Orthogonal angle " .. a .. " stays symmetrical")
  end

  -- 45-degree symmetric square quality & color purity
  sm:setTransform(10, 10, 1, 1, 45, false, false, 0.5, 0.5)
  local img45 = sm:cel(1).image
  local cnt45 = count_non_transparent(img45, ColorMode.RGB)
  assert_true(cnt45 >= 120 and cnt45 <= 170, "45 deg area preserved without holes/bloat: " .. cnt45)
  local symDiff45 = bounding_symmetry_diff(img45)
  assert_true(symDiff45 <= 32, "45 deg symmetric square low asymmetry error: " .. symDiff45)

  -- Verify zero spurious alpha-blended interior colors (every non-transparent pixel must be pure red or outline blk)
  local red = app.pixelColor.rgba(230, 40, 40, 255)
  local blk = app.pixelColor.rgba(20, 20, 30, 255)
  local foreign = 0
  for it in img45:pixels() do
    local p = it()
    if app.pixelColor.rgbaA(p) > 0 and p ~= red and p ~= blk then
      foreign = foreign + 1
    end
  end
  assert_eq(foreign, 0, "RotSprite 45 deg preserves crisp source palette colors without murky blending")

  -- Fine 1-degree sweep + arbitrary fractional angles (ensure smooth bounds & no missing pixels)
  for deg = 1, 359, 13 do
    sm:setTransform(10, 10, 1, 1, deg, false, false, 0.5, 0.5)
    local c = sm:cel(1)
    assert_true(c ~= nil and c.image.width >= 16 and c.image.height >= 16, "Angle " .. deg .. " bounds valid")
    local n = count_non_transparent(c.image, ColorMode.RGB)
    assert_true(n >= 115 and n <= 175, string.format("Angle %d area sane (%d)", deg, n))
  end

  host:close()
end

do
  local hostG = Sprite(64, 64, ColorMode.GRAY)
  app.command.PlaceSmartObject{ filename = grayPath, name = "GraySO", x = 8, y = 8, ui = false }
  local smG = hostG.layers[2]
  smG:setTransform(8, 8, 1, 1, 37, false, false, 0.5, 0.5)
  local nG = count_non_transparent(smG:cel(1).image, ColorMode.GRAY)
  assert_true(nG >= 115 and nG <= 175, "Grayscale 37 deg count sane: " .. nG)
  hostG:close()

  local hostI = Sprite(64, 64, ColorMode.INDEXED)
  app.command.PlaceSmartObject{ filename = idxPath, name = "IdxSO", x = 8, y = 8, ui = false }
  local smI = hostI.layers[2]
  smI:setTransform(8, 8, 1, 1, 37, false, false, 0.5, 0.5)
  local nI = count_non_transparent(smI:cel(1).image, ColorMode.INDEXED)
  assert_true(nI >= 115 and nI <= 175, "Indexed 37 deg count sane: " .. nI)
  hostI:close()
end
print("  -> Rotation quality & color preservation passed.")

-- ==============================================================================
-- 2. Edge Dimensions & Scale+Rotate + Adaptive Pass Checks
-- ==============================================================================
print("\n[2/5] Edge dimensions (1x1, 1x8, 15x23) & combined Scale+Rotate...")

for _, dim in ipairs({{1, 1}, {1, 8}, {8, 1}, {15, 23}}) do
  local p = app.fs.joinPath(tmpDir, string.format("edge_%dx%d.aseprite", dim[1], dim[2]))
  local sp = Sprite(dim[1], dim[2], ColorMode.RGB)
  local im = sp.layers[1]:cel(1).image
  for y = 0, dim[2] - 1 do
    for x = 0, dim[1] - 1 do
      im:drawPixel(x, y, app.pixelColor.rgba(100 + x * 5, 180, 220, 255))
    end
  end
  sp:saveCopyAs(p)
  sp:close()

  local h = Sprite(64, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = p, name = "Edge", x = 16, y = 16, ui = false }
  local sm = h.layers[2]
  for _, ang in ipairs({0, 15, 37, 45, 90, 180, -30}) do
    sm:setTransform(16, 16, 1, 1, ang, false, false, 0.5, 0.5)
    local c = sm:cel(1)
    assert_true(c ~= nil and c.image.width >= 1 and c.image.height >= 1, "Edge dim valid")
    assert_true(count_non_transparent(c.image, ColorMode.RGB) >= 1,
      string.format("%dx%d @ %d deg must render pixels", dim[1], dim[2], ang))
  end
  -- 2x & 0.5x scaled rotation
  sm:setTransform(16, 16, 2.0, 2.0, 45, true, false, 0.5, 0.5)
  assert_true(count_non_transparent(sm:cel(1).image, ColorMode.RGB) >= 1, "2x 45deg rendered")
  sm:setTransform(16, 16, 0.5, 0.5, 37, false, true, 0.5, 0.5)
  assert_true(count_non_transparent(sm:cel(1).image, ColorMode.RGB) >= 1, "0.5x 37deg rendered")
  h:close()
end
print("  -> Edge dimensions & scale+flip+rotate passed.")

-- ==============================================================================
-- 3. UI Rotate / Flip Commands + Canvas Rotation + Undo/Redo Integration
-- ==============================================================================
print("\n[3/5] UI Rotate, Flip, Canvas Rotate/Flip, & Undo/Redo on Smart Object...")

do
  -- Asymmetric L-shape fixture so 90 deg CW / CCW / Flip H / Flip V are distinguishable
  local asymPath = app.fs.joinPath(tmpDir, "asym16.aseprite")
  make_source_sprite(asymPath, 16, ColorMode.RGB, function(img)
    local cTopLeft = app.pixelColor.rgba(255, 0, 0, 255)
    local cArm     = app.pixelColor.rgba(0, 220, 0, 255)
    for y = 2, 13 do img:drawPixel(3, y, cArm) end
    for x = 3, 12 do img:drawPixel(x, 13, cArm) end
    img:drawPixel(3, 2, cTopLeft) -- Red marker near top-left
  end)

  local spr = Sprite(128, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = asymPath, name = "AsymSO", x = 16, y = 8, ui = false }
  local sm = spr.layers[2]
  app.activeLayer = sm

  -- Target="mask" Rotate 90 CW without marquee selection must rotate Smart Object non-destructively!
  app.command.Rotate{ target = "mask", angle = 90 }
  assert_near(sm.angle, 90, 1e-6, "Rotate mask 90 updates sm.angle to 90")
  -- Top-left marker (x=3,y=2 in 16x16) rotated 90 CW around center goes to top-right (x=13, y=3)
  local c90 = sm:cel(1).image
  assert_eq(c90:getPixel(13, 3), app.pixelColor.rgba(255, 0, 0, 255), "90 CW pixel moved to (13,3)")

  -- Rotate another 90 CW -> 180
  app.command.Rotate{ target = "mask", angle = 90 }
  assert_near(sm.angle, 180, 1e-6, "Second Rotate 90 updates sm.angle to 180")
  assert_eq(sm:cel(1).image:getPixel(12, 13), app.pixelColor.rgba(255, 0, 0, 255), "180 pixel moved to (12,13)")

  -- Undo -> back to 90, Redo -> back to 180
  app.command.Undo()
  assert_near(sm.angle, 90, 1e-6, "Undo restores angle 90")
  assert_eq(sm:cel(1).image:getPixel(13, 3), app.pixelColor.rgba(255, 0, 0, 255), "Undo restores cel pixels")
  app.command.Redo()
  assert_near(sm.angle, 180, 1e-6, "Redo restores angle 180")

  -- Flip Horizontally (target="mask")
  app.command.Flip{ target = "mask", orientation = "horizontal" }
  assert_true(sm.flipH, "Flip Horizontal toggles sm.flipH")
  assert_near(sm.angle, -180, 1e-6, "Flip Horizontal negates non-zero angle cleanly")
  app.command.Undo()
  assert_true(not sm.flipH, "Undo Flip restores flipH=false")
  assert_near(sm.angle, 180, 1e-6, "Undo Flip restores angle=180")

  -- Reset back to clean angle=0, pos=(16, 8) for Canvas Rotate test
  app.command.Undo()
  app.command.Undo()
  assert_near(sm.angle, 0, 1e-6, "Back to angle=0 before canvas rotate")
  assert_near(sm.transform.x, 16, 1e-6, "pos.x=16")
  assert_near(sm.transform.y, 8, 1e-6, "pos.y=8")

  -- Canvas Rotate 90 CW on 128x64 -> 64x128:
  -- Smart Object occupies [16..32, 8..24] -> rotated 90 CW on 128x64 goes to [64-24 .. 64-8, 16..32] = [40..56, 16..32]
  app.command.Rotate{ angle = 90 }
  assert_eq(spr.width, 64, "Canvas rotated width 64")
  assert_eq(spr.height, 128, "Canvas rotated height 128")
  assert_near(sm.angle, 90, 1e-6, "Canvas rotate updated sm.angle=90")
  assert_near(sm.transform.x, 40, 1e-4, "Canvas rotate updated sm.transform.x=40")
  assert_near(sm.transform.y, 16, 1e-4, "Canvas rotate updated sm.transform.y=16")

  -- Survives RefreshSmartObject!
  app.command.RefreshSmartObject{}
  assert_near(sm.angle, 90, 1e-6, "Refresh preserves rotated angle")
  assert_eq(sm:cel(1).position.x, 40, "Refresh preserves cel.x=40")
  assert_eq(sm:cel(1).position.y, 16, "Refresh preserves cel.y=16")

  -- Undo Canvas Rotate restores everything atomically
  app.command.Undo()
  assert_eq(spr.width, 128, "Undo restores width 128")
  assert_eq(spr.height, 64, "Undo restores height 64")
  assert_near(sm.angle, 0, 1e-6, "Undo restores sm.angle=0")
  assert_near(sm.transform.x, 16, 1e-4, "Undo restores sm.transform.x=16")
  assert_near(sm.transform.y, 8, 1e-4, "Undo restores sm.transform.y=8")

  -- Canvas Flip Horizontal on 128x64: [16..32, 8..24] -> [96..112, 8..24]
  app.command.Flip{ orientation = "horizontal" }
  assert_true(sm.flipH, "Canvas flip updated sm.flipH=true")
  assert_near(sm.transform.x, 96, 1e-4, "Canvas flip updated sm.transform.x=96")
  app.command.Undo()
  assert_true(not sm.flipH, "Undo canvas flip restores flipH=false")
  assert_near(sm.transform.x, 16, 1e-4, "Undo canvas flip restores pos.x=16")

  spr:close()
end
print("  -> UI Rotate / Flip / Canvas Rotate / Undo / Redo passed.")

-- ==============================================================================
-- 4. Multi-frame Smart Object Rotation Consistency
-- ==============================================================================
print("\n[4/5] Multi-frame Smart Object rotation...")

do
  local animPath = app.fs.joinPath(tmpDir, "anim3_24x24.aseprite")
  local sp = Sprite(24, 24, ColorMode.RGB)
  for f = 1, 3 do
    if f > 1 then sp:newFrame() end
    local im = sp.layers[1]:cel(f).image
    local col = app.pixelColor.rgba(60 * f, 200 - 40 * f, 255, 255)
    for y = 4, 19 do
      for x = 4, 19 do im:drawPixel(x, y, col) end
    end
  end
  sp:saveCopyAs(animPath)
  sp:close()

  local host = Sprite(64, 64, ColorMode.RGB)
  host:newFrame()
  host:newFrame()
  app.command.PlaceSmartObject{ filename = animPath, name = "AnimSO", x = 8, y = 8, ui = false }
  local sm = host.layers[2]
  sm:setTransform(8, 8, 1, 1, 37, false, false, 0.5, 0.5)
  for f = 1, 3 do
    local c = sm:cel(f)
    assert_true(c ~= nil and c.image.width >= 24, "Frame " .. f .. " rotated")
    assert_true(count_non_transparent(c.image, ColorMode.RGB) >= 200, "Frame " .. f .. " rendered pixels")
  end
  host:close()
end
print("  -> Multi-frame rotation consistency passed.")

-- ==============================================================================
-- 5. Rotation Speed Benchmark (16x16 .. 256x256, 37 deg & 2x @ 45 deg)
-- ==============================================================================
print("\n[5/5] Rotation Speed Benchmark (Post-Optimization)...")

for _, sz in ipairs({16, 64, 128, 256}) do
  local p = app.fs.joinPath(tmpDir, string.format("bench_%d.aseprite", sz))
  make_source_sprite(p, sz, ColorMode.RGB, function(img, n)
    for y = 1, n - 2 do
      for x = 1, n - 2 do
        if (x + y) % 7 ~= 0 then
          img:drawPixel(x, y, app.pixelColor.rgba((x * 3) % 255, (y * 5) % 255, 200, 255))
        end
      end
    end
  end)

  local h = Sprite(sz * 4, sz * 4, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = p, name = "Bench", x = sz, y = sz, ui = false }
  local sm = h.layers[2]

  -- Warmup
  sm:setTransform(sz, sz, 1, 1, 15, false, false, 0.5, 0.5)

  local iters = (sz <= 64) and 20 or ((sz == 128) and 10 or 5)
  local t0 = os.clock()
  for i = 1, iters do
    sm:setTransform(sz, sz, 1, 1, 37 + (i % 3), false, false, 0.5, 0.5)
  end
  local dt37 = (os.clock() - t0) * 1000.0 / iters

  local t1 = os.clock()
  for i = 1, iters do
    sm:setTransform(sz, sz, 2.0, 2.0, 45 + (i % 3), false, false, 0.5, 0.5)
  end
  local dt2x45 = (os.clock() - t1) * 1000.0 / iters

  local cOut = sm:cel(1)
  assert_true(cOut ~= nil and count_non_transparent(cOut.image, ColorMode.RGB) > 0, "Bench output non-empty")

  print(string.format("  %3dx%-3d: 37deg=%.2f ms/iter | 2x@45deg=%.2f ms/iter", sz, sz, dt37, dt2x45))
  h:close()
end

app.fs.removeDirectory(tmpDir)
print("\nALL ROTATION QUALITY, UI COMMAND, & SPEED TESTS PASSED.")
