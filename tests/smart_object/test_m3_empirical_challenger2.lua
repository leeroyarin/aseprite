-- ==============================================================================
-- Empirical Challenger Stress Harness — Milestone 3 Iteration 3
-- Author: challenger_smart_m3_it3_2 (teamwork_challenger)
-- ==============================================================================

local passCount = 0
local failCount = 0

local function assert_true(cond, msg)
  if cond then
    passCount = passCount + 1
  else
    failCount = failCount + 1
    error("ASSERTION FAILED: " .. (msg or "expected true, got false"), 2)
  end
end

local function assert_false(cond, msg)
  if not cond then
    passCount = passCount + 1
  else
    failCount = failCount + 1
    error("ASSERTION FAILED: " .. (msg or "expected false, got true"), 2)
  end
end

local function assert_eq(actual, expected, msg)
  if actual == expected then
    passCount = passCount + 1
  else
    failCount = failCount + 1
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      tostring(msg or "values match"), tostring(expected), tostring(actual)), 2)
  end
end

local function assert_nil(val, msg)
  if val == nil then
    passCount = passCount + 1
  else
    failCount = failCount + 1
    error(string.format("ASSERTION FAILED: %s (expected nil, got non-nil)", tostring(msg)), 2)
  end
end

local function assert_not_nil(val, msg)
  if val ~= nil then
    passCount = passCount + 1
  else
    failCount = failCount + 1
    error(string.format("ASSERTION FAILED: %s (expected non-nil, got nil)", tostring(msg)), 2)
  end
end

local function getColorName(pixel)
  local r = app.pixelColor.rgbaR(pixel)
  local g = app.pixelColor.rgbaG(pixel)
  local b = app.pixelColor.rgbaB(pixel)
  local a = app.pixelColor.rgbaA(pixel)
  if a == 0 then return "Transparent" end
  if r == 255 and g == 0 and b == 0 then return "Red" end
  if r == 0 and g == 255 and b == 0 then return "Green" end
  if r == 0 and g == 0 and b == 255 then return "Blue" end
  if r == 255 and g == 255 and b == 0 then return "Yellow" end
  if r == 0 and g == 255 and b == 255 then return "Cyan" end
  if r == 255 and g == 0 and b == 255 then return "Magenta" end
  if r == 255 and g == 255 and b == 255 then return "White" end
  if r == 0 and g == 0 and b == 0 then return "Black" end
  return string.format("RGB(%d,%d,%d)", r, g, b)
end

local function countUniqueBuffers(lay)
  local uniqueIds = {}
  local count = 0
  for _, c in ipairs(lay.cels) do
    if not uniqueIds[c.image.id] then
      uniqueIds[c.image.id] = true
      count = count + 1
    end
  end
  return count, uniqueIds
end

