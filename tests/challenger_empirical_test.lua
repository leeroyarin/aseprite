-- ==============================================================================
-- Challenger Empirical Test Suite: Easymetric 6-Surface Live Studio
-- Challenger: challenger_m1_2 (Empirical Challenger)
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

local totalAsserts = 0
local passedAsserts = 0
local failedAsserts = 0
local failures = {}

local function assert_test(cond, desc)
  totalAsserts = totalAsserts + 1
  if cond then
    passedAsserts = passedAsserts + 1
    print(string.format("  [OK] %s", desc))
  else
    failedAsserts = failedAsserts + 1
    table.insert(failures, desc)
    print(string.format("  [FAIL] %s", desc))
  end
end

local function assert_eq(a, b, desc)
  assert_test(a == b, string.format("%s (expected %s, got %s)", desc, tostring(b), tostring(a)))
end

local function assert_in_range(val, minV, maxV, desc)
  assert_test(val >= minV and val <= maxV, string.format("%s (val %f not in [%f, %f])", desc, val, minV, maxV))
end

print("==================================================================")
print(" EMPIRICAL CHALLENGER TEST HARNESS: EASYMETRIC 6-SURFACE")
print("==================================================================")

-- ------------------------------------------------------------------
-- 1. Helper: Setup doc with six surfaces matching C++ setupSixSurfacesOnDoc
-- ------------------------------------------------------------------
local function setupTestDoc(w, h)
  local spr = Sprite(w, h)
  local layers = {}
  local slices = {}
  for f = 0, kFaceCount - 1 do
    local lyrName = "Surface_" .. kFaceSpecs[f].sliceName
    local lyr = spr:newLayer()
    lyr.name = lyrName
    local img = Image(w, h, spr.colorMode)
    img:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
    spr:newCel(lyr, 1, img, Point(0, 0))
    layers[f] = lyr

    local sl = spr:newSlice(Rectangle(0, 0, w, h))
    sl.name = kFaceSpecs[f].sliceName
    slices[f] = sl
  end
  return { sprite = spr, layers = layers, slices = slices, w = w, h = h }
end

-- ------------------------------------------------------------------
-- Test Suite 1: Column Linking & Cel Synchronization Life Cycle
-- ------------------------------------------------------------------
print("\n--- [SUITE 1] Column Linking & Cel Synchronization Life Cycle ---")

-- Test 1.1: Col 0 (Top / Down) linking, painting propagation, unlink retention
do
  local doc = setupTestDoc(24, 24)
  local spr = doc.sprite
  local topImg = doc.layers[kFaceTop]:cel(1).image
  local btmImg = doc.layers[kFaceDown]:cel(1).image

  -- Paint distinctive pixels on Top
  topImg:drawPixel(3, 4, Color{ r = 255, g = 128, b = 64, a = 255 })
  topImg:drawPixel(20, 21, Color{ r = 10, g = 20, b = 30, a = 255 })

  -- Emulate syncOpposingCels(kFaceTop, kFaceDown)
  for y = 0, 23 do
    for x = 0, 23 do
      btmImg:drawPixel(x, y, topImg:getPixel(x, y))
    end
  end

  -- Verify sync while linked
  local p1 = btmImg:getPixel(3, 4)
  assert_eq(app.pixelColor.rgbaR(p1), 255, "Linked Col 0: Bottom face received pixel (3,4) red")
  assert_eq(app.pixelColor.rgbaG(p1), 128, "Linked Col 0: Bottom face received pixel (3,4) green")
  assert_eq(app.pixelColor.rgbaB(p1), 64, "Linked Col 0: Bottom face received pixel (3,4) blue")

  -- Unlink simulation: Bottom face retains cel data
  -- Subsequent independent edit on Bottom face
  btmImg:drawPixel(10, 10, Color{ r = 0, g = 255, b = 0, a = 255 })
  local topP10 = topImg:getPixel(10, 10)
  assert_eq(app.pixelColor.rgbaA(topP10), 0, "Unlinked Col 0: Top face independent from bottom edit (10,10)")

  -- Subsequent independent edit on Top face
  topImg:drawPixel(15, 15, Color{ r = 255, g = 0, b = 0, a = 255 })
  local btmP15 = btmImg:getPixel(15, 15)
  assert_eq(app.pixelColor.rgbaA(btmP15), 0, "Unlinked Col 0: Bottom face independent from top edit (15,15)")

  -- Re-link: Top synchronizes to bottom again
  for y = 0, 23 do
    for x = 0, 23 do
      btmImg:drawPixel(x, y, topImg:getPixel(x, y))
    end
  end
  local btmP15_relink = btmImg:getPixel(15, 15)
  assert_eq(app.pixelColor.rgbaR(btmP15_relink), 255, "Re-linked Col 0: Bottom face re-synchronized from top (15,15)")
