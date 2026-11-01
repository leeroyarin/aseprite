-- tests/smart_object/challenger_m4_stale_cache_defect.lua
-- Minimal reproduction of Stale Cache Hit on PlaceSmartObject (M4 Defect)
--
-- Root cause:
--   In src/app/commands/cmd_place_smart_object.cpp:
--   doc::Document* cachedDoc = LayerSmartObject::getDetachedDoc(fullpath);
--   If fullpath was previously loaded into s_detachedDocsCache, PlaceSmartObject reuses
--   cachedDoc WITHOUT checking if the file on disk has a newer modification epoch.
--   It then stamps smartLayer->setLastModifiedTime(diskTime) with the current disk epoch,
--   leaving the smart layer displaying stale pixels while claiming to be up-to-date.

local tempDir = app.fs.tempPath or "."
local srcPath = app.fs.joinPath(tempDir, "m4_defect_repro.aseprite")

local function cleanup()
  pcall(function() app.fs.removeFile(srcPath) end)
  pcall(function() os.remove(srcPath) end)
end

cleanup()

local function advanceTimestamp()
  local start = os.time()
  while os.time() <= start do
    -- wait for second boundary
  end
end

-- Step 1: Create initial source file on disk with Orange pixels (255, 128, 0)
do
  local spr = Sprite(16, 16, ColorMode.RGB)
  for y = 0, 15 do
    for x = 0, 15 do
      spr.cels[1].image:drawPixel(x, y, Color{ r = 255, g = 128, b = 0, a = 255 })
    end
  end
  spr:saveAs(srcPath)
  spr:close()
end

-- Step 2: Import into Host Document 1 (loads into s_detachedDocsCache)
local host1 = Sprite(32, 32, ColorMode.RGB)
app.command.PlaceSmartObject{ filename = srcPath, x = 0, y = 0, ui = false }
local lay1 = host1.layers[2]
local px1 = lay1:cel(1).image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(px1) == 255 and app.pixelColor.rgbaG(px1) == 128, "Host 1 has Orange")
host1:close()

-- Step 3: Advance timestamp and modify source file on disk to Teal (0, 128, 255)
advanceTimestamp()
do
  local spr = Sprite(16, 16, ColorMode.RGB)
  for y = 0, 15 do
    for x = 0, 15 do
      spr.cels[1].image:drawPixel(x, y, Color{ r = 0, g = 128, b = 255, a = 255 })
    end
  end
  spr:saveAs(srcPath)
  spr:close()
end

-- Step 4: Open Host Document 2 and place the modified file
local host2 = Sprite(32, 32, ColorMode.RGB)
app.command.PlaceSmartObject{ filename = srcPath, x = 0, y = 0, ui = false }
local lay2 = host2.layers[2]

local px2 = lay2:cel(1).image:getPixel(0, 0)
local r = app.pixelColor.rgbaR(px2)
local g = app.pixelColor.rgbaG(px2)
local b = app.pixelColor.rgbaB(px2)

print(string.format("Host 2 Placed Cel Pixel: R=%d, G=%d, B=%d (Expected Teal: R=0, G=128, B=255)", r, g, b))
print(string.format("Host 2 Layer smartObjectLastModified: %s", tostring(lay2.smartObjectLastModified)))
print(string.format("Host 2 Layer linkStatus: %s", tostring(lay2.linkStatus)))

host2:close()
cleanup()

if r == 255 and g == 128 and b == 0 then
  error(string.format("DEFECT REPRODUCED: PlaceSmartObject served STALE cached Document (Orange R=255, G=128) instead of reading current disk file (Teal R=0, G=128)!"))
end

print("SUCCESS: PlaceSmartObject loaded up-to-date file from disk.")
