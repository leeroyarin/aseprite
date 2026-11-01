-- Independent Reviewer Adversarial Test Suite for M1 Iteration 3
-- Testing LUA-07, CMD-01, CMD-02, CMD-03, and preservation of non-destructive operations

local tempDir = app.fs.tempPath or "."
local srcPath = app.fs.joinPath(tempDir, "reviewer_it3_src.aseprite")

print("======================================================================")
print("INDEPENDENT REVIEWER ADVERSARIAL STRESS TEST: M1 IT3 LOCKOUT & REGRESSION")
print("======================================================================")

-- 1. Setup fixture
local sprFixture = Sprite(16, 16, ColorMode.RGB)
local fImg = sprFixture.cels[1].image
for y = 0, 15 do
  for x = 0, 15 do
    fImg:drawPixel(x, y, Color{ r = 120, g = x * 8, b = y * 8, a = 255 })
  end
end
sprFixture:saveAs(srcPath)
sprFixture:close()

local totalChecks = 0
local passedChecks = 0
local failedChecks = 0

local function check(desc, condition, errMessage)
  totalChecks = totalChecks + 1
  if condition then
    passedChecks = passedChecks + 1
    print(string.format("  [PASS] %s", desc))
  else
    failedChecks = failedChecks + 1
    print(string.format("  [FAIL] %s: %s", desc, errMessage or "Assertion failed"))
  end
end

-- ==============================================================================
-- SECTION 1: LUA-07 ImageIterator Lockout & Non-Destructive Reading
-- ==============================================================================
print("\n--- SECTION 1: ImageIterator Lockout & Reading ---")

local hostSpr = Sprite(32, 32, ColorMode.RGB)
app.command.PlaceSmartObject{ filename = srcPath, x = 2, y = 2, ui = false }
local smartLayer = hostSpr.layers[2]
local smartCel = smartLayer.cels[1]
local smartImg = smartCel.image

-- 1.1 Read pixels via iterator (MUST WORK)
local readCount = 0
local readMatches = true
for it in smartImg:pixels() do
  readCount = readCount + 1
  local px = it()
  local r = app.pixelColor.rgbaR(px)
  if r ~= 120 then
    readMatches = false
  end
  if readCount >= 256 then break end
end
check("1.1: Image:pixels() iterator reads all pixels without error", readCount == 256 and readMatches)

-- 1.2 Read pixels in sub-rectangle via iterator (MUST WORK)
local subCount = 0
for it in smartImg:pixels(Rectangle(2, 2, 4, 4)) do
  subCount = subCount + 1
  local px = it()
  local r = app.pixelColor.rgbaR(px)
  if r ~= 120 then readMatches = false end
end
check("1.2: Image:pixels(rect) iterator reads sub-bounds without error", subCount == 16 and readMatches)

-- 1.3 Write pixel via iterator on Smart Object (MUST THROW ERROR)
local itWriteBlocked = false
local ok, err = pcall(function()
  for it in smartImg:pixels() do
    it(app.pixelColor.rgba(255, 0, 0, 255))
    break
  end
end)
if not ok and string.find(tostring(err), "smart object") then
  itWriteBlocked = true
end
check("1.3: Image:pixels() iterator it(color) write is blocked on smart cel", itWriteBlocked, tostring(err))

-- 1.4 Normal Layer Image iterator write (MUST SUCCEED - NO REGRESSION)
local normalLayer = hostSpr.layers[1]
local normalCel = normalLayer.cels[1]
local normalOk = pcall(function()
  for it in normalCel.image:pixels() do
    it(app.pixelColor.rgba(99, 99, 99, 255))
    break
  end
end)
check("1.4: Normal Layer Image iterator it(color) succeeds (no regression)", normalOk and app.pixelColor.rgbaR(normalCel.image:getPixel(0, 0)) == 99)

-- 1.5 Standalone Image iterator write (MUST SUCCEED - NO REGRESSION)
local standaloneImg = Image(8, 8, ColorMode.RGB)
local standOk = pcall(function()
  for it in standaloneImg:pixels() do
    it(app.pixelColor.rgba(77, 77, 77, 255))
    break
  end
end)
check("1.5: Standalone Image iterator it(color) succeeds (no regression)", standOk and app.pixelColor.rgbaR(standaloneImg:getPixel(0, 0)) == 77)

