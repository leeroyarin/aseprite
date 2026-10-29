local dir = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/"
local out = io.open("c:/Users/leero/Source/Repos/aseprite2/tools/inspect_gp.txt", "w")

local function dumpLayers(layers, indent)
  for i, l in ipairs(layers) do
    if l.isGroup then
      out:write(indent .. "Group '" .. l.name .. "' (vis=" .. tostring(l.isVisible) .. ")\n")
      dumpLayers(l.layers, indent .. "  ")
    else
      local cels = {}
      for f = 1, #l.sprite.frames do
        local c = l:cel(f)
        if c then
          table.insert(cels, string.format("f%d(%dx%d@%d,%d)", f, c.bounds.width, c.bounds.height, c.bounds.x, c.bounds.y))
        end
      end
      out:write(indent .. "Layer '" .. l.name .. "' (vis=" .. tostring(l.isVisible) .. ", op=" .. l.opacity .. ") cels: " .. table.concat(cels, ", ") .. "\n")
    end
  end
end

local gp = app.open(dir .. "Grand Piano.aseprite")
if gp then
  out:write("=== Grand Piano.aseprite (" .. gp.width .. "x" .. gp.height .. ") ===\n")
  dumpLayers(gp.layers, "  ")
  gp:saveCopyAs("c:/Users/leero/Source/Repos/aseprite2/tools/gp_flat.png")
  gp:close()
end

local p = app.open(dir .. "Piano.aseprite")
if p then
  out:write("=== Piano.aseprite (" .. p.width .. "x" .. p.height .. ") ===\n")
  dumpLayers(p.layers, "  ")
  p:saveCopyAs("c:/Users/leero/Source/Repos/aseprite2/tools/piano_flat.png")
  p:close()
end

out:close()
