-- ==============================================================================
-- CHALLENGER MILESTONE 1 ITERATION 3: FLATTEN, MERGE DOWN & NON-DESTRUCTIVE HARNESS
-- ==============================================================================
-- Rigorous empirical verification of:
--   1. sprite:flatten() defense (throws error, does not destroy smart layer, regression safe)
--   2. app.command.MergeDownLayer() defense (upper/lower/stacked, no-op, no cel corruption, regression safe)
--   3. app.command.FlattenLayers() defense (default, visibleOnly, hidden smart layers preserved)
--   4. Non-destructive operations (translations X/Y, cel read, property queries, image clone)
--   5. Auxiliary layer conversion defense (ConvertLayer, BackgroundFromLayer)
-- ==============================================================================

print("======================================================================")
print("STARTING CHALLENGER M1 IT3: FLATTEN, MERGE & NON-DESTRUCTIVE DEFENSE SUITE")
print("======================================================================")

local tempDir = app.fs.tempPath or "."
local srcFileRed = app.fs.joinPath(tempDir, "challenger_it3_src_red.aseprite")
local srcFileBlue = app.fs.joinPath(tempDir, "challenger_it3_src_blue.aseprite")
local savedHostFile = app.fs.joinPath(tempDir, "challenger_it3_host_saved.aseprite")

-- Clean up any preexisting artifacts
pcall(function() os.remove(srcFileRed) end)
pcall(function() os.remove(srcFileBlue) end)
pcall(function() os.remove(savedHostFile) end)

-- Test counters
local totalAssertions = 0
local passedAssertions = 0
local failedAssertions = 0
local failures = {}

local function assert_eq(actual, expected, msg)
  totalAssertions = totalAssertions + 1
  if actual ~= expected then
    failedAssertions = failedAssertions + 1
    local err = string.format("%s: expected '%s', got '%s'", msg or "assertion failed", tostring(expected), tostring(actual))
    table.insert(failures, err)
    error(err, 2)
  else
    passedAssertions = passedAssertions + 1
  end
end

local function assert_true(cond, msg)
  totalAssertions = totalAssertions + 1
  if not cond then
    failedAssertions = failedAssertions + 1
    local err = msg or "expected true, got false"
    table.insert(failures, err)
    error(err, 2)
  else
    passedAssertions = passedAssertions + 1
  end
end

local function assert_false(cond, msg)
  totalAssertions = totalAssertions + 1
  if cond then
    failedAssertions = failedAssertions + 1
    local err = msg or "expected false, got true"
    table.insert(failures, err)
    error(err, 2)
  else
    passedAssertions = passedAssertions + 1
  end
end

local function run_test(name, fn)
  print(string.format("\n[TEST CASE] %s", name))
  local ok, err = pcall(fn)
  if ok then
    print(string.format("  --> PASS: %s", name))
  else
    print(string.format("  --> FAIL: %s", name))
    print(string.format("      Error: %s", tostring(err)))
  end
end

-- ------------------------------------------------------------------------------
-- Fixture Setup: Deterministic 16x16 red and blue sources
-- ------------------------------------------------------------------------------
local function createFixtures()
  -- Red fixture (r=220, g=x*10, b=y*10, a=255)
  local sprRed = Sprite(16, 16, ColorMode.RGB)
  local imgRed = sprRed.cels[1].image
  for y = 0, 15 do
    for x = 0, 15 do
      imgRed:drawPixel(x, y, Color{ r = 220, g = x * 10, b = y * 10, a = 255 })
    end
  end
  sprRed:saveAs(srcFileRed)
  sprRed:close()

  -- Blue fixture (r=x*10, g=y*10, b=220, a=255)
  local sprBlue = Sprite(16, 16, ColorMode.RGB)
  local imgBlue = sprBlue.cels[1].image
  for y = 0, 15 do
    for x = 0, 15 do
      imgBlue:drawPixel(x, y, Color{ r = x * 10, g = y * 10, b = 220, a = 255 })
    end
  end
  sprBlue:saveAs(srcFileBlue)
  sprBlue:close()

  assert_true(app.fs.isFile(srcFileRed), "Red source fixture must exist")
  assert_true(app.fs.isFile(srcFileBlue), "Blue source fixture must exist")
