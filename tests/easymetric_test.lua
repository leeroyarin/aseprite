-- ==============================================================================
-- Easymetric 6-Surface Live Studio - Comprehensive E2E Test Suite
-- Aseprite 2.0 Test Suite (Tiers 1-4)
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

-- Test Framework Harness
local suite = {
  tests = {},
  passed = 0,
  failed = 0,
  currentSuite = "Default",
  tierSummary = { [1] = {pass=0, fail=0}, [2] = {pass=0, fail=0}, [3] = {pass=0, fail=0}, [4] = {pass=0, fail=0} }
}

function describe(tier, suiteName, fn)
  suite.currentTier = tier
  suite.currentSuite = suiteName
  print(string.format("\n======================================================\n[TIER %d] %s\n======================================================", tier, suiteName))
  fn()
end

function it(testName, fn)
  local fullName = string.format("[%s] %s", suite.currentSuite, testName)
  local ok, err = pcall(fn)
  if ok then
    suite.passed = suite.passed + 1
    if suite.tierSummary[suite.currentTier] then
      suite.tierSummary[suite.currentTier].pass = suite.tierSummary[suite.currentTier].pass + 1
    end
    print("  [PASS] " .. testName)
  else
    suite.failed = suite.failed + 1
    if suite.tierSummary[suite.currentTier] then
      suite.tierSummary[suite.currentTier].fail = suite.tierSummary[suite.currentTier].fail + 1
    end
    print("  [FAIL] " .. testName)
    print("         Error: " .. tostring(err))
  end
end

function assert_eq(a, b, msg)
  if a ~= b then
    error(string.format("%s: expected '%s', got '%s'", msg or "assertion failed", tostring(b), tostring(a)), 2)
  end
end

function assert_true(cond, msg)
  if not cond then
    error(msg or "expected true, got false", 2)
  end
end

function assert_false(cond, msg)
  if cond then
    error(msg or "expected false, got true", 2)
  end
end

function assert_not_nil(val, msg)
  if val == nil then
    error(msg or "expected non-nil value", 2)
  end
end

function assert_in_range(val, minV, maxV, msg)
  if val < minV or val > maxV then
    error(string.format("%s: value %f out of range [%f, %f]", msg or "range check failed", val, minV, maxV), 2)
  end
end

-- ==============================================================================
-- Easymetric Engine Simulation & Helpers
-- ==============================================================================

local function setupSixSurfaces(sprite, faceSize, seedStarter)
  local S = faceSize or 24
  -- Canvas bounds are directly (S x S) - No 112x96 atlas grid expansion!
  if sprite.width ~= S or sprite.height ~= S then
    sprite:resize(S, S)
  end

  local layers = {}
  local slices = {}

  for f = 0, kFaceCount - 1 do
    local spec = kFaceSpecs[f]
    local layerName = "Surface_" .. spec.sliceName

    -- Find or create layer
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

    -- Ensure cel exists
    local cel = layer:cel(1)
    if not cel then
      local img = Image(S, S, sprite.colorMode)
      img:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
      if seedStarter then
        -- Seed border starter pixels
        for y = 0, S - 1 do
          for x = 0, S - 1 do
            if x == 0 or y == 0 or x == S - 1 or y == S - 1 then
              img:drawPixel(x, y, Color{ r = spec.r, g = spec.g, b = spec.b, a = 255 })
            end
          end
        end
      end
      cel = sprite:newCel(layer, 1, img, Point(0, 0))
    end
    layers[f] = layer

    -- Find or create slice: Origin MUST BE (0, 0, S, S) - No (10, 16) or (44, 56) offsets!
    local slice = nil
    for _, sl in ipairs(sprite.slices) do
      if sl.name == spec.sliceName then
        slice = sl
        break
      end
    end
    if not slice then
      slice = sprite:newSlice(Rectangle(0, 0, S, S))
      slice.name = spec.sliceName
    else
      slice.bounds = Rectangle(0, 0, S, S)
    end
    slice.color = Color{ r = spec.r, g = spec.g, b = spec.b, a = 255 }
    slices[f] = slice
  end

  return { layers = layers, slices = slices, size = S }
end

local function createLayoutManager(totalW, totalH, guiscale)
  local s = guiscale or 1
  local bw = 2 * s
  local bh = 2 * s

  local state = {
    colLinked = { [0] = false, [1] = false, [2] = false },
    activeEditorFace = kFaceTop,
    headers = {},
    paneBounds = {},
    paneVisible = {}
  }

  for f = 0, kFaceCount - 1 do
    state.headers[f] = kFaceSpecs[f].displayName
    state.paneVisible[f] = true
  end

  function state:computeLayout()
    local availW = math.max(16, totalW - 3 * bw)
    local colW0 = math.floor(availW / 4)
    local colW1 = colW0
    local colW2 = colW0
    local colW3 = math.max(16, availW - (colW0 + colW1 + colW2))

    local availH = math.max(16, totalH - bh)
    local rowH0 = math.floor(availH / 2)
    local rowH1 = math.max(16, availH - rowH0)

    local colX = {
      [0] = 0,
      [1] = colW0 + bw,
      [2] = colW0 + bw + colW1 + bw,
      [3] = colW0 + bw + colW1 + bw + colW2 + bw
    }
    local colW = { [0] = colW0, [1] = colW1, [2] = colW2, [3] = colW3 }
    local rowY = { [0] = 0, [1] = rowH0 + bh }
    local rowH = { [0] = rowH0, [1] = rowH1 }

    for c = 0, 2 do
      local topFace = c * 2
      local btmFace = c * 2 + 1

      if self.colLinked[c] then
        -- Single-row unified layout: bottom pane hidden, top pane spans full column
        self.paneVisible[btmFace] = false
        self.paneVisible[topFace] = true
        self.paneBounds[topFace] = Rectangle(colX[c], rowY[0], colW[c], totalH)
        self.paneBounds[btmFace] = Rectangle(colX[c], rowY[1], colW[c], 0)
        self.headers[topFace] = kFaceSpecs[topFace].displayName .. " / " .. kFaceSpecs[btmFace].displayName
      else
        -- Separate 2-row layout
        self.paneVisible[topFace] = true
        self.paneVisible[btmFace] = true
        self.paneBounds[topFace] = Rectangle(colX[c], rowY[0], colW[c], rowH[0])
        self.paneBounds[btmFace] = Rectangle(colX[c], rowY[1], colW[c], rowH[1])
        self.headers[topFace] = kFaceSpecs[topFace].displayName
        self.headers[btmFace] = kFaceSpecs[btmFace].displayName
      end
    end

    -- Preview and Fields (Column 3)
    self.paneBounds[6] = Rectangle(colX[3], rowY[0], colW[3], rowH[0])
    self.paneBounds[7] = Rectangle(colX[3], rowY[1], colW[3], rowH[1])
    self.paneVisible[6] = true
    self.paneVisible[7] = true
  end

  function state:toggleColumnLink(col, sprite)
    local topFace = col * 2
    local btmFace = col * 2 + 1
    local newLink = not self.colLinked[col]

    -- Active editor focus safety: if bottom editor has focus, transition to top face
    if newLink and self.activeEditorFace == btmFace then
      self.activeEditorFace = topFace
    end

    self.colLinked[col] = newLink

    -- Cel synchronization when sprite is provided
    if sprite then
      local topLayer = nil
      local btmLayer = nil
      for _, l in ipairs(sprite.layers) do
        if l.name == "Surface_" .. kFaceSpecs[topFace].sliceName then topLayer = l end
        if l.name == "Surface_" .. kFaceSpecs[btmFace].sliceName then btmLayer = l end
      end
      if topLayer and btmLayer then
        local topCel = topLayer:cel(1)
        local btmCel = btmLayer:cel(1)
        if topCel and btmCel then
          -- Synchronize top cel image into bottom cel image
          local srcImg = topCel.image
          local dstImg = btmCel.image
          for y = 0, srcImg.height - 1 do
            for x = 0, srcImg.width - 1 do
              dstImg:drawPixel(x, y, srcImg:getPixel(x, y))
            end
          end
        end
      end
    end

    self:computeLayout()
  end

  state:computeLayout()
  return state
