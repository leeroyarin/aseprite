-- build_mic_project.lua
-- Assembles multi-layer, multi-frame Mic.aseprite from generated rarity variations

local targetPath = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Mic.aseprite"
local localPath = "c:/Users/leero/Source/Repos/aseprite2/Mic.aseprite"
local imgDir = "c:/Users/leero/Source/Repos/aseprite2/ref_instruments/"

print("Building Mic.aseprite...")

local img1 = Image{ fromFile = imgDir .. "Mic1.png" }
local img2 = Image{ fromFile = imgDir .. "Mic2.png" }
local img3 = Image{ fromFile = imgDir .. "Mic3.png" }
local img4 = Image{ fromFile = imgDir .. "Mic4.png" }

if not img1 or not img2 or not img3 or not img4 then
  print("Error: Could not load one or more mic PNG files")
  return
end

-- Create 32x60 RGB Sprite
local spr = Sprite(32, 60, ColorMode.RGB)
spr.filename = targetPath

-- We want 4 frames so each rarity can be viewed by advancing frames
while #spr.frames < 4 do
  spr:newEmptyFrame()
end

-- Create 4 Layers
local l1 = spr.layers[1]
l1.name = "Common (Rarity 1)"

local l2 = spr:newLayer()
l2.name = "Rare (Rarity 2)"

local l3 = spr:newLayer()
l3.name = "Epic (Rarity 3)"

local l4 = spr:newLayer()
l4.name = "Legendary (Rarity 4)"

-- Frame 1: Common visible, others empty or hidden
-- Frame 2: Rare visible
-- Frame 3: Epic visible
-- Frame 4: Legendary visible
spr:newCel(l1, spr.frames[1], img1, Point(0, 0))
spr:newCel(l2, spr.frames[2], img2, Point(0, 0))
spr:newCel(l3, spr.frames[3], img3, Point(0, 0))
spr:newCel(l4, spr.frames[4], img4, Point(0, 0))

-- Also add cels on all frames if user prefers layer-toggle workflow:
-- (or keep 1 cel per layer per frame so animation plays through all 4 rarities)
for i, f in ipairs(spr.frames) do
  f.duration = 0.5 -- 500ms per frame for smooth preview
end

-- Save locally and to shared drive
spr:saveCopyAs(localPath)
spr:saveCopyAs(targetPath)
print("Saved Mic.aseprite to local repo and Google Drive!")
spr:close()
