local spr = Sprite(16, 16)
app.command.PlaceSmartObject{
  filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
  ui = false
}
local lay = spr.layers[2]

app.command.NewFrame{ content = "empty" } -- 2
spr:newEmptyFrame()                      -- 3
spr:newEmptyFrame(2)                     -- 4
spr:newEmptyFrame(1)                     -- 5

print("Before deleteFrame(1): total frames=" .. #spr.frames)
spr:deleteFrame(1)
print("After deleteFrame(1): total frames=" .. #spr.frames)

print("Calling app.undo()...")
app.undo()
print("After app.undo(): total frames=" .. #spr.frames)

local c1 = lay:cel(1)
local r = app.pixelColor.rgbaR(c1.image:getPixel(0, 0))
local g = app.pixelColor.rgbaG(c1.image:getPixel(0, 0))
local b = app.pixelColor.rgbaB(c1.image:getPixel(0, 0))

print(string.format("After undo head delete: frame 1 RGB = (%d, %d, %d)", r, g, b))
spr:close()
