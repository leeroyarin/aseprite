-- ==============================================================================
-- Test Suite: test_adversarial_import_conv.lua
-- Adversarial Challenge & Stress Tests for Smart Object Import & Convert-To
--
-- Target Focus Areas:
-- 1. Convert to Group on translated smart objects (offsets carry to cels).
-- 2. Convert to Group on multi-frame smart objects & linked cels.
-- 3. Repeated undo/redo stress cycles (25+ iterations) on all conversion types.
-- 4. ConvertLayer with invalid parameters, null documents, unlinked/missing sources.
-- 5. Special characters in layer names & nested group preservation on unpack.
-- ==============================================================================

print("======================================================================")
print("  ADVERSARIAL STRESS TEST: Smart Object Import Menu & Convert-To      ")
print("======================================================================")

local function getScriptDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  local dir = path:match("(.*[/\\])") or "./"
  return dir
end

local scriptDir = getScriptDir()

local totalAssertions = 0
local passedAssertions = 0

local function check(cond, msg)
  totalAssertions = totalAssertions + 1
  if not cond then
    error("ASSERTION FAILED: " .. (msg or "condition false"), 2)
  end
  passedAssertions = passedAssertions + 1
end

local function assert_eq(actual, expected, msg)
  totalAssertions = totalAssertions + 1
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected '%s', got '%s')",
      msg or "mismatch", tostring(expected), tostring(actual)), 2)
  end
  passedAssertions = passedAssertions + 1
end

local function assert_true(cond, msg)
  check(cond == true, msg or "expected true")
end

local function assert_false(cond, msg)
  check(cond == false, msg or "expected false")
end

local function assert_not_nil(val, msg)
  check(val ~= nil, msg or "expected non-nil")
end

local function colors_match(c1, c2)
  return c1.red == c2.red and c1.green == c2.green and
         c1.blue == c2.blue and c1.alpha == c2.alpha
end

local fixtureDir = scriptDir .. "fixtures/"
local singleFixture = fixtureDir .. "single_frame_16x16.aseprite"
local multiFixture = fixtureDir .. "multi_frame_3f.aseprite"
local tenFrameFixture = fixtureDir .. "multi_frame_10f.aseprite"

-- Verify required fixtures exist
for _, fix in ipairs({ singleFixture, multiFixture, tenFrameFixture }) do
  local f = io.open(fix, "rb")
  if f then
    f:close()
  else
    error("Required fixture not found: " .. fix)
  end
end

-- ==============================================================================
-- SUITE 1: Convert to Group on Translated Smart Objects
-- ==============================================================================
print("\n[SUITE 1] Convert to Group on Translated Smart Objects")

-- 1.1 Positive translation offset carries to unpacked cels
print("  Test 1.1: Positive translation offset (dx=15, dy=23)...")
do
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "SmartPos",
    x = 15,
    y = 23,
    ui = false
  }
  local smartLay = spr.layers[2]
  assert_true(smartLay.isSmartObject, "Smart layer created")
  app.layer = smartLay

  -- Execute Convert to Group
  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup, "Must be converted to group")
  assert_eq(#grp.layers, 1, "Unpacked group must have 1 child layer")
  local childLay = grp.layers[1]
  assert_false(childLay.isSmartObject, "Child layer is standard image layer")

  -- Check unpacked cel position
  local cel = childLay:cel(1)
  assert_not_nil(cel, "Unpacked child layer must have cel on frame 1")
  assert_eq(cel.position.x, 15, "Cel X offset must match smart object X position")
  assert_eq(cel.position.y, 23, "Cel Y offset must match smart object Y position")

  -- Verify pixel content at translated position:
  -- Single fixture (16x16) has top-left 8x8 as Red (255, 0, 0, 255)
  local px = cel.image:getPixel(0, 0)
  local c = Color(px)
  assert_eq(c.red, 255, "Top-left red channel")
  assert_eq(c.green, 0, "Top-left green channel")
  assert_eq(c.blue, 0, "Top-left blue channel")
  assert_eq(c.alpha, 255, "Top-left alpha channel")

  spr:close()
  print("    -> Passed: Cel position and pixels match positive translation.")
end

-- 1.2 Negative translation offset carries to unpacked cels
print("  Test 1.2: Negative translation offset (dx=-5, dy=-8)...")
do
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "SmartNeg",
    x = -5,
    y = -8,
    ui = false
  }
  local smartLay = spr.layers[2]
  app.layer = smartLay

  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup, "Converted to group")
  local childLay = grp.layers[1]
  local cel = childLay:cel(1)
  assert_not_nil(cel, "Child cel exists")
  assert_eq(cel.position.x, -5, "Negative X offset preserved")
  assert_eq(cel.position.y, -8, "Negative Y offset preserved")

  spr:close()
  print("    -> Passed: Cel position matches negative translation.")
