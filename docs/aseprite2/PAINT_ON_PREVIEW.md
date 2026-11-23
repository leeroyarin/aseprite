# Paint on the 3D preview — design note (Phase 3, NOT built)

Status: proposal for Arin to decide on. Nothing here is implemented. Code references are to
`src/app/commands/cmd_easymetric.cpp` as of 2026-11-16.

## 1. Picking a click to a face and pixel

**Rule: the renderer records what it drew; picking only reads that record.** No inverse mapping
is written.

- `compute3DPreviewImages` already decides, for every output pixel, which face the ray hit
  (from the voxel gradient at the hit) and which `(u, v)` of that face it samples (the
  `switch (face)` on the hit point). It keeps only `faceMap` (face per pixel), and only locally.
- Extend `Render3DResult` with a **pick buffer**: per output pixel `{face, u, v}` (or -1). Store it
  next to the images in the studio. This is the same mapping the picture came from, so a click
  always lands on the pixel the user is looking at, including at sculpted depth, tie-broken
  corners and after a zoom or orbit.
- Screen → output pixel: the preview widget draws the image at `(dx, dy)` with
  `scale = fit * m_zoom` (`EasymetricPreviewWidget::onPaint`). `ix = (x - dx) / scale`,
  `iy = (y - dy) / scale`. When several maps are shown side by side (Diffuse / Normal / Depth),
  use the cell under the mouse; they share one geometry and one pick buffer.
- Two corrections before the pixel is written:
  1. **Linked columns**: a hidden lower face (Back, Left, Bottom while linked) is a mirror of its
     partner. Redirect the write to the shown face at the mirrored `(u, v)` (the same flips as
     `syncOpposingCels`), so the link keeps holding.
  2. **Empty-face fallback**: `SixSurfaceData::sampleSymmetric` shows the OPPOSITE face's
     pixels when a face is empty. A click there must go to the face actually shown, at the
     mirrored `(u, v)`. (That fallback used to be unmirrored; fixed 2026-11-16, see HANDOFF.)

## 2. Turning it into a normal, undoable edit

Reuse the depth-stroke pattern already in the studio (`paintDepthDab` / `endDepthStroke`):

- Mouse down opens one `Tx` ("Paint on Preview"); the first changed pixel installs a work copy
  of the cel image with `DocApi::replaceImage` (so undo stores the original), and later dabs edit
  that copy in place. Mouse up commits. **One stroke = one undo step**, like a panel stroke.
- Target layer = the active sheet layer (`activeSheetLayer`), exactly what a panel stroke would
  write to; frame = the active panel's frame.
- Between two mouse samples, draw a line in `(u, v)` **only if both samples hit the same face**;
  across a face edge, end the segment and start a new one. That avoids lines cutting across
  the sheet between unrelated cells.
- After each dab, `notifySpritePixelsModified` updates the panels; the preview re-renders on its
  normal throttled poll.
- Auto-align: preview strokes should NOT trigger it. Rays only hit existing surface, so they
  cannot move a silhouette sideways. `onAddUndoState` already ignores transactions that do not
  come from a busy panel editor.

## 3. Tools, first version

- **Pencil** (1 px, foreground colour) and **Eraser** (1 px, transparent). Brush size, other inks,
  fill and symmetry come later.
- **Input**: a "Paint" toggle on the Preview header. While it is on, and the active tool is the
  pencil or eraser, left-drag paints and **Space + left-drag orbits**. Right/middle-drag pan,
  wheel zoom and double-click reset stay as now. With the toggle off, nothing changes, so the
  current orbit habit is safe.
- Cursor: crosshair, plus a 1-face-pixel outline at the picked texel so it is clear which pixel
  will change.

## 4. Risks

1. **Eraser on Base drills.** Erasing Base carves the volume, so the next re-render exposes the
   surface behind, and a continued drag keeps erasing deeper. Mitigation: freeze the pick buffer
   for the whole stroke (pick from the image shown at mouse down), and in v1 restrict the eraser
   to the Texture layer unless Base is explicitly active.
2. **Resolution.** The preview image is about `2 * max(object size) + 16` px wide and is scaled to
   the pane. At low zoom one screen pixel can cover several face texels, so precise work needs
   zoom. Showing the picked-texel outline (section 3) makes that visible instead of surprising.
3. **Lag.** Re-renders are throttled to max(120 ms, 3 x render time) while a stroke is active.
   Feedback would trail the cursor by that much at 128-voxel sizes. Mitigation: paint the dab into
   the displayed image immediately (cheap, using the pick buffer), and let the full re-render
   catch up.
4. **Sculpted and corner surfaces.** On depth-sculpted walls the face is chosen from the voxel
   gradient, so a slanted wall maps to one face with stretched texels. That is correct for the
   data model, but painting there can feel uneven. Corners tie-break by axis order. Because
   picking uses the renderer's own record, what you click is still what you see.
5. **Stale pick buffer.** Any document change (undo, panel stroke, auto-align) invalidates it.
   Rebuild it with each render and reject a mouse down until the first render after a change.
6. **Testing.** Needs a state hook (e.g. `previewPick(x, y)` → `{face, u, v}`) so the UI harness
   can assert picks without screenshots, plus a test per correction (linked mirror, empty-face
   fallback, face-edge stroke split, undo).

## 5. Rough size

The pick buffer is small, and so are the input mode and cursor. The stroke itself is moderate
because it reuses the depth-stroke code. Tests are moderate. The `sampleSymmetric`
prerequisite is already done.
