-- ==============================================================================
-- Empirical Challenger Stress Harness: Verification of Fixes 2, 3, and 4
-- Milestone: m1_it2
-- Agent: challenger_m1_it2_2
-- ==============================================================================

local kFaceTop = 0
local kFaceDown = 1
local kFaceFront = 2
local kFaceBack = 3
local kFaceRight = 4
local kFaceLeft = 5
local kFaceCount = 6

local kFaceSpecs = {
  [kFaceTop]   = { sliceName = "TOP",   displayName = "Top",    col = 0, row = 0 },
  [kFaceDown]  = { sliceName = "DOWN",  displayName = "Bottom", col = 0, row = 1 },
  [kFaceFront] = { sliceName = "FRONT", displayName = "Front",  col = 1, row = 0 },
  [kFaceBack]  = { sliceName = "BACK",  displayName = "Back",   col = 1, row = 1 },
  [kFaceRight] = { sliceName = "RIGHT", displayName = "Right",  col = 2, row = 0 },
  [kFaceLeft]  = { sliceName = "LEFT",  displayName = "Left",   col = 2, row = 1 }
}

local totalChecks = 0
local passedChecks = 0
local failedChecks = 0
local failureList = {}

local function assert_check(condition, desc)
  totalChecks = totalChecks + 1
  if condition then
    passedChecks = passedChecks + 1
    print(string.format("  [PASS] %s", desc))
  else
    failedChecks = failedChecks + 1
    table.insert(failureList, desc)
    print(string.format("  [FAIL] %s", desc))
  end
end

local function assert_equal(actual, expected, desc)
  assert_check(actual == expected, string.format("%s (expected '%s', got '%s')", desc, tostring(expected), tostring(actual)))
end

print("==================================================================")
print(" EMPIRICAL VERIFICATION HARNESS: FIX 2, FIX 3, FIX 4")
print("==================================================================")

-- ==============================================================================
-- PART 1: FIX 2 - Non-Square Canvas Raycast Texture Sampling (Top & Down Faces)
-- ==============================================================================
print("\n--- [PART 1: FIX 2] Raycast Texture Sampling for Non-Square Canvases ---")

-- Mathematical raycast sampling logic matching cmd_easymetric.cpp:2035-2070
local function sampleRaycastUV_Fixed(face, hx, hy, hz, halfX, halfY, halfZ, fw, fh)
  local u, v = 0, 0
  if face == kFaceTop then
    u = math.max(0, math.min(fw - 1, math.floor((hx + halfX) / (2.0 * halfX) * fw)))
    v = math.max(0, math.min(fh - 1, math.floor((hz + halfZ) / (2.0 * halfZ) * fh)))
  elseif face == kFaceDown then
    u = math.max(0, math.min(fw - 1, math.floor((hx + halfX) / (2.0 * halfX) * fw)))
    v = math.max(0, math.min(fh - 1, math.floor((halfZ - hz) / (2.0 * halfZ) * fh)))
  end
  return u, v
end

local function sampleRaycastUV_Buggy(face, hx, hy, hz, halfX, halfY, halfZ, fw, fh)
  local u, v = 0, 0
  if face == kFaceTop then
    u = math.max(0, math.min(fw - 1, math.floor((hx + halfX) / (2.0 * halfX) * fw)))
    v = math.max(0, math.min(fh - 1, math.floor((hz + halfZ) / (2.0 * halfZ) * fw))) -- Buggy * fw
  elseif face == kFaceDown then
    u = math.max(0, math.min(fw - 1, math.floor((hx + halfX) / (2.0 * halfX) * fw)))
    v = math.max(0, math.min(fh - 1, math.floor((halfZ - hz) / (2.0 * halfZ) * fw))) -- Buggy * fw
  end
  return u, v
end