end

-- 1.3 Translation after placement via layer:translate() method
print("  Test 1.3: Translation after placement via layer:translate(10, 15)...")
do
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "SmartTranslateMethod",
    x = 0,
    y = 0,
    ui = false
  }
  local smartLay = spr.layers[2]
  smartLay:translate(10, 15)
  app.layer = smartLay

  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  local cel = grp.layers[1]:cel(1)
  assert_not_nil(cel)
  assert_eq(cel.position.x, 10, "Translated X offset carried over")
  assert_eq(cel.position.y, 15, "Translated Y offset carried over")

  spr:close()
  print("    -> Passed: Translation via layer:translate() correctly preserved.")
end

-- 1.4 Translation combined with source cel with non-zero initial offset
print("  Test 1.4: Source with non-zero cel offset + smart object translation...")
do
  -- Create a source sprite with an offset cel at (6, 7)
  local tempSrcPath = fixtureDir .. "temp_offset_src_" .. tostring(os.time()) .. ".aseprite"
  local srcSpr = Sprite(32, 32)
  local srcImg = Image(10, 10)
  srcImg:clear(Color{ r=120, g=200, b=50, a=255 })
  srcSpr:newCel(srcSpr.layers[1], 1, srcImg, Point(6, 7))
  srcSpr:saveAs(tempSrcPath)
  srcSpr:close()

  -- Place into host and translate by (14, 20)
  local hostSpr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = tempSrcPath,
    x = 14,
    y = 20,
    ui = false
  }
  local smartLay = hostSpr.layers[2]
  app.layer = smartLay
  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  local childCel = grp.layers[1]:cel(1)
  assert_not_nil(childCel)
  -- Expected position: 6 + 14 = 20, 7 + 20 = 27
  assert_eq(childCel.position.x, 20, "Combined offset X must be src.x + smart.x")
  assert_eq(childCel.position.y, 27, "Combined offset Y must be src.y + smart.y")

  hostSpr:close()
  os.remove(tempSrcPath)
  print("    -> Passed: Additive source cel offset + smart translation.")
end

-- 1.5 Undo/Redo preserves translation offset precisely
print("  Test 1.5: Undo/Redo on translated unpack preserves exact coordinates...")
do
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "SmartUndoRedoPos",
    x = 21,
    y = 34,
    ui = false
  }
  local smartLay = spr.layers[2]
  app.layer = smartLay

  app.command.ConvertLayer{ to = "group" }
  local grp = app.layer
  assert_true(grp.isGroup)
  assert_eq(grp.layers[1]:cel(1).position.x, 21)
  assert_eq(grp.layers[1]:cel(1).position.y, 34)

  -- Undo back to smart object
  app.undo()
  local restoredSmart = spr.layers[2]
  assert_true(restoredSmart.isSmartObject, "Restored layer is smart object")
  local t = restoredSmart.transform
  assert_eq(math.floor(t.x + 0.5), 21, "Restored smart object X position")
  assert_eq(math.floor(t.y + 0.5), 34, "Restored smart object Y position")

  -- Redo back to group
  app.redo()
  local redoneGrp = spr.layers[2]
  assert_true(redoneGrp.isGroup, "Redone layer is group")
  assert_eq(redoneGrp.layers[1]:cel(1).position.x, 21, "Redone cel X matches")
  assert_eq(redoneGrp.layers[1]:cel(1).position.y, 34, "Redone cel Y matches")

  spr:close()
  print("    -> Passed: Undo and Redo maintain coordinates identically.")
