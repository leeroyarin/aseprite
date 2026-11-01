-- tests/smart_object/test_m3_redo_crash_repro.lua
-- Minimal reproducer for Milestone 3 Iteration 3 Redo Crash / Heap Corruption
-- Caused by premature `delete c;` in `LayerSmartObject::displaceFrames` freeing cels
-- that are owned and suspended by `app::cmd::RemoveCel` during `DocApi::copyFrame`.

local spr = Sprite(16, 16)
app.command.PlaceSmartObject{
  filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
  ui = false
}
local lay = spr.layers[2]
assert(#spr.frames == 1)
assert(#lay.cels == 1)

-- Step 1: Execute standard NewFrame (duplicates active frame & copies cel)
app.command.NewFrame()
assert(#spr.frames == 2)
assert(#lay.cels == 2)

-- Step 2: Undo NewFrame
app.undo()
assert(#spr.frames == 1)
assert(#lay.cels == 1)

-- Step 3: Redo NewFrame -> Crashes with STATUS_HEAP_CORRUPTION (0xc0000374)
print("Executing app.redo()...")
app.redo()
print("Redo succeeded cleanly!")
spr:close()
