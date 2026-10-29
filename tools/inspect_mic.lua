local micPath = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Mic.aseprite"
local celloPath = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Cello.aseprite"

local out = io.open("c:/Users/leero/Source/Repos/aseprite2/tools/inspect_out.txt", "w")

local function inspect(path)
  local spr = app.open(path)
  if not spr then
    out:write("Failed to open " .. path .. "\n")
    return
  end
  out:write("--- " .. path .. " ---\n")
  out:write("Size: " .. spr.width .. "x" .. spr.height .. "\n")
  out:write("ColorMode: " .. tostring(spr.colorMode) .. "\n")
  out:write("Frames: " .. #spr.frames .. "\n")
  out:write("Layers: " .. #spr.layers .. "\n")
  for i, l in ipairs(spr.layers) do
    out:write("  Layer " .. i .. ": " .. l.name .. " (visible=" .. tostring(l.isVisible) .. ", opacity=" .. l.opacity .. ")\n")
  end
  for i, f in ipairs(spr.frames) do
    out:write("  Frame " .. i .. " duration: " .. f.duration .. "\n")
  end
  spr:close()
end

inspect(micPath)
inspect(celloPath)
out:close()
