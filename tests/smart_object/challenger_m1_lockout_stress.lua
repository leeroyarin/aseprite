-- ==============================================================================
-- Challenger Milestone 1 Adversarial Stress Test: Pixel Edit Lockout & Non-Destructive Integrity
-- ==============================================================================
-- Tests adversarial bypass attempts across:
--   1. Drawing tools (pencil, brush, eraser, bucket, blur, smudge) via app.useTool
--   2. Selection modifications, Cut, Clear, Paste
--   3. Filter commands (InvertColor, Despeckle, ColorCurve, ConvolutionMatrix, etc.)
--   4. Direct Cel & Image modifications from Lua API
--
-- Specification contract:
--   Smart Object layers must remain strictly non-destructive.
--   ALL pixel modification attempts on a smart object layer MUST be blocked.
-- ==============================================================================

print("======================================================================")
print("STARTING ADVERSARIAL STRESS TEST: Smart Object Lockout & Integrity")
print("======================================================================")

local tempDir = app.fs.tempPath or "."
local srcFile = app.fs.joinPath(tempDir, "challenger_stress_src.aseprite")

-- Helper: Create 16x16 reference sprite with deterministic pixel pattern
local function createSourceFixture()
  local spr = Sprite(16, 16, ColorMode.RGB)
  local img = spr.cels[1].image
  for y = 0, 15 do
    for x = 0, 15 do
      -- Deterministic pattern: Red channel = 200, Green = x*10, Blue = y*10, Alpha = 255
      img:drawPixel(x, y, Color{ r = 200, g = x * 10, b = y * 10, a = 255 })
    end
  end
  spr:saveAs(srcFile)
  spr:close()
  assert(app.fs.isFile(srcFile), "Source fixture file must exist")
end

createSourceFixture()

