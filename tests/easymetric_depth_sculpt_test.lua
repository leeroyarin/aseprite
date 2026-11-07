-- =============================================================================
-- Easymetric Depth Sculpt tests
-- =============================================================================
-- Proves the 2026-11-07 depth-sculpt change (William: "the depth map
-- should allow me to carve up the contents so that there could be
-- extra mesh... I want 3D object to be modified not the textures"):
-- the depth map now moves real voxels in buildSculptedVolume instead
-- of only bending normals/brightness.
--
-- Setup (all tests): a 72x48 sprite divides into 24x24 faces, the
-- Base sheet filled solid white, i.e. a 24x24x24 box. Depth step is
-- 128 / D with D = 12 per axis, so one dab moves the gray by ~10.7.
--   6 plus dabs  -> gray 192 -> +6 voxels out (bump)
--  10 minus dabs -> gray  21 -> -10 voxels in (carve)
--  13 minus dabs -> gray 0 at 12, 13th -> transparent + Base cleared
--   (through-hole; matches the depth-geometry simulation, sim1)
--
-- View facts the tests rely on (verified by probes, 2026-11-07):
-- yaw 0 looks at the BACK face, yaw 180 at the FRONT face (the
-- camera sits at -camDist*vd and rays travel in +vd). A centre
-- bump cannot change the silhouette from the front (the outline
-- is the face perimeter), but from the side (yaw 90) it pokes out
-- past the profile, so DS2 measures lit pixels at yaw 90.
-- Iso_Depth is range-normalized to the volume's bounding size
-- (normZ divides by maxDepthRange, which grows with the volume),
-- so a uniform Plus growth barely moves the head-on depth centre;
-- DS3 therefore measures a Minus pit at yaw 180, where the volume
-- does NOT grow, and generates the depth layer BEFORE the baseline
-- bake so the relief baseline matches (otherwise the luma->gray
-- relief swing eats the whole margin).
--
-- Run: xvfb-run -a ../aseprite-linux-build/bin/aseprite -b --script
--   tests/easymetric_depth_sculpt_test.lua
-- =============================================================================

local suite = {
  passed = 0,
  failed = 0,
  currentSuite = "Default",
  failures = {}
}

local function describe(suiteName, fn)
  suite.currentSuite = suiteName
  print(string.format("\n======================================================\n%s\n======================================================", suiteName))
  fn()
end

local function it(testName, fn)
  local ok, err = pcall(fn)
  if ok then
    suite.passed = suite.passed + 1
    print("  [PASS] " .. testName)
  else
    suite.failed = suite.failed + 1
    table.insert(suite.failures, { suite = suite.currentSuite, test = testName, err = err })
    print("  [FAIL] " .. testName .. " -- " .. tostring(err))
  end
end

local function assert_eq(a, b, msg)
  if a ~= b then error(msg .. " (expected " .. tostring(b) .. ", got " .. tostring(a) .. ")") end
end

local function assert_true(cond, msg)
  if not cond then error(msg) end
end

local function assert_not_nil(val, msg)
  if val == nil then error(msg) end
end

-- ---------------------------------------------------------------- helpers

local function findLayer(spr, name)
  for _, l in ipairs(spr.layers) do
    if l.name == name then return l end
  end
  return nil
end

-- A 72x48 sprite divides into 24x24 faces; fill the Base sheet solid
-- white so the volume is a 24x24x24 box.
local function makeBoxSprite()
  local spr = Sprite(72, 48)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "setupSurfaces" }
  local baseLyr = findLayer(spr, "Surface_Base")
  assert_not_nil(baseLyr, "Surface_Base layer exists")
  baseLyr:cel(1).image:clear(Color{ r = 255, g = 255, b = 255, a = 255 })
  return spr
end

local function bake(spr, yaw, pitch)
  app.command.EasymetricStudio{ action = "bake3D", yaw = yaw, pitch = pitch }
end

local function litPixels(spr)
  local diffLyr = findLayer(spr, "Iso_Diffuse")
  assert_not_nil(diffLyr, "Iso_Diffuse layer generated")
  local img = diffLyr:cel(1).image
  local lit = 0
  for y = 0, img.height - 1 do
    for x = 0, img.width - 1 do
      if app.pixelColor.rgbaA(img:getPixel(x, y)) > 0 then
        lit = lit + 1
      end
    end
  end
  return lit
end

-- Sum of all R+G+B in Iso_Diffuse: a strict fingerprint of the render.
local function diffuseChecksum(spr)
  local diffLyr = findLayer(spr, "Iso_Diffuse")
  assert_not_nil(diffLyr, "Iso_Diffuse layer generated")
  local img = diffLyr:cel(1).image
  local sum = 0
  for y = 0, img.height - 1 do
    for x = 0, img.width - 1 do
      local c = img:getPixel(x, y)
      sum = sum + app.pixelColor.rgbaR(c) + app.pixelColor.rgbaG(c) + app.pixelColor.rgbaB(c)
    end
  end
  return sum
end

-- Gray of the Iso_Depth centre pixel at a head-on view. The depth
-- channel encodes distance: farther = brighter.
local function depthCenterGray(spr)
  local deptLyr = findLayer(spr, "Iso_Depth")
  assert_not_nil(deptLyr, "Iso_Depth layer generated")
  local img = deptLyr:cel(1).image
  local c = img:getPixel(math.floor(img.width / 2), math.floor(img.height / 2))
  return app.pixelColor.rgbaR(c)
