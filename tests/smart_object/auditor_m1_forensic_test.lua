-- Auditor Forensic Verification Suite for Milestone 1
-- Independently checks:
-- 1. Multi-layer source sprite composite rendering
-- 2. Non-trivial coordinates and dimension preservation
-- 3. Self-placement guard
-- 4. Missing file error handling
-- 5. Complete pixel edit lockout across multiple filter and edit commands
-- 6. UserData property persistence roundtrip

print("[AUDITOR TEST] Starting auditor_m1_forensic_test.lua...")

local tempDir = app.fs.tempPath or "."
local srcMultiFile = app.fs.joinPath(tempDir, "auditor_m1_multi_source.aseprite")
local hostFile = app.fs.joinPath(tempDir, "auditor_m1_host.aseprite")

-- Clean up any preexisting artifacts
pcall(function() app.fs.deleteFile(srcMultiFile) end)
pcall(function() app.fs.deleteFile(hostFile) end)

--------------------------------------------------------------------------------
-- Check 1: Multi-layer composite rendering authenticity
--------------------------------------------------------------------------------
print("[AUDITOR TEST] 1. Creating multi-layer source document...")
local srcSpr = Sprite(20, 20, ColorMode.RGB)
-- Default layer: Blue pixels at (2, 2)
local lyr1 = srcSpr.layers[1]
lyr1.name = "BackgroundLayer"
local img1 = lyr1.cels[1].image
img1:drawPixel(2, 2, Color{ r = 0, g = 0, b = 255, a = 255 })

-- Layer 2: Green pixels at (3, 3)
local lyr2 = srcSpr:newLayer()
lyr2.name = "ForegroundLayer"
local cel2 = srcSpr:newCel(lyr2, 1)
local img2 = cel2.image
img2:drawPixel(3, 3, Color{ r = 0, g = 255, b = 0, a = 255 })

srcSpr:saveAs(srcMultiFile)
srcSpr:close()

assert(app.fs.isFile(srcMultiFile), "Source multi-layer file must exist on disk")

--------------------------------------------------------------------------------
-- Check 2: Place smart object and verify authentic composite rendering
--------------------------------------------------------------------------------
print("[AUDITOR TEST] 2. Placing multi-layer smart object at non-zero coordinates...")
local hostSpr = Sprite(50, 50, ColorMode.RGB)
local origLayerCount = #hostSpr.layers

app.command.PlaceSmartObject{
  filename = srcMultiFile,
  name = "MultiSmartObj",
  x = 17,
  y = 23,
  ui = false
}

