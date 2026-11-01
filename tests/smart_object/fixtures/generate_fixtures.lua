-- ==============================================================================
-- Smart Object Test Suite - Fixture Generator
-- Generates deterministic sample .aseprite files for E2E verification
-- ==============================================================================

local function scriptDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  return path:match("(.*[/\\])") or "./"
end

local outDir = scriptDir()
print("Generating Smart Object test fixtures in: " .. outDir)

-- Helper: Create and save sprite
local function createAndSave(filename, w, h, colorMode, drawFn)
  local spr = Sprite(w, h, colorMode or ColorMode.RGB)
  if drawFn then
    drawFn(spr)
  end
  local fullPath = outDir .. filename
  spr:saveAs(fullPath)
  spr:close()
  print("  [FIXTURE] Created: " .. filename)
end

-- 1. Single Frame 16x16 RGBA (Quadrant Pattern: Red, Green, Blue, Yellow)
createAndSave("single_frame_16x16.aseprite", 16, 16, ColorMode.RGB, function(spr)
  local img = spr.cels[1].image
  for y = 0, 15 do
    for x = 0, 15 do
      if x < 8 and y < 8 then
        img:drawPixel(x, y, Color{ r = 255, g = 0, b = 0, a = 255 }) -- Red TL
      elseif x >= 8 and y < 8 then
        img:drawPixel(x, y, Color{ r = 0, g = 255, b = 0, a = 255 }) -- Green TR
      elseif x < 8 and y >= 8 then
        img:drawPixel(x, y, Color{ r = 0, g = 0, b = 255, a = 255 }) -- Blue BL
      else
        img:drawPixel(x, y, Color{ r = 255, g = 255, b = 0, a = 255 }) -- Yellow BR
      end
    end
  end
end)

-- 2. Single Frame 32x32 RGBA (Centered Diamond Pattern)
createAndSave("single_frame_32x32.aseprite", 32, 32, ColorMode.RGB, function(spr)
  local img = spr.cels[1].image
  img:clear(Color{ r = 0, g = 0, b = 0, a = 0 })
  for y = 0, 31 do
    for x = 0, 31 do
      local dist = math.abs(x - 15.5) + math.abs(y - 15.5)
      if dist <= 12 then
        img:drawPixel(x, y, Color{ r = 100, g = 180, b = 240, a = 255 })
      end
    end
  end
end)

-- 3. Single Frame 64x64 RGBA (Checkerboard Pattern)
createAndSave("single_frame_64x64.aseprite", 64, 64, ColorMode.RGB, function(spr)
  local img = spr.cels[1].image
  for y = 0, 63 do
    for x = 0, 63 do
      local check = (math.floor(x / 8) + math.floor(y / 8)) % 2 == 0
      if check then
        img:drawPixel(x, y, Color{ r = 200, g = 200, b = 200, a = 255 })
      else
        img:drawPixel(x, y, Color{ r = 50, g = 50, b = 50, a = 255 })
      end
    end
  end
end)

-- 4. Multi-Frame 3-Frame RGBA (Idle/Walk: Red, Green, Blue frames)
createAndSave("multi_frame_3f.aseprite", 16, 16, ColorMode.RGB, function(spr)
  -- Frame 1 (Red)
  spr.cels[1].image:clear(Color{ r = 255, g = 0, b = 0, a = 255 })
  -- Frame 2 (Green)
  local f2 = spr:newEmptyFrame()
  local img2 = Image(16, 16, ColorMode.RGB)
  img2:clear(Color{ r = 0, g = 255, b = 0, a = 255 })
  spr:newCel(spr.layers[1], f2, img2)
  -- Frame 3 (Blue)
  local f3 = spr:newEmptyFrame()
  local img3 = Image(16, 16, ColorMode.RGB)
  img3:clear(Color{ r = 0, g = 0, b = 255, a = 255 })
  spr:newCel(spr.layers[1], f3, img3)
end)

-- 5. Multi-Frame 10-Frame RGBA (Progress bar indicator)
createAndSave("multi_frame_10f.aseprite", 20, 20, ColorMode.RGB, function(spr)
  for f = 1, 10 do
    if f > 1 then spr:newEmptyFrame() end
    local img = Image(20, 20, ColorMode.RGB)
    img:clear(Color{ r = 20, g = 20, b = 20, a = 255 })
    -- Draw bar of length 2 * f
    for x = 0, f * 2 - 1 do
      for y = 8, 11 do
        img:drawPixel(x, y, Color{ r = 255, g = 128, b = 0, a = 255 })
      end
    end
    if f == 1 then
      spr.cels[1].image = img
    else
      spr:newCel(spr.layers[1], f, img)
    end
  end
end)

-- 6. Grayscale 32x32
createAndSave("grayscale_32x32.aseprite", 32, 32, ColorMode.GRAY, function(spr)
  local img = spr.cels[1].image
  for y = 0, 31 do
    for x = 0, 31 do
      local v = math.floor((x / 31) * 255)
      img:drawPixel(x, y, Color{ gray = v, a = 255 })
    end
  end
end)

-- 7. Indexed 16x16
createAndSave("indexed_16x16.aseprite", 16, 16, ColorMode.INDEXED, function(spr)
  local img = spr.cels[1].image
  for y = 0, 15 do
    for x = 0, 15 do
      img:drawPixel(x, y, (x + y) % #spr.palettes[1])
    end
  end
end)

-- 8. Non-Square 16x32 (Tall)
createAndSave("nonsquare_16x32.aseprite", 16, 32, ColorMode.RGB, function(spr)
  local img = spr.cels[1].image
  for y = 0, 31 do
    for x = 0, 15 do
      img:drawPixel(x, y, Color{ r = x * 16, g = y * 8, b = 150, a = 255 })
    end
  end
end)

-- 9. Non-Square 48x12 (Wide)
createAndSave("nonsquare_48x12.aseprite", 48, 12, ColorMode.RGB, function(spr)
  local img = spr.cels[1].image
  for y = 0, 11 do
    for x = 0, 47 do
      img:drawPixel(x, y, Color{ r = 180, g = x * 5, b = y * 20, a = 255 })
    end
  end
end)

-- 10. Extreme 1x1 Minimal Pixel
createAndSave("extreme_1x1.aseprite", 1, 1, ColorMode.RGB, function(spr)
  spr.cels[1].image:drawPixel(0, 0, Color{ r = 255, g = 0, b = 128, a = 255 })
end)

-- 11. Extreme 256x256 Large Asset
createAndSave("extreme_256x256.aseprite", 256, 256, ColorMode.RGB, function(spr)
  local img = spr.cels[1].image
  img:clear(Color{ r = 30, g = 40, b = 50, a = 255 })
  -- Center cross
  for i = 0, 255 do
    img:drawPixel(i, 128, Color{ r = 255, g = 255, b = 255, a = 255 })
    img:drawPixel(128, i, Color{ r = 255, g = 255, b = 255, a = 255 })
  end
end)

-- 12. Empty 0-byte file
local emptyFile = io.open(outDir .. "empty.aseprite", "wb")
if emptyFile then
  emptyFile:close()
  print("  [FIXTURE] Created: empty.aseprite (0 bytes)")
end

-- 13. Corrupt Binary File
local corruptFile = io.open(outDir .. "corrupt.aseprite", "wb")
if corruptFile then
  corruptFile:write("CORRUPT_HEADER_NOT_AN_ASEPRITE_FILE_MAGIC_BYTES_1234567890\0\0\0\0")
  corruptFile:close()
  print("  [FIXTURE] Created: corrupt.aseprite (invalid binary header)")
end

print("All fixtures generated successfully.")
