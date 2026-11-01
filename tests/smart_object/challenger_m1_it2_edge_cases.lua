-- ==============================================================================
-- Challenger Milestone 1 Iteration 2: Empirical Edge Case & Regression Challenge
-- ==============================================================================
-- Tests:
--   1. Multiple smart object layers in same document under rapid script execution.
--   2. Moving smart object cels after failed draw attempts (verify state is not corrupted).
--   3. Calling app.useTool with eyedropper or non-paint tools (verify non-destructive tools still function).
--   4. Reading cel.image:getPixel(x, y) on smart object layer (verify read access is unimpeded).
--   5. File roundtrip: save document, reopen, verify smart object flags, pixel integrity, and lockout persistence.
-- ==============================================================================

print("======================================================================")
print("STARTING CHALLENGER M1 IT2: EMPIRICAL EDGE CASE & REGRESSION SUITE")
print("======================================================================")

local tempDir = app.fs.tempPath or "."
local srcFileRed = app.fs.joinPath(tempDir, "challenger_m1_it2_src_red.aseprite")
local srcFileGreen = app.fs.joinPath(tempDir, "challenger_m1_it2_src_green.aseprite")
local srcFileBlue = app.fs.joinPath(tempDir, "challenger_m1_it2_src_blue.aseprite")
local roundtripFile = app.fs.joinPath(tempDir, "challenger_m1_it2_roundtrip.aseprite")

-- Test tracking
local totalAssertions = 0
local passedAssertions = 0
local failedAssertions = 0
local testFailures = {}

local function assert_true(cond, msg)
  totalAssertions = totalAssertions + 1
  if cond then
    passedAssertions = passedAssertions + 1
  else
    failedAssertions = failedAssertions + 1
    table.insert(testFailures, msg or "assertion failed: condition is false")
    print("  [ASSERT FAIL] " .. (msg or "condition is false"))
    error(msg or "assertion failed", 2)
  end
end

local function assert_eq(actual, expected, msg)
  totalAssertions = totalAssertions + 1
  if actual == expected then
    passedAssertions = passedAssertions + 1
  else
    failedAssertions = failedAssertions + 1
    local detail = string.format("%s: expected '%s', got '%s'", msg or "assertion failed", tostring(expected), tostring(actual))
    table.insert(testFailures, detail)
    print("  [ASSERT FAIL] " .. detail)
    error(detail, 2)
  end
end

local function it(name, fn)
  print("\n[TEST CASE] " .. name)
  local ok, err = pcall(fn)
  if ok then
    print("  --> PASS: " .. name)
  else
    print("  --> FAIL: " .. name .. " | Error: " .. tostring(err))
  end
end

-- ------------------------------------------------------------------------------
-- FIXTURE GENERATION
-- ------------------------------------------------------------------------------
print("\n--- Generating Source Fixtures ---")

-- Source Red: 16x16 with deterministic red pattern
do
  local spr = Sprite(16, 16, ColorMode.RGB)
  local img = spr.cels[1].image
  for y = 0, 15 do
    for x = 0, 15 do
      -- Red dominates, green varies slightly with x, blue with y
      img:drawPixel(x, y, Color{ r = 255, g = (x * 8) % 64, b = (y * 8) % 64, a = 255 })
    end
  end
  spr:saveAs(srcFileRed)
  spr:close()
  assert(app.fs.isFile(srcFileRed), "Red source fixture must exist")
  print("  Created Red Fixture: " .. srcFileRed)
end

-- Source Green: 20x20 with deterministic green pattern
do
  local spr = Sprite(20, 20, ColorMode.RGB)
  local img = spr.cels[1].image
  for y = 0, 19 do
    for x = 0, 19 do
      img:drawPixel(x, y, Color{ r = (x * 5) % 50, g = 255, b = (y * 5) % 50, a = 255 })
    end
  end
  spr:saveAs(srcFileGreen)
  spr:close()
  assert(app.fs.isFile(srcFileGreen), "Green source fixture must exist")
  print("  Created Green Fixture: " .. srcFileGreen)
