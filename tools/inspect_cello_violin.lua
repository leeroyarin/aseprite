local function inspect(path, name)
    print("=== " .. name .. " ===")
    local spr = app.open(path)
    if not spr then
        print("FAILED TO OPEN: " .. path)
        return
    end
    print("Canvas: " .. spr.width .. "x" .. spr.height)
    print("Frames: " .. #spr.frames)
    for f = 1, #spr.frames do
        print("  Frame " .. f .. " duration: " .. spr.frames[f].duration)
    end
    print("Layers: " .. #spr.layers)
    for i, l in ipairs(spr.layers) do
        print("  Layer " .. i .. ": " .. l.name .. " (visible: " .. tostring(l.isVisible) .. ")")
        for f = 1, #spr.frames do
            local cel = l:cel(f)
            if cel then
                print("    Cel f" .. f .. ": bounds " .. cel.bounds.x .. "," .. cel.bounds.y .. " " .. cel.bounds.width .. "x" .. cel.bounds.height)
            end
        end
    end
    print("Tags: " .. #spr.tags)
    for i, t in ipairs(spr.tags) do
        print("  Tag " .. i .. ": " .. t.name .. " (" .. t.fromFrame.frameNumber .. ".." .. t.toFrame.frameNumber .. ")")
    end
    spr:close()
end

inspect([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Cello.aseprite]], "Cello")
inspect([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Violine.aseprite]], "Violine")