-- Helper: Create host sprite with smart object placed
local function createHostWithSmartObject()
  local spr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{
    filename = srcFile,
    x = 4,
    y = 4,
    ui = false
  }
  assert(#spr.layers == 2, "Host should have 2 layers")
  local smartLayer = spr.layers[2]
  assert(smartLayer.isSmartObject, "Layer must be smart object")
  app.activeSprite = spr
  app.activeLayer = smartLayer
  return spr, smartLayer
end

-- Helper: Verify if cel image matches the pristine source pattern
local function isCelImagePristine(cel)
  if not cel or not cel.image then return false end
  local img = cel.image
  if img.width ~= 16 or img.height ~= 16 then return false end
  for y = 0, 15 do
    for x = 0, 15 do
      local px = img:getPixel(x, y)
      local r = app.pixelColor.rgbaR(px)
      local g = app.pixelColor.rgbaG(px)
      local b = app.pixelColor.rgbaB(px)
      local a = app.pixelColor.rgbaA(px)
      if r ~= 200 or g ~= (x * 10) or b ~= (y * 10) or a ~= 255 then
        return false, string.format("Pixel mismatch at (%d,%d): expected (200,%d,%d,255) got (%d,%d,%d,%d)",
                                     x, y, x*10, y*10, r, g, b, a)
      end
    end
  end
  return true
end

-- Test accumulator
local testResults = {}
local totalTests = 0
local passedTests = 0
local failedTests = 0

local function recordResult(id, category, name, blocked, details)
  totalTests = totalTests + 1
  if blocked then
    passedTests = passedTests + 1
    print(string.format("  [PASS - BLOCKED] %s: %s", id, name))
  else
    failedTests = failedTests + 1
    print(string.format("  [FAIL - BYPASSED] %s: %s | Details: %s", id, name, details))
  end
  table.insert(testResults, {
    id = id,
    category = category,
    name = name,
    blocked = blocked,
    details = details
  })
end

-- ==============================================================================
-- CATEGORY 1: DRAWING TOOLS VIA app.useTool
-- ==============================================================================
print("\n--- CATEGORY 1: Drawing Tools via app.useTool ---")

local drawToolsToTest = {
  { id = "TOOL-01", name = "pencil", points = { Point(8, 8), Point(9, 9) } },
  { id = "TOOL-02", name = "brush", points = { Point(8, 8) } },
  { id = "TOOL-03", name = "eraser", points = { Point(8, 8) } },
  { id = "TOOL-04", name = "paint_bucket", points = { Point(8, 8) } },
  { id = "TOOL-05", name = "blur", points = { Point(8, 8) } },
  { id = "TOOL-06", name = "smudge", points = { Point(8, 8), Point(9, 9) } },
}

for _, t in ipairs(drawToolsToTest) do
  local spr, smartLayer = createHostWithSmartObject()
  local celBefore = smartLayer.cels[1]
  local ok, err = pcall(function()
    app.useTool{
      tool = t.name,
      color = Color{ r = 255, g = 255, b = 255, a = 255 },
      points = t.points
    }
  end)
  
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Cel was deleted by tool execution"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Pixels modified: " .. (errDetail or "pattern corrupted")
    end
  end
  recordResult(t.id, "Drawing Tools", "Tool '" .. t.name .. "'", blocked, details)
  spr:close()
end

-- ==============================================================================
-- CATEGORY 2: SELECTION & CLIPBOARD COMMANDS
-- ==============================================================================
print("\n--- CATEGORY 2: Selection & Clipboard Commands ---")

-- 2.1 Clear without selection
do
  local spr, smartLayer = createHostWithSmartObject()
  local ok, err = pcall(function() app.command.Clear() end)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Cel was removed by Clear command without selection"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Pixels modified: " .. (errDetail or "pattern corrupted")
    end
  end
  recordResult("CLIP-01", "Selection/Clipboard", "Clear without selection", blocked, details)
  spr:close()
end

-- 2.2 Clear with fully enclosing selection
do
  local spr, smartLayer = createHostWithSmartObject()
  spr.selection:select(Rectangle(0, 0, 32, 32))
  local ok, err = pcall(function() app.command.Clear() end)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Cel was deleted by Clear command with active selection"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Pixels cleared inside selection: " .. (errDetail or "pattern corrupted")
    end
  end
  recordResult("CLIP-02", "Selection/Clipboard", "Clear with enclosing selection", blocked, details)
  spr:close()
end

-- 2.3 Clear with partial intersecting selection
do
  local spr, smartLayer = createHostWithSmartObject()
  spr.selection:select(Rectangle(6, 6, 8, 8))
  local ok, err = pcall(function() app.command.Clear() end)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Cel was deleted by Clear command with partial selection"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Partial selection pixels cleared: " .. (errDetail or "pattern corrupted")
    end
  end
  recordResult("CLIP-03", "Selection/Clipboard", "Clear with partial selection", blocked, details)
  spr:close()
end

-- 2.4 Cut with selection
do
  local spr, smartLayer = createHostWithSmartObject()
  spr.selection:select(Rectangle(4, 4, 16, 16))
  local ok, err = pcall(function() app.command.Cut() end)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Cel was deleted by Cut command"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Pixels removed by Cut: " .. (errDetail or "pattern corrupted")
    end
  end
  recordResult("CLIP-04", "Selection/Clipboard", "Cut with selection", blocked, details)
  spr:close()
end

-- 2.5 Paste image clipboard
do
  local spr, smartLayer = createHostWithSmartObject()
  -- Populate clipboard with distinct 8x8 white image
  local clipSpr = Sprite(8, 8, ColorMode.RGB)
  clipSpr.cels[1].image:clear(Color{ r = 255, g = 255, b = 255, a = 255 })
  clipSpr.selection:select(Rectangle(0, 0, 8, 8))
  app.activeSprite = clipSpr
  app.command.Copy()
  clipSpr:close()

  app.activeSprite = spr
  app.activeLayer = smartLayer
  local initialLayerCount = #spr.layers
  local ok, err = pcall(function() app.command.Paste() end)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Smart layer cel deleted during paste"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Paste modified pixels on smart layer cel: " .. (errDetail or "")
    end
  end
  recordResult("CLIP-05", "Selection/Clipboard", "Paste from clipboard", blocked, details)
  spr:close()
end

-- ==============================================================================
-- CATEGORY 3: FILTER COMMANDS
-- ==============================================================================
print("\n--- CATEGORY 3: Filter Commands ---")

local filterCommands = {
  { id = "FILT-01", name = "InvertColor", fn = function() app.command.InvertColor() end },
  { id = "FILT-02", name = "Despeckle", fn = function() app.command.Despeckle() end },
  { id = "FILT-03", name = "ColorCurve", fn = function() app.command.ColorCurve() end },
  { id = "FILT-04", name = "ConvolutionMatrix", fn = function() app.command.ConvolutionMatrix() end },
  { id = "FILT-05", name = "BrightnessContrast", fn = function() app.command.BrightnessContrast() end },
  { id = "FILT-06", name = "HueSaturation", fn = function() app.command.HueSaturation() end },
  { id = "FILT-07", name = "Outline", fn = function() app.command.Outline() end },
  { id = "FILT-08", name = "ReplaceColor", fn = function()
      app.command.ReplaceColor{ from = Color{r=200,g=0,b=0}, to = Color{r=0,g=255,b=0} }
    end },
}

for _, f in ipairs(filterCommands) do
  local spr, smartLayer = createHostWithSmartObject()
  local ok, err = pcall(f.fn)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "Cel deleted by filter"
  else
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine then
      blocked = false
      details = "Pixels modified by filter: " .. (errDetail or "")
    end
  end
  recordResult(f.id, "Filter Commands", f.name, blocked, details)
  spr:close()
end

-- ==============================================================================
-- CATEGORY 4: DIRECT CEL & IMAGE MUTATIONS VIA LUA API
-- ==============================================================================
print("\n--- CATEGORY 4: Direct Cel & Image Mutations via Lua API ---")

-- 4.1 Direct image:drawPixel
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local ok, err = pcall(function()
    cel.image:drawPixel(0, 0, Color{ r = 0, g = 255, b = 0, a = 255 })
  end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(cel)
  if not pristine then
    blocked = false
    details = "cel.image:drawPixel successfully mutated pixel without lockout"
  end
  recordResult("LUA-01", "Direct Lua Mutation", "cel.image:drawPixel()", blocked, details)
  spr:close()
end

-- 4.2 Direct image:clear
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local ok, err = pcall(function()
    cel.image:clear(Color{ r = 0, g = 0, b = 255, a = 255 })
  end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(cel)
  if not pristine then
    blocked = false
    details = "cel.image:clear() erased entire cel image without lockout"
  end
  recordResult("LUA-02", "Direct Lua Mutation", "cel.image:clear()", blocked, details)
  spr:close()
end

-- 4.3 Direct image:drawImage
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local alienImg = Image(8, 8, ColorMode.RGB)
  alienImg:clear(Color{ r = 255, g = 255, b = 0, a = 255 })
  local ok, err = pcall(function()
    cel.image:drawImage(alienImg, Point(0, 0))
  end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(cel)
  if not pristine then
    blocked = false
    details = "cel.image:drawImage() stamped alien pixels onto cel without lockout"
  end
  recordResult("LUA-03", "Direct Lua Mutation", "cel.image:drawImage()", blocked, details)
  spr:close()
end

-- 4.4 Reassign cel.image
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local alienImg = Image(16, 16, ColorMode.RGB)
  alienImg:clear(Color{ r = 128, g = 128, b = 128, a = 255 })
  local ok, err = pcall(function()
    cel.image = alienImg
  end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(cel)
  if not pristine then
    blocked = false
    details = "cel.image reassignment replaced cel image without lockout"
  end
  recordResult("LUA-04", "Direct Lua Mutation", "cel.image reassignment", blocked, details)
  spr:close()
end

-- 4.5 Delete cel via cel.image = nil
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local ok, err = pcall(function()
    cel.image = nil
  end)
  local blocked = true
  local details = ""
  if #smartLayer.cels == 0 then
    blocked = false
    details = "cel.image = nil deleted cel from smart object layer"
  end
  recordResult("LUA-05", "Direct Lua Mutation", "cel.image = nil cel deletion", blocked, details)
  spr:close()
end

-- 4.6 Sprite:newCel on smart object layer
do
  local spr, smartLayer = createHostWithSmartObject()
  local alienImg = Image(16, 16, ColorMode.RGB)
  alienImg:clear(Color{ r = 50, g = 50, b = 50, a = 255 })
  local initialCelCount = #smartLayer.cels
  local ok, err = pcall(function()
    spr:newCel(smartLayer, 1, alienImg)
  end)
  local blocked = true
  local details = ""
  if ok or #smartLayer.cels > initialCelCount then
    -- Check if image changed
    local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
    if not pristine or ok then
      blocked = false
      details = "spr:newCel succeeded on smart object layer, violating non-destructive link"
    end
  end
  recordResult("LUA-06", "Direct Lua Mutation", "spr:newCel() on smart layer", blocked, details)
  spr:close()
end

-- 4.7 cel.image:pixels() iterator mutation
do
  local spr, smartLayer = createHostWithSmartObject()
  local cel = smartLayer.cels[1]
  local ok, err = pcall(function()
    for it in cel.image:pixels() do
      it(app.pixelColor.rgba(0, 255, 0, 255))
      break
    end
  end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(cel)
  if not pristine then
    blocked = false
    details = "cel.image:pixels() iterator mutated pixel via it(color) without lockout"
  end
  recordResult("LUA-07", "Direct Lua Mutation", "cel.image:pixels() iterator mutation", blocked, details)
  spr:close()
end

-- ==============================================================================
-- CATEGORY 5: CANVAS & LAYER COMMANDS (Fill, Stroke, MergeDown)
-- ==============================================================================
print("\n--- CATEGORY 5: Canvas & Layer Commands ---")

-- 5.1 Stroke command with active selection
do
  local spr, smartLayer = createHostWithSmartObject()
  spr.selection:select(Rectangle(6, 6, 8, 8))
  local ok, err = pcall(function() app.command.Stroke() end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
  if not pristine then
    blocked = false
    details = "Stroke command drew border onto smart object cel: " .. (errDetail or "")
  end
  recordResult("CMD-01", "Canvas/Layer Commands", "app.command.Stroke() with selection", blocked, details)
  spr:close()
end

-- 5.2 Fill command with active selection
do
  local spr, smartLayer = createHostWithSmartObject()
  spr.selection:select(Rectangle(6, 6, 8, 8))
  local ok, err = pcall(function() app.command.Fill() end)
  local blocked = true
  local details = ""
  local pristine, errDetail = isCelImagePristine(smartLayer.cels[1])
  if not pristine then
    blocked = false
    details = "Fill command filled selection onto smart object cel: " .. (errDetail or "")
  end
  recordResult("CMD-02", "Canvas/Layer Commands", "app.command.Fill() with selection", blocked, details)
  spr:close()
end

-- 5.3 MergeDownLayer command
do
  local spr, smartLayer = createHostWithSmartObject()
  local initialCount = #spr.layers
  local ok, err = pcall(function() app.command.MergeDownLayer() end)
  local blocked = true
  local details = ""
  if #spr.layers < initialCount or (not spr.layers[1].isSmartObject and #spr.layers == 1) then
    blocked = false
    details = "MergeDownLayer collapsed smart object layer into raster layer"
  end
  recordResult("CMD-03", "Canvas/Layer Commands", "app.command.MergeDownLayer() on smart object", blocked, details)
  spr:close()
end

-- Clean up fixture file
pcall(function() os.remove(srcFile) end)

-- ==============================================================================
-- STRESS TEST SUMMARY & EVALUATION
-- ==============================================================================
print("\n======================================================================")
print("ADVERSARIAL STRESS TEST SUMMARY")
print("======================================================================")
print(string.format("Total Stress Test Vectors: %d", totalTests))
print(string.format("Blocked (Protected):       %d", passedTests))
print(string.format("Bypassed (Vulnerabilities): %d", failedTests))
print("======================================================================")

if failedTests > 0 then
  print("\nFAILURE DETAILS:")
  for _, r in ipairs(testResults) do
    if not r.blocked then
      print(string.format("  [%s] %s: %s", r.id, r.name, r.details))
    end
  end
  print("\n[VERDICT] REQUEST_CHANGES - Non-destructive lockout bypass detected!")
  assert(failedTests == 0, string.format("Adversarial stress test failed: %d lockout bypasses found!", failedTests))
else
  print("\n[VERDICT] APPROVE - All adversarial bypass attempts successfully blocked.")
end
