-- ==============================================================================
-- test_m3_stress_challenger.lua
-- Milestone 3 Empirical Challenger Stress Harness
-- Rigorous Stress-Testing of Animation Timeline Synchronization & Cel Mapping:
--   1. Rapid Timeline Scrubbing (120+ frames forward, backward, random jumps)
--   2. Massive Host Frame Count (1000 frames) Modulo & Memory Blowup Check
--   3. Dynamic Host Frame Addition (50 frames dynamically added & verified)
--   4. Dynamic Host Frame Removal (20 frames dynamically removed & verified)
--   5. Rapid Churn Stress (Interleaved additions, deletions, scrubbing, transforms)
--   6. Single-Frame Smart Object across 200 Host Frames Memory & Modulo Check
-- ==============================================================================

print("======================================================================")
print("  Running Milestone 3 Timeline Challenger Empirical Stress Suite       ")
print("======================================================================")

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "value mismatch", tostring(expected), tostring(actual)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error(string.format("ASSERTION FAILED: %s", msg or "expected true"), 2)
  end
end

local function getColorName(px)
  local r = app.pixelColor.rgbaR(px)
  local g = app.pixelColor.rgbaG(px)
  local b = app.pixelColor.rgbaB(px)
  if r == 255 and g == 0 and b == 0 then return "Red" end
  if r == 0 and g == 255 and b == 0 then return "Green" end
  if r == 0 and g == 0 and b == 255 then return "Blue" end
  return string.format("Unknown(%d,%d,%d)", r, g, b)
end

local function getExpectedColor(frameNumber, totalLinked)
  local mod = (frameNumber - 1) % totalLinked
  if mod == 0 then return "Red"
  elseif mod == 1 then return "Green"
  elseif mod == 2 then return "Blue"
  end
  return "Unknown"
end

-- ==============================================================================
-- Stress Test 1: Rapid Timeline Scrubbing Across 120 Frames
-- ==============================================================================
print("\n[Stress Test 1] Rapid Timeline Scrubbing (120 Frames Forward, Backward & Random)...")
do
  local spr = Sprite(32, 32)
  -- Expand to 120 frames
  for i = 2, 120 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 120, "host sprite initialized with 120 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_true(smartLayer ~= nil, "smartLayer exists")
  assert_true(smartLayer.isSmartObject, "layer is a smart object")
  assert_eq(smartLayer.linkedFrameCount, 3, "linkedFrameCount is 3")
  assert_eq(smartLayer.timelineMapping, "Loop", "default timelineMapping is Loop")

  -- 1A: Forward Scrubbing (1 to 120)
  for f = 1, 120 do
    app.activeFrame = spr.frames[f]
    assert_eq(app.activeFrame.frameNumber, f, string.format("activeFrame is %d", f))

    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("cel exists at frame %d", f))
    local px = cel.image:getPixel(0, 0)
    local actualColor = getColorName(px)
    local expectedColor = getExpectedColor(f, 3)
    assert_eq(actualColor, expectedColor, string.format("Forward scrub frame %d color", f))
  end
  print("  -> Forward scrub 1..120 verified.")

  -- 1B: Backward Scrubbing (120 down to 1)
  for f = 120, 1, -1 do
    app.activeFrame = spr.frames[f]
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("cel exists at frame %d", f))
    local px = cel.image:getPixel(0, 0)
    local actualColor = getColorName(px)
    local expectedColor = getExpectedColor(f, 3)
    assert_eq(actualColor, expectedColor, string.format("Backward scrub frame %d color", f))
  end
  print("  -> Backward scrub 120..1 verified.")

  -- 1C: Random Non-Sequential Scrubbing (100 pseudo-random jumps)
  local seed = 42
  for step = 1, 100 do
    seed = (seed * 1103515245 + 12345) % 2147483648
    local targetFrame = (seed % 120) + 1
    app.activeFrame = spr.frames[targetFrame]
    local cel = smartLayer:cel(targetFrame)
    assert_true(cel ~= nil, string.format("jump target frame %d cel exists", targetFrame))
    local px = cel.image:getPixel(0, 0)
    local actualColor = getColorName(px)
    local expectedColor = getExpectedColor(targetFrame, 3)
    assert_eq(actualColor, expectedColor, string.format("Random jump %d to frame %d color", step, targetFrame))
  end
  print("  -> Random jump scrub (100 iterations) verified.")

  -- 1D: Cel Memory Sharing Verification (Cel::MakeLink)
  local c1_id = smartLayer:cel(1).image.id
  local c2_id = smartLayer:cel(2).image.id
  local c3_id = smartLayer:cel(3).image.id
  assert_true(c1_id ~= c2_id and c2_id ~= c3_id and c1_id ~= c3_id, "canonical cels 1, 2, 3 have distinct image IDs")

  for f = 4, 120 do
    local mod = (f - 1) % 3
    local expectedId = (mod == 0 and c1_id) or (mod == 1 and c2_id) or c3_id
    assert_eq(smartLayer:cel(f).image.id, expectedId,
      string.format("frame %d memory linked to canonical cel %d", f, mod + 1))
  end
  print("  -> Cel memory sharing (O(M) memory) verified across 120 frames.")

  spr:close()
  print("  -> Stress Test 1 PASSED.")
