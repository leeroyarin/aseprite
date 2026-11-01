-- tests/smart_object/test_m3_cel_linking_stress.lua
-- ==============================================================================
-- Empirical Challenger Stress Harness: Cel Linking, Buffer Sharing & Mode Switching
-- Milestone 3 Iteration 2: Deep Invariant Verification
-- ==============================================================================

print("================================================================================")
print("  EMPIRICAL CHALLENGER: CEL LINKING, BUFFER SHARING & MODE SWITCHING HARNESS   ")
print("================================================================================")

local testCount = 0
local passCount = 0

local function assert_eq(actual, expected, msg)
  testCount = testCount + 1
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "value mismatch", tostring(expected), tostring(actual)), 2)
  end
  passCount = passCount + 1
end

local function assert_true(cond, msg)
  testCount = testCount + 1
  if not cond then
    error(string.format("ASSERTION FAILED: %s (expected true)", msg or "condition failed"), 2)
  end
  passCount = passCount + 1
end

local function assert_nil(val, msg)
  testCount = testCount + 1
  if val ~= nil then
    error(string.format("ASSERTION FAILED: %s (expected nil, got %s)",
      msg or "value is not nil", tostring(val)), 2)
  end
  passCount = passCount + 1
end

local function getColorName(px)
  local r = app.pixelColor.rgbaR(px)
  local g = app.pixelColor.rgbaG(px)
  local b = app.pixelColor.rgbaB(px)
  if r == 255 and g == 0 and b == 0 then return "Red" end
  if r == 0 and g == 255 and b == 0 then return "Green" end
  if r == 0 and g == 0 and b == 255 then return "Blue" end
  return string.format("Unknown(R=%d,G=%d,B=%d)", r, g, b)
end

-- ==============================================================================
-- Challenge 1: Deep Buffer Sharing Verification (Loop, Hold, Fixed on 60-frame host)
-- ==============================================================================
print("\n[Challenge 1] Exhaustive Buffer Sharing Across Modes (60-frame Host, 3-frame Source)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 59 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 60, "host has 60 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 60, "smart layer has 60 cels initially")

  -- 1.1 LOOP MODE
  lay.timelineMapping = "Loop"
  lay:syncTimeline()
  assert_eq(lay.timelineMapping, "Loop")

  local canonicalLoop1 = lay:cel(1).image.id
  local canonicalLoop2 = lay:cel(2).image.id
  local canonicalLoop3 = lay:cel(3).image.id

  local uniqueLoop = {}
  for f = 1, 60 do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Loop frame %d cel exists", f))
    local mod = (f - 1) % 3
    local expectedId = (mod == 0 and canonicalLoop1) or (mod == 1 and canonicalLoop2) or canonicalLoop3
    local expectedColor = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"

    assert_eq(c.image.id, expectedId, string.format("Loop frame %d buffer ID matches canonical", f))
    assert_eq(getColorName(c.image:getPixel(0, 0)), expectedColor, string.format("Loop frame %d pixel color", f))
    uniqueLoop[c.image.id] = true
  end
  local loopCount = 0
  for _ in pairs(uniqueLoop) do loopCount = loopCount + 1 end
  assert_eq(loopCount, 3, "EXACTLY 3 unique image buffers in Loop mode across 60 frames")

  -- 1.2 HOLD MODE
  lay.timelineMapping = "Hold"
  lay:syncTimeline()
  assert_eq(lay.timelineMapping, "Hold")

  local canonicalHold1 = lay:cel(1).image.id
  local canonicalHold2 = lay:cel(2).image.id
  local canonicalHold3 = lay:cel(3).image.id

  local uniqueHold = {}
  for f = 1, 60 do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Hold frame %d cel exists", f))
    local expectedId = (f == 1 and canonicalHold1) or (f == 2 and canonicalHold2) or canonicalHold3
    local expectedColor = (f == 1 and "Red") or (f == 2 and "Green") or "Blue"

    assert_eq(c.image.id, expectedId, string.format("Hold frame %d buffer ID matches canonical", f))
    assert_eq(getColorName(c.image:getPixel(0, 0)), expectedColor, string.format("Hold frame %d pixel color", f))
    uniqueHold[c.image.id] = true
  end
  local holdCount = 0
  for _ in pairs(uniqueHold) do holdCount = holdCount + 1 end
  assert_eq(holdCount, 3, "EXACTLY 3 unique image buffers in Hold mode across 60 frames")

  -- 1.3 FIXED MODE
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()
  assert_eq(lay.timelineMapping, "Fixed")

  local canonicalFixed1 = lay:cel(1).image.id
  local uniqueFixed = {}
  for f = 1, 60 do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Fixed frame %d cel exists", f))
    assert_eq(c.image.id, canonicalFixed1, string.format("Fixed frame %d shares cel 1 buffer ID", f))
    assert_eq(getColorName(c.image:getPixel(0, 0)), "Red", string.format("Fixed frame %d is Red", f))
    uniqueFixed[c.image.id] = true
  end
  local fixedCount = 0
  for _ in pairs(uniqueFixed) do fixedCount = fixedCount + 1 end
  assert_eq(fixedCount, 1, "EXACTLY 1 unique image buffer in Fixed mode across 60 frames")

  spr:close()
  print("  -> Challenge 1 PASSED: Buffer sharing invariant hold rigorously in Loop, Hold, and Fixed.")