-- 1.6 Cloned Image from Smart Object (MUST BE MUTABLE INDEPENDENTLY)
local clonedImg = smartImg:clone()
local cloneOk = pcall(function()
  for it in clonedImg:pixels() do
    it(app.pixelColor.rgba(55, 55, 55, 255))
    break
  end
end)
check("1.6: Cloned Image from smart cel can be mutated via iterator independently", cloneOk and app.pixelColor.rgbaR(clonedImg:getPixel(0, 0)) == 55)
check("1.6b: Original smart cel image remains pristine (unaffected by clone mutation)", app.pixelColor.rgbaR(smartImg:getPixel(0, 0)) == 120)

-- ==============================================================================
-- SECTION 2: Direct Image Property & Method Mutations
-- ==============================================================================
print("\n--- SECTION 2: Image Property & Method Mutations ---")

-- 2.1 Image.bytes setter on Smart Object (MUST BE BLOCKED)
local bytesOk, bytesErr = pcall(function()
  local b = smartImg.bytes
  smartImg.bytes = b
end)
check("2.1: Image.bytes setter blocked on smart cel", not bytesOk and string.find(tostring(bytesErr), "smart object"), tostring(bytesErr))

-- 2.2 Image:resize on Smart Object (MUST BE BLOCKED)
local resizeOk, resizeErr = pcall(function()
  smartImg:resize(32, 32)
end)
check("2.2: Image:resize() blocked on smart cel", not resizeOk and string.find(tostring(resizeErr), "smart object"), tostring(resizeErr))

-- 2.3 Image:flip on Smart Object (MUST BE BLOCKED)
local flipOk, flipErr = pcall(function()
  smartImg:flip(FlipType.HORIZONTAL)
end)
check("2.3: Image:flip() blocked on smart cel", not flipOk and string.find(tostring(flipErr), "smart object"), tostring(flipErr))

-- 2.4 Image:shrinkBounds on Smart Object (MUST RETURN RECT WITHOUT ERROR)
local shrinkOk, shrinkRect = pcall(function()
  return smartImg:shrinkBounds()
end)
check("2.4: Image:shrinkBounds() succeeds non-destructively on smart cel", shrinkOk and shrinkRect ~= nil and shrinkRect.width == 16 and shrinkRect.height == 16)

-- 2.5 Sprite:deleteCel on Smart Object (MUST BE BLOCKED)
local delCelOk, delCelErr = pcall(function()
  hostSpr:deleteCel(smartCel)
end)
check("2.5: Sprite:deleteCel() blocked on smart cel", not delCelOk and string.find(tostring(delCelErr), "smart object"), tostring(delCelErr))

-- 2.6 Sprite:newCel on Smart Object (MUST BE BLOCKED)
local newCelOk, newCelErr = pcall(function()
  hostSpr:newCel(smartLayer, 2)
end)
check("2.6: Sprite:newCel() blocked on smart cel", not newCelOk and string.find(tostring(newCelErr), "smart object"), tostring(newCelErr))

-- ==============================================================================
-- SECTION 3: Commands Lockout (CMD-01 Stroke, CMD-02 Fill, CMD-03 MergeDown)
-- ==============================================================================
print("\n--- SECTION 3: Stroke, Fill, and Layer Operations ---")

app.activeSprite = hostSpr
app.activeLayer = smartLayer
hostSpr.selection:select(Rectangle(2, 2, 8, 8))

-- 3.1 Stroke command on Smart Object
local strokeOk = pcall(function() app.command.Stroke() end)
check("3.1: Stroke on active smart layer leaves pixels pristine", app.pixelColor.rgbaR(smartImg:getPixel(2, 2)) == 120)

-- 3.2 Fill command on Smart Object
local fillOk = pcall(function() app.command.Fill() end)
check("3.2: Fill on active smart layer leaves pixels pristine", app.pixelColor.rgbaR(smartImg:getPixel(2, 2)) == 120)

-- 3.3 Stroke and Fill on NORMAL layer (MUST WORK - NO REGRESSION)
app.activeLayer = normalLayer
hostSpr.selection:select(Rectangle(0, 0, 4, 4))
app.fgColor = Color{ r = 88, g = 88, b = 88, a = 255 }
pcall(function() app.command.Fill() end)
check("3.3: Fill on normal layer succeeds without regression", app.pixelColor.rgbaR(normalCel.image:getPixel(0, 0)) == 88)

