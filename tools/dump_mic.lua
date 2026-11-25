local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Mic.aseprite]])
if not spr then return end
print("Dimensions: " .. spr.width .. "x" .. spr.height)
print("Frames: " .. #spr.frames)
for f = 1, #spr.frames do
    local outPath = string.format([[C:\Users\leero\Source\Repos\aseprite2\tools\frame_%d.png]], f)
    local img = Image(spr.spec)
    img:drawSprite(spr, f)
    img:saveAs(outPath)
    print("Saved frame " .. f)
end
for i, l in ipairs(spr.layers) do
    print("Layer " .. i .. " (" .. l.name .. ") visible: " .. tostring(l.isVisible))
    for f = 1, #spr.frames do
        local cel = l:cel(f)
        if cel then
            print("  Cel at frame " .. f .. ": bounds " .. cel.bounds.x .. "," .. cel.bounds.y .. " " .. cel.bounds.width .. "x" .. cel.bounds.height)
        end
    end
end
spr:close()