end

-- ==============================================================================
-- Challenge 2: Fixed Mode 1-Cel Invariant & Zero-Churn Under Idempotent Resync
-- ==============================================================================
print("\n[Challenge 2] Fixed Mode 1-Cel Invariant & Zero Churn Under Idempotent Re-sync...")
do
  local spr = Sprite(32, 32)
  for i = 1, 19 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 20, "host has 20 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()

  local initialCanonicalId = lay:cel(1).image.id

  -- Verify idempotent resync doesn't recreate cel 1 image buffer
  for iter = 1, 20 do
    lay:syncTimeline()
    assert_eq(lay:cel(1).image.id, initialCanonicalId, string.format("Iteration %d: image ID unchanged", iter))
    for f = 1, 20 do
      assert_eq(lay:cel(f).image.id, initialCanonicalId, string.format("Iteration %d, frame %d matches initial ID", iter, f))
    end
  end

  -- Add frames in Fixed mode and verify new cels link to same canonical ID without churn
  spr:newEmptyFrame() -- frame 21
  spr:newEmptyFrame() -- frame 22
  assert_eq(#spr.frames, 22)
  assert_eq(#lay.cels, 22)

  local newCanonicalId = lay:cel(1).image.id
  local uniqueAfterAdd = {}
  for f = 1, 22 do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Frame %d cel exists after add", f))
    assert_eq(c.image.id, newCanonicalId, string.format("Frame %d shares canonical image ID %d", f, newCanonicalId))
    uniqueAfterAdd[c.image.id] = true
  end
  local countAfterAdd = 0
  for _ in pairs(uniqueAfterAdd) do countAfterAdd = countAfterAdd + 1 end
  assert_eq(countAfterAdd, 1, "EXACTLY 1 unique image buffer shared across all 22 frames in Fixed mode")

  -- Re-sync without adding frames: zero churn, canonical ID remains strictly identical
  lay:syncTimeline()
  assert_eq(lay:cel(1).image.id, newCanonicalId, "Zero churn: canonical ID strictly preserved across re-sync")

  spr:close()
  print("  -> Challenge 2 PASSED: Fixed mode maintains 1-cel invariant with zero redundant churn.")
end

-- ==============================================================================
-- Challenge 3: Rapid High-Frequency Mode Transitions (150 Cycles)
-- ==============================================================================
print("\n[Challenge 3] Rapid High-Frequency Mode Transitions (150 Cycles: Loop <-> Hold <-> Fixed)...")
do
  local spr = Sprite(16, 16)
  for i = 1, 11 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 12, "host has 12 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local modes = { "Loop", "Hold", "Fixed" }
  for cycle = 1, 50 do
    for _, mode in ipairs(modes) do
      lay.timelineMapping = mode
      lay:syncTimeline()
      assert_eq(lay.timelineMapping, mode, string.format("Cycle %d: mode set to %s", cycle, mode))
      assert_eq(#lay.cels, 12, string.format("Cycle %d: exactly 12 cels present in %s", cycle, mode))

      -- Quick sanity checks based on mode
      if mode == "Fixed" then
        assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
        assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Red")
        assert_eq(getColorName(lay:cel(12).image:getPixel(0, 0)), "Red")
        assert_eq(lay:cel(12).image.id, lay:cel(1).image.id)
      elseif mode == "Hold" then
        assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
        assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green")
        assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue")
        assert_eq(getColorName(lay:cel(12).image:getPixel(0, 0)), "Blue")
        assert_eq(lay:cel(12).image.id, lay:cel(3).image.id)
      elseif mode == "Loop" then
        assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
        assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green")
        assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue")
        assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red")
        assert_eq(lay:cel(4).image.id, lay:cel(1).image.id)
      end
    end
  end

  spr:close()
  print("  -> Challenge 3 PASSED: 150 consecutive mode transitions completed flawlessly.")
end

-- ==============================================================================
-- Challenge 4: Mode Switching Interleaved with Timeline Structural Mutations
-- ==============================================================================
print("\n[Challenge 4] Mode Switching Interleaved with Frame Additions & Deletions...")
do
  local spr = Sprite(32, 32)
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 6, "initial host has 6 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  -- Step 1: In Fixed mode, delete head frame (frame 1)
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()
  assert_eq(#lay.cels, 6)

  spr:deleteFrame(1)
  assert_eq(#spr.frames, 5, "host has 5 frames after head deletion")
  assert_eq(#lay.cels, 5, "smart layer has 5 cels after head deletion in Fixed mode")
  assert_nil(lay:cel(6), "cel 6 is nil")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 is Red")
  assert_eq(getColorName(lay:cel(5).image:getPixel(0, 0)), "Red", "frame 5 is Red")
  assert_eq(lay:cel(5).image.id, lay:cel(1).image.id, "frame 5 shares frame 1 buffer")

  -- Step 2: Switch to Loop mode on 5 frames
  lay.timelineMapping = "Loop"
  lay:syncTimeline()
  assert_eq(#lay.cels, 5)
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue")
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay:cel(5).image:getPixel(0, 0)), "Green")

  -- Step 3: Insert middle frame at 3 in Loop mode
  spr:newEmptyFrame(3)
  assert_eq(#spr.frames, 6)
  assert_eq(#lay.cels, 6)
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue")
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay:cel(5).image:getPixel(0, 0)), "Green")
  assert_eq(getColorName(lay:cel(6).image:getPixel(0, 0)), "Blue")

  -- Step 4: Switch to Hold mode, delete tail frame (frame 6)
  lay.timelineMapping = "Hold"
  lay:syncTimeline()
  spr:deleteFrame(6)
  assert_eq(#spr.frames, 5)
  assert_eq(#lay.cels, 5)
  assert_nil(lay:cel(6))
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Blue")
  assert_eq(getColorName(lay:cel(5).image:getPixel(0, 0)), "Blue")
  assert_eq(lay:cel(5).image.id, lay:cel(3).image.id)

  -- Step 5: Switch back to Fixed mode and shrink to 1 frame
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()
  for i = 5, 2, -1 do
    spr:deleteFrame(i)
  end
  assert_eq(#spr.frames, 1)
  assert_eq(#lay.cels, 1)
  assert_nil(lay:cel(2))
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")

  -- Step 6: Expand from 1 frame to 10 frames in Fixed mode
  for i = 2, 10 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10)
  lay:syncTimeline()
  assert_eq(#lay.cels, 10)
  for f = 1, 10 do
    assert_eq(getColorName(lay:cel(f).image:getPixel(0, 0)), "Red")
    assert_eq(lay:cel(f).image.id, lay:cel(1).image.id)
  end

  spr:close()
  print("  -> Challenge 4 PASSED: Interleaved structural mutations and mode switches robust.")
end

-- ==============================================================================
-- Challenge 5: Single-Frame Source Document Invariants (1-frame linked object)
-- ==============================================================================
print("\n[Challenge 5] Single-Frame Source Document Across Modes and Resizing...")
do
  local spr = Sprite(16, 16)
  for i = 1, 7 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 8, "host has 8 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(lay.linkedFrameCount, 1, "single frame source has linkedFrameCount == 1")
  assert_eq(#lay.cels, 8, "smart layer has 8 cels")

  local c1_id = lay:cel(1).image.id

  -- Across Loop, Hold, and Fixed, all 8 cels must share cel 1 image buffer
  for _, mode in ipairs({ "Loop", "Hold", "Fixed", "Loop" }) do
    lay.timelineMapping = mode
    lay:syncTimeline()
    local modeC1Id = lay:cel(1).image.id
    local uniqueInMode = {}
    for f = 1, 8 do
      local c = lay:cel(f)
      assert_true(c ~= nil, string.format("Single-frame mode %s frame %d exists", mode, f))
      assert_eq(c.image.id, modeC1Id, string.format("Single-frame mode %s frame %d shares cel 1 buffer", mode, f))
      uniqueInMode[c.image.id] = true
    end
    local countMode = 0
    for _ in pairs(uniqueInMode) do countMode = countMode + 1 end
    assert_eq(countMode, 1, string.format("Single-frame source mode %s has exactly 1 unique buffer", mode))
  end

  -- Delete down to 1 frame, expand to 15 frames
  for i = 8, 2, -1 do
    spr:deleteFrame(i)
  end
  assert_eq(#lay.cels, 1)

  for i = 2, 15 do
    spr:newEmptyFrame()
  end
  lay:syncTimeline()
  assert_eq(#lay.cels, 15)
  local finalC1Id = lay:cel(1).image.id
  for f = 1, 15 do
    assert_eq(lay:cel(f).image.id, finalC1Id)
  end

  spr:close()
  print("  -> Challenge 5 PASSED: Single-frame source document behaves invariantly across all modes.")
end

-- ==============================================================================
-- Challenge 6: 1-Frame Host Sprite Edge Cases
-- ==============================================================================
print("\n[Challenge 6] 1-Frame Host Sprite Edge Cases...")
do
  local spr = Sprite(20, 20)
  assert_eq(#spr.frames, 1, "host has exactly 1 frame")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 1, "smart layer has exactly 1 cel")
  assert_nil(lay:cel(2), "cel 2 is nil")

  local origId = lay:cel(1).image.id

  for _, mode in ipairs({ "Fixed", "Hold", "Loop", "Fixed" }) do
    lay.timelineMapping = mode
    lay:syncTimeline()
    assert_eq(#lay.cels, 1, string.format("1-frame host in %s has 1 cel", mode))
    assert_nil(lay:cel(2), string.format("1-frame host in %s cel 2 is nil", mode))
    assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
  end

  spr:close()
  print("  -> Challenge 6 PASSED: 1-frame host edge cases verified.")
end

print("\n================================================================================")
print(string.format("  ALL EMPIRICAL CHALLENGES PASSED (%d assertions)!             ", passCount))
print("================================================================================")