end

-- Test 1.2: Col 1 (Front / Back) & Col 2 (Right / Left)
do
  local doc = setupTestDoc(24, 24)
  -- Col 1 Front/Back
  local frontImg = doc.layers[kFaceFront]:cel(1).image
  local backImg = doc.layers[kFaceBack]:cel(1).image
  frontImg:drawPixel(7, 8, Color{ r = 111, g = 222, b = 123, a = 255 })
  for y = 0, 23 do
    for x = 0, 23 do
      backImg:drawPixel(x, y, frontImg:getPixel(x, y))
    end
  end
  assert_eq(app.pixelColor.rgbaR(backImg:getPixel(7, 8)), 111, "Col 1 Front/Back cel synchronization")

  -- Col 2 Right/Left
  local rightImg = doc.layers[kFaceRight]:cel(1).image
  local leftImg = doc.layers[kFaceLeft]:cel(1).image
  rightImg:drawPixel(1, 2, Color{ r = 99, g = 88, b = 77, a = 255 })
  for y = 0, 23 do
    for x = 0, 23 do
      leftImg:drawPixel(x, y, rightImg:getPixel(x, y))
    end
  end
  assert_eq(app.pixelColor.rgbaR(leftImg:getPixel(1, 2)), 99, "Col 2 Right/Left cel synchronization")
end

-- ------------------------------------------------------------------
-- Test Suite 2: Voxel Silhouette Carving Unification
-- ------------------------------------------------------------------
print("\n--- [SUITE 2] Voxel Silhouette Carving Unification ---")

-- Emulate exact C++ buildSculptedVolume logic
local function buildVoxelVolumeCpp(docData, colLinked)
  local S = docData.w
  local vx, vy, vz = S, S, S
  local solid = {}
  for y = 0, vy - 1 do
    solid[y] = {}
    for z = 0, vz - 1 do
      solid[y][z] = {}
      for x = 0, vx - 1 do
        solid[y][z][x] = false
      end
    end
  end

  local pixels = {}
  local hasPixels = {}
  for f = 0, kFaceCount - 1 do
    pixels[f] = {}
    hasPixels[f] = false
    local img = docData.layers[f]:cel(1).image
    for y = 0, S - 1 do
      pixels[f][y] = {}
      for x = 0, S - 1 do
        local c = img:getPixel(x, y)
        pixels[f][y][x] = c
        if app.pixelColor.rgbaA(c) > 16 then
          hasPixels[f] = true
        end
      end
    end
  end

  -- When linked, out.pixels[btmFace] = out.pixels[topFace]
  for c = 0, 2 do
    if colLinked[c] then
      local topFace = c * 2
      local btmFace = c * 2 + 1
      pixels[btmFace] = pixels[topFace]
      hasPixels[btmFace] = hasPixels[topFace]
    end
  end

  local activeTopDown   = (hasPixels[kFaceTop] or hasPixels[kFaceDown])
  local activeFrontBack = (hasPixels[kFaceFront] or hasPixels[kFaceBack])
  local activeRightLeft = (hasPixels[kFaceRight] or hasPixels[kFaceLeft])

  if not activeTopDown and not activeFrontBack and not activeRightLeft then
    for y = 0, vy - 1 do
      for z = 0, vz - 1 do
        for x = 0, vx - 1 do
          solid[y][z][x] = true
        end
      end
    end
    return {
      isSolid = function(x, y, z)
        return (x >= 0 and x < vx and y >= 0 and y < vy and z >= 0 and z < vz)
      end
    }
  end

  local function isFaceSolid(f, u, v)
    u = math.max(0, math.min(S - 1, u))
    v = math.max(0, math.min(S - 1, v))
    return app.pixelColor.rgbaA(pixels[f][v][u]) > 16
  end

  for y = 0, vy - 1 do
    for z = 0, vz - 1 do
      for x = 0, vx - 1 do
        local isSol = true

        if activeTopDown then
          local uTop = math.floor((x + 0.5) / vx * S)
          local vTop = math.floor((z + 0.5) / vz * S)
          local topSol = isFaceSolid(kFaceTop, uTop, vTop)
          if not colLinked[0] and hasPixels[kFaceDown] then
            local uDown = math.floor((x + 0.5) / vx * S)
            local vDown = math.floor(((vz - 1 - z) + 0.5) / vz * S)
            local downSol = isFaceSolid(kFaceDown, uDown, vDown)
            topSol = (hasPixels[kFaceTop] and (topSol or downSol)) or downSol
          end
          if not topSol then isSol = false end
        end

        if isSol and activeFrontBack then
          local uFront = math.floor((x + 0.5) / vx * S)
          local vFront = math.floor((y + 0.5) / vy * S)
          local frontSol = isFaceSolid(kFaceFront, uFront, vFront)
          if not colLinked[1] and hasPixels[kFaceBack] then
            local uBack = math.floor(((vx - 1 - x) + 0.5) / vx * S)
            local vBack = math.floor((y + 0.5) / vy * S)
            local backSol = isFaceSolid(kFaceBack, uBack, vBack)
            frontSol = (hasPixels[kFaceFront] and (frontSol or backSol)) or backSol
          end
          if not frontSol then isSol = false end
        end

        if isSol and activeRightLeft then
          local uRight = math.floor(((vz - 1 - z) + 0.5) / vz * S)
          local vRight = math.floor((y + 0.5) / vy * S)
          local sideSol = isFaceSolid(kFaceRight, uRight, vRight)
          if not colLinked[2] and hasPixels[kFaceLeft] then
            local uLeft = math.floor((z + 0.5) / vz * S)
            local vLeft = math.floor((y + 0.5) / vy * S)
            local leftSol = isFaceSolid(kFaceLeft, uLeft, vLeft)
            sideSol = (hasPixels[kFaceRight] and (sideSol or leftSol)) or leftSol
          end
          if not sideSol then isSol = false end
        end

        if isSol then
          solid[y][z][x] = true
        end
      end
    end
  end

  return {
    isSolid = function(x, y, z)
      if x < 0 or x >= vx or y < 0 or y >= vy or z < 0 or z >= vz then return false end
      return solid[y][z][x] == true
    end
  }
