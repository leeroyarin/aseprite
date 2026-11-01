-- tests/smart_object/test_m3_undo_stress_challenger1.lua
-- ==============================================================================
-- Challenger 1 Empirical Stress Test: Comprehensive Undo/Redo & Cel Lifecycle
-- Milestone 3 Iteration 3: Frame Addition & Deletion Types + Interleaved Sequences
--
-- Validates:
--   1. app.command.NewFrame{ content = "empty" }
--   2. spr:newEmptyFrame() (tail and middle insertions)
--   3. app.command.RemoveFrame()
--   4. spr:deleteFrame() (head, middle, tail)
--   5. Interleaved add -> undo -> redo -> delete -> undo -> redo sequences.
--   6. Redo stack invalidation / branching history.
--   7. Dual smart layer (Loop + Fixed) invariant preservation.
--   8. Deterministic random fuzzing harness (100 steps).
--   9. Defect isolation for NewFrame copy / default NewFrame crash (see test_m3_redo_crash_repro.lua).
-- ==============================================================================

print("================================================================================")
print("  CHALLENGER 1 EMPIRICAL STRESS TEST: UNDO/REDO & CEL LIFECYCLE HARDENING        ")
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

-- Helper to verify modulo color sequence for Loop mode with multi_frame_3f.aseprite
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

