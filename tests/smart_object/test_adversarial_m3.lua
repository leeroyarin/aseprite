-- test_adversarial_m3.lua
-- Adversarial Stress Test Suite for Milestone 3 (Robustness & Cel Lifecycle)

print("================================================================================")
print("     ADVERSARIAL STRESS TEST: MILESTONE 3 ROBUSTNESS & CEL LIFECYCLE            ")
print("================================================================================")

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

-- ==============================================================================
-- TEST 1: Rapid Scrubbing Forward and Backward Through 200+ Frames
-- ==============================================================================
print("\n[TEST 1] Rapid Scrubbing Forward and Backward (200+ Frames)...")
do
  local spr = Sprite(64, 64)
  for i = 1, 199 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 200, "host has 200 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_true(smartLayer.isSmartObject, "smart layer created")
  assert_eq(smartLayer.linkedFrameCount, 3, "linked frame count is 3")

  -- Verify all 200 frames have cels populated
  for f = 1, 200 do
    local c = smartLayer:cel(f)
    assert_true(c ~= nil, string.format("cel exists at frame %d", f))
    local expectedLinked = ((f - 1) % 3) + 1
    local px = c.image:getPixel(0, 0)
    if expectedLinked == 1 then
      assert_eq(app.pixelColor.rgbaR(px), 255, "frame is red")
    elseif expectedLinked == 2 then
      assert_eq(app.pixelColor.rgbaG(px), 255, "frame is green")
    else
      assert_eq(app.pixelColor.rgbaB(px), 255, "frame is blue")
    end
  end

  -- Simulate rapid scrubbing 500 times back and forth
  for iter = 1, 500 do
    local f = ((iter * 17) % 200) + 1
    local c = smartLayer:cel(f)
    assert_true(c ~= nil, "scrubbed cel not null")
    assert_true(c.image ~= nil, "scrubbed cel image not null")
  end

  spr:close()
  print("  -> TEST 1 PASSED: 200 frames scrubbed 500 times with zero failures.")
end