local function verifyCelTopology(spr, lay, expectedCount, label)
  assert_eq(#lay.cels, expectedCount, label .. " cel count == expected")
  assert_eq(#spr.frames, expectedCount, label .. " frame count == expected")
  local seenFrames = {}
  for i, c in ipairs(lay.cels) do
    assert_not_nil(c, label .. " cel " .. i .. " non-nil")
    assert_true(c.frameNumber >= 1 and c.frameNumber <= expectedCount,
      label .. " cel " .. i .. " frameNumber " .. tostring(c.frameNumber) .. " in range")
    assert_false(seenFrames[c.frameNumber],
      label .. " duplicate cel for frameNumber " .. tostring(c.frameNumber))
    seenFrames[c.frameNumber] = true
  end
  for f = 1, expectedCount do
    assert_true(seenFrames[f], label .. " missing cel for frame " .. f)
    assert_not_nil(lay:cel(f), label .. " lay:cel(" .. f .. ") exists")
  end
end

print("================================================================================")
print("  EMPIRICAL CHALLENGER STRESS SUITE: TIMELINE MAPPING & CEL LIFECYCLE")
print("================================================================================")

-- ==============================================================================
-- CHALLENGE 1: Empirical Verification of Timeline Mappings (Loop, Hold, Fixed, PingPong)
-- Under Arbitrary Host Lengths N > canonicalCount (M = 3)
-- ==============================================================================
print("\n[Challenge 1] Timeline Mappings (Loop, Hold, Fixed, PingPong) under N > M...")
do
  local testLengths = { 1, 2, 3, 4, 5, 6, 7, 8, 11, 15, 33 }
  local fixture = "tests/smart_object/fixtures/multi_frame_3f.aseprite"

  for _, N in ipairs(testLengths) do
    local spr = Sprite(32, 32)
    for i = 1, N - 1 do
      spr:newEmptyFrame()
    end
    assert_eq(#spr.frames, N, string.format("Host sprite created with %d frames", N))

    app.command.PlaceSmartObject{ filename = fixture, ui = false }
    local lay = spr.layers[2]
    assert_true(lay.isSmartObject, "Layer is smart object")
    assert_eq(lay.linkedFrameCount, 3, "Linked frame count is 3")

    -- 1A: LOOP MODE
    lay.timelineMapping = "Loop"
    lay:syncTimeline()
    assert_eq(lay.timelineMapping, "Loop")
    verifyCelTopology(spr, lay, N, string.format("Loop N=%d", N))

    local expectedUnique = math.min(3, N)
    local uniqueCount, uniqueMap = countUniqueBuffers(lay)
    assert_eq(uniqueCount, expectedUnique, string.format("Loop N=%d unique buffer count", N))

    for f = 1, N do
      local c = lay:cel(f)
      local mod = (f - 1) % 3
      local expectedColor = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
      local canonicalFrame = mod + 1
      if canonicalFrame <= N then
        local canonicalCel = lay:cel(canonicalFrame)
        assert_eq(c.image.id, canonicalCel.image.id,
          string.format("Loop N=%d frame %d shares buffer with canonical %d", N, f, canonicalFrame))
      end
      assert_eq(getColorName(c.image:getPixel(0, 0)), expectedColor,
        string.format("Loop N=%d frame %d color", N, f))
    end

    -- 1B: HOLD MODE
    lay.timelineMapping = "Hold"
    lay:syncTimeline()
    assert_eq(lay.timelineMapping, "Hold")
    verifyCelTopology(spr, lay, N, string.format("Hold N=%d", N))

    local uniqueCountHold, _ = countUniqueBuffers(lay)
    assert_eq(uniqueCountHold, expectedUnique, string.format("Hold N=%d unique buffer count", N))

    for f = 1, N do
      local c = lay:cel(f)
      local expectedColor = (f == 1 and "Red") or (f == 2 and "Green") or "Blue"
      local canonicalFrame = math.min(f, math.min(3, N))
      local canonicalCel = lay:cel(canonicalFrame)
      assert_eq(c.image.id, canonicalCel.image.id,
        string.format("Hold N=%d frame %d shares buffer with canonical %d", N, f, canonicalFrame))
      assert_eq(getColorName(c.image:getPixel(0, 0)), expectedColor,
        string.format("Hold N=%d frame %d color", N, f))
    end

    -- 1C: FIXED MODE
    lay.timelineMapping = "Fixed"
    lay:syncTimeline()
    assert_eq(lay.timelineMapping, "Fixed")
    verifyCelTopology(spr, lay, N, string.format("Fixed N=%d", N))

    local uniqueCountFixed, _ = countUniqueBuffers(lay)
    assert_eq(uniqueCountFixed, 1, string.format("Fixed N=%d strictly 1 unique buffer", N))

    local canonicalFixedId = lay:cel(1).image.id
    for f = 1, N do
      local c = lay:cel(f)
      assert_eq(c.image.id, canonicalFixedId,
        string.format("Fixed N=%d frame %d shares cel 1 buffer ID", N, f))
      assert_eq(getColorName(c.image:getPixel(0, 0)), "Red",
        string.format("Fixed N=%d frame %d is Red", N, f))
    end

    -- 1D: PINGPONG FALLBACK MODE
    lay.timelineMapping = "PingPong"
    lay:syncTimeline()
    -- PingPong is not in C++ enum TimelineMapping { Loop=0, Hold=1, Fixed=2 }.
    -- Contract verify: gracefully defaults to Loop without crashing.
    assert_eq(lay.timelineMapping, "Loop", "PingPong string input defaults to Loop")
    verifyCelTopology(spr, lay, N, string.format("PingPong fallback N=%d", N))
    local uniqueCountPP, _ = countUniqueBuffers(lay)
    assert_eq(uniqueCountPP, expectedUnique, string.format("PingPong fallback N=%d unique buffer count", N))

    spr:close()
  end

  print("  -> Challenge 1 PASSED: Timeline mappings verified across 11 host lengths (N=1..33).")
end

-- ==============================================================================
-- CHALLENGE 2: Fixed Timeline Mode: Modifying Cel 1 and Moving Frames
-- Single Shared Buffer Invariant & 0-Churn Buffer Integrity
-- ==============================================================================
print("\n[Challenge 2] Fixed Mode: Modifying Cel 1, Moving Frames, and Buffer Invariants...")
do
  local spr = Sprite(32, 32)
  for i = 1, 9 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10, "Host has 10 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  lay.timelineMapping = "Fixed"
  lay:syncTimeline()

  verifyCelTopology(spr, lay, 10, "Fixed 10 frames initial")
  local initialSharedId = lay:cel(1).image.id
  local initialCount, _ = countUniqueBuffers(lay)
  assert_eq(initialCount, 1, "Initially exactly 1 shared buffer")

  -- 2A: Modifying cel 1 position / transform
  local initX = lay:cel(1).position.x
  local initY = lay:cel(1).position.y
  lay:translate(15, -8)
  local translatedSharedId = lay:cel(1).image.id
  for f = 1, 10 do
    local c = lay:cel(f)
    assert_eq(c.position.x, initX + 15, string.format("Translated cel %d pos.x", f))
    assert_eq(c.position.y, initY - 8, string.format("Translated cel %d pos.y", f))
    assert_eq(c.image.id, translatedSharedId, string.format("Translated cel %d preserves shared ID", f))
  end
  local countAfterTrans, _ = countUniqueBuffers(lay)
  assert_eq(countAfterTrans, 1, "Translating cel 1 does not duplicate buffers")

  -- 2B: Modifying cel 1 scale and rotation
  lay.scale = { 2.0, 2.0 }
  lay.angle = 90.0
  lay:syncTimeline()
  for f = 1, 10 do
    local c = lay:cel(f)
    assert_eq(c.image.width, 32, string.format("Transformed cel %d width", f))
    assert_eq(c.image.height, 32, string.format("Transformed cel %d height", f))
  end
  local countAfterScale, _ = countUniqueBuffers(lay)
  assert_eq(countAfterScale, 1, "Scaling/rotating cel 1 preserves single buffer invariant")
  local scaledSharedId = lay:cel(1).image.id

  -- 2C: Frame deletion at head (frame 1 deleted)
  spr:deleteFrame(1)
  assert_eq(#spr.frames, 9, "Host reduced to 9 frames")
  verifyCelTopology(spr, lay, 9, "Fixed mode after delete frame 1")
  local countAfterDel1, _ = countUniqueBuffers(lay)
  assert_eq(countAfterDel1, 1, "Deleting frame 1 preserves single shared buffer invariant")
  local currentSharedId = lay:cel(1).image.id
  for f = 1, 9 do
    assert_eq(lay:cel(f).image.id, currentSharedId, string.format("Frame %d shares new cel 1 ID", f))
  end

  -- 2D: Frame deletion at middle (frame 5 deleted)
  spr:deleteFrame(5)
  assert_eq(#spr.frames, 8, "Host reduced to 8 frames")
  verifyCelTopology(spr, lay, 8, "Fixed mode after delete frame 5")
  local countAfterDelMid, _ = countUniqueBuffers(lay)
  assert_eq(countAfterDelMid, 1, "Deleting middle frame preserves single shared buffer")
  for f = 1, 8 do
    assert_eq(lay:cel(f).image.id, currentSharedId, string.format("Post-mid-del frame %d shares ID", f))
  end

  -- 2E: Frame insertion at head (frame 1 inserted)
  spr:newEmptyFrame(1)
  assert_eq(#spr.frames, 9, "Host expanded to 9 frames")
  lay:syncTimeline()
  verifyCelTopology(spr, lay, 9, "Fixed mode after insert frame 1")
  local countAfterInsHead, _ = countUniqueBuffers(lay)
  assert_eq(countAfterInsHead, 1, "Inserting frame at head preserves single shared buffer")

  -- 2F: Frame insertion at middle (frame 4 inserted)
  spr:newEmptyFrame(4)
  assert_eq(#spr.frames, 10, "Host expanded to 10 frames")
  lay:syncTimeline()
  verifyCelTopology(spr, lay, 10, "Fixed mode after insert frame 4")
  local countAfterInsMid, _ = countUniqueBuffers(lay)
  assert_eq(countAfterInsMid, 1, "Inserting frame at middle preserves single shared buffer")

  -- 2G: Frame insertion at tail
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 11, "Host expanded to 11 frames")
  lay:syncTimeline()
  verifyCelTopology(spr, lay, 11, "Fixed mode after insert frame tail")
  local countAfterInsTail, _ = countUniqueBuffers(lay)
  assert_eq(countAfterInsTail, 1, "Inserting frame at tail preserves single shared buffer")

  -- 2H: Rapid frame addition & deletion stress in Fixed mode (30 iterations)
  for iter = 1, 30 do
    if iter % 2 == 1 then
      spr:newEmptyFrame(1)
    else
      spr:deleteFrame(1)
    end
    lay:syncTimeline()
    local cnt, _ = countUniqueBuffers(lay)
    assert_eq(cnt, 1, string.format("Fixed mode stress iter %d unique buffer count == 1", iter))
  end
  verifyCelTopology(spr, lay, #spr.frames, "Fixed mode post-stress topology")

  spr:close()
  print("  -> Challenge 2 PASSED: Fixed mode single shared buffer invariant strictly preserved.")
end

-- ==============================================================================
-- CHALLENGE 3: Canonical Cels (k < canonicalCount) Update Reflection & Determinism
-- ==============================================================================
print("\n[Challenge 3] Canonical Cels Update Propagation to Arbitrary Host Lengths (N > M)...")
do
  -- Host with N = 8 frames, linked file has M = 3 canonical cels
  local spr = Sprite(32, 32)
  for i = 1, 7 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 8, "Host has 8 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(lay.timelineMapping, "Loop")
  assert_eq(lay.linkedFrameCount, 3)

  -- Canonical cels are 1, 2, 3 (k < 3)
  -- Host frames 4, 5, 6, 7, 8 are linked cels:
  -- 4 -> 1, 5 -> 2, 6 -> 3, 7 -> 1, 8 -> 2
  local c1_id = lay:cel(1).image.id
  local c2_id = lay:cel(2).image.id
  local c3_id = lay:cel(3).image.id
  assert_true(c1_id ~= c2_id and c2_id ~= c3_id, "3 distinct canonical image buffers")

  assert_eq(lay:cel(4).image.id, c1_id, "Frame 4 linked to canonical 1")
  assert_eq(lay:cel(5).image.id, c2_id, "Frame 5 linked to canonical 2")
  assert_eq(lay:cel(6).image.id, c3_id, "Frame 6 linked to canonical 3")
  assert_eq(lay:cel(7).image.id, c1_id, "Frame 7 linked to canonical 1")
  assert_eq(lay:cel(8).image.id, c2_id, "Frame 8 linked to canonical 2")

  -- Apply 2D transformation: scale 2.0x, flipH true, angle 90
  lay.scale = { 2.0, 2.0 }
  lay.flipH = true
  lay.angle = 90.0
  lay:syncTimeline()

  -- After sync/render, verify all 3 canonical cels re-rendered transformed
  local new_c1_id = lay:cel(1).image.id
  local new_c2_id = lay:cel(2).image.id
  local new_c3_id = lay:cel(3).image.id

  -- Verify deterministic propagation across all 8 host frames
  assert_eq(lay:cel(4).image.id, new_c1_id, "Frame 4 reflects updated canonical 1")
  assert_eq(lay:cel(5).image.id, new_c2_id, "Frame 5 reflects updated canonical 2")
  assert_eq(lay:cel(6).image.id, new_c3_id, "Frame 6 reflects updated canonical 3")
  assert_eq(lay:cel(7).image.id, new_c1_id, "Frame 7 reflects updated canonical 1")
  assert_eq(lay:cel(8).image.id, new_c2_id, "Frame 8 reflects updated canonical 2")

  for f = 1, 8 do
    local c = lay:cel(f)
    assert_eq(c.image.width, 32, string.format("Host frame %d width is 32", f))
    assert_eq(c.image.height, 32, string.format("Host frame %d height is 32", f))
    local mod = (f - 1) % 3
    local expCol = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
    assert_eq(getColorName(c.image:getPixel(0, 0)), expCol,
      string.format("Host frame %d pixel color matches canonical", f))
  end

  local uCnt, _ = countUniqueBuffers(lay)
  assert_eq(uCnt, 3, "Exactly 3 canonical buffers shared across all 8 frames")

  spr:close()
  print("  -> Challenge 3 PASSED: Canonical cels update and map deterministically.")
end

-- ==============================================================================
-- CHALLENGE 4: Rapid Scrubbing Stress & Frame Re-ordering
-- ==============================================================================
print("\n[Challenge 4] Scrubbing Stress & Out-of-Order Access Across 50 Frames...")
do
  local spr = Sprite(16, 16)
  for i = 1, 49 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 50, "50 frames in host")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 50)

  local initialUCount, _ = countUniqueBuffers(lay)
  assert_eq(initialUCount, 3, "Initially 3 unique buffers for 50 frames in Loop mode")

  -- Pseudo-random scrubbing (500 steps)
  local seed = 12345
  local function prng()
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed
  end

  for step = 1, 500 do
    local f = (prng() % 50) + 1
    local c = lay:cel(f)
    assert_not_nil(c, string.format("Scrub step %d frame %d exists", step, f))
    local mod = (f - 1) % 3
    local expCol = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
    assert_eq(getColorName(c.image:getPixel(0, 0)), expCol,
      string.format("Scrub step %d frame %d color", step, f))
  end

  -- Reverse scrubbing: 50 down to 1
  for f = 50, 1, -1 do
    local c = lay:cel(f)
    local mod = (f - 1) % 3
    local expCol = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
    assert_eq(getColorName(c.image:getPixel(0, 0)), expCol,
      string.format("Reverse scrub frame %d color", f))
  end

  -- Invariant check: scrubbing must never duplicate buffers or mutate cels
  local postScrubUCount, _ = countUniqueBuffers(lay)
  assert_eq(postScrubUCount, 3, "Buffer count immutable after 550 scrubbing operations")
  verifyCelTopology(spr, lay, 50, "Post-scrub topology")

  spr:close()
  print("  -> Challenge 4 PASSED: Scrubbing stress verified without side-effects.")
end

-- ==============================================================================
-- CHALLENGE 5: Cel Linking and Lifecycle Under Rapid Addition/Deletion Cycles
-- (100 Interleaved Mutations)
-- ==============================================================================
print("\n[Challenge 5] Cel Lifecycle Under 100 Interleaved Add/Delete Mutations...")
do
  local spr = Sprite(16, 16)
  for i = 1, 9 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10, "Host has 10 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local seed = 98765
  local function prng()
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed
  end

  local modes = { "Loop", "Hold", "Fixed" }

  for step = 1, 100 do
    local action = prng() % 100
    local n = #spr.frames

    if action < 30 and n < 35 then
      -- Add frame at random position
      local pos = (prng() % (n + 1)) + 1
      if pos > n then
        spr:newEmptyFrame()
      else
        spr:newEmptyFrame(pos)
      end
      lay:syncTimeline()
    elseif action < 60 and n > 2 then
      -- Delete frame at random position
      local pos = (prng() % n) + 1
      spr:deleteFrame(pos)
      lay:syncTimeline()
    elseif action < 85 then
      -- Switch timeline mode
      local m = modes[(prng() % 3) + 1]
      lay.timelineMapping = m
      lay:syncTimeline()
    else
      -- 2D translation / rotation
      local dx = (prng() % 10) - 5
      local dy = (prng() % 10) - 5
      lay:translate(dx, dy)
    end

    -- Strict Invariant Verification on EVERY mutation step
    local currentN = #spr.frames
    verifyCelTopology(spr, lay, currentN, string.format("Stress step %d (n=%d)", step, currentN))

    local uCount, _ = countUniqueBuffers(lay)
    if lay.timelineMapping == "Fixed" then
      assert_eq(uCount, 1, string.format("Stress step %d Fixed mode unique count", step))
    else
      local expU = math.min(3, currentN)
      assert_eq(uCount, expU, string.format("Stress step %d %s mode unique count", step, lay.timelineMapping))
    end
  end

  spr:close()
  print("  -> Challenge 5 PASSED: 100 interleaved mutations verified with 0 invariant violations.")
end

-- ==============================================================================
-- CHALLENGE 6: Multi-Step Undo/Redo & Transaction Consistency Stress
-- ==============================================================================
print("\n[Challenge 6] Transaction Consistency & Multi-Step Undo/Redo Stress...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 1)

  -- Record baseline
  local baseId = lay:cel(1).image.id

  -- Execute 8 NewFrame commands
  for i = 1, 8 do
    app.command.NewFrame{ content = "empty" }
    verifyCelTopology(spr, lay, 1 + i, string.format("NewFrame step %d", i))
  end
  assert_eq(#spr.frames, 9)
  assert_eq(#lay.cels, 9)

  -- Undo all 8 frames sequentially
  for i = 8, 1, -1 do
    app.undo()
    verifyCelTopology(spr, lay, i, string.format("Undo step down to %d", i))
  end
  assert_eq(#spr.frames, 1, "Back to 1 frame after 8 undos")
  assert_eq(#lay.cels, 1, "Back to 1 cel after 8 undos")
  assert_nil(lay:cel(2), "cel 2 strictly nil after full undo")

  -- Redo all 8 frames sequentially
  for i = 1, 8 do
    app.redo()
    verifyCelTopology(spr, lay, 1 + i, string.format("Redo step up to %d", 1 + i))
  end
  assert_eq(#spr.frames, 9, "Restored 9 frames after 8 redos")
  assert_eq(#lay.cels, 9, "Restored 9 cels after 8 redos")

  -- Mixed undo / redo oscillation
  for osc = 1, 5 do
    app.undo() -- 8
    app.undo() -- 7
    verifyCelTopology(spr, lay, 7, string.format("Oscillation %d after 2 undos", osc))
    app.redo() -- 8
    verifyCelTopology(spr, lay, 8, string.format("Oscillation %d after 1 redo", osc))
    app.redo() -- 9
    verifyCelTopology(spr, lay, 9, string.format("Oscillation %d after 2nd redo", osc))
  end

  spr:close()
  print("  -> Challenge 6 PASSED: Multi-step undo/redo verified without leaks or duplicate cels.")
end

-- ==============================================================================
-- CHALLENGE 7: Direct Pixel Editing Lockout Integrity in All Modes
-- ==============================================================================
print("\n[Challenge 7] Direct Pixel Edit Lockout Invariant Across All Timeline Modes...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local modes = { "Loop", "Hold", "Fixed" }
  for _, mode in ipairs(modes) do
    lay.timelineMapping = mode
    lay:syncTimeline()

    -- Verification 1: isSmartObject property
    assert_true(lay.isSmartObject, mode .. " layer is smart object")

    -- Verification 2: cel.image assignment blocked
    local cel = lay:cel(1)
    local dummyImg = Image(16, 16)
    local okImage, errImage = pcall(function() cel.image = dummyImg end)
    assert_false(okImage, mode .. " cel.image assignment blocked")
    assert_true(string.find(tostring(errImage), "cannot modify cel image") ~= nil,
      mode .. " correct error on cel.image assignment")

    -- Verification 3: cel.frame movement blocked
    local okFrame, errFrame = pcall(function() cel.frame = 2 end)
    assert_false(okFrame, mode .. " cel.frame reassignment blocked")
    assert_true(string.find(tostring(errFrame), "cannot move cel") ~= nil,
      mode .. " correct error on cel.frame reassignment")
  end

  spr:close()
  print("  -> Challenge 7 PASSED: Edit lockout strictly maintained across all modes.")
end

print("\n================================================================================")
print(string.format("  ALL %d EMPIRICAL CHALLENGER TESTS PASSED (%d assertions)!     ", 7, passCount))
print("================================================================================")