end

-- ==============================================================================
-- SUITE 2: Convert to Group on Multi-Frame Smart Objects
-- ==============================================================================
print("\n[SUITE 2] Convert to Group on Multi-Frame Smart Objects")

-- 2.1 Multi-frame unpacking matching host frame count
print("  Test 2.1: Multi-frame (3 frames) smart object unpack...")
do
  local spr = Sprite(32, 32)
  -- Expand host to 3 frames
  spr:newEmptyFrame()
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 3)

  app.command.PlaceSmartObject{
    filename = multiFixture,
    name = "SmartMulti3F",
    x = 4,
    y = 5,
    ui = false
  }
  local smartLay = spr.layers[2]
  app.layer = smartLay

  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  local child = grp.layers[1]

  -- Check cels on each frame
  local cel1 = child:cel(1)
  local cel2 = child:cel(2)
  local cel3 = child:cel(3)

  assert_not_nil(cel1, "Cel 1 must exist")
  assert_not_nil(cel2, "Cel 2 must exist")
  assert_not_nil(cel3, "Cel 3 must exist")

  -- Check positions carry translation
  assert_eq(cel1.position.x, 4)
  assert_eq(cel1.position.y, 5)
  assert_eq(cel2.position.x, 4)
  assert_eq(cel2.position.y, 5)
  assert_eq(cel3.position.x, 4)
  assert_eq(cel3.position.y, 5)

  -- Check distinct colors (Frame 1: Red, Frame 2: Green, Frame 3: Blue)
  local c1 = Color(cel1.image:getPixel(0, 0))
  local c2 = Color(cel2.image:getPixel(0, 0))
  local c3 = Color(cel3.image:getPixel(0, 0))

  assert_eq(c1.red, 255, "Frame 1 must be Red")
  assert_eq(c2.green, 255, "Frame 2 must be Green")
  assert_eq(c3.blue, 255, "Frame 3 must be Blue")

  spr:close()
  print("    -> Passed: 3-frame cels unpacked with correct colors and offsets.")
end