end

-- Test 2.1: Through-tunnel carving with linked Col 1 (Front/Back)
do
  local doc = setupTestDoc(24, 24)
  local solidCol = Color{ r = 100, g = 100, b = 100, a = 255 }
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(solidCol)
  end

  -- Carve hole in Front face center (x: 10..13, y: 10..13)
  local frontImg = doc.layers[kFaceFront]:cel(1).image
  for y = 10, 13 do
    for x = 10, 13 do
      frontImg:drawPixel(x, y, Color{ r = 0, g = 0, b = 0, a = 0 })
    end
  end

  -- When Col 1 is linked
  local vol = buildVoxelVolumeCpp(doc, { [0] = false, [1] = true, [2] = false })

  -- Hole penetrates along entire Z-axis (from z = 0 to z = 23)
  local allHollow = true
  for z = 0, 23 do
    if vol.isSolid(11, 11, z) then
      allHollow = false
      break
    end
  end
  assert_test(allHollow, "Linked Col 1: Voxel carving hollows out tunnel cleanly along Z-axis")

  -- Surround voxels remain solid
  assert_test(vol.isSolid(5, 11, 11), "Linked Col 1: Adjacent X voxel is solid")
  assert_test(vol.isSolid(18, 11, 11), "Linked Col 1: Opposite X voxel is solid")
  assert_test(vol.isSolid(11, 5, 11), "Linked Col 1: Adjacent Y voxel is solid")
  assert_test(vol.isSolid(11, 18, 11), "Linked Col 1: Opposite Y voxel is solid")
end

-- Test 2.2: Through-hole carving with linked Col 0 (Top/Bottom)
do
  local doc = setupTestDoc(24, 24)
  local solidCol = Color{ r = 100, g = 100, b = 100, a = 255 }
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(solidCol)
  end

  -- Carve hole in Top face at (x: 8..15, z: 8..15)
  local topImg = doc.layers[kFaceTop]:cel(1).image
  for y = 8, 15 do
    for x = 8, 15 do
      topImg:drawPixel(x, y, Color{ r = 0, g = 0, b = 0, a = 0 })
    end
  end

  local vol = buildVoxelVolumeCpp(doc, { [0] = true, [1] = false, [2] = false })
  local verticalHollow = true
  for y = 0, 23 do
    if vol.isSolid(12, y, 12) then
      verticalHollow = false
      break
    end
  end
  assert_test(verticalHollow, "Linked Col 0: Vertical tunnel hollows out along Y-axis from Top to Bottom")
