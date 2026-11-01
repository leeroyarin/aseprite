-- ==============================================================================
-- Empirical Challenger Test Suite: Smart Object Milestone 1
-- Verifies:
--   1. Varied sizes (1x1, 16x16, 64x64, 128x32) & color modes (RGB, Indexed, Grayscale)
--   2. Bitwise immutability of source file on disk across all operations
--   3. Cel repositioning (positive, negative, out-of-bounds) and link preservation
--   4. Serialization round-trip (save to .aseprite, reopen, verify flags & UserData)
--   5. Color mode cross-conversions between host and source
--   6. Robust error handling (missing files, self-placement lockout)
-- ==============================================================================

print("======================================================================")
print("  EMPIRICAL CHALLENGER TEST SUITE: SMART OBJECT MILESTONE 1")
print("======================================================================")

local testCount = 0
local passCount = 0
local failCount = 0
local failures = {}

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    error(string.format("%s: expected %s, got %s", msg or "assertion failed", tostring(expected), tostring(actual)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error(msg or "assertion failed: condition is not true", 2)
  end
end

local function it(title, fn)
  testCount = testCount + 1
  local ok, err = pcall(fn)
  if ok then
    passCount = passCount + 1
    print(string.format("  [PASS] (%d) %s", testCount, title))
  else
    failCount = failCount + 1
    table.insert(failures, { id = testCount, title = title, err = tostring(err) })
    print(string.format("  [FAIL] (%d) %s", testCount, title))
    print(string.format("         Error: %s", tostring(err)))
  end
end

-- File helpers
local function read_binary_file(filepath)
  local f, err = io.open(filepath, "rb")
  if not f then
    error("Cannot open file for reading: " .. tostring(filepath) .. " (" .. tostring(err) .. ")")
  end
  local content = f:read("*a")
  f:close()
  return content
end

local function get_file_size(filepath)
  local content = read_binary_file(filepath)
  return #content
end

local tempDir = app.fs.tempPath or "."
local cleanupFiles = {}

local function register_temp_file(filename)
  local path = app.fs.joinPath(tempDir, filename)
  table.insert(cleanupFiles, path)
  return path
end

-- ==============================================================================
-- SECTION 1: Source File Creation & Multi-Format/Size Generation
-- ==============================================================================
print("\n--- Phase 1: Preparing Deterministic Test Fixtures ---")

local fixtures = {
  { name = "src_rgb_1x1.aseprite", w = 1, h = 1, mode = ColorMode.RGB },
  { name = "src_rgb_16x16.aseprite", w = 16, h = 16, mode = ColorMode.RGB },
  { name = "src_rgb_64x64.aseprite", w = 64, h = 64, mode = ColorMode.RGB },
  { name = "src_rgb_128x32.aseprite", w = 128, h = 32, mode = ColorMode.RGB },
  { name = "src_gray_16x16.aseprite", w = 16, h = 16, mode = ColorMode.GRAY },
  { name = "src_gray_64x64.aseprite", w = 64, h = 64, mode = ColorMode.GRAY },
  { name = "src_indexed_1x1.aseprite", w = 1, h = 1, mode = ColorMode.INDEXED },
  { name = "src_indexed_16x16.aseprite", w = 16, h = 16, mode = ColorMode.INDEXED },
  { name = "src_indexed_128x32.aseprite", w = 128, h = 32, mode = ColorMode.INDEXED },
}

local fixturePaths = {}

for _, fix in ipairs(fixtures) do
  local path = register_temp_file(fix.name)
  fixturePaths[fix.name] = path

  local spr = Sprite(fix.w, fix.h, fix.mode)
  local img = spr.cels[1].image

  if fix.mode == ColorMode.RGB then
    -- Draw specific distinct corner/center pattern
    img:drawPixel(0, 0, Color{ r = 255, g = 0, b = 0, a = 255 }) -- Red at (0, 0)
    if fix.w > 1 and fix.h > 1 then
      img:drawPixel(fix.w - 1, fix.h - 1, Color{ r = 0, g = 255, b = 0, a = 255 }) -- Green at bottom-right
      local midX = math.floor(fix.w / 2)
      local midY = math.floor(fix.h / 2)
      img:drawPixel(midX, midY, Color{ r = 0, g = 0, b = 255, a = 255 }) -- Blue at center
    end
  elseif fix.mode == ColorMode.GRAY then
    -- Grayscale pattern
    img:drawPixel(0, 0, Color{ gray = 200, a = 255 })
    if fix.w > 1 and fix.h > 1 then
      img:drawPixel(fix.w - 1, fix.h - 1, Color{ gray = 100, a = 255 })
      local midX = math.floor(fix.w / 2)
      local midY = math.floor(fix.h / 2)
      img:drawPixel(midX, midY, Color{ gray = 50, a = 255 })
    end
  elseif fix.mode == ColorMode.INDEXED then
    -- Ensure palette has distinct entries
    if #spr.palettes > 0 then
      local pal = spr.palettes[1]
      if #pal >= 4 then
        pal:setColor(1, Color{ r = 255, g = 128, b = 0, a = 255 }) -- Orange at idx 1
        pal:setColor(2, Color{ r = 128, g = 0, b = 255, a = 255 }) -- Purple at idx 2
        pal:setColor(3, Color{ r = 0, g = 255, b = 255, a = 255 }) -- Cyan at idx 3
      end
    end
    img:drawPixel(0, 0, 1)
    if fix.w > 1 and fix.h > 1 then
      img:drawPixel(fix.w - 1, fix.h - 1, 2)
      local midX = math.floor(fix.w / 2)
      local midY = math.floor(fix.h / 2)
      img:drawPixel(midX, midY, 3)
    end
  end

  spr:saveAs(path)
  spr:close()
  assert_true(app.fs.isFile(path), "Fixture must exist on disk: " .. path)
end

print(string.format("  Successfully generated %d deterministic fixture files.", #fixtures))

-- Record initial binary fingerprints of all fixture files
local fixtureFingerprints = {}
for name, path in pairs(fixturePaths) do
  fixtureFingerprints[name] = read_binary_file(path)
  assert_true(#fixtureFingerprints[name] > 0, "Fingerprint cannot be empty for " .. name)
end

-- ==============================================================================
-- SECTION 2: Empirical Verification — Varied Sizes and Color Modes
-- ==============================================================================
print("\n--- Phase 2: Placing Linked Files of Varied Sizes & Formats ---")

it("Place 1x1 RGB Smart Object into RGB Host", function()
  local host = Sprite(64, 64, ColorMode.RGB)
  local path = fixturePaths["src_rgb_1x1.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 15, y = 20, ui = false }
  
  assert_eq(#host.layers, 2, "Host should have 2 layers")
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  
  local props = soLayer.properties("smart_object")
  assert_eq(props.filepath, path, "Stored filepath match")
  assert_eq(props.src_w, 1, "Source width must be 1")
  assert_eq(props.src_h, 1, "Source height must be 1")
  assert_eq(props.pos_x, 15, "pos_x must be 15")
  assert_eq(props.pos_y, 20, "pos_y must be 20")
  
  local cel = soLayer.cels[1]
  assert_eq(cel.position.x, 15, "Cel pos x must be 15")
  assert_eq(cel.position.y, 20, "Cel pos y must be 20")
  assert_eq(cel.image.width, 1, "Cel image width must be 1")
  assert_eq(cel.image.height, 1, "Cel image height must be 1")
  
  local px = cel.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(px), 255, "1x1 pixel red must be 255")
  assert_eq(app.pixelColor.rgbaA(px), 255, "1x1 pixel alpha must be 255")
  host:close()
end)

it("Place 16x16 RGB Smart Object into RGB Host", function()
  local host = Sprite(64, 64, ColorMode.RGB)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 5, y = 8, ui = false }
  
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.width, 16, "Width must be 16")
  assert_eq(cel.image.height, 16, "Height must be 16")
  assert_eq(cel.position.x, 5, "X must be 5")
  assert_eq(cel.position.y, 8, "Y must be 8")
  
  -- Check corner red pixel at (0, 0)
  local p0 = cel.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(p0), 255, "Corner (0,0) red component must be 255")
  -- Check center blue pixel at (8, 8)
  local pMid = cel.image:getPixel(8, 8)
  assert_eq(app.pixelColor.rgbaB(pMid), 255, "Center (8,8) blue component must be 255")
  host:close()
end)

it("Place 64x64 RGB Smart Object into RGB Host", function()
  local host = Sprite(128, 128, ColorMode.RGB)
  local path = fixturePaths["src_rgb_64x64.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 30, y = 40, ui = false }
  
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.width, 64, "Width must be 64")
  assert_eq(cel.image.height, 64, "Height must be 64")
  assert_eq(cel.position.x, 30, "X must be 30")
  assert_eq(cel.position.y, 40, "Y must be 40")
  
  local pMid = cel.image:getPixel(32, 32)
  assert_eq(app.pixelColor.rgbaB(pMid), 255, "Center blue pixel")
  host:close()
end)

it("Place 128x32 Non-Square RGB Smart Object into RGB Host", function()
  local host = Sprite(256, 128, ColorMode.RGB)
  local path = fixturePaths["src_rgb_128x32.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 10, y = 10, ui = false }
  
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.width, 128, "Width must be 128")
  assert_eq(cel.image.height, 32, "Height must be 32")
  assert_eq(cel.position.x, 10, "X must be 10")
  assert_eq(cel.position.y, 10, "Y must be 10")
  
  local pEnd = cel.image:getPixel(127, 31)
  assert_eq(app.pixelColor.rgbaG(pEnd), 255, "Bottom-right green pixel")
  host:close()
end)

it("Place Grayscale 16x16 Smart Object into RGB Host", function()
  local host = Sprite(64, 64, ColorMode.RGB)
  local path = fixturePaths["src_gray_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 12, y = 14, ui = false }
  
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.width, 16, "Width must be 16")
  assert_eq(cel.image.height, 16, "Height must be 16")
  
  -- Since host is RGB, converted composite image should be RGB with matching grayscale values
  local p0 = cel.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(p0), 200, "Grayscale 200 should convert to RGB 200")
  assert_eq(app.pixelColor.rgbaG(p0), 200, "Grayscale 200 should convert to RGB 200")
  assert_eq(app.pixelColor.rgbaB(p0), 200, "Grayscale 200 should convert to RGB 200")
  host:close()
end)

it("Place Indexed 16x16 Smart Object into RGB Host", function()
  local host = Sprite(64, 64, ColorMode.RGB)
  local path = fixturePaths["src_indexed_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 0, y = 0, ui = false }
  
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.width, 16, "Width must be 16")
  assert_eq(cel.image.height, 16, "Height must be 16")
  
  -- Pixel at (0, 0) was index 1 (Orange: 255, 128, 0)
  local p0 = cel.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(p0), 255, "Indexed orange red component")
  assert_eq(app.pixelColor.rgbaG(p0), 128, "Indexed orange green component")
  assert_eq(app.pixelColor.rgbaB(p0), 0, "Indexed orange blue component")
  host:close()
end)

it("Place 1x1 Indexed and 128x32 Indexed into Host", function()
  local host = Sprite(150, 100, ColorMode.RGB)
  local path1 = fixturePaths["src_indexed_1x1.aseprite"]
  local path2 = fixturePaths["src_indexed_128x32.aseprite"]
  
  app.command.PlaceSmartObject{ filename = path1, x = 1, y = 1, ui = false }
  app.command.PlaceSmartObject{ filename = path2, x = 10, y = 20, ui = false }
  
  assert_eq(#host.layers, 3, "Host should have 3 layers")
  assert_true(host.layers[2].isSmartObject, "Layer 2 is smart object")
  assert_true(host.layers[3].isSmartObject, "Layer 3 is smart object")
  
  assert_eq(host.layers[2].cels[1].image.width, 1, "1x1 width")
  assert_eq(host.layers[3].cels[1].image.width, 128, "128x32 width")
  assert_eq(host.layers[3].cels[1].image.height, 32, "128x32 height")
  host:close()
end)

it("Place RGB Smart Object into Grayscale Host with Format Conversion", function()
  local host = Sprite(64, 64, ColorMode.GRAY)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 8, y = 8, ui = false }
  
  assert_eq(#host.layers, 2, "Host should have 2 layers")
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.colorMode, ColorMode.GRAY, "Cel image must match host Grayscale mode")
  assert_eq(cel.image.width, 16, "Cel image width must be 16")
  assert_eq(cel.image.height, 16, "Cel image height must be 16")
  host:close()
end)

it("Place RGB Smart Object into Indexed Host with Format Conversion", function()
  local host = Sprite(64, 64, ColorMode.INDEXED)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 4, y = 4, ui = false }
  
  assert_eq(#host.layers, 2, "Host should have 2 layers")
  local soLayer = host.layers[2]
  assert_true(soLayer.isSmartObject, "Layer must be smart object")
  local cel = soLayer.cels[1]
  assert_eq(cel.image.colorMode, ColorMode.INDEXED, "Cel image must match host Indexed mode")
  assert_eq(cel.image.width, 16, "Cel image width must be 16")
  assert_eq(cel.image.height, 16, "Cel image height must be 16")
  host:close()
end)

it("Undo and Redo lifecycle for PlaceSmartObject command", function()
  local host = Sprite(64, 64, ColorMode.RGB)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  
  assert_eq(#host.layers, 1, "Initial layer count")
  app.command.PlaceSmartObject{ filename = path, x = 12, y = 14, ui = false }
  assert_eq(#host.layers, 2, "Layer count after PlaceSmartObject")
  assert_true(host.layers[2].isSmartObject, "Smart object layer present")
  
  -- Test Undo
  app.command.Undo()
  assert_eq(#host.layers, 1, "Layer count after Undo must be 1")
  
  -- Test Redo
  app.command.Redo()
  assert_eq(#host.layers, 2, "Layer count after Redo must be 2")
  assert_true(host.layers[2].isSmartObject, "Smart object layer restored on Redo")
  assert_eq(host.layers[2].cels[1].position.x, 12, "Restored cel pos x")
  assert_eq(host.layers[2].cels[1].position.y, 14, "Restored cel pos y")
  host:close()
end)

it("Multiple independent Smart Layers referencing the SAME source file", function()
  local host = Sprite(100, 100, ColorMode.RGB)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  
  app.command.PlaceSmartObject{ filename = path, x = 10, y = 10, ui = false }
  app.command.PlaceSmartObject{ filename = path, x = 40, y = 50, ui = false }
  
  assert_eq(#host.layers, 3, "Host should have 1 default + 2 smart layers")
  local lay1 = host.layers[2]
  local lay2 = host.layers[3]
  
  assert_true(lay1.isSmartObject, "Layer 1 is smart object")
  assert_true(lay2.isSmartObject, "Layer 2 is smart object")
  
  -- Reposition only layer 1
  lay1.cels[1].position = Point(15, 25)
  
  -- Verify layer 2 position remains unchanged
  assert_eq(lay1.cels[1].position.x, 15, "Layer 1 moved X")
  assert_eq(lay1.cels[1].position.y, 25, "Layer 1 moved Y")
  assert_eq(lay2.cels[1].position.x, 40, "Layer 2 unmoved X")
  assert_eq(lay2.cels[1].position.y, 50, "Layer 2 unmoved Y")
  
  host:close()
end)

it("Canvas resize preserves Smart Object cel position and integrity", function()
  local host = Sprite(64, 64, ColorMode.RGB)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 20, y = 20, ui = false }
  
  local soLayer = host.layers[2]
  assert_eq(soLayer.cels[1].position.x, 20, "Initial X")
  assert_eq(soLayer.cels[1].position.y, 20, "Initial Y")
  
  -- Resize canvas/sprite from 64x64 to 128x128
  host:resize(128, 128)
  assert_eq(host.width, 128, "Resized canvas width")
  assert_eq(host.height, 128, "Resized canvas height")
  
  -- Smart object cel position remains intact
  assert_true(soLayer.isSmartObject, "Layer remains smart object")
  assert_eq(#soLayer.cels, 1, "Cel exists")
  assert_eq(soLayer.cels[1].position.x, 40, "Cel X scaled proportionally with sprite:resize (20 * 2 = 40)")
  assert_eq(soLayer.cels[1].position.y, 40, "Cel Y scaled proportionally with sprite:resize (20 * 2 = 40)")
  host:close()

end)



-- ==============================================================================
-- SECTION 3: Empirical Verification — Source File Immutability / Integrity
-- ==============================================================================
print("\n--- Phase 3: Source File Disk Immutability Verification ---")

it("Source files on disk are NEVER modified or corrupted after placement", function()
  for name, originalBytes in pairs(fixtureFingerprints) do
    local currentBytes = read_binary_file(fixturePaths[name])
    assert_eq(#currentBytes, #originalBytes, "File size must remain identical for " .. name)
    assert_eq(currentBytes, originalBytes, "Byte content must remain 100% bitwise identical for " .. name)
  end
end)

it("Source files on disk remain bitwise identical after cel repositioning and edits", function()
  local host = Sprite(100, 100, ColorMode.RGB)
  local targetFixture = "src_rgb_16x16.aseprite"
  local path = fixturePaths[targetFixture]
  local originalBytes = fixtureFingerprints[targetFixture]
  
  app.command.PlaceSmartObject{ filename = path, x = 10, y = 10, ui = false }
  local soLayer = host.layers[2]
  local cel = soLayer.cels[1]
  
  -- Repeatedly reposition cel
  cel.position = Point(25, 30)
  cel.position = Point(-10, -5)
  cel.position = Point(80, 75)
  cel.position = Point(0, 0)
  
  -- Attempt destructive edits on the active smart layer
  app.activeLayer = soLayer
  pcall(function() app.command.InvertColor() end)
  pcall(function() app.command.Clear() end)
  
  -- Save host document to disk
  local hostSavePath = register_temp_file("test_host_saved.aseprite")
  host:saveAs(hostSavePath)
  host:close()
  
  -- Verify disk file is still bitwise identical to pristine state
  local afterBytes = read_binary_file(path)
  assert_eq(#afterBytes, #originalBytes, "Target fixture byte size must not change")
  assert_eq(afterBytes, originalBytes, "Target fixture must remain 100% bitwise identical")
end)

-- ==============================================================================
-- SECTION 4: Empirical Verification — Cel Repositioning & Coordinates
-- ==============================================================================
print("\n--- Phase 4: Cel Repositioning & Link Integrity ---")

it("Cel position updates across positive, zero, negative, and out-of-bounds coordinates", function()
  local host = Sprite(100, 100, ColorMode.RGB)
  local path = fixturePaths["src_rgb_16x16.aseprite"]
  app.command.PlaceSmartObject{ filename = path, x = 10, y = 10, ui = false }
  
  local soLayer = host.layers[2]
  local cel = soLayer.cels[1]
  
  -- Test 1: Positive translation
  cel.position = Point(45, 55)
  assert_eq(cel.position.x, 45, "Cel X after positive move")
  assert_eq(cel.position.y, 55, "Cel Y after positive move")
  assert_true(soLayer.isSmartObject, "Smart object flag retained")
  assert_eq(soLayer.properties("smart_object").filepath, path, "Link retained")
  
  -- Test 2: Origin (0, 0)
  cel.position = Point(0, 0)
  assert_eq(cel.position.x, 0, "Cel X at origin")
  assert_eq(cel.position.y, 0, "Cel Y at origin")
  assert_true(soLayer.isSmartObject, "Smart object flag retained")
  
  -- Test 3: Negative coordinates (partially off-canvas)
  cel.position = Point(-8, -12)
  assert_eq(cel.position.x, -8, "Negative Cel X")
  assert_eq(cel.position.y, -12, "Negative Cel Y")
  assert_true(soLayer.isSmartObject, "Smart object flag retained")
  
  -- Test 4: Completely out-of-bounds coordinates
  cel.position = Point(500, 800)
  assert_eq(cel.position.x, 500, "Out-of-bounds Cel X")
  assert_eq(cel.position.y, 800, "Out-of-bounds Cel Y")
  assert_true(soLayer.isSmartObject, "Smart object flag retained")
  
  -- Test 5: Verify pixel content unchanged after all movements
  cel.position = Point(10, 10)
  local p0 = cel.image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(p0), 255, "Pixel at local (0,0) must still be red")
  local pMid = cel.image:getPixel(8, 8)
  assert_eq(app.pixelColor.rgbaB(pMid), 255, "Pixel at local (8,8) must still be blue")
  
  host:close()
end)

-- ==============================================================================
-- SECTION 5: Empirical Verification — Save/Reopen Persistence Round-Trip
-- ==============================================================================
print("\n--- Phase 5: Persistence & Serialization Round-Trip ---")

it("Round-trip save and reload preserves Smart Object layer flags, metadata, and pixel content", function()
  local hostFile = register_temp_file("test_roundtrip_master.aseprite")
  local host = Sprite(200, 200, ColorMode.RGB)
  
  local pathA = fixturePaths["src_rgb_16x16.aseprite"]
  local pathB = fixturePaths["src_gray_64x64.aseprite"]
  local pathC = fixturePaths["src_indexed_128x32.aseprite"]
  
  -- Place 3 smart objects
  app.command.PlaceSmartObject{ filename = pathA, x = 10, y = 15, ui = false }
  app.command.PlaceSmartObject{ filename = pathB, x = 30, y = 35, ui = false }
  app.command.PlaceSmartObject{ filename = pathC, x = 50, y = 55, ui = false }
  
  assert_eq(#host.layers, 4, "Host should have 1 default + 3 smart layers")
  
  -- Reposition the first smart object cel
  host.layers[2].cels[1].position = Point(22, 28)
  
  -- Save and close host
  host:saveAs(hostFile)
  host:close()
  assert_true(app.fs.isFile(hostFile), "Saved host file must exist on disk")
  
  -- Reload saved document
  local reloaded = Sprite{ fromFile = hostFile }
  assert_true(reloaded ~= nil, "Reloaded sprite must not be nil")
  assert_eq(#reloaded.layers, 4, "Reloaded sprite must preserve 4 layers")
  
  -- Verify Layer 2 (Smart Object A)
  local layA = reloaded.layers[2]
  assert_true(layA.isSmartObject, "Layer A must have isSmartObject == true")
  local propsA = layA.properties("smart_object")
  assert_true(propsA ~= nil, "Layer A must have properties('smart_object')")
  assert_eq(propsA.filepath, pathA, "Layer A filepath match")
  assert_eq(propsA.src_w, 16, "Layer A src_w match")
  assert_eq(propsA.src_h, 16, "Layer A src_h match")
  assert_true(propsA.last_modified ~= nil and propsA.last_modified > 0, "Layer A last_modified valid")
  assert_eq(#layA.cels, 1, "Layer A has 1 cel")
  assert_eq(layA.cels[1].position.x, 22, "Layer A cel pos x preserved")
  assert_eq(layA.cels[1].position.y, 28, "Layer A cel pos y preserved")
  assert_eq(layA.cels[1].image.width, 16, "Layer A image width preserved")
  assert_eq(layA.cels[1].image.height, 16, "Layer A image height preserved")
  local pxA = layA.cels[1].image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaR(pxA), 255, "Layer A red pixel preserved")
  
  -- Verify Layer 3 (Smart Object B)
  local layB = reloaded.layers[3]
  assert_true(layB.isSmartObject, "Layer B isSmartObject preserved")
  local propsB = layB.properties("smart_object")
  assert_eq(propsB.filepath, pathB, "Layer B filepath match")
  assert_eq(propsB.src_w, 64, "Layer B src_w match")
  assert_eq(propsB.src_h, 64, "Layer B src_h match")
  assert_eq(layB.cels[1].position.x, 30, "Layer B cel pos x preserved")
  assert_eq(layB.cels[1].position.y, 35, "Layer B cel pos y preserved")
  assert_eq(layB.cels[1].image.width, 64, "Layer B image width preserved")
  
  -- Verify Layer 4 (Smart Object C)
  local layC = reloaded.layers[4]
  assert_true(layC.isSmartObject, "Layer C isSmartObject preserved")
  local propsC = layC.properties("smart_object")
  assert_eq(propsC.filepath, pathC, "Layer C filepath match")
  assert_eq(propsC.src_w, 128, "Layer C src_w match")
  assert_eq(propsC.src_h, 32, "Layer C src_h match")
  assert_eq(layC.cels[1].position.x, 50, "Layer C cel pos x preserved")
  assert_eq(layC.cels[1].position.y, 55, "Layer C cel pos y preserved")
  
  -- Verify Pixel Edit Lockout persists after reload
  app.activeLayer = layA
  local initialPixelA = layA.cels[1].image:getPixel(0, 0)
  pcall(function() app.command.InvertColor() end)
  pcall(function() app.command.Clear() end)
  local afterPixelA = layA.cels[1].image:getPixel(0, 0)
  assert_eq(afterPixelA, initialPixelA, "Pixel edit lockout must remain active on reloaded smart layer")
  
  reloaded:close()
end)

-- ==============================================================================
-- SECTION 6: Empirical Verification — Edge Conditions & Error Handling
-- ==============================================================================
print("\n--- Phase 6: Edge Conditions & Error Handling ---")

it("Gracefully handles non-existent file path without crash", function()
  local host = Sprite(32, 32, ColorMode.RGB)
  local fakePath = app.fs.joinPath(tempDir, "this_file_does_not_exist_xyz123.aseprite")
  
  -- PlaceSmartObject should report error safely and not add layer
  app.command.PlaceSmartObject{ filename = fakePath, ui = false }
  assert_eq(#host.layers, 1, "Host layer count must remain 1 on non-existent file")
  host:close()
end)

it("Prevents placing document into itself (self-placement cycle)", function()
  local selfHostPath = register_temp_file("test_self_host.aseprite")
  local host = Sprite(32, 32, ColorMode.RGB)
  host:saveAs(selfHostPath)
  
  -- Attempt to place itself
  app.command.PlaceSmartObject{ filename = selfHostPath, ui = false }
  assert_eq(#host.layers, 1, "Self-placement must be rejected and layer count remain 1")
  host:close()
end)

it("Final check: All fixtures remain bitwise pristine after entire test run", function()
  for name, originalBytes in pairs(fixtureFingerprints) do
    local currentBytes = read_binary_file(fixturePaths[name])
    assert_eq(#currentBytes, #originalBytes, "Final check: File size unchanged for " .. name)
    assert_eq(currentBytes, originalBytes, "Final check: Bytes bitwise pristine for " .. name)
  end
end)

-- ==============================================================================
-- CLEANUP & SUMMARY
-- ==============================================================================
for _, path in ipairs(cleanupFiles) do
  pcall(function()
    if app.fs.isFile(path) then
      app.fs.deleteFile(path)
    end
  end)
end

print("\n======================================================================")
print(string.format("  TEST RESULTS: Total: %d | Passed: %d | Failed: %d", testCount, passCount, failCount))
print("======================================================================")

if failCount > 0 then
  print("\nFAILURE DETAILS:")
  for _, f in ipairs(failures) do
    print(string.format("  [%d] %s: %s", f.id, f.title, f.err))
  end
  error(string.format("CHALLENGER TEST SUITE FAILED: %d test(s) failed.", failCount))
else
  print("\nALL EMPIRICAL ASSERTIONS PASSED WITH ZERO ERRORS.")
end

