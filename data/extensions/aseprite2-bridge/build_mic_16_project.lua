-- build_mic_16_project.lua
-- Assembles 16-frame, 4-layer, 4-tag Mic.aseprite from generated rarity variations

local targetPath = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Mic.aseprite"
local localPath = "C:/Users/leero/Source/Repos/aseprite2/ref_instruments/mic/Mic.aseprite"
local imgDir = "C:/Users/leero/Source/Repos/aseprite2/ref_instruments/mic/"

print("Building 16-frame Mic.aseprite...")

local spr = Sprite(32, 60, ColorMode.RGB)
spr.filename = targetPath

-- Ensure 16 frames exist (4 per rarity tier)
while #spr.frames < 16 do
  spr:newEmptyFrame()
end

for i, f in ipairs(spr.frames) do
  f.duration = 0.5 -- 500ms per frame
end

-- Create 4 Rarity Layers
local lCommon = spr.layers[1]
lCommon.name = "Common (Tier 1)"

local lRare = spr:newLayer()
lRare.name = "Rare (Tier 2)"

local lEpic = spr:newLayer()
lEpic.name = "Epic (Tier 3)"

local lLegendary = spr:newLayer()
lLegendary.name = "Legendary (Tier 4)"

-- Load and place cels for each variation
for id = 1, 16 do
  local img = Image{ fromFile = imgDir .. "Mic_Var" .. id .. ".png" }
  if img then
    local targetLayer = lCommon
    if id >= 13 then targetLayer = lLegendary
    elseif id >= 9 then targetLayer = lEpic
    elseif id >= 5 then targetLayer = lRare
    end

    spr:newCel(targetLayer, spr.frames[id], img, Point(0, 0))
  else
    print("Warning: Missing Mic_Var" .. id .. ".png")
  end
end

-- Create Animation Tags for each rarity tier
local tagCommon = spr:newTag(1, 4)
tagCommon.name = "Common"

local tagRare = spr:newTag(5, 8)
tagRare.name = "Rare"

local tagEpic = spr:newTag(9, 12)
tagEpic.name = "Epic"

local tagLegendary = spr:newTag(13, 16)
tagLegendary.name = "Legendary"

-- Save locally and to shared drive Working Files
spr:saveCopyAs(localPath)
spr:saveCopyAs(targetPath)
spr:close()

print("Successfully assembled 16-frame Mic.aseprite!")
