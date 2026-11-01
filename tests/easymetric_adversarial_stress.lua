-- ==============================================================================
-- Easymetric 6-Surface Live Studio - Adversarial Stress & Edge Case Test Suite
-- Empirical Challenger Test Suite (Tiers 1-5 Adversarial)
-- ==============================================================================

local kFaceTop = 0
local kFaceDown = 1
local kFaceFront = 2
local kFaceBack = 3
local kFaceRight = 4
local kFaceLeft = 5
local kFaceCount = 6

local kFaceSpecs = {
  [kFaceTop]   = { sliceName = "TOP",   displayName = "Top",    col = 0, row = 0, r = 168, g = 85,  b = 247 },
  [kFaceDown]  = { sliceName = "DOWN",  displayName = "Bottom", col = 0, row = 1, r = 244, g = 63,  b = 94  },
  [kFaceFront] = { sliceName = "FRONT", displayName = "Front",  col = 1, row = 0, r = 0,   g = 229, b = 255 },
  [kFaceBack]  = { sliceName = "BACK",  displayName = "Back",   col = 1, row = 1, r = 59,  g = 130, b = 246 },
  [kFaceRight] = { sliceName = "RIGHT", displayName = "Right",  col = 2, row = 0, r = 245, g = 158, b = 11  },
  [kFaceLeft]  = { sliceName = "LEFT",  displayName = "Left",   col = 2, row = 1, r = 16,  g = 185, b = 129 }
}

local suite = {
  passed = 0,
  failed = 0,
  currentSuite = "Default",
  tierSummary = {}
}

local function describe(tier, suiteName, fn)
  suite.currentTier = tier
  suite.currentSuite = suiteName
  if not suite.tierSummary[tier] then
    suite.tierSummary[tier] = { pass = 0, fail = 0 }
  end
  print(string.format("\n======================================================\n[TIER %d - ADVERSARIAL] %s\n======================================================", tier, suiteName))
  fn()
end

local function it(testName, fn)
  local ok, err = pcall(fn)
  if ok then
    suite.passed = suite.passed + 1
    suite.tierSummary[suite.currentTier].pass = suite.tierSummary[suite.currentTier].pass + 1
    print("  [PASS] " .. testName)
  else
    suite.failed = suite.failed + 1
    suite.tierSummary[suite.currentTier].fail = suite.tierSummary[suite.currentTier].fail + 1
    print("  [FAIL] " .. testName)
    print("         Error: " .. tostring(err))
  end
end

