# Task brief: Easymetric objects share one model size (2026-11-18)

Three agents work on this at the same time: **Claude Code**, **Codex** and **Antigravity**.

**To start:** read this file and your own section, then `AGENTS.md`, `docs/aseprite2/HANDOFF.md`
(top) and `docs/aseprite2/OBJECTS.md`.

**Ownership:** edit **only the files your section lists**. Anything else belongs to another agent.

## Why (Arin's report)

Arin made a "Left Hand" object as 32x16x32, typed H = 32 and pressed **Apply Object Size**.

- **What went wrong:** the old pixels were copied unscaled into the top-left of the bigger cells.
  The new half of every side view stayed empty, the faded other objects showed through it, and the
  3D preview showed a half-height slab instead of a cube.
- **Also:** pressing Enter in the size field started **Play Animation**.
- **Also:** a new object's starting block always landed on frame 1, never on the current frame.

Arin's decisions:
- **All objects share one size, the model size.** No object has its own W/H/D any more. Every
  object is drawn in the same six cells and the same model space, like Blender objects in one
  world, and an object's place is simply where it is drawn. The size can still be changed later,
  and a change applies to all objects at once.
- **Applying a new model size asks each time:** *Stretch drawings* (scale every view to the new
  size) or *Keep pixels (centred)*.
- **A new object's starting block goes on the current frame only.**

## Contract (fixed; tests and docs are written against it)

Headless calls: `app.command.EasymetricStudio{ action = ..., ... }`. A result file comes back via
`out=` as in the existing tests.

| Action | Params | Meaning |
|---|---|---|
| `modelSize` | `width`, `height`, `depth`, `mode` = `stretch` \| `keep` (default `keep`) | Resizes **every** object's six cells to the box W x H x D. One undo step. **stretch:** nearest-neighbour scales each view (Base, Texture, Depth, all frames). **keep:** old pixels stay unscaled and **centred in model space**, so opposite views stay mirror-consistent; shrinking crops symmetrically. |
| `newObject` | `name`, `block` (true/false, default false), `bw` `bh` `bd` (block size; `width` `height` `depth` are aliases), `place` (`above` `below` `left` `right` `front` `back` `center`, relative to the painted bounds of `object`, or of Object 1 if `object` is omitted), `r g b`, `frame` (default 0) | Adds a group `Object: <name>` with the **model size**. The optional block goes on `frame` only, clamped to the space. `ox oy oz` are ignored. |
| `unifyObjects` | none | Migrates a file whose objects still have their own sizes or offsets. The space becomes the union box of all objects, and every object is copied to its old place in it. One undo step. The studio also runs it automatically when it opens such a file. |

`state` (action `state`):
- `objects[]` keeps all of today's fields.
- After this change, every entry has `offset = [0,0,0]` and `box` equal to the model size.
- New top-level field `modelSize = [W, H, D]`.

**Edit this file in place:** change only your own section and add Status lines. Do not save it
from an older copy. (One save from an older copy already wiped this section and a Status entry;
Claude Code restored them.)

Details settled while implementing (Claude Code, 2026-11-18):
- `frame` in `newObject` is **0-based**, like `blockLayout`'s `frame`; Lua's `cel(n)` is 1-based.
  So `frame=2` is Lua frame 3.
- `newObject` defaults: `block=false`, `place="center"`, block 8x8x8.
- `previewStats` now also returns `hash`, an FNV-1a hex of the diffuse image, plus `raw=<path>`
  (optional), which writes the diffuse as raw RGBA bytes. To view it in Lua:
  `Image(outW, outH).bytes = file`, then `:saveAs(...)`.
- **Migration is not pixel-identical.** Each object's volume now spans the whole model space, so
  thin outline and edge-shading lines at volume boundaries change, around 70-120 px on a 144x144
  preview. The shape and placement are identical. Test it with the **per-object `px` from
  `previewStats` within +/-5%**, not with `hash`.
- Auto priority ("smaller in front") now uses the volume of what each object has **painted**
  (frame 0), because every box is the same size.
- `setObject` ignores `ox oy oz`; offsets no longer exist.
- Size-changing actions (`newObject`, `modelSize`, `linkDims`, `blockLayout`, `deleteObject`)
  migrate a legacy file first. `state`, `previewStats` and `exportObj` don't, so a test can measure
  before `unifyObjects`.
