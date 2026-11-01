-- ==============================================================================
-- Reviewer Adversarial Stress Test Suite: reviewer_convert_adversarial.lua
-- Tests edge cases, invariants, boundary conditions, and failure modes for:
-- 1. Background layer conversion & unpacking
-- 2. Deep nested group hierarchy conversion & roundtrip unpacking
-- 3. Multi-frame with sparse cels and linked cels across frames
-- 4. Filename sanitization with special characters & collision avoidance
-- 5. Translation offset propagation during unpacking
-- 6. Lockout & illegal conversion prevention (Background/Tilemap/Layer)
-- 7. Broken / missing linked file handling on unpack
-- 8. Rapid undo/redo cycles (20x) stability
-- ==============================================================================

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    error(string.format("ADVERSARIAL FAIL: %s (expected %s, got %s)",
      msg or "mismatch", tostring(expected), tostring(actual)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error("ADVERSARIAL FAIL: " .. (msg or "expected true"), 2)
  end
end

local function assert_false(cond, msg)
  if cond then
    error("ADVERSARIAL FAIL: " .. (msg or "expected false"), 2)
  end
end

local function getScriptDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  return path:match("(.*[/\\])") or "./"
end

local scriptDir = getScriptDir()

print("======================================================")
print("  Reviewer Adversarial Stress Testing: Convert-To     ")
print("======================================================")

-- -----------------------------------------------------------------------------
-- ADV-1: Background Layer Conversion to Smart Object & Undo/Redo
-- -----------------------------------------------------------------------------
print("\n[ADV-1] Testing Background Layer Conversion to Smart Object...")
local sprBg = Sprite(32, 32)
local bgLayer = sprBg.layers[1]
app.layer = bgLayer
app.command.BackgroundFromLayer()
assert_true(bgLayer.isBackground, "Layer 1 should be Background")

local bgImg = Image(32, 32)
bgImg:drawPixel(5, 5, Color{ r=100, g=150, b=200, a=255 })
sprBg:newCel(bgLayer, 1, bgImg)

local bgOutPath = scriptDir .. "fixtures/temp_adv_bg_" .. tostring(os.time()) .. ".aseprite"
app.range.layers = { bgLayer }
app.command.ConvertLayer{
  to = "smart_object",
  filename = bgOutPath,
  ui = false
}

assert_eq(#sprBg.layers, 1)
assert_true(sprBg.layers[1].isSmartObject, "Converted background must be smart object")
assert_false(sprBg.layers[1].isBackground, "Converted layer cannot be background")

-- Undo
app.undo()
assert_eq(#sprBg.layers, 1)
assert_true(sprBg.layers[1].isBackground, "Undo must restore Background layer")

-- Redo
app.redo()
assert_true(sprBg.layers[1].isSmartObject, "Redo must restore Smart Object")

-- Unpack smart object created from background
app.command.ConvertLayer{ to = "group" }
assert_true(sprBg.layers[1].isGroup, "Unpacked smart object must be Group")
assert_eq(#sprBg.layers[1].layers, 1)
-- Check that child layer inside group is transparent, not background (no background inside groups)
assert_false(sprBg.layers[1].layers[1].isBackground, "Child layer in group cannot be background")

-- Clean up
os.remove(bgOutPath)
sprBg:close()
print("  -> ADV-1 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-2: Deep Nested Group Hierarchy Conversion & Unpack Roundtrip
-- -----------------------------------------------------------------------------
print("\n[ADV-2] Testing Nested Group Hierarchy Conversion & Unpack...")
local sprNest = Sprite(32, 32)
local rootGroup = sprNest:newGroup()
rootGroup.name = "RootGroup"
local subGroup = sprNest:newGroup()
subGroup.name = "SubGroup"
subGroup.parent = rootGroup

local innerLayer = sprNest:newLayer()
innerLayer.name = "InnerLayer"
innerLayer.parent = subGroup

local innerImg = Image(32, 32)
innerImg:drawPixel(3, 3, Color{ r=255, g=50, b=50, a=255 })
sprNest:newCel(innerLayer, 1, innerImg)

-- Delete default layer 1
sprNest:deleteLayer(sprNest.layers[1])
app.layer = rootGroup
assert_eq(app.layer.name, "RootGroup")

local nestOutPath = scriptDir .. "fixtures/temp_adv_nest_" .. tostring(os.time()) .. ".aseprite"
app.command.ConvertLayer{
  to = "smart_object",
  filename = nestOutPath,
  ui = false
}

print("  -> SprNest layers count:", #sprNest.layers)
for i, l in ipairs(sprNest.layers) do
  print(string.format("     Layer %d: name='%s' isSmart=%s isGroup=%s", i, l.name, tostring(l.isSmartObject), tostring(l.isGroup)))
  if l.isGroup then
    for j, sub in ipairs(l.layers) do
      print(string.format("       Child %d: name='%s' isSmart=%s isGroup=%s", j, sub.name, tostring(sub.isSmartObject), tostring(sub.isGroup)))
      if sub.isGroup then
        for k, inn in ipairs(sub.layers) do
          print(string.format("         Inner %d: name='%s' isSmart=%s isGroup=%s", k, inn.name, tostring(inn.isSmartObject), tostring(inn.isGroup)))
        end
      end
    end
  end
end

-- Unpack the smart object
app.command.ConvertLayer{ to = "group" }
assert_true(sprNest.layers[1].isGroup, "Must unpack to group")
local unpRoot = sprNest.layers[1]
print("  -> Unpacked Root name:", unpRoot.name)
for i, c in ipairs(unpRoot.layers) do
  print(string.format("     Unpacked child %d: name='%s' isGroup=%s", i, c.name, tostring(c.isGroup)))
  if c.isGroup then
    for j, sc in ipairs(c.layers) do
      print(string.format("       Unpacked subchild %d: name='%s' isGroup=%s", j, sc.name, tostring(sc.isGroup)))
    end
  end
end

-- Undo unpack
app.undo()
assert_true(sprNest.layers[1].isSmartObject, "Undo must restore smart object")

-- Redo unpack
app.redo()
assert_true(sprNest.layers[1].isGroup, "Redo must restore unpacked group")

os.remove(nestOutPath)
sprNest:close()
print("  -> ADV-2 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-3: Multi-Frame Sparse Cels & Linked Cels Across Timeline
-- -----------------------------------------------------------------------------
print("\n[ADV-3] Testing Multi-Frame Sparse Cels & Linked Cels...")
local sprAnim = Sprite(32, 32)
sprAnim.layers[1].name = "AnimLayer"
local animLayer = sprAnim.layers[1]

-- Create 5 frames
for i = 1, 4 do
  sprAnim:newEmptyFrame()
end
assert_eq(#sprAnim.frames, 5)

-- Cel at frame 1 (index 1)
local c1Img = Image(32, 32)
c1Img:drawPixel(1, 1, Color{ r=11, g=22, b=33, a=255 })
sprAnim:newCel(animLayer, 1, c1Img)

-- Cel at frame 3 (index 3) - different image
local c3Img = Image(32, 32)
c3Img:drawPixel(2, 2, Color{ r=44, g=55, b=66, a=255 })
sprAnim:newCel(animLayer, 3, c3Img)

-- Cel at frame 5 (index 5) linked to frame 1
sprAnim:newCel(animLayer, 5, c1Img)

local animOutPath = scriptDir .. "fixtures/temp_adv_anim_" .. tostring(os.time()) .. ".aseprite"
app.range.layers = { animLayer }
app.command.ConvertLayer{
  to = "smart_object",
  filename = animOutPath,
  ui = false
}

assert_eq(#sprAnim.layers, 1)
local animSmart = sprAnim.layers[1]
assert_true(animSmart.isSmartObject)

-- Unpack back to Group
app.command.ConvertLayer{ to = "group" }
assert_true(sprAnim.layers[1].isGroup)
local unpAnimGrp = sprAnim.layers[1]
assert_eq(#unpAnimGrp.layers, 1)
local unpChild = unpAnimGrp.layers[1]

-- Verify cels at frames 1, 3, 5 exist, and frame 2, 4 are empty
assert_true(unpChild:cel(1) ~= nil, "Cel at frame 1 must exist")
assert_true(unpChild:cel(2) == nil, "Cel at frame 2 must be empty")
assert_true(unpChild:cel(3) ~= nil, "Cel at frame 3 must exist")
assert_true(unpChild:cel(4) == nil, "Cel at frame 4 must be empty")
assert_true(unpChild:cel(5) ~= nil, "Cel at frame 5 must exist")

-- Verify cel image content
local pixel1 = unpChild:cel(1).image:getPixel(1, 1)
assert_eq(pixel1, Color{ r=11, g=22, b=33, a=255 }.rgbaPixel, "Frame 1 pixel must match")

os.remove(animOutPath)
sprAnim:close()
print("  -> ADV-3 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-4: Filename Sanitization with Special Characters & Collision Avoidance
-- -----------------------------------------------------------------------------
print("\n[ADV-4] Testing Filename Sanitization & Collisions in Separate Smart Objects...")
local sprSan = Sprite(32, 32)
local lA = sprSan.layers[1]
lA.name = "Layer/With\\Illegal:Chars*?<Test>|"
local lB = sprSan:newLayer()
lB.name = "Layer/With\\Illegal:Chars*?<Test>|" -- identical name to trigger collision avoidance
local lC = sprSan:newLayer()
lC.name = "Layer/With\\Illegal:Chars*?<Test>|"

local sanImg = Image(32, 32)
sanImg:drawPixel(0, 0, Color{ r=1, g=2, b=3, a=255 })
sprSan:newCel(lA, 1, sanImg)
sprSan:newCel(lB, 1, sanImg)
sprSan:newCel(lC, 1, sanImg)

app.range.layers = { lA, lB, lC }
local sanFolder = scriptDir .. "fixtures/temp_adv_san_" .. tostring(os.time())

app.command.ConvertLayer{
  to = "separate_smart_objects",
  folder = sanFolder,
  ui = false
}

assert_eq(#sprSan.layers, 3)
for i = 1, 3 do
  assert_true(sprSan.layers[i].isSmartObject, "Layer " .. i .. " must be smart object")
  local path = sprSan.layers[i].smartObjectFilepath
  assert_true(path ~= nil and path ~= "", "Smart object path must exist")
  local check = io.open(path, "rb")
  assert_true(check ~= nil, "Sanitized file must exist on disk: " .. path)
  check:close()
  os.remove(path)
end

os.remove(sanFolder)
sprSan:close()
print("  -> ADV-4 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-5: Translation Offset Propagation during Unpack
-- -----------------------------------------------------------------------------
print("\n[ADV-5] Testing Translation Offset Propagation during Unpack...")
local sprOff = Sprite(64, 64)
local singleFixture = scriptDir .. "fixtures/single_frame_16x16.aseprite"

app.command.PlaceSmartObject{
  filename = singleFixture,
  name = "OffsetSmart",
  x = 15,
  y = -5,
  ui = false
}

assert_eq(#sprOff.layers, 2)
local offLayer = sprOff.layers[2]
assert_true(offLayer.isSmartObject)
assert_eq(offLayer:cel(1).position.x, 15)
assert_eq(offLayer:cel(1).position.y, -5)

-- Unpack to group
app.layer = offLayer
app.command.ConvertLayer{ to = "group" }
assert_true(app.layer.isGroup)
local unpGrp = app.layer
assert_true(#unpGrp.layers >= 1)

local childLayer = unpGrp.layers[1]
local childCel = childLayer:cel(1)
assert_true(childCel ~= nil, "Child cel must exist")
-- The child cel position must reflect the parent smart object offset (15, -5)
assert_eq(childCel.position.x, 15, "Child cel X position must be shifted by dx")
assert_eq(childCel.position.y, -5, "Child cel Y position must be shifted by dy")

-- Undo unpack
app.undo()
assert_true(sprOff.layers[2].isSmartObject, "Undo restores smart object")
assert_eq(sprOff.layers[2]:cel(1).position.x, 15)
assert_eq(sprOff.layers[2]:cel(1).position.y, -5)

sprOff:close()
print("  -> ADV-5 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-6: Lockout of Illegal Conversions
-- -----------------------------------------------------------------------------
print("\n[ADV-6] Testing Lockout of Illegal Conversions on Smart Objects...")
local sprLock = Sprite(32, 32)
app.command.PlaceSmartObject{
  filename = singleFixture,
  name = "LockSmart",
  ui = false
}
local lockSmart = sprLock.layers[2]
app.layer = lockSmart
assert_true(app.layer.isSmartObject)

-- Try converting smart object to Background: must be rejected / no-op
app.command.ConvertLayer{ to = "background" }
assert_true(sprLock.layers[2].isSmartObject, "Smart object must NOT convert to background")

-- Try converting smart object to Tilemap: must be rejected / no-op
app.command.ConvertLayer{ to = "tilemap" }
assert_true(sprLock.layers[2].isSmartObject, "Smart object must NOT convert to tilemap")

-- Try converting smart object to Transparent Layer: must be rejected / no-op
app.command.ConvertLayer{ to = "layer" }
assert_true(sprLock.layers[2].isSmartObject, "Smart object must NOT convert to transparent layer")

-- Try separate_smart_objects with only 1 layer selected: must be rejected / no-op
app.range.layers = { sprLock.layers[1] }
app.command.ConvertLayer{ to = "separate_smart_objects" }
assert_false(sprLock.layers[1].isSmartObject, "Single layer must NOT convert to separate smart objects")

sprLock:close()
print("  -> ADV-6 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-7: Rapid 20x Undo/Redo Stress
-- -----------------------------------------------------------------------------
print("\n[ADV-7] Testing Rapid 20x Undo/Redo Cycles...")
local sprStress = Sprite(32, 32)
local sLayer = sprStress.layers[1]
local sImg = Image(32, 32)
sImg:drawPixel(10, 10, Color{ r=77, g=88, b=99, a=255 })
sprStress:newCel(sLayer, 1, sImg)

local stressPath = scriptDir .. "fixtures/temp_stress_" .. tostring(os.time()) .. ".aseprite"
app.range.layers = { sLayer }
app.command.ConvertLayer{
  to = "smart_object",
  filename = stressPath,
  ui = false
}
assert_true(sprStress.layers[1].isSmartObject)

for cycle = 1, 20 do
  app.undo()
  assert_false(sprStress.layers[1].isSmartObject, "Cycle " .. cycle .. ": undo failed")
  app.redo()
  assert_true(sprStress.layers[1].isSmartObject, "Cycle " .. cycle .. ": redo failed")
end

os.remove(stressPath)
sprStress:close()
print("  -> ADV-7 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-8: Unpack Smart Object with Missing File on Disk
-- -----------------------------------------------------------------------------
print("\n[ADV-8] Testing Unpack Smart Object with Missing/Deleted File...")
local sprMiss = Sprite(32, 32)
local delPath = scriptDir .. "fixtures/temp_del_target_" .. tostring(os.time()) .. ".aseprite"
-- Copy singleFixture to delPath
local fin = io.open(singleFixture, "rb")
local fcontent = fin:read("*a")
fin:close()
local fout = io.open(delPath, "wb")
fout:write(fcontent)
fout:close()

app.command.PlaceSmartObject{
  filename = delPath,
  name = "DeleteMeSmart",
  ui = false
}
local delSmart = sprMiss.layers[2]
app.layer = delSmart
assert_true(delSmart.isSmartObject)

-- Now delete the file from disk
os.remove(delPath)
assert_true(io.open(delPath, "rb") == nil, "File must be deleted from disk")

-- Try unpack: linkedDoc was cached in memory, so it can still safely unpack or gracefully report
local ok, err = pcall(function()
  app.command.ConvertLayer{ to = "group" }
end)
print("  -> Unpack after disk deletion pcall result: ok=" .. tostring(ok))
-- Sprite should still be intact and have 2 layers
assert_eq(#sprMiss.layers, 2)
sprMiss:close()
print("  -> ADV-8 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-9: Empty Layer (No Cels) Conversion to Smart Object
-- -----------------------------------------------------------------------------
print("\n[ADV-9] Testing Empty Layer (No Cels) Conversion to Smart Object...")
local sprEmpty = Sprite(32, 32)
-- Sprite has 1 default transparent layer, delete its cel
if sprEmpty.layers[1]:cel(1) then
  sprEmpty:deleteCel(sprEmpty.layers[1], 1)
end
assert_true(sprEmpty.layers[1]:cel(1) == nil, "Layer has no cels")

local emptyOutPath = scriptDir .. "fixtures/temp_empty_" .. tostring(os.time()) .. ".aseprite"
app.range.layers = { sprEmpty.layers[1] }
app.command.ConvertLayer{
  to = "smart_object",
  filename = emptyOutPath,
  ui = false
}
assert_true(sprEmpty.layers[1].isSmartObject, "Empty layer successfully converted to smart object")
app.undo()
assert_false(sprEmpty.layers[1].isSmartObject, "Undo restored empty layer")
app.redo()
assert_true(sprEmpty.layers[1].isSmartObject, "Redo restored smart object")

os.remove(emptyOutPath)
sprEmpty:close()
print("  -> ADV-9 Passed.")

-- -----------------------------------------------------------------------------
-- ADV-10: Repeat Separate Smart Objects with Modification between Invocations
-- -----------------------------------------------------------------------------
print("\n[ADV-10] Testing Re-conversion after Undo and Edit...")
local sprRe = Sprite(32, 32)
local r1 = sprRe.layers[1]
r1.name = "ReLayer1"
local r2 = sprRe:newLayer()
r2.name = "ReLayer2"

local reFolder = scriptDir .. "fixtures/temp_adv_re_" .. tostring(os.time())
app.range.layers = { r1, r2 }
app.command.ConvertLayer{
  to = "separate_smart_objects",
  folder = reFolder,
  ui = false
}
assert_true(sprRe.layers[1].isSmartObject)
assert_true(sprRe.layers[2].isSmartObject)

-- Undo
app.undo()
assert_false(sprRe.layers[1].isSmartObject)
assert_false(sprRe.layers[2].isSmartObject)

-- Modify a pixel on r1
local reImg = Image(32, 32)
reImg:drawPixel(1, 1, Color{ r=255, g=255, b=0, a=255 })
sprRe:newCel(sprRe.layers[1], 1, reImg)

-- Convert to separate smart objects again (files already exist in folder)
app.range.layers = { sprRe.layers[1], sprRe.layers[2] }
app.command.ConvertLayer{
  to = "separate_smart_objects",
  folder = reFolder,
  ui = false
}
assert_true(sprRe.layers[1].isSmartObject)
assert_true(sprRe.layers[2].isSmartObject)

-- Clean up
for _, f in ipairs({ "ReLayer1.aseprite", "ReLayer2.aseprite", "ReLayer1_2.aseprite", "ReLayer2_2.aseprite" }) do
  os.remove(reFolder .. "/" .. f)
  os.remove(reFolder .. "\\" .. f)
end
os.remove(reFolder)
sprRe:close()
print("  -> ADV-10 Passed.")

print("\n======================================================")
print("  ALL ADVERSARIAL STRESS TESTS PASSED SUCCESSFULLY!   ")
print("======================================================")
