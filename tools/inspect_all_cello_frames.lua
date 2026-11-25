local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Cello.aseprite]])
if not spr then return end
for f = 1, #spr.frames do
    local cel = spr.layers[3]:cel(f) -- Layer 1
    if cel then
        print(string.format("Frame %d cel: pos=(%d,%d) size=(%d,%d)", f, cel.position.x, cel.position.y, cel.image.width, cel.image.height))
    else
        print(string.format("Frame %d cel: nil", f))
    end
end
spr:close()
