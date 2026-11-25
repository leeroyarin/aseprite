local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Grand Piano.aseprite]])
if not spr then return end

for f = 1, #spr.frames do
    local img = Image(spr.spec)
    img:drawSprite(spr, f)
    local outPath = string.format([[C:\Users\leero\Source\Repos\aseprite2\tools\gp_current_f%d.png]], f)
    img:saveAs(outPath)
    print("Exported frame " .. f .. " -> " .. outPath)
end

spr:close()