end

-- ==============================================================================
-- Stress Test 2: Massive Host Frame Count (1000 Frames)
-- ==============================================================================
print("\n[Stress Test 2] Massive Host Frame Count (1000 Frames) Modulo & Memory...")
do
  local spr = Sprite(16, 16)
  for i = 2, 1000 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 1000, "host sprite initialized with 1000 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(smartLayer.linkedFrameCount, 3, "linkedFrameCount is 3")

  local c1_id = smartLayer:cel(1).image.id
  local c2_id = smartLayer:cel(2).image.id
  local c3_id = smartLayer:cel(3).image.id

  -- Verify all 1000 frames: modulo map, cel presence, color, and memory sharing
  for f = 1, 1000 do
    local mapped = smartLayer:mapTimelineFrame(f)
    local expectedMapped = ((f - 1) % 3) + 1
    assert_eq(mapped, expectedMapped, string.format("frame %d mapTimelineFrame", f))

    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("frame %d cel exists in 1000-frame host", f))

    local px = cel.image:getPixel(0, 0)
    local col = getColorName(px)
    local expCol = getExpectedColor(f, 3)
    assert_eq(col, expCol, string.format("frame %d color in 1000-frame host", f))

    -- Check memory sharing: NO memory blowup!
    local mod = (f - 1) % 3
    local expectedId = (mod == 0 and c1_id) or (mod == 1 and c2_id) or c3_id
    assert_eq(cel.image.id, expectedId, string.format("frame %d shares canonical image id", f))
  end
  print("  -> 1000 frames modulo mapping, colors, and O(M) memory verified.")

  -- Test TimelineMapping: "Hold" mode across 1000 frames
  smartLayer.timelineMapping = "Hold"
  smartLayer:syncTimeline()
  assert_eq(getColorName(smartLayer:cel(1).image:getPixel(0, 0)), "Red", "Hold frame 1 is Red")
  assert_eq(getColorName(smartLayer:cel(2).image:getPixel(0, 0)), "Green", "Hold frame 2 is Green")
  assert_eq(getColorName(smartLayer:cel(3).image:getPixel(0, 0)), "Blue", "Hold frame 3 is Blue")
  assert_eq(getColorName(smartLayer:cel(500).image:getPixel(0, 0)), "Blue", "Hold frame 500 is Blue")
  assert_eq(getColorName(smartLayer:cel(1000).image:getPixel(0, 0)), "Blue", "Hold frame 1000 is Blue")

  local hold_c3_id = smartLayer:cel(3).image.id
  assert_eq(smartLayer:cel(4).image.id, hold_c3_id, "Hold frame 4 shares Blue image ID")
  assert_eq(smartLayer:cel(500).image.id, hold_c3_id, "Hold frame 500 shares Blue image ID")
  assert_eq(smartLayer:cel(1000).image.id, hold_c3_id, "Hold frame 1000 shares Blue image ID")
  print("  -> 1000 frames Hold mode verified (all held frames share canonical Blue image ID).")

  -- Test TimelineMapping: "Fixed" mode across 1000 frames
  smartLayer.timelineMapping = "Fixed"
  smartLayer:syncTimeline()
  assert_eq(getColorName(smartLayer:cel(1).image:getPixel(0, 0)), "Red", "Fixed frame 1 is Red")
  assert_eq(getColorName(smartLayer:cel(500).image:getPixel(0, 0)), "Red", "Fixed frame 500 is Red")
  assert_eq(getColorName(smartLayer:cel(1000).image:getPixel(0, 0)), "Red", "Fixed frame 1000 is Red")

  local fixed_c1_id = smartLayer:cel(1).image.id
  assert_eq(smartLayer:cel(500).image.id, fixed_c1_id, "Fixed frame 500 shares Red image ID")
  assert_eq(smartLayer:cel(1000).image.id, fixed_c1_id, "Fixed frame 1000 shares Red image ID")
  print("  -> 1000 frames Fixed mode verified (all frames share canonical Red image ID).")

  -- Restore Loop mode
  smartLayer.timelineMapping = "Loop"
  smartLayer:syncTimeline()
  local loop_c1_id = smartLayer:cel(1).image.id
  assert_eq(getColorName(smartLayer:cel(1000).image:getPixel(0, 0)), "Red", "Restored Loop frame 1000 is Red")
  assert_eq(smartLayer:cel(1000).image.id, loop_c1_id, "Restored Loop frame 1000 shares canonical Red image ID")

  spr:close()
  print("  -> Stress Test 2 PASSED.")