-- ==============================================================================
-- TEST 2: Missing or Corrupted Linked Document
-- ==============================================================================
print("\n[TEST 2] Missing / Non-Existent Linked Document Handling...")
do
  local spr = Sprite(32, 32)
  spr:newEmptyFrame()

  -- Attempt to place a non-existent file
  local ok, err = pcall(function()
    app.command.PlaceSmartObject{
      filename = "tests/smart_object/fixtures/non_existent_file_xyz.aseprite",
      ui = false
    }
  end)

  -- Command should fail safely without crashing host or corruption
  assert_eq(#spr.layers, 1, "no smart layer added on invalid file")
  spr:close()
  print("  -> TEST 2 PASSED: Missing file handled gracefully without crash.")
end

-- ==============================================================================
-- TEST 3: Extreme Frame Shrink & Grow (Dynamic Cel Pruning & Memory Lifecycle)
-- ==============================================================================
print("\n[TEST 3] Host Frame Shrink & Growth (Cel Pruning & Re-linking)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 49 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 50, "initial host has 50 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local smartLayer = spr.layers[2]
  assert_eq(#smartLayer.cels, 50, "50 cels initially")

  -- Delete frames down to 1 frame
  for i = 50, 2, -1 do
    spr:deleteFrame(i)
  end
  assert_eq(#spr.frames, 1, "host reduced to 1 frame")

  -- Verify smartLayer now has exactly 1 cel and no dangling/stale cels
  assert_eq(#smartLayer.cels, 1, "smartLayer pruned to 1 cel")
  local c1 = smartLayer:cel(1)
  assert_true(c1 ~= nil, "frame 1 cel exists")
  assert_eq(app.pixelColor.rgbaR(c1.image:getPixel(0, 0)), 255, "frame 1 pixel is red")

  -- Now expand frames back to 10
  for i = 2, 10 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10, "host expanded to 10 frames")

  -- Call syncTimeline to re-populate
  smartLayer:syncTimeline()
  assert_eq(#smartLayer.cels, 10, "smartLayer re-populated to 10 cels")

  -- Verify modulo alignment
  for f = 1, 10 do
    local c = smartLayer:cel(f)
    assert_true(c ~= nil, string.format("cel %d exists", f))
    local expected = ((f - 1) % 3) + 1
    local px = c.image:getPixel(0, 0)
    if expected == 1 then
      assert_eq(app.pixelColor.rgbaR(px), 255)
    elseif expected == 2 then
      assert_eq(app.pixelColor.rgbaG(px), 255)
    else
      assert_eq(app.pixelColor.rgbaB(px), 255)
    end
  end

  spr:close()
  print("  -> TEST 3 PASSED: Frame shrink and growth handled cleanly.")
end

-- ==============================================================================
-- TEST 4: Timeline Mapping Modes (Loop, Hold, Fixed) Under Stress
-- ==============================================================================
print("\n[TEST 4] Timeline Mapping Modes (Loop, Hold, Fixed)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 6, "host has 6 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local smartLayer = spr.layers[2]

  -- 1. Mode: Hold
  smartLayer:syncTimeline("Hold")
  assert_eq(smartLayer.timelineMapping, "Hold")
  -- Frames 1, 2, 3 correspond to source 1, 2, 3. Frames 4, 5, 6 must hold frame 3 (Blue)
  for f = 3, 6 do
    local c = smartLayer:cel(f)
    local px = c.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaB(px), 255, string.format("frame %d is held blue", f))
  end

  -- 2. Mode: Fixed
  smartLayer:syncTimeline("Fixed")
  assert_eq(smartLayer.timelineMapping, "Fixed")
  -- All frames must be fixed to frame 1 (Red)
  for f = 1, 6 do
    local c = smartLayer:cel(f)
    local px = c.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, string.format("frame %d is fixed red", f))
  end

  -- 3. Mode: Loop
  smartLayer:syncTimeline("Loop")
  assert_eq(smartLayer.timelineMapping, "Loop")
  for f = 1, 6 do
    local expected = ((f - 1) % 3) + 1
    local px = smartLayer:cel(f).image:getPixel(0, 0)
    if expected == 1 then assert_eq(app.pixelColor.rgbaR(px), 255)
    elseif expected == 2 then assert_eq(app.pixelColor.rgbaG(px), 255)
    else assert_eq(app.pixelColor.rgbaB(px), 255) end
  end

  spr:close()
  print("  -> TEST 4 PASSED: Timeline mapping mode switching verified.")
end

-- ==============================================================================
-- TEST 5: Re-render Canonical Cel and Verify Instant Linked Cel Update
-- ==============================================================================
print("\n[TEST 5] Canonical Cel Re-render & Linked Cel Mutation Verification...")
do
  local spr = Sprite(32, 32)
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }
  local smartLayer = spr.layers[2]

  -- In 6 frames, frame 1 and frame 4 map to source frame 1
  -- Frame 2 and frame 5 map to source frame 2
  -- Frame 3 and frame 6 map to source frame 3
  local img1_before = smartLayer:cel(1).image.id
  local img4_before = smartLayer:cel(4).image.id
  assert_eq(img1_before, img4_before, "frame 1 and frame 4 share image buffer id")

  -- Apply a 2D scale transformation to smart layer
  smartLayer.scale = { x = 2.0, y = 2.0 }
  -- renderAllCels was called by setter
  assert_eq(smartLayer:cel(1).image.width, 32, "frame 1 scaled to 32")
  assert_eq(smartLayer:cel(4).image.width, 32, "frame 4 scaled to 32")

  -- Verify image buffer is STILL shared between frame 1 and frame 4
  local img1_after = smartLayer:cel(1).image.id
  local img4_after = smartLayer:cel(4).image.id
  assert_eq(img1_after, img4_after, "frame 1 and frame 4 STILL share image buffer id")

  spr:close()
  print("  -> TEST 5 PASSED: CelData sharing preserved across re-rendering.")
end

print("\n================================================================================")
print("     ALL ADVERSARIAL STRESS TESTS COMPLETED SUCCESSFULLY (5/5 PASS)             ")
print("================================================================================")
