-- ==============================================================================
-- Empirical Verification: Asymmetric Silhouette Carving on Linked Columns
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

local function assert_test(cond, desc)
  totalAsserts = totalAsserts + 1
  if cond then
    passedAsserts = passedAsserts + 1
    print(string.format("  [OK] %s", desc))
  else
    failedAsserts = failedAsserts + 1
    print(string.format("  [FAIL] %s", desc))
  end
end

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

-- Emulate exact fixed C++ buildSculptedVolume logic
local function buildVoxelVolumeFixed(docData, colLinked)
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
          -- Fixed: guard opposing face union by !colLinked[0]
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
          -- Fixed: guard opposing face union by !colLinked[1]
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
          -- Fixed: guard opposing face union by !colLinked[2]
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

print("==================================================================")
print(" EMPIRICAL VERIFICATION: ASYMMETRIC SILHOUETTE CARVING")
print("==================================================================")

-- Test 1: Asymmetric hole carving on linked Col 1 (Front/Back)
do
  local doc = setupTestDoc(24, 24)
  local solidCol = Color{ r = 120, g = 120, b = 120, a = 255 }
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(solidCol)
  end

  -- Carve off-center asymmetric hole at x = 5, y = 10 on Front
  local frontImg = doc.layers[kFaceFront]:cel(1).image
  frontImg:drawPixel(5, 10, Color{ r = 0, g = 0, b = 0, a = 0 })

  local vol = buildVoxelVolumeFixed(doc, { [0] = false, [1] = true, [2] = false })
  assert_test(not vol.isSolid(5, 10, 11), "Linked Col 1: Asymmetric hole at x=5, y=10 is hollow (not solid)")
  assert_test(vol.isSolid(18, 10, 11), "Linked Col 1: Mirrored coordinate x=18, y=10 remains solid")
end

-- Test 2: Phantom twin voxel prevention on linked Col 1
do
  local doc = setupTestDoc(24, 24)
  -- Transparent doc with single pixel at x = 5, y = 10 on Front
  local frontImg = doc.layers[kFaceFront]:cel(1).image
  frontImg:drawPixel(5, 10, Color{ r = 255, g = 0, b = 0, a = 255 })

  local vol = buildVoxelVolumeFixed(doc, { [0] = false, [1] = true, [2] = false })
  assert_test(vol.isSolid(5, 10, 11), "Linked Col 1: Painted pixel at x=5, y=10 produces solid voxel")
  assert_test(not vol.isSolid(18, 10, 11), "Linked Col 1: Mirrored x=18, y=10 has NO phantom voxel")
end

-- Test 3: Asymmetric hole carving on linked Col 0 (Top/Bottom)
do
  local doc = setupTestDoc(24, 24)
  local solidCol = Color{ r = 120, g = 120, b = 120, a = 255 }
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(solidCol)
  end

  -- Carve off-center asymmetric hole at x = 5, z = 7 on Top
  local topImg = doc.layers[kFaceTop]:cel(1).image
  topImg:drawPixel(5, 7, Color{ r = 0, g = 0, b = 0, a = 0 })

  local vol = buildVoxelVolumeFixed(doc, { [0] = true, [1] = false, [2] = false })
  assert_test(not vol.isSolid(5, 11, 7), "Linked Col 0: Asymmetric hole at x=5, z=7 is hollow (not solid)")
  assert_test(vol.isSolid(5, 11, 16), "Linked Col 0: Mirrored coordinate z=16 remains solid")
end

-- Test 4: Asymmetric hole carving on linked Col 2 (Right/Left)
do
  local doc = setupTestDoc(24, 24)
  local solidCol = Color{ r = 120, g = 120, b = 120, a = 255 }
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(solidCol)
  end

  -- Carve off-center asymmetric hole at z = 5, y = 8 on Right
  local rightImg = doc.layers[kFaceRight]:cel(1).image
  rightImg:drawPixel(5, 8, Color{ r = 0, g = 0, b = 0, a = 0 })

  local vol = buildVoxelVolumeFixed(doc, { [0] = false, [1] = false, [2] = true })
  assert_test(not vol.isSolid(11, 8, 18), "Linked Col 2: Asymmetric hole at uRight=5 (z=18) is hollow")
end

-- Test 5: Unlinked Col 1 union behavior preserved
do
  local doc = setupTestDoc(24, 24)
  local solidCol = Color{ r = 120, g = 120, b = 120, a = 255 }
  for f = 0, kFaceCount - 1 do
    doc.layers[f]:cel(1).image:clear(solidCol)
  end

  -- Hole on Front only; Back is solid
  local frontImg = doc.layers[kFaceFront]:cel(1).image
  frontImg:drawPixel(5, 10, Color{ r = 0, g = 0, b = 0, a = 0 })

  local vol = buildVoxelVolumeFixed(doc, { [0] = false, [1] = false, [2] = false })
  assert_test(vol.isSolid(5, 10, 11), "Unlinked Col 1: Back face solid correctly unions when unlinked")
end

print(string.format("\nASYMMETRIC CARVING TEST RESULT: %d PASSED, %d FAILED (Total %d)",
  passedAsserts, failedAsserts, totalAsserts))

if failedAsserts > 0 then
  app.exit(1)
else
  print("ALL ASYMMETRIC CARVING VERIFICATION CHECKS PASSED.")
end