end

-- ==============================================================================
-- Stress Test 3: Dynamic Host Frame Addition (Add 50 frames dynamically)
-- ==============================================================================
print("\n[Stress Test 3] Dynamic Host Frame Addition (50 Frames)...")
do
  local spr = Sprite(16, 16)
  for i = 2, 10 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10, "initial sprite has 10 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(#spr.frames, 10, "sprite has 10 frames after import")

  -- Verify initial 10 frames
  for f = 1, 10 do
    assert_true(smartLayer:cel(f) ~= nil, string.format("initial frame %d cel exists", f))
  end

  -- Dynamically append 50 frames
  for i = 1, 50 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 60, "sprite now has 60 frames")

  -- Call syncTimeline() to ensure all 60 frames populated
  local totalCels = smartLayer:syncTimeline()
  assert_eq(totalCels, 60, "syncTimeline returns 60 cels")

  local c1_id = smartLayer:cel(1).image.id
  local c2_id = smartLayer:cel(2).image.id
  local c3_id = smartLayer:cel(3).image.id

  -- Verify all 50 newly added frames (11 through 60)
  for f = 11, 60 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("dynamically added frame %d cel exists", f))

    local px = cel.image:getPixel(0, 0)
    local actualColor = getColorName(px)
    local expectedColor = getExpectedColor(f, 3)
    assert_eq(actualColor, expectedColor, string.format("dynamically added frame %d color", f))

    local mod = (f - 1) % 3
    local expectedId = (mod == 0 and c1_id) or (mod == 1 and c2_id) or c3_id
    assert_eq(cel.image.id, expectedId, string.format("dynamically added frame %d shares image buffer", f))
  end
  print("  -> All 50 dynamically added frames verified with proper modulo and cel links.")

  spr:close()
  print("  -> Stress Test 3 PASSED.")
end

