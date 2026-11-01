-- tests/smart_object/challenger_m3_it4_timeline_invariants.lua
-- ==============================================================================
-- Empirical Challenger Stress Harness for Timeline Invariants (M3 It4)
-- Tests:
--   1. Timeline invariants across Loop, Hold, Fixed modes
--   2. Cel uniqueness (#layer.cels == #sprite.frames, 0 duplicates, 0 missing)
--   3. Zero buffer churn in Fixed and Hold modes (shared canonical image buffers)
--   4. Frame duplication / insertion modes (content="copy", NewFrame, etc.)
--   5. High-frequency mode transitions under aggressive timeline mutations
--   6. Deep Undo / Redo symmetry across structural timeline operations
-- ==============================================================================

print("================================================================================")
print("  EMPIRICAL CHALLENGER: TIMELINE INVARIANTS, CEL UNIQUENESS & ZERO CHURN        ")
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
  if r == 255 and g == 255 and b == 0 then return "Yellow" end
  return string.format("Unknown(R=%d,G=%d,B=%d)", r, g, b)
end

-- Invariant checker: Verifies cel uniqueness, ordering, and topology
local function checkCelUniqueness(spr, lay, label)
  local totalFrames = #spr.frames
  assert_eq(#lay.cels, totalFrames, string.format("[%s] #lay.cels == #spr.frames (%d)", label, totalFrames))

  local seen = {}
  for i, c in ipairs(lay.cels) do
    assert_true(c ~= nil, string.format("[%s] cel #%d is not nil", label, i))
    local fn = c.frameNumber
    assert_true(fn >= 1 and fn <= totalFrames,
      string.format("[%s] cel #%d frameNumber %d out of bounds [1, %d]", label, i, fn, totalFrames))
    assert_true(seen[fn] == nil,
      string.format("[%s] duplicate cel found for frameNumber %d at index %d (previously at %s)",
        label, fn, i, tostring(seen[fn])))
    seen[fn] = i
  end

  for f = 1, totalFrames do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("[%s] lay:cel(%d) must exist", label, f))
    assert_eq(c.frameNumber, f, string.format("[%s] lay:cel(%d).frameNumber must match frame index", label, f))
  end
end

-- ==============================================================================
-- Test 1: Cel Uniqueness Under Extreme Frame Addition & Deletion Sequences
-- ==============================================================================
print("\n[Challenger 1] Cel Uniqueness Under Extreme Frame Additions/Deletions...")
do
  local spr = Sprite(32, 32)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  checkCelUniqueness(spr, lay, "Init 1 frame")

  -- Add frames via various APIs up to 20 frames
  for i = 2, 8 do
    spr:newEmptyFrame()
    checkCelUniqueness(spr, lay, "spr:newEmptyFrame " .. i)
  end

  for i = 9, 14 do
    app.command.NewFrame()
    checkCelUniqueness(spr, lay, "app.command.NewFrame " .. i)
  end

  for i = 15, 20 do
    app.command.NewFrame{ content = "copy" }
    checkCelUniqueness(spr, lay, "app.command.NewFrame copy " .. i)
  end

  assert_eq(#spr.frames, 20)
  assert_eq(#lay.cels, 20)

  -- Delete from Tail down to 15
  for d = 20, 16, -1 do
    spr:deleteFrame(d)
    checkCelUniqueness(spr, lay, "delete tail " .. d)
  end

  -- Delete from Head 5 times (15 down to 10)
  for d = 1, 5 do
    spr:deleteFrame(1)
    checkCelUniqueness(spr, lay, "delete head " .. d)
  end

  -- Delete from Middle (frame 3) 5 times (10 down to 5)
  for d = 1, 5 do
    spr:deleteFrame(3)
    checkCelUniqueness(spr, lay, "delete mid " .. d)
  end

  -- Delete down to 1 frame
  while #spr.frames > 1 do
    spr:deleteFrame(#spr.frames)
    checkCelUniqueness(spr, lay, "delete down to 1 frame (at " .. #spr.frames .. ")")
  end

  assert_eq(#spr.frames, 1)
  assert_eq(#lay.cels, 1)
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")

  spr:close()
  print("  -> Challenger 1 PASSED: Cel uniqueness strictly maintained through extreme mutations.")
end

-- ==============================================================================
-- Test 2: Zero Buffer Churn & Canonical Buffer Sharing in Fixed Mode
-- ==============================================================================
print("\n[Challenger 2] Zero Buffer Churn & Buffer Sharing in Fixed Mode...")
do
  local spr = Sprite(32, 32)
  for i = 1, 29 do spr:newEmptyFrame() end
  assert_eq(#spr.frames, 30)

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()

  checkCelUniqueness(spr, lay, "Fixed init 30f")

  local canonicalImageId = lay:cel(1).image.id
  local uniqueBuffers = {}

  for f = 1, 30 do
    local c = lay:cel(f)
    assert_eq(c.image.id, canonicalImageId, string.format("Fixed frame %d shares canonical ID", f))
    assert_eq(getColorName(c.image:getPixel(0, 0)), "Red", string.format("Fixed frame %d is Red", f))
    uniqueBuffers[c.image.id] = true
  end

  local countUnique = 0
  for _ in pairs(uniqueBuffers) do countUnique = countUnique + 1 end
  assert_eq(countUnique, 1, "Exactly 1 buffer used across 30 frames in Fixed mode")

  -- Test zero churn over 20 repeated syncTimeline calls
  for iter = 1, 20 do
    lay:syncTimeline()
    assert_eq(lay:cel(1).image.id, canonicalImageId, "Canonical buffer ID must not churn on syncTimeline")
    assert_eq(lay:cel(30).image.id, canonicalImageId, "Last frame buffer ID must not churn on syncTimeline")
  end

  -- Add 10 more frames in Fixed mode
  for i = 1, 10 do
    app.command.NewFrame{ content = "copy" }
  end
  assert_eq(#spr.frames, 40)
  checkCelUniqueness(spr, lay, "Fixed after 10 NewFrame")

  for f = 1, 40 do
    assert_eq(lay:cel(f).image.id, canonicalImageId, string.format("Frame %d shares canonical ID after add", f))
  end

  -- Delete 15 frames in Fixed mode
  for i = 1, 15 do
    spr:deleteFrame(2) -- delete frame 2 repeatedly
  end
  assert_eq(#spr.frames, 25)
  checkCelUniqueness(spr, lay, "Fixed after 15 deletes")

  for f = 1, 25 do
    assert_eq(lay:cel(f).image.id, canonicalImageId, string.format("Frame %d shares canonical ID after delete", f))
  end

  spr:close()
  print("  -> Challenger 2 PASSED: Fixed mode verified for 1-buffer sharing and zero churn.")
end

-- ==============================================================================
-- Test 3: Zero Buffer Churn & Buffer Sharing in Hold Mode
-- ==============================================================================
print("\n[Challenger 3] Zero Buffer Churn & Buffer Sharing in Hold Mode...")
do
  local spr = Sprite(32, 32)
  for i = 1, 19 do spr:newEmptyFrame() end
  assert_eq(#spr.frames, 20)

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  lay.timelineMapping = "Hold"
  lay:syncTimeline()

  checkCelUniqueness(spr, lay, "Hold init 20f")

  local idF1 = lay:cel(1).image.id
  local idF2 = lay:cel(2).image.id
  local idF3 = lay:cel(3).image.id

  -- Ensure 3 distinct canonical buffers
  assert_true(idF1 ~= idF2 and idF2 ~= idF3 and idF1 ~= idF3, "Hold frames 1, 2, 3 have distinct buffers")

  -- Frames 4..20 must all hold frame 3's buffer (Blue)
  local uniqueBuffers = {}
  for f = 1, 20 do
    local c = lay:cel(f)
    local expectedId = (f == 1 and idF1) or (f == 2 and idF2) or idF3
    local expectedColor = (f == 1 and "Red") or (f == 2 and "Green") or "Blue"
    assert_eq(c.image.id, expectedId, string.format("Hold frame %d buffer ID matches expected", f))
    assert_eq(getColorName(c.image:getPixel(0, 0)), expectedColor, string.format("Hold frame %d pixel color", f))
    uniqueBuffers[c.image.id] = true
  end

  local countUnique = 0
  for _ in pairs(uniqueBuffers) do countUnique = countUnique + 1 end
  assert_eq(countUnique, 3, "Exactly 3 unique buffers used across 20 frames in Hold mode")

  -- Zero churn test: Repeated syncTimeline preserves buffer IDs
  for iter = 1, 10 do
    lay:syncTimeline()
    assert_eq(lay:cel(1).image.id, idF1, "Hold cel 1 ID preserved across sync")
    assert_eq(lay:cel(2).image.id, idF2, "Hold cel 2 ID preserved across sync")
    assert_eq(lay:cel(3).image.id, idF3, "Hold cel 3 ID preserved across sync")
    assert_eq(lay:cel(20).image.id, idF3, "Hold cel 20 ID preserved across sync")
  end

  spr:close()
  print("  -> Challenger 3 PASSED: Hold mode canonical sharing and zero churn verified.")
end

-- ==============================================================================
-- Test 4: NewFrame Synchronous Rendering Across All Modes (Defect 3 Verification)
-- ==============================================================================
print("\n[Challenger 4] NewFrame Duplication & Rendering Across Playback Modes...")
do
  -- 4A: Loop Mode modulo color sequence: Red -> Green -> Blue -> Red -> Green -> Blue
  local sprLoop = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layLoop = sprLoop.layers[2]
  assert_eq(layLoop.timelineMapping, "Loop")

  local expectedColors = { "Red", "Green", "Blue", "Red", "Green", "Blue", "Red", "Green", "Blue" }
  for f = 2, 9 do
    -- Alternate addition methods
    if f % 2 == 0 then
      app.command.NewFrame{ content = "copy" }
    else
      app.command.NewFrame()
    end
    checkCelUniqueness(sprLoop, layLoop, "Loop add frame " .. f)
    for checkF = 1, f do
      assert_eq(getColorName(layLoop:cel(checkF).image:getPixel(0, 0)), expectedColors[checkF],
        string.format("Loop after add %d: frame %d color", f, checkF))
    end
  end

  sprLoop:close()

  -- 4B: Fixed Mode color sequence: All frames Red
  local sprFixed = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layFixed = sprFixed.layers[2]
  layFixed.timelineMapping = "Fixed"
  layFixed:syncTimeline()

  for f = 2, 7 do
    app.command.NewFrame{ content = "copy" }
    checkCelUniqueness(sprFixed, layFixed, "Fixed add frame " .. f)
    for checkF = 1, f do
      assert_eq(getColorName(layFixed:cel(checkF).image:getPixel(0, 0)), "Red",
        string.format("Fixed after add %d: frame %d is Red", f, checkF))
    end
  end

  sprFixed:close()

  -- 4C: Hold Mode color sequence: Red, Green, Blue, Blue, Blue, Blue
  local sprHold = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layHold = sprHold.layers[2]
  layHold.timelineMapping = "Hold"
  layHold:syncTimeline()

  local expectedHoldColors = { "Red", "Green", "Blue", "Blue", "Blue", "Blue" }
  for f = 2, 6 do
    app.command.NewFrame{ content = "copy" }
    checkCelUniqueness(sprHold, layHold, "Hold add frame " .. f)
    for checkF = 1, f do
      assert_eq(getColorName(layHold:cel(checkF).image:getPixel(0, 0)), expectedHoldColors[checkF],
        string.format("Hold after add %d: frame %d color", f, checkF))
    end
  end

  sprHold:close()
  print("  -> Challenger 4 PASSED: NewFrame rendering and timeline synchronization verified.")
end

-- ==============================================================================
-- Test 5: Dynamic Mode Transitions Interleaved with Frame Mutations
-- ==============================================================================
print("\n[Challenger 5] Dynamic Mode Transitions Interleaved with Mutations...")
do
  local spr = Sprite(32, 32)
  for i = 1, 9 do spr:newEmptyFrame() end
  assert_eq(#spr.frames, 10)

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local modes = { "Loop", "Hold", "Fixed" }
  for cycle = 1, 30 do
    local mode = modes[(cycle % 3) + 1]
    lay.timelineMapping = mode
    lay:syncTimeline()
    assert_eq(lay.timelineMapping, mode)
    checkCelUniqueness(spr, lay, "Mode transition " .. mode .. " cycle " .. cycle)

    -- Intersperse structural mutations
    if cycle % 5 == 1 then
      spr:newEmptyFrame()
      checkCelUniqueness(spr, lay, "Cycle " .. cycle .. " added frame")
    elseif cycle % 5 == 3 and #spr.frames > 3 then
      spr:deleteFrame(#spr.frames)
      checkCelUniqueness(spr, lay, "Cycle " .. cycle .. " deleted frame")
    end
  end

  spr:close()
  print("  -> Challenger 5 PASSED: Dynamic mode transitions with mutations verified.")
end

-- ==============================================================================
-- Test 6: Deep Undo / Redo Symmetry Across Mutations
-- ==============================================================================
print("\n[Challenger 6] Deep Undo/Redo Symmetry Across Timeline Mutations...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  -- Perform 6 sequential NewFrame{ content = "copy" } operations
  for i = 1, 6 do
    app.command.NewFrame{ content = "copy" }
    checkCelUniqueness(spr, lay, "Undo-test NewFrame " .. i)
  end
  assert_eq(#spr.frames, 7)
  assert_eq(#lay.cels, 7)

  -- Undo all 6 operations back to 1 frame
  for u = 1, 6 do
    app.undo()
    local expFrames = 7 - u
    assert_eq(#spr.frames, expFrames, "Undo step " .. u .. " frames count")
    checkCelUniqueness(spr, lay, "Undo step " .. u)
  end
  assert_eq(#spr.frames, 1)
  assert_eq(#lay.cels, 1)
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "Frame 1 pristine after undo")

  -- Redo all 6 operations back to 7 frames
  for r = 1, 6 do
    app.redo()
    local expFrames = 1 + r
    assert_eq(#spr.frames, expFrames, "Redo step " .. r .. " frames count")
    checkCelUniqueness(spr, lay, "Redo step " .. r)
  end
  assert_eq(#spr.frames, 7)
  assert_eq(#lay.cels, 7)

  -- Modulo check on re-done state
  local expectedLoopColors = { "Red", "Green", "Blue", "Red", "Green", "Blue", "Red" }
  for f = 1, 7 do
    assert_eq(getColorName(lay:cel(f).image:getPixel(0, 0)), expectedLoopColors[f],
      string.format("Redone frame %d color", f))
  end

  spr:close()
  print("  -> Challenger 6 PASSED: Deep undo/redo symmetry verified.")
end

print("\n================================================================================")
print(string.format("  ALL %d EMPIRICAL CHALLENGES PASSED (%d assertions)!            ", 6, passCount))
print("================================================================================")