-- ==============================================================================
-- Stress Test 1: NewFrame{ content = "empty" } Deep Undo/Redo Cycles (10 frames)
-- ==============================================================================
print("\n[Stress 1] app.command.NewFrame{ content = 'empty' } Deep Undo/Redo Cycles...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  verifyCelTopology(spr, lay, 1, "Stress1 initial")
  verifyLoopColors(lay, 1, "Stress1 initial")

  -- Sequentially add 8 frames with content = "empty"
  for i = 1, 8 do
    app.command.NewFrame{ content = "empty" }
    verifyCelTopology(spr, lay, i + 1, string.format("Stress1 add empty step %d", i))
    verifyLoopColors(lay, i + 1, string.format("Stress1 add empty step %d", i))
  end

  -- Undo all 8 frames step-by-step
  for i = 1, 8 do
    app.undo()
    local expected = 9 - i
    verifyCelTopology(spr, lay, expected, string.format("Stress1 undo empty step %d", i))
    verifyLoopColors(lay, expected, string.format("Stress1 undo empty step %d", i))
    assert_nil(lay:cel(expected + 1), string.format("Stress1 frame %d is nil after undo", expected + 1))
  end

  -- Redo all 8 frames step-by-step
  for i = 1, 8 do
    app.redo()
    local expected = 1 + i
    verifyCelTopology(spr, lay, expected, string.format("Stress1 redo empty step %d", i))
    verifyLoopColors(lay, expected, string.format("Stress1 redo empty step %d", i))
  end

  spr:close()
  print("  -> Stress 1 PASSED: Deep NewFrame empty undo/redo cycles verified.")
end

-- ==============================================================================
-- Stress Test 2: Complex Interleaving: NewFrame(empty), spr:newEmptyFrame, RemoveFrame, deleteFrame
-- ==============================================================================
print("\n[Stress 2] Interleaved Mix: NewFrame(empty), spr:newEmptyFrame, RemoveFrame, deleteFrame...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  verifyCelTopology(spr, lay, 1, "Stress2 initial")

  -- 2A: Addition via NewFrame(empty) -> 2 frames
  app.command.NewFrame{ content = "empty" }
  verifyCelTopology(spr, lay, 2, "Stress2 NewFrame empty")
  verifyLoopColors(lay, 2, "Stress2 NewFrame empty")

  -- 2B: Addition via spr:newEmptyFrame() (tail) -> 3 frames
  spr:newEmptyFrame()
  verifyCelTopology(spr, lay, 3, "Stress2 spr:newEmptyFrame tail")
  verifyLoopColors(lay, 3, "Stress2 spr:newEmptyFrame tail")

  -- 2C: Insertion via spr:newEmptyFrame(2) (middle) -> 4 frames
  spr:newEmptyFrame(2)
  verifyCelTopology(spr, lay, 4, "Stress2 spr:newEmptyFrame middle")
  verifyLoopColors(lay, 4, "Stress2 spr:newEmptyFrame middle")

  -- 2D: Insertion via spr:newEmptyFrame(1) (head) -> 5 frames
  spr:newEmptyFrame(1)
  verifyCelTopology(spr, lay, 5, "Stress2 spr:newEmptyFrame head")
  verifyLoopColors(lay, 5, "Stress2 spr:newEmptyFrame head")

  -- 2E: Undo 2 operations -> 3 frames
  app.undo() -- undo head insert
  verifyCelTopology(spr, lay, 4, "Stress2 undo head")
  verifyLoopColors(lay, 4, "Stress2 undo head")

  app.undo() -- undo middle insert
  verifyCelTopology(spr, lay, 3, "Stress2 undo middle")
  verifyLoopColors(lay, 3, "Stress2 undo middle")

  -- 2F: Redo 2 operations -> 5 frames
  app.redo() -- redo middle insert
  verifyCelTopology(spr, lay, 4, "Stress2 redo middle")
  verifyLoopColors(lay, 4, "Stress2 redo middle")

  app.redo() -- redo head insert
  verifyCelTopology(spr, lay, 5, "Stress2 redo head")
  verifyLoopColors(lay, 5, "Stress2 redo head")

  -- 2G: Deletions:
  -- Delete middle frame via app.command.RemoveFrame() -> 4 frames
  app.activeFrame = spr.frames[3]
  app.command.RemoveFrame()
  verifyCelTopology(spr, lay, 4, "Stress2 RemoveFrame middle")
  verifyLoopColors(lay, 4, "Stress2 RemoveFrame middle")

  -- Delete head frame via spr:deleteFrame(1) -> 3 frames
  spr:deleteFrame(1)
  verifyCelTopology(spr, lay, 3, "Stress2 deleteFrame head")
  verifyLoopColors(lay, 3, "Stress2 deleteFrame head")

  -- Delete tail frame via spr:deleteFrame(#spr.frames) -> 2 frames
  spr:deleteFrame(#spr.frames)
  verifyCelTopology(spr, lay, 2, "Stress2 deleteFrame tail")
  verifyLoopColors(lay, 2, "Stress2 deleteFrame tail")

  -- 2H: Undo all 3 deletions:
  app.undo() -- undo tail delete -> 3 frames
  verifyCelTopology(spr, lay, 3, "Stress2 undo tail delete")
  verifyLoopColors(lay, 3, "Stress2 undo tail delete")

  app.undo() -- undo head delete -> 4 frames
  verifyCelTopology(spr, lay, 4, "Stress2 undo head delete")
  verifyLoopColors(lay, 4, "Stress2 undo head delete")

  app.undo() -- undo middle delete -> 5 frames
  verifyCelTopology(spr, lay, 5, "Stress2 undo middle delete")
  verifyLoopColors(lay, 5, "Stress2 undo middle delete")

  -- 2I: Redo all 3 deletions -> 2 frames
  app.redo()
  verifyCelTopology(spr, lay, 4, "Stress2 redo middle delete")
  app.redo()
  verifyCelTopology(spr, lay, 3, "Stress2 redo head delete")
  app.redo()
  verifyCelTopology(spr, lay, 2, "Stress2 redo tail delete")

  -- 2J: Undo back down to initial frame 1
  while #spr.frames > 1 do
    app.undo()
    verifyCelTopology(spr, lay, #spr.frames, string.format("Stress2 unwinding to 1: frames=%d", #spr.frames))
    verifyLoopColors(lay, #spr.frames, string.format("Stress2 unwinding to 1: frames=%d", #spr.frames))
  end
  verifyCelTopology(spr, lay, 1, "Stress2 final unwound")
  verifyLoopColors(lay, 1, "Stress2 final unwound")

  spr:close()
  print("  -> Stress 2 PASSED: Complex interleaving completely verified.")
end

-- ==============================================================================
-- Stress Test 3: Redo Stack Invalidation & Branching History Integrity
-- ==============================================================================
print("\n[Stress 3] Redo Stack Invalidation & Branching Timeline History...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  -- Build branch A: 5 frames
  app.command.NewFrame{ content = "empty" } -- 2
  spr:newEmptyFrame()                      -- 3
  spr:newEmptyFrame()                      -- 4
  app.command.NewFrame{ content = "empty" } -- 5
  verifyCelTopology(spr, lay, 5, "Stress3 branch A full")

  -- Undo back to 2 frames
  app.undo()
  app.undo()
  app.undo()
  verifyCelTopology(spr, lay, 2, "Stress3 branch A undone to 2")

  -- Branch B: Introduce new action to invalidate redo stack
  spr:newEmptyFrame() -- 3 frames in new branch
  verifyCelTopology(spr, lay, 3, "Stress3 branch B step 1")
  verifyLoopColors(lay, 3, "Stress3 branch B step 1")

  -- Verify redo does not revive stale cels from branch A
  local framesBeforeRedo = #spr.frames
  local okRedo, _ = pcall(function() app.redo() end)
  assert_eq(#spr.frames, framesBeforeRedo, "Stress3 redo did nothing on truncated branch")
  verifyCelTopology(spr, lay, framesBeforeRedo, "Stress3 topology stable after invalid redo")

  -- Delete frame 2 in branch B
  app.activeFrame = spr.frames[2]
  app.command.RemoveFrame()
  verifyCelTopology(spr, lay, 2, "Stress3 branch B after RemoveFrame")
  verifyLoopColors(lay, 2, "Stress3 branch B after RemoveFrame")

  -- Undo delete
  app.undo()
  verifyCelTopology(spr, lay, 3, "Stress3 branch B after undo RemoveFrame")
  verifyLoopColors(lay, 3, "Stress3 branch B after undo RemoveFrame")

  -- Unwind branch B to 1
  app.undo()
  verifyCelTopology(spr, lay, 2, "Stress3 branch B undo to 2")
  app.undo()
  verifyCelTopology(spr, lay, 1, "Stress3 branch B undo to 1")
  verifyLoopColors(lay, 1, "Stress3 branch B undo to 1")

  spr:close()
  print("  -> Stress 3 PASSED: Branching history and redo invalidation verified.")
end

-- ==============================================================================
-- Stress Test 4: Dual Smart Object Layers (Loop + Fixed Mode) Undo/Redo Invariants
-- ==============================================================================
print("\n[Stress 4] Dual Smart Object Layers (Loop + Fixed) Undo/Redo Invariants...")
do
  local spr = Sprite(16, 16)
  -- Layer 2: Smart Object (Loop mode)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layLoop = spr.layers[2]

  -- Layer 3: Smart Object (Fixed mode)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layFixed = spr.layers[3]
  layFixed.timelineMapping = "Fixed"
  layFixed:syncTimeline()

  assert_eq(layLoop.timelineMapping, "Loop", "Stress4 layLoop is Loop")
  assert_eq(layFixed.timelineMapping, "Fixed", "Stress4 layFixed is Fixed")

  -- Add 3 frames
  app.command.NewFrame{ content = "empty" }
  spr:newEmptyFrame()
  app.command.NewFrame{ content = "empty" }
  assert_eq(#spr.frames, 4, "Stress4 4 frames created")

  -- Verify both layers satisfy #lay.cels == 4
  verifyCelTopology(spr, layLoop, 4, "Stress4 layLoop 4 frames")
  verifyLoopColors(layLoop, 4, "Stress4 layLoop 4 frames")

  verifyCelTopology(spr, layFixed, 4, "Stress4 layFixed 4 frames")
  local fixedSharedId = layFixed:cel(1).image.id
  for f = 1, 4 do
    assert_eq(layFixed:cel(f).image.id, fixedSharedId,
      string.format("Stress4 layFixed frame %d shares image id", f))
    assert_eq(getColorName(layFixed:cel(f).image:getPixel(0, 0)), "Red",
      string.format("Stress4 layFixed frame %d is Red", f))
  end

  -- Undo 2 frames
  app.undo()
  app.undo()
  assert_eq(#spr.frames, 2, "Stress4 frames reduced to 2 after undo")
  verifyCelTopology(spr, layLoop, 2, "Stress4 layLoop after undo")
  verifyCelTopology(spr, layFixed, 2, "Stress4 layFixed after undo")
  assert_eq(layFixed:cel(1).image.id, fixedSharedId, "Stress4 layFixed shared image preserved after undo")
  assert_eq(layFixed:cel(2).image.id, fixedSharedId, "Stress4 layFixed shared image preserved after undo")

  -- Redo 2 frames
  app.redo()
  app.redo()
  assert_eq(#spr.frames, 4, "Stress4 frames restored to 4 after redo")
  verifyCelTopology(spr, layLoop, 4, "Stress4 layLoop after redo")
  verifyCelTopology(spr, layFixed, 4, "Stress4 layFixed after redo")
  assert_eq(layFixed:cel(3).image.id, fixedSharedId, "Stress4 layFixed shared image preserved after redo")
  assert_eq(layFixed:cel(4).image.id, fixedSharedId, "Stress4 layFixed shared image preserved after redo")

  spr:close()
  print("  -> Stress 4 PASSED: Dual layer Loop + Fixed invariants preserved.")
end

-- ==============================================================================
-- Stress Test 5: Deterministic Fuzzing / Random Walk Harness (100 Operations)
-- ==============================================================================
print("\n[Stress 5] Deterministic Fuzzing Harness (100 Random Operations)...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  local seed = 4242
  local function rnd(minVal, maxVal)
    seed = (seed * 1103515245 + 12345) % 2147483648
    local r = minVal + math.floor((seed / 2147483648) * (maxVal - minVal + 1))
    return r
  end

  local undoDepth = 0
  local redoDepth = 0

  for opIdx = 1, 100 do
    local action = rnd(1, 5)

    if action == 1 then
      -- Add empty frame via command
      if #spr.frames < 12 then
        app.command.NewFrame{ content = "empty" }
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 2 then
      -- spr:newEmptyFrame()
      if #spr.frames < 12 then
        spr:newEmptyFrame()
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 3 then
      -- Delete frame if > 1
      if #spr.frames > 1 then
        local target = rnd(1, #spr.frames)
        spr:deleteFrame(target)
        undoDepth = undoDepth + 1
        redoDepth = 0
      end
    elseif action == 4 then
      -- Undo if depth > 0
      if undoDepth > 0 and #spr.frames > 1 then
        app.undo()
        undoDepth = undoDepth - 1
        redoDepth = redoDepth + 1
      end
    elseif action == 5 then
      -- Redo if depth > 0
      if redoDepth > 0 then
        app.redo()
        undoDepth = undoDepth + 1
        redoDepth = redoDepth - 1
      end
    end

    local expected = #spr.frames
    verifyCelTopology(spr, lay, expected, string.format("Fuzz step %d (action %d)", opIdx, action))
    verifyLoopColors(lay, expected, string.format("Fuzz step %d (action %d)", opIdx, action))
  end

  while undoDepth > 0 and #spr.frames > 1 do
    app.undo()
    undoDepth = undoDepth - 1
    verifyCelTopology(spr, lay, #spr.frames, string.format("Fuzz unwind frames=%d", #spr.frames))
    verifyLoopColors(lay, #spr.frames, string.format("Fuzz unwind frames=%d", #spr.frames))
  end

  spr:close()
  print("  -> Stress 5 PASSED: 100 fuzz steps executed with zero invariant violations.")
end

-- ==============================================================================
-- Stress Test 6: Defect Isolation Documentation (NewFrame Copy / Default Redo Crash)
-- ==============================================================================
print("\n[Stress 6] Defect Isolation: NewFrame copy / default redo crash...")
print("  Note: Full reproducer is isolated in tests/smart_object/test_m3_redo_crash_repro.lua")
print("  Root Cause: displaceFrames(delta < 0) prematurely deletes Cel* while")
print("              cmd::RemoveCel transaction holds pointer -> 0xc0000374 crash on redo.")
print("  -> Stress 6 RECORDED.")

print("\n================================================================================")
print(string.format("  ALL %d CHALLENGER STRESS TESTS PASSED (%d assertions)!         ", 5, passCount))
print("================================================================================")
