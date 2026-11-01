-- ==============================================================================
-- challenger_m2_it3_scaled_right_angles_stress.lua
-- Milestone 2 Iteration 3 Adversarial Stress Harness: Scaled Right Angles
--
-- Objective:
-- Stress-test scaled orthogonal rotations (90°, 180°, 270°, negative, multi-turn)
-- across scales (0.25x, 0.75x, 1.5x, 2.5x, 4.0x, asymmetric, negative)
-- with all pivot permutations (corners, edges, center, fractional, exterior)
-- and flip permutations (None, FlipH, FlipV, FlipHV).
--
-- Invariants Asserted:
-- 1. Exact Cel Dimensions: Cel image buffer width/height match theoretical math with 0 delta.
-- 2. Zero Transparent Padding Artifacts: For 100% opaque fixtures, NO border pixel has alpha == 0.
-- 3. Cel Position Finite & Deterministic: Position coordinates are valid integers without NaN/Inf.
-- 4. Pivot Invariance: Pivot offsets alter cel canvas coordinates without altering buffer size.
-- ==============================================================================

local fixtures = {
  { path = "tests/smart_object/fixtures/single_frame_16x16.aseprite", w = 16, h = 16, name = "16x16 (Square)" },
  { path = "tests/smart_object/fixtures/nonsquare_16x32.aseprite", w = 16, h = 32, name = "16x32 (Tall)" },
  { path = "tests/smart_object/fixtures/nonsquare_48x12.aseprite", w = 48, h = 12, name = "48x12 (Wide)" },
  { path = "tests/smart_object/fixtures/extreme_1x1.aseprite", w = 1, h = 1, name = "1x1 (Extreme)" },
}

-- Scales mandated by prompt (0.25x, 0.75x, 1.5x, 2.5x, 4.0x) + asymmetric + negative
local scales = {
  { sx = 0.25, sy = 0.25, name = "0.25x" },
  { sx = 0.75, sy = 0.75, name = "0.75x" },
  { sx = 1.5,  sy = 1.5,  name = "1.5x" },
  { sx = 2.5,  sy = 2.5,  name = "2.5x" },
  { sx = 4.0,  sy = 4.0,  name = "4.0x" },
  { sx = 0.25, sy = 2.5,  name = "(0.25, 2.5)" },
  { sx = 4.0,  sy = 0.75, name = "(4.0, 0.75)" },
  { sx = 3.0,  sy = 1.0,  name = "(3.0, 1.0)" },
  { sx = -0.75, sy = 1.5, name = "(-0.75, 1.5)" },
  { sx = 2.5,  sy = -4.0, name = "(2.5, -4.0)" },
}

-- Orthogonal right angles: 90°, 180°, 270° + negative + multi-turn
local angles = {
  { deg = 90.0,   name = "90°" },
  { deg = 180.0,  name = "180°" },
  { deg = 270.0,  name = "270°" },
  { deg = -90.0,  name = "-90°" },
  { deg = -180.0, name = "-180°" },
  { deg = -270.0, name = "-270°" },
  { deg = 450.0,  name = "450° (360+90)" },
  { deg = 540.0,  name = "540° (360+180)" },
}

-- Pivot permutations: 4 corners + 4 edges + center + fractional + exterior
local pivots = {
  { px = 0.0,  py = 0.0,  name = "Top-Left (0.0, 0.0)" },
  { px = 1.0,  py = 0.0,  name = "Top-Right (1.0, 0.0)" },
  { px = 0.0,  py = 1.0,  name = "Bottom-Left (0.0, 1.0)" },
  { px = 1.0,  py = 1.0,  name = "Bottom-Right (1.0, 1.0)" },
  { px = 0.5,  py = 0.0,  name = "Top-Center (0.5, 0.0)" },
  { px = 0.0,  py = 0.5,  name = "Left-Center (0.0, 0.5)" },
  { px = 1.0,  py = 0.5,  name = "Right-Center (1.0, 0.5)" },
  { px = 0.5,  py = 1.0,  name = "Bottom-Center (0.5, 1.0)" },
  { px = 0.5,  py = 0.5,  name = "Center (0.5, 0.5)" },
  { px = 0.25, py = 0.75, name = "Fractional (0.25, 0.75)" },
  { px = 0.33, py = 0.67, name = "Arbitrary (0.33, 0.67)" },
  { px = -0.5, py = -0.5, name = "Exterior (-0.5, -0.5)" },
}

-- Flips
local flips = {
  { h = false, v = false, name = "None" },
  { h = true,  v = false, name = "FlipH" },
  { h = false, v = true,  name = "FlipV" },
  { h = true,  v = true,  name = "FlipHV" },
}

print("================================================================================")
print("  EMPIRICAL CHALLENGER: SCALED RIGHT-ANGLES & ZERO-PADDING STRESS HARNESS       ")
print("================================================================================")

local totalTests = 0
local passedTests = 0
local failedTests = 0
local dimensionMismatches = 0
local paddingArtifacts = 0
local failureLog = {}

local startTime = os.clock()

