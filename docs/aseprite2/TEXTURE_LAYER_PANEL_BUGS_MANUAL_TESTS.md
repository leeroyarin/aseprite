# Manual tests — fix/texture-layer-panel-bugs

These steps cannot be driven by a Lua script. Run them on a Windows
MSVC build of this branch, in the Easymetric studio, before merge.
All must pass. Record the build commit, and the pass/fail of each
step, in the PR body.

## Setup

1. Open a sprite with Surface_Base, Surface_Texture and
   Surface_DEPTH layers, each face 32x32, and enter the Easymetric
   studio.
2. Paint a different marker in each face on Texture, and a
   different marker on Base, so a whole-sheet move is visible.

## Bug 1 — editing part of the texture must not move the whole base map

1. In the Front panel, draw a small rectangle selection inside the
   face (not the whole face).
2. With the Move tool (V), drag inside that rectangle.
   - PASS: only the rectangle's pixels move. No other face, and
     no Base marker, moves. The sheet's UV positions do not shift.
   - FAIL: the whole face, the whole sheet, or any Base marker moves.
3. Repeat with the selection tool's transform handles (drag a
   corner handle on the same small rectangle).
   - PASS: only the rectangle transforms.
4. Make a small selection, click the Layers panel, then click back
   into the same face panel.
   - PASS: the small selection is still there, not replaced by the
     whole face rect.
   - FAIL: the selection becomes the whole face.

## Bug 2 — panels must not resize while drawing

1. With the brush, draw a stroke that starts a few pixels inside a
   panel edge and crosses towards the panel border.
   - PASS: the stroke paints, no panel resizes, no resize cursor.
2. Draw a long stroke in the middle of a panel.
   - PASS: panel widths/heights and panel zoom/scroll do not change
     during or after the stroke.
3. Deliberately drag a splitter in the gap between two panels.
   - PASS: only the two columns either side of that boundary
     change, and only while dragging the gap itself.

## Bug 3 — the active layer must not switch by itself

1. In the Layers panel, click Surface_Base, then draw in any panel.
   - PASS: the stroke goes on Base, and the Layers panel still
     shows Base as active after the stroke.
   - FAIL: the active layer flips to Texture, or the stroke lands
     on Texture.
2. Repeat with Surface_DEPTH selected.
   - PASS: stroke goes on Depth, active layer stays Depth.
3. Lock Surface_Texture, then with the Move tool (V) and auto-select
   on, click on a texture pixel.
   - PASS: the locked layer is NOT auto-selected and does not move.
4. Hide Surface_Texture, then with the Move tool and auto-select
   on, click where it was.
   - PASS: the hidden layer is NOT auto-selected.
5. With the brush tool, click inside a named surface border.
   - PASS: the active layer does not change (symmetry axes may
     still align, that is not a layer change).