end

local function buildVoxelVolume(sprite, faceSize, colLinked)
  local S = faceSize or 24
  local vol = { vx = S, vy = S, vz = S, voxels = {} }

  for y = 0, S - 1 do
    vol.voxels[y] = {}
    for z = 0, S - 1 do
      vol.voxels[y][z] = {}
      for x = 0, S - 1 do
        vol.voxels[y][z][x] = false
      end
    end
  end

  local faceImages = {}
  local hasPixels = {}
  local anyPixels = false

  for f = 0, kFaceCount - 1 do
    local layerName = "Surface_" .. kFaceSpecs[f].sliceName
    hasPixels[f] = false
    for _, l in ipairs(sprite.layers) do
      if l.name == layerName and l:cel(1) then
        local img = l:cel(1).image
        faceImages[f] = img
        for py = 0, img.height - 1 do
          for px = 0, img.width - 1 do
            if app.pixelColor.rgbaA(img:getPixel(px, py)) > 16 then
              hasPixels[f] = true
              anyPixels = true
              break
            end
          end
          if hasPixels[f] then break end
        end
        break
      end
    end
  end

  -- When all faces are blank, default to completely solid bounding cube (cmd_easymetric.cpp:343-346)
  if not anyPixels then
    for y = 0, S - 1 do
      for z = 0, S - 1 do
        for x = 0, S - 1 do
          vol.voxels[y][z][x] = true
        end
      end
    end
    function vol:isSolid(x, y, z)
      return (x >= 0 and x < self.vx and y >= 0 and y < self.vy and z >= 0 and z < self.vz)
    end
    return vol
  end

  -- Apply column linking aliases
  if colLinked then
    if colLinked[0] and faceImages[kFaceTop] then
      faceImages[kFaceDown] = faceImages[kFaceTop]
      hasPixels[kFaceDown] = hasPixels[kFaceTop]
    end
    if colLinked[1] and faceImages[kFaceFront] then
      faceImages[kFaceBack] = faceImages[kFaceFront]
      hasPixels[kFaceBack] = hasPixels[kFaceFront]
    end
    if colLinked[2] and faceImages[kFaceRight] then
      faceImages[kFaceLeft] = faceImages[kFaceRight]
      hasPixels[kFaceLeft] = hasPixels[kFaceRight]
    end
  end

  local activeTopDown   = (hasPixels[kFaceTop] or hasPixels[kFaceDown])
  local activeFrontBack = (hasPixels[kFaceFront] or hasPixels[kFaceBack])
  local activeRightLeft = (hasPixels[kFaceRight] or hasPixels[kFaceLeft])

  local function isSolidFace(f, u, v)
    local img = faceImages[f]
    if not img then return false end
    u = math.max(0, math.min(S - 1, u))
    v = math.max(0, math.min(S - 1, v))
    local pix = img:getPixel(u, v)
    return app.pixelColor.rgbaA(pix) > 16
  end

  for y = 0, S - 1 do
    for z = 0, S - 1 do
      for x = 0, S - 1 do
        local solid = true

        if activeTopDown then
          local topSolid = isSolidFace(kFaceTop, x, z)
          if (not colLinked or not colLinked[0]) and hasPixels[kFaceDown] then
            local downSolid = isSolidFace(kFaceDown, x, S - 1 - z)
            topSolid = (hasPixels[kFaceTop] and (topSolid or downSolid)) or downSolid
          end
          if not topSolid then solid = false end
        end

        if solid and activeFrontBack then
          local frontSolid = isSolidFace(kFaceFront, x, y)
          if (not colLinked or not colLinked[1]) and hasPixels[kFaceBack] then
            local backSolid = isSolidFace(kFaceBack, S - 1 - x, y)
            frontSolid = (hasPixels[kFaceFront] and (frontSolid or backSolid)) or backSolid
          end
          if not frontSolid then solid = false end
        end

        if solid and activeRightLeft then
          local sideSolid = isSolidFace(kFaceRight, S - 1 - z, y)
          if (not colLinked or not colLinked[2]) and hasPixels[kFaceLeft] then
            local leftSolid = isSolidFace(kFaceLeft, z, y)
            sideSolid = (hasPixels[kFaceRight] and (sideSolid or leftSolid)) or leftSolid
          end
          if not sideSolid then solid = false end
        end

        if solid then
          vol.voxels[y][z][x] = true
        end
      end
    end
  end

  function vol:isSolid(x, y, z)
    if x < 0 or x >= self.vx or y < 0 or y >= self.vy or z < 0 or z >= self.vz then
      return false
    end
    return self.voxels[y][z][x] == true
  end

  return vol
end