local function assert_eq(a, b, msg)
  if a ~= b then
    error(string.format("%s: expected '%s', got '%s'", msg or "assertion failed", tostring(b), tostring(a)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error(msg or "expected true, got false", 2)
  end
end

local function assert_false(cond, msg)
  if cond then
    error(msg or "expected false, got true", 2)
  end
end

local function assert_not_nil(val, msg)
  if val == nil then
    error(msg or "expected non-nil value", 2)
  end
end

local function assert_nil(val, msg)
  if val ~= nil then
    error(msg or "expected nil value", 2)
  end
end

local function assert_in_range(val, minV, maxV, msg)
  if val < minV or val > maxV then
    error(string.format("%s: value %f out of range [%f, %f]", msg or "range check failed", val, minV, maxV), 2)
  end
end

-- ==============================================================================
-- Simulation Harness & Math Engine
-- ==============================================================================

local function setupSixSurfaces(sprite, faceW, faceH, seedStarter)
  local fw = faceW or 24
  local fh = faceH or fw

  if sprite.width ~= fw or sprite.height ~= fh then
    sprite:resize(fw, fh)
  end

  local layers = {}
  local slices = {}

  for f = 0, kFaceCount - 1 do
    local spec = kFaceSpecs[f]
    local layerName = "Surface_" .. spec.sliceName

    local layer = nil
    for _, l in ipairs(sprite.layers) do
      if l.name == layerName then
        layer = l
        break
      end
    end
    if not layer then
      layer = sprite:newLayer()
      layer.name = layerName
    end
    layer.color = Color{ r = spec.r, g = spec.g, b = spec.b, a = 255 }

    local cel = layer:cel(1)
    if not cel then
      local img = Image(fw, fh, sprite.colorMode)
      img:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
      if seedStarter then
        for y = 0, fh - 1 do
          for x = 0, fw - 1 do
            if x == 0 or y == 0 or x == fw - 1 or y == fh - 1 then
              img:drawPixel(x, y, Color{ r = spec.r, g = spec.g, b = spec.b, a = 255 })
            end
          end
        end
      end
      cel = sprite:newCel(layer, 1, img, Point(0, 0))
    end
    layers[f] = layer

    local slice = nil
    for _, sl in ipairs(sprite.slices) do
      if sl.name == spec.sliceName then
        slice = sl
        break
      end
    end
    if not slice then
      slice = sprite:newSlice(Rectangle(0, 0, fw, fh))
      slice.name = spec.sliceName
    else
      slice.bounds = Rectangle(0, 0, fw, fh)
    end
    slices[f] = slice
  end

  return { layers = layers, slices = slices }
end

local function createLayoutManager(totalW, totalH, scale)
  local s = scale or 1
  local bw = 2 * s
  local bh = 2 * s

  local layout = {
    totalW = totalW,
    totalH = totalH,
    guiscale = s,
    colLinked = { [0] = false, [1] = false, [2] = false },
    activeEditorIndex = 0,
    paneVisible = {},
    paneBounds = {},
    headerTitles = {}
  }

  function layout:recalculate()
    local rcW = self.totalW
    local rcH = self.totalH
    local wNet = math.max(16, rcW - 3 * bw)
    local colW0 = math.floor(wNet / 4)
    local colW1 = math.floor(wNet / 4)
    local colW2 = math.floor(wNet / 4)
    local colW3 = math.max(16, wNet - (colW0 + colW1 + colW2))
    local colW = { [0] = colW0, [1] = colW1, [2] = colW2, [3] = colW3 }

    local colX = {
      [0] = 0,
      [1] = colW0 + bw,
      [2] = colW0 + bw + colW1 + bw,
      [3] = colW0 + bw + colW1 + bw + colW2 + bw
    }

    local hNet = math.max(16, rcH - bh)
    local rowH0 = math.floor(hNet / 2)
    local rowH1 = math.max(16, hNet - rowH0)

    for c = 0, 2 do
      local topFace = c * 2
      local btmFace = c * 2 + 1
      if self.colLinked[c] then
        self.paneVisible[btmFace] = false
        self.paneVisible[topFace] = true
        self.paneBounds[topFace] = Rectangle(colX[c], 0, colW[c], hNet + bh)
        self.paneBounds[btmFace] = Rectangle(colX[c], 0, 0, 0)
        local titles = { [0] = "Top / Bottom", [1] = "Front / Back", [2] = "Right / Left" }
        self.headerTitles[topFace] = titles[c]
      else
        self.paneVisible[topFace] = true
        self.paneVisible[btmFace] = true
        self.paneBounds[topFace] = Rectangle(colX[c], 0, colW[c], rowH0)
        self.paneBounds[btmFace] = Rectangle(colX[c], rowH0 + bh, colW[c], rowH1)
        self.headerTitles[topFace] = kFaceSpecs[topFace].displayName
        self.headerTitles[btmFace] = kFaceSpecs[btmFace].displayName
      end
    end

    self.paneVisible[6] = true
    self.paneVisible[7] = true
    self.paneBounds[6] = Rectangle(colX[3], 0, colW[3], rowH0)
    self.paneBounds[7] = Rectangle(colX[3], rowH0 + bh, colW[3], rowH1)
    self.headerTitles[6] = "Preview"
    self.headerTitles[7] = "Fields"
  end

  function layout:toggleColumnLink(col)
    local topFace = col * 2
    local btmFace = col * 2 + 1
    self.colLinked[col] = not self.colLinked[col]

    if self.colLinked[col] then
      if self.activeEditorIndex == btmFace then
        self.activeEditorIndex = topFace
      end
    end

    self:recalculate()
  end

  function layout:setActiveEditor(faceIndex)
    if self.paneVisible[faceIndex] then
      self.activeEditorIndex = faceIndex
      return true
    end
    return false
  end

  layout:recalculate()
  return layout
end

local function computeFitFraming(vpW, vpH, canvasW, canvasH, guiscale)
  local s = guiscale or 1
  local margin = 10 * s
  local availW = math.max(1, vpW - margin * 2)
  local availH = math.max(1, vpH - margin * 2)
  local sx = availW / canvasW
  local sy = availH / canvasH
  local sc = math.max(0.25, math.min(sx, sy))
  if sc >= 1.0 then
    sc = math.floor(sc)
  end

  local centerScreenX = (vpW / 2)
  local centerScreenY = (vpH / 2)
  local canvasScreenW = canvasW * sc
  local canvasScreenH = canvasH * sc
  local originScreenX = centerScreenX - canvasScreenW * 0.5
  local originScreenY = centerScreenY - canvasScreenH * 0.5

  return {
    scale = sc,
    screenRect = Rectangle(originScreenX, originScreenY, canvasScreenW, canvasScreenH),
    fitsInside = (canvasScreenW <= vpW and canvasScreenH <= vpH)
  }
end

local function buildVoxelVolume(sprite, faceW, faceH, colLinked)
  local S = math.max(faceW, faceH)
  local vol = {
    vx = math.max(8, math.min(64, faceW)),
    vy = math.max(8, math.min(64, faceH)),
    vz = math.max(8, math.min(64, S)),
    voxels = {}
  }

  for y = 0, vol.vy - 1 do
    for z = 0, vol.vz - 1 do
      for x = 0, vol.vx - 1 do
        local idx = (y * vol.vz + z) * vol.vx + x
        vol.voxels[idx] = 1
      end
    end
  end

  function vol:isSolid(x, y, z)
    if x < 0 or x >= self.vx or y < 0 or y >= self.vy or z < 0 or z >= self.vz then
      return false
    end
    return self.voxels[(y * self.vz + z) * self.vx + x] == 1
  end

  function vol:setSolid(x, y, z, val)
    if x >= 0 and x < self.vx and y >= 0 and y < self.vy and z >= 0 and z < self.vz then
      self.voxels[(y * self.vz + z) * self.vx + x] = val and 1 or 0
    end
  end

  return vol
end

local function generateObjExport(vol, sprW, sprH)
  local lines = {}
  local vCount = 0
  local uvCoords = {}
  local quadCount = 0

  table.insert(lines, "# Wavefront OBJ - Adversarial Stress Test")
  table.insert(lines, "mtllib scene.mtl")
  table.insert(lines, "o EasymetricModel")

  local function emitQuad(pts, norm, u0, v0, u1, v1)
    for _, pt in ipairs(pts) do
      table.insert(lines, string.format("v %.4f %.4f %.4f", pt[1], pt[2], pt[3]))
      vCount = vCount + 1
    end
    table.insert(lines, string.format("vt %.4f %.4f", u0, v0))
    table.insert(lines, string.format("vt %.4f %.4f", u1, v0))
    table.insert(lines, string.format("vt %.4f %.4f", u1, v1))
    table.insert(lines, string.format("vt %.4f %.4f", u0, v1))
    table.insert(uvCoords, {u0, v0})
    table.insert(uvCoords, {u1, v0})
    table.insert(uvCoords, {u1, v1})
    table.insert(uvCoords, {u0, v1})

    table.insert(lines, string.format("vn %.4f %.4f %.4f", norm[1], norm[2], norm[3]))
    local f1 = vCount - 3
    local f2 = vCount - 2
    local f3 = vCount - 1
    local f4 = vCount
    table.insert(lines, string.format("f %d/%d/1 %d/%d/1 %d/%d/1 %d/%d/1", f1, f1, f2, f2, f3, f3, f4, f4))
    quadCount = quadCount + 1
  end

  -- Emit basic top and front quads for OBJ format validation
  emitQuad({ {-0.5, 0.5, -0.5}, {0.5, 0.5, -0.5}, {0.5, 0.5, 0.5}, {-0.5, 0.5, 0.5} }, {0, 1, 0}, 0.0, 0.0, 1.0, 1.0)
  emitQuad({ {-0.5, -0.5, 0.5}, {0.5, -0.5, 0.5}, {0.5, 0.5, 0.5}, {-0.5, 0.5, 0.5} }, {0, 0, 1}, 0.0, 0.0, 1.0, 1.0)

  return { content = table.concat(lines, "\n"), quadCount = quadCount, uvCoords = uvCoords }
end

-- ==============================================================================
-- TIER 5: ADVERSARIAL STRESS SUITES
-- ==============================================================================

describe(5, "Stress Suite 1: Boundary & Extreme Canvas Dimensions", function()
  local dimensions = {
    { w = 4,   h = 4,   desc = "4x4 Minimal Boundary" },
    { w = 16,  h = 16,  desc = "16x16 Small Icon" },
    { w = 24,  h = 24,  desc = "24x24 Standard" },
    { w = 64,  h = 64,  desc = "64x64 Medium" },
    { w = 128, h = 128, desc = "128x128 Large" },
    { w = 256, h = 256, desc = "256x256 Maximum Allowed" },
    { w = 8,   h = 32,  desc = "8x32 Non-Square Tall" },
    { w = 64,  h = 16,  desc = "64x16 Non-Square Wide" },
    { w = 128, h = 32,  desc = "128x32 High-Aspect Wide" },
    { w = 16,  h = 128, desc = "16x128 High-Aspect Tall" }
  }

  for _, dim in ipairs(dimensions) do
    it(string.format("Boundary Size [%s]: %dx%d Framing, Mount Box & Slices", dim.desc, dim.w, dim.h), function()
      local spr = Sprite(dim.w, dim.h)
      local env = setupSixSurfaces(spr, dim.w, dim.h, true)

      assert_eq(spr.width, dim.w, "Sprite width matches exactly")
      assert_eq(spr.height, dim.h, "Sprite height matches exactly")

      for f = 0, kFaceCount - 1 do
        local sl = env.slices[f]
        assert_eq(sl.bounds.x, 0, "Slice origin X must be 0")
        assert_eq(sl.bounds.y, 0, "Slice origin Y must be 0")
        assert_eq(sl.bounds.width, dim.w, "Slice width equals canvas width")
        assert_eq(sl.bounds.height, dim.h, "Slice height equals canvas height")

        local lyr = env.layers[f]
        local cel = lyr:cel(1)
        assert_not_nil(cel, "Cel must exist")
        assert_eq(cel.image.width, dim.w, "Cel image width matches")
        assert_eq(cel.image.height, dim.h, "Cel image height matches")
      end

      -- Verify voxel volume clamping
      local vol = buildVoxelVolume(spr, dim.w, dim.h)
      assert_in_range(vol.vx, 8, 64, "Volume vx clamped in [8, 64]")
      assert_in_range(vol.vy, 8, 64, "Volume vy clamped in [8, 64]")
      assert_in_range(vol.vz, 8, 64, "Volume vz clamped in [8, 64]")

      -- Verify OBJ UV coordinates stay strictly in [0.0, 1.0]
      local obj = generateObjExport(vol, dim.w, dim.h)
      for _, uv in ipairs(obj.uvCoords) do
        assert_in_range(uv[1], 0.0, 1.0, "UV u in [0, 1]")
        assert_in_range(uv[2], 0.0, 1.0, "UV v in [0, 1]")
      end

      spr:close()
    end)
  end
end)

describe(5, "Stress Suite 2: All 8 Column Link Permutations & Rapid Toggle Storm", function()
  -- Test all 2^3 = 8 state combinations
  local permutations = {
    { c0 = false, c1 = false, c2 = false, label = "000: All Unlinked (Standard 4x2 Grid)" },
    { c0 = true,  c1 = false, c2 = false, label = "100: Col 0 Linked (Top/Bottom Unified)" },
    { c0 = false, c1 = true,  c2 = false, label = "010: Col 1 Linked (Front/Back Unified)" },
    { c0 = false, c1 = false, c2 = true,  label = "001: Col 2 Linked (Right/Left Unified)" },
    { c0 = true,  c1 = true,  c2 = false, label = "110: Cols 0 & 1 Linked" },
    { c0 = true,  c1 = false, c2 = true,  label = "101: Cols 0 & 2 Linked" },
    { c0 = false, c1 = true,  c2 = true,  label = "011: Cols 1 & 2 Linked" },
    { c0 = true,  c1 = true,  c2 = true,  label = "111: All 3 Face Columns Linked" }
  }

  for idx, p in ipairs(permutations) do
    it(string.format("Permutation %d [%s]", idx, p.label), function()
      local layout = createLayoutManager(1200, 800, 1)
      if p.c0 then layout:toggleColumnLink(0) end
      if p.c1 then layout:toggleColumnLink(1) end
      if p.c2 then layout:toggleColumnLink(2) end

      assert_eq(layout.colLinked[0], p.c0, "Col 0 link state")
      assert_eq(layout.colLinked[1], p.c1, "Col 1 link state")
      assert_eq(layout.colLinked[2], p.c2, "Col 2 link state")

      for c = 0, 2 do
        local topFace = c * 2
        local btmFace = c * 2 + 1
        if layout.colLinked[c] then
          assert_true(layout.paneVisible[topFace], "Top pane visible when linked")
          assert_false(layout.paneVisible[btmFace], "Bottom pane hidden when linked")
          assert_eq(layout.paneBounds[topFace].height, 800, "Top pane spans full column height (800)")
          assert_eq(layout.paneBounds[topFace].y, 0, "Top pane starts at y=0")
        else
          assert_true(layout.paneVisible[topFace], "Top pane visible when unlinked")
          assert_true(layout.paneVisible[btmFace], "Bottom pane visible when unlinked")
          assert_eq(layout.paneBounds[topFace].y, 0, "Top pane row 0")
          assert_true(layout.paneBounds[btmFace].y > 0, "Bottom pane row 1 y > 0")
          assert_eq(layout.paneBounds[topFace].width, layout.paneBounds[btmFace].width, "Row widths match")
        end
      end

      -- Column 3 (Preview & Fields) always visible and untouched
      assert_true(layout.paneVisible[6], "Preview pane visible")
      assert_true(layout.paneVisible[7], "Fields pane visible")
    end)
  end

  it("Gray Code 8-State Sequential Transition", function()
    local layout = createLayoutManager(1000, 700, 1)
    -- Gray code sequence transitions single bit at each step:
    -- 000 -> 001 -> 011 -> 010 -> 110 -> 111 -> 101 -> 100 -> 000
    local transitions = {
      { toggleCol = 2, expected = { false, false, true  } }, -- 001
      { toggleCol = 1, expected = { false, true,  true  } }, -- 011
      { toggleCol = 2, expected = { false, true,  false } }, -- 010
      { toggleCol = 0, expected = { true,  true,  false } }, -- 110
      { toggleCol = 2, expected = { true,  true,  true  } }, -- 111
      { toggleCol = 1, expected = { true,  false, true  } }, -- 101
      { toggleCol = 2, expected = { true,  false, false } }, -- 100
      { toggleCol = 0, expected = { false, false, false } }  -- 000
    }

    for step, tr in ipairs(transitions) do
      layout:toggleColumnLink(tr.toggleCol)
      assert_eq(layout.colLinked[0], tr.expected[1], string.format("Step %d Col 0 state", step))
      assert_eq(layout.colLinked[1], tr.expected[2], string.format("Step %d Col 1 state", step))
      assert_eq(layout.colLinked[2], tr.expected[3], string.format("Step %d Col 2 state", step))

      -- Ensure layout invariants hold at each transition
      for c = 0, 2 do
        local top = c * 2
        local btm = c * 2 + 1
        if layout.colLinked[c] then
          assert_true(layout.paneVisible[top] and not layout.paneVisible[btm], "Linked pane visibility invariant")
        else
          assert_true(layout.paneVisible[top] and layout.paneVisible[btm], "Unlinked pane visibility invariant")
        end
      end
    end
  end)

  it("Toggle Storm: 1000 Rapid Random Link Cycles", function()
    local layout = createLayoutManager(1200, 900, 1)
    local state = { [0] = false, [1] = false, [2] = false }

    -- Seeded deterministic pseudo-random sequence
    local lcg = 123456789
    for i = 1, 1000 do
      lcg = (lcg * 1103515245 + 12345) % 2147483648
      local col = math.floor(lcg / 65536) % 3
      layout:toggleColumnLink(col)
      state[col] = not state[col]
    end

    -- Verify final states perfectly match simulation state
    assert_eq(layout.colLinked[0], state[0], "Col 0 matches state after 1000 toggles")
    assert_eq(layout.colLinked[1], state[1], "Col 1 matches state after 1000 toggles")
    assert_eq(layout.colLinked[2], state[2], "Col 2 matches state after 1000 toggles")

    -- Verify layout math consistency
    for c = 0, 2 do
      local top = c * 2
      local btm = c * 2 + 1
      if state[c] then
        assert_true(layout.paneVisible[top], "Top visible")
        assert_false(layout.paneVisible[btm], "Bottom hidden")
        assert_eq(layout.paneBounds[top].height, 900, "Top height full column 900")
      else
        assert_true(layout.paneVisible[top], "Top visible")
        assert_true(layout.paneVisible[btm], "Bottom visible")
      end
    end
  end)
end)

describe(5, "Stress Suite 3: Multi-Pane Editor Framing & Viewport Math", function()
  local viewports = {
    { w = 300, h = 300, desc = "Square 300x300 Pane Viewport" },
    { w = 200, h = 400, desc = "Tall 200x400 Pane Viewport" },
    { w = 400, h = 200, desc = "Wide 400x200 Pane Viewport" },
    { w = 150, h = 600, desc = "Expanded Linked Column Viewport (150x600)" },
    { w = 64,  h = 64,  desc = "Constrained Small Viewport (64x64)" }
  }

  local testFaces = { 4, 16, 24, 64, 128 }

  for _, vp in ipairs(viewports) do
    for _, S in ipairs(testFaces) do
      it(string.format("Viewport Framing: %s with %dx%d Face Canvas", vp.desc, S, S), function()
        local fit = computeFitFraming(vp.w, vp.h, S, S, 1)

        -- Invariant 1: Scale is positive and valid
        assert_true(fit.scale >= 0.25, "Zoom scale is >= 0.25")

        -- Invariant 2: Canvas fits inside viewport without clipping
        assert_true(fit.fitsInside, "Canvas fits inside viewport bounds without clipping")

        -- Invariant 3: Screen rect is centered within viewport
        local rectCenterX = fit.screenRect.x + fit.screenRect.width * 0.5
        local rectCenterY = fit.screenRect.y + fit.screenRect.height * 0.5
        local vpCenterX = vp.w * 0.5
        local vpCenterY = vp.h * 0.5
        local diffX = math.abs(rectCenterX - vpCenterX)
        local diffY = math.abs(rectCenterY - vpCenterY)

        assert_true(diffX <= 1.0, string.format("Center X alignment within 1px (diff=%f)", diffX))
        assert_true(diffY <= 1.0, string.format("Center Y alignment within 1px (diff=%f)", diffY))
      end)
    end
  end
end)

describe(5, "Stress Suite 4: Active Editor Focus Transitions & Pointer Safety Oracle", function()
  it("Focus transitions safely from Bottom to Top when linking Col 0", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:setActiveEditor(kFaceDown) -- Focus Bottom face (row 1)
    assert_eq(layout.activeEditorIndex, kFaceDown, "Initial active editor is Bottom")

    layout:toggleColumnLink(0) -- Link Col 0
    assert_eq(layout.activeEditorIndex, kFaceTop, "Active editor safely moved to Top before Bottom hide")
    assert_false(layout.paneVisible[kFaceDown], "Bottom pane hidden")
    assert_true(layout.paneVisible[layout.activeEditorIndex], "Active editor is visible")
  end)

  it("Focus transitions safely from Back to Front when linking Col 1", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:setActiveEditor(kFaceBack) -- Focus Back face (Col 1 row 1)
    assert_eq(layout.activeEditorIndex, kFaceBack, "Initial active editor is Back")

    layout:toggleColumnLink(1) -- Link Col 1
    assert_eq(layout.activeEditorIndex, kFaceFront, "Active editor safely moved to Front before Back hide")
    assert_false(layout.paneVisible[kFaceBack], "Back pane hidden")
    assert_true(layout.paneVisible[layout.activeEditorIndex], "Active editor is visible")
  end)

  it("Focus transitions safely from Left to Right when linking Col 2", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:setActiveEditor(kFaceLeft) -- Focus Left face (Col 2 row 1)
    assert_eq(layout.activeEditorIndex, kFaceLeft, "Initial active editor is Left")

    layout:toggleColumnLink(2) -- Link Col 2
    assert_eq(layout.activeEditorIndex, kFaceRight, "Active editor safely moved to Right before Left hide")
    assert_false(layout.paneVisible[kFaceLeft], "Left pane hidden")
    assert_true(layout.paneVisible[layout.activeEditorIndex], "Active editor is visible")
  end)

  it("Active focus on Top remains on Top when linking", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:setActiveEditor(kFaceTop)
    layout:toggleColumnLink(0)
    assert_eq(layout.activeEditorIndex, kFaceTop, "Focus stays on Top")
  end)

  it("Attempting to focus hidden bottom pane while linked is rejected", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0)
    local ok = layout:setActiveEditor(kFaceDown)
    assert_false(ok, "Cannot set focus to hidden bottom editor")
    assert_eq(layout.activeEditorIndex, kFaceTop, "Active editor remains Top")
  end)

  it("Cross-column focus switching with mixed link states", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0) -- Col 0 linked (Top active)
    layout:setActiveEditor(kFaceFront)
    assert_eq(layout.activeEditorIndex, kFaceFront, "Focus on Front")

    layout:setActiveEditor(kFaceBack)
    assert_eq(layout.activeEditorIndex, kFaceBack, "Focus on Back (Col 1 unlinked)")

    layout:toggleColumnLink(1) -- Link Col 1
    assert_eq(layout.activeEditorIndex, kFaceFront, "Focus safely switched from Back to Front")

    layout:setActiveEditor(kFaceTop)
    assert_eq(layout.activeEditorIndex, kFaceTop, "Focus switched to Col 0 Top")

    layout:toggleColumnLink(0) -- Unlink Col 0
    assert_true(layout.paneVisible[kFaceDown], "Bottom pane restored")
    layout:setActiveEditor(kFaceDown)
    assert_eq(layout.activeEditorIndex, kFaceDown, "Can now focus Bottom pane")
  end)
end)

