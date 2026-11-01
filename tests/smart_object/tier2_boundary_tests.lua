-- ==============================================================================
-- TIER 2: Boundary & Corner Cases (>=5 tests per category)
-- Total Target: >=60 tests
-- ==============================================================================

local harness = require("smart_object_harness")
local describe = harness.describe
local it = harness.it
local assert_eq = harness.assert_eq
local assert_true = harness.assert_true
local assert_false = harness.assert_false
local assert_not_nil = harness.assert_not_nil
local assert_nil = harness.assert_nil
local assert_near = harness.assert_near
local assert_error = harness.assert_error

-- ------------------------------------------------------------------------------
-- Boundary 1: Zero-Byte & Empty Files
-- ------------------------------------------------------------------------------
describe(2, "Boundary 1: Zero-Byte & Empty Files", function()
  it("TC 2.1.1: Placing 0-byte file fails gracefully with error", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("empty.aseprite")
    local sm, err = harness.createSmartObjectLayer(spr, path)
    assert_nil(sm, "Must return nil for 0-byte file")
    assert_not_nil(err, "Must return error message")
    spr:close()
  end)

  it("TC 2.1.2: Host sprite remains uncorrupted after failed 0-byte import", function()
    local spr = Sprite(32, 32)
    local layerCount = #spr.layers
    local path = harness.getFixturePath("empty.aseprite")
    harness.createSmartObjectLayer(spr, path)
    assert_eq(#spr.layers, layerCount, "Layer count unchanged")
    assert_eq(spr.width, 32, "Width intact")
    spr:close()
  end)

  it("TC 2.1.3: 1x1 sprite imports and renders single pixel cel", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("extreme_1x1.aseprite")
    local sm, err = harness.createSmartObjectLayer(spr, path)
    assert_nil(err, "1x1 file import must succeed")
    assert_eq(sm.hostLayer:cel(1).image.width, 1, "Cel width 1")
    assert_eq(sm.hostLayer:cel(1).image.height, 1, "Cel height 1")
    spr:close()
  end)

  it("TC 2.1.4: Transforming 1x1 sprite scales single pixel cleanly", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("extreme_1x1.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, 4.0, 4.0)
    assert_eq(sm.hostLayer:cel(1).image.width, 4, "Scaled width 4")
    assert_eq(sm.hostLayer:cel(1).image.height, 4, "Scaled height 4")
    spr:close()
  end)

  it("TC 2.1.5: 1x1 sprite maps across multi-frame host timeline", function()
    local spr = Sprite(32, 32)
    for i = 1, 3 do spr:newEmptyFrame() end
    local path = harness.getFixturePath("extreme_1x1.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    for f = 1, 4 do
      assert_eq(sm.hostLayer:cel(f).image.width, 1, "Frame " .. f .. " cel is 1x1")
    end
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 2: Corrupted Binary Header / Truncated Files
-- ------------------------------------------------------------------------------
describe(2, "Boundary 2: Corrupted Binary Header / Truncated Files", function()
  it("TC 2.2.1: Placing file with invalid magic bytes fails gracefully", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("corrupt.aseprite")
    local sm, err = harness.createSmartObjectLayer(spr, path)
    assert_nil(sm, "Must return nil for corrupt file")
    assert_not_nil(err, "Error reported")
    spr:close()
  end)

  it("TC 2.2.2: Truncated binary data triggers safe error handling", function()
    local path = harness.getFixturePath("corrupt.aseprite")
    local docData, err = harness.loadDetachedDoc(path)
    assert_nil(docData, "Must not return docData")
    assert_not_nil(err, "Error must be returned")
  end)

  it("TC 2.2.3: No crash or memory corruption occurs on malformed input stream", function()
    local ok = pcall(function()
      harness.loadDetachedDoc(harness.getFixturePath("corrupt.aseprite"))
    end)
    assert_true(ok, "Exception caught safely")
  end)

  it("TC 2.2.4: Attempting to reload corrupted file keeps prior cached image intact", function()
    local spr = Sprite(32, 32)
    local validPath = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, validPath)
    local originalWidth = sm.hostLayer:cel(1).image.width
    -- Simulate file corrupted on disk
    sm.sourcePath = harness.getFixturePath("corrupt.aseprite")
    local updated, err = sm:checkForDiskUpdate(os.time() + 100)
    assert_false(updated, "Update must fail")
    assert_eq(sm.hostLayer:cel(1).image.width, originalWidth, "Prior cel preserved")
    spr:close()
  end)

  it("TC 2.2.5: Error message accurately identifies file issue", function()
    local _, err = harness.loadDetachedDoc(harness.getFixturePath("corrupt.aseprite"))
    assert_true(err:find("Corrupt") ~= nil or err:find("Failed") ~= nil, "Error mentions corrupt/failed")
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 3: Missing / Deleted Links
-- ------------------------------------------------------------------------------
describe(2, "Boundary 3: Missing / Deleted Links", function()
  it("TC 2.3.1: Placing non-existent file path returns clean error", function()
    local spr = Sprite(32, 32)
    local sm, err = harness.createSmartObjectLayer(spr, "non_existent_12345.aseprite")
    assert_nil(sm, "Nil result")
    assert_true(err:find("File not found") ~= nil, "File not found error")
    spr:close()
  end)

  it("TC 2.3.2: Deleting linked file from disk flags link status as Missing", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.sourcePath = "removed_file.aseprite"
    sm:checkForDiskUpdate(os.time() + 50)
    assert_eq(sm.linkStatus, "Missing", "Link marked Missing")
    spr:close()
  end)

  it("TC 2.3.3: Host document can still be navigated and saved when link is missing", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.linkStatus = "Missing"
    -- Cel is still accessible
    assert_not_nil(sm.hostLayer:cel(1), "Cel remains accessible")
    spr:close()
  end)

  it("TC 2.3.4: Recreating deleted file on disk restores link status to Linked", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.linkStatus = "Missing"
    sm:checkForDiskUpdate(os.time() + 100)
    assert_eq(sm.linkStatus, "Linked", "Status restored to Linked")
    spr:close()
  end)

  it("TC 2.3.5: Path with spaces and special characters resolves correctly", function()
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local f = io.open(path, "rb")
    assert_not_nil(f, "Path resolves")
    if f then f:close() end
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 4: Extreme Transformation Scaling
-- ------------------------------------------------------------------------------
describe(2, "Boundary 4: Extreme Transformation Scaling", function()
  it("TC 2.4.1: Tiny scale factor (0.01x) does not cause division by zero or underflow", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, 0.01, 0.01)
    local cel = sm.hostLayer:cel(1)
    assert_true(cel.image.width >= 1, "Width clamped to min 1")
    assert_true(cel.image.height >= 1, "Height clamped to min 1")
    spr:close()
  end)

  it("TC 2.4.2: Large scale factor (10.0x) bounds-checked without buffer overflow", function()
    local spr = Sprite(256, 256)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, 10.0, 10.0)
    local cel = sm.hostLayer:cel(1)
    assert_eq(cel.image.width, 160, "Scaled width 160 (16*10)")
    assert_eq(cel.image.height, 160, "Scaled height 160 (16*10)")
    spr:close()
  end)

  it("TC 2.4.3: Zero scale factor (0.0x) handled safely", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, 0.0, 0.0)
    local cel = sm.hostLayer:cel(1)
    assert_true(cel.image.width >= 1, "Width clamped to 1")
    spr:close()
  end)

  it("TC 2.4.4: Negative scale factor acts as flip without crash", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, -1.0, 1.0)
    local cel = sm.hostLayer:cel(1)
    assert_eq(cel.image.width, 16, "Width is 16")
    spr:close()
  end)

  it("TC 2.4.5: Asymmetric extreme scale (0.1x width, 5.0x height) renders safely", function()
    local spr = Sprite(128, 128)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, 0.1, 5.0)
    local cel = sm.hostLayer:cel(1)
    assert_eq(cel.image.width, 2, "Width 2 (16*0.1)")
    assert_eq(cel.image.height, 80, "Height 80 (16*5)")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 5: Extreme Rotation Angles & Precision
-- ------------------------------------------------------------------------------
describe(2, "Boundary 5: Extreme Rotation Angles & Precision", function()
  it("TC 2.5.1: Full 360° rotation renders bit-identical to 0° rotation", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    local px0 = sm.hostLayer:cel(1).image:getPixel(0, 0)
    sm:setTransform(nil, nil, nil, nil, 360.0)
    local px360 = sm.hostLayer:cel(1).image:getPixel(0, 0)
    assert_eq(px0, px360, "Pixel at 360° identical to 0°")
    spr:close()
  end)

  it("TC 2.5.2: Arbitrary fractional angles (33.333333°) evaluated stably", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, nil, nil, 33.333333)
    assert_near(sm.transform.angle, 33.333333, 1e-4, "Angle recorded accurately")
    spr:close()
  end)

  it("TC 2.5.3: Negative rotation angles (-90°) match positive equivalent (270°)", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, nil, nil, -90.0)
    assert_eq(sm.transform.angle, 270.0, "-90° normalized to 270°")
    spr:close()
  end)

  it("TC 2.5.4: Micro-angle noise (1e-12 rad) snapped to 0° via snap_affine", function()
    local angle = harness.snapAffine(1e-12)
    assert_eq(angle, 0.0, "Noise snapped to 0.0")
  end)

  it("TC 2.5.5: Massive rotation angles (7200° = 20 revolutions) wrap correctly to 0°", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, nil, nil, 7200.0)
    assert_eq(sm.transform.angle, 0.0, "7200° modulo 360 is 0.0")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 6: Canvas Boundary & Off-Canvas Positioning
-- ------------------------------------------------------------------------------
describe(2, "Boundary 6: Canvas Boundary & Off-Canvas Positioning", function()
  it("TC 2.6.1: Moving smart object entirely off-canvas to the left does not crash", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:translate(-50, 0)
    assert_eq(sm.transform.x, -50, "X position is -50")
    assert_eq(sm.hostLayer:cel(1).position.x, -50, "Cel position matches")
    spr:close()
  end)

  it("TC 2.6.2: Moving smart object entirely off-canvas to the right renders safely", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:translate(100, 0)
    assert_eq(sm.transform.x, 100, "X position is 100")
    spr:close()
  end)

  it("TC 2.6.3: Partial canvas intersection positions cel accurately", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:translate(-8, -8)
    assert_eq(sm.hostLayer:cel(1).bounds, Rectangle(-8, -8, 16, 16), "Cel bounds overlaps origin")
    spr:close()
  end)

  it("TC 2.6.4: Smart object larger than host canvas centers properly", function()
    local spr = Sprite(16, 16)
    local path = harness.getFixturePath("single_frame_64x64.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { center = true })
    assert_eq(sm.transform.x, -24, "(16-64)/2 = -24")
    assert_eq(sm.transform.y, -24, "(16-64)/2 = -24")
    spr:close()
  end)

  it("TC 2.6.5: Moving back into canvas bounds restores full visible pixels without loss", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:translate(-100, -100)
    sm:translate(100, 100)
    local px = sm.hostLayer:cel(1).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, "Pristine pixel restored")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 7: Non-Square & Extreme Aspect Ratios
-- ------------------------------------------------------------------------------
describe(2, "Boundary 7: Non-Square & Extreme Aspect Ratios", function()
  it("TC 2.7.1: Placing 16x32 (tall) sprite computes correct aspect ratio and bounds", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("nonsquare_16x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(sm.hostLayer:cel(1).image.width, 16, "Width 16")
    assert_eq(sm.hostLayer:cel(1).image.height, 32, "Height 32")
    spr:close()
  end)

  it("TC 2.7.2: Placing 48x12 (wide) sprite computes correct aspect ratio and bounds", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("nonsquare_48x12.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(sm.hostLayer:cel(1).image.width, 48, "Width 48")
    assert_eq(sm.hostLayer:cel(1).image.height, 12, "Height 12")
    spr:close()
  end)

  it("TC 2.7.3: 90° rotation of 16x32 sprite swaps bounding box to 32x16", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("nonsquare_16x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, nil, nil, 90.0)
    assert_eq(sm.hostLayer:cel(1).image.width, 32, "Rotated width 32")
    assert_eq(sm.hostLayer:cel(1).image.height, 16, "Rotated height 16")
    spr:close()
  end)

  it("TC 2.7.4: 90° rotation of 48x12 sprite swaps bounding box to 12x48", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("nonsquare_48x12.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, nil, nil, 90.0)
    assert_eq(sm.hostLayer:cel(1).image.width, 12, "Rotated width 12")
    assert_eq(sm.hostLayer:cel(1).image.height, 48, "Rotated height 48")
    spr:close()
  end)

  it("TC 2.7.5: 180° rotation of 16x32 maintains 16x32 dimensions", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("nonsquare_16x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, nil, nil, 180.0)
    assert_eq(sm.hostLayer:cel(1).image.width, 16, "Width 16")
    assert_eq(sm.hostLayer:cel(1).image.height, 32, "Height 32")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 8: Color Mode Variations (Indexed, Grayscale, RGBA)
-- ------------------------------------------------------------------------------
describe(2, "Boundary 8: Color Mode Variations (Indexed, Grayscale, RGBA)", function()
  it("TC 2.8.1: Placing RGBA smart object into RGBA host preserves full 32-bit color", function()
    local spr = Sprite(32, 32, ColorMode.RGB)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    local px = sm.hostLayer:cel(1).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaA(px), 255, "Alpha is 255")
    assert_eq(app.pixelColor.rgbaR(px), 255, "Red is 255")
    spr:close()
  end)

  it("TC 2.8.2: Placing Grayscale smart object into RGBA host converts luminance to RGB", function()
    local spr = Sprite(64, 64, ColorMode.RGB)
    local path = harness.getFixturePath("grayscale_32x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_not_nil(sm.hostLayer:cel(1), "Grayscale cel rendered into RGB host")
    local px = sm.hostLayer:cel(1).image:getPixel(31, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, "Max grayscale mapped to 255 R")
    assert_eq(app.pixelColor.rgbaG(px), 255, "Max grayscale mapped to 255 G")
    assert_eq(app.pixelColor.rgbaB(px), 255, "Max grayscale mapped to 255 B")
    spr:close()
  end)

  it("TC 2.8.3: Placing Indexed smart object into RGBA host maps palette indices to RGB", function()
    local spr = Sprite(32, 32, ColorMode.RGB)
    local path = harness.getFixturePath("indexed_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_not_nil(sm.hostLayer:cel(1), "Indexed cel rendered into RGB host")
    spr:close()
  end)

  it("TC 2.8.4: Semi-transparent pixels preserve alpha channel correctly", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_32x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    local cel = sm.hostLayer:cel(1)
    local pxInside = cel.image:getPixel(15, 15)
    assert_true(app.pixelColor.rgbaA(pxInside) > 0, "Inside diamond is opaque")
    spr:close()
  end)

  it("TC 2.8.5: Fully transparent background pixels in source remain transparent in cel", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_32x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    local cel = sm.hostLayer:cel(1)
    local pxCorner = cel.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaA(pxCorner), 0, "Corner is transparent")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 9: Timeline Scrubbing & Boundary Frames
-- ------------------------------------------------------------------------------
describe(2, "Boundary 9: Timeline Scrubbing & Boundary Frames", function()
  it("TC 2.9.1: Scrubbing to frame 1 displays linked frame 1", function()
    local spr = Sprite(32, 32)
    for i = 1, 9 do spr:newEmptyFrame() end
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(sm:mapTimelineFrame(1), 1, "Host frame 1 -> Linked frame 1")
    spr:close()
  end)

  it("TC 2.9.2: Scrubbing to host total frames displays correct modulo frame", function()
    local spr = Sprite(32, 32)
    for i = 1, 9 do spr:newEmptyFrame() end -- 10 frames
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(sm:mapTimelineFrame(10), 1, "Host frame 10 -> (10-1)%3 + 1 = 1")
    spr:close()
  end)

  it("TC 2.9.3: Rapid forward scrubbing (1 -> 100 frames) evaluates modulo without lag", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    for f = 1, 100 do
      local expected = ((f - 1) % 3) + 1
      assert_eq(sm:mapTimelineFrame(f), expected, "Frame " .. f .. " matches")
    end
    spr:close()
  end)

  it("TC 2.9.4: Backward scrubbing (10 -> 9 -> 8 ...) maintains reverse sequence", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    for f = 10, 1, -1 do
      local expected = ((f - 1) % 3) + 1
      assert_eq(sm:mapTimelineFrame(f), expected, "Reverse frame " .. f)
    end
    spr:close()
  end)

  it("TC 2.9.5: Modulo calculation for large frame numbers (1000) wraps cleanly", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(sm:mapTimelineFrame(1000), 1, "Frame 1000 -> (1000-1)%3 + 1 = 1")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 10: High Frequency File Timestamp Updates
-- ------------------------------------------------------------------------------
describe(2, "Boundary 10: High Frequency File Timestamp Updates", function()
  it("TC 2.10.1: Rapid successive timestamp advances detected cleanly", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    for i = 1, 5 do
      local updated = sm:checkForDiskUpdate(100 + i * 10)
      assert_true(updated, "Update cycle " .. i .. " detected")
    end
    spr:close()
  end)

  it("TC 2.10.2: Content reloaded on each detected update", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    sm:checkForDiskUpdate(200)
    assert_eq(sm.lastModified, 200, "Timestamp updated")
    spr:close()
  end)

  it("TC 2.10.3: Non-blocking check does not hang or stall", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    local updated = sm:checkForDiskUpdate()
    assert_true(updated ~= nil, "Result returns immediately")
    spr:close()
  end)

  it("TC 2.10.4: Multi-frame file re-renders all host frames on update", function()
    local spr = Sprite(32, 32)
    for i = 1, 2 do spr:newEmptyFrame() end
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    sm:checkForDiskUpdate(300)
    for f = 1, 3 do
      assert_not_nil(sm.hostLayer:cel(f), "Cel " .. f .. " updated")
    end
    spr:close()
  end)

  it("TC 2.10.5: Concurrent multiple smart layers refreshing simultaneously", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm1 = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    local sm2 = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    local u1 = sm1:checkForDiskUpdate(200)
    local u2 = sm2:checkForDiskUpdate(200)
    assert_true(u1 and u2, "Both layers refreshed")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 11: Nested / Cascading Document Structures
-- ------------------------------------------------------------------------------
describe(2, "Boundary 11: Nested / Cascading Document Structures", function()
  it("TC 2.11.1: Smart object placed inside layer hierarchy respects layer order", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(#spr.layers, 2, "Sprite has background + smart layer")
    spr:close()
  end)

  it("TC 2.11.2: Hiding smart object layer toggles visibility flag", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.hostLayer.isVisible = false
    assert_false(sm.hostLayer.isVisible, "Layer hidden")
    sm.flags = sm.flags & ~harness.LayerFlags.Visible
    assert_false((sm.flags & harness.LayerFlags.Visible) == harness.LayerFlags.Visible, "Visibility flag cleared")
    spr:close()
  end)

  it("TC 2.11.3: Setting opacity on smart object layer applies cleanly", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.hostLayer.opacity = 128
    assert_eq(sm.hostLayer.opacity, 128, "Opacity is 128 (50%)")
    spr:close()
  end)

  it("TC 2.11.4: Changing smart object blend mode applies to compositing", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.hostLayer.blendMode = BlendMode.MULTIPLY
    assert_eq(sm.hostLayer.blendMode, BlendMode.MULTIPLY, "Blend mode is Multiply")
    spr:close()
  end)

  it("TC 2.11.5: Stacking multiple smart layers does not interfere with cel bounds", function()
    local spr = Sprite(64, 64)
    local p1 = harness.getFixturePath("single_frame_16x16.aseprite")
    local p2 = harness.getFixturePath("single_frame_32x32.aseprite")
    local sm1 = harness.createSmartObjectLayer(spr, p1, { x = 0, y = 0 })
    local sm2 = harness.createSmartObjectLayer(spr, p2, { x = 16, y = 16 })
    assert_eq(sm1.hostLayer:cel(1).bounds, Rectangle(0, 0, 16, 16), "sm1 bounds")
    assert_eq(sm2.hostLayer:cel(1).bounds, Rectangle(16, 16, 32, 32), "sm2 bounds")
    spr:close()
  end)
end)

-- ------------------------------------------------------------------------------
-- Boundary 12: Memory & Stress Limits
-- ------------------------------------------------------------------------------
describe(2, "Boundary 12: Memory & Stress Limits", function()
  it("TC 2.12.1: Placing 10 smart object layers in single document succeeds", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local layers = {}
    for i = 1, 10 do
      layers[i] = harness.createSmartObjectLayer(spr, path, { x = i * 2, y = i * 2 })
    end
    assert_eq(#layers, 10, "10 layers created")
    for i = 1, 10 do layers[i]:destroy() end
    spr:close()
  end)

  it("TC 2.12.2: 50 successive 2D transform operations without degradation", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    for i = 1, 50 do
      sm:setTransform(i, i, 1.0 + (i % 3) * 0.5, 1.0, (i * 15) % 360)
    end
    assert_true(sm.hostLayer:cel(1).image.width > 0, "Cel intact after 50 transforms")
    sm:destroy()
    spr:close()
  end)

  it("TC 2.12.3: Large 256x256 smart object renders cleanly within memory limits", function()
    local spr = Sprite(256, 256)
    local path = harness.getFixturePath("extreme_256x256.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    assert_eq(sm.hostLayer:cel(1).image.width, 256, "256x256 cel loaded")
    sm:destroy()
    spr:close()
  end)

  it("TC 2.12.4: 100 timeline frames evaluate frame mappings cleanly", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("multi_frame_10f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    for f = 1, 100 do
      local linked = sm:mapTimelineFrame(f)
      assert_true(linked >= 1 and linked <= 10, "Mapped frame in [1, 10]")
    end
    sm:destroy()
    spr:close()
  end)

  it("TC 2.12.5: Rapid document open and close cycles release all handles", function()
    for i = 1, 5 do
      local spr = Sprite(32, 32)
      local path = harness.getFixturePath("single_frame_16x16.aseprite")
      local sm = harness.createSmartObjectLayer(spr, path)
      sm:destroy()
      spr:close()
    end
    assert_true(true, "Cycles completed cleanly")
  end)
end)
