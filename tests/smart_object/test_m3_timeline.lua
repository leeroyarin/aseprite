-- test_m3_timeline.lua
-- Milestone 3: Animation Timeline Synchronization & Multi-Frame Cel Mapping Tests
-- Validates:
--   Scenario 1: Single-frame smart object in multi-frame host
--   Scenario 2: 3-frame smart object in 3-frame host (1-to-1 sync)
--   Scenario 3: 3-frame smart object wrapped in 6-frame host (modulo wrapping, cel linking & dynamic growth)
--   Scenario 4: Multi-frame smart object with 2D transformations

print("======================================================")
print("  Running Milestone 3 Native Timeline Test Suite       ")
print("======================================================")

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
-- Scenario 1: Single-frame Smart Object in Multi-Frame Host
-- ==============================================================================
print("\n[Scenario 1] Single-frame Smart Object in Multi-Frame Host...")
do
  local spr = Sprite(64, 64)
  for i = 1, 4 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 5, "host sprite has 5 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
    ui = false
  }

  assert_eq(#spr.layers, 2, "sprite has 2 layers")
  local smartLayer = spr.layers[2]
  assert_true(smartLayer.isSmartObject, "layer is a smart object")
  assert_eq(smartLayer.linkedFrameCount, 1, "linked frame count is 1")
  assert_eq(smartLayer.timelineMapping, "Loop", "default timeline mapping is Loop")

  -- Check all 5 frames have cels populated with 16x16 red/green pixels
  for f = 1, 5 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("cel exists for host frame %d", f))
    assert_eq(cel.image.width, 16, string.format("frame %d cel width is 16", f))
    assert_eq(cel.image.height, 16, string.format("frame %d cel height is 16", f))

    local pxTL = cel.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(pxTL), 255, string.format("frame %d TL is Red", f))
    local pxTR = cel.image:getPixel(8, 0)
    assert_eq(app.pixelColor.rgbaG(pxTR), 255, string.format("frame %d TR is Green", f))
  end

  -- Verify image buffer memory sharing across frames (Cel::MakeLink)
  local canonicalImageId = smartLayer:cel(1).image.id
  for f = 2, 5 do
    assert_eq(smartLayer:cel(f).image.id, canonicalImageId,
      string.format("frame %d shares image buffer id with frame 1", f))
  end

  spr:close()
  print("  -> Scenario 1 PASSED.")
end

-- ==============================================================================
-- Scenario 2: 3-Frame Smart Object in 3-Frame Host (1-to-1 Synchronization)
-- ==============================================================================
print("\n[Scenario 2] 3-Frame Smart Object in 3-Frame Host (1-to-1 Sync)...")
do
  local spr = Sprite(32, 32)
  spr:newEmptyFrame()
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 3, "host sprite has 3 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_true(smartLayer.isSmartObject, "layer is smart object")
  assert_eq(smartLayer.linkedFrameCount, 3, "linked frame count is 3")

  -- Frame 1: Red (255, 0, 0)
  local c1 = smartLayer:cel(1)
  assert_true(c1 ~= nil, "frame 1 cel exists")
  local px1 = c1.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(px1), 255, "frame 1 pixel is Red")
  assert_eq(app.pixelColor.rgbaG(px1), 0, "frame 1 pixel G is 0")
  assert_eq(app.pixelColor.rgbaB(px1), 0, "frame 1 pixel B is 0")

  -- Frame 2: Green (0, 255, 0)
  local c2 = smartLayer:cel(2)
  assert_true(c2 ~= nil, "frame 2 cel exists")
  local px2 = c2.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(px2), 0, "frame 2 pixel R is 0")
  assert_eq(app.pixelColor.rgbaG(px2), 255, "frame 2 pixel is Green")
  assert_eq(app.pixelColor.rgbaB(px2), 0, "frame 2 pixel B is 0")

  -- Frame 3: Blue (0, 0, 255)
  local c3 = smartLayer:cel(3)
  assert_true(c3 ~= nil, "frame 3 cel exists")
  local px3 = c3.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(px3), 0, "frame 3 pixel R is 0")
  assert_eq(app.pixelColor.rgbaG(px3), 0, "frame 3 pixel G is 0")
  assert_eq(app.pixelColor.rgbaB(px3), 255, "frame 3 pixel is Blue")

  -- All 3 frames are canonical cels (distinct image buffers)
  assert_true(c1.image.id ~= c2.image.id, "c1 and c2 have distinct image buffers")
  assert_true(c2.image.id ~= c3.image.id, "c2 and c3 have distinct image buffers")

  spr:close()
  print("  -> Scenario 2 PASSED.")