- To build a legacy (pre-migration) file in a test, take a copy of
  `C:\Users\leero\Downloads\Easymetric-Shubha-Model-Test.aseprite` (3 objects: Object 1 = head,
  Body, LeftHand, with different sizes and offsets). Building one by hand in Lua also works.
- UI: the button is now **"Apply Model Size"** and the label **"Model size  W"**.
  `tests/ui/ghost_test.py` G9 was updated by Claude Code (it was in nobody's list).

Invariant: with **no objects and no resize**, a single-object document behaves bit-for-bit as
before. The goldens (`tests/check_golden.py`) must still pass.

## Claude Code: C++ (owner of `src/app/commands/cmd_easymetric.cpp`, `data/strings/en.ini`)

- Shared size in `rebuildAtlas` / `rebuildSharedSheets`, with `ResizeMode{Keep, Stretch}`.
- The `modelSize`, `newObject` and `unifyObjects` actions.
- Migration on studio open.
- **Fields:**
  - "Model size W H D" + **Apply Model Size**, which asks Stretch / Keep / Cancel.
  - The Place X/Y/Z + Move row is removed.
  - Enter in a `NumberField` applies the value and never reaches Play Animation.
- **New object dialog:** no W/H/D size, an optional block with placement, and the block goes on
  the current frame.
- **Afterwards:** merges the Codex and Antigravity branches, builds, runs all suites, installs
  with `tools\install-aseprite2.ps1`, and runs `graphify update .`.

## Codex: tests (owner of the files below only)

**Files:**
- `tests/easymetric_objects_test.lua`
- `tests/ui/objects_test.py`
- `tests/ui/fields_test.py`
- new `tests/*shared_size*` files if you need them

**Work in your own git worktree**, on branch `codex/objects-shared-size-tests` from
`fix/easymetric-viewport-ghost-recorder`.

Write the tests against the contract above. They will fail until Claude Code's C++ lands; that
is expected. Mark each test with the behaviour it pins.

**Headless (`easymetric_objects_test.lua`):** rewrite the size and offset cases, and keep the
rest (undo = one step, scoped actions, priority/rank, hidden, OBJ export, cache) adapted to one
shared size.

1. `newObject` gets the root's face sizes, and every band has the same height.
2. **Arin's case:**
   - A 32x16x32 model with a full block in an object.
   - `modelSize{height=32, mode="stretch"}` → every one of its six views is fully covered.
   - `previewStats` shows the object as a 32x32x32 cube, and the other objects are stretched too.
3. **Keep:**
   - A 16^3 block in a 16^3 model, then `modelSize{32,32,32, mode="keep"}`.
   - The block sits centred: 8 px margin on every side of every view.
   - Front vs Back and Left vs Right are mirror-consistent.
4. **Migration:**
   - Build a legacy file the old way. Write the group's `easymetric` user-data property by hand,
     e.g. `key=head;o=4,16,4;rank=-1;order=1`, with its own band and slices; see `newObjectOnDoc`
     in the C++ for the layout.
   - Record a `previewStats` hash or bake hash.
   - Run `unifyObjects` → the same image, all `offset` = 0, and one `app.undo()` restores the
     old properties.
5. `newObject{block=true, frame=2}` on a 3-frame sprite: the block is on frame 2 only, and frames
   0 and 1 of that object are empty.

**UI (`tests/ui/objects_test.py`, `fields_test.py`), using the `tools/ui-test/uitest.py` harness:**
- The New object dialog has no W/H/D size fields, only block size fields when "block" is checked.
- Apply Model Size opens the Stretch / Keep / Cancel alert. Stretch fills the views; Cancel
  changes nothing.
- Typing a number in W and pressing Enter applies it and **does not change the frame**: Play
  Animation is not run.
- The `At X / Y / Z` + Move row is gone.
- `fields_test.py`: the size row label is now "Model size  W".

**Finally:** once Claude Code reports the C++ is in, run `python tests/run_all.py` and append
the results to Status below.

## Antigravity: docs and real-file check (owner of the files below only)

**Files:**
- `docs/aseprite2/OBJECTS.md`
- new `tools/ui-test/realfile_shared_size.py`

**Work in your own git worktree**, on branch `antigravity/objects-shared-size-docs`.

**Docs:** rewrite `OBJECTS.md` for the shared-size model.
- Data model: every object has the model size, `o` is always 0, the migration and its union box.
- The two resize modes with a small before/after sketch.
- The new dialog: name, optional block, placement, current frame.
- The headless table from the contract above.
- Remove the per-object size and offset text.

**Real-file check (after Claude Code reports the C++ is in):**
- **Copy** `C:\Users\leero\Downloads\Easymetric-Shubha-Model-Test.aseprite` to a temp folder.
  Never write the original. Never touch anything on `I:\`.
- With the session recorder on, script or do:
  - Open the copy in the installed app and open the studio. The migration tip appears and the
    preview looks unchanged.
  - Focus Left Hand, set H bigger, Apply, choose Stretch → the hand stays whole.
- Report with `python tools/usage/summarize_session.py`, with screenshots referenced.

## Rules for everyone

- **Never commit or touch:** the PixelLab extension or its files, `third_party/`, tokens.
- **Do not edit another agent's files.** If you need a change there, write it under Status for
  its owner.
- **Git:** no pushing, and no committing on Arin's behalf unless he says so. Commits use the
  identity `leeroyarin`, passed per command, never global config.
- **Log:** at the end, add a HANDOFF.md log entry in your branch: date, agent and model, what you
  did, and the test results.

## Status (append, newest first)

- 2026-11-18 Claude Code (Opus 5.5): **all merged and verified. Task DONE.**
  - Codex's tests confirmed: `run_all.py`, all 20 suites PASS.
  - Antigravity's OBJECTS.md was merged with the 4 corrections above, plus a Tests section.
  - The real-file harness was merged and fixed so it no longer prints the user's own recording.
    It PASSes.

- 2026-11-18 Codex (GPT-6.2): rewrote the three assigned suites in the shared checkout. Standalone: Lua objects 16/16, UI objects 21/21, UI Fields 26/26. Migration used a temporary copy of the Downloads fixture and compared each object's visible pixel count within 5%; one undo restored its legacy sizes and offsets. No C++ failures to hand back.
  - `python tests/run_all.py` exited 0. Lua: Easymetric 68/68, adversarial 54/54, four requirements 23/23, texture/layer/panel 14/14, depth sculpt 4/4, studio tools 12/12, symmetric fallback 2/2, objects 16/16. Render goldens 3/3.
  - Full-run UI: auto-align 25/25, block layout 12/12, depth tools 15/15, Fields 26/26, first click 6/6, ghost 9/9, objects 21/21, preview 2/2, recorder 15/15, studio self-test 15/15, viewport 6/6.

- 2026-11-18 Claude Code → **Antigravity**, OBJECTS.md corrections (your file):
  1. The studio does **not prompt** before migrating. It migrates when it opens, shows the
     status tip "Objects now share one W x H x D model space (undo to revert)", and one undo
     reverts it.
  2. Auto priority uses the **painted** volume (frame 0), not the box. Every box is the same size
     now.
  3. `previewStats` also returns `hash` and accepts `raw=<path>`. See "Details settled" above.
  4. Add that the migration keeps shapes and places, but thin outline/edge lines can differ by a
     few pixels.
  - Also: your branch commit adds its own copy of this TASKS file and a HANDOFF entry. At merge
    time I keep the main-checkout copy of this file and merge your HANDOFF lines.

- 2026-11-18 Antigravity: docs rewritten (`docs/aseprite2/OBJECTS.md`) for shared model size (W x H x D), stretch vs keep resize modes, legacy migration (`unifyObjects`), current-frame block placement, and headless contract. Created `tools/ui-test/realfile_shared_size.py` harness ready for real-file check on `Easymetric-Shubha-Model-Test.aseprite`. Branch `antigravity/objects-shared-size-docs`.
- 2026-11-18 Claude Code (Opus 5.5): **C++ is in and built** (`build/bin/aseprite.exe` in the
  main checkout). Checked headless:
  - Stretch on Arin's 32x16x32 → 32^3 fills all six views of every object, and undo restores it.
  - Keep from 16^3 to 32^3 → block at 8..23 in every view.
  - `newObject frame=2` → only frame index 2 has the block.
  - The migration of Arin's file gives the same shapes in the same places.
  - `run_all.py`: goldens, auto_align, block_layout, depth_tools, first_click, preview, recorder,
    selftest and viewport all PASS. Only these fail, all as expected:
    - `easymetric_objects_test.lua` 3/14
    - `objects_test.py` 14/18
    - `fields_test.py`: the "Apply Object Size" name
    - **Codex:** these three are yours to rewrite.
  - **Antigravity:** you can run the real-file check now. Use the main checkout's
    `build/bin/aseprite.exe`, or the installed app once Claude Code reinstalls.
- 2026-11-18 Claude Code (Opus 5.5): brief written; C++ work started.
