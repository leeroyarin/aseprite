local f = io.open("C:/Users/leero/Source/Repos/aseprite2/tools/mic_log.txt", "w")
f:write("Starting assembly...\n")
local ok, err = pcall(function()
  local spr = Sprite(32, 60, ColorMode.RGB)
  spr.filename = "I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Mic.aseprite"
  while #spr.frames < 16 do spr:newEmptyFrame() end
  local lCommon = spr.layers[1]
  lCommon.name = "Common"
  local lRare = spr:newLayer()
  lRare.name = "Rare"
  local lEpic = spr:newLayer()
  lEpic.name = "Epic"
  local lLegendary = spr:newLayer()
  lLegendary.name = "Legendary"
  local imgDir = "C:/Users/leero/Source/Repos/aseprite2/ref_instruments/mic/"
  for id = 1, 16 do
    local path = imgDir .. "Mic_Var" .. id .. ".png"
    local s = app.open(path)
    if s then
      local celImg = Image(s.width, s.height, ColorMode.RGB)
      celImg:drawSprite(s, s.frames[1])
      s:close()
      local targetL = lCommon
      if id >= 13 then targetL = lLegendary
      elseif id >= 9 then targetL = lEpic
      elseif id >= 5 then targetL = lRare end
      spr:newCel(targetL, spr.frames[id], celImg, Point(0, 0))
    else
      f:write("Failed to open " .. path .. "\n")
    end
  end
  spr:newTag(1, 4).name = "Common"
  spr:newTag(5, 8).name = "Rare"
  spr:newTag(9, 12).name = "Epic"
  spr:newTag(13, 16).name = "Legendary"
  spr:saveCopyAs("I:/Shared drives/Pitch Quest Shared Drive/Working Files/Music Store/Export/Instruments/Mic.aseprite")
  spr:saveCopyAs("C:/Users/leero/Source/Repos/aseprite2/ref_instruments/mic/Mic.aseprite")
  spr:close()
  f:write("Successfully assembled Mic.aseprite\n")
end)
if not ok then f:write("Error: " .. tostring(err) .. "\n") end
f:close()
