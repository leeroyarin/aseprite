-- test_m3_edge_challenger.lua
-- Empirical Challenger Test Suite for Milestone 3 (Cel Linking, Memory Footprint & Edge Cases)
-- Tests:
--   1. Cel linking verification (6-frame host, 3-frame linked object: cel 4 links to cel 1, 5 to 2, 6 to 3)
--   2. Transform propagation across linked cels (90 deg, 45 deg, scaling, reset)
--   3. Timeline mode switching (Loop, Hold, Fixed: frame queries, pixel values, image sharing)
--   4. Single-frame smart object in 20-frame host (memory footprint, 1 unique image id, dynamic growth/shrinkage)
--   5. Adversarial edge cases (1-frame host expansion, asymmetric pixel rotation test)

print("======================================================================")
print("  Milestone 3 Empirical Challenger Test Harness: Cel Linking & Edge   ")
print("======================================================================")

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

local function assert_pixels_equal(img1, img2, msg)
  assert_eq(img1.width, img2.width, (msg or "") .. " width match")
  assert_eq(img1.height, img2.height, (msg or "") .. " height match")
  for y = 0, img1.height - 1 do
    for x = 0, img1.width - 1 do
      local p1 = img1:getPixel(x, y)
      local p2 = img2:getPixel(x, y)
      if p1 ~= p2 then
        error(string.format("ASSERTION FAILED: pixel mismatch at (%d, %d): %s (img1=%08X, img2=%08X)",
          x, y, msg or "", p1, p2), 2)
      end
    end
  end
end

