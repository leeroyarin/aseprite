-- ==============================================================================
-- test_m2_empirical_stress.lua
-- Empirical Challenger Stress Harness for Milestone 2:
-- Non-Destructive 2D Transformation & Rendering Engine
-- ==============================================================================

local function readFileBytes(path)
  local f = io.open(path, "rb")
  if not f then error("Cannot open file: " .. path) end
  local content = f:read("*all")
  f:close()
  return content
end

local function computeSimpleChecksum(str)
  local hash = 5381
  for i = 1, #str do
    hash = ((hash * 33) + string.byte(str, i)) % 4294967296
  end
  return hash
end

print("================================================================================")
print("     EMPIRICAL STRESS TEST: MILESTONE 2 TRANSFORMATION & RENDERING ENGINE       ")
print("================================================================================")

local fixturePath = "tests/smart_object/fixtures/single_frame_16x16.aseprite"
local multiFixturePath = "tests/smart_object/fixtures/multi_frame_3f.aseprite"

-- ------------------------------------------------------------------------------
-- 1. SOURCE FILE PRE-TEST INTEGRITY & HASH
-- ------------------------------------------------------------------------------
print("\n[SECTION 1] Source File Disk Integrity Check (Pre-Test Snapshot)")
local preBytes = readFileBytes(fixturePath)
local preChecksum = computeSimpleChecksum(preBytes)
local preMultiBytes = readFileBytes(multiFixturePath)
local preMultiChecksum = computeSimpleChecksum(preMultiBytes)
print(string.format("  Source fixture: %s (%d bytes, checksum: 0x%08X)", fixturePath, #preBytes, preChecksum))
print(string.format("  Multi fixture:  %s (%d bytes, checksum: 0x%08X)", multiFixturePath, #preMultiBytes, preMultiChecksum))

-- ------------------------------------------------------------------------------
-- 2. BASELINE CAPTURE OF PRISTINE SOURCE PIXELS
-- ------------------------------------------------------------------------------
print("\n[SECTION 2] Pristine Source Pixel Baseline Capture")
local hostSpr = Sprite(64, 64)
app.command.PlaceSmartObject{ filename = fixturePath, ui = false }
assert(#hostSpr.layers >= 2, "Smart layer placed successfully")
local smLayer = hostSpr.layers[#hostSpr.layers]
assert(smLayer.isSmartObject == true, "Layer is smart object")

local baselineCel = smLayer:cel(1)
assert(baselineCel ~= nil, "Baseline cel exists")
assert(baselineCel.image.width == 16 and baselineCel.image.height == 16, "Dimensions 16x16")

local pristinePixels = {}
for y = 0, 15 do
  pristinePixels[y] = {}
  for x = 0, 15 do
    pristinePixels[y][x] = baselineCel.image:getPixel(x, y)
  end
end

-- Validate known test pattern (Quadrant: TL Red, TR Green, BL Blue, BR Yellow)
local cTL = pristinePixels[0][0]
local cTR = pristinePixels[0][15]
local cBL = pristinePixels[15][0]
local cBR = pristinePixels[15][15]
assert(app.pixelColor.rgbaR(cTL) == 255 and app.pixelColor.rgbaG(cTL) == 0, "TL is Red")
assert(app.pixelColor.rgbaR(cTR) == 0 and app.pixelColor.rgbaG(cTR) == 255, "TR is Green")
assert(app.pixelColor.rgbaB(cBL) == 255 and app.pixelColor.rgbaR(cBL) == 0, "BL is Blue")
assert(app.pixelColor.rgbaR(cBR) == 255 and app.pixelColor.rgbaG(cBR) == 255, "BR is Yellow")
print("  Pristine 16x16 baseline captured successfully (256/256 pixels mapped).")

-- ------------------------------------------------------------------------------
-- 3. 100 SUCCESSIVE ARBITRARY TRANSFORMATIONS & REVERSIBILITY VERIFICATION
-- ------------------------------------------------------------------------------
print("\n[SECTION 3] 100 Successive Arbitrary Transformations Harness")
-- Deterministic pseudo-random generator
local seed = 123456789
local function nextRand(min, max)
  seed = (1103515245 * seed + 12345) % 2147483648
  local norm = seed / 2147483648.0
  return min + norm * (max - min)
end

local transformHistory = {}
for iter = 1, 100 do
  local sx = nextRand(0.2, 4.0)
  local sy = nextRand(0.2, 4.0)
  local angle = nextRand(0.0, 720.0)
  local flipH = (nextRand(0.0, 1.0) > 0.5)
  local flipV = (nextRand(0.0, 1.0) > 0.5)
  local px = nextRand(0.0, 1.0)
  local py = nextRand(0.0, 1.0)
  local posX = nextRand(-20.0, 40.0)
  local posY = nextRand(-20.0, 40.0)

  smLayer:setTransform(sx, sy, angle, flipH, flipV, px, py)
  smLayer:translate(posX, posY)

  local cel = smLayer:cel(1)
  assert(cel ~= nil, string.format("Iter %d: Cel exists", iter))
  assert(cel.image.width > 0 and cel.image.height > 0, string.format("Iter %d: Valid cel size %dx%d", iter, cel.image.width, cel.image.height))

  if iter % 20 == 0 or iter == 100 then
    print(string.format("  Step %3d/100: scale=(%.2f, %.2f) angle=%6.1f deg flips=(%s,%s) -> rendered cel %dx%d",
      iter, sx, sy, angle, tostring(flipH), tostring(flipV), cel.image.width, cel.image.height))
  end
end

print("  Applying resetTransform()...")
smLayer:resetTransform()

local resetCel = smLayer:cel(1)
assert(resetCel ~= nil, "Reset cel exists")
assert(resetCel.image.width == 16, "Reset cel width must be 16")
assert(resetCel.image.height == 16, "Reset cel height must be 16")
assert(smLayer.scale.x == 1.0 and smLayer.scale.y == 1.0, "Scale is 1.0")
assert(smLayer.angle == 0.0, "Angle is 0.0")
assert(smLayer.flipH == false and smLayer.flipV == false, "Flips are false")

local mismatchCount = 0
for y = 0, 15 do
  for x = 0, 15 do
    local actual = resetCel.image:getPixel(x, y)
    local expected = pristinePixels[y][x]
    if actual ~= expected then
      mismatchCount = mismatchCount + 1
    end
  end
end

print(string.format("  Reversibility check: %d/256 matching pixels (%d mismatches)",
  256 - mismatchCount, mismatchCount))
assert(mismatchCount == 0, "FATAL: Generational degradation detected! Pixels do not match pristine source 100% bit-for-bit!")
print("  --> PASS: 100% BIT-FOR-BIT IDENTICAL REVERSIBILITY VERIFIED (0 bits lost).")

-- ------------------------------------------------------------------------------
-- 4. REVERTING TO IDENTITY VIA DIRECT PROPERTY SETTERS
-- ------------------------------------------------------------------------------
print("\n[SECTION 4] Revert to Identity via Direct Property Setters")
-- Perform 20 high-distortion transforms
for i = 1, 20 do
  smLayer.scale = { 0.33 + i * 0.15, 3.5 - i * 0.1 }
  smLayer.angle = (i * 37.7) % 360.0
  smLayer.flipH = (i % 2 == 1)
  smLayer.flipV = (i % 3 == 0)
end

-- Revert manually by property assignments
smLayer.scale = { 1.0, 1.0 }
smLayer.angle = 0.0
smLayer.flipH = false
smLayer.flipV = false
smLayer.pivot = { 0.5, 0.5 }

local manualResetCel = smLayer:cel(1)
assert(manualResetCel.image.width == 16 and manualResetCel.image.height == 16, "16x16 after property revert")

local manualMismatch = 0
for y = 0, 15 do
  for x = 0, 15 do
    if manualResetCel.image:getPixel(x, y) ~= pristinePixels[y][x] then
      manualMismatch = manualMismatch + 1
    end
  end
end
assert(manualMismatch == 0, "Manual property revert failed bit-for-bit match")
print("  --> PASS: Direct property setters revert to 100% bit-for-bit pristine source.")

-- ------------------------------------------------------------------------------
-- 5. FLOAT PRECISION SNAPPING (`snap_affine`) ADVERSARIAL STRESS
-- ------------------------------------------------------------------------------
print("\n[SECTION 5] Float Precision Snapping (snap_affine) Stress Cases")

-- Case 5.1: Micro-noise on Scale 1.0
smLayer.scale = { 1.0000000000000002, 0.9999999999999999 }
assert(smLayer.scale.x == 1.0, "Scale X snapped 1.0000000000000002 -> 1.0")
assert(smLayer.scale.y == 1.0, "Scale Y snapped 0.9999999999999999 -> 1.0")
print("  Case 5.1 PASS: Scale micro-noise around 1.0 snaps cleanly.")

-- Case 5.2: Micro-noise on Angle 360 / 0
smLayer.angle = 359.9999999999999
assert(smLayer.angle == 360.0, "Angle 359.9999999999999 snapped -> 360.0")
print("  Case 5.2 PASS: Angle 359.9999999999999 snaps to 360.0.")

-- Case 5.3: Micro-noise on Orthogonal Angles 90 and 180
smLayer.angle = 89.99999999999995
assert(smLayer.angle == 90.0, "Angle 89.99999999999995 snapped -> 90.0")
smLayer.angle = 180.00000000000006
assert(smLayer.angle == 180.0, "Angle 180.00000000000006 snapped -> 180.0")
print("  Case 5.3 PASS: Right-angle micro-noise snaps to exact 90.0 and 180.0.")

-- Case 5.4: Near-zero Angle Noise
smLayer.angle = 1e-12
assert(smLayer.angle == 0.0, "Angle 1e-12 snapped -> 0.0")
smLayer.angle = -1e-12
assert(smLayer.angle == 0.0, "Angle -1e-12 snapped -> 0.0")
print("  Case 5.4 PASS: Sub-epsilon angle snapped to clean 0.0.")

-- Case 5.5: Micro-noise triggering Identity Fast-Path
smLayer:setTransform(1.0000000000000002, 0.9999999999999999, 1e-11, false, false, 0.5, 0.5)
assert(smLayer.scale.x == 1.0 and smLayer.scale.y == 1.0, "Transform scale snapped to 1.0")
assert(smLayer.angle == 0.0, "Transform angle snapped to 0.0")
local snapCel = smLayer:cel(1)
assert(snapCel.image.width == 16 and snapCel.image.height == 16, "Cel is 16x16")
local snapMismatch = 0
for y = 0, 15 do
  for x = 0, 15 do
    if snapCel.image:getPixel(x, y) ~= pristinePixels[y][x] then
      snapMismatch = snapMismatch + 1
    end
  end
end
assert(snapMismatch == 0, "Identity fast-path with snapped micro-noise produces bit-identical copy")
print("  Case 5.5 PASS: Snapped identity triggers bit-identical copy path.")

-- Case 5.6: Intentional Small Float (Beyond 1e-9 tolerance) Preserved
smLayer.scale = { 1.0001, 1.0001 }
assert(math.abs(smLayer.scale.x - 1.0001) < 1e-6, "Intentional 1.0001 scale preserved")
smLayer.angle = 0.05
assert(math.abs(smLayer.angle - 0.05) < 1e-6, "Intentional 0.05 deg preserved")
print("  Case 5.6 PASS: Legitimate fine-grained floats outside epsilon tolerance preserved.")

hostSpr:close()

-- ------------------------------------------------------------------------------
-- 6. MULTI-FRAME ANIMATED SMART OBJECT REVERSIBILITY
-- ------------------------------------------------------------------------------
print("\n[SECTION 6] Multi-Frame Animated Smart Object Reversibility")
local multiSpr = Sprite(64, 64)
-- Create 3 frames in host
multiSpr:newEmptyFrame()
multiSpr:newEmptyFrame()
assert(#multiSpr.frames == 3, "Host has 3 frames")

app.command.PlaceSmartObject{ filename = multiFixturePath, ui = false }
local multiLayer = multiSpr.layers[#multiSpr.layers]
assert(multiLayer.isSmartObject == true, "Multi-frame smart layer created")

-- Ensure cels exist for frames 1, 2, 3
multiLayer:renderCel(1, 1)
multiLayer:renderCel(2, 2)
multiLayer:renderCel(3, 3)

-- Capture baseline for all 3 frames
local multiBaselines = {}
for f = 1, 3 do
  local c = multiLayer:cel(f)
  assert(c ~= nil, string.format("Multi cel frame %d exists", f))
  multiBaselines[f] = {}
  for y = 0, c.image.height - 1 do
    multiBaselines[f][y] = {}
    for x = 0, c.image.width - 1 do
      multiBaselines[f][y][x] = c.image:getPixel(x, y)
    end
  end
end
print("  Captured baselines for 3 frames.")

-- Apply 30 arbitrary transforms
for i = 1, 30 do
  local s = 0.5 + (i * 0.1)
  local a = (i * 24.0) % 360.0
  multiLayer:setTransform(s, s, a, (i % 2 == 0), (i % 3 == 0))
end
print("  Applied 30 transforms to multi-frame smart layer.")

-- Reset
multiLayer:resetTransform()
for f = 1, 3 do
  local c = multiLayer:cel(f)
  assert(c ~= nil, string.format("Reset multi cel %d exists", f))
  assert(c.image.width == 16 and c.image.height == 16, "16x16 cel dimensions")
  local fMismatch = 0
  for y = 0, 15 do
    for x = 0, 15 do
      if c.image:getPixel(x, y) ~= multiBaselines[f][y][x] then
        fMismatch = fMismatch + 1
      end
    end
  end
  assert(fMismatch == 0, string.format("Frame %d failed bit-identical match after reset", f))
end
print("  --> PASS: All 3 animation timeline frames match pristine source 100% bit-for-bit after reset.")
multiSpr:close()

-- ------------------------------------------------------------------------------
-- 7. EXTREME BOUNDARY & STRESS INPUTS
-- ------------------------------------------------------------------------------
print("\n[SECTION 7] Extreme Boundary & Stress Inputs")
local stressSpr = Sprite(64, 64)
app.command.PlaceSmartObject{ filename = fixturePath, ui = false }
local sLayer = stressSpr.layers[#stressSpr.layers]

-- Boundary 7.1: Extreme min scale (clamping to >= 1x1 image)
sLayer.scale = { 0.001, 0.001 }
local minCel = sLayer:cel(1)
assert(minCel ~= nil and minCel.image.width >= 1 and minCel.image.height >= 1, "Extreme min scale clamped cleanly")
print("  Boundary 7.1 PASS: Sub-pixel scale clamps safely to minimum dimensions.")

-- Boundary 7.2: Large scale
sLayer.scale = { 10.0, 10.0 }
local maxCel = sLayer:cel(1)
assert(maxCel ~= nil and maxCel.image.width == 160 and maxCel.image.height == 160, "10x scale renders 160x160")
print("  Boundary 7.2 PASS: 10x upscale renders cleanly.")

-- Boundary 7.3: RotSprite memory fallback stress (Arbitrary angle with large scale)
sLayer:setTransform(8.0, 8.0, 33.3, false, false)
local rotScaleCel = sLayer:cel(1)
assert(rotScaleCel ~= nil and rotScaleCel.image.width > 0, "Large rotated cel renders cleanly")
print("  Boundary 7.3 PASS: High-scale RotSprite rotation renders safely without crash.")

-- Boundary 7.4: Rapid Flip Flapping (50 toggles)
for i = 1, 50 do
  sLayer.flipH = (i % 2 == 1)
  sLayer.flipV = (i % 2 == 0)
end
sLayer.flipH = false
sLayer.flipV = false
sLayer:resetTransform()
local flapCel = sLayer:cel(1)
local flapMismatch = 0
for y = 0, 15 do
  for x = 0, 15 do
    if flapCel.image:getPixel(x, y) ~= pristinePixels[y][x] then
      flapMismatch = flapMismatch + 1
    end
  end
end
assert(flapMismatch == 0, "Flap test mismatch")
print("  Boundary 7.4 PASS: Rapid flip toggling maintains bit-for-bit pristine source.")

stressSpr:close()

-- ------------------------------------------------------------------------------
-- 8. SOURCE FILE POST-TEST DISK IMMUTABILITY & HASH VERIFICATION
-- ------------------------------------------------------------------------------
print("\n[SECTION 8] Source File Disk Immutability Verification (Post-Test)")
local postBytes = readFileBytes(fixturePath)
local postChecksum = computeSimpleChecksum(postBytes)
local postMultiBytes = readFileBytes(multiFixturePath)
local postMultiChecksum = computeSimpleChecksum(postMultiBytes)

print(string.format("  Single fixture: pre=%d bytes (0x%08X), post=%d bytes (0x%08X)",
  #preBytes, preChecksum, #postBytes, postChecksum))
print(string.format("  Multi fixture:  pre=%d bytes (0x%08X), post=%d bytes (0x%08X)",
  #preMultiBytes, preMultiChecksum, #postMultiBytes, postMultiChecksum))

assert(#preBytes == #postBytes, "Single frame file length changed!")
assert(preChecksum == postChecksum, "Single frame file checksum modified on disk!")
assert(preBytes == postBytes, "Single frame file binary content byte comparison failed!")

assert(#preMultiBytes == #postMultiBytes, "Multi-frame file length changed!")
assert(preMultiChecksum == postMultiChecksum, "Multi-frame file checksum modified on disk!")
assert(preMultiBytes == postMultiBytes, "Multi-frame file binary content byte comparison failed!")

print("  --> PASS: 100% BYTE-FOR-BYTE SOURCE FILE DISK IMMUTABILITY CONFIRMED.")
print("================================================================================")
print("     ALL EMPIRICAL STRESS TESTS PASSED WITH ZERO ERRORS OR BIT DRIFT            ")
print("================================================================================")