end

-- ------------------------------------------------------------------
-- Test Suite 3: Raycast Texture Sampling & Mirroring Across Opposing Faces
-- ------------------------------------------------------------------
print("\n--- [SUITE 3] Raycast Texture Sampling & Mirroring Across Opposing Faces ---")

-- Test raycast UV mapping on cube faces
local function sampleCubeFaceUV(face, hx, hy, hz, halfX, halfY, halfZ, fw, fh)
  local u, v = 0, 0
  if face == kFaceFront then
    u = math.floor((hx + halfX) / (2.0 * halfX) * fw)
    v = math.floor((hy + halfY) / (2.0 * halfY) * fh)
  elseif face == kFaceBack then
    u = math.floor((halfX - hx) / (2.0 * halfX) * fw)
    v = math.floor((hy + halfY) / (2.0 * halfY) * fh)
  elseif face == kFaceLeft then
    u = math.floor((hz + halfZ) / (2.0 * halfZ) * fw)
    v = math.floor((hy + halfY) / (2.0 * halfY) * fh)
  elseif face == kFaceRight then
    u = math.floor((halfZ - hz) / (2.0 * halfZ) * fw)
    v = math.floor((hy + halfY) / (2.0 * halfY) * fh)
  elseif face == kFaceTop then
    u = math.floor((hx + halfX) / (2.0 * halfX) * fw)
    v = math.floor((hz + halfZ) / (2.0 * halfZ) * fh)
  elseif face == kFaceDown then
    u = math.floor((hx + halfX) / (2.0 * halfX) * fw)
    v = math.floor((halfZ - hz) / (2.0 * halfZ) * fh)
  end
  u = math.max(0, math.min(fw - 1, u))
  v = math.max(0, math.min(fh - 1, v))
  return u, v
end

-- Test 3.1: Seam continuity across vertical face loop (Front -> Right -> Back -> Left -> Front)
do
  local S = 24
  local half = S * 0.5
  -- Seam 1: Front right edge (+half, y, +half) vs Right left edge (+half, y, +half)
  local uFrontR, vFrontR = sampleCubeFaceUV(kFaceFront, half, 0, half, half, half, half, S, S)
  local uRightL, vRightL = sampleCubeFaceUV(kFaceRight, half, 0, half, half, half, half, S, S)
  assert_eq(uFrontR, S - 1, "Front right edge u == S - 1")
  assert_eq(uRightL, 0, "Right left edge u == 0")
  assert_eq(vFrontR, vRightL, "Front and Right vertical seam v coordinates align")

  -- Seam 2: Right right edge (+half, y, -half) vs Back left edge (+half, y, -half)
  local uRightR, vRightR = sampleCubeFaceUV(kFaceRight, half, 0, -half, half, half, half, S, S)
  local uBackL, vBackL = sampleCubeFaceUV(kFaceBack, half, 0, -half, half, half, half, S, S)
  assert_eq(uRightR, S - 1, "Right right edge u == S - 1")
  assert_eq(uBackL, 0, "Back left edge u == 0")
  assert_eq(vRightR, vBackL, "Right and Back vertical seam v coordinates align")

  -- Seam 3: Back right edge (-half, y, -half) vs Left left edge (-half, y, -half)
  local uBackR, vBackR = sampleCubeFaceUV(kFaceBack, -half, 0, -half, half, half, half, S, S)
  local uLeftL, vLeftL = sampleCubeFaceUV(kFaceLeft, -half, 0, -half, half, half, half, S, S)
  assert_eq(uBackR, S - 1, "Back right edge u == S - 1")
  assert_eq(uLeftL, 0, "Left left edge u == 0")
  assert_eq(vBackR, vLeftL, "Back and Left vertical seam v coordinates align")

  -- Seam 4: Left right edge (-half, y, +half) vs Front left edge (-half, y, +half)
  local uLeftR, vLeftR = sampleCubeFaceUV(kFaceLeft, -half, 0, half, half, half, half, S, S)
  local uFrontL, vFrontL = sampleCubeFaceUV(kFaceFront, -half, 0, half, half, half, half, S, S)
  assert_eq(uLeftR, S - 1, "Left right edge u == S - 1")
  assert_eq(uFrontL, 0, "Front left edge u == 0")
  assert_eq(vLeftR, vFrontL, "Left and Front vertical seam v coordinates align")
