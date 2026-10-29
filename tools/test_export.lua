local spr = app.open("c:/Users/leero/Source/Repos/aseprite2/Mic.aseprite")
if spr then
  spr:saveCopyAs("c:/Users/leero/Source/Repos/aseprite2/tools/test_mic_copy.png")
  spr:close()
end
