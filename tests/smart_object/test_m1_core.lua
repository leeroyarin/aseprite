-- Test Milestone 1: Smart Object Core Functionality
-- Tests LayerSmartObject data model, PlaceSmartObject command,
-- direct pixel edit lockout, and cel repositioning.

print("[TEST M1] Starting test_m1_core.lua...")

local tempDir = app.fs.tempPath or "."
local srcFile = app.fs.joinPath(tempDir, "test_m1_source.aseprite")
local hostFile = app.fs.joinPath(tempDir, "test_m1_host.aseprite")

-- 1. Create a sample source document with colored pixels
local srcSpr = Sprite(16, 16, ColorMode.RGB)
local srcImg = srcSpr.cels[1].image
-- Draw a distinct 4x4 red square in the center
for y = 6, 9 do
  for x = 6, 9 do
    srcImg:drawPixel(x, y, Color{ r = 255, g = 0, b = 0, a = 255 })
  end
end
srcSpr:saveAs(srcFile)
srcSpr:close()

assert(app.fs.isFile(srcFile), "Source file should exist on disk")
print("[TEST M1] Created source .aseprite file: " .. srcFile)

-- 2. Create host document
local hostSpr = Sprite(64, 64, ColorMode.RGB)
assert(#hostSpr.layers == 1, "Host should have 1 default layer")

-- 3. Execute PlaceSmartObject command
print("[TEST M1] Executing PlaceSmartObject command...")
app.command.PlaceSmartObject{
  filename = srcFile,
  x = 10,
  y = 15,
  ui = false
}

-- Verify layer was added
assert(#hostSpr.layers == 2, "Host should now have 2 layers after placement")
local smartLayer = hostSpr.layers[2]
assert(smartLayer ~= nil, "Smart layer should exist")

-- 4. Verify LayerSmartObject properties & metadata
print("[TEST M1] Verifying LayerSmartObject flags and metadata...")
assert(smartLayer.isSmartObject == true, "Layer should be identified as a smart object")

local props = smartLayer.properties("smart_object")
assert(props ~= nil, "Properties map 'smart_object' must exist")
assert(props.filepath == srcFile, "Stored filepath must match source file")
assert(props.pos_x == 10, "Stored pos_x must match placement x")
assert(props.pos_y == 15, "Stored pos_y must match placement y")
assert(props.src_w == 16, "Stored src_w must match source width (16)")
assert(props.src_h == 16, "Stored src_h must match source height (16)")

-- Verify Cel creation and image content
assert(#smartLayer.cels == 1, "Smart layer should have 1 cel at frame 1")
local cel = smartLayer.cels[1]
assert(cel.position.x == 10, "Cel X position must be 10")
assert(cel.position.y == 15, "Cel Y position must be 15")
assert(cel.image.width == 16, "Cel image width must match source")
assert(cel.image.height == 16, "Cel image height must match source")

-- Verify rendered pixel content
local pixel = cel.image:getPixel(6, 6)
assert(app.pixelColor.rgbaR(pixel) == 255, "Source pixel red component must be 255")
assert(app.pixelColor.rgbaA(pixel) == 255, "Source pixel alpha must be 255")

-- 5. Test Direct Pixel Edit Prevention
print("[TEST M1] Testing direct pixel edit prevention...")
app.activeLayer = smartLayer

-- A. Test Filter lockout (should fail or throw without altering pixels)
local filterSuccess = pcall(function()
  app.command.InvertColor()
end)
print("[TEST M1] InvertColor pcall result: " .. tostring(filterSuccess))
-- Pixel must remain unchanged (red, not inverted to cyan)
local pixelAfterFilter = cel.image:getPixel(6, 6)
assert(app.pixelColor.rgbaR(pixelAfterFilter) == 255, "Pixels on smart layer must not be altered by filters")
assert(app.pixelColor.rgbaG(pixelAfterFilter) == 0, "Green channel must still be 0")

-- B. Test Clear / Cut lockout
local initialPixel = cel.image:getPixel(6, 6)
pcall(function()
  app.command.Clear()
end)
local pixelAfterClear = cel.image:getPixel(6, 6)
assert(pixelAfterClear == initialPixel, "Pixels must not be cleared on smart object layer")

-- 6. Test Translation / Repositioning X/Y
print("[TEST M1] Testing layer/cel repositioning...")
cel.position = Point(24, 28)
assert(cel.position.x == 24, "Cel position X should update to 24")
assert(cel.position.y == 28, "Cel position Y should update to 28")

-- Save host document to verify file saving with smart object layer
hostSpr:saveAs(hostFile)
hostSpr:close()
assert(app.fs.isFile(hostFile), "Host file with smart object should save cleanly")

-- Reopen saved host file and verify smart object persists
local reloadedHost = Sprite{ fromFile = hostFile }
assert(reloadedHost ~= nil, "Reloaded host sprite should load")
assert(#reloadedHost.layers == 2, "Reloaded host should retain 2 layers")
local reloadedSmartLayer = reloadedHost.layers[2]
assert(reloadedSmartLayer.isSmartObject == true, "Persisted layer must still be recognized as smart object")
local reloadedProps = reloadedSmartLayer.properties("smart_object")
assert(reloadedProps.filepath == srcFile, "Persisted filepath should match")
reloadedHost:close()

-- Clean up test files
pcall(function() app.fs.deleteFile(srcFile) end)
pcall(function() app.fs.deleteFile(hostFile) end)

print("[TEST M1] All Milestone 1 test assertions passed successfully!")