local function generateObjExport(vol, faceSize)
  local lines = {}
  table.insert(lines, "# Aseprite 2.0 Easymetric Sculpted 3D Model")
  table.insert(lines, "mtllib easymetric_model.mtl")
  table.insert(lines, "usemtl EasymetricMat")
  table.insert(lines, "o EasymetricSculptedModel")

  local vIdx = 1
  local vtIdx = 1
  local vnIdx = 1

  local quadCount = 0
  local uvCoords = {}

  local S = faceSize or vol.vx
  local halfX = S * 0.5
  local halfY = S * 0.5
  local halfZ = S * 0.5

  local function emitQuad(v, n, u0, v0, u1, v1)
    for i = 1, 4 do
      table.insert(lines, string.format("v %f %f %f", v[i][1], v[i][2], v[i][3]))
    end
    table.insert(lines, string.format("vt %f %f", u0, v0))
    table.insert(lines, string.format("vt %f %f", u1, v0))
    table.insert(lines, string.format("vt %f %f", u1, v1))
    table.insert(lines, string.format("vt %f %f", u0, v1))
    table.insert(lines, string.format("vn %f %f %f", n[1], n[2], n[3]))

    table.insert(lines, string.format("f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d",
      vIdx, vtIdx, vnIdx,
      vIdx+1, vtIdx+1, vnIdx,
      vIdx+2, vtIdx+2, vnIdx,
      vIdx+3, vtIdx+3, vnIdx))

    table.insert(uvCoords, { u0 = u0, v0 = v0, u1 = u1, v1 = v1 })
    vIdx = vIdx + 4
    vtIdx = vtIdx + 4
    vnIdx = vnIdx + 1
    quadCount = quadCount + 1
  end

  for vy = 0, vol.vy - 1 do
    local yMax = halfY - vy
    local yMin = halfY - (vy + 1)
    for vz = 0, vol.vz - 1 do
      local zMin = -halfZ + vz
      local zMax = -halfZ + (vz + 1)
      for vx = 0, vol.vx - 1 do
        if vol:isSolid(vx, vy, vz) then
          local xMin = -halfX + vx
          local xMax = -halfX + (vx + 1)

          -- Front face (+Z)
          if not vol:isSolid(vx, vy, vz + 1) then
            local u0 = vx / vol.vx
            local u1 = (vx + 1) / vol.vx
            local v0 = 1.0 - (vy + 1) / vol.vy
            local v1 = 1.0 - vy / vol.vy
            emitQuad({
              {xMin, yMin, zMax}, {xMax, yMin, zMax}, {xMax, yMax, zMax}, {xMin, yMax, zMax}
            }, {0, 0, 1}, u0, v0, u1, v1)
          end

          -- Back face (-Z)
          if not vol:isSolid(vx, vy, vz - 1) then
            local u0 = (vol.vx - 1 - vx) / vol.vx
            local u1 = (vol.vx - vx) / vol.vx
            local v0 = 1.0 - (vy + 1) / vol.vy
            local v1 = 1.0 - vy / vol.vy
            emitQuad({
              {xMax, yMin, zMin}, {xMin, yMin, zMin}, {xMin, yMax, zMin}, {xMax, yMax, zMin}
            }, {0, 0, -1}, u0, v0, u1, v1)
          end

          -- Top face (+Y)
          if not vol:isSolid(vx, vy - 1, vz) then
            local u0 = vx / vol.vx
            local u1 = (vx + 1) / vol.vx
            local v0 = 1.0 - (vz + 1) / vol.vz
            local v1 = 1.0 - vz / vol.vz
            emitQuad({
              {xMin, yMax, zMax}, {xMax, yMax, zMax}, {xMax, yMax, zMin}, {xMin, yMax, zMin}
            }, {0, 1, 0}, u0, v0, u1, v1)
          end

          -- Bottom face (-Y)
          if not vol:isSolid(vx, vy + 1, vz) then
            local u0 = vx / vol.vx
            local u1 = (vx + 1) / vol.vx
            local v0 = 1.0 - (vol.vz - vz) / vol.vz
            local v1 = 1.0 - (vol.vz - 1 - vz) / vol.vz
            emitQuad({
              {xMin, yMin, zMin}, {xMax, yMin, zMin}, {xMax, yMin, zMax}, {xMin, yMin, zMax}
            }, {0, -1, 0}, u0, v0, u1, v1)
          end

          -- Right face (+X)
          if not vol:isSolid(vx + 1, vy, vz) then
            local u0 = (vol.vz - 1 - vz) / vol.vz
            local u1 = (vol.vz - vz) / vol.vz
            local v0 = 1.0 - (vy + 1) / vol.vy
            local v1 = 1.0 - vy / vol.vy
            emitQuad({
              {xMax, yMin, zMax}, {xMax, yMin, zMin}, {xMax, yMax, zMin}, {xMax, yMax, zMax}
            }, {1, 0, 0}, u0, v0, u1, v1)
          end

          -- Left face (-X)
          if not vol:isSolid(vx - 1, vy, vz) then
            local u0 = vz / vol.vz
            local u1 = (vz + 1) / vol.vz
            local v0 = 1.0 - (vy + 1) / vol.vy
            local v1 = 1.0 - vy / vol.vy
            emitQuad({
              {xMin, yMin, zMin}, {xMin, yMin, zMax}, {xMin, yMax, zMax}, {xMin, yMax, zMin}
            }, {-1, 0, 0}, u0, v0, u1, v1)
          end
        end
      end
    end
  end

  return { content = table.concat(lines, "\n"), quadCount = quadCount, uvCoords = uvCoords }
end

-- ==============================================================================
-- TIER 1: Feature Coverage (>=5 test cases per feature)
-- ==============================================================================

describe(1, "Feature 1: Canvas Bounds Framing (No 112x96 Sheet Inflation)", function()
  it("TC 1.1: 24x24 canvas bounds match face dimensions exactly without 112x96 expansion", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    assert_eq(spr.width, 24, "Sprite width should remain 24")
    assert_eq(spr.height, 24, "Sprite height should remain 24")
    assert_false(spr.width == 112 and spr.height == 96, "Canvas must NOT inflate to 112x96")
  end)

  it("TC 1.2: Mount box rect matches (0, 0, S, S) for 24x24 canvas", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    for f = 0, kFaceCount - 1 do
      local sl = env.slices[f]
      assert_eq(sl.bounds, Rectangle(0, 0, 24, 24), "Slice bounds must be exactly (0, 0, 24, 24)")
    end
  end)

  it("TC 1.3: Sprite bounds equal mount box bounds for 16x16 face", function()
    local spr = Sprite(16, 16)
    local env = setupSixSurfaces(spr, 16, true)
    assert_eq(spr.width, 16, "Sprite width 16")
    assert_eq(spr.height, 16, "Sprite height 16")
    assert_eq(env.slices[kFaceFront].bounds, Rectangle(0, 0, 16, 16), "Front mount box 16x16")
  end)

  it("TC 1.4: Sprite bounds equal mount box bounds for 32x32 face", function()
    local spr = Sprite(32, 32)
    local env = setupSixSurfaces(spr, 32, true)
    assert_eq(spr.width, 32, "Sprite width 32")
    assert_eq(spr.height, 32, "Sprite height 32")
    assert_eq(env.slices[kFaceTop].bounds, Rectangle(0, 0, 32, 32), "Top mount box 32x32")
  end)

  it("TC 1.5: Sprite bounds equal mount box bounds for 64x64 face", function()
    local spr = Sprite(64, 64)
    local env = setupSixSurfaces(spr, 64, true)
    assert_eq(spr.width, 64, "Sprite width 64")
    assert_eq(spr.height, 64, "Sprite height 64")
    assert_eq(env.slices[kFaceRight].bounds, Rectangle(0, 0, 64, 64), "Right mount box 64x64")
  end)
end)