end

-- Source Blue: 24x24 with deterministic blue pattern
do
  local spr = Sprite(24, 24, ColorMode.RGB)
  local img = spr.cels[1].image
  for y = 0, 23 do
    for x = 0, 23 do
      img:drawPixel(x, y, Color{ r = (x * 4) % 40, g = (y * 4) % 40, b = 255, a = 255 })
    end
  end
  spr:saveAs(srcFileBlue)
  spr:close()
  assert(app.fs.isFile(srcFileBlue), "Blue source fixture must exist")
  print("  Created Blue Fixture: " .. srcFileBlue)
end

-- Helper: verify red pattern
local function isRedSourcePixel(px, x, y)
  local r = app.pixelColor.rgbaR(px)
  local g = app.pixelColor.rgbaG(px)
  local b = app.pixelColor.rgbaB(px)
  local a = app.pixelColor.rgbaA(px)
  return (r == 255 and g == ((x * 8) % 64) and b == ((y * 8) % 64) and a == 255)
end

-- Helper: verify green pattern
local function isGreenSourcePixel(px, x, y)
  local r = app.pixelColor.rgbaR(px)
  local g = app.pixelColor.rgbaG(px)
  local b = app.pixelColor.rgbaB(px)
  local a = app.pixelColor.rgbaA(px)
  return (r == ((x * 5) % 50) and g == 255 and b == ((y * 5) % 50) and a == 255)
end

-- Helper: verify blue pattern
local function isBlueSourcePixel(px, x, y)
  local r = app.pixelColor.rgbaR(px)
  local g = app.pixelColor.rgbaG(px)
  local b = app.pixelColor.rgbaB(px)
  local a = app.pixelColor.rgbaA(px)
  return (r == ((x * 4) % 40) and g == ((y * 4) % 40) and b == 255 and a == 255)
end

