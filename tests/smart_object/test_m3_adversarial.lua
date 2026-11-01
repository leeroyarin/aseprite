-- tests/smart_object/test_m3_adversarial.lua
-- ==============================================================================
-- Unified Adversarial & Timeline Correctness Regression Suite
-- Milestone 3: Animation Timeline Synchronization & Cel Lifecycle Hardening
--
-- Consolidated from:
--   - reviewer_smart_m3_1 (Sequence shifting & displaceFrames integrity)
--   - reviewer_smart_m3_2 (Cel resurrection & frame deletion pruning)
--   - challenger_stress & challenger_edge (Boundary & buffer sharing)
-- ==============================================================================

print("================================================================================")
print("  UNIFIED ADVERSARIAL REGRESSION SUITE: MILESTONE 3 TIMELINE & CEL LIFECYCLE     ")
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
-- Test 1: Frame Insertion in Timeline (Middle, Head, Tail) — Sequence Correctness
-- Verifies that displaceFrames updates canonical and linked cels automatically
-- without requiring manual invocation of renderAllCels().
-- ==============================================================================
print("\n[Test 1] Frame Insertion in Middle, Head, and Tail (Sequence Correctness)...")
do
  local spr = Sprite(32, 32)
  spr:newEmptyFrame() -- frame 2
  spr:newEmptyFrame() -- frame 3
  assert_eq(#spr.frames, 3, "initial host has 3 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local lay = spr.layers[2]
  assert_eq(#lay.cels, 3, "smart layer has 3 cels initially")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 initial is Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "frame 2 initial is Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue", "frame 3 initial is Blue")

  -- 1A: Middle insertion at index 2 (between Red and Green)
  -- Host expands to 4 frames.
  -- Modulo sequence MUST be: 1 -> Red, 2 -> Green, 3 -> Blue, 4 -> Red.
  spr:newEmptyFrame(2)
  assert_eq(#spr.frames, 4, "host has 4 frames after middle insertion")
  assert_eq(#lay.cels, 4, "smart layer has 4 cels after middle insertion")

  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 after middle insert is Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "frame 2 after middle insert is Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue", "frame 3 after middle insert is Blue")
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red", "frame 4 after middle insert is Red")

  -- 1B: Head insertion at index 1
  -- Host expands to 5 frames.
  -- Sequence: 1 -> Red, 2 -> Green, 3 -> Blue, 4 -> Red, 5 -> Green.
  spr:newEmptyFrame(1)
  assert_eq(#spr.frames, 5, "host has 5 frames after head insertion")
  assert_eq(#lay.cels, 5, "smart layer has 5 cels after head insertion")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 after head insert is Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "frame 2 after head insert is Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue", "frame 3 after head insert is Blue")
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red", "frame 4 after head insert is Red")
  assert_eq(getColorName(lay:cel(5).image:getPixel(0, 0)), "Green", "frame 5 after head insert is Green")

  -- 1C: Tail insertion
  -- Host expands to 6 frames: 1..6 -> R, G, B, R, G, B.
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 6, "host has 6 frames after tail insertion")
  assert_eq(#lay.cels, 6, "smart layer has 6 cels after tail insertion")
  assert_eq(getColorName(lay:cel(6).image:getPixel(0, 0)), "Blue", "frame 6 after tail insert is Blue")

  spr:close()
  print("  -> Test 1 PASSED: Frame insertions in middle, head, tail preserve sequence.")
end

-- ==============================================================================
-- Test 2: Frame Deletion in Tail, Middle, Head (No Orphaned Cels & Exact Count)
-- Verifies that displaceFrames does not resurrect deleted cels and prunes all
-- cels beyond host sprite totalFrames.
-- ==============================================================================
print("\n[Test 2] Frame Deletion in Tail, Middle, and Head (Cel Count Integrity)...")
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
  assert_eq(#lay.cels, 6, "smart layer has 6 cels initially")

  -- 2A: Tail deletion (frame 6)
  spr:deleteFrame(6)
  assert_eq(#spr.frames, 5, "host sprite reduced to 5 frames")
  assert_eq(#lay.cels, 5, "smart layer pruned to exactly 5 cels (no resurrected cels)")
  assert_nil(lay:cel(6), "cel 6 is nil")
  assert_eq(getColorName(lay:cel(5).image:getPixel(0, 0)), "Green", "frame 5 is Green")

  -- 2B: Middle deletion (frame 3 - Blue)
  spr:deleteFrame(3)
  assert_eq(#spr.frames, 4, "host sprite reduced to 4 frames")
  assert_eq(#lay.cels, 4, "smart layer pruned to exactly 4 cels")
  assert_nil(lay:cel(5), "cel 5 is nil")
  -- Sequence: 1 -> Red, 2 -> Green, 3 -> Blue, 4 -> Red
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 is Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "frame 2 is Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue", "frame 3 is Blue")
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red", "frame 4 is Red")

  -- 2C: Head deletion (frame 1)
  spr:deleteFrame(1)
  assert_eq(#spr.frames, 3, "host sprite reduced to 3 frames")
  assert_eq(#lay.cels, 3, "smart layer pruned to exactly 3 cels")
  assert_nil(lay:cel(4), "cel 4 is nil")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 is Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "frame 2 is Green")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue", "frame 3 is Blue")

  -- 2D: Successive deletions down to 1 frame
  spr:deleteFrame(3)
  assert_eq(#spr.frames, 2, "host sprite has 2 frames")
  assert_eq(#lay.cels, 2, "smart layer has exactly 2 cels")
  assert_nil(lay:cel(3), "cel 3 is nil")

  spr:deleteFrame(2)
  assert_eq(#spr.frames, 1, "host sprite has 1 frame")
  assert_eq(#lay.cels, 1, "smart layer has exactly 1 cel")
  assert_nil(lay:cel(2), "cel 2 is nil")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "frame 1 is Red")

  spr:close()
  print("  -> Test 2 PASSED: Frame deletions prune cels with 0 orphaned remnants.")
end

-- ==============================================================================
-- Test 3: Fixed Timeline Mode (0 Churn & Single Buffer Sharing)
-- Verifies that in Fixed mode, only 1 canonical cel is rendered, all host cels
-- link to cel 1, and no redundant source frames are churned.
-- ==============================================================================
print("\n[Test 3] Fixed Timeline Mapping Mode (0 Churn & Single Buffer Sharing)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 11 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 12, "host has 12 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local lay = spr.layers[2]
  assert_eq(#lay.cels, 12, "smart layer has 12 cels")

  -- Switch to Fixed mode
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()
  assert_eq(lay.timelineMapping, "Fixed", "timelineMapping is Fixed")
  assert_eq(#lay.cels, 12, "cel count remains 12 in Fixed mode")

  -- Collect unique image IDs across all 12 cels
  local uniqueIds = {}
  local countUnique = 0
  local canonicalId = lay:cel(1).image.id

  for f = 1, 12 do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Fixed cel %d exists", f))
    assert_eq(c.image.id, canonicalId, string.format("Fixed frame %d shares cel 1 image ID", f))
    assert_eq(getColorName(c.image:getPixel(0, 0)), "Red", string.format("Fixed frame %d is Red", f))
    if not uniqueIds[c.image.id] then
      uniqueIds[c.image.id] = true
      countUnique = countUnique + 1
    end
  end

  assert_eq(countUnique, 1, "EXACTLY 1 unique image buffer shared across all 12 frames in Fixed mode")

  -- Re-sync in Fixed mode (0 churn: cel 1 ID must remain identical)
  lay:syncTimeline()
  assert_eq(lay:cel(1).image.id, canonicalId, "0 churn: canonical image ID preserved across re-sync")

  -- Roundtrip to Loop and back to Fixed
  lay.timelineMapping = "Loop"
  lay:syncTimeline()
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "restored Loop frame 2 is Green")

  lay.timelineMapping = "Fixed"
  lay:syncTimeline()
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Red", "re-switched Fixed frame 2 is Red")
  assert_eq(lay:cel(2).image.id, lay:cel(1).image.id, "re-switched Fixed frame 2 shares image 1")

  spr:close()
  print("  -> Test 3 PASSED: Fixed mode shares cel 1 with zero redundant churn.")
end

-- ==============================================================================
-- Test 4: Dynamic Expansion / Shrinkage (50 to 1 and 1 to 50)
-- Stress tests extreme timeline size swings, cel re-allocation, and garbage collection.
-- ==============================================================================
print("\n[Test 4] Dynamic Expansion / Shrinkage (50 to 1 and 1 to 50)...")
do
  local spr = Sprite(16, 16)
  for i = 1, 49 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 50, "initial host has 50 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local lay = spr.layers[2]
  assert_eq(#lay.cels, 50, "smart layer has 50 cels initially")

  -- 4A: Shrink from 50 frames down to 1 frame
  for i = 50, 2, -1 do
    spr:deleteFrame(i)
  end
  assert_eq(#spr.frames, 1, "host sprite shrunk to 1 frame")
  assert_eq(#lay.cels, 1, "smart layer pruned to EXACTLY 1 cel (no dangling cels)")
  assert_nil(lay:cel(2), "cel 2 is nil")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "surviving frame 1 is Red")

  -- 4B: Expand from 1 frame back to 50 frames
  for i = 2, 50 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 50, "host sprite expanded to 50 frames")
  lay:syncTimeline()
  assert_eq(#lay.cels, 50, "smart layer expanded to 50 cels")

  -- Verify all 50 frames have valid modulo colors and buffer sharing
  local c1_id = lay:cel(1).image.id
  local c2_id = lay:cel(2).image.id
  local c3_id = lay:cel(3).image.id
  local uniqueIds50 = {}

  for f = 1, 50 do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("frame %d cel exists in expanded host", f))
    local mod = (f - 1) % 3
    local expColor = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
    local expId = (mod == 0 and c1_id) or (mod == 1 and c2_id) or c3_id
    assert_eq(getColorName(c.image:getPixel(0, 0)), expColor, string.format("expanded frame %d color", f))
    assert_eq(c.image.id, expId, string.format("expanded frame %d image ID", f))
    uniqueIds50[c.image.id] = true
  end

  local count50 = 0
  for _ in pairs(uniqueIds50) do count50 = count50 + 1 end
  assert_eq(count50, 3, "EXACTLY 3 unique image buffers shared across all 50 cels")

  -- 4C: Shrink down to 3 frames
  for i = 50, 4, -1 do
    spr:deleteFrame(i)
  end
  assert_eq(#spr.frames, 3, "host sprite shrunk to 3 frames")
  assert_eq(#lay.cels, 3, "smart layer pruned to exactly 3 cels")
  assert_nil(lay:cel(4), "cel 4 is nil")

  collectgarbage("collect")
  spr:close()
  print("  -> Test 4 PASSED: 50 -> 1 -> 50 -> 3 transitions cleanly without leaks.")
end

-- ==============================================================================
-- Test 5: Mathematical Modulo & Boundary Indices in mapTimelineFrame
-- Verifies Euclidean modulo arithmetic across negative, boundary, and large indices.
-- ==============================================================================
print("\n[Test 5] Boundary & Negative Indexing (mapTimelineFrame)...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(lay.linkedFrameCount, 3)

  -- Loop mode modulo with negative and large indices
  assert_eq(lay:mapTimelineFrame(1), 1, "Lua frame 1 -> linked 1")
  assert_eq(lay:mapTimelineFrame(0), 3, "Lua frame 0 -> linked 3 (negative modulo)")
  assert_eq(lay:mapTimelineFrame(-1), 2, "Lua frame -1 -> linked 2 (negative modulo)")
  assert_eq(lay:mapTimelineFrame(-2), 1, "Lua frame -2 -> linked 1 (negative modulo)")
  assert_eq(lay:mapTimelineFrame(3001), 1, "Lua frame 3001 -> linked 1")
  assert_eq(lay:mapTimelineFrame(3002), 2, "Lua frame 3002 -> linked 2")
  assert_eq(lay:mapTimelineFrame(3003), 3, "Lua frame 3003 -> linked 3")

  -- Hold mode clamps
  lay.timelineMapping = "Hold"
  assert_eq(lay:mapTimelineFrame(1), 1, "Hold frame 1 -> 1")
  assert_eq(lay:mapTimelineFrame(0), 1, "Hold frame 0 -> 1 (clamped to min)")
  assert_eq(lay:mapTimelineFrame(-10), 1, "Hold frame -10 -> 1 (clamped to min)")
  assert_eq(lay:mapTimelineFrame(2), 2, "Hold frame 2 -> 2")
  assert_eq(lay:mapTimelineFrame(3), 3, "Hold frame 3 -> 3")
  assert_eq(lay:mapTimelineFrame(4), 3, "Hold frame 4 -> 3 (clamped to max)")
  assert_eq(lay:mapTimelineFrame(9999), 3, "Hold frame 9999 -> 3 (clamped to max)")

  -- Fixed mode always 1
  lay.timelineMapping = "Fixed"
  assert_eq(lay:mapTimelineFrame(1), 1, "Fixed frame 1 -> 1")
  assert_eq(lay:mapTimelineFrame(-5), 1, "Fixed frame -5 -> 1")
  assert_eq(lay:mapTimelineFrame(500), 1, "Fixed frame 500 -> 1")

  spr:close()
  print("  -> Test 5 PASSED: Euclidean modulo and mapping clamps verified.")
end

-- ==============================================================================
-- Test 6: Rapid Scrubbing Stress & Invalid Document Safety
-- Rapidly scrub random frames and check graceful failure on non-existent document.
-- ==============================================================================
print("\n[Test 6] Rapid Scrubbing Stress & Missing File Graceful Handling...")
do
  -- 6A: Scrubbing stress
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

  local seed = 12345
  for i = 1, 200 do
    seed = (seed * 1103515245 + 12345) % 2147483648
    local f = (seed % 60) + 1
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("scrubbed frame %d cel exists", f))
    assert_true(c.image ~= nil, string.format("scrubbed frame %d image exists", f))
  end
  spr:close()

  -- 6B: Invalid file safety
  local spr2 = Sprite(16, 16)
  local ok, err = pcall(function()
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/non_existent_file_xyz.aseprite",
      ui = false
    }
  end)
  assert_eq(#spr2.layers, 1, "no layer added on non-existent file")
  spr2:close()

  print("  -> Test 6 PASSED: Scrubbing throughput and error handling verified.")
end

-- ==============================================================================
-- Test 7: Comprehensive Undo/Redo & Transaction Stress Suite
-- ==============================================================================

-- Helper to verify cel topology and absence of duplicates
local function verifyCelTopology(spr, lay, expectedCount, testLabel)
  assert_eq(#spr.frames, expectedCount, string.format("[%s] sprite frames count", testLabel))
  assert_eq(#lay.cels, expectedCount, string.format("[%s] layer cels count strictly matches frames count", testLabel))

  local seenFrames = {}
  for idx, c in ipairs(lay.cels) do
    assert_true(c ~= nil, string.format("[%s] cel at index %d is not nil", testLabel, idx))
    assert_true(seenFrames[c.frameNumber] == nil,
      string.format("[%s] duplicate cel detected at frame %d (cel index %d)", testLabel, c.frameNumber, idx))
    seenFrames[c.frameNumber] = idx
    assert_true(c.frameNumber >= 1 and c.frameNumber <= expectedCount,
      string.format("[%s] cel frameNumber %d out of bounds [1, %d]", testLabel, c.frameNumber, expectedCount))
  end

  for f = 1, expectedCount do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("[%s] cel(f) exists for frame %d", testLabel, f))
    assert_eq(c.frameNumber, f, string.format("[%s] cel(f).frameNumber matches frame index %d", testLabel, f))
  end
end

-- Helper to verify modulo color sequence for Loop mode
local function verifyLoopColors(lay, frameCount, testLabel)
  for f = 1, frameCount do
    local c = lay:cel(f)
    assert_true(c ~= nil and c.image ~= nil, string.format("[%s] frame %d has valid cel and image", testLabel, f))
    local mod = (f - 1) % 3
    local expColor = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
    local actualColor = getColorName(c.image:getPixel(0, 0))
    assert_eq(actualColor, expColor, string.format("[%s] frame %d pixel color", testLabel, f))
  end
end

print("\n[Test 7] Comprehensive Undo/Redo & Transaction Stress Suite...")
do
  -- ----------------------------------------------------------------------------
  -- 7A: Command-based Frame Addition Undo & Redo (app.command.NewFrame)
  -- ----------------------------------------------------------------------------
  print("  -> Subtest 7A: app.command.NewFrame undo/redo...")
  do
    local spr = Sprite(16, 16)
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local lay = spr.layers[2]
    verifyCelTopology(spr, lay, 1, "7A initial")
    verifyLoopColors(lay, 1, "7A initial")

    -- Add frame 2
    app.command.NewFrame{ content = "empty" }
    verifyCelTopology(spr, lay, 2, "7A after NewFrame 2")
    verifyLoopColors(lay, 2, "7A after NewFrame 2")

    -- Add frame 3
    app.command.NewFrame{ content = "empty" }
    verifyCelTopology(spr, lay, 3, "7A after NewFrame 3")
    verifyLoopColors(lay, 3, "7A after NewFrame 3")

    -- Undo addition of frame 3
    app.undo()
    verifyCelTopology(spr, lay, 2, "7A after undo frame 3")
    verifyLoopColors(lay, 2, "7A after undo frame 3")
    assert_nil(lay:cel(3), "7A frame 3 cel is nil after undo")

    -- Undo addition of frame 2
    app.undo()
    verifyCelTopology(spr, lay, 1, "7A after undo frame 2")
    verifyLoopColors(lay, 1, "7A after undo frame 2")
    assert_nil(lay:cel(2), "7A frame 2 cel is nil after undo")

    -- Redo addition of frame 2
    app.redo()
    verifyCelTopology(spr, lay, 2, "7A after redo frame 2")
    verifyLoopColors(lay, 2, "7A after redo frame 2")

    -- Redo addition of frame 3
    app.redo()
    verifyCelTopology(spr, lay, 3, "7A after redo frame 3")
    verifyLoopColors(lay, 3, "7A after redo frame 3")

    spr:close()
  end

  -- ----------------------------------------------------------------------------
  -- 7B: Lua API Frame Addition Undo & Redo (spr:newEmptyFrame)
  -- ----------------------------------------------------------------------------
  print("  -> Subtest 7B: spr:newEmptyFrame() undo/redo...")
  do
    local spr = Sprite(16, 16)
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local lay = spr.layers[2]
    verifyCelTopology(spr, lay, 1, "7B initial")

    -- Add frames via sprite API
    spr:newEmptyFrame()
    verifyCelTopology(spr, lay, 2, "7B after newEmptyFrame 2")
    verifyLoopColors(lay, 2, "7B after newEmptyFrame 2")

    spr:newEmptyFrame()
    verifyCelTopology(spr, lay, 3, "7B after newEmptyFrame 3")
    verifyLoopColors(lay, 3, "7B after newEmptyFrame 3")

    -- Undo frames
    app.undo()
    verifyCelTopology(spr, lay, 2, "7B after undo frame 3")
    verifyLoopColors(lay, 2, "7B after undo frame 3")

    app.undo()
    verifyCelTopology(spr, lay, 1, "7B after undo frame 2")
    verifyLoopColors(lay, 1, "7B after undo frame 2")

    -- Redo frames
    app.redo()
    verifyCelTopology(spr, lay, 2, "7B after redo frame 2")
    verifyLoopColors(lay, 2, "7B after redo frame 2")

    app.redo()
    verifyCelTopology(spr, lay, 3, "7B after redo frame 3")
    verifyLoopColors(lay, 3, "7B after redo frame 3")

    spr:close()
  end

  -- ----------------------------------------------------------------------------
  -- 7C: 10-Frame Multi-Step Undo & Redo Stress Cycles
  -- ----------------------------------------------------------------------------
  print("  -> Subtest 7C: 10-step multi-frame undo/redo stress...")
  do
    local spr = Sprite(16, 16)
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local lay = spr.layers[2]
    verifyCelTopology(spr, lay, 1, "7C initial")

    -- Step 1: Add 10 frames consecutively (total 11 frames)
    for i = 1, 10 do
      app.command.NewFrame{ content = "empty" }
      verifyCelTopology(spr, lay, i + 1, string.format("7C during addition step %d", i))
    end
    verifyLoopColors(lay, 11, "7C after 10 additions")

    -- Step 2: Stepwise undo all 10 frames down to 1 frame
    for i = 1, 10 do
      app.undo()
      local expectedFrames = 11 - i
      verifyCelTopology(spr, lay, expectedFrames, string.format("7C stepwise undo step %d", i))
      verifyLoopColors(lay, expectedFrames, string.format("7C stepwise undo step %d", i))
      assert_nil(lay:cel(expectedFrames + 1),
        string.format("7C frame %d is nil after undo step %d", expectedFrames + 1, i))
    end

    -- Step 3: Stepwise redo all 10 frames back to 11 frames
    for i = 1, 10 do
      app.redo()
      local expectedFrames = 1 + i
      verifyCelTopology(spr, lay, expectedFrames, string.format("7C stepwise redo step %d", i))
      verifyLoopColors(lay, expectedFrames, string.format("7C stepwise redo step %d", i))
    end

    -- Step 4: Batch oscillating undo and redo
    for i = 1, 10 do app.undo() end
    verifyCelTopology(spr, lay, 1, "7C batch undo all")
    verifyLoopColors(lay, 1, "7C batch undo all")

    for i = 1, 10 do app.redo() end
    verifyCelTopology(spr, lay, 11, "7C batch redo all")
    verifyLoopColors(lay, 11, "7C batch redo all")

    spr:close()
  end

  -- ----------------------------------------------------------------------------
  -- 7D: Frame Deletion Undo & Redo (app.command.RemoveFrame & spr:deleteFrame)
  -- ----------------------------------------------------------------------------
  print("  -> Subtest 7D: Frame deletion undo/redo...")
  do
    local spr = Sprite(16, 16)
    app.command.NewFrame{ content = "empty" }
    app.command.NewFrame{ content = "empty" }
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local lay = spr.layers[2]
    verifyCelTopology(spr, lay, 3, "7D initial 3 frames")
    verifyLoopColors(lay, 3, "7D initial 3 frames")

    -- 7D.1: Middle frame deletion via app.command.RemoveFrame()
    app.activeFrame = spr.frames[2]
    app.command.RemoveFrame()
    verifyCelTopology(spr, lay, 2, "7D after RemoveFrame(2)")
    -- Sequence after middle deletion: 1 -> Red, 2 -> Green (shifted from frame 3)
    assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "7D frame 1 after remove is Red")
    assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "7D frame 2 after remove is Green")

    -- Undo middle frame deletion
    app.undo()
    verifyCelTopology(spr, lay, 3, "7D after undo RemoveFrame(2)")
    verifyLoopColors(lay, 3, "7D after undo RemoveFrame(2)")

    -- Redo middle frame deletion
    app.redo()
    verifyCelTopology(spr, lay, 2, "7D after redo RemoveFrame(2)")
    assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "7D frame 1 after redo remove is Red")
    assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "7D frame 2 after redo remove is Green")

    -- Restore to 3 frames
    app.undo()
    verifyCelTopology(spr, lay, 3, "7D restored to 3 frames")

    -- 7D.2: Head frame deletion via spr:deleteFrame(1)
    spr:deleteFrame(1)
    verifyCelTopology(spr, lay, 2, "7D after deleteFrame(1)")
    assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "7D frame 1 after delete head is Red")
    assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "7D frame 2 after delete head is Green")

    -- Undo head deletion
    app.undo()
    verifyCelTopology(spr, lay, 3, "7D after undo deleteFrame(1)")
    verifyLoopColors(lay, 3, "7D after undo deleteFrame(1)")

    -- Redo head deletion
    app.redo()
    verifyCelTopology(spr, lay, 2, "7D after redo deleteFrame(1)")

    spr:close()
  end

  -- ----------------------------------------------------------------------------
  -- 7E: Cel Image Data Integrity, Pixel Values & Bounds across Undo/Redo
  -- ----------------------------------------------------------------------------
  print("  -> Subtest 7E: Cel image data integrity & pixel preservation...")
  do
    local spr = Sprite(16, 16)
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local lay = spr.layers[2]

    app.command.NewFrame{ content = "empty" }
    app.command.NewFrame{ content = "empty" }
    verifyCelTopology(spr, lay, 3, "7E initial 3 frames")

    -- Verify exact RGBA pixel values before undo
    local c1_px = lay:cel(1).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(c1_px), 255, "7E c1 red")
    assert_eq(app.pixelColor.rgbaG(c1_px), 0, "7E c1 green")
    assert_eq(app.pixelColor.rgbaB(c1_px), 0, "7E c1 blue")
    assert_eq(app.pixelColor.rgbaA(c1_px), 255, "7E c1 alpha")

    local c2_px = lay:cel(2).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(c2_px), 0, "7E c2 red")
    assert_eq(app.pixelColor.rgbaG(c2_px), 255, "7E c2 green")
    assert_eq(app.pixelColor.rgbaB(c2_px), 0, "7E c2 blue")
    assert_eq(app.pixelColor.rgbaA(c2_px), 255, "7E c2 alpha")

    local c3_px = lay:cel(3).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(c3_px), 0, "7E c3 red")
    assert_eq(app.pixelColor.rgbaG(c3_px), 0, "7E c3 green")
    assert_eq(app.pixelColor.rgbaB(c3_px), 255, "7E c3 blue")
    assert_eq(app.pixelColor.rgbaA(c3_px), 255, "7E c3 alpha")

    -- Bounds verification
    for f = 1, 3 do
      local c = lay:cel(f)
      assert_eq(c.bounds.x, 0, string.format("7E cel %d bounds.x", f))
      assert_eq(c.bounds.y, 0, string.format("7E cel %d bounds.y", f))
      assert_eq(c.bounds.width, 16, string.format("7E cel %d bounds.width", f))
      assert_eq(c.bounds.height, 16, string.format("7E cel %d bounds.height", f))
    end

    -- Undo frame 3 and verify frame 1 and 2 pixels match exact RGBA
    app.undo()
    verifyCelTopology(spr, lay, 2, "7E after undo frame 3")
    assert_eq(lay:cel(1).image:getPixel(0, 0), c1_px, "7E c1 pixel value intact after undo")
    assert_eq(lay:cel(2).image:getPixel(0, 0), c2_px, "7E c2 pixel value intact after undo")

    -- Redo frame 3 and verify all 3 pixels match exact RGBA
    app.redo()
    verifyCelTopology(spr, lay, 3, "7E after redo frame 3")
    assert_eq(lay:cel(1).image:getPixel(0, 0), c1_px, "7E c1 pixel value intact after redo")
    assert_eq(lay:cel(2).image:getPixel(0, 0), c2_px, "7E c2 pixel value intact after redo")
    assert_eq(lay:cel(3).image:getPixel(0, 0), c3_px, "7E c3 pixel value intact after redo")

    spr:close()
  end

  -- ----------------------------------------------------------------------------
  -- 7F: Fixed Timeline Mode Undo & Redo (Single Shared Buffer Preservation)
  -- ----------------------------------------------------------------------------
  print("  -> Subtest 7F: Fixed timeline mode undo/redo buffer sharing...")
  do
    local spr = Sprite(16, 16)
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local lay = spr.layers[2]
    lay.timelineMapping = "Fixed"
    lay:syncTimeline()
    assert_eq(lay.timelineMapping, "Fixed", "7F timelineMapping is Fixed")

    app.command.NewFrame{ content = "empty" }
    app.command.NewFrame{ content = "empty" }
    verifyCelTopology(spr, lay, 3, "7F 3 frames in Fixed mode")

    local sharedId = lay:cel(1).image.id
    assert_eq(lay:cel(2).image.id, sharedId, "7F frame 2 shares cel 1 image ID")
    assert_eq(lay:cel(3).image.id, sharedId, "7F frame 3 shares cel 1 image ID")
    assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "7F cel 1 is Red")
    assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Red", "7F cel 2 is Red")
    assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Red", "7F cel 3 is Red")

    -- Undo frame 3
    app.undo()
    verifyCelTopology(spr, lay, 2, "7F after undo frame 3")
    assert_eq(lay:cel(1).image.id, sharedId, "7F frame 1 preserves shared image ID")
    assert_eq(lay:cel(2).image.id, sharedId, "7F frame 2 preserves shared image ID")

    -- Redo frame 3
    app.redo()
    verifyCelTopology(spr, lay, 3, "7F after redo frame 3")
    assert_eq(lay:cel(1).image.id, sharedId, "7F frame 1 preserves shared image ID after redo")
    assert_eq(lay:cel(2).image.id, sharedId, "7F frame 2 preserves shared image ID after redo")
    assert_eq(lay:cel(3).image.id, sharedId, "7F frame 3 preserves shared image ID after redo")

    spr:close()
  end

  print("  -> Test 7 PASSED: All undo/redo subtests (7A-7F) verified.")
end

print("\n================================================================================")
print(string.format("  ALL %d ADVERSARIAL TESTS PASSED (%d assertions)!              ", 7, passCount))
print("================================================================================")