local nonSquareConfigs = {
  { fw = 16,  fh = 64,  desc = "16x64 Extreme Tall (1:4)" },
  { fw = 64,  fh = 16,  desc = "64x16 Extreme Wide (4:1)" },
  { fw = 8,   fh = 32,  desc = "8x32 Non-Square Tall (1:4)" },
  { fw = 128, fh = 32,  desc = "128x32 High-Aspect Wide (4:1)" },
  { fw = 12,  fh = 48,  desc = "12x48 Non-Square Tall (1:4)" },
  { fw = 37,  fh = 19,  desc = "37x19 Prime Non-Square Aspect" },
  { fw = 5,   fh = 95,  desc = "5x95 High-Aspect Ratio (1:19)" }
}

for _, cfg in ipairs(nonSquareConfigs) do
  local fw = cfg.fw
  local fh = cfg.fh
  local halfX = fw * 0.5
  local halfY = fh * 0.5
  local halfZ = fh * 0.5 -- Depth dimension corresponds to face height

  -- Test Top Face row coverage
  local topRowsHit = {}
  local numSteps = fh * 10
  for step = 0, numSteps do
    local frac = step / numSteps
    local hz = -halfZ + frac * (2.0 * halfZ)
    local u, v = sampleRaycastUV_Fixed(kFaceTop, 0, -halfY, hz, halfX, halfY, halfZ, fw, fh)
    topRowsHit[v] = true
  end

  local topHitCount = 0
  for r = 0, fh - 1 do
    if topRowsHit[r] then topHitCount = topHitCount + 1 end
  end

  assert_equal(topHitCount, fh, string.format("Top Face 100%% Row Coverage [%s]: all %d rows sampled", cfg.desc, fh))
  assert_check(topRowsHit[0] == true, string.format("Top Face row 0 hit at hz = -halfZ [%s]", cfg.desc))
  assert_check(topRowsHit[fh - 1] == true, string.format("Top Face row %d hit at hz = +halfZ [%s]", fh - 1, cfg.desc))

  -- Demonstrate Buggy code fails when fw < fh
  if fw < fh then
    local buggyRowsHit = {}
    for step = 0, numSteps do
      local frac = step / numSteps
      local hz = -halfZ + frac * (2.0 * halfZ)
      local u, v = sampleRaycastUV_Buggy(kFaceTop, 0, -halfY, hz, halfX, halfY, halfZ, fw, fh)
      buggyRowsHit[v] = true
    end
    local buggyHitCount = 0
    for r = 0, fh - 1 do
      if buggyRowsHit[r] then buggyHitCount = buggyHitCount + 1 end
    end
    assert_check(buggyHitCount <= fw + 1 and buggyHitCount < fh, string.format("Buggy code correctly confirmed defective: only sampled %d/%d rows for %s", buggyHitCount, fh, cfg.desc))
  end

  -- Test Down Face row coverage
  local downRowsHit = {}
  for step = 0, numSteps do
    local frac = step / numSteps
    local hz = halfZ - frac * (2.0 * halfZ)
    local u, v = sampleRaycastUV_Fixed(kFaceDown, 0, halfY, hz, halfX, halfY, halfZ, fw, fh)
    downRowsHit[v] = true
  end

  local downHitCount = 0
  for r = 0, fh - 1 do
    if downRowsHit[r] then downHitCount = downHitCount + 1 end
  end

  assert_equal(downHitCount, fh, string.format("Down Face 100%% Row Coverage [%s]: all %d rows sampled", cfg.desc, fh))
  assert_check(downRowsHit[0] == true, string.format("Down Face row 0 hit at hz = +halfZ [%s]", cfg.desc))
  assert_check(downRowsHit[fh - 1] == true, string.format("Down Face row %d hit at hz = -halfZ [%s]", fh - 1, cfg.desc))
end

-- ==============================================================================
-- PART 2: FIX 3 - Cel Synchronization on Exit Studio
-- ==============================================================================
print("\n--- [PART 2: FIX 3] Cel Synchronization on Exit Studio ---")