-- ==============================================================================
-- EDGE CASE 1: MULTIPLE SMART OBJECT LAYERS UNDER RAPID SCRIPT EXECUTION
-- ==============================================================================
it("EC1.1: Rapid consecutive placement of 6 smart object layers in single document", function()
  local hostSpr = Sprite(128, 128, ColorMode.RGB)
  app.activeSprite = hostSpr

  local configs = {
    { file = srcFileRed,   name = "SO_Red_1",   x = 5,  y = 5,  w = 16, h = 16, checker = isRedSourcePixel },
    { file = srcFileGreen, name = "SO_Green_1", x = 30, y = 10, w = 20, h = 20, checker = isGreenSourcePixel },
    { file = srcFileBlue,  name = "SO_Blue_1",  x = 60, y = 20, w = 24, h = 24, checker = isBlueSourcePixel },
    { file = srcFileRed,   name = "SO_Red_2",   x = 10, y = 50, w = 16, h = 16, checker = isRedSourcePixel },
    { file = srcFileGreen, name = "SO_Green_2", x = 40, y = 60, w = 20, h = 20, checker = isGreenSourcePixel },
    { file = srcFileBlue,  name = "SO_Blue_2",  x = 70, y = 70, w = 24, h = 24, checker = isBlueSourcePixel },
  }

  -- Rapid placement loop
  for i, cfg in ipairs(configs) do
    app.command.PlaceSmartObject{
      filename = cfg.file,
      name = cfg.name,
      x = cfg.x,
      y = cfg.y,
      ui = false
    }
  end

  -- Total layers: 1 base + 6 smart objects = 7
  assert_eq(#hostSpr.layers, 7, "Host document should contain exactly 7 layers")

  -- Verify each placed layer independently
  for i, cfg in ipairs(configs) do
    local layer = hostSpr.layers[i + 1]
    assert_true(layer ~= nil, "Layer " .. cfg.name .. " must exist")
    assert_true(layer.isSmartObject, "Layer " .. cfg.name .. " must be identified as smart object")
    assert_eq(layer.name, cfg.name, "Layer name should match configured name")

    local props = layer.properties("smart_object")
    assert_true(props ~= nil, "Properties map 'smart_object' must exist on layer " .. cfg.name)
    assert_eq(props.filepath, cfg.file, "Stored filepath must match source")
    assert_eq(props.pos_x, cfg.x, "Stored pos_x must match placement x")
    assert_eq(props.pos_y, cfg.y, "Stored pos_y must match placement y")
    assert_eq(props.src_w, cfg.w, "Stored src_w must match source width")
    assert_eq(props.src_h, cfg.h, "Stored src_h must match source height")

    assert_eq(#layer.cels, 1, "Layer must have exactly 1 cel")
    local cel = layer.cels[1]
    assert_eq(cel.position.x, cfg.x, "Cel X must match placement X")
    assert_eq(cel.position.y, cfg.y, "Cel Y must match placement Y")
    assert_eq(cel.image.width, cfg.w, "Cel image width must match source width")
    assert_eq(cel.image.height, cfg.h, "Cel image height must match source height")

    -- Check pixel integrity: sample corner and center pixels
    assert_true(cfg.checker(cel.image:getPixel(0, 0), 0, 0), "Corner (0,0) pixel mismatch on " .. cfg.name)
    local midX = math.floor(cfg.w / 2)
    local midY = math.floor(cfg.h / 2)
    assert_true(cfg.checker(cel.image:getPixel(midX, midY), midX, midY), "Center pixel mismatch on " .. cfg.name)
  end

  -- Rapid layer switching and repositioning loop
  for i, cfg in ipairs(configs) do
    local layer = hostSpr.layers[i + 1]
    app.activeLayer = layer
    local cel = layer.cels[1]
    local newX = cfg.x + 3
    local newY = cfg.y + 4
    cel.position = Point(newX, newY)
    assert_eq(cel.position.x, newX, "Repositioned cel X must match for layer " .. cfg.name)
    assert_eq(cel.position.y, newY, "Repositioned cel Y must match for layer " .. cfg.name)
  end

  -- Verify all 6 layers remain protected against drawing tools
  for i, cfg in ipairs(configs) do
    local layer = hostSpr.layers[i + 1]
    app.activeLayer = layer
    local cel = layer.cels[1]
    local drawAttempt = pcall(function()
      app.useTool{
        tool = "pencil",
        color = Color{ r = 255, g = 255, b = 255, a = 255 },
        points = { Point(cel.position.x + 2, cel.position.y + 2) }
      }
    end)
    -- Verify pixel was NOT changed
    assert_true(cfg.checker(cel.image:getPixel(2, 2), 2, 2), "Pencil must not alter pixels on layer " .. cfg.name)
  end

  hostSpr:close()
end)

-- ==============================================================================
-- EDGE CASE 2: MOVING SMART OBJECT CELS AFTER FAILED DRAW ATTEMPTS
-- ==============================================================================
it("EC2.1: Cel translation immediately after failed draw attempts and exceptions", function()
  local hostSpr = Sprite(64, 64, ColorMode.RGB)
  app.activeSprite = hostSpr

  app.command.PlaceSmartObject{
    filename = srcFileRed,
    x = 10,
    y = 12,
    ui = false
  }

  local smartLayer = hostSpr.layers[2]
  assert_true(smartLayer.isSmartObject, "Layer must be smart object")
  app.activeLayer = smartLayer
  local cel = smartLayer.cels[1]
  assert_eq(cel.position.x, 10, "Initial cel X must be 10")
  assert_eq(cel.position.y, 12, "Initial cel Y must be 12")

  -- Attempt 1: Failed pencil draw tool
  pcall(function()
    app.useTool{
      tool = "pencil",
      color = Color{ r = 0, g = 255, b = 0 },
      points = { Point(14, 16) }
    }
  end)

  -- Attempt 2: Failed direct cel.image:drawPixel (throws Lua error)
  local threwDrawPixel = false
  pcall(function()
    cel.image:drawPixel(4, 4, Color{ r = 0, g = 0, b = 255 })
  end)

  -- Attempt 3: Failed clear command
  pcall(function()
    app.command.Clear()
  end)

  -- Attempt 4: Failed direct cel.image:clear (throws Lua error)
  pcall(function()
    cel.image:clear(Color{ r = 0, g = 0, b = 0 })
  end)

  -- Verify pixels are STILL completely pristine after all failed attempts
  for y = 0, 15 do
    for x = 0, 15 do
      assert_true(isRedSourcePixel(cel.image:getPixel(x, y), x, y),
        string.format("Pixel at (%d,%d) corrupted by failed edit attempt", x, y))
    end
  end

  -- STEP: Move cel position after failed draw attempts
  cel.position = Point(28, 35)
  assert_eq(cel.position.x, 28, "Cel position X should update cleanly to 28")
  assert_eq(cel.position.y, 35, "Cel position Y should update cleanly to 35")

  -- Verify cel image integrity after move
  assert_eq(#smartLayer.cels, 1, "Cel count must remain exactly 1")
  for y = 0, 15 do
    for x = 0, 15 do
      assert_true(isRedSourcePixel(cel.image:getPixel(x, y), x, y),
        string.format("Pixel at (%d,%d) corrupted after cel repositioning", x, y))
    end
  end

  -- Attempt 5: Another failed draw attempt at the NEW location (canvas 30, 37)
  pcall(function()
    app.useTool{
      tool = "brush",
      color = Color{ r = 255, g = 255, b = 255 },
      points = { Point(30, 37) }
    }
  end)

  -- STEP: Move cel to negative canvas coordinates
  cel.position = Point(-5, -8)
  assert_eq(cel.position.x, -5, "Cel position X should allow negative offset -5")
  assert_eq(cel.position.y, -8, "Cel position Y should allow negative offset -8")
  assert_true(isRedSourcePixel(cel.image:getPixel(0, 0), 0, 0), "Pixels intact at negative position")

  -- STEP: Move cel back to (0, 0)
  cel.position = Point(0, 0)
  assert_eq(cel.position.x, 0, "Cel position X returned to 0")
  assert_eq(cel.position.y, 0, "Cel position Y returned to 0")

  -- Negative test: cel.frame alteration must be blocked
  local okFrame, errFrame = pcall(function()
    cel.frame = 2
  end)
  assert_true(not okFrame, "cel.frame reassignment must throw error on smart object layer")
  assert_eq(cel.frame.frameNumber, 1, "Cel must remain at frame 1")

  -- Negative test: cel deletion via cel.image = nil must be blocked
  local okNil, errNil = pcall(function()
    cel.image = nil
  end)
  assert_true(not okNil, "cel.image = nil must throw error on smart object layer")
  assert_eq(#smartLayer.cels, 1, "Cel must not be deleted")

  hostSpr:close()
end)

-- ==============================================================================
-- EDGE CASE 3: NON-DESTRUCTIVE TOOLS (EYEDROPPER, SELECTIONS) ON SMART OBJECT LAYER
-- ==============================================================================
it("EC3.1: Calling app.useTool with eyedropper samples color without modifying pixels", function()
  local hostSpr = Sprite(48, 48, ColorMode.RGB)
  app.activeSprite = hostSpr

  -- Place Red fixture at (8, 8)
  app.command.PlaceSmartObject{
    filename = srcFileRed,
    x = 8,
    y = 8,
    ui = false
  }

  local smartLayer = hostSpr.layers[2]
  app.activeLayer = smartLayer
  local cel = smartLayer.cels[1]

  -- In Source Red: pixel at local (4, 4) has:
  -- r = 255, g = (4*8)%64 = 32, b = (4*8)%64 = 32, a = 255
  -- Canvas coordinate is (8 + 4, 8 + 4) = (12, 12)
  local targetCanvasX = 12
  local targetCanvasY = 12
  local expectedR = 255
  local expectedG = (4 * 8) % 64
  local expectedB = (4 * 8) % 64

  local pickedColor = nil
  local ok, err = pcall(function()
    pickedColor = app.useTool{
      tool = "eyedropper",
      points = { Point(targetCanvasX, targetCanvasY) }
    }
  end)

  assert_true(ok, "app.useTool with eyedropper must succeed on smart object layer: " .. tostring(err))
  assert_true(pickedColor ~= nil, "Eyedropper must return a Color object")
  assert_eq(pickedColor.red, expectedR, "Picked color red component must match smart object pixel")
  assert_eq(pickedColor.green, expectedG, "Picked color green component must match smart object pixel")
  assert_eq(pickedColor.blue, expectedB, "Picked color blue component must match smart object pixel")

  -- Check app.fgColor updated by eyedropper
  assert_eq(app.fgColor.red, expectedR, "app.fgColor red must match picked color")
  assert_eq(app.fgColor.green, expectedG, "app.fgColor green must match picked color")
  assert_eq(app.fgColor.blue, expectedB, "app.fgColor blue must match picked color")

  -- Check that cel pixels were NOT modified by eyedropper
  assert_true(isRedSourcePixel(cel.image:getPixel(4, 4), 4, 4), "Eyedropper must not modify cel pixels")

  hostSpr:close()
end)

it("EC3.2: Calling app.useTool with marquee selection creates selection without modifying cel", function()
  local hostSpr = Sprite(48, 48, ColorMode.RGB)
  app.activeSprite = hostSpr

  app.command.PlaceSmartObject{
    filename = srcFileBlue,
    x = 10,
    y = 10,
    ui = false
  }

  local smartLayer = hostSpr.layers[2]
  app.activeLayer = smartLayer
  local cel = smartLayer.cels[1]

  -- Initial state: selection is empty
  assert_true(hostSpr.selection.isEmpty, "Initial selection should be empty")

  -- Use rectangular marquee across canvas
  local ok, err = pcall(function()
    app.useTool{
      tool = "rectangular_marquee",
      points = { Point(12, 12), Point(20, 20) }
    }
  end)

  assert_true(ok, "Marquee selection tool must succeed: " .. tostring(err))
  assert_true(not hostSpr.selection.isEmpty, "Selection should now be active on sprite")
  assert_true(hostSpr.selection.bounds.width > 0, "Selection bounds width should be positive")

  -- Verify smart object pixels are 100% untouched
  for y = 0, 23 do
    for x = 0, 23 do
      assert_true(isBlueSourcePixel(cel.image:getPixel(x, y), x, y),
        string.format("Pixel at (%d,%d) modified by selection tool", x, y))
    end
  end

  -- Clear selection safely
  app.command.DeselectMask()
  assert_true(hostSpr.selection.isEmpty, "Selection should be cleared by DeselectMask")

  hostSpr:close()
end)

-- ==============================================================================
-- EDGE CASE 4: READING cel.image:getPixel(x, y) AND CEL METADATA ON SMART OBJECT
-- ==============================================================================
it("EC4.1: Exhaustive getPixel, out-of-bounds queries, metadata access and image cloning", function()
  local hostSpr = Sprite(64, 64, ColorMode.RGB)
  app.activeSprite = hostSpr

  app.command.PlaceSmartObject{
    filename = srcFileGreen,
    x = 14,
    y = 18,
    ui = false
  }

  local smartLayer = hostSpr.layers[2]
  local cel = smartLayer.cels[1]

  -- 4.1.1 Exhaustive grid read (all 20x20 = 400 pixels)
  local readCount = 0
  for y = 0, 19 do
    for x = 0, 19 do
      local px = cel.image:getPixel(x, y)
      assert_true(isGreenSourcePixel(px, x, y), string.format("getPixel mismatch at (%d,%d)", x, y))
      readCount = readCount + 1
    end
  end
  assert_eq(readCount, 400, "Must successfully read all 400 pixels without hindrance")

  -- 4.1.2 Out of bounds reads (doc::get_pixel returns (color_t)-1 when out of bounds)
  local oob1 = cel.image:getPixel(-1, -1)
  local oob2 = cel.image:getPixel(20, 20)
  local oob3 = cel.image:getPixel(100, 100)
  assert_true(oob1 == -1 or (oob1 & 0xffffffff) == 0xffffffff, "Out-of-bounds getPixel should return -1/0xffffffff")
  assert_true(oob2 == -1 or (oob2 & 0xffffffff) == 0xffffffff, "Out-of-bounds getPixel should return -1/0xffffffff")
  assert_true(oob3 == -1 or (oob3 & 0xffffffff) == 0xffffffff, "Out-of-bounds getPixel should return -1/0xffffffff")

  -- 4.1.3 Cel metadata queries
  assert_eq(cel.bounds.x, 14, "cel.bounds.x must match 14")
  assert_eq(cel.bounds.y, 18, "cel.bounds.y must match 18")
  assert_eq(cel.bounds.width, 20, "cel.bounds.width must match 20")
  assert_eq(cel.bounds.height, 20, "cel.bounds.height must match 20")
  assert_eq(cel.position.x, 14, "cel.position.x must match 14")
  assert_eq(cel.position.y, 18, "cel.position.y must match 18")
  assert_eq(cel.opacity, 255, "cel.opacity must be 255")
  assert_eq(cel.zIndex, 0, "cel.zIndex must be 0")
  assert_eq(cel.image.width, 20, "cel.image.width must be 20")
  assert_eq(cel.image.height, 20, "cel.image.height must be 20")
  assert_eq(cel.image.colorMode, ColorMode.RGB, "cel.image.colorMode must be RGB")

  -- 4.1.4 Image cloning: cloning locked cel.image creates a detached mutable copy
  local clonedImage = cel.image:clone()
  assert_true(clonedImage ~= nil, "Cloned image must be valid")
  assert_eq(clonedImage.width, 20, "Cloned image width must be 20")
  assert_eq(clonedImage.height, 20, "Cloned image height must be 20")

  -- Mutating detached clone MUST succeed
  local cloneEditOk = pcall(function()
    clonedImage:drawPixel(0, 0, Color{ r = 123, g = 45, b = 67, a = 255 })
  end)
  assert_true(cloneEditOk, "Mutating detached clone must succeed")
  assert_eq(app.pixelColor.rgbaR(clonedImage:getPixel(0, 0)), 123, "Clone pixel modified")

  -- Cel image itself MUST remain unchanged
  assert_true(isGreenSourcePixel(cel.image:getPixel(0, 0), 0, 0),
    "Mutating cloned image must NOT affect locked cel.image")

  hostSpr:close()
end)

-- ==============================================================================
-- EDGE CASE 5: FILE ROUNDTRIP — SAVE, REOPEN, FLAGS, PIXELS & LOCKOUT RETENTION
-- ==============================================================================
it("EC5.1: Save multi-layer document with smart objects, reload from disk, verify persistence and lockout", function()
  local hostSpr = Sprite(96, 96, ColorMode.RGB)
  app.activeSprite = hostSpr

  -- Base layer: regular layer
  local baseLayer = hostSpr.layers[1]
  baseLayer.name = "Background_Base"
  local baseCel = baseLayer.cels[1]
  baseCel.image:clear(Color{ r = 10, g = 10, b = 10, a = 255 })

  -- Place Smart Object Red at (12, 16)
  app.command.PlaceSmartObject{
    filename = srcFileRed,
    name = "Smart_Red",
    x = 12,
    y = 16,
    ui = false
  }

  -- Place Smart Object Blue at (40, 48)
  app.command.PlaceSmartObject{
    filename = srcFileBlue,
    name = "Smart_Blue",
    x = 40,
    y = 48,
    ui = false
  }

  assert_eq(#hostSpr.layers, 3, "Pre-save layer count must be 3")

  -- Save document to disk
  hostSpr:saveAs(roundtripFile)
  hostSpr:close()
  assert_true(app.fs.isFile(roundtripFile), "Roundtrip .aseprite file must exist on disk")

  -- Reopen saved document
  local loadedSpr = Sprite{ fromFile = roundtripFile }
  assert_true(loadedSpr ~= nil, "Reloaded sprite must not be nil")
  assert_eq(#loadedSpr.layers, 3, "Reloaded document must have exactly 3 layers")

  local reloadedBase = loadedSpr.layers[1]
  local reloadedRed = loadedSpr.layers[2]
  local reloadedBlue = loadedSpr.layers[3]

  -- 5.1.1 Flag verification
  assert_true(not reloadedBase.isSmartObject, "Base layer must NOT be smart object")
  assert_true(reloadedRed.isSmartObject, "Reloaded Red layer MUST retain smart object flag")
  assert_true(reloadedBlue.isSmartObject, "Reloaded Blue layer MUST retain smart object flag")

  -- 5.1.2 UserData properties verification
  local propsRed = reloadedRed.properties("smart_object")
  assert_true(propsRed ~= nil, "Properties map 'smart_object' must persist on Red layer")
  assert_eq(propsRed.filepath, srcFileRed, "Stored filepath on Red layer must persist")
  assert_eq(propsRed.pos_x, 12, "Stored pos_x on Red layer must persist")
  assert_eq(propsRed.pos_y, 16, "Stored pos_y on Red layer must persist")
  assert_eq(propsRed.src_w, 16, "Stored src_w on Red layer must persist")
  assert_eq(propsRed.src_h, 16, "Stored src_h on Red layer must persist")

  local propsBlue = reloadedBlue.properties("smart_object")
  assert_true(propsBlue ~= nil, "Properties map 'smart_object' must persist on Blue layer")
  assert_eq(propsBlue.filepath, srcFileBlue, "Stored filepath on Blue layer must persist")
  assert_eq(propsBlue.pos_x, 40, "Stored pos_x on Blue layer must persist")
  assert_eq(propsBlue.pos_y, 48, "Stored pos_y on Blue layer must persist")
  assert_eq(propsBlue.src_w, 24, "Stored src_w on Blue layer must persist")
  assert_eq(propsBlue.src_h, 24, "Stored src_h on Blue layer must persist")

  -- 5.1.3 Cel position & pixel integrity
  local celRed = reloadedRed.cels[1]
  assert_eq(celRed.position.x, 12, "Reloaded Red cel X position must be 12")
  assert_eq(celRed.position.y, 16, "Reloaded Red cel Y position must be 16")
  for y = 0, 15 do
    for x = 0, 15 do
      assert_true(isRedSourcePixel(celRed.image:getPixel(x, y), x, y),
        string.format("Reloaded Red pixel mismatch at (%d,%d)", x, y))
    end
  end

  local celBlue = reloadedBlue.cels[1]
  assert_eq(celBlue.position.x, 40, "Reloaded Blue cel X position must be 40")
  assert_eq(celBlue.position.y, 48, "Reloaded Blue cel Y position must be 48")
  for y = 0, 23 do
    for x = 0, 23 do
      assert_true(isBlueSourcePixel(celBlue.image:getPixel(x, y), x, y),
        string.format("Reloaded Blue pixel mismatch at (%d,%d)", x, y))
    end
  end

  -- 5.1.4 Post-reload lockout persistence check (CRITICAL)
  app.activeSprite = loadedSpr
  app.activeLayer = reloadedRed

  -- Tool lockout on reloaded smart layer
  pcall(function()
    app.useTool{
      tool = "pencil",
      color = Color{ r = 255, g = 255, b = 255 },
      points = { Point(14, 18) }
    }
  end)
  assert_true(isRedSourcePixel(celRed.image:getPixel(2, 2), 2, 2),
    "Lockout MUST persist after reload: pencil tool must be blocked")

  -- Lua mutation lockout on reloaded smart layer
  local okMut, errMut = pcall(function()
    celRed.image:drawPixel(0, 0, Color{ r = 0, g = 0, b = 0 })
  end)
  assert_true(not okMut, "Lockout MUST persist after reload: drawPixel must throw")

  -- Clear command lockout on reloaded smart layer
  pcall(function() app.command.Clear() end)
  assert_true(isRedSourcePixel(celRed.image:getPixel(0, 0), 0, 0),
    "Lockout MUST persist after reload: Clear command must be blocked")

  -- Base regular layer MUST remain editable (lockout must not affect normal layers)
  app.activeLayer = reloadedBase
  local okBaseEdit, errBaseEdit = pcall(function()
    reloadedBase.cels[1].image:drawPixel(1, 1, Color{ r = 99, g = 88, b = 77, a = 255 })
  end)
  assert_true(okBaseEdit, "Regular base layer must remain editable after reload: " .. tostring(errBaseEdit))
  assert_eq(app.pixelColor.rgbaR(reloadedBase.cels[1].image:getPixel(1, 1)), 99,
    "Base layer pixel edit should take effect")

  -- 5.1.5 Reposition on reloaded layer & second roundtrip
  celRed.position = Point(22, 26)
  assert_eq(celRed.position.x, 22, "Updated Red cel position X to 22")
  assert_eq(celRed.position.y, 26, "Updated Red cel position Y to 26")

  loadedSpr:saveAs(roundtripFile)
  loadedSpr:close()

  local loadedSpr2 = Sprite{ fromFile = roundtripFile }
  assert_true(loadedSpr2 ~= nil, "Second reload must succeed")
  local reloadedRed2 = loadedSpr2.layers[2]
  assert_true(reloadedRed2.isSmartObject, "Second reload retains smart object flag")
  assert_eq(reloadedRed2.cels[1].position.x, 22, "Second reload retains updated X position 22")
  assert_eq(reloadedRed2.cels[1].position.y, 26, "Second reload retains updated Y position 26")
  assert_true(isRedSourcePixel(reloadedRed2.cels[1].image:getPixel(0, 0), 0, 0),
    "Second reload retains pixel integrity")
  loadedSpr2:close()
end)

-- ------------------------------------------------------------------------------
-- CLEANUP
-- ------------------------------------------------------------------------------
pcall(function() app.fs.deleteFile(srcFileRed) end)
pcall(function() app.fs.deleteFile(srcFileGreen) end)
pcall(function() app.fs.deleteFile(srcFileBlue) end)
pcall(function() app.fs.deleteFile(roundtripFile) end)

-- ------------------------------------------------------------------------------
-- SUMMARY & VERDICT
-- ------------------------------------------------------------------------------
print("\n======================================================================")
print("CHALLENGER M1 IT2 EDGE CASE SUMMARY")
print("======================================================================")
print(string.format("Total Assertions:  %d", totalAssertions))
print(string.format("Passed Assertions: %d", passedAssertions))
print(string.format("Failed Assertions: %d", failedAssertions))
print("======================================================================")

if failedAssertions > 0 then
  print("\nFAILURES ENCOUNTERED:")
  for i, f in ipairs(testFailures) do
    print(string.format("  [%d] %s", i, f))
  end
  print("\n[VERDICT] REQUEST_CHANGES")
  assert(failedAssertions == 0, string.format("Challenger edge case suite failed with %d assertion failures!", failedAssertions))
else
  print("\n[VERDICT] APPROVE - All empirical edge cases and regression tests passed cleanly!")
end