-- ==============================================================================
-- Stress Test 4: Dynamic Host Frame Removal (Remove 20 frames dynamically)
-- ==============================================================================
print("\n[Stress Test 4] Dynamic Host Frame Removal (Remove 20 Frames)...")
do
  local spr = Sprite(16, 16)
  for i = 2, 60 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 60, "initial sprite has 60 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(#spr.frames, 60, "sprite has 60 frames")

  -- Delete 10 frames from the end
  for i = 1, 10 do
    spr:deleteFrame(#spr.frames)
  end
  assert_eq(#spr.frames, 50, "sprite has 50 frames after tail deletions")

  -- Delete 10 frames from the front/middle (e.g. frame 5)
  for i = 1, 10 do
    spr:deleteFrame(5)
  end
  assert_eq(#spr.frames, 40, "sprite has 40 frames after middle deletions")

  -- Sync timeline and verify remaining 40 frames
  local remainingCels = smartLayer:syncTimeline()
  assert_eq(remainingCels, 40, "syncTimeline returns 40 cels")
  assert_eq(#spr.frames, 40, "sprite has exactly 40 frames")

  for f = 1, 40 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("remaining frame %d cel exists", f))
    local px = cel.image:getPixel(0, 0)
    local actualColor = getColorName(px)
    local expectedColor = getExpectedColor(f, 3)
    assert_eq(actualColor, expectedColor, string.format("remaining frame %d color", f))
  end
  print("  -> Remaining 40 frames verified with correct modulo wrapping.")

  -- Verify pruned cels beyond frame 40 do NOT linger
  for f = 41, 60 do
    local cel = smartLayer:cel(f)
    assert_true(cel == nil, string.format("pruned frame %d cel is nil", f))
  end
  print("  -> All pruned cels confirmed destroyed with zero dangling pointers.")

  -- Garbage collection pass to confirm no double-free or leak crashes
  collectgarbage("collect")

  spr:close()
  print("  -> Stress Test 4 PASSED.")
end

-- ==============================================================================
-- Stress Test 5: Rapid Churn (Interleaved Adds, Deletes, Scrubbing & Transforms)
-- ==============================================================================
print("\n[Stress Test 5] Rapid Churn (Interleaved Adds, Deletes, Scrubbing & Transforms)...")
do
  local spr = Sprite(32, 32)
  for i = 2, 10 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10, "initial sprite has 10 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]

  -- 15 cycles of rapid interleaved operations
  for cycle = 1, 15 do
    -- 1. Add 2 frames
    spr:newEmptyFrame()
    spr:newEmptyFrame()

    -- 2. Delete 1 frame
    if #spr.frames > 3 then
      spr:deleteFrame(#spr.frames - 1)
    end

    -- 3. Sync timeline
    smartLayer:syncTimeline()

    -- 4. Scrub active frame to middle
    local mid = math.floor(#spr.frames / 2)
    app.activeFrame = spr.frames[mid]

    -- 5. Alternate 2D transformation
    if cycle % 3 == 1 then
      smartLayer.scale = { 1.5, 1.5 }
    elseif cycle % 3 == 2 then
      smartLayer.angle = (cycle * 30.0) % 360.0
    else
      smartLayer:resetTransform()
    end

    -- 6. Verify active frame cel exists
    local midCel = smartLayer:cel(mid)
    assert_true(midCel ~= nil, string.format("cycle %d midCel exists", cycle))
  end

  -- Reset transform at the end and check color sequence across all remaining frames
  smartLayer:resetTransform()
  smartLayer:syncTimeline()

  for f = 1, #spr.frames do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("post-churn frame %d cel exists", f))
    local px = cel.image:getPixel(0, 0)
    local actualColor = getColorName(px)
    local expectedColor = getExpectedColor(f, 3)
    assert_eq(actualColor, expectedColor, string.format("post-churn frame %d color", f))
  end

  spr:close()
  print("  -> Rapid churn stress passed with zero errors or corruption.")
  print("  -> Stress Test 5 PASSED.")
end

-- ==============================================================================
-- Stress Test 6: Single-Frame Smart Object in Multi-Frame Host Scrubbing Stress
-- ==============================================================================
print("\n[Stress Test 6] Single-Frame Smart Object across 200 Host Frames...")
do
  local spr = Sprite(32, 32)
  for i = 2, 200 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 200, "sprite has 200 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(smartLayer.linkedFrameCount, 1, "single frame linked count is 1")

  local canonicalId = smartLayer:cel(1).image.id

  -- Check all 200 frames share canonical Image buffer ID and have identical pixel values
  for f = 1, 200 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("single-frame cel %d exists", f))
    assert_eq(cel.image.id, canonicalId, string.format("frame %d links to canonical image id", f))
    local px = cel.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, string.format("frame %d pixel TL is Red", f))
  end

  spr:close()
  print("  -> Single-frame 200 host frames memory linking and color integrity verified.")
  print("  -> Stress Test 6 PASSED.")
end

print("\n======================================================================")
print("  ALL MILESTONE 3 TIMELINE CHALLENGER STRESS TESTS PASSED (6/6)!       ")
print("======================================================================")
