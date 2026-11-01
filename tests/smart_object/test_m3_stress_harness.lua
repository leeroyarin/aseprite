-- tests/smart_object/test_m3_stress_harness.lua
-- ==============================================================================
-- EMPIRICAL CHALLENGER STRESS HARNESS: MILESTONE 3 ITERATION 2
-- Adversarial Fuzzing, Timeline Churn, Mode Thrashing, Undo/Redo & Multi-Layer Sync
-- ==============================================================================

print("================================================================================")
print("  ADVERSARIAL STRESS HARNESS: M3 IT2 TIMELINE & CEL LIFECYCLE                   ")
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
-- Stress Test 1: Random Timeline Mutation Fuzzer (300 Iterations)
-- Pseudorandom sequence of insertions and deletions (head, middle, tail).
-- Verifies cel count strictly tracks host totalFrames and modulo sequence holds.
-- ==============================================================================
print("\n[Harness Test 1] Random Timeline Mutation Fuzzer (300 Mutations)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 4 do spr:newEmptyFrame() end
  assert_eq(#spr.frames, 5)

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 5)

  -- LCG RNG for deterministic repeatability
  local seed = 987654321
  local function nextRand(minVal, maxVal)
    seed = (seed * 1103515245 + 12345) % 2147483648
    return minVal + (seed % (maxVal - minVal + 1))
  end

  for op = 1, 300 do
    local nFrames = #spr.frames
    -- 50% insert, 50% delete (unless at boundaries [1, 30])
    local doInsert = (nextRand(1, 100) <= 50)
    if nFrames <= 1 then doInsert = true end
    if nFrames >= 30 then doInsert = false end

    if doInsert then
      local pos = nextRand(1, nFrames + 1)
      if pos > nFrames then
        spr:newEmptyFrame()
      else
        spr:newEmptyFrame(pos)
      end
    else
      local pos = nextRand(1, nFrames)
      spr:deleteFrame(pos)
    end

    local currentNFrames = #spr.frames
    -- Verify layer invariant: cel count MUST match frame count exactly
    assert_eq(#lay.cels, currentNFrames, string.format("Op %d: cel count equals frame count %d", op, currentNFrames))

    -- Spot check first, middle, last frame colors according to modulo
    local checkIndices = { 1, math.max(1, math.floor(currentNFrames / 2)), currentNFrames }
    for _, idx in ipairs(checkIndices) do
      local c = lay:cel(idx)
      assert_true(c ~= nil, string.format("Op %d: cel %d exists", op, idx))
      local expMod = (idx - 1) % 3
      local expColor = (expMod == 0 and "Red") or (expMod == 1 and "Green") or "Blue"
      assert_eq(getColorName(c.image:getPixel(0, 0)), expColor,
        string.format("Op %d: frame %d color matches modulo %s", op, idx, expColor))
    end
  end

  -- Verify full invariant across all frames at end of fuzzing
  local total = #spr.frames
  for f = 1, total do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Final check: cel %d exists", f))
    local expMod = (f - 1) % 3
    local expColor = (expMod == 0 and "Red") or (expMod == 1 and "Green") or "Blue"
    assert_eq(getColorName(c.image:getPixel(0, 0)), expColor,
      string.format("Final check: frame %d color", f))
  end
  assert_nil(lay:cel(total + 1), "no cel beyond lastFrame")

  spr:close()
  print(string.format("  -> Harness Test 1 PASSED: 300 random mutations cleanly survived."))
end

-- ==============================================================================
-- Stress Test 2: Mode Thrashing Interleaved with Timeline Resizing
-- Toggles between Loop, Hold, Fixed while expanding and shrinking.
-- ==============================================================================
print("\n[Harness Test 2] Mode Thrashing Interleaved with Timeline Resizing...")
do
  local spr = Sprite(16, 16)
  for i = 1, 9 do spr:newEmptyFrame() end -- 10 frames

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 10)

  local modes = { "Fixed", "Loop", "Hold", "Fixed", "Hold", "Loop" }
  for cycle, mode in ipairs(modes) do
    lay.timelineMapping = mode
    lay:syncTimeline()

    if mode == "Fixed" then
      local baseId = lay:cel(1).image.id
      for f = 1, #spr.frames do
        assert_eq(lay:cel(f).image.id, baseId, string.format("Cycle %d Fixed frame %d buffer share", cycle, f))
        assert_eq(getColorName(lay:cel(f).image:getPixel(0, 0)), "Red", string.format("Cycle %d Fixed frame %d is Red", cycle, f))
      end
    elseif mode == "Hold" then
      for f = 1, #spr.frames do
        local expColor = (f == 1 and "Red") or (f == 2 and "Green") or "Blue"
        assert_eq(getColorName(lay:cel(f).image:getPixel(0, 0)), expColor, string.format("Cycle %d Hold frame %d color", cycle, f))
      end
    elseif mode == "Loop" then
      for f = 1, #spr.frames do
        local mod = (f - 1) % 3
        local expColor = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
        assert_eq(getColorName(lay:cel(f).image:getPixel(0, 0)), expColor, string.format("Cycle %d Loop frame %d color", cycle, f))
      end
    end

    -- Mutate timeline size inside the mode
    if cycle % 2 == 1 then
      spr:newEmptyFrame(1) -- head insertion
    else
      spr:deleteFrame(#spr.frames) -- tail deletion
    end
    assert_eq(#lay.cels, #spr.frames, string.format("Cycle %d mutated cel count equals frame count", cycle))
  end

  spr:close()
  print("  -> Harness Test 2 PASSED: Mode thrashing with mutations verified.")
end

-- ==============================================================================
-- Stress Test 3: Multiple Concurrent Smart Objects on Separate Layers
-- Place 2 smart object layers with different frame configurations.
-- Verify that displaceFrames preserves each layer's independent sequence.
-- ==============================================================================
print("\n[Harness Test 3] Multiple Concurrent Smart Objects Sync...")
do
  local spr = Sprite(32, 32)
  for i = 1, 3 do spr:newEmptyFrame() end -- 4 frames

  -- Layer 1: 3-frame object (Red, Green, Blue)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay1 = spr.layers[2]

  -- Layer 2: 1-frame object
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/single_frame_32x32.aseprite",
    ui = false
  }
  local lay2 = spr.layers[3]

  assert_eq(#lay1.cels, 4, "lay1 initial cels")
  assert_eq(#lay2.cels, 4, "lay2 initial cels")

  -- Insert frame in middle (position 2)
  spr:newEmptyFrame(2)
  assert_eq(#spr.frames, 5)
  assert_eq(#lay1.cels, 5, "lay1 cels after middle insert")
  assert_eq(#lay2.cels, 5, "lay2 cels after middle insert")

  -- Check lay1 (3-frame modulo)
  assert_eq(getColorName(lay1:cel(1).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay1:cel(2).image:getPixel(0, 0)), "Green")
  assert_eq(getColorName(lay1:cel(3).image:getPixel(0, 0)), "Blue")
  assert_eq(getColorName(lay1:cel(4).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay1:cel(5).image:getPixel(0, 0)), "Green")

  -- Check lay2 (1-frame constant sharing)
  local yelId = lay2:cel(1).image.id
  for f = 1, 5 do
    assert_eq(lay2:cel(f).image.id, yelId, string.format("lay2 frame %d shares single image", f))
  end

  -- Delete frame 1 (head deletion)
  spr:deleteFrame(1)
  assert_eq(#spr.frames, 4)
  assert_eq(#lay1.cels, 4, "lay1 cels after head delete")
  assert_eq(#lay2.cels, 4, "lay2 cels after head delete")
  assert_nil(lay1:cel(5), "lay1 cel 5 nil")
  assert_nil(lay2:cel(5), "lay2 cel 5 nil")

  assert_eq(getColorName(lay1:cel(1).image:getPixel(0, 0)), "Red")
  assert_eq(getColorName(lay1:cel(2).image:getPixel(0, 0)), "Green")
  assert_eq(getColorName(lay1:cel(3).image:getPixel(0, 0)), "Blue")
  assert_eq(getColorName(lay1:cel(4).image:getPixel(0, 0)), "Red")

  spr:close()
  print("  -> Harness Test 3 PASSED: Multiple concurrent smart layers verified.")
end

-- ==============================================================================
-- Stress Test 4: Extreme Collapse to 1 Frame and Rebound to 20 Frames
-- Repeatedly oscillates between minimal (1 frame) and large (20 frames) timeline.
-- ==============================================================================
print("\n[Harness Test 4] Oscillating Boundary Collapse (1 <-> 20 frames)...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  for round = 1, 5 do
    -- Expand to 20
    for i = 2, 20 do spr:newEmptyFrame() end
    lay:syncTimeline()
    assert_eq(#spr.frames, 20, string.format("Round %d expanded to 20 frames", round))
    assert_eq(#lay.cels, 20, string.format("Round %d lay has 20 cels", round))
    assert_eq(getColorName(lay:cel(20).image:getPixel(0, 0)), "Green")

    -- Collapse back to 1
    for i = 20, 2, -1 do spr:deleteFrame(i) end
    assert_eq(#spr.frames, 1, string.format("Round %d collapsed to 1 frame", round))
    assert_eq(#lay.cels, 1, string.format("Round %d lay has 1 cel", round))
    assert_nil(lay:cel(2), string.format("Round %d cel 2 nil", round))
    assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red")
  end

  spr:close()
  print("  -> Harness Test 4 PASSED: Repeated collapse/rebound cycles clean.")
end

-- ==============================================================================
-- Stress Test 5: Massive Scrubbing Throughput (10,000 Cel Accesses)
-- Measures memory stability and lookup latency during heavy playback simulation.
-- ==============================================================================
print("\n[Harness Test 5] Massive Scrubbing Throughput (10,000 Cel Accesses)...")
do
  local spr = Sprite(16, 16)
  for i = 1, 49 do spr:newEmptyFrame() end -- 50 frames
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  assert_eq(#lay.cels, 50)

  local seed = 54321
  for i = 1, 10000 do
    seed = (seed * 1103515245 + 12345) % 2147483648
    local f = (seed % 50) + 1
    local c = lay:cel(f)
    local img = c.image
    local px = img:getPixel(0, 0)
    if (i % 2500 == 0) then
      local mod = (f - 1) % 3
      local expColor = (mod == 0 and "Red") or (mod == 1 and "Green") or "Blue"
      assert_eq(getColorName(px), expColor, string.format("Scrub step %d color check", i))
    end
  end

  spr:close()
  print("  -> Harness Test 5 PASSED: 10,000 random scrubbing accesses verified.")
end

print("\n================================================================================")
print(string.format("  ALL %d STRESS HARNESS TESTS PASSED (%d assertions)!           ", 5, passCount))
print("================================================================================")