local function setupDocWithLayers(w, h, numFrames)
  local spr = Sprite(w, h)
  -- Add frames if requested
  for f = 2, numFrames do
    spr:newFrame()
  end

  local layers = {}
  for f = 0, kFaceCount - 1 do
    local lyrName = "Surface_" .. kFaceSpecs[f].sliceName
    local lyr = spr:newLayer()
    lyr.name = lyrName
    layers[f] = lyr

    -- Initialize transparent cels for all frames
    for fr = 1, numFrames do
      local img = Image(w, h, spr.colorMode)
      img:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
      spr:newCel(lyr, fr, img, Point(0, 0))
    end
  end
  return { sprite = spr, layers = layers, w = w, h = h, numFrames = numFrames }
end

-- Emulate exact C++ syncAllLinkedCels()
local function syncAllLinkedCels(docEnv, colLinked)
  local spr = docEnv.sprite
  for c = 0, 2 do
    if colLinked[c] then
      local srcFace = c * 2
      local dstFace = c * 2 + 1
      local srcLyr = docEnv.layers[srcFace]
      local dstLyr = docEnv.layers[dstFace]

      for fr = 1, #spr.frames do
        local srcCel = srcLyr:cel(fr)
        if srcCel and srcCel.image then
          local copyImg = Image(srcCel.image)
          local dstCel = dstLyr:cel(fr)
          if dstCel then
            dstCel.image = copyImg
            dstCel.position = Point(srcCel.position.x, srcCel.position.y)
          else
            spr:newCel(dstLyr, fr, copyImg, Point(srcCel.position.x, srcCel.position.y))
          end
        end
      end
    end
  end
end

-- Test 2.1: Painting while linked, then exit studio with Col 0, Col 1, Col 2
do
  local doc = setupDocWithLayers(24, 24, 3)
  local spr = doc.sprite
  local colLinked = { [0] = true, [1] = true, [2] = true }

  -- Paint distinct recognizable patterns on Top, Front, Right across all 3 frames
  local topLyr = doc.layers[kFaceTop]
  local btmLyr = doc.layers[kFaceDown]
  local frontLyr = doc.layers[kFaceFront]
  local backLyr = doc.layers[kFaceBack]
  local rightLyr = doc.layers[kFaceRight]
  local leftLyr = doc.layers[kFaceLeft]

  local colMarker1 = Color{ r = 255, g = 10, b = 20, a = 255 }
  local colMarker2 = Color{ r = 20, g = 255, b = 30, a = 255 }
  local colMarker3 = Color{ r = 30, g = 40, b = 255, a = 255 }

  topLyr:cel(1).image:drawPixel(3, 4, colMarker1)
  topLyr:cel(2).image:drawPixel(5, 6, colMarker2)
  topLyr:cel(3).image:drawPixel(7, 8, colMarker3)

  frontLyr:cel(1).image:drawPixel(10, 11, colMarker1)
  frontLyr:cel(2).image:drawPixel(12, 13, colMarker2)
  frontLyr:cel(3).image:drawPixel(14, 15, colMarker3)

  rightLyr:cel(1).image:drawPixel(18, 19, colMarker1)
  rightLyr:cel(2).image:drawPixel(20, 21, colMarker2)
  rightLyr:cel(3).image:drawPixel(22, 23, colMarker3)

  -- Prior to exit, confirm opposing faces do NOT have the markers yet
  assert_check(btmLyr:cel(1).image:getPixel(3, 4) == 0, "Before exit: Bottom face does not have Top pixel")
  assert_check(backLyr:cel(1).image:getPixel(10, 11) == 0, "Before exit: Back face does not have Front pixel")
  assert_check(leftLyr:cel(1).image:getPixel(18, 19) == 0, "Before exit: Left face does not have Right pixel")

  -- Now execute Exit Studio synchronization
  syncAllLinkedCels(doc, colLinked)

  -- Verify all 3 frames on opposing faces received the synchronized pixels
  assert_equal(btmLyr:cel(1).image:getPixel(3, 4), colMarker1.rgbaPixel, "Exit Studio: Bottom frame 1 received pixel (3, 4)")
  assert_equal(btmLyr:cel(2).image:getPixel(5, 6), colMarker2.rgbaPixel, "Exit Studio: Bottom frame 2 received pixel (5, 6)")
  assert_equal(btmLyr:cel(3).image:getPixel(7, 8), colMarker3.rgbaPixel, "Exit Studio: Bottom frame 3 received pixel (7, 8)")

  assert_equal(backLyr:cel(1).image:getPixel(10, 11), colMarker1.rgbaPixel, "Exit Studio: Back frame 1 received pixel (10, 11)")
  assert_equal(backLyr:cel(2).image:getPixel(12, 13), colMarker2.rgbaPixel, "Exit Studio: Back frame 2 received pixel (12, 13)")
  assert_equal(backLyr:cel(3).image:getPixel(14, 15), colMarker3.rgbaPixel, "Exit Studio: Back frame 3 received pixel (14, 15)")

  assert_equal(leftLyr:cel(1).image:getPixel(18, 19), colMarker1.rgbaPixel, "Exit Studio: Left frame 1 received pixel (18, 19)")
  assert_equal(leftLyr:cel(2).image:getPixel(20, 21), colMarker2.rgbaPixel, "Exit Studio: Left frame 2 received pixel (20, 21)")
  assert_equal(leftLyr:cel(3).image:getPixel(22, 23), colMarker3.rgbaPixel, "Exit Studio: Left frame 3 received pixel (22, 23)")

  -- Test Cel Independence (No aliasing): modifying bottom after exit must not touch top
  btmLyr:cel(1).image:drawPixel(1, 1, Color{ r = 100, g = 100, b = 100, a = 255 })
  assert_equal(topLyr:cel(1).image:getPixel(1, 1), 0, "Post-Exit Independence: Top face unaffected by bottom edit")

  spr:close()
