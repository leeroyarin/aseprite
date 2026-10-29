local dir = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/"
local out = io.open("c:/Users/leero/Source/Repos/aseprite2/tools/inspect_all_inst.txt", "w")
local files = { "Grand Piano.aseprite", "Piano.aseprite", "Cello.aseprite", "Drumset.aseprite", "Keyboard.aseprite", "Guitar.aseprite", "Saxophone.aseprite", "Trumphet.aseprite", "Trumbone.aseprite", "Mic.aseprite", "Double Bass.aseprite", "Double Bass 2.aseprite" }
for _, name in ipairs(files) do
  local spr = app.open(dir .. name)
  if spr then
    out:write("=== " .. name .. " (" .. spr.width .. "x" .. spr.height .. ") ===\n")
    out:write("Frames: " .. #spr.frames .. "\n")
    for i, l in ipairs(spr.layers) do
      local cels = {}
      for f = 1, #spr.frames do
        if l:cel(f) then table.insert(cels, tostring(f)) end
      end
      out:write("  Layer " .. i .. ": '" .. l.name .. "' (vis=" .. tostring(l.isVisible) .. ") cels: [" .. table.concat(cels, ",") .. "]\n")
    end
    spr:close()
  end
end
out:close()