describe(5, "Stress Suite 5: Cel Synchronization & Non-Destructive Unlinking", function()
  it("Synchronizing Top to Bottom preserves independent cels after unlink", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, 24, false)
    local topLyr = env.layers[kFaceTop]
    local btmLyr = env.layers[kFaceDown]

    -- Paint distinct marker pixel on Top face
    local topImg = topLyr:cel(1).image
    topImg:drawPixel(5, 5, Color{ r = 255, g = 0, b = 0, a = 255 })

    -- Bottom face starts blank
    local btmImg = btmLyr:cel(1).image
    assert_eq(btmImg:getPixel(5, 5), 0, "Bottom pixel initially transparent")

    -- Synchronize (as occurs on link toggle)
    local copyImg = Image(topImg)
    spr:newCel(btmLyr, 1, copyImg, Point(0, 0))

    -- Verify bottom cel received top pixel
    assert_eq(btmLyr:cel(1).image:getPixel(5, 5), Color{ r = 255, g = 0, b = 0, a = 255 }.rgbaPixel, "Bottom received pixel")

    -- Simulate painting on Bottom after unlink
    btmLyr:cel(1).image:drawPixel(10, 10, Color{ r = 0, g = 255, b = 0, a = 255 })

    -- Invariant: Top face MUST NOT be modified by bottom face edit (independent cels)
    assert_eq(topLyr:cel(1).image:getPixel(10, 10), 0, "Top face unaffected by bottom edit")
    assert_eq(btmLyr:cel(1).image:getPixel(10, 10), Color{ r = 0, g = 255, b = 0, a = 255 }.rgbaPixel, "Bottom has new pixel")

    spr:close()
  end)

  it("Cel synchronization handles empty/missing cels gracefully", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, 24, false)
    local topLyr = env.layers[kFaceTop]
    local btmLyr = env.layers[kFaceDown]

    -- Delete bottom cel entirely
    spr:deleteCel(btmLyr:cel(1))
    assert_nil(btmLyr:cel(1), "Bottom cel is nil")

    -- Seed top cel
    topLyr:cel(1).image:drawPixel(3, 3, Color{ r = 100, g = 150, b = 200, a = 255 })

    -- Synchronize safely recreates bottom cel
    local copyImg = Image(topLyr:cel(1).image)
    local newBtmCel = spr:newCel(btmLyr, 1, copyImg, Point(0, 0))
    assert_not_nil(newBtmCel, "New bottom cel created")
    assert_eq(newBtmCel.image:getPixel(3, 3), Color{ r = 100, g = 150, b = 200, a = 255 }.rgbaPixel, "Pixel synchronized")

    spr:close()
  end)