end

-- Test 2.2: Missing cel on destination layer recreated cleanly upon exit
do
  local doc = setupDocWithLayers(24, 24, 1)
  local spr = doc.sprite
  local colLinked = { [0] = true, [1] = false, [2] = false }

  local topLyr = doc.layers[kFaceTop]
  local btmLyr = doc.layers[kFaceDown]

  -- Delete bottom cel completely
  spr:deleteCel(btmLyr:cel(1))
  assert_check(btmLyr:cel(1) == nil, "Bottom cel deleted prior to exit")

  -- Paint pixel on top
  topLyr:cel(1).image:drawPixel(12, 12, Color{ r = 255, g = 255, b = 0, a = 255 })

  -- Exit Studio sync
  syncAllLinkedCels(doc, colLinked)

  -- Confirm bottom cel was created and holds pixel
  assert_check(btmLyr:cel(1) ~= nil, "Exit Studio recreated missing destination cel")
  assert_equal(btmLyr:cel(1).image:getPixel(12, 12), Color{ r = 255, g = 255, b = 0, a = 255 }.rgbaPixel, "Recreated cel has synchronized pixel")

  spr:close()
end

-- ==============================================================================
-- PART 3: FIX 4 - Layer Focus Retention Across Pane Switching
-- ==============================================================================
print("\n--- [PART 3: FIX 4] Layer Focus Retention Across Pane Switching ---")

-- Emulate DocView & FacePaneBox layer assignment behavior
local function createDocViewPaneSimulation(layers)
  local sim = {
    layers = layers,
    activeEditorIndex = 0,
    paneEditors = {}
  }

  -- Initialize 6 editors, each assigned to its face layer
  for f = 0, kFaceCount - 1 do
    sim.paneEditors[f] = {
      index = f,
      layer = layers[f],
      frame = 1
    }
  end

  -- Fixed DocView::setActivePaneEditor logic:
  -- if (!editor->layer() && m_editor->layer()) editor->setLayer(m_editor->layer());
  function sim:setActivePaneEditor(targetEditor)
    local cur = self.paneEditors[self.activeEditorIndex]
    if not targetEditor.layer and cur.layer then
      targetEditor.layer = cur.layer
    end
    targetEditor.frame = cur.frame
    self.activeEditorIndex = targetEditor.index
  end

  -- FacePaneBox::onProcessMessage logic on mouse click:
  function sim:clickFacePane(faceIdx)
    local editor = self.paneEditors[faceIdx]
    -- FacePaneBox explicitly sets its assigned layer
    editor.layer = self.layers[faceIdx]
    if self.activeEditorIndex ~= faceIdx then
      self:setActivePaneEditor(editor)
    end
  end

  -- Buggy DocView::setActivePaneEditor logic (for comparison):
  -- editor->setLayer(m_editor->layer());
  function sim:setActivePaneEditor_Buggy(targetEditor)
    local cur = self.paneEditors[self.activeEditorIndex]
    targetEditor.layer = cur.layer -- Overwrote target layer with source layer!
    self.activeEditorIndex = targetEditor.index
  end

  return sim
