-- Aseprite Smart Object source tags and host-frame state transitions.
local count = 0
local function check(ok, name)
  assert(ok, name)
  count = count + 1
  print("PASS " .. name)
end
local dir = app.fs.joinPath(app.fs.tempPath, "smart-tags-" .. os.time())
app.fs.makeDirectory(dir)
local sourcePath = app.fs.joinPath(dir, "source.aseprite")
local hostPath = app.fs.joinPath(dir, "host.aseprite")
local src = Sprite(2, 2)
for i = 2, 6 do src:newEmptyFrame(i) end
for i = 1, 6 do
  local im = Image(2, 2)
  im:clear(Color{r=i*30,g=0,b=0,a=255})
  if src.layers[1]:cel(i) then src:deleteCel(src.layers[1], i) end
  src:newCel(src.layers[1], i, im)
end
local idle = src:newTag(2, 3); idle.name = "Idle"
local attack = src:newTag(4, 6); attack.name = "Attack"
local rev = src:newTag(4, 6); rev.name = "Reverse"; rev.aniDir = AniDir.REVERSE
local once = src:newTag(4, 6); once.name = "Once"; once.repeats = 1
local ping = src:newTag(2, 4); ping.name = "Ping"; ping.aniDir = AniDir.PING_PONG
src:saveAs(sourcePath); src:close()
local host = Sprite(8, 8)
for i = 2, 12 do host:newEmptyFrame(i) end
app.command.PlaceSmartObject{filename=sourcePath,ui=false}
local sm = app.activeLayer
local function frames(expected, name)
  for i,v in ipairs(expected) do
    local cel = assert(sm:cel(i), "missing cel "..i)
    local actual = app.pixelColor.rgbaR(cel.image:getPixel(0,0))
    assert(actual == v*30, name.." frame "..i.." expected "..v.." got "..actual/30)
  end
  check(true, name)
end
sm.smartObjectAnimationStates = "1=Idle;5=Attack;10=Idle"
frames({2,3,2,3,4,5,6,4,5,2,3,2}, "loop and state restart")
sm.smartObjectAnimationStates = "2-5L=Idle;7-11H=Attack"
frames({1,2,3,2,3,6,4,5,6,6,6,6}, "bounded looping and one-shot ranges")
check(not pcall(function() sm.smartObjectAnimationStates = "1-4L=Idle;4-6H=Attack" end),
      "reject overlapping ranges")
sm.smartObjectAnimationStates = "1=Reverse"
frames({6,5,4,6,5,4}, "reverse tag")
sm.smartObjectAnimationStates = "1=Ping"
frames({2,3,4,3,2,3,4,3}, "ping pong tag")
sm.smartObjectAnimationStates = "1=Once"
frames({4,5,6,6,6,6}, "finite tag repeats hold the final frame")
sm.smartObjectAnimationStates = "1=Ping"
sm.smartObjectTimelineMapping = "hold"
frames({2,3,4,3,3,3}, "hold at end of tag cycle")
sm.smartObjectTimelineMapping = "fixed"
frames({2,2,2,2}, "fixed uses selected tag start")
sm.smartObjectTimelineMapping = "loop"
sm.smartObjectAnimationStates = "1=Idle;5=Attack"
host:saveAs(hostPath)
host:close()
host=app.open(hostPath); app.activeSprite=host; sm=host.layers[#host.layers]
check(sm.smartObjectAnimationStates=="1=Idle;5=Attack", "save and reopen states")
frames({2,3,2,3,4,5,6,4}, "save and reopen pixels")
sm.smartObjectAnimationStates="1=Reverse"
app.undo()
check(sm.smartObjectAnimationStates=="1=Idle;5=Attack", "undo states")
frames({2,3,2,3,4,5,6,4}, "undo rendered pixels")
app.redo()
frames({6,5,4,6}, "redo rendered pixels")
for _,invalid in ipairs({"2=Idle", "1=Missing", "1=Idle;1=Attack", "1=Idle;", "abc"}) do
  check(not pcall(function() sm.smartObjectAnimationStates=invalid end), "reject "..invalid)
end
sm.smartObjectAnimationStates=""
frames({1,2,3,4,5,6,1}, "clear restores full source")
host:close()
os.remove(sourcePath); os.remove(hostPath); app.fs.removeDirectory(dir)
print("TOTAL: "..count.." Passed, 0 Failed")