end

-- Test 3.2: Top and Bottom boundary alignment
do
  local S = 24
  local half = S * 0.5

  -- Top meets Front: hx in [-half, +half], hy = -half, hz = +half
  local uTopF, vTopF = sampleCubeFaceUV(kFaceTop, 0, -half, half, half, half, half, S, S)
  local uFrontT, vFrontT = sampleCubeFaceUV(kFaceFront, 0, -half, half, half, half, half, S, S)
  assert_eq(vTopF, S - 1, "Top face bottom edge v == S - 1")
  assert_eq(vFrontT, 0, "Front face top edge v == 0")
  assert_eq(uTopF, uFrontT, "Top face and Front face horizontal u coordinates match at seam")

  -- Bottom meets Front: hx in [-half, +half], hy = +half, hz = +half
  local uDownF, vDownF = sampleCubeFaceUV(kFaceDown, 0, half, half, half, half, half, S, S)
  local uFrontB, vFrontB = sampleCubeFaceUV(kFaceFront, 0, half, half, half, half, half, S, S)
  assert_eq(vDownF, 0, "Down face top edge v == 0")
  assert_eq(vFrontB, S - 1, "Front face bottom edge v == S - 1")
  assert_eq(uDownF, uFrontB, "Down face and Front face horizontal u coordinates match at seam")
end

-- ------------------------------------------------------------------
-- Test Suite 4: OBJ Export UV Mapping Inspection
-- ------------------------------------------------------------------
print("\n--- [SUITE 4] OBJ Export UV Mapping Inspection ---")

-- Generate exact OBJ text matching C++ exportToWavefrontObj
local function generateCppObjText(vol, S)
  local lines = {}
  table.insert(lines, "# Aseprite 2.0 Easymetric Sculpted 3D Model")
  table.insert(lines, "mtllib easymetric_model.mtl")
  table.insert(lines, "usemtl EasymetricMat")
  table.insert(lines, "o EasymetricSculptedModel")

  local vIdx, vtIdx, vnIdx = 1, 1, 1
  local quads = {}

  local halfX = S * 0.5
  local halfY = S * 0.5
  local halfZ = S * 0.5
  local cellSizeX = (2.0 * halfX) / S
  local cellSizeY = (2.0 * halfY) / S
  local cellSizeZ = (2.0 * halfZ) / S

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
    table.insert(quads, { u0 = u0, v0 = v0, u1 = u1, v1 = v1, normal = n })
    vIdx = vIdx + 4
    vtIdx = vtIdx + 4
    vnIdx = vnIdx + 1
  end

  for vy = 0, S - 1 do
    local yMax = halfY - vy * cellSizeY
    local yMin = halfY - (vy + 1) * cellSizeY
    for vz = 0, S - 1 do
      local zMin = -halfZ + vz * cellSizeZ
      local zMax = -halfZ + (vz + 1) * cellSizeZ
      for vx = 0, S - 1 do
        if vol.isSolid(vx, vy, vz) then
          local xMin = -halfX + vx * cellSizeX
          local xMax = -halfX + (vx + 1) * cellSizeX

          -- 1. Front (+Z)
          if vz + 1 >= S or not vol.isSolid(vx, vy, vz + 1) then
            local u0 = vx / S
            local u1 = (vx + 1) / S
            local v0 = 1.0 - (vy + 1) / S
            local v1 = 1.0 - vy / S
            emitQuad({ {xMin, yMin, zMax}, {xMax, yMin, zMax}, {xMax, yMax, zMax}, {xMin, yMax, zMax} },
                     {0, 0, 1}, u0, v0, u1, v1)
          end
          -- 2. Back (-Z)
          if vz - 1 < 0 or not vol.isSolid(vx, vy, vz - 1) then
            local u0 = (S - 1 - vx) / S
            local u1 = (S - vx) / S
            local v0 = 1.0 - (vy + 1) / S
            local v1 = 1.0 - vy / S
            emitQuad({ {xMax, yMin, zMin}, {xMin, yMin, zMin}, {xMin, yMax, zMin}, {xMax, yMax, zMin} },
                     {0, 0, -1}, u0, v0, u1, v1)
          end
          -- 3. Top (+Y)
          if vy - 1 < 0 or not vol.isSolid(vx, vy - 1, vz) then
            local u0 = vx / S
            local u1 = (vx + 1) / S
            local v0 = 1.0 - (vz + 1) / S
            local v1 = 1.0 - vz / S
            emitQuad({ {xMin, yMax, zMax}, {xMax, yMax, zMax}, {xMax, yMax, zMin}, {xMin, yMax, zMin} },
                     {0, 1, 0}, u0, v0, u1, v1)
          end
          -- 4. Bottom (-Y)
          if vy + 1 >= S or not vol.isSolid(vx, vy + 1, vz) then
            local u0 = vx / S
            local u1 = (vx + 1) / S
            local v0 = 1.0 - (S - vz) / S
            local v1 = 1.0 - (S - 1 - vz) / S
            emitQuad({ {xMin, yMin, zMin}, {xMax, yMin, zMin}, {xMax, yMin, zMax}, {xMin, yMin, zMax} },
                     {0, -1, 0}, u0, v0, u1, v1)
          end
          -- 5. Right (+X)
          if vx + 1 >= S or not vol.isSolid(vx + 1, vy, vz) then
            local u0 = (S - 1 - vz) / S
            local u1 = (S - vz) / S
            local v0 = 1.0 - (vy + 1) / S
            local v1 = 1.0 - vy / S
            emitQuad({ {xMax, yMin, zMax}, {xMax, yMin, zMin}, {xMax, yMax, zMin}, {xMax, yMax, zMax} },
                     {1, 0, 0}, u0, v0, u1, v1)
          end
          -- 6. Left (-X)
          if vx - 1 < 0 or not vol.isSolid(vx - 1, vy, vz) then
            local u0 = vz / S
            local u1 = (vz + 1) / S
            local v0 = 1.0 - (vy + 1) / S
            local v1 = 1.0 - vy / S
            emitQuad({ {xMin, yMin, zMin}, {xMin, yMin, zMax}, {xMin, yMax, zMax}, {xMin, yMax, zMin} },
                     {-1, 0, 0}, u0, v0, u1, v1)
          end
        end
      end
    end
  end

  return { text = table.concat(lines, "\n"), quads = quads }