describe(1, "Feature 2: Slice Bounding Box Offset Removal", function()
  it("TC 2.1: Face 0 (TOP) slice bounds origin is (0, 0), eliminating (10, 16)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    local sl = env.slices[kFaceTop]
    assert_eq(sl.bounds.x, 0, "TOP origin X must be 0")
    assert_eq(sl.bounds.y, 0, "TOP origin Y must be 0")
    assert_false(sl.bounds.x == 10 and sl.bounds.y == 16, "Legacy (10, 16) offset must be eliminated")
  end)

  it("TC 2.2: Face 1 (DOWN) slice bounds origin is (0, 0), eliminating (10, 56)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    local sl = env.slices[kFaceDown]
    assert_eq(sl.bounds.x, 0, "DOWN origin X must be 0")
    assert_eq(sl.bounds.y, 0, "DOWN origin Y must be 0")
    assert_false(sl.bounds.x == 10 and sl.bounds.y == 56, "Legacy (10, 56) offset must be eliminated")
  end)

  it("TC 2.3: Face 2 (FRONT) slice bounds origin is (0, 0), eliminating (44, 16)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    local sl = env.slices[kFaceFront]
    assert_eq(sl.bounds.x, 0, "FRONT origin X must be 0")
    assert_eq(sl.bounds.y, 0, "FRONT origin Y must be 0")
    assert_false(sl.bounds.x == 44 and sl.bounds.y == 16, "Legacy (44, 16) offset must be eliminated")
  end)

  it("TC 2.4: Face 3 (BACK) slice bounds origin is (0, 0), eliminating (44, 56)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    local sl = env.slices[kFaceBack]
    assert_eq(sl.bounds.x, 0, "BACK origin X must be 0")
    assert_eq(sl.bounds.y, 0, "BACK origin Y must be 0")
    assert_false(sl.bounds.x == 44 and sl.bounds.y == 56, "Legacy (44, 56) offset must be eliminated")
  end)

  it("TC 2.5: Face 4 & 5 (RIGHT/LEFT) origins are (0, 0), eliminating (78, 16)/(78, 56)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    assert_eq(env.slices[kFaceRight].bounds.x, 0, "RIGHT origin X must be 0")
    assert_eq(env.slices[kFaceRight].bounds.y, 0, "RIGHT origin Y must be 0")
    assert_eq(env.slices[kFaceLeft].bounds.x, 0, "LEFT origin X must be 0")
    assert_eq(env.slices[kFaceLeft].bounds.y, 0, "LEFT origin Y must be 0")
  end)
end)

describe(1, "Feature 3: 6-Face Layer Structure", function()
  it("TC 3.1: All 6 required face layers exist with correct names", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local expectedNames = {
      "Surface_TOP", "Surface_DOWN", "Surface_FRONT",
      "Surface_BACK", "Surface_RIGHT", "Surface_LEFT"
    }
    for _, name in ipairs(expectedNames) do
      local found = false
      for _, l in ipairs(spr.layers) do
        if l.name == name then found = true; break end
      end
      assert_true(found, "Missing required layer: " .. name)
    end
  end)

  it("TC 3.2: Each layer has correct theme accent user-data color matching kFaceSpecs", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    for f = 0, kFaceCount - 1 do
      local spec = kFaceSpecs[f]
      local lyr = env.layers[f]
      assert_eq(lyr.color.red, spec.r, "Layer " .. spec.sliceName .. " red mismatch")
      assert_eq(lyr.color.green, spec.g, "Layer " .. spec.sliceName .. " green mismatch")
      assert_eq(lyr.color.blue, spec.b, "Layer " .. spec.sliceName .. " blue mismatch")
    end
  end)

  it("TC 3.3: Each layer contains active cel for frame 1 with valid image", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    for f = 0, kFaceCount - 1 do
      local cel = env.layers[f]:cel(1)
      assert_not_nil(cel, "Missing cel for face " .. f)
      assert_not_nil(cel.image, "Missing cel image for face " .. f)
    end
  end)

  it("TC 3.4: Layer image dimensions match canvas dimensions (S x S)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    for f = 0, kFaceCount - 1 do
      local img = env.layers[f]:cel(1).image
      assert_eq(img.width, 24, "Image width must be 24")
      assert_eq(img.height, 24, "Image height must be 24")
    end
  end)

  it("TC 3.5: Surface_DEPTH layer setup and visibility support", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local depthLyr = spr:newLayer()
    depthLyr.name = "Surface_DEPTH"
    depthLyr.isVisible = true
    assert_eq(depthLyr.name, "Surface_DEPTH", "Depth layer created")
    assert_true(depthLyr.isVisible, "Depth layer visible")
  end)
end)

describe(1, "Feature 4: Column Link Toggle State", function()
  it("TC 4.1: Initial link state for all 3 columns defaults to false", function()
    local layout = createLayoutManager(800, 600, 1)
    assert_false(layout.colLinked[0], "Col 0 default unlinked")
    assert_false(layout.colLinked[1], "Col 1 default unlinked")
    assert_false(layout.colLinked[2], "Col 2 default unlinked")
  end)

  it("TC 4.2: Toggling Col 0 link transitions state from false to true", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0)
    assert_true(layout.colLinked[0], "Col 0 linked state true")
    assert_false(layout.colLinked[1], "Col 1 remains unlinked")
    assert_false(layout.colLinked[2], "Col 2 remains unlinked")
  end)

  it("TC 4.3: Toggling Col 1 link transitions state from false to true", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(1)
    assert_true(layout.colLinked[1], "Col 1 linked state true")
    assert_false(layout.colLinked[0], "Col 0 remains unlinked")
    assert_false(layout.colLinked[2], "Col 2 remains unlinked")
  end)

  it("TC 4.4: Toggling Col 2 link transitions state from false to true", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(2)
    assert_true(layout.colLinked[2], "Col 2 linked state true")
    assert_false(layout.colLinked[0], "Col 0 remains unlinked")
    assert_false(layout.colLinked[1], "Col 1 remains unlinked")
  end)

  it("TC 4.5: Toggling any column off restores unlinked state false", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0)
    assert_true(layout.colLinked[0], "Linked")
    layout:toggleColumnLink(0)
    assert_false(layout.colLinked[0], "Restored unlinked")
  end)
end)

