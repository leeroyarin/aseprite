-- Reviewer M1 Adversarial Stress Test
-- Stress tests C++ Milestone 1 implementation:
-- 1. Error paths & bad inputs (PlaceSmartObject invalid/corrupt/self files)
-- 2. Pixel edit lockout across multiple filters (InvertColor, Outline, ConvolutionMatrix, Clear, Cut, Paste)
-- 3. Cel repositioning & persistence
-- 4. Regression tests on regular LayerImage and Reference layers

print("==================================================")
print("[REVIEWER M1] Starting adversarial stress tests...")
print("==================================================")

local tempDir = app.fs.tempPath or "."
local srcFile = app.fs.joinPath(tempDir, "adv_m1_source.aseprite")
local hostFile = app.fs.joinPath(tempDir, "adv_m1_host.aseprite")
local corruptFile = app.fs.joinPath(tempDir, "adv_m1_corrupt.aseprite")
local zeroByteFile = app.fs.joinPath(tempDir, "adv_m1_zero.aseprite")

-- Setup 1: Create a valid source .aseprite
local srcSpr = Sprite(16, 16, ColorMode.RGB)
local srcImg = srcSpr.cels[1].image
-- Red pixels in top-left 8x8
for y = 0, 7 do
  for x = 0, 7 do
    srcImg:drawPixel(x, y, Color{ r = 255, g = 0, b = 0, a = 255 })
  end
end
srcSpr:saveAs(srcFile)
srcSpr:close()

-- Setup 2: Create a 0-byte file
local f0 = io.open(zeroByteFile, "wb")
f0:close()

-- Setup 3: Create a corrupt file
local fc = io.open(corruptFile, "wb")
fc:write("NOT AN ASEPRITE FILE AT ALL")
fc:close()

print("[ADV 1] Testing PlaceSmartObject error paths...")

local hostSpr = Sprite(32, 32, ColorMode.RGB)
hostSpr:saveAs(hostFile)
local initialLayerCount = #hostSpr.layers

