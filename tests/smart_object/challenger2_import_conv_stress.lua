-- ==============================================================================
-- Test Suite: challenger2_import_conv_stress.lua
-- Empirical & Adversarial Challenger 2 Test Suite for Smart Object Import & Convert-To
--
-- Target Focus Areas:
-- 1. Multi-layer conversion to Single Smart Object with:
--    - Differing opacities (255, 180, 120, etc.)
--    - Differing blend modes (Normal, Multiply, Screen, Darken)
--    - Multiple timeline frames (4 frames)
--    - Sparse cels (some layers missing cels on certain frames)
--    - Exact bit-identical pixel & cel restoration on Undo and Redo
-- 2. Multi-layer conversion to Separate Smart Objects with:
--    - Multiple frames and varying layer properties
--    - Individual on-disk file inspection
--    - Exact bit-identical pixel & cel restoration on Undo and Redo
-- 3. Conversion when Background layer is included:
--    - Case 3A: Single Background layer converted to Smart Object
--    - Case 3B: Background layer + regular layers converted to Single Smart Object
--    - Case 3C: Background layer + regular layers converted to Separate Smart Objects
--    - Verification that Undo restores `isBackground` flag and `sprite.backgroundLayer`
-- 4. Convert to Group (Unpack) preserving opacity and blend modes
-- 5. Scalability & stress: 10 layers batch conversion to Single & Separate Smart Objects
-- ==============================================================================

print("======================================================================")
print("  CHALLENGER 2: SMART OBJECT IMPORT & CONVERT-TO EMPIRICAL STRESS     ")
print("======================================================================")

local function getScriptDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  local dir = path:match("(.*[/\\])") or "./"
  return dir
end

local scriptDir = getScriptDir()
local fixtureDir = scriptDir .. "fixtures/"

local totalTests = 0
local passedTests = 0
local failedTests = 0

local function check(cond, msg)
  totalTests = totalTests + 1
  if not cond then
    failedTests = failedTests + 1
    error("ASSERTION FAILED: " .. (msg or "condition false"), 2)
  end
  passedTests = passedTests + 1
end

local function assert_eq(actual, expected, msg)
  totalTests = totalTests + 1
  if actual ~= expected then
    failedTests = failedTests + 1
    error(string.format("ASSERTION FAILED: %s (expected '%s', got '%s')",
      msg or "mismatch", tostring(expected), tostring(actual)), 2)
  end
  passedTests = passedTests + 1
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

local function assert_nil(val, msg)
  check(val == nil, msg or "expected nil")
end

-- Helper: Snapshot all cels and pixel data from a list of layers across all frames
local function snapshotLayers(layers, numFrames)
  local snap = {}
  for i, layer in ipairs(layers) do
    local layerSnap = {
      name = layer.name,
      opacity = layer.opacity,
      blendMode = layer.blendMode,
      isBackground = layer.isBackground,
      isSmartObject = layer.isSmartObject,
      cels = {}
    }
    for f = 1, numFrames do
      local cel = layer:cel(f)
      if cel then
        local img = cel.image
        local pixels = {}
        for y = 0, img.height - 1 do
          for x = 0, img.width - 1 do
            table.insert(pixels, img:getPixel(x, y))
          end
        end
        layerSnap.cels[f] = {
          x = cel.position.x,
          y = cel.position.y,
          w = img.width,
          h = img.height,
          pixels = pixels
        }
      else
        layerSnap.cels[f] = nil
      end
    end
    snap[i] = layerSnap
  end
  return snap
end

