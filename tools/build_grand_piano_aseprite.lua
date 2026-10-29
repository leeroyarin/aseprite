local outAseprite = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Grand Piano.aseprite"
local refDir = "C:/Users/leero/Source/Repos/aseprite2/ref_instruments/grand_piano/"

local spr = Sprite(108, 80, ColorMode.RGB)
spr.filename = outAseprite

-- Create 16 frames total (0.5s duration each)
spr.frames[1].duration = 0.5
for f = 2, 16 do
  local fr = spr:newEmptyFrame(f)
  fr.duration = 0.5
end

-- Create 4 dedicated rarity layers
local lCommon = spr.layers[1]
lCommon.name = "Common (Rarity 1 - Vars 1-4)"
local lRare = spr:newLayer()
lRare.name = "Rare (Rarity 2 - Vars 5-8)"
local lEpic = spr:newLayer()
lEpic.name = "Epic (Rarity 3 - Vars 9-12)"
local lLegendary = spr:newLayer()
lLegendary.name = "Legendary (Rarity 4 - Vars 13-16)"

local tierLayers = { lCommon, lRare, lEpic, lLegendary }

for i = 1, 16 do
  local tierIdx = math.floor((i - 1) / 4) + 1
  local targetLayer = tierLayers[tierIdx]
  local imgPath = refDir .. "GrandPiano_Var" .. tostring(i) .. ".png"
  local img = Image{ fromFile = imgPath }
  if img then
    spr:newCel(targetLayer, i, img, Point(0, 0))
  end
end

-- Add frame tags for each rarity tier
local t1 = spr:newTag(1, 4)
t1.name = "Common"
t1.color = Color{ r=148, g=163, b=184 }

local t2 = spr:newTag(5, 8)
t2.name = "Rare"
t2.color = Color{ r=56, g=189, b=248 }

local t3 = spr:newTag(9, 12)
t3.name = "Epic"
t3.color = Color{ r=192, g=132, b=252 }

local t4 = spr:newTag(13, 16)
t4.name = "Legendary"
t4.color = Color{ r=250, g=204, b=21 }

spr:saveAs(outAseprite)
spr:close()