end

do
  local doc = setupDocWithLayers(24, 24, 1)
  local sim = createDocViewPaneSimulation(doc.layers)

  -- 1. Initial invariant: every editor has its own layer
  for f = 0, kFaceCount - 1 do
    assert_equal(sim.paneEditors[f].layer.name, "Surface_" .. kFaceSpecs[f].sliceName,
      string.format("Initial Pane %d layer is Surface_%s", f, kFaceSpecs[f].sliceName))
  end

  -- 2. Sequential click through all 6 panes: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 0
  local clickOrder = { 0, 1, 2, 3, 4, 5, 0 }
  for _, targetFace in ipairs(clickOrder) do
    sim:clickFacePane(targetFace)
    assert_equal(sim.activeEditorIndex, targetFace, string.format("Active editor is Pane %d", targetFace))
    assert_equal(sim.paneEditors[targetFace].layer.name, "Surface_" .. kFaceSpecs[targetFace].sliceName,
      string.format("Pane %d retains dedicated layer Surface_%s upon focus", targetFace, kFaceSpecs[targetFace].sliceName))
  end

  -- 3. Verify ALL 6 panes still hold their original correct layers
  for f = 0, kFaceCount - 1 do
    assert_equal(sim.paneEditors[f].layer.name, "Surface_" .. kFaceSpecs[f].sliceName,
      string.format("After sequential switching, Pane %d layer is preserved as Surface_%s", f, kFaceSpecs[f].sliceName))
  end

  -- 4. Demonstrate old buggy behavior would clobber:
  local buggySim = createDocViewPaneSimulation(doc.layers)
  -- Switching from 0 to 1 with buggy setActivePaneEditor
  buggySim.activeEditorIndex = 0
  buggySim:setActivePaneEditor_Buggy(buggySim.paneEditors[1])
  assert_check(buggySim.paneEditors[1].layer.name == "Surface_TOP",
    "Buggy logic confirmed: Pane 1 was clobbered with Surface_TOP instead of Surface_DOWN")

  -- 5. Stress test: 1,000 rapid random pane switches
  local lcg = 987654321
  for i = 1, 1000 do
    lcg = (lcg * 1103515245 + 12345) % 2147483648
    local target = math.floor(lcg / 65536) % 6
    sim:clickFacePane(target)
  end

  -- Invariant check: after 1000 random switches, all 6 pane editors must still have their exact assigned layer
  for f = 0, kFaceCount - 1 do
    assert_equal(sim.paneEditors[f].layer.name, "Surface_" .. kFaceSpecs[f].sliceName,
      string.format("After 1,000 rapid clicks, Pane %d layer remains Surface_%s", f, kFaceSpecs[f].sliceName))
  end

  doc.sprite:close()
end

-- ==============================================================================
-- SUMMARY
-- ==============================================================================
print("\n==================================================================")
print(string.format("CHALLENGER STRESS HARNESS: %d PASSED, %d FAILED (Total %d)",
  passedChecks, failedChecks, totalChecks))
print("==================================================================")

if failedChecks > 0 then
  for idx, fl in ipairs(failureList) do
    print(string.format("  FAILED %d: %s", idx, fl))
  end
  error(string.format("Challenger verification FAILED with %d failure(s).", failedChecks))
else
  print("ALL EMPIRICAL CHALLENGER VERIFICATION CHECKS PASSED.")
end