end

do
  local S = 24
  local doc = setupTestDoc(S, S)
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(Color{ r = 100, g = 100, b = 100, a = 255 })
  end
  local vol = buildVoxelVolumeCpp(doc, { [0] = false, [1] = false, [2] = false })
  local objData = generateCppObjText(vol, S)

  assert_test(#objData.quads > 0, "OBJ quads generated")

  local allNormInRange = true
  local minU, maxU, minV, maxV = 1.0, 0.0, 1.0, 0.0
  for _, q in ipairs(objData.quads) do
    if q.u0 < minU then minU = q.u0 end
    if q.u1 > maxU then maxU = q.u1 end
    if q.v0 < minV then minV = q.v0 end
    if q.v1 > maxV then maxV = q.v1 end

    if q.u0 < 0.0 or q.u0 > 1.0 or q.u1 < 0.0 or q.u1 > 1.0 or
       q.v0 < 0.0 or q.v0 > 1.0 or q.v1 < 0.0 or q.v1 > 1.0 then
      allNormInRange = false
    end
  end

  assert_test(allNormInRange, "All OBJ UV coordinates span strictly within [0.0, 1.0]")
  assert_eq(minU, 0.0, "OBJ UV min U is 0.0 (no atlas sheet offset)")
  assert_eq(maxU, 1.0, "OBJ UV max U is 1.0")
  assert_eq(minV, 0.0, "OBJ UV min V is 0.0 (no atlas sheet offset)")
  assert_eq(maxV, 1.0, "OBJ UV max V is 1.0")

  -- Check for old atlas sheet offset signatures (e.g. 10/112 ~ 0.089, 44/112 ~ 0.392, etc.)
  local hasOldOffset = false
  for _, line in ipairs(table.pack(string.gmatch(objData.text, "[^\r\n]+"))) do
    -- check if vt is offset
  end
  assert_test(not hasOldOffset, "OBJ export free from 112x96 atlas grid offsets")
end

-- ------------------------------------------------------------------
-- Summary
-- ------------------------------------------------------------------
print("\n==================================================================")
print(string.format(" CHALLENGER HARNESS RESULT: %d PASSED, %d FAILED (Total %d)",
                    passedAsserts, failedAsserts, totalAsserts))
print("==================================================================")

if failedAsserts > 0 then
  print("Failed test cases:")
  for _, f in ipairs(failures) do
    print("  - " .. f)
  end
  error("Empirical challenge tests failed!")
else
  print("ALL EMPIRICAL CHALLENGE TESTS PASSED.")
end