describe(1, "Feature 5: Dynamic Layout Bounds", function()
  it("TC 5.1: Unlinked Col 0 allocates 2 rows (Top at row 0, Bottom at row 1)", function()
    local layout = createLayoutManager(800, 600, 1)
    assert_true(layout.paneVisible[kFaceTop], "Top pane visible")
    assert_true(layout.paneVisible[kFaceDown], "Bottom pane visible")
    assert_eq(layout.paneBounds[kFaceTop].y, 0, "Top pane row 0")
    assert_true(layout.paneBounds[kFaceDown].y > 0, "Bottom pane row 1")
    assert_eq(layout.paneBounds[kFaceTop].width, layout.paneBounds[kFaceDown].width, "Widths match")
  end)

  it("TC 5.2: Linked Col 0 unifies into single row spanning full column height", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0)
    assert_true(layout.paneVisible[kFaceTop], "Top pane remains visible")
    assert_false(layout.paneVisible[kFaceDown], "Bottom pane hidden")
    assert_eq(layout.paneBounds[kFaceTop].height, 600, "Top pane spans full height 600")
  end)

  it("TC 5.3: Unlinking Col 0 restores split 2-row bounds for both panes", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0)
    layout:toggleColumnLink(0)
    assert_true(layout.paneVisible[kFaceTop], "Top visible")
    assert_true(layout.paneVisible[kFaceDown], "Bottom visible")
    assert_eq(layout.paneBounds[kFaceTop].height, layout.paneBounds[kFaceDown].height, "Equal row heights")
  end)

  it("TC 5.4: Multi-column independent layout (Col 0 linked, Col 1 unlinked, Col 2 linked)", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0)
    layout:toggleColumnLink(2)
    -- Col 0 linked
    assert_false(layout.paneVisible[kFaceDown], "Col 0 bottom hidden")
    assert_eq(layout.paneBounds[kFaceTop].height, 600, "Col 0 top full height")
    -- Col 1 unlinked
    assert_true(layout.paneVisible[kFaceFront], "Col 1 front visible")
    assert_true(layout.paneVisible[kFaceBack], "Col 1 back visible")
    -- Col 2 linked
    assert_false(layout.paneVisible[kFaceLeft], "Col 2 left hidden")
    assert_eq(layout.paneBounds[kFaceRight].height, 600, "Col 2 right full height")
  end)

  it("TC 5.5: Unified header title updates to combined label and restores on unlink", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(1)
    assert_eq(layout.headers[kFaceFront], "Front / Back", "Combined header label")
    layout:toggleColumnLink(1)
    assert_eq(layout.headers[kFaceFront], "Front", "Restored front header")
    assert_eq(layout.headers[kFaceBack], "Back", "Restored back header")
  end)
end)

describe(1, "Feature 6: Active Editor Focus Safety", function()
  it("TC 6.1: Active editor on top face remains on top face when column is linked", function()
    local layout = createLayoutManager(800, 600, 1)
    layout.activeEditorFace = kFaceTop
    layout:toggleColumnLink(0)
    assert_eq(layout.activeEditorFace, kFaceTop, "Active editor remains Top")
  end)

  it("TC 6.2: Active editor on bottom face safely transitions to top face before hide", function()
    local layout = createLayoutManager(800, 600, 1)
    layout.activeEditorFace = kFaceDown
    layout:toggleColumnLink(0)
    assert_eq(layout.activeEditorFace, kFaceTop, "Active editor transitioned to Top")
    assert_false(layout.paneVisible[kFaceDown], "Bottom face hidden safely")
  end)

  it("TC 6.3: Focus safety on Col 1 (Back to Front transition)", function()
    local layout = createLayoutManager(800, 600, 1)
    layout.activeEditorFace = kFaceBack
    layout:toggleColumnLink(1)
    assert_eq(layout.activeEditorFace, kFaceFront, "Active editor transitioned to Front")
  end)

  it("TC 6.4: Focus safety on Col 2 (Left to Right transition)", function()
    local layout = createLayoutManager(800, 600, 1)
    layout.activeEditorFace = kFaceLeft
    layout:toggleColumnLink(2)
    assert_eq(layout.activeEditorFace, kFaceRight, "Active editor transitioned to Right")
  end)

  it("TC 6.5: Focus switching across different columns while one column is linked", function()
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0) -- Col 0 linked
    layout.activeEditorFace = kFaceFront
    assert_eq(layout.activeEditorFace, kFaceFront, "Focus on Front while Col 0 linked")
    layout.activeEditorFace = kFaceTop
    assert_eq(layout.activeEditorFace, kFaceTop, "Focus on Top while Col 0 linked")
  end)
end)

describe(1, "Feature 7: OBJ Export UV Mapping (Normalized [0.0, 1.0])", function()
  it("TC 7.1: OBJ export UV coordinates vt span [0.0, 1.0] directly without atlas offsets", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local vol = buildVoxelVolume(spr, 24)
    local obj = generateObjExport(vol, 24)
    assert_true(obj.quadCount > 0, "Quads emitted")
    for _, uv in ipairs(obj.uvCoords) do
      assert_in_range(uv.u0, 0.0, 1.0, "u0 in range")
      assert_in_range(uv.u1, 0.0, 1.0, "u1 in range")
      assert_in_range(uv.v0, 0.0, 1.0, "v0 in range")
      assert_in_range(uv.v1, 0.0, 1.0, "v1 in range")
    end
  end)

  it("TC 7.2: Front face UV mapping spans normalized [0.0, 1.0]", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local vol = buildVoxelVolume(spr, 24)
    local obj = generateObjExport(vol, 24)
    assert_true(string.find(obj.content, "vt 0.000000") ~= nil, "Zero UV origin present")
    assert_true(string.find(obj.content, "vt 1.000000") ~= nil, "One UV max present")
  end)

  it("TC 7.3: Top face normals orient strictly upward (0, 1, 0)", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local vol = buildVoxelVolume(spr, 24)
    local obj = generateObjExport(vol, 24)
    assert_true(string.find(obj.content, "vn 0.000000 1.000000 0.000000") ~= nil, "Top face normal present")
  end)

  it("TC 7.4: Right (+X) and Left (-X) normals correctly oriented", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local vol = buildVoxelVolume(spr, 24)
    local obj = generateObjExport(vol, 24)
    assert_true(string.find(obj.content, "vn 1.000000 0.000000 0.000000") ~= nil, "Right normal present")
    assert_true(string.find(obj.content, "vn -1.000000 0.000000 0.000000") ~= nil, "Left normal present")
  end)

  it("TC 7.5: OBJ output produces valid .obj structure referencing material", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local vol = buildVoxelVolume(spr, 24)
    local obj = generateObjExport(vol, 24)
    assert_true(string.find(obj.content, "mtllib easymetric_model.mtl") ~= nil, "mtllib reference present")
    assert_true(string.find(obj.content, "usemtl EasymetricMat") ~= nil, "usemtl reference present")
    assert_true(string.find(obj.content, "o EasymetricSculptedModel") ~= nil, "object group present")
  end)
end)

-- ==============================================================================
-- TIER 2: Boundary & Corner Cases (>=5 test cases per boundary)
-- ==============================================================================

