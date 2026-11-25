local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Mic.aseprite]])
if not spr then
    print("FAILED TO OPEN Mic.aseprite")
    return
end

local outDir = [[I:\Shared drives\Pitch Quest Shared Drive\Finalized Files\Instruments\Mic\]]

-- 1. Export each individual frame as PNG
local tierNames = {
    "Mic - Common (Tier 1).png",
    "Mic - Rare (Tier 2).png",
    "Mic - Epic (Tier 3).png",
    "Mic - Legendary (Tier 4).png"
}

for f = 1, #spr.frames do
    local img = Image(spr.spec)
    img:drawSprite(spr, f)
    local fn = tierNames[f] or string.format("Mic - Frame %d.png", f)
    img:saveAs(outDir .. fn)
    print("Exported frame " .. f .. " -> " .. fn)
end

-- 2. Export spritesheet (horizontal strip and JSON)
app.command.ExportSpriteSheet {
    ui = false,
    askOverwrite = false,
    type = SpriteSheetType.HORIZONTAL,
    textureFilename = outDir .. "Mic - SpriteSheet.png",
    dataFilename = outDir .. "Mic - SpriteSheet.json",
    dataFormat = SpriteSheetDataFormat.JSON_HASH,
    listLayers = false,
    listFrameTags = false,
    splitLayers = false
}
print("Exported spritesheet and JSON.")

spr:close()
