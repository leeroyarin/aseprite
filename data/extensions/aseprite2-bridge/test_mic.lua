local path = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Mic.aseprite"
local spr = app.open(path)
local out = io.open("data/extensions/aseprite2-bridge/test_mic.txt", "w")
if not spr then
  out:write("Failed to open " .. path .. "\n")
  out:close()
  return
end
out:write("Opened: " .. spr.filename .. "\n")
out:write("Canvas: " .. spr.width .. "x" .. spr.height .. "\n")
out:write("Frames: " .. #spr.frames .. "\n")
out:write("Layers: " .. #spr.layers .. "\n")
for i, l in ipairs(spr.layers) do
  out:write("  Layer " .. i .. ": " .. l.name .. "\n")
end
for i, f in ipairs(spr.frames) do
  out:write("  Frame " .. i .. " duration: " .. f.duration .. "\n")
end
out:close()
spr:close()