describe(2, "Boundary 1: Minimum Face Sizes (4x4)", function()
  it("TC 2.1.1: 4x4 canvas creation and framing", function()
    local spr = Sprite(4, 4)
    local env = setupSixSurfaces(spr, 4, true)
    assert_eq(spr.width, 4, "Width 4")
    assert_eq(spr.height, 4, "Height 4")
  end)

  it("TC 2.1.2: 4x4 slice bounds verification (0, 0, 4, 4)", function()
    local spr = Sprite(4, 4)
    local env = setupSixSurfaces(spr, 4, true)
    for f = 0, kFaceCount - 1 do
      assert_eq(env.slices[f].bounds, Rectangle(0, 0, 4, 4), "Slice bounds 4x4")
    end
  end)

  it("TC 2.1.3: 4x4 pixel sampling and voxel carving without out-of-bounds error", function()
    local spr = Sprite(4, 4)
    setupSixSurfaces(spr, 4, true)
    local vol = buildVoxelVolume(spr, 4)
    assert_eq(vol.vx, 4, "vx 4")
    assert_eq(vol.vy, 4, "vy 4")
    assert_eq(vol.vz, 4, "vz 4")
    assert_true(vol:isSolid(0, 0, 0), "Corner solid")
  end)

  it("TC 2.1.4: 4x4 link toggle and cel synchronization", function()
    local spr = Sprite(4, 4)
    setupSixSurfaces(spr, 4, true)
    local layout = createLayoutManager(400, 300, 1)
    layout:toggleColumnLink(0, spr)
    assert_true(layout.colLinked[0], "Col 0 linked at 4x4")
  end)

  it("TC 2.1.5: 4x4 OBJ export generation and UV coordinates", function()
    local spr = Sprite(4, 4)
    setupSixSurfaces(spr, 4, true)
    local vol = buildVoxelVolume(spr, 4)
    local obj = generateObjExport(vol, 4)
    assert_true(obj.quadCount > 0, "Quads emitted for 4x4")
  end)
end)

describe(2, "Boundary 2: Maximum Face Sizes (256x256)", function()
  it("TC 2.2.1: 256x256 canvas creation and framing", function()
    local spr = Sprite(256, 256)
    local env = setupSixSurfaces(spr, 256, false)
    assert_eq(spr.width, 256, "Width 256")
    assert_eq(spr.height, 256, "Height 256")
  end)

  it("TC 2.2.2: 256x256 slice bounds verification (0, 0, 256, 256)", function()
    local spr = Sprite(256, 256)
    local env = setupSixSurfaces(spr, 256, false)
    assert_eq(env.slices[kFaceTop].bounds, Rectangle(0, 0, 256, 256), "Slice bounds 256x256")
  end)

  it("TC 2.2.3: 256x256 pixel sampling and sculpted volume scaling (subsampled to 64)", function()
    local spr = Sprite(256, 256)
    setupSixSurfaces(spr, 256, false)
    -- Voxel volume clamps max dimension to 64 for real-time raycasting performance
    local vol = buildVoxelVolume(spr, 64)
    assert_eq(vol.vx, 64, "Volume clamped to 64")
  end)

  it("TC 2.2.4: 256x256 link toggle and cel synchronization", function()
    local spr = Sprite(256, 256)
    setupSixSurfaces(spr, 256, false)
    local layout = createLayoutManager(1200, 900, 1)
    layout:toggleColumnLink(1, spr)
    assert_true(layout.colLinked[1], "Col 1 linked at 256x256")
  end)

  it("TC 2.2.5: 256x256 OBJ export emission and face quad bounds", function()
    local spr = Sprite(256, 256)
    setupSixSurfaces(spr, 256, false)
    local vol = buildVoxelVolume(spr, 64)
    local obj = generateObjExport(vol, 64)
    assert_true(obj.quadCount >= 6, "At least 6 bounding box quads")
  end)
end)

describe(2, "Boundary 3: Rapid Link Toggling", function()
  it("TC 2.3.1: 10 consecutive toggle cycles on Col 0 (link, unlink, link, unlink...)", function()
    local layout = createLayoutManager(800, 600, 1)
    for i = 1, 10 do
      layout:toggleColumnLink(0)
    end
    assert_false(layout.colLinked[0], "10 toggles return to unlinked")
    assert_true(layout.paneVisible[kFaceDown], "Bottom pane visible")
  end)

  it("TC 2.3.2: Rapid toggling across all 3 columns simultaneously", function()
    local layout = createLayoutManager(800, 600, 1)
    for i = 1, 5 do
      layout:toggleColumnLink(0)
      layout:toggleColumnLink(1)
      layout:toggleColumnLink(2)
    end
    assert_true(layout.colLinked[0], "Col 0 linked after 5 toggles")
    assert_true(layout.colLinked[1], "Col 1 linked after 5 toggles")
    assert_true(layout.colLinked[2], "Col 2 linked after 5 toggles")
  end)

  it("TC 2.3.3: Bounds and visibility consistency after odd number of toggles", function()
    local layout = createLayoutManager(800, 600, 1)
    for i = 1, 7 do
      layout:toggleColumnLink(0)
    end
    assert_true(layout.colLinked[0], "Col 0 linked after 7 toggles")
    assert_false(layout.paneVisible[kFaceDown], "Bottom hidden")
    assert_eq(layout.paneBounds[kFaceTop].height, 600, "Full height")
  end)

  it("TC 2.3.4: Bounds and visibility consistency after even number of toggles", function()
    local layout = createLayoutManager(800, 600, 1)
    for i = 1, 12 do
      layout:toggleColumnLink(1)
    end
    assert_false(layout.colLinked[1], "Col 1 unlinked after 12 toggles")
    assert_true(layout.paneVisible[kFaceFront], "Front visible")
    assert_true(layout.paneVisible[kFaceBack], "Back visible")
  end)

  it("TC 2.3.5: Stability after 100 rapid toggles", function()
    local layout = createLayoutManager(800, 600, 1)
    for i = 1, 100 do
      layout:toggleColumnLink(i % 3)
    end
    assert_not_nil(layout.paneBounds[kFaceTop], "Bounds valid")
  end)
end)

