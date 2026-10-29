local logFile = io.open("c:/Users/leero/Source/Repos/aseprite2/tools/script_log.txt", "w")
logFile:write("Script started\n")

local ok, err = pcall(function()
  local spr = app.open("c:/Users/leero/Source/Repos/aseprite2/Mic.aseprite")
  if not spr then
    logFile:write("Failed to open Mic.aseprite\n")
    return
  end
  logFile:write("Opened Mic.aseprite successfully (" .. spr.width .. "x" .. spr.height .. ")\n")
  spr:saveCopyAs("c:/Users/leero/Source/Repos/aseprite2/tools/test_mic_copy.png")
  logFile:write("saveCopyAs called\n")
  spr:close()
  logFile:write("spr:close called\n")
end)

if not ok then
  logFile:write("Error: " .. tostring(err) .. "\n")
end

logFile:close()
