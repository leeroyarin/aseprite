local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Mic.aseprite]])
if not spr then
    print("FAILED TO OPEN Mic.aseprite")
    return
end
print("Frames: " .. #spr.frames)
print("Layers: " .. #spr.layers)
for i, l in ipairs(spr.layers) do
    print("  Layer " .. i .. ": " .. l.name)
end
print("Tags: " .. #spr.tags)
for i, t in ipairs(spr.tags) do
    print("  Tag " .. i .. ": " .. t.name .. " (" .. t.fromFrame.frameNumber .. ".." .. t.toFrame.frameNumber .. ")")
end
spr:close()