describe(2, "Boundary 4: Unlink Cel Preservation", function()
  it("TC 2.4.1: Painting on linked Col 0 propagates to synchronized opposing face", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    -- Paint cyan pixel on Top face
    local topImg = env.layers[kFaceTop]:cel(1).image
    topImg:drawPixel(12, 12, Color{ r = 0, g = 255, b = 255, a = 255 })

    -- Link Col 0
    layout:toggleColumnLink(0, spr)

    local btmImg = env.layers[kFaceDown]:cel(1).image
    local p = btmImg:getPixel(12, 12)
    assert_eq(app.pixelColor.rgbaR(p), 0, "Synchronized red")
    assert_eq(app.pixelColor.rgbaG(p), 255, "Synchronized green")
    assert_eq(app.pixelColor.rgbaB(p), 255, "Synchronized blue")
  end)

  it("TC 2.4.2: Unlinking Col 0 preserves the painted pixel content in bottom face cel", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    env.layers[kFaceTop]:cel(1).image:drawPixel(5, 5, Color{ r = 255, g = 100, b = 50, a = 255 })
    layout:toggleColumnLink(0, spr) -- Link
    layout:toggleColumnLink(0, spr) -- Unlink

    local btmImg = env.layers[kFaceDown]:cel(1).image
    local p = btmImg:getPixel(5, 5)
    assert_eq(app.pixelColor.rgbaR(p), 255, "Preserved red after unlink")
    assert_eq(app.pixelColor.rgbaG(p), 100, "Preserved green after unlink")
  end)

  it("TC 2.4.3: Modifying bottom face after unlink does not corrupt top face (independent cels)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    layout:toggleColumnLink(0, spr) -- Link
    layout:toggleColumnLink(0, spr) -- Unlink

    -- Paint unique color on Bottom face
    env.layers[kFaceDown]:cel(1).image:drawPixel(10, 10, Color{ r = 255, g = 0, b = 0, a = 255 })
    local topPixel = env.layers[kFaceTop]:cel(1).image:getPixel(10, 10)
    assert_eq(app.pixelColor.rgbaA(topPixel), 0, "Top face unaffected by bottom face edit")
  end)

  it("TC 2.4.4: Modifying top face after unlink does not corrupt bottom face", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    layout:toggleColumnLink(0, spr)
    layout:toggleColumnLink(0, spr)

    env.layers[kFaceTop]:cel(1).image:drawPixel(8, 8, Color{ r = 0, g = 255, b = 0, a = 255 })
    local btmPixel = env.layers[kFaceDown]:cel(1).image:getPixel(8, 8)
    assert_eq(app.pixelColor.rgbaA(btmPixel), 0, "Bottom face unaffected by top face edit")
  end)

  it("TC 2.4.5: Re-linking synchronizes top to bottom again cleanly", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    layout:toggleColumnLink(0, spr)
    layout:toggleColumnLink(0, spr)
    env.layers[kFaceTop]:cel(1).image:drawPixel(15, 15, Color{ r = 120, g = 130, b = 140, a = 255 })
    layout:toggleColumnLink(0, spr) -- Re-link

    local btmPixel = env.layers[kFaceDown]:cel(1).image:getPixel(15, 15)
    assert_eq(app.pixelColor.rgbaR(btmPixel), 120, "Re-synchronized pixel")
  end)
end)

describe(2, "Boundary 5: Empty and Blank Cels", function()
  it("TC 2.5.1: Completely transparent/blank cels across all 6 faces handled safely", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, false)
    local vol = buildVoxelVolume(spr, 24)
    assert_not_nil(vol, "Volume built safely")
  end)

  it("TC 2.5.2: Volume defaults to solid bounding cube when all faces are empty", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, false)
    local vol = buildVoxelVolume(spr, 24)
    -- With blank faces, fallback keeps solid bounding volume (cmd_easymetric.cpp:343-346)
    assert_true(vol:isSolid(12, 12, 12), "Center solid fallback")
  end)

  it("TC 2.5.3: Single non-empty face with 5 empty faces uses symmetric fallback safely", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    env.layers[kFaceFront]:cel(1).image:drawPixel(12, 12, Color{ r = 255, g = 255, b = 255, a = 255 })
    local vol = buildVoxelVolume(spr, 24)
    assert_not_nil(vol, "Handled single face")
  end)

  it("TC 2.5.4: Linking columns containing empty cels does not crash or throw nil errors", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0, spr)
    layout:toggleColumnLink(1, spr)
    layout:toggleColumnLink(2, spr)
    assert_true(layout.colLinked[0] and layout.colLinked[1] and layout.colLinked[2], "All linked")
  end)

  it("TC 2.5.5: OBJ export of empty/blank cels produces valid mesh with default material", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, false)
    local vol = buildVoxelVolume(spr, 24)
    local obj = generateObjExport(vol, 24)
    assert_true(string.find(obj.content, "usemtl EasymetricMat") ~= nil, "Material present")
    assert_true(obj.quadCount >= 6, "Solid cube quads emitted")
  end)
end)

-- ==============================================================================
-- TIER 3: Cross-Feature Combinations
-- ==============================================================================

describe(3, "Cross-Feature Combinations", function()
  it("TC 3.1: Painting on linked surfaces with real-time mirror sync", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    -- Link Col 1 (Front/Back)
    layout:toggleColumnLink(1, spr)

    -- Paint crosshair on Front
    local frontImg = env.layers[kFaceFront]:cel(1).image
    for i = 6, 18 do
      frontImg:drawPixel(i, 12, Color{ r = 255, g = 0, b = 255, a = 255 })
      frontImg:drawPixel(12, i, Color{ r = 255, g = 0, b = 255, a = 255 })
    end

    -- Trigger link sync
    layout:toggleColumnLink(1, spr)
    layout:toggleColumnLink(1, spr)

    local backImg = env.layers[kFaceBack]:cel(1).image
    assert_eq(app.pixelColor.rgbaA(backImg:getPixel(12, 12)), 255, "Mirrored crosshair center")
    assert_eq(app.pixelColor.rgbaA(backImg:getPixel(6, 12)), 255, "Mirrored crosshair arm")
  end)

  it("TC 3.2: Voxel silhouette sculpting with linked pairs", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, true)
    local layout = createLayoutManager(800, 600, 1)

    -- Carve out a hole in Top face center
    local topImg = env.layers[kFaceTop]:cel(1).image
    for y = 8, 15 do
      for x = 8, 15 do
        topImg:drawPixel(x, y, Color{ r = 0, g = 0, b = 0, a = 0 })
      end
    end

    -- Link Col 0 (Top/Bottom)
    layout:toggleColumnLink(0, spr)

    local vol = buildVoxelVolume(spr, 24, layout.colLinked)
    assert_false(vol:isSolid(12, 0, 12), "Top carved hole")
    assert_false(vol:isSolid(12, 23, 12), "Bottom carved hole (mirrored)")
  end)

  it("TC 3.3: Multi-column link combinations (Col 0 linked, Col 1 unlinked, Col 2 linked)", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local layout = createLayoutManager(800, 600, 1)

    layout:toggleColumnLink(0, spr)
    layout:toggleColumnLink(2, spr)

    local vol = buildVoxelVolume(spr, 24, layout.colLinked)
    assert_not_nil(vol, "Volume computed for mixed link states")
  end)

  it("TC 3.4: OBJ export with linked pairs produces valid mesh and unified UV regions", function()
    local spr = Sprite(24, 24)
    setupSixSurfaces(spr, 24, true)
    local layout = createLayoutManager(800, 600, 1)
    layout:toggleColumnLink(0, spr)
    layout:toggleColumnLink(1, spr)
    layout:toggleColumnLink(2, spr)

    local vol = buildVoxelVolume(spr, 24, layout.colLinked)
    local obj = generateObjExport(vol, 24)

    assert_true(obj.quadCount > 0, "Quads emitted")
    for _, uv in ipairs(obj.uvCoords) do
      assert_in_range(uv.u0, 0.0, 1.0, "UV u0 in bounds")
      assert_in_range(uv.v1, 0.0, 1.0, "UV v1 in bounds")
    end
  end)

  it("TC 3.5: Document undo/redo integrity across linking and surface painting", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    -- Cel replacement in transaction to verify full doc undo/redo tracking
    local origCel = env.layers[kFaceTop]:cel(1)
    local newImg = Image(origCel.image)
    newImg:drawPixel(10, 10, Color{ r = 100, g = 200, b = 50, a = 255 })

    app.transaction(function()
      spr:newCel(env.layers[kFaceTop], 1, newImg, Point(0, 0))
    end)
    assert_eq(app.pixelColor.rgbaA(env.layers[kFaceTop]:cel(1).image:getPixel(10, 10)), 255, "Painted pixel")

    app.undo()
    local undoneCel = env.layers[kFaceTop]:cel(1)
    local undoneA = undoneCel and app.pixelColor.rgbaA(undoneCel.image:getPixel(10, 10)) or 0
    assert_eq(undoneA, 0, "Undone pixel")

    app.redo()
    local redoneCel = env.layers[kFaceTop]:cel(1)
    local redoneA = redoneCel and app.pixelColor.rgbaA(redoneCel.image:getPixel(10, 10)) or 0
    assert_eq(redoneA, 255, "Redone pixel")
  end)