end

createFixtures()

-- Helper: Verify cel matches red pattern
local function verifyRedPristine(cel, msgPrefix)
  prefix = msgPrefix or "Cel"
  assert_true(cel ~= nil, prefix .. " cel must not be nil")
  assert_true(cel.image ~= nil, prefix .. " cel image must not be nil")
  local img = cel.image
  assert_eq(img.width, 16, prefix .. " width must be 16")
  assert_eq(img.height, 16, prefix .. " height must be 16")
  for y = 0, 15 do
    for x = 0, 15 do
      local px = img:getPixel(x, y)
      local r = app.pixelColor.rgbaR(px)
      local g = app.pixelColor.rgbaG(px)
      local b = app.pixelColor.rgbaB(px)
      local a = app.pixelColor.rgbaA(px)
      if r ~= 220 or g ~= (x * 10) or b ~= (y * 10) or a ~= 255 then
        error(string.format("%s pixel mismatch at (%d,%d): expected (220,%d,%d,255) got (%d,%d,%d,%d)",
          prefix, x, y, x*10, y*10, r, g, b, a), 2)
      end
    end
  end
  totalAssertions = totalAssertions + 256
  passedAssertions = passedAssertions + 256
end

-- Helper: Verify cel matches blue pattern
local function verifyBluePristine(cel, msgPrefix)
  prefix = msgPrefix or "Cel"
  assert_true(cel ~= nil, prefix .. " cel must not be nil")
  assert_true(cel.image ~= nil, prefix .. " cel image must not be nil")
  local img = cel.image
  assert_eq(img.width, 16, prefix .. " width must be 16")
  assert_eq(img.height, 16, prefix .. " height must be 16")
  for y = 0, 15 do
    for x = 0, 15 do
      local px = img:getPixel(x, y)
      local r = app.pixelColor.rgbaR(px)
      local g = app.pixelColor.rgbaG(px)
      local b = app.pixelColor.rgbaB(px)
      local a = app.pixelColor.rgbaA(px)
      if r ~= (x * 10) or g ~= (y * 10) or b ~= 220 or a ~= 255 then
        error(string.format("%s pixel mismatch at (%d,%d): expected (%d,%d,220,255) got (%d,%d,%d,%d)",
          prefix, x, y, x*10, y*10, r, g, b, a), 2)
      end
    end
  end
  totalAssertions = totalAssertions + 256
  passedAssertions = passedAssertions + 256
end

-- ==============================================================================
-- SECTION 1: sprite:flatten() DEFENSES
-- ==============================================================================

