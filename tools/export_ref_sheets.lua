local dir = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/"
local outDir = "c:/Users/leero/Source/Repos/aseprite2/tools/"

-- Export each layer of Grand Piano individually to understand its anatomy
local gp = app.open(dir .. "Grand Piano.aseprite")
if gp then
  local grp = gp.layers[2]
  for i, l in ipairs(grp.layers) do
    local c = l:cel(1)
    if c then
      local img = Image(gp.width, gp.height)
      img:drawImage(c.image, c.position)
      img:saveAs(outDir .. "gp_layer_" .. i .. "_" .. l.name:gsub(" ", "_") .. ".png")
    end
  end
  gp:close()
end

-- Export horizontal strip of Cello and Saxophone frames
local function exportStrip(file, outName)
  local spr = app.open(dir .. file)
  if spr then
    local n = #spr.frames
    local strip = Image(spr.width * n, spr.height)
    for f = 1, n do
      local flat = Image(spr.width, spr.height)
      flat:drawSprite(spr, f)
      strip:drawImage(flat, Point((f - 1) * spr.width, 0))
    end
    strip:saveAs(outDir .. outName)
    spr:close()
  end
end

exportStrip("Cello.aseprite", "cello_strip.png")
exportStrip("Saxophone.aseprite", "sax_strip.png")
exportStrip("Trumphet.aseprite", "trumpet_strip.png")
exportStrip("Trumbone.aseprite", "trombone_strip.png")
