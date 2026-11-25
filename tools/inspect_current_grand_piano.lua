local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Grand Piano.aseprite]])
if not spr then
    print("FAILED TO OPEN Grand Piano.aseprite")
    return
end

print("=== Grand Piano.aseprite ===")
print("Dimensions: " .. spr.width .. "x" .. spr.height)
print("Frames: " .. #spr.frames)
for f = 1, #spr.frames do
    print(string.format("  Frame %d duration: %.2f", f, spr.frames[f].duration))
end

print("Layers: " .. #spr.layers)
for i, l in ipairs(spr.layers) do
    print(string.format("  Layer %d: '%s' (visible: %s)", i, l.name, tostring(l.isVisible)))
end

print("Tags: " .. #spr.tags)
for i, t in ipairs(spr.tags) do
    print(string.format("  Tag %d: '%s' (%d..%d)", i, t.name, t.fromFrame.frameNumber, t.toFrame.frameNumber))
end

spr:close()