end

-- ==============================================================================
-- Scenario 3: 3-Frame Smart Object Wrapped in 6-Frame Host (Modulo Wrapping & Dynamic Growth)
-- ==============================================================================
print("\n[Scenario 3] 3-Frame Smart Object Wrapped in 6-Frame Host...")
do
  local spr = Sprite(32, 32)
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 6, "host sprite has 6 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(smartLayer.linkedFrameCount, 3, "linkedFrameCount is 3")

  -- Verify mapTimelineFrame mapping
  assert_eq(smartLayer:mapTimelineFrame(1), 1, "frame 1 -> 1")
  assert_eq(smartLayer:mapTimelineFrame(2), 2, "frame 2 -> 2")
  assert_eq(smartLayer:mapTimelineFrame(3), 3, "frame 3 -> 3")
  assert_eq(smartLayer:mapTimelineFrame(4), 1, "frame 4 -> 1 (modulo)")
  assert_eq(smartLayer:mapTimelineFrame(5), 2, "frame 5 -> 2 (modulo)")
  assert_eq(smartLayer:mapTimelineFrame(6), 3, "frame 6 -> 3 (modulo)")
  assert_eq(smartLayer:mapTimelineFrame(10), 1, "frame 10 -> 1 (modulo)")

  -- Verify modulo sequence colors: Red, Green, Blue, Red, Green, Blue
  local expectedColors = { "Red", "Green", "Blue", "Red", "Green", "Blue" }
  for f = 1, 6 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("cel for frame %d exists", f))
    local px = cel.image:getPixel(0, 0)
    local col = expectedColors[f]
    if col == "Red" then
      assert_eq(app.pixelColor.rgbaR(px), 255, string.format("frame %d is Red", f))
    elseif col == "Green" then
      assert_eq(app.pixelColor.rgbaG(px), 255, string.format("frame %d is Green", f))
    elseif col == "Blue" then
      assert_eq(app.pixelColor.rgbaB(px), 255, string.format("frame %d is Blue", f))
    end
  end

  -- Verify Cel::MakeLink memory sharing:
  -- Frames 4, 5, 6 share image IDs with canonical frames 1, 2, 3
  assert_eq(smartLayer:cel(4).image.id, smartLayer:cel(1).image.id, "frame 4 links to frame 1")
  assert_eq(smartLayer:cel(5).image.id, smartLayer:cel(2).image.id, "frame 5 links to frame 2")
  assert_eq(smartLayer:cel(6).image.id, smartLayer:cel(3).image.id, "frame 6 links to frame 3")

  -- Test timelineMapping "Hold" mode
  smartLayer.timelineMapping = "Hold"
  assert_eq(smartLayer.timelineMapping, "Hold", "mapping mode set to Hold")
  smartLayer:syncTimeline()
  assert_eq(smartLayer:mapTimelineFrame(1), 1, "Hold frame 1 -> 1")
  assert_eq(smartLayer:mapTimelineFrame(3), 3, "Hold frame 3 -> 3")
  assert_eq(smartLayer:mapTimelineFrame(4), 3, "Hold frame 4 -> 3 (clamped)")
  assert_eq(smartLayer:mapTimelineFrame(6), 3, "Hold frame 6 -> 3 (clamped)")
  -- Frame 4 should now display Blue (frame 3)
  local pxHold4 = smartLayer:cel(4).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaB(pxHold4), 255, "Hold frame 4 displays Blue")

  -- Test timelineMapping "Fixed" mode
  smartLayer.timelineMapping = "Fixed"
  assert_eq(smartLayer.timelineMapping, "Fixed", "mapping mode set to Fixed")
  smartLayer:syncTimeline()
  assert_eq(smartLayer:mapTimelineFrame(1), 1, "Fixed frame 1 -> 1")
  assert_eq(smartLayer:mapTimelineFrame(3), 1, "Fixed frame 3 -> 1")
  assert_eq(smartLayer:mapTimelineFrame(5), 1, "Fixed frame 5 -> 1")
  local pxFixed3 = smartLayer:cel(3).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(pxFixed3), 255, "Fixed frame 3 displays Red")

  -- Return to "Loop" mode
  smartLayer.timelineMapping = "Loop"
  smartLayer:syncTimeline()
  assert_eq(smartLayer:mapTimelineFrame(4), 1, "Restored Loop frame 4 -> 1")
  local pxLoop4 = smartLayer:cel(4).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(pxLoop4), 255, "Restored Loop frame 4 is Red")

  -- Test Dynamic Host Growth (add frame 7)
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 7, "host sprite now has 7 frames")
  smartLayer:syncTimeline()
  local c7 = smartLayer:cel(7)
  assert_true(c7 ~= nil, "dynamic frame 7 cel exists")
  assert_eq(smartLayer:mapTimelineFrame(7), 1, "frame 7 maps to 1 (7%3 == 1)")
  local px7 = c7.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(px7), 255, "dynamic frame 7 displays Red")
  assert_eq(c7.image.id, smartLayer:cel(1).image.id, "dynamic frame 7 shares image with frame 1")

  spr:close()
  print("  -> Scenario 3 PASSED.")
