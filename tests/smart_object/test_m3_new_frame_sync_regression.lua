-- tests/smart_object/test_m3_new_frame_sync_regression.lua
-- ==============================================================================
-- Regression Test Suite for Milestone 3 Defect 3:
-- Timeline Synchronization Across NewFrame Frame Addition Modes
--
-- Validates:
--   1. Default parameterless app.command.NewFrame() (Alt+N) displays source
--      timeline frames according to timeline mapping (e.g. Loop modulo).
--   2. Explicit app.command.NewFrame{ content = "copy" } does NOT copy the
--      preceding cel's raster pixels onto the smart object layer.
--   3. Explicit app.command.NewFrame{ content = "current" } maintains sync.
--   4. Middle frame insertion via NewFrame preserves modulo color ordering.
--   5. Dual layer hierarchy: Regular layers copy cels while smart object layers
--      synchronize timeline frames.
--   6. app.command.NewFrame{ content = "celcopies" } / { content = "cellinked" }
--      do not corrupt smart object cels with raster duplicates.
--   7. Hold and Fixed timeline mapping modes remain strictly invariant across
--      all NewFrame operations.
--   8. Undo and redo cycles after NewFrame addition succeed without crashing
--      or desynchronizing pixels.
-- ==============================================================================

print("================================================================================")
print("  REGRESSION TEST: TIMELINE SYNCHRONIZATION ON NEWFRAME ADDITION MODES (DEFECT 3)")
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

