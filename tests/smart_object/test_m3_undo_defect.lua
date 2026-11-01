-- tests/smart_object/test_m3_undo_defect.lua
-- Minimal reproduction of Undo NewFrame duplicate cel defect on smart object layer

local spr = Sprite(16, 16)
app.command.PlaceSmartObject{
  filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
  ui = false
}
local lay = spr.layers[2]
assert(#lay.cels == 1, "initial cel count is 1")

-- Add empty frame via command
app.command.NewFrame{ content = "empty" }
assert(#spr.frames == 2, "sprite has 2 frames")
assert(#lay.cels == 2, "smart layer has 2 cels")

-- Add 2nd empty frame
app.command.NewFrame{ content = "empty" }
assert(#spr.frames == 3, "sprite has 3 frames")
assert(#lay.cels == 3, "smart layer has 3 cels")

-- Undo the addition of frame 3
app.undo()

print(string.format("After undo: sprite total frames = %d", #spr.frames))
print(string.format("After undo: smart layer cel count = %d", #lay.cels))

for i, c in ipairs(lay.cels) do
  print(string.format("  Cel index %d: frameNumber = %d", i, c.frameNumber))
end

if #lay.cels ~= #spr.frames then
  error(string.format("BUG CONFIRMED: Cel count (%d) does not match sprite frame count (%d) after undo!",
    #lay.cels, #spr.frames))
end

spr:close()
print("Test passed cleanly.")
