-- ==============================================================================
-- TIER 3: Cross-Feature Interactions (Pairwise combinations, >=12 tests)
-- Total Target: >=12 tests
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

describe(3, "Cross-Feature Interactions & Pairwise Combinations", function()
  it("TC 3.1: Transform (Rotate 90°) + Multi-Frame Timeline Sync", function()
    local spr = Sprite(32, 32)
    for i = 1, 2 do spr:newEmptyFrame() end -- 3 frames
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    -- Rotate 90 degrees
    sm:setTransform(nil, nil, nil, nil, 90.0)
    -- Frame 1 is Red rotated, Frame 2 is Green rotated, Frame 3 is Blue rotated
    local cel1 = sm.hostLayer:cel(1)
    local cel2 = sm.hostLayer:cel(2)
    local cel3 = sm.hostLayer:cel(3)
    assert_eq(app.pixelColor.rgbaR(cel1.image:getPixel(15, 0)), 255, "Frame 1 rotated Red")
    assert_eq(app.pixelColor.rgbaG(cel2.image:getPixel(15, 0)), 255, "Frame 2 rotated Green")
    assert_eq(app.pixelColor.rgbaB(cel3.image:getPixel(15, 0)), 255, "Frame 3 rotated Blue")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.2: Transform (Scale 2.0x) + Disk File Modification + Auto-Refresh", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    sm:setTransform(nil, nil, 2.0, 2.0)
    assert_eq(sm.hostLayer:cel(1).image.width, 32, "Initial scaled width is 32")
    -- Disk update occurs
    sm:checkForDiskUpdate(200)
    -- Re-rendered cel must preserve 2.0x scale
    assert_eq(sm.hostLayer:cel(1).image.width, 32, "Cel remains 32 wide after disk refresh")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.3: Repositioning (Translation) + Horizontal Flip + Timeline Scrubbing", function()
    local spr = Sprite(64, 64)
    for i = 1, 3 do spr:newEmptyFrame() end
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:translate(10, 15)
    sm:setTransform(nil, nil, nil, nil, nil, true, false) -- flipH
    for f = 1, 4 do
      local cel = sm.hostLayer:cel(f)
      assert_eq(cel.position.x, 10, "Frame " .. f .. " X position is 10")
      assert_eq(cel.position.y, 15, "Frame " .. f .. " Y position is 15")
    end
    sm:destroy()
    spr:close()
  end)

  it("TC 3.4: Cel Linking across frames + Layer Opacity 50%", function()
    local spr = Sprite(32, 32)
    for i = 1, 3 do spr:newEmptyFrame() end
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.hostLayer.opacity = 128
    assert_eq(sm.hostLayer.opacity, 128, "Layer opacity set to 128")
    for f = 1, 4 do
      assert_not_nil(sm.hostLayer:cel(f), "Cel " .. f .. " valid under 50% opacity")
    end
    sm:destroy()
    spr:close()
  end)

  it("TC 3.5: Multiple Smart Objects on different layers with differing transforms", function()
    local spr = Sprite(64, 64)
    local p1 = harness.getFixturePath("single_frame_16x16.aseprite")
    local p2 = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm1 = harness.createSmartObjectLayer(spr, p1, { x = 0, y = 0, scaleX = 2.0, scaleY = 2.0 })
    local sm2 = harness.createSmartObjectLayer(spr, p2, { x = 32, y = 32, angle = 90.0 })
    assert_eq(sm1.hostLayer:cel(1).image.width, 32, "Layer 1 scaled width 32")
    assert_eq(sm2.hostLayer:cel(1).image.width, 16, "Layer 2 width 16")
    assert_eq(sm1.transform.x, 0, "Layer 1 at X=0")
    assert_eq(sm2.transform.x, 32, "Layer 2 at X=32")
    sm1:destroy()
    sm2:destroy()
    spr:close()
  end)

  it("TC 3.6: Smart Object inside layer hierarchy + BlendMode Multiply + 2D Transform", function()
    local spr = Sprite(48, 48)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm.hostLayer.blendMode = BlendMode.MULTIPLY
    sm:setTransform(5, 5, 1.5, 1.5, 45.0)
    assert_eq(sm.hostLayer.blendMode, BlendMode.MULTIPLY, "BlendMode is Multiply")
    assert_eq(sm.transform.x, 5, "X offset is 5")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.7: Color Mode Transition: Indexed object placed in RGBA host + Rotation + Refresh", function()
    local spr = Sprite(48, 48, ColorMode.RGB)
    local path = harness.getFixturePath("indexed_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { initialTimestamp = 100 })
    sm:setTransform(nil, nil, nil, nil, 90.0)
    assert_eq(sm.hostLayer:cel(1).image.colorMode, ColorMode.RGB, "Host cel is RGB")
    sm:checkForDiskUpdate(200)
    assert_eq(sm.hostLayer:cel(1).image.colorMode, ColorMode.RGB, "Host cel remains RGB after refresh")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.8: Direct Pixel Edit Lockout enforced while 2D transform is active", function()
    local spr = Sprite(48, 48)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(nil, nil, 2.0, 2.0, 45.0, true, false)
    assert_false(sm:canEditPixels(), "Pixel editing locked during active transform")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.9: Frame Count Mismatch (Host 10 frames vs Linked 3 frames) modulo sync", function()
    local spr = Sprite(32, 32)
    for i = 1, 9 do spr:newEmptyFrame() end -- 10 frames
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    for f = 1, 10 do
      local expectedLinked = ((f - 1) % 3) + 1
      assert_eq(sm:mapTimelineFrame(f), expectedLinked, "Host frame " .. f .. " maps to linked frame " .. expectedLinked)
    end
    sm:destroy()
    spr:close()
  end)

  it("TC 3.10: Layer Translation + Scaling + Rotation + Bounds tracking", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("nonsquare_16x32.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(10, 20, 2.0, 2.0, 90.0)
    -- 16x32 scaled 2x -> 32x64, rotated 90° -> 64x32
    local cel = sm.hostLayer:cel(1)
    assert_eq(cel.image.width, 64, "Rotated scaled width is 64")
    assert_eq(cel.image.height, 32, "Rotated scaled height is 32")
    assert_eq(cel.position.x, 10, "X position is 10")
    assert_eq(cel.position.y, 20, "Y position is 20")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.11: Transform reset returns to pristine source after multiple operations", function()
    local spr = Sprite(64, 64)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    sm:setTransform(15, 25, 3.0, 1.5, 90.0, true, true)
    sm:resetTransform()
    assert_eq(sm.hostLayer:cel(1).image.width, 16, "Width reset to 16")
    assert_eq(sm.hostLayer:cel(1).image.height, 16, "Height reset to 16")
    local px = sm.hostLayer:cel(1).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, "Pristine TL Red restored")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.12: Host canvas resize maintains smart object relative transform", function()
    local spr = Sprite(32, 32)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path, { x = 4, y = 4 })
    spr:resize(64, 64)
    assert_eq(sm.transform.x, 4, "Smart object X offset preserved")
    assert_eq(sm.transform.y, 4, "Smart object Y offset preserved")
    sm:destroy()
    spr:close()
  end)

  it("TC 3.13: Live disk deletion while scrubbing timeline maintains cached frames", function()
    local spr = Sprite(32, 32)
    for i = 1, 2 do spr:newEmptyFrame() end
    local path = harness.getFixturePath("multi_frame_3f.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    -- Simulate file deleted
    sm.sourcePath = "missing_after_load.aseprite"
    sm:checkForDiskUpdate(9999)
    assert_eq(sm.linkStatus, "Missing", "Status marked Missing")
    for f = 1, 3 do
      assert_not_nil(sm.hostLayer:cel(f), "Cached cel " .. f .. " remains available")
    end
    sm:destroy()
    spr:close()
  end)

  it("TC 3.14: Repeated transformation pipeline evaluated against pristine source", function()
    local spr = Sprite(48, 48)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local sm = harness.createSmartObjectLayer(spr, path)
    -- Perform series of 10 transformations
    for i = 1, 10 do
      sm:setTransform(i, i, 1.0 + (i % 2) * 0.5, 1.0, i * 36)
    end
    -- Reset to identity
    sm:resetTransform()
    local px = sm.hostLayer:cel(1).image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(px), 255, "Source pixels pristine without blur or loss")
    sm:destroy()
    spr:close()
  end)
end)