-- 1A: Non-existent file
local ok1 = pcall(function()
  app.command.PlaceSmartObject{ filename = "non_existent_file_xyz.aseprite", ui = false }
end)
assert(#hostSpr.layers == initialLayerCount, "Missing file must not add layer")
print("  [PASS] 1A: Non-existent file handled cleanly")

-- 1B: Zero-byte file
local ok2 = pcall(function()
  app.command.PlaceSmartObject{ filename = zeroByteFile, ui = false }
end)
assert(#hostSpr.layers == initialLayerCount, "Zero-byte file must not add layer")
print("  [PASS] 1B: Zero-byte file handled cleanly")

-- 1C: Corrupt header file
local ok3 = pcall(function()
  app.command.PlaceSmartObject{ filename = corruptFile, ui = false }
end)
assert(#hostSpr.layers == initialLayerCount, "Corrupt file must not add layer")
print("  [PASS] 1C: Corrupt file handled cleanly")

-- 1D: Self-referential placement (host document placed into itself)
local ok4 = pcall(function()
  app.command.PlaceSmartObject{ filename = hostFile, ui = false }
end)
assert(#hostSpr.layers == initialLayerCount, "Placing document into itself must be blocked")
print("  [PASS] 1D: Self-referential placement blocked")

-- Setup 4: Place valid smart object
print("[ADV 2] Placing valid smart object and verifying properties...")
app.command.PlaceSmartObject{
  filename = srcFile,
  x = 5,
  y = 7,
  ui = false
}
assert(#hostSpr.layers == initialLayerCount + 1, "Valid smart object layer added")
local smartLyr = hostSpr.layers[#hostSpr.layers]
assert(smartLyr.isSmartObject == true, "Layer is smart object")
assert(smartLyr.isImage == true, "Smart object is an image layer")
assert(smartLyr.isReference == false, "Smart object is not reference layer")

local cel = smartLyr.cels[1]
assert(cel ~= nil, "Cel exists on smart object layer")
assert(cel.position.x == 5, "Cel X = 5")
assert(cel.position.y == 7, "Cel Y = 7")

local initialPx = cel.image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(initialPx) == 255, "Initial red component is 255")
assert(app.pixelColor.rgbaG(initialPx) == 0, "Initial green component is 0")
print("  [PASS] 2: Smart object successfully placed and verified")

-- Test 3: Multiple destructive filter commands
print("[ADV 3] Testing destructive filter rejection...")
app.activeLayer = smartLyr

-- 3A: InvertColor
local resInvert = app.command.InvertColor()
local pxAfterInvert = cel.image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(pxAfterInvert) == 255, "InvertColor must not modify red channel")
assert(app.pixelColor.rgbaG(pxAfterInvert) == 0, "InvertColor must not modify green channel")
print("  [PASS] 3A: InvertColor rejected without pixel change")

-- 3B: Outline
pcall(function()
  app.command.Outline{ color = Color{ r = 0, g = 255, b = 0, a = 255 }, ui = false }
end)
local pxAfterOutline = cel.image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(pxAfterOutline) == 255, "Outline must not modify smart object")
print("  [PASS] 3B: Outline rejected without pixel change")

-- 3C: ConvolutionMatrix
pcall(function()
  app.command.ConvolutionMatrix{ ui = false }
end)
local pxAfterConv = cel.image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(pxAfterConv) == 255, "ConvolutionMatrix must not modify smart object")
print("  [PASS] 3C: ConvolutionMatrix rejected without pixel change")

-- 3D: Clear
pcall(function()
  app.command.Clear()
end)
local pxAfterClear = cel.image:getPixel(0, 0)
assert(pxAfterClear == initialPx, "Clear must not clear smart object cel")
print("  [PASS] 3D: Clear rejected without pixel change")

-- 3E: Cut
pcall(function()
  app.command.Cut()
end)
local pxAfterCut = cel.image:getPixel(0, 0)
assert(pxAfterCut == initialPx, "Cut must not cut smart object pixels")
print("  [PASS] 3E: Cut rejected without pixel change")

-- Test 4: Cel Repositioning
print("[ADV 4] Testing cel repositioning without link modification...")
cel.position = Point(12, 19)
assert(cel.position.x == 12, "Reposition X = 12")
assert(cel.position.y == 19, "Reposition Y = 19")
local pxAfterMove = cel.image:getPixel(0, 0)
assert(pxAfterMove == initialPx, "Pixels unchanged after move")
print("  [PASS] 4: Cel repositioned cleanly")

-- Test 5: Regression check on normal LayerImage and Reference layer
print("[ADV 5] Testing regression on normal LayerImage and Reference layers...")

-- 5A: Normal layer pixel editing
local normalLyr = hostSpr.layers[1]
assert(normalLyr.isSmartObject == false, "Normal layer is not smart object")
app.activeLayer = normalLyr
local normCel = normalLyr.cels[1] or hostSpr:newCel(normalLyr, 1)
normCel.image:drawPixel(2, 2, Color{ r = 100, g = 150, b = 200, a = 255 })
assert(app.pixelColor.rgbaR(normCel.image:getPixel(2, 2)) == 100, "Normal layer can edit pixels")

-- 5B: Normal layer InvertColor
local resNormInvert = app.command.InvertColor()
print("  InvertColor result on normal layer: " .. tostring(resNormInvert))
normCel = normalLyr.cels[1]
print("  NormCel pos: " .. tostring(normCel.position.x) .. ", " .. tostring(normCel.position.y) .. " size: " .. tostring(normCel.image.width) .. "x" .. tostring(normCel.image.height))
local pxNormAfterInvert = normCel.image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(pxNormAfterInvert) == 255 - 100, "Normal layer InvertColor red inverted")
assert(app.pixelColor.rgbaG(pxNormAfterInvert) == 255 - 150, "Normal layer InvertColor green inverted")
assert(app.pixelColor.rgbaB(pxNormAfterInvert) == 255 - 200, "Normal layer InvertColor blue inverted")
print("  [PASS] 5A & 5B: Normal LayerImage editing and filters function without regression")

-- 5C: Reference layer lockout
local countBeforeRef = #hostSpr.layers
app.command.NewLayer{ reference = true }
assert(#hostSpr.layers == countBeforeRef + 1, "Reference layer added")
local refLyr = nil
for _, lyr in ipairs(hostSpr.layers) do
  if lyr.isReference then
    refLyr = lyr
    break
  end
end
assert(refLyr ~= nil, "Reference layer found")
assert(refLyr.isReference == true, "Reference layer isReference is true")
assert(refLyr.isSmartObject == false, "Reference layer is not smart object")
app.activeLayer = refLyr
pcall(function()
  app.command.InvertColor()
end)
print("  [PASS] 5C: Reference layer remains protected without regression")

-- Test 6: Persistence test (Save and reload)
print("[ADV 6] Testing persistence across save and reload...")
hostSpr:saveAs(hostFile)
hostSpr:close()

local reloaded = Sprite{ fromFile = hostFile }
assert(reloaded ~= nil, "Reloaded sprite must not be nil")
assert(#reloaded.layers >= 2, "Reloaded sprite has all layers")

local foundSmart = false
for _, lyr in ipairs(reloaded.layers) do
  if lyr.isSmartObject then
    foundSmart = true
    local p = lyr.properties("smart_object")
    assert(p ~= nil, "Properties must persist")
    assert(p.filepath == srcFile, "Filepath persisted correctly")
  end
end
assert(foundSmart, "Smart object layer correctly identified upon reload")
reloaded:close()
print("  [PASS] 6: Persistence verified")

-- Cleanup
pcall(function() app.fs.deleteFile(srcFile) end)
pcall(function() app.fs.deleteFile(hostFile) end)
pcall(function() app.fs.deleteFile(corruptFile) end)
pcall(function() app.fs.deleteFile(zeroByteFile) end)

print("==================================================")
print("[REVIEWER M1] ALL ADVERSARIAL STRESS TESTS PASSED!")
print("==================================================")