-- 2.2 Host has FEWER frames than source (Host=1, Source=3)
print("  Test 2.2: Host has fewer frames than source (Host=1, Source=3)...")
do
  local spr = Sprite(32, 32)
  assert_eq(#spr.frames, 1)

  app.command.PlaceSmartObject{
    filename = multiFixture,
    name = "SmartHost1F",
    x = 2,
    y = 3,
    ui = false
  }
  app.layer = spr.layers[2]
  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  local child = grp.layers[1]
  assert_not_nil(child:cel(1), "Frame 1 cel unpacked")
  assert_eq(child:cel(1).position.x, 2)
  assert_eq(child:cel(1).position.y, 3)
  -- Should have exactly 1 frame without crash or out-of-bounds error
  assert_eq(#child.cels, 1, "Only 1 cel in host sprite")

  spr:close()
  print("    -> Passed: Asymmetric frames (Host 1 < Source 3) handled safely.")
end

-- 2.3 Host has MORE frames than source (Host=5, Source=3)
print("  Test 2.3: Host has more frames than source (Host=5, Source=3)...")
do
  local spr = Sprite(32, 32)
  for f = 2, 5 do spr:newEmptyFrame() end
  assert_eq(#spr.frames, 5)

  app.command.PlaceSmartObject{
    filename = multiFixture,
    name = "SmartHost5F",
    x = 0,
    y = 0,
    ui = false
  }
  app.layer = spr.layers[2]
  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  local child = grp.layers[1]
  -- Source had 3 frames, so 3 cels unpacked cleanly without crash
  assert_true(#child.cels >= 3, "At least 3 source cels unpacked")

  spr:close()
  print("    -> Passed: Asymmetric frames (Host 5 > Source 3) handled safely.")
end

-- 2.4 Multi-frame source with LINKED CELS
print("  Test 2.4: Multi-frame source with linked cels unpacks properly...")
do
  local tempLinkedSrc = fixtureDir .. "temp_linked_src_" .. tostring(os.time()) .. ".aseprite"
  local src = Sprite(16, 16)
  src:newEmptyFrame()
  -- Make cel on frame 2 link to cel on frame 1
  local cel1Img = Image(16, 16)
  cel1Img:clear(Color{ r=11, g=22, b=33, a=255 })
  src:newCel(src.layers[1], 1, cel1Img)
  -- Link frame 2 by reusing cel1Img
  src:newCel(src.layers[1], 2, cel1Img)
  src:saveAs(tempLinkedSrc)
  src:close()

  -- Place into host and unpack
  local hostSpr = Sprite(32, 32)
  hostSpr:newEmptyFrame()
  app.command.PlaceSmartObject{
    filename = tempLinkedSrc,
    x = 5,
    y = 6,
    ui = false
  }
  app.layer = hostSpr.layers[2]
  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  local child = grp.layers[1]
  local c1 = child:cel(1)
  local c2 = child:cel(2)
  assert_not_nil(c1, "Unpacked frame 1 cel")
  assert_not_nil(c2, "Unpacked frame 2 cel")
  assert_eq(c1.position.x, 5)
  assert_eq(c1.position.y, 6)
  assert_eq(c2.position.x, 5)
  assert_eq(c2.position.y, 6)

  hostSpr:close()
  os.remove(tempLinkedSrc)
  print("    -> Passed: Linked cels in source unpack with matching positions.")
end

-- ==============================================================================
-- SUITE 3: Repeated Undo/Redo Stress Cycles
-- ==============================================================================
print("\n[SUITE 3] Repeated Undo/Redo Stress Cycles (25+ Iterations)")

-- 3.1 30 rapid undo/redo cycles on Convert to Group
print("  Test 3.1: 30 consecutive undo/redo cycles on Convert to Group...")
do
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "StressUnpack",
    x = 12,
    y = 14,
    ui = false
  }
  local smartLay = spr.layers[2]
  app.layer = smartLay
  app.command.ConvertLayer{ to = "group" }

  for cycle = 1, 30 do
    app.undo()
    assert_eq(#spr.layers, 2, "Layer count after undo cycle " .. cycle)
    assert_true(spr.layers[2].isSmartObject, "Layer 2 must be smart object after undo")

    app.redo()
    assert_eq(#spr.layers, 2, "Layer count after redo cycle " .. cycle)
    assert_true(spr.layers[2].isGroup, "Layer 2 must be group after redo")
  end

  local finalGrp = spr.layers[2]
  assert_eq(finalGrp.name, "StressUnpack")
  assert_eq(finalGrp.layers[1]:cel(1).position.x, 12)
  assert_eq(finalGrp.layers[1]:cel(1).position.y, 14)

  spr:close()
  print("    -> Passed: 30 undo/redo cycles on Convert to Group without degradation.")
end

-- 3.2 25 consecutive undo/redo cycles on Convert Selected Layers to Smart Object
print("  Test 3.2: 25 consecutive undo/redo cycles on Convert to Smart Object...")
do
  local spr = Sprite(32, 32)
  local l1 = spr.layers[1]
  local l2 = spr:newLayer()
  l2.name = "LayerStressA"
  local l3 = spr:newLayer()
  l3.name = "LayerStressB"

  local imgA = Image(32, 32)
  imgA:drawPixel(1, 1, Color{ r=255, g=0, b=0, a=255 })
  spr:newCel(l2, 1, imgA)

  local imgB = Image(32, 32)
  imgB:drawPixel(2, 2, Color{ r=0, g=255, b=0, a=255 })
  spr:newCel(l3, 1, imgB)

  app.range.layers = { l2, l3 }
  local tempStressSmart = fixtureDir .. "temp_stress_smart_" .. tostring(os.time()) .. ".aseprite"

  app.command.ConvertLayer{
    to = "smart_object",
    filename = tempStressSmart,
    ui = false
  }

  for cycle = 1, 25 do
    app.undo()
    assert_eq(#spr.layers, 3, "Layer count after undo cycle " .. cycle)
    assert_false(spr.layers[2].isSmartObject)
    assert_false(spr.layers[3].isSmartObject)

    app.redo()
    assert_eq(#spr.layers, 2, "Layer count after redo cycle " .. cycle)
    assert_true(spr.layers[2].isSmartObject)
  end

  spr:close()
  os.remove(tempStressSmart)
  print("    -> Passed: 25 undo/redo cycles on Convert to Smart Object.")
end

-- 3.3 20 consecutive undo/redo cycles on Convert to Separate Smart Objects
print("  Test 3.3: 20 consecutive undo/redo cycles on Convert to Separate Smart Objects...")
do
  local spr = Sprite(32, 32)
  local l1 = spr.layers[1]
  local l2 = spr:newLayer()
  l2.name = "SepStressA"
  local l3 = spr:newLayer()
  l3.name = "SepStressB"

  app.range.layers = { l2, l3 }
  local tempSepFolder = fixtureDir .. "temp_stress_sep_" .. tostring(os.time())

  app.command.ConvertLayer{
    to = "separate_smart_objects",
    folder = tempSepFolder,
    ui = false
  }

  local fileA = spr.layers[2].smartObjectFilepath
  local fileB = spr.layers[3].smartObjectFilepath

  for cycle = 1, 20 do
    app.undo()
    assert_eq(#spr.layers, 3)
    assert_false(spr.layers[2].isSmartObject)
    assert_false(spr.layers[3].isSmartObject)

    app.redo()
    assert_eq(#spr.layers, 3)
    assert_true(spr.layers[2].isSmartObject)
    assert_true(spr.layers[3].isSmartObject)
  end

  spr:close()
  os.remove(fileA)
  os.remove(fileB)
  os.remove(tempSepFolder)
  print("    -> Passed: 20 undo/redo cycles on Separate Smart Objects.")
end

-- ==============================================================================
-- SUITE 4: Invalid Parameters, Null Documents, and Edge Cases
-- ==============================================================================
print("\n[SUITE 4] Invalid Parameters, Null Documents & Edge Cases")

-- 4.1 Invalid 'to' parameter string
print("  Test 4.1: Invalid 'to' parameter handled gracefully...")
do
  local spr = Sprite(32, 32)
  local initialLayers = #spr.layers
  -- Pass non-existent conversion target
  local success = pcall(function()
    app.command.ConvertLayer{ to = "invalid_conversion_target_xyz" }
  end)
  assert_true(success, "pcall must succeed without crash")
  assert_eq(#spr.layers, initialLayers, "Layers must remain unchanged")
  spr:close()
  print("    -> Passed: Unknown 'to' parameter treated as no-op.")
end

-- 4.2 Nil 'to' parameter
print("  Test 4.2: Nil 'to' parameter handled gracefully...")
do
  local spr = Sprite(32, 32)
  local initialLayers = #spr.layers
  local success = pcall(function()
    app.command.ConvertLayer{}
  end)
  assert_true(success, "pcall must succeed")
  assert_eq(#spr.layers, initialLayers, "Layers remain unchanged")
  spr:close()
  print("    -> Passed: Default/nil 'to' parameter handled safely.")
end

-- 4.3 Null active document (no documents open)
print("  Test 4.3: Invoking ConvertLayer with zero open documents...")
do
  -- Ensure no sprites are open
  while #app.sprites > 0 do
    app.sprites[1]:close()
  end
  assert_eq(#app.sprites, 0, "No sprites open")

  for _, target in ipairs({ "group", "smart_object", "separate_smart_objects", "layer", "background", "tilemap" }) do
    local ok = pcall(function()
      app.command.ConvertLayer{ to = target }
    end)
    assert_true(ok, "ConvertLayer to=" .. target .. " must not crash with no documents open")
  end
  print("    -> Passed: Null document guard executed cleanly for all types.")
end

-- 4.4 Convert to Group when linked file was deleted from disk
print("  Test 4.4: Convert to Group when linked disk file is deleted...")
do
  local tempDelPath = fixtureDir .. "temp_to_delete_" .. tostring(os.time()) .. ".aseprite"
  local s = Sprite(16, 16)
  s:saveAs(tempDelPath)
  s:close()

  local hostSpr = Sprite(32, 32)
  app.command.PlaceSmartObject{
    filename = tempDelPath,
    name = "SmartToDel",
    ui = false
  }
  local smartLay = hostSpr.layers[2]
  app.layer = smartLay

  -- Now delete source file from disk
  os.remove(tempDelPath)

  -- Execute ConvertLayer to group
  local ok = pcall(function()
    app.command.ConvertLayer{ to = "group" }
  end)
  assert_true(ok, "Convert to group must not crash when file is removed from disk")

  -- If memory copy is cached, unpacking succeeds or keeps smart object safely
  assert_true(app.layer.isGroup or app.layer.isSmartObject, "Layer state remains valid")

  hostSpr:close()
  print("    -> Passed: Missing/deleted disk file handled without crash.")
end

-- 4.5 Convert to Group on regular single layer (blocked)
print("  Test 4.5: Convert to Group on a regular single layer is blocked...")
do
  local spr = Sprite(32, 32)
  app.layer = spr.layers[1]
  app.range.layers = { spr.layers[1] }

  app.command.ConvertLayer{ to = "group" }
  assert_false(spr.layers[1].isGroup, "Single regular layer cannot convert to group")
  assert_eq(#spr.layers, 1)

  spr:close()
  print("    -> Passed: Regular single layer is not converted to group.")
end

-- 4.6 Convert to Smart Object when layer is already a Smart Object (blocked)
print("  Test 4.6: Convert to Smart Object on existing Smart Object is blocked...")
do
  local spr = Sprite(32, 32)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "AlreadySmart",
    ui = false
  }
  local smartLay = spr.layers[2]
  app.layer = smartLay
  app.range.layers = { smartLay }

  local initialCount = #spr.layers
  app.command.ConvertLayer{ to = "smart_object", ui = false }
  assert_eq(#spr.layers, initialCount, "Smart object cannot be converted into smart object again")

  spr:close()
  print("    -> Passed: Existing Smart Object rejects redundant convert.")
end

-- 4.7 Convert to Separate Smart Objects with only 1 layer selected (blocked)
print("  Test 4.7: Convert to Separate Smart Objects with 1 layer selected is blocked...")
do
  local spr = Sprite(32, 32)
  app.range.layers = { spr.layers[1] }
  local initialCount = #spr.layers

  app.command.ConvertLayer{ to = "separate_smart_objects", ui = false }
  assert_eq(#spr.layers, initialCount, "1 layer cannot be converted to separate smart objects")
  assert_false(spr.layers[1].isSmartObject)

  spr:close()
  print("    -> Passed: Single layer rejects separate smart objects conversion.")
end

-- 4.8 Special characters in layer names during Separate Smart Objects
print("  Test 4.8: Sanitization of special characters in layer names...")
do
  local spr = Sprite(32, 32)
  local l1 = spr.layers[1]
  local l2 = spr:newLayer()
  l2.name = "Layer/With:Bad*Chars?<>|"
  local l3 = spr:newLayer()
  l3.name = "NormalLayer"

  app.range.layers = { l2, l3 }
  local tempSpecialFolder = fixtureDir .. "temp_special_" .. tostring(os.time())

  local ok = pcall(function()
    app.command.ConvertLayer{
      to = "separate_smart_objects",
      folder = tempSpecialFolder,
      ui = false
    }
  end)
  assert_true(ok, "Separate smart objects must succeed despite illegal path characters")

  local sm2 = spr.layers[2]
  local sm3 = spr.layers[3]
  assert_true(sm2.isSmartObject, "Layer with special chars converted to smart object")
  assert_true(sm3.isSmartObject, "Normal layer converted to smart object")

  -- Clean up generated files
  if sm2.smartObjectFilepath then os.remove(sm2.smartObjectFilepath) end
  if sm3.smartObjectFilepath then os.remove(sm3.smartObjectFilepath) end
  os.remove(tempSpecialFolder)

  spr:close()
  print("    -> Passed: Illegal characters in layer names sanitized properly.")
end

-- ==============================================================================
-- SUITE 5: Complex Hierarchy & Nested Groups
-- ==============================================================================
print("\n[SUITE 5] Unpack Smart Object Containing Nested Groups")
do
  local tempNestedPath = fixtureDir .. "temp_nested_src_" .. tostring(os.time()) .. ".aseprite"
  local src = Sprite(32, 32)
  local grp1 = src:newGroup()
  grp1.name = "ParentGroup"
  local childA = src:newLayer()
  childA.name = "ChildA"
  childA.parent = grp1
  local childB = src:newLayer()
  childB.name = "ChildB"
  childB.parent = grp1

  local imgA = Image(32, 32)
  imgA:clear(Color{ r=100, g=150, b=200, a=255 })
  src:newCel(childA, 1, imgA)

  local imgB = Image(32, 32)
  imgB:clear(Color{ r=200, g=150, b=100, a=255 })
  src:newCel(childB, 1, imgB)

  src:saveAs(tempNestedPath)
  src:close()

  -- Place into host and unpack
  local hostSpr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = tempNestedPath,
    x = 8,
    y = 12,
    ui = false
  }
  local smartLay = hostSpr.layers[2]
  app.layer = smartLay

  app.command.ConvertLayer{ to = "group" }

  local outerGrp = app.layer
  assert_true(outerGrp.isGroup, "Root unpacked container is group")
  -- In source, root had Layer 1 + grp1 (ParentGroup)
  local innerGrp = nil
  for _, l in ipairs(outerGrp.layers) do
    if l.name == "ParentGroup" then
      innerGrp = l
      break
    end
  end
  assert_not_nil(innerGrp, "Nested group 'ParentGroup' preserved inside unpacked hierarchy")
  assert_true(innerGrp.isGroup, "Nested entity is group")
  assert_eq(#innerGrp.layers, 2, "Nested group has 2 children")

  local unpackedCelA = innerGrp.layers[1]:cel(1)
  assert_not_nil(unpackedCelA, "Unpacked nested cel exists")
  assert_eq(unpackedCelA.position.x, 8, "Nested cel X inherits smart translation")
  assert_eq(unpackedCelA.position.y, 12, "Nested cel Y inherits smart translation")

  hostSpr:close()
  os.remove(tempNestedPath)
  print("    -> Passed: Nested layer groups preserved with correct translation offsets.")
end

-- ==============================================================================
-- SUITE 6: Advanced Adversarial Structural Stress
-- ==============================================================================
print("\n[SUITE 6] Advanced Structural & Hierarchy Stress")

-- 6.1 Smart Object inside an existing parent group converted to group
print("  Test 6.1: Smart Object inside existing parent group...")
do
  local spr = Sprite(64, 64)
  local baseGroup = spr:newGroup()
  baseGroup.name = "ContainerGroup"

  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "NestedSmart",
    x = 10,
    y = 10,
    ui = false
  }
  local smartLay = spr.layers[#spr.layers]
  smartLay.parent = baseGroup

  assert_eq(smartLay.parent.name, "ContainerGroup", "Smart object moved inside ContainerGroup")
  app.layer = smartLay

  app.command.ConvertLayer{ to = "group" }

  -- The converted group should now be a child inside ContainerGroup
  local unpackedSubGrp = nil
  for _, l in ipairs(baseGroup.layers) do
    if l.name == "NestedSmart" and l.isGroup then
      unpackedSubGrp = l
      break
    end
  end
  assert_not_nil(unpackedSubGrp, "Unpacked group remains inside ContainerGroup")
  assert_eq(unpackedSubGrp.layers[1]:cel(1).position.x, 10)
  assert_eq(unpackedSubGrp.layers[1]:cel(1).position.y, 10)

  spr:close()
  print("    -> Passed: Unpacked group preserves parent group container.")
end

-- 6.2 Round-trip: Regular layers -> Smart Object -> Group -> Smart Object
print("  Test 6.2: Round-trip conversion (Layers -> Smart -> Group -> Smart)...")
do
  local spr = Sprite(32, 32)
  local lA = spr.layers[1]
  lA.name = "LayerA"
  local lB = spr:newLayer()
  lB.name = "LayerB"

  local img = Image(32, 32)
  img:drawPixel(5, 5, Color{ r=50, g=100, b=150, a=255 })
  spr:newCel(lB, 1, img)

  app.range.layers = { lA, lB }
  local rtPath1 = fixtureDir .. "temp_rt1_" .. tostring(os.time()) .. ".aseprite"
  app.command.ConvertLayer{
    to = "smart_object",
    filename = rtPath1,
    ui = false
  }
  assert_eq(#spr.layers, 1)
  assert_true(spr.layers[1].isSmartObject)

  -- Convert to group
  app.layer = spr.layers[1]
  app.command.ConvertLayer{ to = "group" }
  assert_eq(#spr.layers, 1)
  assert_true(spr.layers[1].isGroup)
  assert_eq(#spr.layers[1].layers, 2)

  -- Select child layers and convert back to smart object
  app.range.layers = spr.layers[1].layers
  local rtPath2 = fixtureDir .. "temp_rt2_" .. tostring(os.time()) .. ".aseprite"
  app.command.ConvertLayer{
    to = "smart_object",
    filename = rtPath2,
    ui = false
  }
  assert_true(spr.layers[1].isSmartObject)

  spr:close()
  os.remove(rtPath1)
  os.remove(rtPath2)
  print("    -> Passed: Multi-step round-trip conversion succeeds.")
end

-- 6.3 Smart object with scale & rotation converted to group
print("  Test 6.3: Smart object with 2D transform (scale, rotate) unpacks safely...")
do
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = singleFixture,
    name = "SmartTransformed",
    x = 16,
    y = 16,
    ui = false
  }
  local smartLay = spr.layers[2]
  -- Apply 2.0x scale and 90 deg rotation
  smartLay:setTransform(16, 16, 2.0, 2.0, 90.0, false, false)
  app.layer = smartLay

  local ok = pcall(function()
    app.command.ConvertLayer{ to = "group" }
  end)
  assert_true(ok, "Unpack on transformed smart object must not crash")
  local grp = app.layer
  assert_true(grp.isGroup)
  assert_eq(grp.layers[1]:cel(1).position.x, 16)
  assert_eq(grp.layers[1]:cel(1).position.y, 16)

  spr:close()
  print("    -> Passed: Transformed smart object unpacks cleanly.")
end

-- ==============================================================================
-- SUMMARY
-- ==============================================================================
print("\n======================================================================")
print(string.format("  ADVERSARIAL STRESS TEST COMPLETE: %d / %d Assertions Passed",
  passedAssertions, totalAssertions))
print("======================================================================")