end

-- ==============================================================================
-- Scenario 4: Multi-Frame Smart Object with 2D Transformations
-- ==============================================================================
print("\n[Scenario 4] Multi-Frame Smart Object with 2D Transformations...")
do
  local spr = Sprite(64, 64)
  spr:newEmptyFrame()
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 3, "host sprite has 3 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]

  -- Apply 2D Transformations: scale 2.0x, rotation 90, flipH true, translate (10, 10)
  smartLayer.scale = { 2.0, 2.0 }
  smartLayer.angle = 90.0
  smartLayer.flipH = true
  smartLayer:translate(10, 10)

  -- Verify all 3 frames scaled to 32x32
  for f = 1, 3 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("transformed cel %d exists", f))
    assert_eq(cel.image.width, 32, string.format("frame %d transformed cel width is 32", f))
    assert_eq(cel.image.height, 32, string.format("frame %d transformed cel height is 32", f))
  end

  -- Verify rotated & mirrored colors on all 3 frames
  local pxT1 = smartLayer:cel(1).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(pxT1), 255, "frame 1 transformed pixel is Red")
  local pxT2 = smartLayer:cel(2).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaG(pxT2), 255, "frame 2 transformed pixel is Green")
  local pxT3 = smartLayer:cel(3).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaB(pxT3), 255, "frame 3 transformed pixel is Blue")

  -- Reset Transform
  smartLayer:resetTransform()
  for f = 1, 3 do
    local cel = smartLayer:cel(f)
    assert_eq(cel.image.width, 16, string.format("frame %d reset cel width is 16", f))
    assert_eq(cel.image.height, 16, string.format("frame %d reset cel height is 16", f))
  end

  local pxR1 = smartLayer:cel(1).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(pxR1), 255, "reset frame 1 pixel is pristine Red")
  local pxR2 = smartLayer:cel(2).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaG(pxR2), 255, "reset frame 2 pixel is pristine Green")
  local pxR3 = smartLayer:cel(3).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaB(pxR3), 255, "reset frame 3 pixel is pristine Blue")

  spr:close()
  print("  -> Scenario 4 PASSED.")
end

print("\n======================================================")
print("  ALL MILESTONE 3 TIMELINE SCENARIOS PASSED (4/4)!     ")
print("======================================================")
