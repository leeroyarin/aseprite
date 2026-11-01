-- tests/smart_object/test_m3_reviewer_adversarial.lua
-- Minimal reproducer: Use-After-Free (0xC0000005) on redo after undoing duplicate frame

print("Testing NewFrame{ content = 'current' } -> undo -> redo...")
local spr = Sprite(16, 16)
app.command.PlaceSmartObject{
  filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
  ui = false
}
assert(#spr.frames == 1, "initial frame count is 1")

-- Duplicate frame
app.command.NewFrame{ content = "current" }
assert(#spr.frames == 2, "frame count is 2 after duplicate")

-- Undo duplicate
app.undo()
assert(#spr.frames == 1, "frame count is 1 after undo")

-- Redo duplicate (triggers use-after-free in transaction history)
app.redo()
assert(#spr.frames == 2, "frame count is 2 after redo")

spr:close()
print("Reproducer passed without crash.")