assert(#hostSpr.layers == origLayerCount + 1, "Host sprite must have exactly 1 new layer")
local smartLayer = hostSpr.layers[#hostSpr.layers]
assert(smartLayer.name == "MultiSmartObj", "Smart object layer name must match parameter")
assert(smartLayer.isSmartObject == true, "Layer must be marked as smart object")

-- Verify cel placement & dimensions
assert(#smartLayer.cels == 1, "Must contain exactly 1 cel")
local smartCel = smartLayer.cels[1]
assert(smartCel.position.x == 17, "Cel position X must be 17")
assert(smartCel.position.y == 23, "Cel position Y must be 23")
assert(smartCel.image.width == 20, "Cel image width must match source width (20)")
assert(smartCel.image.height == 20, "Cel image height must match source height (20)")

-- Verify both layers were composited correctly
local pBlue = smartCel.image:getPixel(2, 2)
local pGreen = smartCel.image:getPixel(3, 3)
assert(app.pixelColor.rgbaB(pBlue) == 255, "Composite cel must have blue pixel from layer 1")
assert(app.pixelColor.rgbaG(pGreen) == 255, "Composite cel must have green pixel from layer 2")
print("[AUDITOR TEST] -> Composite cel verified authentic (both layers rendered).")

--------------------------------------------------------------------------------
-- Check 3: Verify metadata synchronization
--------------------------------------------------------------------------------
print("[AUDITOR TEST] 3. Checking UserData properties...")
local props = smartLayer.properties("smart_object")
assert(props ~= nil, "Properties table 'smart_object' must exist")
assert(props.filepath == srcMultiFile, "Properties filepath must match")
assert(props.pos_x == 17, "Properties pos_x must match 17")
assert(props.pos_y == 23, "Properties pos_y must match 23")
assert(props.src_w == 20, "Properties src_w must match 20")
assert(props.src_h == 20, "Properties src_h must match 20")
assert(props.last_modified ~= nil and props.last_modified > 0, "Last modified timestamp must be recorded")

--------------------------------------------------------------------------------
-- Check 4: Pixel edit prevention against multiple commands
--------------------------------------------------------------------------------
print("[AUDITOR TEST] 4. Stress testing pixel edit lockout...")
app.activeLayer = smartLayer

-- InvertColor
pcall(function() app.command.InvertColor() end)
assert(app.pixelColor.rgbaB(smartCel.image:getPixel(2, 2)) == 255, "InvertColor must NOT modify smart object pixels")

-- Clear
pcall(function() app.command.Clear() end)
assert(app.pixelColor.rgbaB(smartCel.image:getPixel(2, 2)) == 255, "Clear must NOT clear smart object pixels")

-- HueSaturation
pcall(function() app.command.HueSaturation{ mode = "hsl", hue = 180, saturation = 50, lightness = 0 } end)
assert(app.pixelColor.rgbaB(smartCel.image:getPixel(2, 2)) == 255, "HueSaturation must NOT alter smart object pixels")

-- Despeckle
pcall(function() app.command.Despeckle{ width = 3, height = 3 } end)
assert(app.pixelColor.rgbaB(smartCel.image:getPixel(2, 2)) == 255, "Despeckle must NOT alter smart object pixels")
print("[AUDITOR TEST] -> Pixel edit lockout verified across all tested operations.")

--------------------------------------------------------------------------------
-- Check 5: Defensive checks (self-placement & non-existent file)
--------------------------------------------------------------------------------
print("[AUDITOR TEST] 5. Testing defensive failure modes...")
hostSpr:saveAs(hostFile)
local layersBeforeSelfPlace = #hostSpr.layers

-- Try placing host document into itself
app.command.PlaceSmartObject{
  filename = hostFile,
  ui = false
}
assert(#hostSpr.layers == layersBeforeSelfPlace, "Self-placement must be rejected without creating a layer")

-- Try placing non-existent file
local layersBeforeBadFile = #hostSpr.layers
app.command.PlaceSmartObject{
  filename = "C:\\path\\does_not_exist_xyz123456789.aseprite",
  ui = false
}
assert(#hostSpr.layers == layersBeforeBadFile, "Placing non-existent file must be rejected without creating a layer")
print("[AUDITOR TEST] -> Defensive failure modes passed cleanly.")

--------------------------------------------------------------------------------
-- Check 6: File persistence round-trip
--------------------------------------------------------------------------------
print("[AUDITOR TEST] 6. Testing file save & load roundtrip...")
hostSpr:saveAs(hostFile)
hostSpr:close()

local loadedSpr = Sprite{ fromFile = hostFile }
assert(loadedSpr ~= nil, "File should reload cleanly")
assert(#loadedSpr.layers == 2, "Reloaded document must have 2 layers")
local reloadedLyr = loadedSpr.layers[2]
assert(reloadedLyr.isSmartObject == true, "Persisted layer must still have isSmartObject == true")
local reloadedCel = reloadedLyr.cels[1]
assert(reloadedCel.position.x == 17, "Reloaded cel X must be 17")
assert(reloadedCel.position.y == 23, "Reloaded cel Y must be 23")
assert(app.pixelColor.rgbaB(reloadedCel.image:getPixel(2, 2)) == 255, "Reloaded cel pixel blue must match")
assert(app.pixelColor.rgbaG(reloadedCel.image:getPixel(3, 3)) == 255, "Reloaded cel pixel green must match")
loadedSpr:close()

-- Cleanup
pcall(function() app.fs.deleteFile(srcMultiFile) end)
pcall(function() app.fs.deleteFile(hostFile) end)

print("[AUDITOR TEST] ALL FORENSIC CHECKS PASSED EMPIRICALLY!")
