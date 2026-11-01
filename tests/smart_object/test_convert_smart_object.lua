-- ==============================================================================
-- Test Suite: test_convert_smart_object.lua
-- Verification for:
-- 1. Import / Place Smart Object command execution
-- 2. Convert to Group (unpacking smart object)
-- 3. Undo/Redo cycles for Convert to Group
-- 4. Convert selected layers to Single Smart Object
-- 5. Convert multiple layers to Separate Smart Objects
-- 6. Convert single layer to Smart Object
-- 7. Convert multiple layers to Group
-- 8. Undo/Redo cycles for all conversions
-- ==============================================================================

print("======================================================")
print("  Running Smart Object Import & Convert-To Tests      ")
print("======================================================")

local function getScriptDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  local dir = path:match("(.*[/\\])") or "./"
  return dir
end

local scriptDir = getScriptDir()

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "mismatch", tostring(expected), tostring(actual)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error("ASSERTION FAILED: " .. (msg or "expected true"), 2)
  end
end

local function assert_false(cond, msg)
  if cond then
    error("ASSERTION FAILED: " .. (msg or "expected false"), 2)
  end
end

local function assert_not_nil(val, msg)
  if val == nil then
    error("ASSERTION FAILED: " .. (msg or "expected non-nil"), 2)
  end
end

local fixturePath = scriptDir .. "fixtures/single_frame_16x16.aseprite"
local altFixturePath = scriptDir .. "fixtures/multi_frame_16x16.aseprite"

-- Check fixture existence
local f = io.open(fixturePath, "rb")
if f then
  f:close()
else
  error("Fixture not found: " .. fixturePath)
end

