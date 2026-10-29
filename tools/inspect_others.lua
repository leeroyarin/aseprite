local dir = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/"
local out = io.open("c:/Users/leero/Source/Repos/aseprite2/tools/inspect_out2.txt", "w")

local files = { "Trumphet.aseprite", "Saxophone.aseprite", "Double Bass.aseprite" }
for _, name in ipairs(files) do
  local spr = app.open(dir .. name)
  if spr then
    out:write("=== " .. name .. " (" .. spr.width .. "x" .. spr.height .. ") ===\n")
    out:write("Layers (" .. #spr.layers .. "):\n")
    for _, l in ipairs(spr.layers) do
      out:write("  " .. l.name .. "\n")
    end
    out:write("Frames: " .. #spr.frames .. "\n")
    spr:close()
  end
end
out:close()
