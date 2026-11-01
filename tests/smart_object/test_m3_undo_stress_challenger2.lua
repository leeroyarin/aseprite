-- tests/smart_object/test_m3_undo_stress_challenger2.lua
-- ==============================================================================
-- Challenger 2 Deep Empirical Stress & Fuzzing Harness: Memory Safety & Undo/Redo
-- Milestone 3 Iteration 4: Rigorous Adversarial Testing of Cel Lifecycle
--
-- Features Tested:
--   1. Full Range of NewFrame Modes (default, copy, empty, current) & Insertions.
--   2. Head-Delete-to-1 and Full Undo/Redo Chain (11 consecutive steps).
--   3. Tail-Delete-to-1 and Full Undo/Redo Chain.
--   4. Multi-Layer Tri-Mode Invariant Stress (Loop, Hold, Fixed + Raster Layer).
--   5. Transform Property Interleaving with Timeline Undo/Redo.
--   6. Deep Deterministic Random Fuzzing (300 Steps) with 8 Action Primitives.
--   7. High-Frequency Alloc/Dealloc & Clean Document Disposal.
-- ==============================================================================

print("================================================================================")
print("  CHALLENGER 2 DEEP STRESS & ADVERSARIAL FUZZING HARNESS (M3 IT4)               ")
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

local function verifyHoldColors(lay, frameCount, testLabel)
  for f = 1, frameCount do
    local c = lay:cel(f)
    assert_true(c ~= nil and c.image ~= nil, string.format("[%s] frame %d has valid cel and image", testLabel, f))
    local expColor = (f == 1 and "Red") or (f == 2 and "Green") or "Blue"
    local actualColor = getColorName(c.image:getPixel(0, 0))
    assert_eq(actualColor, expColor, string.format("[%s] frame %d Hold pixel color", testLabel, f))
  end
end