-- ==============================================================================
-- 1. Import / Place Smart Object Command Execution
-- ==============================================================================
print("\n[Section 1] Verifying PlaceSmartObject Command...")
local spr1 = Sprite(64, 64)
assert_eq(#spr1.layers, 1)

app.command.PlaceSmartObject{
  filename = fixturePath,
  name = "ImportedSmartLayer",
  x = 0,
  y = 0,
  ui = false
}

assert_eq(#spr1.layers, 2, "Sprite should now have 2 layers")
local smartLayer1 = spr1.layers[2]
assert_true(smartLayer1.isSmartObject, "Placed layer must be smart object")
assert_eq(smartLayer1.name, "ImportedSmartLayer", "Layer name must match")
assert_not_nil(smartLayer1.smartObjectFilepath, "Smart object filepath must not be nil")
print("  -> PlaceSmartObject command passed.")

-- ==============================================================================
-- 2. Convert to Group (Unpack Smart Object) & Undo/Redo
-- ==============================================================================
print("\n[Section 2] Verifying Convert to Group (Unpacking Smart Object)...")
app.layer = smartLayer1
assert_true(app.layer.isSmartObject)

-- Execute Convert to Group
app.command.ConvertLayer{ to = "group" }

assert_false(app.layer.isSmartObject, "Active layer must no longer be smart object")
assert_true(app.layer.isGroup, "Active layer must now be a Group")
assert_eq(app.layer.name, "ImportedSmartLayer", "Group should retain smart object name")
local group1 = app.layer
assert_true(#group1.layers >= 1, "Unpacked group must contain child layers from linked doc")
print(string.format("  -> Unpacked group contains %d child layer(s).", #group1.layers))

-- Test Undo of Unpack
print("  -> Testing Undo of Convert to Group...")
app.undo()
assert_eq(#spr1.layers, 2)
local restoredSmart = spr1.layers[2]
assert_true(restoredSmart.isSmartObject, "Undo must restore smart object layer")
assert_false(restoredSmart.isGroup, "Restored layer is not a group")

-- Test Redo of Unpack
print("  -> Testing Redo of Convert to Group...")
app.redo()
assert_false(spr1.layers[2].isSmartObject, "Redo must re-convert to group")
assert_true(spr1.layers[2].isGroup, "Redo must restore group layer")
print("  -> Convert to Group & Undo/Redo verified.")
spr1:close()

-- ==============================================================================
-- 3. Convert Selected Layers to Single Smart Object & Undo/Redo
-- ==============================================================================
print("\n[Section 3] Verifying Convert Selected Layers to Single Smart Object...")
local spr2 = Sprite(64, 64)
local l1 = spr2.layers[1]
l1.name = "Background"
local l2 = spr2:newLayer()
l2.name = "LayerA"
local l3 = spr2:newLayer()
l3.name = "LayerB"

-- Draw distinct pixels on l2 and l3
local img2 = Image(64, 64)
img2:drawPixel(10, 10, Color{ r=255, g=0, b=0, a=255 })
spr2:newCel(l2, 1, img2)

local img3 = Image(64, 64)
img3:drawPixel(20, 20, Color{ r=0, g=255, b=0, a=255 })
spr2:newCel(l3, 1, img3)

assert_eq(#spr2.layers, 3)

-- Select l2 and l3
app.range.layers = { l2, l3 }
local tempSmartPath = scriptDir .. "fixtures/temp_single_smart_" .. tostring(os.time()) .. ".aseprite"

app.command.ConvertLayer{
  to = "smart_object",
  filename = tempSmartPath,
  ui = false
}

-- Check file on disk
local checkFile = io.open(tempSmartPath, "rb")
assert_not_nil(checkFile, "Saved smart object file must exist on disk: " .. tempSmartPath)
checkFile:close()

-- In spr2, l2 and l3 should be replaced by a single smart object layer
assert_eq(#spr2.layers, 2, "Sprite should have 2 layers (Background + SmartObject)")
local smartLayer2 = spr2.layers[2]
assert_true(smartLayer2.isSmartObject, "Replaced layer must be smart object")
print(string.format("  -> Smart object created: name='%s', filepath='%s'", smartLayer2.name, smartLayer2.smartObjectFilepath or ""))

-- Test Undo of Convert to Smart Object
print("  -> Testing Undo of Convert to Smart Object...")
app.undo()
assert_eq(#spr2.layers, 3, "Undo must restore original 3 layers")
assert_eq(spr2.layers[1].name, "Background")
assert_eq(spr2.layers[2].name, "LayerA")
assert_eq(spr2.layers[3].name, "LayerB")
assert_false(spr2.layers[2].isSmartObject)
assert_false(spr2.layers[3].isSmartObject)

-- Test Redo of Convert to Smart Object
print("  -> Testing Redo of Convert to Smart Object...")
app.redo()
assert_eq(#spr2.layers, 2, "Redo must restore smart object layer")
assert_true(spr2.layers[2].isSmartObject)

-- Clean up temp file
os.remove(tempSmartPath)
spr2:close()
print("  -> Convert to Single Smart Object & Undo/Redo verified.")

-- ==============================================================================
-- 4. Convert Multiple Layers to Separate Smart Objects & Undo/Redo
-- ==============================================================================
print("\n[Section 4] Verifying Convert to Separate Smart Objects...")
local spr3 = Sprite(64, 64)
local baseLayer = spr3.layers[1]
baseLayer.name = "Base"
local sep1 = spr3:newLayer()
sep1.name = "PartAlpha"
local sep2 = spr3:newLayer()
sep2.name = "PartBeta"

local imgA = Image(64, 64)
imgA:drawPixel(5, 5, Color{ r=255, g=100, b=0, a=255 })
spr3:newCel(sep1, 1, imgA)

local imgB = Image(64, 64)
imgB:drawPixel(15, 15, Color{ r=0, g=100, b=255, a=255 })
spr3:newCel(sep2, 1, imgB)

assert_eq(#spr3.layers, 3)

app.range.layers = { sep1, sep2 }
local tempFolder = scriptDir .. "fixtures/temp_sep_" .. tostring(os.time())

app.command.ConvertLayer{
  to = "separate_smart_objects",
  folder = tempFolder,
  ui = false
}

-- Check replacement
assert_eq(#spr3.layers, 3, "Sprite must still have 3 layers")
assert_false(spr3.layers[1].isSmartObject, "Base layer remains regular")
assert_true(spr3.layers[2].isSmartObject, "Layer 2 must now be smart object")
assert_true(spr3.layers[3].isSmartObject, "Layer 3 must now be smart object")
assert_eq(spr3.layers[2].name, "PartAlpha")
assert_eq(spr3.layers[3].name, "PartBeta")
print(string.format("  -> Separate smart objects created: '%s' and '%s'",
  spr3.layers[2].smartObjectFilepath, spr3.layers[3].smartObjectFilepath))

-- Verify individual files exist
local fA = io.open(spr3.layers[2].smartObjectFilepath, "rb")
assert_not_nil(fA, "File PartAlpha must exist on disk")
fA:close()

local fB = io.open(spr3.layers[3].smartObjectFilepath, "rb")
assert_not_nil(fB, "File PartBeta must exist on disk")
fB:close()

-- Test Undo of Separate Smart Objects
print("  -> Testing Undo of Convert to Separate Smart Objects...")
app.undo()
assert_eq(#spr3.layers, 3)
assert_false(spr3.layers[2].isSmartObject, "Undo must restore regular layer PartAlpha")
assert_false(spr3.layers[3].isSmartObject, "Undo must restore regular layer PartBeta")

-- Test Redo of Separate Smart Objects
print("  -> Testing Redo of Convert to Separate Smart Objects...")
app.redo()
assert_eq(#spr3.layers, 3)
assert_true(spr3.layers[2].isSmartObject, "Redo must restore smart object PartAlpha")
assert_true(spr3.layers[3].isSmartObject, "Redo must restore smart object PartBeta")

-- Clean up
os.remove(spr3.layers[2].smartObjectFilepath)
os.remove(spr3.layers[3].smartObjectFilepath)
os.remove(tempFolder)
spr3:close()
print("  -> Convert to Separate Smart Objects & Undo/Redo verified.")

-- ==============================================================================
-- 5. Convert Single Layer to Smart Object
-- ==============================================================================
print("\n[Section 5] Verifying Convert Single Layer to Smart Object...")
local spr4 = Sprite(32, 32)
local singleLayer = spr4.layers[1]
singleLayer.name = "MySoloLayer"
local singleTempPath = scriptDir .. "fixtures/temp_solo_" .. tostring(os.time()) .. ".aseprite"

app.range.layers = { singleLayer }
app.command.ConvertLayer{
  to = "smart_object",
  filename = singleTempPath,
  ui = false
}

assert_eq(#spr4.layers, 1)
assert_true(spr4.layers[1].isSmartObject, "Single layer must be converted to smart object")
assert_eq(spr4.layers[1].name, "MySoloLayer")

app.undo()
assert_false(spr4.layers[1].isSmartObject, "Undo must restore original layer")
app.redo()
assert_true(spr4.layers[1].isSmartObject, "Redo must restore smart object layer")

os.remove(singleTempPath)
spr4:close()
print("  -> Convert Single Layer to Smart Object verified.")

-- ==============================================================================
-- 6. Convert Multiple Selected Regular Layers to Group
-- ==============================================================================
print("\n[Section 6] Verifying Convert Multiple Layers to Group...")
local spr5 = Sprite(32, 32)
local rootA = spr5.layers[1]
rootA.name = "RootA"
local rootB = spr5:newLayer()
rootB.name = "RootB"
local rootC = spr5:newLayer()
rootC.name = "RootC"

app.range.layers = { rootB, rootC }
app.command.ConvertLayer{ to = "group" }

assert_eq(#spr5.layers, 2, "Sprite should have RootA and the new Group")
local grp = spr5.layers[2]
assert_true(grp.isGroup, "New layer must be a group")
assert_eq(#grp.layers, 2, "Group must contain RootB and RootC")
assert_eq(grp.layers[1].name, "RootB")
assert_eq(grp.layers[2].name, "RootC")

app.undo()
assert_eq(#spr5.layers, 3, "Undo must ungroup back to 3 layers")
assert_eq(spr5.layers[1].name, "RootA")
assert_eq(spr5.layers[2].name, "RootB")
assert_eq(spr5.layers[3].name, "RootC")

app.redo()
assert_eq(#spr5.layers, 2, "Redo must re-group")
assert_true(spr5.layers[2].isGroup)

spr5:close()
print("  -> Convert Multiple Layers to Group verified.")

print("\n======================================================")
print("  ALL CONVERT TO & IMPORT SMART OBJECT TESTS PASSED!  ")
print("======================================================")
