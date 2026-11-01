-- ==============================================================================
-- TIER 4: Real-World Scenarios (Complex Application Workflows, >=6 scenarios)
-- Total Target: >=6 tests
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

describe(4, "Real-World Application Workflows", function()
  -- ----------------------------------------------------------------------------
  -- Scenario 1: Multi-layer scene with animated character smart object
  -- ----------------------------------------------------------------------------
  it("TC 4.1: Multi-layer scene with animated character smart object", function()
    -- Host scene: 64x64 canvas, 10 timeline frames
    local scene = Sprite(64, 64, ColorMode.RGB)
    for i = 1, 9 do scene:newEmptyFrame() end -- 10 frames total

    -- Layer 1: Background Grass/Sky
    local bgLayer = scene.layers[1]
    bgLayer.name = "Background"
    for f = 1, 10 do
      local bgImg = Image(64, 64, ColorMode.RGB)
      bgImg:clear(Color{ r = 40, g = 120, b = 60, a = 255 })
      if f == 1 then
        bgLayer:cel(1).image = bgImg
      else
        scene:newCel(bgLayer, f, bgImg)
      end
    end

    -- Layer 2: Hero Character Smart Object (3-frame walk animation)
    local charPath = harness.getFixturePath("multi_frame_3f.aseprite")
    local heroObj, err = harness.createSmartObjectLayer(scene, charPath, {
      name = "Hero_WalkCycle",
      x = 24,
      y = 24
    })
    assert_nil(err, "Hero smart object created")

    -- Layer 3: Foreground Overlay / UI
    local fgLayer = scene:newLayer()
    fgLayer.name = "Foreground_UI"
    local uiImg = Image(64, 64, ColorMode.RGB)
    uiImg:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
    for x = 0, 63 do uiImg:drawPixel(x, 0, Color{ r = 255, g = 255, b = 255, a = 255 }) end
    scene:newCel(fgLayer, 1, uiImg)

    -- Verify timeline scrubbing across all 10 frames
    for f = 1, 10 do
      local expectedFrame = ((f - 1) % 3) + 1
      assert_eq(heroObj:mapTimelineFrame(f), expectedFrame, "Host frame " .. f .. " maps to walk frame " .. expectedFrame)
      local cel = heroObj.hostLayer:cel(f)
      assert_not_nil(cel, "Cel exists at frame " .. f)
      assert_eq(cel.position.x, 24, "Hero position X is 24")
      assert_eq(cel.position.y, 24, "Hero position Y is 24")
    end

    heroObj:destroy()
    scene:close()
  end)

  -- ----------------------------------------------------------------------------
  -- Scenario 2: Prop scaling and rotation without source distortion
  -- ----------------------------------------------------------------------------
  it("TC 4.2: Prop scaling and rotation without source distortion", function()
    local hostSpr = Sprite(64, 64, ColorMode.RGB)
    local propPath = harness.getFixturePath("single_frame_16x16.aseprite")

    -- Place weapon prop as smart object
    local weaponObj = harness.createSmartObjectLayer(hostSpr, propPath, {
      name = "Weapon_Sword",
      x = 10,
      y = 10
    })

    -- Apply weapon transform: 1.5x scale and 45° angle to match hand grip
    weaponObj:setTransform(10, 10, 1.5, 1.5, 45.0)

    -- Verify rendered cel has crisp nearest-neighbor dimensions
    local cel = weaponObj.hostLayer:cel(1)
    assert_true(cel.image.width > 16, "Weapon cel scaled larger than 16")
    assert_true(cel.image.height > 16, "Weapon cel scaled larger than 16")

    -- Verify source file remains bit-identical
    local detachedCheck = harness.loadDetachedDoc(propPath)
    assert_eq(detachedCheck.width, 16, "Source file width untouched")
    assert_eq(detachedCheck.height, 16, "Source file height untouched")
    harness.releaseDetachedDoc(detachedCheck)

    -- Direct edits locked out
    assert_false(weaponObj:canEditPixels(), "Cannot paint directly on weapon smart object")

    weaponObj:destroy()
    hostSpr:close()
  end)

  -- ----------------------------------------------------------------------------
  -- Scenario 3: Background prop modification with live auto-refresh
  -- ----------------------------------------------------------------------------
  it("TC 4.3: Background prop modification with live auto-refresh", function()
    local roomScene = Sprite(64, 64, ColorMode.RGB)
    local lampPath = harness.getFixturePath("single_frame_16x16.aseprite")

    -- Place street lamp prop
    local lampObj = harness.createSmartObjectLayer(roomScene, lampPath, {
      name = "Street_Lamp",
      x = 5,
      y = 5,
      initialTimestamp = 1000
    })

    local originalCelWidth = lampObj.hostLayer:cel(1).image.width
    assert_eq(originalCelWidth, 16, "Initial lamp width 16")

    -- Simulate external artist modifying and saving the lamp on disk
    local updated, msg = lampObj:checkForDiskUpdate(2000)
    assert_true(updated, "Lamp update detected from disk")
    assert_eq(lampObj.lastModified, 2000, "Timestamp updated")
    assert_eq(lampObj.linkStatus, "Linked", "Status is Linked")
    assert_eq(lampObj.hostLayer:cel(1).image.width, 16, "Cel re-rendered seamlessly")

    lampObj:destroy()
    roomScene:close()
  end)

  -- ----------------------------------------------------------------------------
  -- Scenario 4: Repeated 2D transformations maintaining crisp pixels
  -- ----------------------------------------------------------------------------
  it("TC 4.4: Repeated 2D transformations maintaining crisp pixels", function()
    local spr = Sprite(64, 64, ColorMode.RGB)
    local path = harness.getFixturePath("single_frame_16x16.aseprite")
    local obj = harness.createSmartObjectLayer(spr, path)

    -- Step 1: Rotate 45°
    obj:setTransform(nil, nil, nil, nil, 45.0)
    -- Step 2: Scale 1.5x
    obj:setTransform(nil, nil, 1.5, 1.5)
    -- Step 3: Flip horizontally
    obj:setTransform(nil, nil, nil, nil, nil, true, false)
    -- Step 4: Translate (10, -5)
    obj:translate(10, -5)
    -- Step 5: Rotate another 45° (net 90°)
    obj:setTransform(nil, nil, nil, nil, 90.0)
    -- Step 6: Reset scale back to 1.0x
    obj:setTransform(nil, nil, 1.0, 1.0)
    -- Step 7: Reset rotation to 0° and flip to false
    obj:resetTransform()

    -- Verify pixel edges are razor sharp: TL pixel is exact original Red
    local cel = obj.hostLayer:cel(1)
    assert_eq(cel.image.width, 16, "Width returned to 16")
    assert_eq(cel.image.height, 16, "Height returned to 16")
    local pxTL = cel.image:getPixel(0, 0)
    assert_eq(app.pixelColor.rgbaR(pxTL), 255, "Pristine Red preserved without blur")
    assert_eq(app.pixelColor.rgbaG(pxTL), 0, "No color bleed")

    obj:destroy()
    spr:close()
  end)

  -- ----------------------------------------------------------------------------
  -- Scenario 5: Animation speed / frame count mismatch (Host 10 vs Linked 3)
  -- ----------------------------------------------------------------------------
  it("TC 4.5: Animation speed / frame count mismatch (Host 10 vs Linked 3)", function()
    local host = Sprite(48, 48, ColorMode.RGB)
    for i = 1, 9 do host:newEmptyFrame() end -- 10 frames total
    local smokePath = harness.getFixturePath("multi_frame_3f.aseprite")

    -- Place 3-frame smoke puff animation
    local smokeObj = harness.createSmartObjectLayer(host, smokePath, {
      name = "Smoke_Puff",
      timelineMapping = "Loop"
    })

    -- Sequence expected: 1, 2, 3, 1, 2, 3, 1, 2, 3, 1
    local expectedSeq = { 1, 2, 3, 1, 2, 3, 1, 2, 3, 1 }
    for f = 1, 10 do
      assert_eq(smokeObj:mapTimelineFrame(f), expectedSeq[f], "Frame " .. f .. " sequence match")
    end

    -- Switch mapping mode to Hold and verify: 1, 2, 3, 3, 3, 3, 3, 3, 3, 3
    smokeObj.timelineMapping = "Hold"
    for f = 1, 10 do
      local expectedHold = math.min(f, 3)
      assert_eq(smokeObj:mapTimelineFrame(f), expectedHold, "Frame " .. f .. " hold match")
    end

    smokeObj:destroy()
    host:close()
  end)

  -- ----------------------------------------------------------------------------
  -- Scenario 6: Bulk multi-smart-object scene with mixed transformations and scrubbing
  -- ----------------------------------------------------------------------------
  it("TC 4.6: Bulk multi-smart-object scene with mixed transformations and scrubbing", function()
    -- Complex game scene: 12 timeline frames
    local scene = Sprite(128, 128, ColorMode.RGB)
    for i = 1, 11 do scene:newEmptyFrame() end -- 12 frames

    -- 5 distinct smart objects
    local p1 = harness.getFixturePath("multi_frame_3f.aseprite")
    local p2 = harness.getFixturePath("single_frame_16x16.aseprite")
    local p3 = harness.getFixturePath("single_frame_32x32.aseprite")
    local p4 = harness.getFixturePath("nonsquare_16x32.aseprite")
    local p5 = harness.getFixturePath("multi_frame_10f.aseprite")

    local obj1 = harness.createSmartObjectLayer(scene, p1, { name = "Hero", x = 10, y = 10, scaleX = 1.5, scaleY = 1.5 })
    local obj2 = harness.createSmartObjectLayer(scene, p2, { name = "Shadow", x = 10, y = 40, flipV = true })
    local obj3 = harness.createSmartObjectLayer(scene, p3, { name = "Torch", x = 60, y = 20, angle = 45.0 })
    local obj4 = harness.createSmartObjectLayer(scene, p4, { name = "Banner", x = 90, y = 10, scaleX = 2.0 })
    local obj5 = harness.createSmartObjectLayer(scene, p5, { name = "RunFX", x = 20, y = 80 })

    -- Scrub through all 12 timeline frames and verify stability
    for f = 1, 12 do
      assert_not_nil(obj1.hostLayer:cel(f), "Obj1 cel at frame " .. f)
      assert_not_nil(obj2.hostLayer:cel(f), "Obj2 cel at frame " .. f)
      assert_not_nil(obj3.hostLayer:cel(f), "Obj3 cel at frame " .. f)
      assert_not_nil(obj4.hostLayer:cel(f), "Obj4 cel at frame " .. f)
      assert_not_nil(obj5.hostLayer:cel(f), "Obj5 cel at frame " .. f)
    end

    -- Clean destruction
    obj1:destroy()
    obj2:destroy()
    obj3:destroy()
    obj4:destroy()
    obj5:destroy()
    scene:close()
  end)
end)