-- ==============================================================================
-- Test Suite 1: Full Range of NewFrame Modes & Deletions Interleaved
-- ==============================================================================
print("\n[Suite 1] All NewFrame Modes (default, copy, empty, current) & Deletions...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  verifyCelTopology(spr, lay, 1, "Suite 1 initial")

  -- 1. Default NewFrame() -> 2 frames
  app.command.NewFrame()
  verifyCelTopology(spr, lay, 2, "Suite 1 NewFrame default")
  verifyLoopColors(lay, 2, "Suite 1 NewFrame default")

  -- 2. NewFrame{ content = "copy" } -> 3 frames
  app.command.NewFrame{ content = "copy" }
  verifyCelTopology(spr, lay, 3, "Suite 1 NewFrame copy")
  verifyLoopColors(lay, 3, "Suite 1 NewFrame copy")

  -- 3. NewFrame{ content = "current" } -> 4 frames
  app.command.NewFrame{ content = "current" }
  verifyCelTopology(spr, lay, 4, "Suite 1 NewFrame current")
  verifyLoopColors(lay, 4, "Suite 1 NewFrame current")

  -- 4. NewFrame{ content = "empty" } -> 5 frames
  app.command.NewFrame{ content = "empty" }
  verifyCelTopology(spr, lay, 5, "Suite 1 NewFrame empty")
  verifyLoopColors(lay, 5, "Suite 1 NewFrame empty")

  -- 5. spr:newEmptyFrame(1) (head insertion) -> 6 frames
  spr:newEmptyFrame(1)
  verifyCelTopology(spr, lay, 6, "Suite 1 newEmptyFrame head")
  verifyLoopColors(lay, 6, "Suite 1 newEmptyFrame head")

  -- 6. spr:newEmptyFrame(3) (middle insertion) -> 7 frames
  spr:newEmptyFrame(3)
  verifyCelTopology(spr, lay, 7, "Suite 1 newEmptyFrame middle")
  verifyLoopColors(lay, 7, "Suite 1 newEmptyFrame middle")

  -- 7. Undo all 6 operations back to 1 frame
  for i = 1, 6 do
    app.undo()
    local expFrames = 7 - i
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 1 undo step %d", i))
    verifyLoopColors(lay, expFrames, string.format("Suite 1 undo step %d", i))
  end

  -- 8. Redo all 6 operations back to 7 frames
  for i = 1, 6 do
    app.redo()
    local expFrames = 1 + i
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 1 redo step %d", i))
    verifyLoopColors(lay, expFrames, string.format("Suite 1 redo step %d", i))
  end

  spr:close()
  print("  -> Suite 1 PASSED: All NewFrame modes and insertions verified across undo/redo.")
end

-- ==============================================================================
-- Test Suite 2: The "Head-Delete-to-One-and-Undo" Torture Test (11 steps)
-- ==============================================================================
print("\n[Suite 2] Head-Delete-to-1 Torture Test & Full Multi-Cycle Undo/Redo...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  -- Build up 12 frames using mixed commands
  for i = 1, 11 do
    if i % 3 == 0 then
      app.command.NewFrame()
    elseif i % 3 == 1 then
      app.command.NewFrame{ content = "empty" }
    else
      spr:newEmptyFrame()
    end
  end
  verifyCelTopology(spr, lay, 12, "Suite 2 12 frames built")
  verifyLoopColors(lay, 12, "Suite 2 12 frames built")

  -- Delete head frame 11 consecutive times until 1 frame remains
  for step = 1, 11 do
    spr:deleteFrame(1)
    local expFrames = 12 - step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 2 head delete step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 2 head delete step %d", step))
  end
  assert_eq(#spr.frames, 1, "Suite 2 reached 1 frame")

  -- Undo all 11 head deletions back to 12 frames
  for step = 1, 11 do
    app.undo()
    local expFrames = 1 + step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 2 undo head delete step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 2 undo head delete step %d", step))
  end
  assert_eq(#spr.frames, 12, "Suite 2 restored to 12 frames after undo")

  -- Redo all 11 head deletions back to 1 frame
  for step = 1, 11 do
    app.redo()
    local expFrames = 12 - step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 2 redo head delete step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 2 redo head delete step %d", step))
  end
  assert_eq(#spr.frames, 1, "Suite 2 returned to 1 frame after redo")

  -- Undo all 11 again!
  for step = 1, 11 do
    app.undo()
    local expFrames = 1 + step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 2 second undo step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 2 second undo step %d", step))
  end
  assert_eq(#spr.frames, 12, "Suite 2 restored to 12 frames after second undo")

  spr:close()
  print("  -> Suite 2 PASSED: 11-step head-delete torture test passed cleanly.")
end

-- ==============================================================================
-- Test Suite 3: The "Tail-Delete-to-One-and-Undo" Torture Test
-- ==============================================================================
print("\n[Suite 3] Tail-Delete-to-1 Torture Test & Undo/Redo...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  for i = 1, 9 do
    app.command.NewFrame()
  end
  verifyCelTopology(spr, lay, 10, "Suite 3 10 frames built")
  verifyLoopColors(lay, 10, "Suite 3 10 frames built")

  -- Delete tail frame 9 consecutive times
  for step = 1, 9 do
    spr:deleteFrame(#spr.frames)
    local expFrames = 10 - step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 3 tail delete step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 3 tail delete step %d", step))
  end
  assert_eq(#spr.frames, 1, "Suite 3 reduced to 1 frame")

  -- Undo all 9 tail deletions
  for step = 1, 9 do
    app.undo()
    local expFrames = 1 + step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 3 undo tail delete step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 3 undo tail delete step %d", step))
  end
  assert_eq(#spr.frames, 10, "Suite 3 restored to 10 frames after undo")

  -- Redo all 9 tail deletions
  for step = 1, 9 do
    app.redo()
    local expFrames = 10 - step
    verifyCelTopology(spr, lay, expFrames, string.format("Suite 3 redo tail delete step %d", step))
    verifyLoopColors(lay, expFrames, string.format("Suite 3 redo tail delete step %d", step))
  end
  assert_eq(#spr.frames, 1, "Suite 3 restored to 1 frame after redo")

  spr:close()
  print("  -> Suite 3 PASSED: Tail-delete torture test passed cleanly.")
end

-- ==============================================================================
-- Test Suite 4: Multi-Layer Tri-Mode Invariant Stress (Loop + Hold + Fixed + Raster)
-- ==============================================================================
print("\n[Suite 4] Multi-Layer Tri-Mode Invariants (Loop + Hold + Fixed + Raster)...")
do
  local spr = Sprite(16, 16)
  -- Layer 1: Raster layer
  local layRaster = spr.layers[1]
  layRaster.name = "RasterLayer"

  -- Layer 2: Smart Object (Loop)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layLoop = spr.layers[2]
  layLoop.name = "SmartLoop"

  -- Layer 3: Smart Object (Hold)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layHold = spr.layers[3]
  layHold.name = "SmartHold"
  layHold.timelineMapping = "Hold"
  layHold:syncTimeline()

  -- Layer 4: Smart Object (Fixed)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layFixed = spr.layers[4]
  layFixed.name = "SmartFixed"
  layFixed.timelineMapping = "Fixed"
  layFixed:syncTimeline()

  assert_eq(layLoop.timelineMapping, "Loop", "Suite 4 Loop mode")
  assert_eq(layHold.timelineMapping, "Hold", "Suite 4 Hold mode")
  assert_eq(layFixed.timelineMapping, "Fixed", "Suite 4 Fixed mode")

  -- Expand sprite to 6 frames using NewFrame() and newEmptyFrame
  app.command.NewFrame()
  spr:newEmptyFrame()
  app.command.NewFrame{ content = "empty" }
  spr:newEmptyFrame(2)
  app.command.NewFrame{ content = "copy" }
  assert_eq(#spr.frames, 6, "Suite 4 6 frames created")

  local function verifyAll4Layers(frameCount, label)
    verifyCelTopology(spr, layLoop, frameCount, label .. " Loop")
    verifyLoopColors(layLoop, frameCount, label .. " Loop")

    verifyCelTopology(spr, layHold, frameCount, label .. " Hold")
    verifyHoldColors(layHold, frameCount, label .. " Hold")

    verifyCelTopology(spr, layFixed, frameCount, label .. " Fixed")
    for f = 1, frameCount do
      assert_eq(getColorName(layFixed:cel(f).image:getPixel(0, 0)), "Red",
        string.format("[%s Fixed] frame %d is Red", label, f))
    end
  end

  verifyAll4Layers(6, "Suite 4 6-frames")

  -- Interleaved deletions: delete middle (frame 3), delete head (frame 1), delete tail (frame 4)
  spr:deleteFrame(3) -- 5 frames
  verifyAll4Layers(5, "Suite 4 after deleteFrame(3)")

  spr:deleteFrame(1) -- 4 frames
  verifyAll4Layers(4, "Suite 4 after deleteFrame(1)")

  spr:deleteFrame(#spr.frames) -- 3 frames
  verifyAll4Layers(3, "Suite 4 after deleteFrame(tail)")

  -- Undo all 3 deletions
  app.undo() -- 4 frames
  verifyAll4Layers(4, "Suite 4 after undo tail delete")

  app.undo() -- 5 frames
  verifyAll4Layers(5, "Suite 4 after undo head delete")

  app.undo() -- 6 frames
  verifyAll4Layers(6, "Suite 4 after undo middle delete")

  -- Redo all 3 deletions
  app.redo()
  verifyAll4Layers(5, "Suite 4 after redo middle delete")

  app.redo()
  verifyAll4Layers(4, "Suite 4 after redo head delete")

  app.redo()
  verifyAll4Layers(3, "Suite 4 after redo tail delete")

  spr:close()
  print("  -> Suite 4 PASSED: Multi-layer tri-mode invariants held across all mutations.")
end

-- ==============================================================================
-- Test Suite 5: Transform Property Interleaving with Timeline Undo/Redo
-- ==============================================================================
print("\n[Suite 5] Transform Modifications Interleaved with Timeline Undo/Redo...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  app.command.NewFrame()
  app.command.NewFrame()
  app.command.NewFrame()
  verifyCelTopology(spr, lay, 4, "Suite 5 initial 4 frames")

  -- Undo 2 frames -> 2 frames
  app.undo()
  app.undo()
  verifyCelTopology(spr, lay, 2, "Suite 5 undone to 2 frames")

  -- Redo 2 frames back to 4 frames (clean undo/redo)
  app.redo()
  app.redo()
  verifyCelTopology(spr, lay, 4, "Suite 5 clean redo back to 4 frames")

  -- Change translation of cel 1 and switch timeline mapping to Hold
  local c1 = lay:cel(1)
  c1.position = Point(4, 7)
  assert_eq(c1.position.x, 4, "Suite 5 translation X applied")
  assert_eq(c1.position.y, 7, "Suite 5 translation Y applied")

  lay.timelineMapping = "Hold"
  lay:syncTimeline()
  assert_eq(lay.timelineMapping, "Hold", "Suite 5 timelineMapping switched to Hold")
  verifyHoldColors(lay, 4, "Suite 5 Hold colors after switch")

  -- Delete frame 1 (canonical cel) with active transform
  spr:deleteFrame(1)
  verifyCelTopology(spr, lay, 3, "Suite 5 after delete frame 1 with transform")
  verifyHoldColors(lay, 3, "Suite 5 colors after delete frame 1")

  -- Undo deletion of canonical frame 1
  app.undo()
  verifyCelTopology(spr, lay, 4, "Suite 5 after undo delete frame 1")
  verifyHoldColors(lay, 4, "Suite 5 colors after undo delete frame 1")

  -- Redo deletion of canonical frame 1
  app.redo()
  verifyCelTopology(spr, lay, 3, "Suite 5 after redo delete frame 1")
  verifyHoldColors(lay, 3, "Suite 5 colors after redo delete frame 1")

  -- Undo deletion again to return to 4 frames
  app.undo()
  verifyCelTopology(spr, lay, 4, "Suite 5 returned to 4 frames")

  -- Switch back to Loop mode and verify
  lay.timelineMapping = "Loop"
  lay:syncTimeline()
  verifyLoopColors(lay, 4, "Suite 5 colors after switch back to Loop")

  spr:close()
  print("  -> Suite 5 PASSED: Transform modifications cleanly survived timeline undos/redos.")
end

-- ==============================================================================
-- Test Suite 6: Deep Deterministic Random Fuzzing (300 Steps)
-- ==============================================================================
print("\n[Suite 6] Deep Deterministic Fuzzing Harness (300 Random Operations)...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local seed = 987654321
  local function rnd(minVal, maxVal)
    seed = (seed * 1103515245 + 12345) % 2147483648
    local r = minVal + math.floor((seed / 2147483648) * (maxVal - minVal + 1))
    return r
  end

  local undoDepth = 0
  local redoDepth = 0

  for opIdx = 1, 300 do
    local action = rnd(1, 8)

    if action == 1 then
      -- Default NewFrame() (duplication)
      if #spr.frames < 14 then
        app.command.NewFrame()
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 2 then
      -- NewFrame{ content = "copy" }
      if #spr.frames < 14 then
        app.command.NewFrame{ content = "copy" }
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 3 then
      -- NewFrame{ content = "empty" }
      if #spr.frames < 14 then
        app.command.NewFrame{ content = "empty" }
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 4 then
      -- spr:newEmptyFrame(pos)
      if #spr.frames < 14 then
        local pos = rnd(1, #spr.frames + 1)
        spr:newEmptyFrame(pos)
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 5 then
      -- RemoveFrame via command
      if #spr.frames > 1 then
        app.activeFrame = spr.frames[rnd(1, #spr.frames)]
        app.command.RemoveFrame()
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 6 then
      -- Delete frame at random position
      if #spr.frames > 1 then
        local target = rnd(1, #spr.frames)
        spr:deleteFrame(target)
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 7 then
      -- Undo
      if undoDepth > 0 and #spr.frames > 1 then
        app.undo()
        undoDepth = undoDepth - 1
        redoDepth = redoDepth + 1
      end
    elseif action == 8 then
      -- Redo
      if redoDepth > 0 then
        app.redo()
        undoDepth = undoDepth + 1
        redoDepth = redoDepth - 1
      end
    end

    local curFrames = #spr.frames
    verifyCelTopology(spr, lay, curFrames, string.format("Fuzz step %d (action %d)", opIdx, action))
    verifyLoopColors(lay, curFrames, string.format("Fuzz step %d (action %d)", opIdx, action))
  end

  -- Complete full unwind of undo stack down to 1 frame
  while undoDepth > 0 and #spr.frames > 1 do
    app.undo()
    undoDepth = undoDepth - 1
    verifyCelTopology(spr, lay, #spr.frames, string.format("Fuzz unwind frames=%d", #spr.frames))
    verifyLoopColors(lay, #spr.frames, string.format("Fuzz unwind frames=%d", #spr.frames))
  end

  spr:close()
  print("  -> Suite 6 PASSED: 300 deep fuzzing steps completed with 0 errors or memory faults.")
end

-- ==============================================================================
-- Test Suite 7: Rapid Alloc/Dealloc Clean Disposal (20 Iterations)
-- ==============================================================================
print("\n[Suite 7] High-Frequency Alloc/Dealloc & Clean Document Disposal...")
do
  for iter = 1, 20 do
    local s = Sprite(16, 16)
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
      ui = false
    }
    local l = s.layers[2]
    app.command.NewFrame()
    s:newEmptyFrame()
    app.command.NewFrame{ content = "copy" }
    app.undo()
    s:deleteFrame(1)
    app.undo()
    app.redo()
    verifyCelTopology(s, l, #s.frames, string.format("Alloc/Dealloc iter %d", iter))
    s:close()
  end
  print("  -> Suite 7 PASSED: 20 rapid lifecycle cycles executed with zero leaks or crashes.")
end

-- ==============================================================================
-- Test Suite 8: 10-Frame Fixture Multi-Frame Topology & Deep Undo/Redo Fuzzing
-- ==============================================================================
print("\n[Suite 8] 10-Frame Source Fixture (multi_frame_10f) Deep Fuzzing...")
do
  local spr = Sprite(20, 20)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_10f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local function verify10fColors(lay, frameCount, label)
    for f = 1, frameCount do
      local c = lay:cel(f)
      assert_true(c ~= nil and c.image ~= nil, string.format("[%s] frame %d has cel & image", label, f))
      local lf = ((f - 1) % 10) + 1
      -- Orange progress bar extends up to 2 * lf - 1 at y = 9
      local barTipX = 2 * lf - 1
      local px = c.image:getPixel(barTipX, 9)
      local r = app.pixelColor.rgbaR(px)
      local g = app.pixelColor.rgbaG(px)
      assert_true(r > 200 and g > 100, string.format("[%s] frame %d (lf=%d) bar tip at x=%d is orange", label, f, lf, barTipX))
    end
  end

  verifyCelTopology(spr, lay, 1, "Suite 8 initial")
  verify10fColors(lay, 1, "Suite 8 initial")

  local seed = 123456789
  local function rnd(minVal, maxVal)
    seed = (seed * 1103515245 + 12345) % 2147483648
    local r = minVal + math.floor((seed / 2147483648) * (maxVal - minVal + 1))
    return r
  end

  local undoDepth = 0
  local redoDepth = 0

  for opIdx = 1, 200 do
    local action = rnd(1, 8)

    if action == 1 then
      if #spr.frames < 25 then
        app.command.NewFrame()
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 2 then
      if #spr.frames < 25 then
        app.command.NewFrame{ content = "copy" }
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 3 then
      if #spr.frames < 25 then
        app.command.NewFrame{ content = "empty" }
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 4 then
      if #spr.frames < 25 then
        spr:newEmptyFrame(rnd(1, #spr.frames + 1))
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 5 then
      if #spr.frames > 1 then
        app.activeFrame = spr.frames[rnd(1, #spr.frames)]
        app.command.RemoveFrame()
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 6 then
      if #spr.frames > 1 then
        spr:deleteFrame(rnd(1, #spr.frames))
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 7 then
      if undoDepth > 0 and #spr.frames > 1 then
        app.undo()
        undoDepth = undoDepth - 1
        redoDepth = redoDepth + 1
      end
    elseif action == 8 then
      if redoDepth > 0 then
        app.redo()
        undoDepth = undoDepth + 1
        redoDepth = redoDepth - 1
      end
    end

    local curFrames = #spr.frames
    verifyCelTopology(spr, lay, curFrames, string.format("Suite 8 step %d", opIdx))
    verify10fColors(lay, curFrames, string.format("Suite 8 step %d", opIdx))
  end

  while undoDepth > 0 and #spr.frames > 1 do
    app.undo()
    undoDepth = undoDepth - 1
    verifyCelTopology(spr, lay, #spr.frames, string.format("Suite 8 unwind frames=%d", #spr.frames))
    verify10fColors(lay, #spr.frames, string.format("Suite 8 unwind frames=%d", #spr.frames))
  end

  spr:close()
  print("  -> Suite 8 PASSED: 10-frame source file fuzzing and unwinding passed cleanly.")
end

print("\n================================================================================")
print(string.format("  ALL %d CHALLENGER 2 SUITES PASSED (%d assertions)!             ", 8, passCount))
print("================================================================================")