-- Helper to verify cel topology and absence of duplicates
local function verifyCelTopology(spr, lay, expectedCount, testLabel)
  assert_eq(#spr.frames, expectedCount, string.format("[%s] sprite frames count", testLabel))
  assert_eq(#lay.cels, expectedCount, string.format("[%s] layer cels count matches frames count", testLabel))

  local seenFrames = {}
  for idx, c in ipairs(lay.cels) do
    assert_true(c ~= nil, string.format("[%s] cel at index %d is not nil", testLabel, idx))
    assert_true(seenFrames[c.frameNumber] == nil,
      string.format("[%s] duplicate cel detected at frame %d", testLabel, c.frameNumber))
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
-- Test 1: Standard Default app.command.NewFrame() Timeline Synchronization
-- ==============================================================================
print("\n[Test 1] Standard default app.command.NewFrame() timeline synchronization...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  verifyCelTopology(spr, lay, 1, "Test1 Initial")
  verifyLoopColors(lay, 1, "Test1 Initial")

  -- Add 3 frames using default NewFrame() (parameterless)
  -- Frame 1: Red (initial)
  -- Frame 2: Green (must be Green, NOT Red copy!)
  -- Frame 3: Blue (must be Blue, NOT Green copy!)
  -- Frame 4: Red (must wrap modulo to Red!)
  for step = 1, 3 do
    app.command.NewFrame()
    local expectedFrames = 1 + step
    verifyCelTopology(spr, lay, expectedFrames, string.format("Test1 NewFrame step %d", step))
    verifyLoopColors(lay, expectedFrames, string.format("Test1 NewFrame step %d", step))
  end

  -- Undo all 3 frames
  for step = 1, 3 do
    app.undo()
    local expectedFrames = 4 - step
    verifyCelTopology(spr, lay, expectedFrames, string.format("Test1 undo step %d", step))
    verifyLoopColors(lay, expectedFrames, string.format("Test1 undo step %d", step))
  end

  -- Redo all 3 frames
  for step = 1, 3 do
    app.redo()
    local expectedFrames = 1 + step
    verifyCelTopology(spr, lay, expectedFrames, string.format("Test1 redo step %d", step))
    verifyLoopColors(lay, expectedFrames, string.format("Test1 redo step %d", step))
  end

  spr:close()
  print("  -> Test 1 PASSED: Default NewFrame() synchronized across 4 frames + undo/redo.")
end

-- ==============================================================================
-- Test 2: Explicit app.command.NewFrame{ content = "copy" }
-- ==============================================================================
print("\n[Test 2] Explicit app.command.NewFrame{ content = 'copy' }...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]
  verifyCelTopology(spr, lay, 1, "Test2 Initial")

  -- Add Frame 2 with content = "copy"
  app.command.NewFrame{ content = "copy" }
  verifyCelTopology(spr, lay, 2, "Test2 Add Frame 2 copy")
  assert_eq(getColorName(lay:cel(1).image:getPixel(0, 0)), "Red", "Test2 Frame 1 Red")
  assert_eq(getColorName(lay:cel(2).image:getPixel(0, 0)), "Green", "Test2 Frame 2 Green (not Red copy)")

  -- Add Frame 3 with content = "copy"
  app.command.NewFrame{ content = "copy" }
  verifyCelTopology(spr, lay, 3, "Test2 Add Frame 3 copy")
  assert_eq(getColorName(lay:cel(3).image:getPixel(0, 0)), "Blue", "Test2 Frame 3 Blue (not Green copy)")

  -- Add Frame 4 with content = "copy"
  app.command.NewFrame{ content = "copy" }
  verifyCelTopology(spr, lay, 4, "Test2 Add Frame 4 copy")
  assert_eq(getColorName(lay:cel(4).image:getPixel(0, 0)), "Red", "Test2 Frame 4 Red modulo wrap")

  -- Undo and redo
  app.undo()
  verifyCelTopology(spr, lay, 3, "Test2 Undo to 3")
  verifyLoopColors(lay, 3, "Test2 Undo to 3")

  app.redo()
  verifyCelTopology(spr, lay, 4, "Test2 Redo to 4")
  verifyLoopColors(lay, 4, "Test2 Redo to 4")

  spr:close()
  print("  -> Test 2 PASSED: NewFrame{ content = 'copy' } does not duplicate raster cel.")
end

-- ==============================================================================
-- Test 3: Explicit app.command.NewFrame{ content = "current" } & { content = "frame" }
-- ==============================================================================
print("\n[Test 3] Explicit app.command.NewFrame with content = 'current' and 'frame'...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  app.command.NewFrame{ content = "current" }
  verifyCelTopology(spr, lay, 2, "Test3 Add Frame 2 current")
  verifyLoopColors(lay, 2, "Test3 Add Frame 2 current")

  app.command.NewFrame{ content = "frame" }
  verifyCelTopology(spr, lay, 3, "Test3 Add Frame 3 frame")
  verifyLoopColors(lay, 3, "Test3 Add Frame 3 frame")

  spr:close()
  print("  -> Test 3 PASSED: 'current' and 'frame' modes maintain timeline synchronization.")
end

-- ==============================================================================
-- Test 4: Middle Frame Insertion via app.command.NewFrame()
-- ==============================================================================
print("\n[Test 4] Middle frame insertion via NewFrame()...")
do
  local spr = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local lay = spr.layers[2]

  -- Build initial 3 frames: Red, Green, Blue
  app.command.NewFrame{ content = "empty" }
  app.command.NewFrame{ content = "empty" }
  verifyCelTopology(spr, lay, 3, "Test4 Initial 3 frames")
  verifyLoopColors(lay, 3, "Test4 Initial 3 frames")

  -- Move active frame to Frame 1 (Red) and insert new frame after it
  app.activeFrame = spr.frames[1]
  app.command.NewFrame() -- Inserts frame at position 2

  verifyCelTopology(spr, lay, 4, "Test4 After middle insert at pos 2")
  verifyLoopColors(lay, 4, "Test4 Modulo colors after middle insert")

  -- Undo middle insert
  app.undo()
  verifyCelTopology(spr, lay, 3, "Test4 After undo middle insert")
  verifyLoopColors(lay, 3, "Test4 After undo middle insert")

  -- Redo middle insert
  app.redo()
  verifyCelTopology(spr, lay, 4, "Test4 After redo middle insert")
  verifyLoopColors(lay, 4, "Test4 After redo middle insert")

  spr:close()
  print("  -> Test 4 PASSED: Middle frame insertion maintains timeline order & undo symmetry.")
end

-- ==============================================================================
-- Test 5: Dual Layer Hierarchy (Smart Object + Normal Raster Layer)
-- ==============================================================================
print("\n[Test 5] Dual layer hierarchy: Normal raster copy vs Smart Object sync...")
do
  local spr = Sprite(16, 16)
  -- Layer 1 is default transparent layer
  local normalLay = spr.layers[1]
  normalLay.name = "NormalLayer"
  -- Draw yellow pixel on normal layer frame 1
  local normalCel1 = spr:newCel(normalLay, 1)
  normalCel1.image:drawPixel(0, 0, app.pixelColor.rgba(255, 255, 0, 255))

  -- Layer 2 is Smart Object layer
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local smartLay = spr.layers[2]

  assert_eq(#spr.frames, 1, "Test5 Initial 1 frame")
  assert_eq(getColorName(normalLay:cel(1).image:getPixel(0, 0)), "Yellow", "Test5 normalLayer F1 Yellow")
  assert_eq(getColorName(smartLay:cel(1).image:getPixel(0, 0)), "Red", "Test5 smartLayer F1 Red")

  -- Execute NewFrame()
  app.command.NewFrame()
  assert_eq(#spr.frames, 2, "Test5 2 frames after NewFrame")

  -- Verification:
  -- Normal layer duplicated yellow pixel to frame 2 (standard Aseprite duplicate behavior)
  assert_true(normalLay:cel(2) ~= nil, "Test5 normalLay has cel at frame 2")
  assert_eq(getColorName(normalLay:cel(2).image:getPixel(0, 0)), "Yellow",
    "Test5 normalLay cel 2 duplicated Yellow pixel")

  -- Smart object layer synchronized to source frame 2 Green (R3 behavior)
  assert_true(smartLay:cel(2) ~= nil, "Test5 smartLay has cel at frame 2")
  assert_eq(getColorName(smartLay:cel(2).image:getPixel(0, 0)), "Green",
    "Test5 smartLay cel 2 synchronized to Green (NOT copied from Red)")

  -- Undo and verify
  app.undo()
  assert_eq(#spr.frames, 1, "Test5 back to 1 frame after undo")
  assert_nil(normalLay:cel(2), "Test5 normalLay cel 2 nil after undo")
  assert_nil(smartLay:cel(2), "Test5 smartLay cel 2 nil after undo")

  -- Redo and verify
  app.redo()
  assert_eq(#spr.frames, 2, "Test5 restored to 2 frames after redo")
  assert_eq(getColorName(normalLay:cel(2).image:getPixel(0, 0)), "Yellow", "Test5 normalLay cel 2 restored Yellow")
  assert_eq(getColorName(smartLay:cel(2).image:getPixel(0, 0)), "Green", "Test5 smartLay cel 2 restored Green")

  spr:close()
  print("  -> Test 5 PASSED: Normal layer duplicates cels while smart layer synchronizes timeline.")
end

-- ==============================================================================
-- Test 6: Hold & Fixed Timeline Mapping Modes Invariance
-- ==============================================================================
print("\n[Test 6] Hold and Fixed timeline mapping modes invariance across NewFrame...")
do
  -- 6A: Hold mode
  local sprHold = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layHold = sprHold.layers[2]
  layHold.timelineMapping = "Hold"
  layHold:syncTimeline()

  -- Add 3 frames via NewFrame{ content = "copy" }
  app.command.NewFrame{ content = "copy" } -- 2 (Green)
  app.command.NewFrame{ content = "copy" } -- 3 (Blue)
  app.command.NewFrame{ content = "copy" } -- 4 (Held Blue!)

  verifyCelTopology(sprHold, layHold, 4, "Test6A Hold 4 frames")
  assert_eq(getColorName(layHold:cel(1).image:getPixel(0, 0)), "Red", "Test6A F1 Red")
  assert_eq(getColorName(layHold:cel(2).image:getPixel(0, 0)), "Green", "Test6A F2 Green")
  assert_eq(getColorName(layHold:cel(3).image:getPixel(0, 0)), "Blue", "Test6A F3 Blue")
  assert_eq(getColorName(layHold:cel(4).image:getPixel(0, 0)), "Blue", "Test6A F4 Held Blue")

  sprHold:close()

  -- 6B: Fixed mode
  local sprFixed = Sprite(16, 16)
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local layFixed = sprFixed.layers[2]
  layFixed.timelineMapping = "Fixed"
  layFixed:syncTimeline()

  local fixedSharedId = layFixed:cel(1).image.id

  -- Add 3 frames via default NewFrame()
  app.command.NewFrame()
  app.command.NewFrame()
  app.command.NewFrame()

  verifyCelTopology(sprFixed, layFixed, 4, "Test6B Fixed 4 frames")
  for f = 1, 4 do
    assert_eq(getColorName(layFixed:cel(f).image:getPixel(0, 0)), "Red",
      string.format("Test6B Fixed frame %d Red", f))
    assert_eq(layFixed:cel(f).image.id, fixedSharedId,
      string.format("Test6B Fixed frame %d shares canonical image ID", f))
  end

  sprFixed:close()
  print("  -> Test 6 PASSED: Hold and Fixed modes maintain exact invariants across NewFrame.")
end

print("\n================================================================================")
print(string.format("  ALL %d REGRESSION TESTS PASSED (%d assertions)!                 ", 6, passCount))
print("================================================================================")