-- 3.4 MergeDownLayer with Smart Object as active layer
app.activeLayer = smartLayer -- smart layer is layer 2, layer 1 is normal
local layerCountBefore = #hostSpr.layers
pcall(function() app.command.MergeDownLayer() end)
check("3.4: MergeDownLayer on smart layer is blocked (layers not merged)", #hostSpr.layers == layerCountBefore and hostSpr.layers[2].isSmartObject)

-- 3.5 MergeDownLayer with Normal layer above Smart Object
local topNormal = hostSpr:newLayer() -- layer 3
app.activeLayer = topNormal
local layerCountBefore2 = #hostSpr.layers
pcall(function() app.command.MergeDownLayer() end)
check("3.5: MergeDownLayer of normal layer into smart layer below is blocked", #hostSpr.layers == layerCountBefore2 and hostSpr.layers[2].isSmartObject)

-- 3.6 MergeDownLayer between two NORMAL layers (MUST SUCCEED)
local topNormal2 = hostSpr:newLayer() -- layer 4
app.activeLayer = topNormal2
local layerCountBefore3 = #hostSpr.layers
pcall(function() app.command.MergeDownLayer() end)
check("3.6: MergeDownLayer between two normal layers succeeds (no regression)", #hostSpr.layers == layerCountBefore3 - 1)

-- 3.7 Sprite:flatten in Lua with Smart Object (MUST THROW ERROR)
local flattenOk, flattenErr = pcall(function()
  hostSpr:flatten()
end)
check("3.7: Sprite:flatten() blocked when smart object is present", not flattenOk and string.find(tostring(flattenErr), "smart object"), tostring(flattenErr))

-- 3.8 FlattenLayers command with Smart Object
pcall(function() app.command.FlattenLayers() end)
check("3.8: FlattenLayers command blocked (smart layer preserved)", #hostSpr.layers > 1 and hostSpr.layers[2].isSmartObject)

-- 3.9 ConvertLayer on Smart Object
app.activeLayer = smartLayer
pcall(function() app.command.ConvertLayer{ to = "background" } end)
check("3.9a: ConvertLayer to background blocked on smart layer", smartLayer.isSmartObject and not smartLayer.isBackground)

pcall(function() app.command.ConvertLayer{ to = "tilemap" } end)
check("3.9b: ConvertLayer to tilemap blocked on smart layer", smartLayer.isSmartObject and not smartLayer.isTilemap)

-- 3.10 BackgroundFromLayer on Smart Object
pcall(function() app.command.BackgroundFromLayer() end)
check("3.10: BackgroundFromLayer blocked on smart layer", smartLayer.isSmartObject and not smartLayer.isBackground)

-- ==============================================================================
-- SECTION 4: Non-Destructive Operations (Repositioning & Transformations)
-- ==============================================================================
print("\n--- SECTION 4: Non-Destructive Operations Preservation ---")

-- 4.1 Reposition cel via cel.position
local origPos = smartCel.position
smartCel.position = Point(origPos.x + 10, origPos.y + 5)
check("4.1: Reposition smart cel translation updates coordinates", smartCel.position.x == origPos.x + 10 and smartCel.position.y == origPos.y + 5)

-- 4.2 Pixel integrity maintained after translation
check("4.2: Smart cel pixel values bit-identical after translation", app.pixelColor.rgbaR(smartImg:getPixel(0, 0)) == 120)

-- Clean up
hostSpr:close()
pcall(function() os.remove(srcPath) end)

-- Summary
print("\n======================================================================")
print("INDEPENDENT REVIEWER ADVERSARIAL STRESS TEST SUMMARY")
print("======================================================================")
print(string.format("Total Checks:  %d", totalChecks))
print(string.format("Passed Checks: %d", passedChecks))
print(string.format("Failed Checks: %d", failedChecks))
print("======================================================================")

if failedChecks > 0 then
  print("\n[VERDICT] REQUEST_CHANGES - Adversarial flaws found!")
  assert(false, "Adversarial tests failed")
else
  print("\n[VERDICT] APPROVE - All adversarial tests passed with 0 flaws!")
end
