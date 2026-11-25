-- 16-frame / 4-layer Grand Piano rarity master (Reference Base, Palette Swap, Unique Details, VFX / Glow)
local outAseprite = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Grand Piano.aseprite"
local layerDir = "C:/Users/leero/AppData/Local/Temp/claude/2975b516535c99c5/122b1d71-7e79-49b0-8cf3-6753e169f3b9/scratchpad/layers/"

local spr = Sprite(108, 80, ColorMode.RGB)
spr.filename = outAseprite
spr.frames[1].duration = 0.5
for f = 2, 16 do spr:newEmptyFrame(f).duration = 0.5 end

local lBase = spr.layers[1];  lBase.name = "Reference Base"
local lSwap = spr:newLayer(); lSwap.name = "Palette Swap"
local lDet  = spr:newLayer(); lDet.name  = "Unique Details"
local lVfx  = spr:newLayer(); lVfx.name  = "VFX / Glow"

local base = Image{ fromFile = layerDir .. "swap_1.png" } -- Common 1 = untouched original sprite
for i = 1, 16 do
  spr:newCel(lBase, i, base, Point(0, 0))
  spr:newCel(lSwap, i, Image{ fromFile = layerDir .. "swap_" .. i .. ".png" }, Point(0, 0))
  local dp = layerDir .. "detail_" .. i .. ".png"
  if app.fs.isFile(dp) then spr:newCel(lDet, i, Image{ fromFile = dp }, Point(0, 0)) end
end

local tags = {
  { "Common", 1, 4, Color{ r=148, g=163, b=184 } },
  { "Rare", 5, 8, Color{ r=56, g=189, b=248 } },
  { "Epic", 9, 12, Color{ r=192, g=132, b=252 } },
  { "Legendary", 13, 16, Color{ r=250, g=204, b=21 } },
}
for _, t in ipairs(tags) do
  local tag = spr:newTag(t[2], t[3]); tag.name = t[1]; tag.color = t[4]
end
spr:saveAs(outAseprite)
spr:close()