run_test("1.1: sprite:flatten() on sprite with Background + Smart Object throws error and preserves layer", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  assert_eq(#spr.layers, 2, "Sprite should have 2 layers before flatten")
  local smartLayer = spr.layers[2]
  assert_true(smartLayer.isSmartObject, "Layer 2 must be smart object")

  local ok, err = pcall(function() spr:flatten() end)
  assert_false(ok, "spr:flatten() MUST throw error when smart object exists")
  assert_true(string.find(tostring(err), "cannot flatten a sprite containing smart object layers") ~= nil,
    "Error message must specify smart object prohibition: " .. tostring(err))

  -- Verify document state is unmodified
  assert_eq(#spr.layers, 2, "Sprite must still have 2 layers after failed flatten")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 must still be a smart object")
  verifyRedPristine(spr.layers[2].cels[1], "Preserved smart layer")
  spr:close()
end)

run_test("1.2: sprite:flatten() on multi-layer sprite (Background + Smart + Raster) throws error", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  local normalLayer = spr:newLayer()
  normalLayer.name = "TopRaster"
  local topCel = spr:newCel(normalLayer, 1)
  topCel.image:drawPixel(0, 0, Color{ r = 0, g = 255, b = 0, a = 255 })

  assert_eq(#spr.layers, 3, "Sprite should have 3 layers")

  local ok, err = pcall(function() spr:flatten() end)
  assert_false(ok, "spr:flatten() MUST fail with multi-layer smart object stack")
  assert_eq(#spr.layers, 3, "All 3 layers must remain intact")
  assert_true(spr.layers[2].isSmartObject, "Middle layer must remain smart object")
  assert_false(spr.layers[3].isSmartObject, "Top layer must remain regular raster layer")
  verifyRedPristine(spr.layers[2].cels[1], "Middle smart layer")
  spr:close()
end)

run_test("1.3: sprite:flatten() on sprite with multiple smart objects interleaved throws error", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  local middleRaster = spr:newLayer()
  middleRaster.name = "MiddleRaster"
  app.command.PlaceSmartObject{ filename = srcFileBlue, x = 8, y = 8, ui = false }

  assert_eq(#spr.layers, 4, "Sprite should have 4 layers")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 is Red Smart Object")
  assert_false(spr.layers[3].isSmartObject, "Layer 3 is Middle Raster")
  assert_true(spr.layers[4].isSmartObject, "Layer 4 is Blue Smart Object")

  local ok, err = pcall(function() spr:flatten() end)
  assert_false(ok, "spr:flatten() must fail with multiple smart objects")
  assert_eq(#spr.layers, 4, "All 4 layers must remain intact")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 remains smart")
  assert_true(spr.layers[4].isSmartObject, "Layer 4 remains smart")
  verifyRedPristine(spr.layers[2].cels[1], "Red smart layer")
  verifyBluePristine(spr.layers[4].cels[1], "Blue smart layer")
  spr:close()
end)

run_test("1.4: sprite:flatten() regression check: normal sprite without smart objects flattens cleanly", function()
  local spr = Sprite(16, 16, ColorMode.RGB)
  local layer2 = spr:newLayer()
  local cel2 = spr:newCel(layer2, 1)
  cel2.image:drawPixel(0, 0, Color{ r = 10, g = 20, b = 30, a = 255 })
  assert_eq(#spr.layers, 2, "Should start with 2 regular layers")

  local ok, err = pcall(function() spr:flatten() end)
  assert_true(ok, "Regular sprite flatten must succeed without error: " .. tostring(err))
  assert_eq(#spr.layers, 1, "Regular sprite flatten must collapse layers to 1 background layer")
  spr:close()
end)

-- ==============================================================================
-- SECTION 2: app.command.MergeDownLayer() DEFENSES
-- ==============================================================================

run_test("2.1: MergeDownLayer with Smart Object as UPPER layer and Raster as LOWER layer is blocked", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  local rasterCel = spr.cels[1]
  rasterCel.image:drawPixel(5, 5, Color{ r = 50, g = 100, b = 150, a = 255 })

  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  assert_eq(#spr.layers, 2, "Sprite should have 2 layers")
  local smartLayer = spr.layers[2]
  assert_true(smartLayer.isSmartObject, "Upper layer is smart object")
  app.activeLayer = smartLayer

  -- Execute MergeDownLayer
  local ok, err = pcall(function() app.command.MergeDownLayer() end)
  assert_eq(#spr.layers, 2, "MergeDownLayer MUST NOT merge smart object down into lower layer")
  assert_true(spr.layers[2].isSmartObject, "Upper layer must still be smart object")
  assert_false(spr.layers[1].isSmartObject, "Lower layer must still be raster layer")

  -- Verify lower layer cel intact
  local lowerPx = spr.layers[1].cels[1].image:getPixel(5, 5)
  assert_eq(app.pixelColor.rgbaR(lowerPx), 50, "Lower layer pixel R intact")
  assert_eq(app.pixelColor.rgbaG(lowerPx), 100, "Lower layer pixel G intact")
  assert_eq(app.pixelColor.rgbaB(lowerPx), 150, "Lower layer pixel B intact")

  -- Verify smart object cel intact
  verifyRedPristine(smartLayer.cels[1], "Upper smart layer")
  spr:close()
end)

run_test("2.2: MergeDownLayer with Raster as UPPER layer and Smart Object as LOWER layer is blocked", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  assert_eq(#spr.layers, 2, "Sprite should have Background and Smart Object")

  -- Add a regular raster layer on top of the smart object
  local upperRaster = spr:newLayer()
  upperRaster.name = "UpperRaster"
  local upperCel = spr:newCel(upperRaster, 1)
  upperCel.image:drawPixel(3, 3, Color{ r = 12, g = 34, b = 56, a = 255 })
  assert_eq(#spr.layers, 3, "Sprite has [1]=Background, [2]=SmartObject, [3]=UpperRaster")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 is SmartObject")
  assert_false(spr.layers[3].isSmartObject, "Layer 3 is UpperRaster")

  -- Set active layer to UpperRaster (attempting to merge down into SmartObject)
  app.activeLayer = upperRaster
  local ok, err = pcall(function() app.command.MergeDownLayer() end)

  assert_eq(#spr.layers, 3, "MergeDownLayer MUST NOT merge raster layer down into underlying smart object")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 must remain smart object")
  assert_false(spr.layers[3].isSmartObject, "Layer 3 must remain raster layer")

  -- Check upper cel intact
  local upperPx = upperCel.image:getPixel(3, 3)
  assert_eq(app.pixelColor.rgbaR(upperPx), 12, "Upper raster pixel R intact")
  assert_eq(app.pixelColor.rgbaG(upperPx), 34, "Upper raster pixel G intact")

  -- Check smart object intact
  verifyRedPristine(spr.layers[2].cels[1], "Underlying smart layer")
  spr:close()
end)

run_test("2.3: MergeDownLayer between stacked Smart Objects (Smart into Smart) is blocked", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  app.command.PlaceSmartObject{ filename = srcFileBlue, x = 4, y = 4, ui = false }

  assert_eq(#spr.layers, 3, "Sprite has [1]=Background, [2]=SmartRed, [3]=SmartBlue")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 is SmartRed")
  assert_true(spr.layers[3].isSmartObject, "Layer 3 is SmartBlue")

  app.activeLayer = spr.layers[3]
  local ok, err = pcall(function() app.command.MergeDownLayer() end)

  assert_eq(#spr.layers, 3, "MergeDownLayer MUST NOT merge one smart object into another")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 must still be smart object")
  assert_true(spr.layers[3].isSmartObject, "Layer 3 must still be smart object")
  verifyRedPristine(spr.layers[2].cels[1], "SmartRed layer")
  verifyBluePristine(spr.layers[3].cels[1], "SmartBlue layer")
  spr:close()
end)

run_test("2.4: MergeDownLayer regression check: regular raster layers merge successfully", function()
  local spr = Sprite(16, 16, ColorMode.RGB)
  app.activeSprite = spr
  local lowerCel = spr.cels[1]
  lowerCel.image:drawPixel(0, 0, Color{ r = 100, g = 0, b = 0, a = 255 })

  local upperLayer = spr:newLayer()
  local upperCel = spr:newCel(upperLayer, 1)
  upperCel.image:drawPixel(1, 1, Color{ r = 0, g = 100, b = 0, a = 255 })

  assert_eq(#spr.layers, 2, "Should start with 2 regular layers")
  app.activeLayer = upperLayer
  local ok, err = pcall(function() app.command.MergeDownLayer() end)
  assert_true(ok, "MergeDownLayer on standard raster layers must succeed")
  assert_eq(#spr.layers, 1, "MergeDownLayer on regular layers collapses to 1 layer")
  spr:close()
end)

-- ==============================================================================
-- SECTION 3: app.command.FlattenLayers() DEFENSES
-- ==============================================================================

run_test("3.1: app.command.FlattenLayers() with smart object layer does not flatten or destroy layer", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  assert_eq(#spr.layers, 2, "Sprite should have 2 layers")
  assert_true(spr.layers[2].isSmartObject, "Layer 2 is SmartObject")

  local ok, err = pcall(function() app.command.FlattenLayers() end)
  assert_eq(#spr.layers, 2, "FlattenLayers command MUST NOT flatten sprite containing smart object")
  assert_true(spr.layers[2].isSmartObject, "Smart object layer must remain intact")
  verifyRedPristine(spr.layers[2].cels[1], "Protected smart layer")
  spr:close()
end)

run_test("3.2: app.command.FlattenLayers() on complex multi-layer stack containing smart object is blocked", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  local r1 = spr:newLayer()
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  local r2 = spr:newLayer()

  assert_eq(#spr.layers, 4, "Sprite should have 4 layers: Bg, Raster1, SmartRed, Raster2")
  assert_true(spr.layers[3].isSmartObject, "Layer 3 is SmartRed")

  local ok, err = pcall(function() app.command.FlattenLayers() end)
  assert_eq(#spr.layers, 4, "FlattenLayers MUST NOT flatten when smart object is present in stack")
  assert_true(spr.layers[3].isSmartObject, "Smart layer remains intact")
  verifyRedPristine(spr.layers[3].cels[1], "Preserved smart layer")
  spr:close()
end)

run_test("3.3: app.command.FlattenLayers{ visibleOnly = true } with visible smart object is blocked", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  spr.layers[2].isVisible = true

  local ok, err = pcall(function() app.command.FlattenLayers{ visibleOnly = true } end)
  assert_eq(#spr.layers, 2, "FlattenLayers{visibleOnly=true} MUST NOT flatten when smart object is visible")
  assert_true(spr.layers[2].isSmartObject, "Smart object must remain intact")
  verifyRedPristine(spr.layers[2].cels[1], "Visible smart layer")
  spr:close()
end)

run_test("3.4: app.command.FlattenLayers{ visibleOnly = true } with HIDDEN smart object flattens visible raster layers but preserves smart object", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr

  -- Layer 1: Background (visible)
  local bgCel = spr.cels[1]
  bgCel.image:drawPixel(0, 0, Color{ r = 10, g = 10, b = 10, a = 255 })

  -- Layer 2: Hidden Smart Object
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 2, y = 2, ui = false }
  local smartLayer = spr.layers[2]
  assert_true(smartLayer.isSmartObject, "Layer 2 is smart object")
  smartLayer.isVisible = false -- Hide smart object!

  -- Layer 3: Visible Raster Layer
  local topRaster = spr:newLayer()
  topRaster.name = "TopRaster"
  topRaster.isVisible = true
  local topCel = spr:newCel(topRaster, 1)
  topCel.image:drawPixel(5, 5, Color{ r = 90, g = 90, b = 90, a = 255 })

  assert_eq(#spr.layers, 3, "Initial layer count must be 3")

  local ok, err = pcall(function() app.command.FlattenLayers{ visibleOnly = true } end)
  assert_true(ok, "FlattenLayers on visible layers should succeed")

  -- The visible layers (Background + TopRaster) flatten into 1, leaving 2 layers total:
  -- the flattened background and the hidden smart object layer
  assert_eq(#spr.layers, 2, "Flattened visible layers to 1 + 1 preserved hidden smart layer = 2 layers total")

  -- Find the smart layer among remaining layers
  local foundSmart = nil
  for _, l in ipairs(spr.layers) do
    if l.isSmartObject then
      foundSmart = l
      break
    end
  end

  assert_true(foundSmart ~= nil, "Hidden smart object MUST NOT be destroyed by visibleOnly flatten")
  assert_false(foundSmart.isVisible, "Hidden smart object must remain hidden")
  assert_true(foundSmart.isSmartObject, "Hidden smart object must retain smart object flag")
  verifyRedPristine(foundSmart.cels[1], "Preserved hidden smart layer")
  spr:close()
end)

-- ==============================================================================
-- SECTION 4: NON-DESTRUCTIVE OPERATIONS
-- ==============================================================================

run_test("4.1: Repeated translations X/Y (positive, negative, zero) preserve pristine pixel buffer", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  local smartLayer = spr.layers[2]
  local cel = smartLayer.cels[1]

  local testCoordinates = {
    { x = 0, y = 0 },
    { x = 10, y = 15 },
    { x = -5, y = -8 },
    { x = 20, y = -10 },
    { x = -16, y = 16 },
    { x = 50, y = 50 },  -- Out of bounds canvas
    { x = 4, y = 4 }
  }

  for i, coord in ipairs(testCoordinates) do
    cel.position = Point(coord.x, coord.y)
    assert_eq(cel.position.x, coord.x, string.format("Translation step %d: X coordinate", i))
    assert_eq(cel.position.y, coord.y, string.format("Translation step %d: Y coordinate", i))
    assert_eq(cel.bounds.x, coord.x, string.format("Bounds step %d: X coordinate", i))
    assert_eq(cel.bounds.y, coord.y, string.format("Bounds step %d: Y coordinate", i))
    verifyRedPristine(cel, string.format("Cel after translate step %d (%d,%d)", i, coord.x, coord.y))
  end

  spr:close()
end)

run_test("4.2: Cel reading, getPixel, and metadata query integrity", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 3, y = 5, ui = false }
  local smartLayer = spr.layers[2]
  local cel = smartLayer.cels[1]

  -- Metadata queries
  assert_true(smartLayer.isSmartObject, "smartLayer.isSmartObject == true")
  assert_true(smartLayer.isImage, "smartLayer.isImage == true")
  assert_false(smartLayer.isGroup, "smartLayer.isGroup == false")
  assert_false(smartLayer.isTilemap, "smartLayer.isTilemap == false")
  assert_false(smartLayer.isReference, "smartLayer.isReference == false")

  -- Verify pixel edits are strictly prevented even when layer is unlocked in timeline
  local editOk, _ = pcall(function() cel.image:drawPixel(0, 0, Color{ r = 0, g = 0, b = 0, a = 255 }) end)
  assert_false(editOk, "cel.image:drawPixel must be locked out on smart object layer")

  assert_eq(cel.bounds.x, 3, "cel.bounds.x")
  assert_eq(cel.bounds.y, 5, "cel.bounds.y")
  assert_eq(cel.bounds.width, 16, "cel.bounds.width")
  assert_eq(cel.bounds.height, 16, "cel.bounds.height")
  assert_eq(cel.opacity, 255, "cel.opacity")
  assert_eq(cel.zIndex, 0, "cel.zIndex")

  -- Exhaustive pixel reading on cel image
  for y = 0, 15 do
    for x = 0, 15 do
      local px = cel.image:getPixel(x, y)
      assert_eq(app.pixelColor.rgbaR(px), 220, string.format("getPixel(%d,%d) R", x, y))
      assert_eq(app.pixelColor.rgbaG(px), x * 10, string.format("getPixel(%d,%d) G", x, y))
      assert_eq(app.pixelColor.rgbaB(px), y * 10, string.format("getPixel(%d,%d) B", x, y))
      assert_eq(app.pixelColor.rgbaA(px), 255, string.format("getPixel(%d,%d) A", x, y))
    end
  end

  spr:close()
end)

run_test("4.3: Cel image cloning operates independently and does not taint smart cel", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  local smartLayer = spr.layers[2]
  local cel = smartLayer.cels[1]

  -- Clone the cel image
  local clone = cel.image:clone()
  assert_eq(clone.width, 16, "Clone width")
  assert_eq(clone.height, 16, "Clone height")

  -- Mutate clone
  clone:drawPixel(0, 0, Color{ r = 0, g = 255, b = 0, a = 255 })
  local clonePx = clone:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaG(clonePx), 255, "Clone was mutated to green")

  -- Verify original cel image remains completely unaffected
  local origPx = cel.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(origPx), 220, "Original cel pixel R is still 220")
  assert_eq(app.pixelColor.rgbaG(origPx), 0, "Original cel pixel G is still 0")
  assert_eq(app.pixelColor.rgbaB(origPx), 0, "Original cel pixel B is still 0")
  verifyRedPristine(cel, "Cel after clone mutation")

  spr:close()
end)

run_test("4.4: Serialization round-trip: save translated smart object to disk, reload, verify persistence", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 7, y = 9, ui = false }
  local smartLayer = spr.layers[2]
  smartLayer.name = "MyPersistedSmartLayer"

  spr:saveAs(savedHostFile)
  spr:close()

  assert_true(app.fs.isFile(savedHostFile), "Saved host file must exist on disk")

  -- Reload from disk
  local reloadedSpr = app.open(savedHostFile)
  assert_true(reloadedSpr ~= nil, "Reloaded sprite must open")
  assert_eq(#reloadedSpr.layers, 2, "Reloaded sprite must have 2 layers")

  local reloadedSmart = reloadedSpr.layers[2]
  assert_eq(reloadedSmart.name, "MyPersistedSmartLayer", "Layer name preserved")
  assert_true(reloadedSmart.isSmartObject, "Reloaded layer must retain isSmartObject == true")

  local reloadedCel = reloadedSmart.cels[1]
  assert_eq(reloadedCel.position.x, 7, "Cel position X preserved across save/reload")
  assert_eq(reloadedCel.position.y, 9, "Cel position Y preserved across save/reload")
  verifyRedPristine(reloadedCel, "Reloaded cel pixels")

  -- Verify direct edit lockout persists after reload
  local ok, err = pcall(function()
    reloadedCel.image:drawPixel(0, 0, Color{ r = 0, g = 0, b = 0, a = 255 })
  end)
  assert_false(ok, "Direct drawPixel on reloaded smart object cel must be blocked")

  reloadedSpr:close()
  pcall(function() os.remove(savedHostFile) end)
end)

-- ==============================================================================
-- SECTION 5: AUXILIARY LAYER CONVERSIONS & LOCKOUT DEFENSES
-- ==============================================================================

run_test("5.1: ConvertLayer command is disabled/blocked on smart object layers", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  app.activeLayer = spr.layers[2]

  local ok, err = pcall(function() app.command.ConvertLayer() end)
  assert_true(spr.layers[2].isSmartObject, "Layer must remain smart object after ConvertLayer attempt")
  verifyRedPristine(spr.layers[2].cels[1], "Cel after ConvertLayer attempt")
  spr:close()
end)

run_test("5.2: BackgroundFromLayer command is disabled/blocked on smart object layers", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  app.activeLayer = spr.layers[2]

  local ok, err = pcall(function() app.command.BackgroundFromLayer() end)
  assert_true(spr.layers[2].isSmartObject, "Layer must remain smart object after BackgroundFromLayer attempt")
  assert_false(spr.layers[2].isBackground, "Layer must not be converted to background")
  verifyRedPristine(spr.layers[2].cels[1], "Cel after BackgroundFromLayer attempt")
  spr:close()
end)

run_test("5.3: cel.image:pixels() iterator write attempts are strictly blocked", function()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.activeSprite = spr
  app.command.PlaceSmartObject{ filename = srcFileRed, x = 0, y = 0, ui = false }
  local smartLayer = spr.layers[2]
  local cel = smartLayer.cels[1]

  local ok, err = pcall(function()
    for it in cel.image:pixels() do
      it(app.pixelColor.rgba(0, 0, 0, 255))
      break
    end
  end)

  assert_false(ok, "cel.image:pixels() iterator setter MUST throw error on smart object")
  assert_true(string.find(tostring(err), "cannot modify pixels on a smart object layer") ~= nil,
    "Error message must specify smart object prohibition: " .. tostring(err))

  verifyRedPristine(cel, "Cel after iterator attempt")
  spr:close()
end)

-- ------------------------------------------------------------------------------
-- Clean up temporary test fixtures
-- ------------------------------------------------------------------------------
pcall(function() os.remove(srcFileRed) end)
pcall(function() os.remove(srcFileBlue) end)

-- ==============================================================================
-- FINAL SUMMARY & VERDICT
-- ==============================================================================
print("\n======================================================================")
print("CHALLENGER M1 IT3 DEFENSE TEST SUMMARY")
print("======================================================================")
print(string.format("Total Assertions:  %d", totalAssertions))
print(string.format("Passed Assertions: %d", passedAssertions))
print(string.format("Failed Assertions: %d", failedAssertions))
print("======================================================================")

if failedAssertions > 0 then
  print("\nFAILURE DETAILS:")
  for i, f in ipairs(failures) do
    print(string.format("  [%d] %s", i, f))
  end
  print("\n[VERDICT] REQUEST_CHANGES - One or more edge case defense tests failed!")
  assert(failedAssertions == 0, "Test suite failed with " .. failedAssertions .. " errors.")
else
  print("\n[VERDICT] APPROVE - All layer flattening, merge down, and non-destructive operations passed cleanly!")
end