-- ==============================================================================
-- Test 1: Cel Linking Verification
-- Host: 6 frames, Linked Object: 3 frames (multi_frame_3f.aseprite)
-- Cel 4 shares CelData with Cel 1, Cel 5 with Cel 2, Cel 6 with Cel 3
-- ==============================================================================
print("\n[Test 1] Cel Linking Verification (6-frame host, 3-frame linked object)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 6, "host sprite initialized with 6 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_true(smartLayer ~= nil, "smart layer exists")
  assert_true(smartLayer.isSmartObject, "layer is a smart object")
  assert_eq(smartLayer.linkedFrameCount, 3, "linked frame count is 3")

  local c1 = smartLayer:cel(1)
  local c2 = smartLayer:cel(2)
  local c3 = smartLayer:cel(3)
  local c4 = smartLayer:cel(4)
  local c5 = smartLayer:cel(5)
  local c6 = smartLayer:cel(6)

  assert_true(c1 ~= nil, "cel 1 exists")
  assert_true(c2 ~= nil, "cel 2 exists")
  assert_true(c3 ~= nil, "cel 3 exists")
  assert_true(c4 ~= nil, "cel 4 exists")
  assert_true(c5 ~= nil, "cel 5 exists")
  assert_true(c6 ~= nil, "cel 6 exists")

  -- 1a. Cel linking via userdata equality (__eq operator)
  assert_true(c4.image == c1.image, "cel(4).image == cel(1).image (__eq)")
  assert_true(c5.image == c2.image, "cel(5).image == cel(2).image (__eq)")
  assert_true(c6.image == c3.image, "cel(6).image == cel(3).image (__eq)")

  -- 1b. Cel linking via Image ID equality
  assert_eq(c4.image.id, c1.image.id, "cel 4 shares Image ID with cel 1")
  assert_eq(c5.image.id, c2.image.id, "cel 5 shares Image ID with cel 2")
  assert_eq(c6.image.id, c3.image.id, "cel 6 shares Image ID with cel 3")

  -- 1c. Canonical cels 1, 2, 3 must have distinct Image IDs
  assert_true(c1.image.id ~= c2.image.id, "c1 and c2 have distinct image IDs")
  assert_true(c2.image.id ~= c3.image.id, "c2 and c3 have distinct image IDs")
  assert_true(c1.image.id ~= c3.image.id, "c1 and c3 have distinct image IDs")

  -- 1d. Pixel color verification across modulo loop
  -- Frame 1 & 4: Red (255, 0, 0)
  assert_eq(app.pixelColor.rgbaR(c1.image:getPixel(0, 0)), 255, "cel 1 pixel R is 255")
  assert_eq(app.pixelColor.rgbaG(c1.image:getPixel(0, 0)), 0, "cel 1 pixel G is 0")
  assert_eq(app.pixelColor.rgbaB(c1.image:getPixel(0, 0)), 0, "cel 1 pixel B is 0")
  assert_eq(app.pixelColor.rgbaR(c4.image:getPixel(0, 0)), 255, "cel 4 pixel R is 255")
  assert_eq(app.pixelColor.rgbaG(c4.image:getPixel(0, 0)), 0, "cel 4 pixel G is 0")
  assert_eq(app.pixelColor.rgbaB(c4.image:getPixel(0, 0)), 0, "cel 4 pixel B is 0")

  -- Frame 2 & 5: Green (0, 255, 0)
  assert_eq(app.pixelColor.rgbaR(c2.image:getPixel(0, 0)), 0, "cel 2 pixel R is 0")
  assert_eq(app.pixelColor.rgbaG(c2.image:getPixel(0, 0)), 255, "cel 2 pixel G is 255")
  assert_eq(app.pixelColor.rgbaB(c2.image:getPixel(0, 0)), 0, "cel 2 pixel B is 0")
  assert_eq(app.pixelColor.rgbaR(c5.image:getPixel(0, 0)), 0, "cel 5 pixel R is 0")
  assert_eq(app.pixelColor.rgbaG(c5.image:getPixel(0, 0)), 255, "cel 5 pixel G is 255")
  assert_eq(app.pixelColor.rgbaB(c5.image:getPixel(0, 0)), 0, "cel 5 pixel B is 0")

  -- Frame 3 & 6: Blue (0, 0, 255)
  assert_eq(app.pixelColor.rgbaR(c3.image:getPixel(0, 0)), 0, "cel 3 pixel R is 0")
  assert_eq(app.pixelColor.rgbaG(c3.image:getPixel(0, 0)), 0, "cel 3 pixel G is 0")
  assert_eq(app.pixelColor.rgbaB(c3.image:getPixel(0, 0)), 255, "cel 3 pixel B is 255")
  assert_eq(app.pixelColor.rgbaR(c6.image:getPixel(0, 0)), 0, "cel 6 pixel R is 0")
  assert_eq(app.pixelColor.rgbaG(c6.image:getPixel(0, 0)), 0, "cel 6 pixel G is 0")
  assert_eq(app.pixelColor.rgbaB(c6.image:getPixel(0, 0)), 255, "cel 6 pixel B is 255")

  -- 1e. Bounds and position equality
  assert_eq(c4.position.x, c1.position.x, "cel 4 pos X == cel 1 pos X")
  assert_eq(c4.position.y, c1.position.y, "cel 4 pos Y == cel 1 pos Y")
  assert_eq(c4.bounds.width, c1.bounds.width, "cel 4 bounds width == cel 1 bounds width")
  assert_eq(c4.bounds.height, c1.bounds.height, "cel 4 bounds height == cel 1 bounds height")

  assert_eq(c5.position.x, c2.position.x, "cel 5 pos X == cel 2 pos X")
  assert_eq(c5.position.y, c2.position.y, "cel 5 pos Y == cel 2 pos Y")
  assert_eq(c6.position.x, c3.position.x, "cel 6 pos X == cel 3 pos X")
  assert_eq(c6.position.y, c3.position.y, "cel 6 pos Y == cel 3 pos Y")

  spr:close()
  print("  -> Test 1 PASSED.")
end

-- ==============================================================================
-- Test 2: Transform Propagation Across Linked Cels
-- Rotate smart object layer by 90 deg and 45 deg
-- Verify cel 1 and cel 4 both immediately show transformed bounds and pixels
-- ==============================================================================
print("\n[Test 2] Transform Propagation Across Linked Cels (90 deg, 45 deg, Scale)...")
do
  local spr = Sprite(64, 64)
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 6, "host sprite has 6 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]

  -- 2a. Rotate 90 degrees
  smartLayer.angle = 90.0
  assert_eq(smartLayer.angle, 90.0, "angle set to 90.0")

  local c1 = smartLayer:cel(1)
  local c4 = smartLayer:cel(4)

  -- Cel 1 and Cel 4 must still share the exact same image
  assert_true(c4.image == c1.image, "rotated 90: cel 4 image == cel 1 image")
  assert_eq(c4.image.id, c1.image.id, "rotated 90: cel 4 image id == cel 1 image id")

  -- Transformed bounds must match exactly
  assert_eq(c4.bounds.width, c1.bounds.width, "rotated 90: width matches between c1 and c4")
  assert_eq(c4.bounds.height, c1.bounds.height, "rotated 90: height matches between c1 and c4")
  assert_eq(c4.position.x, c1.position.x, "rotated 90: pos X matches")
  assert_eq(c4.position.y, c1.position.y, "rotated 90: pos Y matches")

  -- Check rotated pixels (16x16 red image rotated 90 is still 16x16 red)
  assert_eq(c1.image.width, 16, "rotated 90: width is 16")
  assert_eq(c1.image.height, 16, "rotated 90: height is 16")
  assert_eq(c4.image.width, 16, "rotated 90: cel 4 width is 16")
  assert_eq(c4.image.height, 16, "rotated 90: cel 4 height is 16")
  assert_eq(app.pixelColor.rgbaR(c1.image:getPixel(0, 0)), 255, "rotated 90: c1 is red")
  assert_eq(app.pixelColor.rgbaR(c4.image:getPixel(0, 0)), 255, "rotated 90: c4 is red")

  -- Also check frames 2 & 5 (Green) and 3 & 6 (Blue)
  local c2 = smartLayer:cel(2)
  local c5 = smartLayer:cel(5)
  assert_eq(c5.image.id, c2.image.id, "rotated 90: c5 shares image with c2")
  assert_eq(app.pixelColor.rgbaG(c2.image:getPixel(0, 0)), 255, "rotated 90: c2 is green")
  assert_eq(app.pixelColor.rgbaG(c5.image:getPixel(0, 0)), 255, "rotated 90: c5 is green")

  local c3 = smartLayer:cel(3)
  local c6 = smartLayer:cel(6)
  assert_eq(c6.image.id, c3.image.id, "rotated 90: c6 shares image with c3")
  assert_eq(app.pixelColor.rgbaB(c3.image:getPixel(0, 0)), 255, "rotated 90: c3 is blue")
  assert_eq(app.pixelColor.rgbaB(c6.image:getPixel(0, 0)), 255, "rotated 90: c6 is blue")

  -- 2b. Rotate 45 degrees (diagonal bounding box expansion)
  smartLayer.angle = 45.0
  assert_eq(smartLayer.angle, 45.0, "angle set to 45.0")

  c1 = smartLayer:cel(1)
  c4 = smartLayer:cel(4)

  assert_true(c4.image == c1.image, "rotated 45: cel 4 image == cel 1 image")
  assert_eq(c4.image.id, c1.image.id, "rotated 45: cel 4 image id == cel 1 image id")
  assert_true(c1.image.width > 16, string.format("rotated 45: diagonal width expanded (>16, got %d)", c1.image.width))
  assert_true(c1.image.height > 16, string.format("rotated 45: diagonal height expanded (>16, got %d)", c1.image.height))
  assert_eq(c4.image.width, c1.image.width, "rotated 45: cel 4 width == cel 1 width")
  assert_eq(c4.image.height, c1.image.height, "rotated 45: cel 4 height == cel 1 height")
  assert_eq(c4.position.x, c1.position.x, "rotated 45: position X matches")
  assert_eq(c4.position.y, c1.position.y, "rotated 45: position Y matches")

  -- Center pixel of rotated 45 image must be Red
  local midX = math.floor(c1.image.width / 2)
  local midY = math.floor(c1.image.height / 2)
  assert_eq(app.pixelColor.rgbaR(c1.image:getPixel(midX, midY)), 255, "rotated 45: center pixel c1 is red")
  assert_eq(app.pixelColor.rgbaR(c4.image:getPixel(midX, midY)), 255, "rotated 45: center pixel c4 is red")

  -- 2c. Combined Transform (Scale 2.0x, Translate +10, +10)
  smartLayer.scale = { 2.0, 2.0 }
  smartLayer:translate(10, 10)

  c1 = smartLayer:cel(1)
  c4 = smartLayer:cel(4)
  assert_true(c4.image == c1.image, "scaled+translated: cel 4 image == cel 1 image")
  assert_eq(c4.image.id, c1.image.id, "scaled+translated: cel 4 id == cel 1 id")
  assert_eq(c4.position.x, c1.position.x, "scaled+translated: pos X matches")
  assert_eq(c4.position.y, c1.position.y, "scaled+translated: pos Y matches")

  -- 2d. Reset Transform
  smartLayer:resetTransform()
  c1 = smartLayer:cel(1)
  c4 = smartLayer:cel(4)
  assert_eq(c1.image.width, 16, "reset transform: c1 width is 16")
  assert_eq(c1.image.height, 16, "reset transform: c1 height is 16")
  assert_eq(c4.image.width, 16, "reset transform: c4 width is 16")
  assert_eq(c4.image.height, 16, "reset transform: c4 height is 16")
  assert_true(c4.image == c1.image, "reset transform: cel 4 image == cel 1 image")
  assert_eq(c4.image.id, c1.image.id, "reset transform: cel 4 id == cel 1 id")

  spr:close()
  print("  -> Test 2 PASSED.")
end

-- ==============================================================================
-- Test 3: Timeline Mode Switching (Loop, Hold, Fixed)
-- In 10-frame host with 3-frame linked object
-- Hold: frame 4..10 all display frame 3 (Blue)
-- Fixed: frame 1..10 all display frame 1 (Red)
-- Loop: frames wrap 1, 2, 3, 1, 2, 3, 1, 2, 3, 1
-- ==============================================================================
print("\n[Test 3] Timeline Mode Switching (Loop, Hold, Fixed)...")
do
  local spr = Sprite(32, 32)
  for i = 1, 9 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 10, "host sprite has 10 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(smartLayer.linkedFrameCount, 3, "linked frame count is 3")

  -- 3a. Verify Default Loop Mode
  assert_eq(smartLayer.timelineMapping, "Loop", "initial mode is Loop")
  local loopExpected = { 1, 2, 3, 1, 2, 3, 1, 2, 3, 1 }
  for f = 1, 10 do
    assert_eq(smartLayer:mapTimelineFrame(f), loopExpected[f],
      string.format("Loop mode: host frame %d maps to linked frame %d", f, loopExpected[f]))
  end

  -- Verify Loop colors
  for f = 1, 10 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("Loop frame %d cel exists", f))
    local px = cel.image:getPixel(0, 0)
    local targetLinked = loopExpected[f]
    if targetLinked == 1 then
      assert_eq(app.pixelColor.rgbaR(px), 255, string.format("Loop frame %d is Red", f))
      assert_eq(cel.image.id, smartLayer:cel(1).image.id, string.format("Loop frame %d shares image 1", f))
    elseif targetLinked == 2 then
      assert_eq(app.pixelColor.rgbaG(px), 255, string.format("Loop frame %d is Green", f))
      assert_eq(cel.image.id, smartLayer:cel(2).image.id, string.format("Loop frame %d shares image 2", f))
    elseif targetLinked == 3 then
      assert_eq(app.pixelColor.rgbaB(px), 255, string.format("Loop frame %d is Blue", f))
      assert_eq(cel.image.id, smartLayer:cel(3).image.id, string.format("Loop frame %d shares image 3", f))
    end
  end

  -- 3b. Switch to Hold Mode
  smartLayer.timelineMapping = "Hold"
  assert_eq(smartLayer.timelineMapping, "Hold", "mapping switched to Hold")
  smartLayer:syncTimeline()

  -- In Hold mode, frames 1..3 map to 1..3, frames 4..10 map to 3 (Blue)
  assert_eq(smartLayer:mapTimelineFrame(1), 1, "Hold frame 1 maps to 1")
  assert_eq(smartLayer:mapTimelineFrame(2), 2, "Hold frame 2 maps to 2")
  assert_eq(smartLayer:mapTimelineFrame(3), 3, "Hold frame 3 maps to 3")
  for f = 4, 10 do
    assert_eq(smartLayer:mapTimelineFrame(f), 3,
      string.format("Hold mode: host frame %d clamps to linked frame 3", f))
  end

  -- Verify frames 4..10 all display Blue (frame 3) and share image with frame 3
  local blueImageId = smartLayer:cel(3).image.id
  for f = 4, 10 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("Hold frame %d cel exists", f))
    local px = cel.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 0, string.format("Hold frame %d R is 0", f))
    assert_eq(app.pixelColor.rgbaG(px), 0, string.format("Hold frame %d G is 0", f))
    assert_eq(app.pixelColor.rgbaB(px), 255, string.format("Hold frame %d displays Blue (frame 3)", f))
    assert_true(cel.image == smartLayer:cel(3).image,
      string.format("Hold frame %d image == frame 3 image", f))
    assert_eq(cel.image.id, blueImageId,
      string.format("Hold frame %d shares image ID with frame 3", f))
  end

  -- 3c. Switch to Fixed Mode
  smartLayer.timelineMapping = "Fixed"
  assert_eq(smartLayer.timelineMapping, "Fixed", "mapping switched to Fixed")
  smartLayer:syncTimeline()

  -- In Fixed mode, all frames 1..10 map to frame 1 (Red)
  for f = 1, 10 do
    assert_eq(smartLayer:mapTimelineFrame(f), 1,
      string.format("Fixed mode: host frame %d maps to linked frame 1", f))
  end

  -- Verify frames 1..10 all display Red (frame 1) and share image with frame 1
  local redImageId = smartLayer:cel(1).image.id
  for f = 1, 10 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("Fixed frame %d cel exists", f))
    local px = cel.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, string.format("Fixed frame %d displays Red (frame 1)", f))
    assert_eq(app.pixelColor.rgbaG(px), 0, string.format("Fixed frame %d G is 0", f))
    assert_eq(app.pixelColor.rgbaB(px), 0, string.format("Fixed frame %d B is 0", f))
    assert_true(cel.image == smartLayer:cel(1).image,
      string.format("Fixed frame %d image == frame 1 image", f))
    assert_eq(cel.image.id, redImageId,
      string.format("Fixed frame %d shares image ID with frame 1", f))
  end

  -- 3d. Switch back to Loop Mode and verify full restoration
  smartLayer.timelineMapping = "Loop"
  assert_eq(smartLayer.timelineMapping, "Loop", "restored to Loop mode")
  smartLayer:syncTimeline()

  for f = 1, 10 do
    assert_eq(smartLayer:mapTimelineFrame(f), loopExpected[f],
      string.format("Restored Loop: host frame %d maps to %d", f, loopExpected[f]))
  end
  assert_eq(app.pixelColor.rgbaR(smartLayer:cel(4).image:getPixel(0, 0)), 255, "Restored Loop frame 4 is Red")
  assert_eq(app.pixelColor.rgbaG(smartLayer:cel(5).image:getPixel(0, 0)), 255, "Restored Loop frame 5 is Green")
  assert_eq(app.pixelColor.rgbaB(smartLayer:cel(6).image:getPixel(0, 0)), 255, "Restored Loop frame 6 is Blue")

  -- 3e. Repeated rapid mode switching stress test
  for iter = 1, 5 do
    smartLayer.timelineMapping = "Hold"
    smartLayer:syncTimeline()
    assert_eq(app.pixelColor.rgbaB(smartLayer:cel(10).image:getPixel(0, 0)), 255, "Rapid toggle Hold is Blue")

    smartLayer.timelineMapping = "Fixed"
    smartLayer:syncTimeline()
    assert_eq(app.pixelColor.rgbaR(smartLayer:cel(10).image:getPixel(0, 0)), 255, "Rapid toggle Fixed is Red")

    smartLayer.timelineMapping = "Loop"
    smartLayer:syncTimeline()
    assert_eq(app.pixelColor.rgbaR(smartLayer:cel(10).image:getPixel(0, 0)), 255, "Rapid toggle Loop 10 is Red")
  end

  spr:close()
  print("  -> Test 3 PASSED.")