-- Helper: Verify a list of layers against a previous snapshot
local function verifyLayerSnapshot(layers, snap, numFrames, contextMsg)
  assert_eq(#layers, #snap, contextMsg .. ": Layer count mismatch")
  for i, layer in ipairs(layers) do
    local s = snap[i]
    assert_eq(layer.name, s.name, contextMsg .. string.format(": Layer %d name mismatch", i))
    assert_eq(layer.opacity, s.opacity, contextMsg .. string.format(": Layer %d opacity mismatch", i))
    assert_eq(layer.blendMode, s.blendMode, contextMsg .. string.format(": Layer %d blendMode mismatch", i))
    assert_eq(layer.isBackground, s.isBackground, contextMsg .. string.format(": Layer %d isBackground mismatch", i))
    assert_eq(layer.isSmartObject, s.isSmartObject, contextMsg .. string.format(": Layer %d isSmartObject mismatch", i))

    for f = 1, numFrames do
      local cel = layer:cel(f)
      local expCel = s.cels[f]
      if expCel == nil then
        assert_nil(cel, contextMsg .. string.format(": Layer %d Frame %d expected nil cel", i, f))
      else
        assert_not_nil(cel, contextMsg .. string.format(": Layer %d Frame %d missing cel", i, f))
        assert_eq(cel.position.x, expCel.x, contextMsg .. string.format(": Layer %d Frame %d cel X mismatch", i, f))
        assert_eq(cel.position.y, expCel.y, contextMsg .. string.format(": Layer %d Frame %d cel Y mismatch", i, f))
        assert_eq(cel.image.width, expCel.w, contextMsg .. string.format(": Layer %d Frame %d cel width mismatch", i, f))
        assert_eq(cel.image.height, expCel.h, contextMsg .. string.format(": Layer %d Frame %d cel height mismatch", i, f))

        local idx = 1
        for y = 0, expCel.h - 1 do
          for x = 0, expCel.w - 1 do
            local actualPx = cel.image:getPixel(x, y)
            local expectedPx = expCel.pixels[idx]
            if actualPx ~= expectedPx then
              error(string.format("%s: Pixel mismatch at Layer %d ('%s') Frame %d pos (%d,%d): expected %x, got %x",
                contextMsg, i, layer.name, f, x, y, expectedPx, actualPx), 2)
            end
            idx = idx + 1
          end
        end
      end
    end
  end
end

-- ==============================================================================
-- FOCUS AREA 1: Convert Selected Layers to Single Smart Object with Opacity,
-- Blend Modes, Multiple Frames & Sparse Cels + Undo/Redo Fidelity
-- ==============================================================================
print("\n[FOCUS AREA 1] Convert Selected Layers to Single Smart Object (Opacity, BlendModes, Multi-Frame, Sparse Cels)")

do
  local spr = Sprite(40, 40)
  for f = 2, 4 do spr:newEmptyFrame() end
  assert_eq(#spr.frames, 4)

  -- Layer 1: Base (unselected)
  local layBase = spr.layers[1]
  layBase.name = "BaseLayer"
  for f = 1, 4 do
    local img = Image(40, 40)
    img:clear(Color{ r = 30 * f, g = 20, b = 10, a = 255 })
    spr:newCel(layBase, f, img)
  end

  -- Layer 2: Selected 1 (Opacity 180, BlendMode Multiply)
  local layMult = spr:newLayer()
  layMult.name = "LayerMultiply"
  layMult.opacity = 180
  layMult.blendMode = BlendMode.MULTIPLY
  for f = 1, 4 do
    local img = Image(20, 20)
    img:clear(Color{ r = 255, g = 50 * f, b = 0, a = 200 })
    spr:newCel(layMult, f, img, Point(5, 5))
  end

  -- Layer 3: Selected 2 (Opacity 120, BlendMode Screen)
  local layScreen = spr:newLayer()
  layScreen.name = "LayerScreen"
  layScreen.opacity = 120
  layScreen.blendMode = BlendMode.SCREEN
  for f = 1, 4 do
    local img = Image(15, 15)
    img:clear(Color{ r = 0, g = 200, b = 40 * f, a = 255 })
    spr:newCel(layScreen, f, img, Point(15, 10))
  end

  -- Layer 4: Selected 3 (Opacity 220, BlendMode Darken, SPARSE: exists on frames 2 & 4 only)
  local layDark = spr:newLayer()
  layDark.name = "LayerDarkenSparse"
  layDark.opacity = 220
  layDark.blendMode = BlendMode.DARKEN
  for _, f in ipairs({ 2, 4 }) do
    local img = Image(10, 10)
    img:clear(Color{ r = 50, g = 50, b = 255, a = 255 })
    spr:newCel(layDark, f, img, Point(20, 20))
  end

  assert_eq(#spr.layers, 4)
  assert_nil(layDark:cel(1), "Frame 1 must be sparse/nil for layDark")
  assert_nil(layDark:cel(3), "Frame 3 must be sparse/nil for layDark")

  -- Record baseline snapshot of all 4 layers
  local origSnapshot = snapshotLayers(spr.layers, 4)

  -- Select layers 2, 3, 4
  app.range.layers = { layMult, layScreen, layDark }
  local tempSinglePath = fixtureDir .. "temp_challenger2_single_" .. tostring(os.time()) .. ".aseprite"

  print("  -> Executing ConvertLayer to Single Smart Object...")
  app.command.ConvertLayer{
    to = "smart_object",
    filename = tempSinglePath,
    ui = false
  }

  -- Verification: Host sprite
  assert_eq(#spr.layers, 2, "Sprite should now have 2 layers: BaseLayer and SmartObject")
  assert_eq(spr.layers[1].name, "BaseLayer")
  local smartLay = spr.layers[2]
  assert_true(smartLay.isSmartObject, "Replaced layer must be smart object")
  assert_eq(smartLay.smartObjectFilepath, tempSinglePath)

  -- Check cels across timeline
  for f = 1, 4 do
    assert_not_nil(smartLay:cel(f), string.format("Smart object must have rendered cel on frame %d", f))
  end

  -- Verification: Check exported detached document on disk
  print("  -> Verifying exported document on disk...")
  local diskSpr = app.open(tempSinglePath)
  assert_not_nil(diskSpr, "Exported file must open cleanly")
  assert_eq(#diskSpr.layers, 3, "Detached doc must contain exactly 3 layers")
  assert_eq(#diskSpr.frames, 4, "Detached doc must have 4 frames")

  local dL1 = diskSpr.layers[1]
  local dL2 = diskSpr.layers[2]
  local dL3 = diskSpr.layers[3]

  assert_eq(dL1.name, "LayerMultiply")
  assert_eq(dL1.opacity, 180, "LayerMultiply opacity in disk doc")
  assert_eq(dL1.blendMode, BlendMode.MULTIPLY, "LayerMultiply blendMode in disk doc")

  assert_eq(dL2.name, "LayerScreen")
  assert_eq(dL2.opacity, 120, "LayerScreen opacity in disk doc")
  assert_eq(dL2.blendMode, BlendMode.SCREEN, "LayerScreen blendMode in disk doc")

  assert_eq(dL3.name, "LayerDarkenSparse")
  assert_eq(dL3.opacity, 220, "LayerDarkenSparse opacity in disk doc")
  assert_eq(dL3.blendMode, BlendMode.DARKEN, "LayerDarkenSparse blendMode in disk doc")

  -- Check sparsity in disk doc
  assert_nil(dL3:cel(1), "Frame 1 in disk doc must be nil")
  assert_not_nil(dL3:cel(2), "Frame 2 in disk doc must exist")
  assert_nil(dL3:cel(3), "Frame 3 in disk doc must be nil")
  assert_not_nil(dL3:cel(4), "Frame 4 in disk doc must exist")
  diskSpr:close()

  -- Ensure host sprite is active
  app.sprite = spr

  -- Snapshot the converted state for redo comparison
  local postConvertSnapshot = snapshotLayers(spr.layers, 4)

  -- Test UNDO Fidelity
  print("  -> Testing Undo fidelity: restoring 4 original layers...")
  app.undo()
  verifyLayerSnapshot(spr.layers, origSnapshot, 4, "Post-Undo Focus Area 1")
  print("    -> Bit-identical undo verified across all 4 frames, sparse cels, and opacities/blend modes!")

  -- Test REDO Fidelity
  print("  -> Testing Redo fidelity: re-applying smart object...")
  app.redo()
  verifyLayerSnapshot(spr.layers, postConvertSnapshot, 4, "Post-Redo Focus Area 1")
  assert_true(spr.layers[2].isSmartObject)
  print("    -> Redo verified.")

  os.remove(tempSinglePath)
  spr:close()
  print("  [PASS] Focus Area 1 completed successfully.")
end

-- ==============================================================================
-- FOCUS AREA 2: Convert Multiple Layers to Separate Smart Objects with
-- Sparse Cels & Timeline Fidelity + Undo/Redo
-- ==============================================================================
print("\n[FOCUS AREA 2] Convert Multiple Layers to Separate Smart Objects")

do
  local spr = Sprite(32, 32)
  spr:newEmptyFrame()
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 3)

  local lKeep = spr.layers[1]
  lKeep.name = "KeepLayer"

  local lSep1 = spr:newLayer()
  lSep1.name = "CharBody"
  lSep1.opacity = 210
  lSep1.blendMode = BlendMode.NORMAL
  for f = 1, 3 do
    local img = Image(16, 16)
    img:clear(Color{ r = 100, g = 100 + 30 * f, b = 200, a = 255 })
    spr:newCel(lSep1, f, img, Point(2, 4))
  end

  local lSep2 = spr:newLayer()
  lSep2.name = "CharWeapon"
  lSep2.opacity = 160
  lSep2.blendMode = BlendMode.MULTIPLY
  -- Sparse: only on frames 1 and 3
  for _, f in ipairs({ 1, 3 }) do
    local img = Image(8, 20)
    img:clear(Color{ r = 255, g = 200, b = 50 * f, a = 255 })
    spr:newCel(lSep2, f, img, Point(18, 2))
  end

  local origSnapshot = snapshotLayers(spr.layers, 3)

  app.range.layers = { lSep1, lSep2 }
  local tempSepFolder = fixtureDir .. "temp_challenger2_sep_" .. tostring(os.time())

  print("  -> Executing ConvertLayer to Separate Smart Objects...")
  app.command.ConvertLayer{
    to = "separate_smart_objects",
    folder = tempSepFolder,
    ui = false
  }

  assert_eq(#spr.layers, 3, "Sprite must still have 3 layers")
  assert_false(spr.layers[1].isSmartObject, "KeepLayer remains regular")
  assert_true(spr.layers[2].isSmartObject, "CharBody must be smart object")
  assert_true(spr.layers[3].isSmartObject, "CharWeapon must be smart object")
  assert_eq(spr.layers[2].name, "CharBody")
  assert_eq(spr.layers[3].name, "CharWeapon")

  local file1 = spr.layers[2].smartObjectFilepath
  local file2 = spr.layers[3].smartObjectFilepath
  assert_not_nil(file1, "CharBody smartObjectFilepath must not be nil")
  assert_not_nil(file2, "CharWeapon smartObjectFilepath must not be nil")

  -- Check individual files on disk
  local diskSpr1 = app.open(file1)
  assert_not_nil(diskSpr1, "File 1 must open")
  assert_eq(#diskSpr1.layers, 1)
  assert_eq(diskSpr1.layers[1].opacity, 210)
  assert_eq(diskSpr1.layers[1].blendMode, BlendMode.NORMAL)
  diskSpr1:close()

  local diskSpr2 = app.open(file2)
  assert_not_nil(diskSpr2, "File 2 must open")
  assert_eq(#diskSpr2.layers, 1)
  assert_eq(diskSpr2.layers[1].opacity, 160)
  assert_eq(diskSpr2.layers[1].blendMode, BlendMode.MULTIPLY)
  assert_not_nil(diskSpr2.layers[1]:cel(1))
  assert_nil(diskSpr2.layers[1]:cel(2), "Sparse frame 2 must be nil in CharWeapon file")
  assert_not_nil(diskSpr2.layers[1]:cel(3))
  diskSpr2:close()

  app.sprite = spr

  local postSepSnapshot = snapshotLayers(spr.layers, 3)

  -- Test UNDO
  print("  -> Testing Undo fidelity...")
  app.undo()
  verifyLayerSnapshot(spr.layers, origSnapshot, 3, "Post-Undo Focus Area 2")
  print("    -> Undo accurately restored original regular layers and sparse cels!")

  -- Test REDO
  print("  -> Testing Redo fidelity...")
  app.redo()
  verifyLayerSnapshot(spr.layers, postSepSnapshot, 3, "Post-Redo Focus Area 2")
  assert_true(spr.layers[2].isSmartObject)
  assert_true(spr.layers[3].isSmartObject)
  print("    -> Redo verified.")

  os.remove(file1)
  os.remove(file2)
  os.remove(tempSepFolder)
  spr:close()
  print("  [PASS] Focus Area 2 completed successfully.")
end

-- ==============================================================================
-- FOCUS AREA 3: Converting Layers When Background Layer is Included
-- ==============================================================================
print("\n[FOCUS AREA 3] Converting Layers When Background Layer is Included")

-- 3A: Single Background Layer converted to Smart Object
print("  Test 3A: Convert Single Background Layer to Smart Object...")
do
  local spr = Sprite(32, 32)
  -- By default, new Sprite with 1 layer creates a Background layer if RGBA, let's verify
  local bg = spr.layers[1]
  -- Ensure it is configured as Background
  if not bg.isBackground then
    app.command.ConvertLayer{ to = "background" }
  end
  assert_true(bg.isBackground, "Layer 1 must be Background layer")
  assert_not_nil(spr.backgroundLayer, "Sprite has backgroundLayer")

  -- Draw distinct pixel pattern on background
  local img = Image(32, 32)
  img:clear(Color{ r = 210, g = 180, b = 140, a = 255 })
  for x = 0, 31 do
    img:drawPixel(x, x, Color{ r = 0, g = 0, b = 255, a = 255 })
  end
  spr:newCel(bg, 1, img)

  local bgSnapshot = snapshotLayers(spr.layers, 1)
  app.range.layers = { bg }
  local tempBgPath = fixtureDir .. "temp_challenger2_bg_" .. tostring(os.time()) .. ".aseprite"

  app.command.ConvertLayer{
    to = "smart_object",
    filename = tempBgPath,
    ui = false
  }

  assert_eq(#spr.layers, 1, "Sprite must still have 1 layer")
  assert_true(spr.layers[1].isSmartObject, "Layer must now be Smart Object")
  assert_false(spr.layers[1].isBackground, "Smart Object is not a background layer")
  assert_nil(spr.backgroundLayer, "Sprite should no longer have a background layer")

  -- Check Undo: Does it restore the Background layer flag and pointer?
  print("    -> Testing Undo on converted Background layer...")
  app.undo()
  assert_eq(#spr.layers, 1)
  assert_false(spr.layers[1].isSmartObject)
  assert_true(spr.layers[1].isBackground, "Undo must restore isBackground == true")
  assert_not_nil(spr.backgroundLayer, "Undo must restore sprite.backgroundLayer")
  assert_eq(spr.backgroundLayer.name, spr.layers[1].name)
  verifyLayerSnapshot(spr.layers, bgSnapshot, 1, "Undo Single Background Conversion")

  -- Check Redo
  print("    -> Testing Redo...")
  app.redo()
  assert_true(spr.layers[1].isSmartObject)
  assert_nil(spr.backgroundLayer)

  os.remove(tempBgPath)
  spr:close()
  print("    -> Passed: Single Background layer converts and undoes restoring background identity perfectly.")
end

-- 3B: Background Layer + Regular Layers converted to Single Smart Object
print("  Test 3B: Convert Background Layer + Regular Layers to Single Smart Object...")
do
  local spr = Sprite(32, 32)
  spr:newEmptyFrame()
  assert_eq(#spr.frames, 2)

  local bg = spr.layers[1]
  if not bg.isBackground then
    app.command.ConvertLayer{ to = "background" }
  end
  assert_true(bg.isBackground)

  local fg1 = spr:newLayer()
  fg1.name = "FG1_OverBg"
  fg1.opacity = 200

  local fg2 = spr:newLayer()
  fg2.name = "FG2_OverBg"
  fg2.blendMode = BlendMode.MULTIPLY

  -- Populate cels
  for f = 1, 2 do
    local imgBg = Image(32, 32)
    imgBg:clear(Color{ r = 100 * f, g = 50, b = 25, a = 255 })
    spr:newCel(bg, f, imgBg)

    local imgFg1 = Image(16, 16)
    imgFg1:clear(Color{ r = 255, g = 255, b = 0, a = 255 })
    spr:newCel(fg1, f, imgFg1, Point(0, 0))

    local imgFg2 = Image(16, 16)
    imgFg2:clear(Color{ r = 0, g = 255, b = 255, a = 180 })
    spr:newCel(fg2, f, imgFg2, Point(16, 16))
  end

  local allSnapshot = snapshotLayers(spr.layers, 2)
  assert_eq(#spr.layers, 3)

  -- Select ALL 3 layers including Background
  app.range.layers = { bg, fg1, fg2 }
  local tempBgMultiPath = fixtureDir .. "temp_challenger2_bg_multi_" .. tostring(os.time()) .. ".aseprite"

  app.command.ConvertLayer{
    to = "smart_object",
    filename = tempBgMultiPath,
    ui = false
  }

  assert_eq(#spr.layers, 1, "3 layers reduced to 1 Smart Object")
  assert_true(spr.layers[1].isSmartObject)
  assert_nil(spr.backgroundLayer, "Host sprite has no background layer after conversion")

  -- Check exported document on disk
  local diskSpr = app.open(tempBgMultiPath)
  assert_not_nil(diskSpr)
  assert_eq(#diskSpr.layers, 3, "Exported file has all 3 layers")
  diskSpr:close()

  app.sprite = spr

  -- Test UNDO
  print("    -> Testing Undo on Background + Multiple Layers...")
  app.undo()
  assert_eq(#spr.layers, 3)
  assert_true(spr.layers[1].isBackground, "Layer 1 restored as Background")
  assert_false(spr.layers[2].isBackground, "Layer 2 is regular")
  assert_false(spr.layers[3].isBackground, "Layer 3 is regular")
  assert_not_nil(spr.backgroundLayer, "Sprite backgroundLayer restored")
  verifyLayerSnapshot(spr.layers, allSnapshot, 2, "Undo Background + Multi Layers")

  -- Test REDO
  print("    -> Testing Redo...")
  app.redo()
  assert_eq(#spr.layers, 1)
  assert_true(spr.layers[1].isSmartObject)
  assert_nil(spr.backgroundLayer)

  os.remove(tempBgMultiPath)
  spr:close()
  print("    -> Passed: Multi-layer with Background converts and undoes with exact background fidelity.")
end

-- 3C: Background Layer + Regular Layers converted to Separate Smart Objects
print("  Test 3C: Convert Background Layer + Regular Layers to Separate Smart Objects...")
do
  local spr = Sprite(32, 32)
  local bg = spr.layers[1]
  if not bg.isBackground then
    app.command.ConvertLayer{ to = "background" }
  end
  assert_true(bg.isBackground)

  local fg = spr:newLayer()
  fg.name = "MyFG"
  fg.opacity = 175

  local bgFgSnapshot = snapshotLayers(spr.layers, 1)

  app.range.layers = { bg, fg }
  local tempSepBgFolder = fixtureDir .. "temp_challenger2_sep_bg_" .. tostring(os.time())

  app.command.ConvertLayer{
    to = "separate_smart_objects",
    folder = tempSepBgFolder,
    ui = false
  }

  assert_eq(#spr.layers, 2)
  assert_true(spr.layers[1].isSmartObject)
  assert_true(spr.layers[2].isSmartObject)
  assert_nil(spr.backgroundLayer)

  local bgFile = spr.layers[1].smartObjectFilepath
  local fgFile = spr.layers[2].smartObjectFilepath

  -- Test UNDO
  print("    -> Testing Undo on Separate Smart Objects with Background...")
  app.undo()
  assert_eq(#spr.layers, 2)
  assert_true(spr.layers[1].isBackground, "Layer 1 restored as Background")
  assert_false(spr.layers[2].isBackground, "Layer 2 restored as normal")
  assert_not_nil(spr.backgroundLayer, "Background pointer restored")
  verifyLayerSnapshot(spr.layers, bgFgSnapshot, 1, "Undo Separate Smart Objects with Background")

  -- Test REDO
  print("    -> Testing Redo...")
  app.redo()
  assert_true(spr.layers[1].isSmartObject)
  assert_true(spr.layers[2].isSmartObject)

  os.remove(bgFile)
  os.remove(fgFile)
  os.remove(tempSepBgFolder)
  spr:close()
  print("    -> Passed: Separate Smart Objects with Background converts and undoes cleanly.")
end

-- ==============================================================================
-- FOCUS AREA 4: Convert to Group (Unpacking) Preserving Opacities & Blend Modes
-- ==============================================================================
print("\n[FOCUS AREA 4] Unpack Smart Object Containing Mixed Opacity and Blend Modes")
do
  -- Create source document with 3 layers of varying blend modes and opacities
  local tempSrcPath = fixtureDir .. "temp_challenger2_mixed_src_" .. tostring(os.time()) .. ".aseprite"
  local srcSpr = Sprite(32, 32)
  local lA = srcSpr.layers[1]
  lA.name = "SrcNormal"
  lA.opacity = 255

  local lB = srcSpr:newLayer()
  lB.name = "SrcMult"
  lB.opacity = 180
  lB.blendMode = BlendMode.MULTIPLY

  local lC = srcSpr:newLayer()
  lC.name = "SrcScreen"
  lC.opacity = 140
  lC.blendMode = BlendMode.SCREEN

  for _, l in ipairs({ lA, lB, lC }) do
    local img = Image(32, 32)
    img:clear(Color{ r = 10, g = 50, b = 100, a = 200 })
    srcSpr:newCel(l, 1, img)
  end
  srcSpr:saveAs(tempSrcPath)
  srcSpr:close()

  -- Place into host sprite
  local hostSpr = Sprite(64, 64)
  app.command.PlaceSmartObject{
    filename = tempSrcPath,
    name = "PlacedMixed",
    x = 10,
    y = 12,
    ui = false
  }

  local smartLay = hostSpr.layers[2]
  assert_true(smartLay.isSmartObject)
  app.layer = smartLay

  -- Unpack via ConvertLayer { to = "group" }
  print("  -> Unpacking smart object to group...")
  app.command.ConvertLayer{ to = "group" }

  local grp = app.layer
  assert_true(grp.isGroup)
  assert_eq(#grp.layers, 3, "Unpacked group has 3 children")

  local u1 = grp.layers[1]
  local u2 = grp.layers[2]
  local u3 = grp.layers[3]

  assert_eq(u1.name, "SrcNormal")
  assert_eq(u1.opacity, 255)

  assert_eq(u2.name, "SrcMult")
  assert_eq(u2.opacity, 180, "Unpacked child preserves opacity")
  assert_eq(u2.blendMode, BlendMode.MULTIPLY, "Unpacked child preserves Multiply blendMode")

  assert_eq(u3.name, "SrcScreen")
  assert_eq(u3.opacity, 140, "Unpacked child preserves opacity")
  assert_eq(u3.blendMode, BlendMode.SCREEN, "Unpacked child preserves Screen blendMode")

  -- Check cel positions inherited translation (10, 12)
  for _, u in ipairs({ u1, u2, u3 }) do
    assert_eq(u:cel(1).position.x, 10, "Inherited translation X")
    assert_eq(u:cel(1).position.y, 12, "Inherited translation Y")
  end

  -- Undo and Redo cycles
  print("  -> Testing Undo / Redo on unpack...")
  app.undo()
  assert_true(hostSpr.layers[2].isSmartObject)
  app.redo()
  assert_true(hostSpr.layers[2].isGroup)

  hostSpr:close()
  os.remove(tempSrcPath)
  print("  [PASS] Focus Area 4 completed successfully.")
end

-- ==============================================================================
-- FOCUS AREA 5: Scalability & Stress (10 Layers Batch Conversion)
-- ==============================================================================
print("\n[FOCUS AREA 5] Scalability & Stress: 10 Layers Batch Conversion")

-- 5A: 10 Layers to Single Smart Object
print("  Test 5A: 10 Layers converted to Single Smart Object...")
do
  local spr = Sprite(32, 32)
  local layers = { spr.layers[1] }
  layers[1].name = "L1"
  for i = 2, 10 do
    local l = spr:newLayer()
    l.name = "L" .. i
    l.opacity = 100 + i * 15
    table.insert(layers, l)
  end
  assert_eq(#spr.layers, 10)

  for i, l in ipairs(layers) do
    local img = Image(16, 16)
    img:clear(Color{ r = i * 20, g = 50, b = 100, a = 255 })
    spr:newCel(l, 1, img, Point(i, i))
  end

  local snap10 = snapshotLayers(spr.layers, 1)

  app.range.layers = layers
  local temp10Path = fixtureDir .. "temp_challenger2_10layers_" .. tostring(os.time()) .. ".aseprite"

  app.command.ConvertLayer{
    to = "smart_object",
    filename = temp10Path,
    ui = false
  }

  assert_eq(#spr.layers, 1)
  assert_true(spr.layers[1].isSmartObject)

  -- Check file on disk has 10 layers
  local diskSpr = app.open(temp10Path)
  assert_eq(#diskSpr.layers, 10)
  diskSpr:close()

  app.sprite = spr

  -- Undo
  app.undo()
  assert_eq(#spr.layers, 10)
  verifyLayerSnapshot(spr.layers, snap10, 1, "Undo 10 layers single conversion")

  -- Redo
  app.redo()
  assert_eq(#spr.layers, 1)
  assert_true(spr.layers[1].isSmartObject)

  os.remove(temp10Path)
  spr:close()
  print("    -> Passed: 10 layers converted to single smart object, verified, and undone.")
end

-- 5B: 10 Layers to Separate Smart Objects
print("  Test 5B: 10 Layers converted to Separate Smart Objects...")
do
  local spr = Sprite(32, 32)
  local layers = { spr.layers[1] }
  layers[1].name = "Sep1"
  for i = 2, 10 do
    local l = spr:newLayer()
    l.name = "Sep" .. i
    table.insert(layers, l)
  end
  assert_eq(#spr.layers, 10)

  for i, l in ipairs(layers) do
    local img = Image(16, 16)
    img:clear(Color{ r = 10, g = i * 20, b = 150, a = 255 })
    spr:newCel(l, 1, img, Point(i, i))
  end

  local snapSep10 = snapshotLayers(spr.layers, 1)

  app.range.layers = layers
  local tempSep10Folder = fixtureDir .. "temp_challenger2_sep10_" .. tostring(os.time())

  app.command.ConvertLayer{
    to = "separate_smart_objects",
    folder = tempSep10Folder,
    ui = false
  }

  assert_eq(#spr.layers, 10)
  local filepaths = {}
  for i = 1, 10 do
    assert_true(spr.layers[i].isSmartObject, "Layer " .. i .. " must be smart object")
    table.insert(filepaths, spr.layers[i].smartObjectFilepath)
  end

  -- Undo
  app.undo()
  assert_eq(#spr.layers, 10)
  for i = 1, 10 do
    assert_false(spr.layers[i].isSmartObject)
  end
  verifyLayerSnapshot(spr.layers, snapSep10, 1, "Undo 10 layers separate conversion")

  -- Redo
  app.redo()
  assert_eq(#spr.layers, 10)
  for i = 1, 10 do
    assert_true(spr.layers[i].isSmartObject)
  end

  for _, fp in ipairs(filepaths) do
    if fp then os.remove(fp) end
  end
  os.remove(tempSep10Folder)
  spr:close()
  print("    -> Passed: 10 layers converted to 10 separate smart objects, verified, and undone.")
end

-- ==============================================================================
-- SUMMARY
-- ==============================================================================
print("\n======================================================================")
print(string.format("  CHALLENGER 2 TESTS COMPLETE: %d Passed, %d Failed (Total %d)",
  passedTests, failedTests, totalTests))
print("======================================================================")

if failedTests > 0 then
  error(string.format("FAILURE: %d test assertions failed!", failedTests))
end