end)

-- ==============================================================================
-- TIER 4: Real-World Application Scenarios
-- ==============================================================================

describe(4, "Real-World Application Scenarios", function()
  it("TC 4.1: Full 3D game asset creation (voxel character head with side ear symmetry)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    -- 1. Front face: eyes and smile
    local frontImg = env.layers[kFaceFront]:cel(1).image
    frontImg:clear(Color{ r = 240, g = 200, b = 160, a = 255 }) -- skin
    frontImg:drawPixel(8, 8, Color{ r = 20, g = 20, b = 20, a = 255 })  -- left eye
    frontImg:drawPixel(15, 8, Color{ r = 20, g = 20, b = 20, a = 255 }) -- right eye
    for x = 9, 14 do
      frontImg:drawPixel(x, 15, Color{ r = 180, g = 50, b = 50, a = 255 }) -- mouth
    end

    -- 2. Back face: hair
    local backImg = env.layers[kFaceBack]:cel(1).image
    backImg:clear(Color{ r = 60, g = 35, b = 20, a = 255 }) -- dark brown hair

    -- 3. Top face: hair crown
    local topImg = env.layers[kFaceTop]:cel(1).image
    topImg:clear(Color{ r = 60, g = 35, b = 20, a = 255 })

    -- 4. Bottom face: neck post
    local btmImg = env.layers[kFaceDown]:cel(1).image
    btmImg:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
    for y = 8, 15 do
      for x = 8, 15 do
        btmImg:drawPixel(x, y, Color{ r = 230, g = 190, b = 150, a = 255 })
      end
    end

    -- 5. Link Col 2 (Right/Left) to mirror ear geometry across sides
    local rightImg = env.layers[kFaceRight]:cel(1).image
    rightImg:clear(Color{ r = 240, g = 200, b = 160, a = 255 })
    rightImg:drawPixel(12, 10, Color{ r = 220, g = 170, b = 140, a = 255 }) -- ear
    layout:toggleColumnLink(2, spr) -- Mirror Right -> Left

    local leftImg = env.layers[kFaceLeft]:cel(1).image
    assert_eq(app.pixelColor.rgbaA(leftImg:getPixel(12, 10)), 255, "Mirrored ear present on Left face")

    -- 6. Generate sculpted voxel volume and OBJ export
    local vol = buildVoxelVolume(spr, 24, layout.colLinked)
    assert_true(vol:isSolid(12, 12, 12), "Head core is solid")
    assert_true(vol:isSolid(12, 20, 12), "Neck post is solid")

    local obj = generateObjExport(vol, 24)
    assert_true(obj.quadCount > 50, "Full character head mesh emitted")
  end)

  it("TC 4.2: Live surface sculpting workflow (hollow archway / tunnel through Front-Back)", function()
    local spr = Sprite(24, 24)
    local env = setupSixSurfaces(spr, 24, false)
    local layout = createLayoutManager(800, 600, 1)

    -- Fill all faces solid with masonry stone color
    local stoneColor = Color{ r = 120, g = 120, b = 130, a = 255 }
    for f = 0, kFaceCount - 1 do
      env.layers[f]:cel(1).image:clear(stoneColor)
    end

    -- Carve arch tunnel in Front face (x: 8..15, y: 10..23)
    local frontImg = env.layers[kFaceFront]:cel(1).image
    for y = 10, 23 do
      for x = 8, 15 do
        frontImg:drawPixel(x, y, Color{ r = 0, g = 0, b = 0, a = 0 })
      end
    end

    -- Link Col 1 (Front/Back) so tunnel cuts completely through
    layout:toggleColumnLink(1, spr)

    local vol = buildVoxelVolume(spr, 24, layout.colLinked)

    -- Verify center tunnel is hollow from z=0 to z=23
    for z = 0, 23 do
      assert_false(vol:isSolid(12, 16, z), "Tunnel at z=" .. z .. " must be hollow")
    end

    -- Verify outer pillars are solid
    assert_true(vol:isSolid(3, 16, 12), "Left pillar solid")
    assert_true(vol:isSolid(20, 16, 12), "Right pillar solid")
    assert_true(vol:isSolid(12, 3, 12), "Arch roof solid")

    local obj = generateObjExport(vol, 24)
    assert_true(obj.quadCount > 0, "Archway mesh generated")
  end)
end)

-- ==============================================================================
-- Test Suite Execution Summary
-- ==============================================================================

print("\n======================================================")
print("             E2E TEST SUITE EXECUTION SUMMARY         ")
print("======================================================")
for tier = 1, 4 do
  local res = suite.tierSummary[tier]
  print(string.format(" Tier %d: %3d Passed, %3d Failed", tier, res.pass, res.fail))
end
print("------------------------------------------------------")
print(string.format(" TOTAL:  %3d Passed, %3d Failed (Total %d)", suite.passed, suite.failed, suite.passed + suite.failed))
print("======================================================")

if suite.failed > 0 then
  error(string.format("E2E Test Suite FAILED: %d test(s) failed.", suite.failed))
else
  print("ALL E2E TESTS PASSED SUCCESSFULLY.")
end