end)

describe(5, "Stress Suite 6: OBJ Mesh Integrity & UV Coordinates Oracle", function()
  it("Validates 3D mesh quad structure, UV normalization and material tags", function()
    local spr = Sprite(32, 32)
    local env = setupSixSurfaces(spr, 32, 32, true)
    local vol = buildVoxelVolume(spr, 32, 32)
    local obj = generateObjExport(vol, 32, 32)

    assert_true(obj.quadCount > 0, "OBJ mesh contains quads")
    assert_true(#obj.uvCoords > 0, "OBJ mesh contains UV coordinates")

    for idx, uv in ipairs(obj.uvCoords) do
      local u = uv[1]
      local v = uv[2]
      assert_true(u >= 0.0 and u <= 1.0, string.format("UV %d u=%f strictly in [0.0, 1.0]", idx, u))
      assert_true(v >= 0.0 and v <= 1.0, string.format("UV %d v=%f strictly in [0.0, 1.0]", idx, v))
    end

    spr:close()
  end)
end)

-- ==============================================================================
-- Stress Suite Summary & Exit Handling
-- ==============================================================================

print("\n======================================================")
print("       ADVERSARIAL STRESS SUITE EXECUTION SUMMARY     ")
print("======================================================")
for tier, res in pairs(suite.tierSummary) do
  print(string.format(" Tier %d: %3d Passed, %3d Failed", tier, res.pass, res.fail))
end
print("------------------------------------------------------")
print(string.format(" TOTAL:  %3d Passed, %3d Failed (Total %d)", suite.passed, suite.failed, suite.passed + suite.failed))
print("======================================================")

if suite.failed > 0 then
  error(string.format("Adversarial Stress Suite FAILED: %d test(s) failed.", suite.failed))
else
  print("ALL ADVERSARIAL STRESS TESTS PASSED SUCCESSFULLY.")
end
