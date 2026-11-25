local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Cello.aseprite]])
if not spr then return end
for i, l in ipairs(spr.layers) do
    print(string.format("Layer %d: '%s' isVisible=%s isEditable=%s", i, l.name, tostring(l.isVisible), tostring(l.isEditable)))
    for f = 1, math.min(#spr.frames, 5) do
        local cel = l:cel(f)
        if cel then
            print(string.format("  cel f%d: pos=(%d,%d) size=(%d,%d)", f, cel.position.x, cel.position.y, cel.image.width, cel.image.height))
        end
    end
end
spr:close()
