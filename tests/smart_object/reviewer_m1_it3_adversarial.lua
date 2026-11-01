-- Independent Adversarial Test Script for Reviewer Smart M1 It3
-- Verifies LUA-07, CMD-01, CMD-02, CMD-03, Flatten, Convert, Background, and Regressions

local testCount = 0
local passCount = 0
local failCount = 0

local function check(id, name, cond, detail)
  testCount = testCount + 1
  if cond then
    passCount = passCount + 1
    print(string.format("  [PASS] %s: %s", id, name))
  else
    failCount = failCount + 1
    print(string.format("  [FAIL] %s: %s - %s", id, name, detail or "condition failed"))
  end
end

-- Setup helper to create a linked smart object fixture
local fixturePath = "test_reviewer_so_fixture.aseprite"
local function createFixture()
  local srcSpr = Sprite(16, 16, ColorMode.RGB)
  srcSpr.cels[1].image:clear(Color{ r = 100, g = 150, b = 200, a = 255 })
  -- Write a distinctive pixel
  srcSpr.cels[1].image:drawPixel(0, 0, Color{ r = 255, g = 0, b = 0, a = 255 })
  srcSpr:saveAs(fixturePath)
  srcSpr:close()
end

createFixture()

local function createHostWithSmartObject()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{
    filename = fixturePath,
    x = 0,
    y = 0,
    ui = false
  }
  local smartLayer = spr.layers[#spr.layers]
  assert(smartLayer.isSmartObject, "Layer must be smart object")
  app.activeSprite = spr
  app.activeLayer = smartLayer
  return spr, smartLayer
end

print("================================================================")
print("INDEPENDENT ADVERSARIAL STRESS & INTEGRITY SUITE (M1-IT3)")
print("================================================================")

-- -------------------------------------------------------------
-- 1. LUA-07: cel.image:pixels() read vs write & no regression
-- -------------------------------------------------------------
print("\n--- SECTION 1: LUA-07 pixels() Read/Write Verification ---")
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  assert(cel ~= nil, "cel must exist")

  -- Read should succeed
  local readCount = 0
  local readOk, readErr = pcall(function()
    for it in cel.image:pixels() do
      local c = it()
      readCount = readCount + 1
      if readCount >= 5 then break end
    end
  end)
  check("LUA-07-R", "cel.image:pixels() read succeeds on smart object", readOk and readCount == 5, readErr)

  -- Write must throw error with exact message
  local writeOk, writeErr = pcall(function()
    for it in cel.image:pixels() do
      it(app.pixelColor.rgba(0, 255, 0, 255))
      break
    end
  end)
  local expectedMsg = "cannot modify pixels on a smart object layer"
  local matchedMsg = writeErr and string.find(tostring(writeErr), expectedMsg, 1, true) ~= nil
  check("LUA-07-W", "cel.image:pixels() write throws exact lockout message", (not writeOk) and matchedMsg, tostring(writeErr))

  -- Verify pixel (0,0) was not mutated
  local px0 = cel.image:getPixel(0, 0)
  local redVal = app.pixelColor.rgba(255, 0, 0, 255)
  check("LUA-07-P", "cel.image pixels remain pristine after write attempt", px0 == redVal, "pixel mutated: " .. tostring(px0))

  spr:close()
end

-- 1b. LUA-07 Regression: Standalone Image and Normal Layer should still allow write via pixels()
do
  -- Standalone image
  local standaloneImg = Image(8, 8, ColorMode.RGB)
  standaloneImg:clear(Color{ r = 0, g = 0, b = 0, a = 255 })
  local standOk, standErr = pcall(function()
    for it in standaloneImg:pixels() do
      it(app.pixelColor.rgba(123, 234, 56, 255))
      break
    end
  end)
  check("REG-01-A", "Standalone Image allows write via pixels() without error", standOk, standErr)
  check("REG-01-B", "Standalone Image pixel was mutated", standaloneImg:getPixel(0, 0) == app.pixelColor.rgba(123, 234, 56, 255), "pixel mismatch")

  -- Normal layer cel image
  local normalSpr = Sprite(16, 16, ColorMode.RGB)
  local normalCel = normalSpr.cels[1]
  local normOk, normErr = pcall(function()
    for it in normalCel.image:pixels() do
      it(app.pixelColor.rgba(50, 100, 150, 255))
      break
    end
  end)
  check("REG-01-C", "Normal Layer cel.image allows write via pixels() without error", normOk, normErr)
  check("REG-01-D", "Normal Layer cel.image pixel was mutated", normalCel.image:getPixel(0, 0) == app.pixelColor.rgba(50, 100, 150, 255), "pixel mismatch")
  normalSpr:close()
end

-- -------------------------------------------------------------
-- 2. CMD-01 and CMD-02: Stroke and Fill Commands
-- -------------------------------------------------------------
print("\n--- SECTION 2: CMD-01 & CMD-02 Stroke/Fill Verification ---")
do
  local spr, smartLayer = createHostWithSmartObject()

  -- Stroke test
  spr.selection:select(Rectangle(2, 2, 8, 8))
  local strokeOk, strokeErr = pcall(function() app.command.Stroke() end)
  local cel = smartLayer.cels[1]
  local pxStroke = cel.image:getPixel(2, 2)
  check("CMD-01", "Stroke command on smart object does not mutate pixels", pxStroke == app.pixelColor.rgba(100, 150, 200, 255), "stroke mutated pixel: " .. tostring(pxStroke))

  -- Fill test
  local fillOk, fillErr = pcall(function() app.command.Fill() end)
  local pxFill = cel.image:getPixel(3, 3)
  check("CMD-02", "Fill command on smart object does not mutate pixels", pxFill == app.pixelColor.rgba(100, 150, 200, 255), "fill mutated pixel: " .. tostring(pxFill))

  spr:close()
end

-- 2b. CMD-01/02 Regression: Stroke and Fill on Normal Layer must work
do
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  local normalLayer = spr.layers[1]
  normalLayer.cels[1].image:clear(Color{ r = 0, g = 0, b = 0, a = 255 })
  app.activeLayer = normalLayer
  app.fgColor = Color{ r = 255, g = 255, b = 255, a = 255 }
  spr.selection:select(Rectangle(4, 4, 10, 10))

  app.command.Fill()
  local pxFill = normalLayer.cels[1].image:getPixel(5, 5)
  check("REG-02-A", "Fill command works normally on regular raster layer", pxFill == app.pixelColor.rgba(255, 255, 255, 255), "fill failed on regular layer")

  spr.selection:select(Rectangle(18, 18, 6, 6))
  app.command.Stroke()
  local pxStroke = normalLayer.cels[1].image:getPixel(18, 18)
  check("REG-02-B", "Stroke command works normally on regular raster layer", pxStroke == app.pixelColor.rgba(255, 255, 255, 255), "stroke failed on regular layer")

  spr:close()
end

-- -------------------------------------------------------------
-- 3. CMD-03: MergeDownLayer Command
-- -------------------------------------------------------------
print("\n--- SECTION 3: CMD-03 MergeDownLayer Verification ---")
-- 3a. Smart object layer on top, regular layer below
do
  local spr, smartLayer = createHostWithSmartObject()

  local layerCountBefore = #spr.layers
  local mergeOk, mergeErr = pcall(function() app.command.MergeDownLayer() end)
  check("CMD-03-A", "MergeDownLayer on top smart object layer is blocked (layer count unchanged)", #spr.layers == layerCountBefore, "layer count changed: " .. #spr.layers)
  check("CMD-03-B", "Top layer remains smart object", spr.layers[#spr.layers].isSmartObject == true, "top layer lost smart object status")

  spr:close()
end

-- 3b. Regular layer on top, smart object layer below
do
  local spr, smartLayer = createHostWithSmartObject()

  local topNormal = spr:newLayer("top_raster")
  local normalCel = spr:newCel(topNormal, 1)
  normalCel.image:clear(Color{ r = 10, g = 20, b = 30, a = 255 })
  app.activeLayer = topNormal

  local layerCountBefore = #spr.layers
  local mergeOk, mergeErr = pcall(function() app.command.MergeDownLayer() end)
  check("CMD-03-C", "MergeDownLayer of raster layer into bottom smart object is blocked", #spr.layers == layerCountBefore, "layer count changed: " .. #spr.layers)
  check("CMD-03-D", "Bottom smart object layer remains intact and smart object", spr.layers[2].isSmartObject == true, "smart object layer altered")

  spr:close()
end

-- 3c. Regular layers merge down regression check
do
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  local layer1 = spr.layers[1]
  local layer2 = spr:newLayer("layer2")
  local c2 = spr:newCel(layer2, 1)
  c2.image:clear(Color{ r = 1, g = 2, b = 3, a = 255 })
  app.activeLayer = layer2
  local mergeOk, mergeErr = pcall(function() app.command.MergeDownLayer() end)
  check("REG-03", "MergeDownLayer between two regular raster layers still works", #spr.layers == 1, "failed merging regular layers")
  spr:close()
end

-- -------------------------------------------------------------
-- 4. Flatten, Convert, Background Guards & Regressions
-- -------------------------------------------------------------
print("\n--- SECTION 4: Flatten, Convert, Background Guards ---")
-- 4a. Sprite:flatten() on sprite with smart object
do
  local spr, smartLayer = createHostWithSmartObject()
  local flatOk, flatErr = pcall(function() spr:flatten() end)
  local expectedMsg = "cannot flatten a sprite containing smart object layers"
  local matched = flatErr and string.find(tostring(flatErr), expectedMsg, 1, true) ~= nil
  check("FLAT-01", "Sprite:flatten() throws error when sprite contains smart object", (not flatOk) and matched, tostring(flatErr))
  check("FLAT-02", "Sprite layers preserved after rejected flatten", #spr.layers >= 2, "layers corrupted")
  spr:close()
end

-- 4b. app.command.FlattenLayers() on sprite with smart object
do
  local spr, smartLayer = createHostWithSmartObject()
  local layerCountBefore = #spr.layers
  local flatCmdOk, flatCmdErr = pcall(function() app.command.FlattenLayers() end)
  check("FLAT-03", "app.command.FlattenLayers() is no-op/blocked on smart object", #spr.layers == layerCountBefore, "layers flattened")
  spr:close()
end

-- 4c. Flatten on regular sprite without smart object (regression check)
do
  local spr = Sprite(32, 32, ColorMode.RGB)
  local l2 = spr:newLayer("l2")
  check("REG-04-PRE", "Sprite has 2 regular layers before flatten", #spr.layers == 2, "initial layers not 2")
  local flatOk, flatErr = pcall(function() spr:flatten() end)
  check("REG-04", "Sprite:flatten() succeeds on regular sprite", flatOk and #spr.layers == 1, flatErr)
  spr:close()
end

-- 4d. ConvertLayerCommand on smart object
do
  local spr, smartLayer = createHostWithSmartObject()

  local convOk, convErr = pcall(function() app.command.ConvertLayer{ to = "background" } end)
  check("CONV-01", "ConvertLayer to background blocked on smart object", smartLayer.isSmartObject and (not smartLayer.isBackground), "smart object converted to background")

  local convTileOk, convTileErr = pcall(function() app.command.ConvertLayer{ to = "tilemap" } end)
  check("CONV-02", "ConvertLayer to tilemap blocked on smart object", smartLayer.isSmartObject and (not smartLayer.isTilemap), "smart object converted to tilemap")
  spr:close()
end

-- 4e. BackgroundFromLayer on smart object
do
  local spr, smartLayer = createHostWithSmartObject()

  local bgOk, bgErr = pcall(function() app.command.BackgroundFromLayer() end)
  check("BG-01", "BackgroundFromLayer blocked on smart object", smartLayer.isSmartObject and (not smartLayer.isBackground), "smart object converted to background")
  spr:close()
end

-- -------------------------------------------------------------
-- 5. Additional Adversarial Probes & Edge Cases
-- -------------------------------------------------------------
print("\n--- SECTION 5: Additional Adversarial Probes ---")
-- 5a. Smart object inside a Group / Folder
do
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  local grp = spr:newGroup()
  grp.name = "MyGroup"
  app.activeLayer = grp
  app.command.PlaceSmartObject{
    filename = fixturePath,
    x = 0,
    y = 0,
    ui = false
  }

  local layerCountBefore = #spr.layers
  local okFlat, errFlat = pcall(function() app.command.FlattenLayers() end)
  check("ADV-GRP-01", "FlattenLayers blocked when smart object is inside a Group", #spr.layers == layerCountBefore, "group with smart object was flattened")

  local okSprFlat, errSprFlat = pcall(function() spr:flatten() end)
  check("ADV-GRP-02", "Sprite:flatten() throws when smart object is inside a Group", not okSprFlat, "flatten succeeded inside group")
  spr:close()
end

-- 5b. Non-destructive translation and reading verify
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local origPos = Point(cel.position.x, cel.position.y)
  cel.position = Point(origPos.x + 10, origPos.y + 15)
  check("ADV-MOV-01", "Cel position translated without mutating pixels", cel.position.x == origPos.x + 10 and cel.position.y == origPos.y + 15, "position failed")
  local px = cel.image:getPixel(0, 0)
  check("ADV-MOV-02", "Source pixel red still intact after move", px == app.pixelColor.rgba(255, 0, 0, 255), "pixel corrupted")
  spr:close()
end

-- Clean up fixture
pcall(function() os.remove(fixturePath) end)

print("\n================================================================")
print(string.format("REVIEWER TEST SUMMARY: %d Total | %d Passed | %d Failed", testCount, passCount, failCount))
print("================================================================")

if failCount > 0 then
  print("\nVERDICT: REQUEST_CHANGES")
  assert(failCount == 0, "Independent adversarial tests failed!")
else
  print("\nVERDICT: APPROVE")
end