end

local function dab(spr, face, u, v, mode, radius)
  app.command.EasymetricStudio{ action = "depthDab", face = face, u = u, v = v, mode = mode, radius = radius }
end

-- ---------------------------------------------------------------- tests

describe("Depth Sculpt", function()

  it("DS1: a flat-128 depth layer changes nothing (pixel-identical bake)", function()
    local spr = makeBoxSprite()
    bake(spr, 45, 30)
    local beforeLit = litPixels(spr)
    local beforeSum = diffuseChecksum(spr)
    assert_true(beforeLit > 200, "baseline bake is a solid box")

    -- Flat mid-gray depth: 128 = surface, no displacement.
    app.command.EasymetricStudio{ action = "generateDepthFromBase" }
    local depthLyr = findLayer(spr, "Surface_DEPTH")
    assert_not_nil(depthLyr, "Surface_DEPTH layer exists")
    depthLyr:cel(1).image:clear(Color{ r = 128, g = 128, b = 128, a = 255 })

    bake(spr, 45, 30)
    assert_eq(litPixels(spr), beforeLit, "flat-128 depth keeps the same lit pixel count")
    assert_eq(diffuseChecksum(spr), beforeSum, "flat-128 depth keeps the render pixel-identical")
    spr:close()
  end)

  it("DS2: Plus dabs bump the mesh out (more lit pixels at yaw 90)", function()
    local spr = makeBoxSprite()
    -- Side view: a centre bump on the Front face pokes out past the
    -- box profile, so the silhouette grows (a head-on lit count
    -- cannot see a centre bump — the outline is the face perimeter).
    bake(spr, 90, 0)
    local beforeLit = litPixels(spr)
    assert_true(beforeLit > 200, "baseline bake is a solid box")

    -- 6 Plus dabs, radius 3, Front centre: gray 128 -> 192,
    -- +6 voxels out of the Front face.
    for _ = 1, 6 do
      dab(spr, "front", 12, 12, "plus", 3)
    end
    bake(spr, 90, 0)
    local afterLit = litPixels(spr)
    assert_true(afterLit > beforeLit,
      "Plus bump grows the silhouette (lit " .. beforeLit .. " -> " .. afterLit .. ")")
    spr:close()
  end)

  it("DS3: Minus dabs carve the mesh in (pit reads farther in Iso_Depth)", function()
    local spr = makeBoxSprite()
    -- Yaw 180 is the FRONT view (yaw 0 looks at the Back face).
    -- Generate the depth layer first so the baseline bake has the
    -- same relief baseline as the after bake.
    app.command.EasymetricStudio{ action = "generateDepthFromBase" }
    bake(spr, 180, 0)
    local beforeGray = depthCenterGray(spr)
    local beforeLit = litPixels(spr)
    assert_true(beforeLit > 200, "baseline head-on bake is a solid box")

    -- 10 Minus dabs, radius 3, Front centre: gray 128 -> 21,
    -- the Front surface recedes 10 voxels at the pit.
    for _ = 1, 10 do
      dab(spr, "front", 12, 12, "minus", 3)
    end
    bake(spr, 180, 0)
    local afterGray = depthCenterGray(spr)
    assert_true(afterGray > beforeGray + 5,
      "carved pit reads farther (depth centre " .. beforeGray .. " -> " .. afterGray .. ")")
    -- A pit is not a hole: the head-on silhouette is intact.
    assert_eq(litPixels(spr), beforeLit, "partial carve keeps the silhouette")
    spr:close()
  end)

  it("DS4: Minus past black cuts a through-hole (fewer lit pixels, Base cleared)", function()
    local spr = makeBoxSprite()
    bake(spr, 0, 0)
    local beforeLit = litPixels(spr)
    assert_true(beforeLit > 200, "baseline head-on bake is a solid box")

    -- 13 Minus dabs, radius 1, Front centre: 12 dabs reach black,
    -- the 13th goes transparent and clears the Base pixel.
    for _ = 1, 13 do
      dab(spr, "front", 12, 12, "minus", 1)
    end
    bake(spr, 0, 0)
    local afterLit = litPixels(spr)
    assert_true(afterLit < beforeLit,
      "through-hole removes lit pixels (lit " .. beforeLit .. " -> " .. afterLit .. ")")

    -- Front face is col 1, row 0 of the 3x2 sheet; dab (12,12)
    -- lands at sheet pixel (36,12) in the Base image.
    local baseLyr = findLayer(spr, "Surface_Base")
    assert_not_nil(baseLyr, "Surface_Base layer exists")
    local c = baseLyr:cel(1).image:getPixel(36, 12)
    assert_eq(app.pixelColor.rgbaA(c), 0, "Base pixel at the hole is transparent")
    spr:close()
  end)

end)

-- ==============================================================================
-- Test Suite Execution Summary
-- ==============================================================================
print("\n======================================================")
print("     DEPTH SCULPT TEST SUITE EXECUTION SUMMARY      ")
print("======================================================")
print(string.format(" TOTAL:  %3d Passed, %3d Failed (Total %d)", suite.passed, suite.failed, suite.passed + suite.failed))
print("======================================================")

if suite.failed > 0 then
  error(string.format("Depth Sculpt Test Suite FAILED: %d test(s) failed.", suite.failed))
else
  print("ALL DEPTH SCULPT TESTS PASSED SUCCESSFULLY.")
end