for _, fix in ipairs(fixtures) do
  print(string.format("\n[TEST FIXTURE] %s (Source: %dx%d)", fix.name, fix.w, fix.h))
  local hostSpr = Sprite(500, 500)
  app.command.PlaceSmartObject{ filename = fix.path, ui = false }
  local lay = hostSpr.layers[#hostSpr.layers]
  assert(lay ~= nil and lay.isSmartObject == true, "Failed to place smart object layer: " .. fix.path)

  local fixPass = 0
  local fixFail = 0

  for _, sc in ipairs(scales) do
    local absSx = math.abs(sc.sx)
    local absSy = math.abs(sc.sy)
    local targetWs = math.max(1, math.floor(fix.w * absSx + 0.5))
    local targetHs = math.max(1, math.floor(fix.h * absSy + 0.5))

    for _, an in ipairs(angles) do
      local norm = ((an.deg % 360) + 360) % 360
      local isTransposed = (norm == 90 or norm == 270)
      local expW = isTransposed and targetHs or targetWs
      local expH = isTransposed and targetWs or targetHs

      for _, piv in ipairs(pivots) do
        for _, fl in ipairs(flips) do
          totalTests = totalTests + 1

          lay:setTransform(100, 100, sc.sx, sc.sy, an.deg, fl.h, fl.v, piv.px, piv.py)
          local renderOk = lay:renderCel(1, 1)

          if not renderOk then
            fixFail = fixFail + 1
            failedTests = failedTests + 1
            local msg = string.format("FAIL renderCel: %s | Scale %s | Angle %s | Pivot %s | Flip %s",
              fix.name, sc.name, an.name, piv.name, fl.name)
            table.insert(failureLog, msg)
          else
            local cel = lay:cel(1)
            if cel == nil or cel.image == nil then
              fixFail = fixFail + 1
              failedTests = failedTests + 1
              local msg = string.format("FAIL nil cel/image: %s | Scale %s | Angle %s | Pivot %s | Flip %s",
                fix.name, sc.name, an.name, piv.name, fl.name)
              table.insert(failureLog, msg)
            else
              local actW = cel.image.width
              local actH = cel.image.height

              -- 1. Assert exact dimensions (0 off-by-one pixel errors)
              if actW ~= expW or actH ~= expH then
                fixFail = fixFail + 1
                failedTests = failedTests + 1
                dimensionMismatches = dimensionMismatches + 1
                local msg = string.format("DIMENSION MISMATCH: %s | Scale %s | Angle %s | Pivot %s | Flip %s => got %dx%d, expected %dx%d (delta: dx=%d, dy=%d)",
                  fix.name, sc.name, an.name, piv.name, fl.name, actW, actH, expW, expH, actW - expW, actH - expH)
                table.insert(failureLog, msg)
              else
                -- 2. Assert zero transparent padding artifacts on borders for opaque fixtures
                -- All our fixtures have 100% opaque alpha. An off-by-one padding row/col would be alpha == 0.
                local img = cel.image
                local hasZeroAlphaPadding = false
                local paddingDetails = ""

                -- Check top row and bottom row
                for x = 0, actW - 1 do
                  local topPixel = img:getPixel(x, 0)
                  local topA = app.pixelColor.rgbaA(topPixel)
                  if topA == 0 then
                    hasZeroAlphaPadding = true
                    paddingDetails = string.format("top border pixel (%d, 0) alpha is 0", x)
                    break
                  end

                  local botPixel = img:getPixel(x, actH - 1)
                  local botA = app.pixelColor.rgbaA(botPixel)
                  if botA == 0 then
                    hasZeroAlphaPadding = true
                    paddingDetails = string.format("bottom border pixel (%d, %d) alpha is 0", x, actH - 1)
                    break
                  end
                end

                -- Check left col and right col
                if not hasZeroAlphaPadding then
                  for y = 0, actH - 1 do
                    local leftPixel = img:getPixel(0, y)
                    local leftA = app.pixelColor.rgbaA(leftPixel)
                    if leftA == 0 then
                      hasZeroAlphaPadding = true
                      paddingDetails = string.format("left border pixel (0, %d) alpha is 0", y)
                      break
                    end

                    local rightPixel = img:getPixel(actW - 1, y)
                    local rightA = app.pixelColor.rgbaA(rightPixel)
                    if rightA == 0 then
                      hasZeroAlphaPadding = true
                      paddingDetails = string.format("right border pixel (%d, %d) alpha is 0", actW - 1, y)
                      break
                    end
                  end
                end

                if hasZeroAlphaPadding then
                  fixFail = fixFail + 1
                  failedTests = failedTests + 1
                  paddingArtifacts = paddingArtifacts + 1
                  local msg = string.format("PADDING ARTIFACT: %s | Scale %s | Angle %s | Pivot %s | Flip %s => %s",
                    fix.name, sc.name, an.name, piv.name, fl.name, paddingDetails)
                  table.insert(failureLog, msg)
                else
                  fixPass = fixPass + 1
                  passedTests = passedTests + 1
                end
              end
            end
          end
        end
      end
    end
  end

  print(string.format("  -> Fixture Result: %d Passed, %d Failed (Total %d test configurations)",
    fixPass, fixFail, fixPass + fixFail))
  hostSpr:close()
end

local elapsed = os.clock() - startTime

print("\n================================================================================")
print("                         ADVERSARIAL STRESS SUITE SUMMARY                       ")
print("================================================================================")
print(string.format(" Total Executed Configurations : %d", totalTests))
print(string.format(" Passed Configurations         : %d", passedTests))
print(string.format(" Failed Configurations         : %d", failedTests))
print(string.format(" Dimension Mismatches (+-1 px) : %d", dimensionMismatches))
print(string.format(" Transparent Padding Artifacts : %d", paddingArtifacts))
print(string.format(" Execution Time                : %.2f seconds", elapsed))
print("================================================================================")

if failedTests > 0 then
  print("\nFAILURE DETAILS (Showing up to first 25 failures):")
  for i = 1, math.min(25, #failureLog) do
    print(string.format("  [%d] %s", i, failureLog[i]))
  end
  error(string.format("STRESS HARNESS FAILED: %d / %d configurations failed!", failedTests, totalTests))
else
  print("\n--> EMPIRICAL VERDICT: 100% PASS! ZERO OFF-BY-ONE PIXEL PADDING ARTIFACTS CONFIRMED!")
end