end

-- ==============================================================================
-- Test 4: Single-Frame Smart Object in 20-Frame Host (Memory Footprint)
-- Single-frame smart object placed in 20-frame host:
-- Verify all 20 frames share the single cel image and no redundant raster allocations exist
-- ==============================================================================
print("\n[Test 4] Single-frame Smart Object in 20-frame Host (Memory Footprint)...")
do
  local spr = Sprite(64, 64)
  for i = 1, 19 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 20, "host sprite initialized with 20 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_true(smartLayer ~= nil, "smart layer exists")
  assert_eq(smartLayer.linkedFrameCount, 1, "linked frame count is 1")

  -- 4a. Verify all 20 frames have non-nil cels
  for f = 1, 20 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("frame %d cel exists", f))
    assert_eq(cel.image.width, 16, string.format("frame %d cel width is 16", f))
    assert_eq(cel.image.height, 16, string.format("frame %d cel height is 16", f))
  end

  -- 4b. Verify all 20 frames share the single cel image
  local canonicalImage = smartLayer:cel(1).image
  local canonicalId = canonicalImage.id

  local uniqueImageIds = {}
  uniqueImageIds[canonicalId] = true

  for f = 2, 20 do
    local cel = smartLayer:cel(f)
    -- Verify userdata equality
    assert_true(cel.image == canonicalImage,
      string.format("frame %d cel.image == canonicalImage (__eq)", f))
    -- Verify Image ID equality
    assert_eq(cel.image.id, canonicalId,
      string.format("frame %d shares canonical image ID %d", f, canonicalId))
    uniqueImageIds[cel.image.id] = true
  end

  -- 4c. Verify exactly 1 unique image allocation exists across all 20 cels
  local uniqueCount = 0
  for _ in pairs(uniqueImageIds) do
    uniqueCount = uniqueCount + 1
  end
  assert_eq(uniqueCount, 1, "EXACTLY 1 unique raster allocation across all 20 host frames")

  -- 4d. Dynamic host expansion: add 10 more frames (total 30)
  for i = 1, 10 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 30, "host sprite grown to 30 frames")
  smartLayer:syncTimeline()

  for f = 21, 30 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("expanded frame %d cel exists", f))
    assert_eq(cel.image.id, canonicalId,
      string.format("expanded frame %d shares canonical image ID %d", f, canonicalId))
  end

  -- Verify still exactly 1 unique raster allocation across all 30 frames
  local uniqueCount30 = 0
  local uniqueIds30 = {}
  for f = 1, 30 do
    uniqueIds30[smartLayer:cel(f).image.id] = true
  end
  for _ in pairs(uniqueIds30) do uniqueCount30 = uniqueCount30 + 1 end
  assert_eq(uniqueCount30, 1, "EXACTLY 1 unique raster allocation across 30 host frames after dynamic growth")

  -- 4e. Dynamic host shrinkage: remove 15 frames (down to 15 frames)
  while #spr.frames > 15 do
    spr:deleteFrame(spr.frames[#spr.frames])
  end
  assert_eq(#spr.frames, 15, "host sprite shrunk to 15 frames")
  smartLayer:syncTimeline()

  local uniqueCount15 = 0
  local uniqueIds15 = {}
  for f = 1, 15 do
    local cel = smartLayer:cel(f)
    assert_true(cel ~= nil, string.format("shrunk frame %d cel exists", f))
    uniqueIds15[cel.image.id] = true
  end
  for _ in pairs(uniqueIds15) do uniqueCount15 = uniqueCount15 + 1 end
  assert_eq(uniqueCount15, 1, "EXACTLY 1 unique raster allocation across 15 remaining frames")

  -- 4f. Transform applied to multi-frame single-object: verify 1 re-render updates all frames
  smartLayer.scale = { 2.0, 2.0 }
  smartLayer.angle = 90.0

  local transformedId = smartLayer:cel(1).image.id
  assert_eq(smartLayer:cel(1).image.width, 32, "transformed c1 width is 32")
  assert_eq(smartLayer:cel(1).image.height, 32, "transformed c1 height is 32")

  for f = 2, 15 do
    local cel = smartLayer:cel(f)
    assert_eq(cel.image.width, 32, string.format("frame %d transformed width is 32", f))
    assert_eq(cel.image.height, 32, string.format("frame %d transformed height is 32", f))
    assert_eq(cel.image.id, transformedId, string.format("frame %d shares transformed image id", f))
  end

  spr:close()
  print("  -> Test 4 PASSED.")
end

-- ==============================================================================
-- Test 5: Adversarial Edge Cases
-- 5a. Initial 1-frame host placing 3-frame object, followed by host expansion to 6 frames
-- 5b. Pixel-accurate asymmetric rotation test across linked cels
-- ==============================================================================
print("\n[Test 5] Adversarial Edge Cases...")
do
  -- 5a. 1-frame host placement -> 6-frame host expansion
  local spr = Sprite(32, 32)
  assert_eq(#spr.frames, 1, "host initialized with 1 frame")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
    ui = false
  }

  local smartLayer = spr.layers[2]
  assert_eq(#smartLayer.cels, 1, "1-frame host has exactly 1 cel")
  assert_eq(app.pixelColor.rgbaR(smartLayer:cel(1).image:getPixel(0, 0)), 255, "cel 1 is Red")

  -- Now expand host to 6 frames
  for i = 1, 5 do
    spr:newEmptyFrame()
  end
  assert_eq(#spr.frames, 6, "host expanded to 6 frames")
  smartLayer:syncTimeline()

  -- After syncTimeline, all 6 frames must be populated
  for f = 1, 6 do
    assert_true(smartLayer:cel(f) ~= nil, string.format("expanded cel %d exists", f))
  end
  -- Frames 1, 2, 3 must have distinct image IDs
  assert_true(smartLayer:cel(1).image.id ~= smartLayer:cel(2).image.id, "c1 and c2 distinct after expansion")
  assert_true(smartLayer:cel(2).image.id ~= smartLayer:cel(3).image.id, "c2 and c3 distinct after expansion")
  -- Frames 4, 5, 6 must link to 1, 2, 3
  assert_eq(smartLayer:cel(4).image.id, smartLayer:cel(1).image.id, "c4 linked to c1 after expansion")
  assert_eq(smartLayer:cel(5).image.id, smartLayer:cel(2).image.id, "c5 linked to c2 after expansion")
  assert_eq(smartLayer:cel(6).image.id, smartLayer:cel(3).image.id, "c6 linked to c3 after expansion")
  spr:close()

  -- 5b. Asymmetric Pixel Pattern Rotation across Linked Cels
  -- Using single_frame_16x16.aseprite (TL=Red, TR=Green, BL=Blue, BR=Yellow)
  local spr2 = Sprite(32, 32)
  for i = 1, 3 do
    spr2:newEmptyFrame()
  end
  assert_eq(#spr2.frames, 4, "host sprite has 4 frames")

  app.command.PlaceSmartObject{
    filename = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
    ui = false
  }
  local sl2 = spr2.layers[2]

  -- In pristine state:
  -- TL (0,0) is Red (255, 0, 0)
  -- TR (15,0) is Green (0, 255, 0)
  -- BL (0,15) is Blue (0, 0, 255)
  -- BR (15,15) is Yellow (255, 255, 0)
  local cPristine1 = sl2:cel(1)
  local cPristine4 = sl2:cel(4)
  assert_eq(app.pixelColor.rgbaR(cPristine1.image:getPixel(0, 0)), 255, "Pristine TL is Red")
  assert_eq(app.pixelColor.rgbaG(cPristine1.image:getPixel(15, 0)), 255, "Pristine TR is Green")
  assert_eq(app.pixelColor.rgbaB(cPristine1.image:getPixel(0, 15)), 255, "Pristine BL is Blue")
  assert_eq(app.pixelColor.rgbaR(cPristine1.image:getPixel(15, 15)), 255, "Pristine BR R is 255")
  assert_eq(app.pixelColor.rgbaG(cPristine1.image:getPixel(15, 15)), 255, "Pristine BR G is 255")

  -- Now rotate layer by 90 degrees clockwise
  sl2.angle = 90.0

  local cRot1 = sl2:cel(1)
  local cRot4 = sl2:cel(4)
  assert_true(cRot4.image == cRot1.image, "rotated asymmetric: cel 4 image == cel 1 image")
  assert_eq(cRot4.image.id, cRot1.image.id, "rotated asymmetric: cel 4 id == cel 1 id")

  -- After 90 deg clockwise rotation:
  -- Old TL (Red) -> New TR (15, 0)
  -- Old TR (Green) -> New BR (15, 15)
  -- Old BR (Yellow) -> New BL (0, 15)
  -- Old BL (Blue) -> New TL (0, 0)
  -- Verify both cel 1 and cel 4 have identical rotated pixel quadrants!
  for _, c in ipairs({ cRot1, cRot4 }) do
    local pxTL = c.image:getPixel(0, 0)
    local pxTR = c.image:getPixel(15, 0)
    local pxBL = c.image:getPixel(0, 15)
    local pxBR = c.image:getPixel(15, 15)

    assert_eq(app.pixelColor.rgbaB(pxTL), 255, "Rotated TL is now Blue")
    assert_eq(app.pixelColor.rgbaR(pxTR), 255, "Rotated TR is now Red")
    assert_eq(app.pixelColor.rgbaR(pxBL), 255, "Rotated BL is now Yellow (R=255)")
    assert_eq(app.pixelColor.rgbaG(pxBL), 255, "Rotated BL is now Yellow (G=255)")
    assert_eq(app.pixelColor.rgbaG(pxBR), 255, "Rotated BR is now Green")
  end

  spr2:close()
  print("  -> Test 5 PASSED.")
end

-- ==============================================================================
-- Summary
-- ==============================================================================
print("\n======================================================================")
print(string.format("  ALL EMPIRICAL CHALLENGER TESTS PASSED! (%d/%d assertions)  ", passCount, testCount))
print("======================================================================")
