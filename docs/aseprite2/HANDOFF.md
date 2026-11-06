## 2026-11-06 — Four-requirements back to 23/23: TC 4.6 code fix + 9 outdated tests updated (Draft PR)

- William, 2026-11-06: "Try using simulation" / "Simulate all one by one", then "Do both one by one" — fix the one genuine code failure (TC 4.6) and update the 9 outdated tests, in that order.
- Simulation first (harness + logs in workspace goals/aseprite-easymetric-development/files/simulations-2026-11-06/sim3-four-req/): a Python model of the old vs new setup behaviour reproduced the recorded result exactly — OLD 23/23, NEW 13/23, same 10 failures, zero mismatches. Verdict per test: 9 fix-the-test (they assert the old 2-layer setup, canvas expansion, and pre-filled box that 54114e3d6 deliberately removed), 1 fix-the-code (TC 4.6).
- Fix 1, code (src/app/commands/cmd_easymetric.cpp, +18/-1, rebuildSharedSheets only): the slice loop only updated slices that already existed, so linkDims on a document with only "surface_front" left the other five faces sliceless (#slices = 1, not 6). It now creates a missing face slice with the same AddSlice pattern setupSharedSurfaceLayers already uses (face name, face colour, UV rect key). The studio needs all six face slices to exist; sharedFaceSizes falls back to sprite/3, sprite/2 without them.
- Fix 2, tests (tests/easymetric_four_requirements_test.lua): TC 1.2 fills the Base sheet white on a 72x48 sprite (divides into 24x24 faces, the same volume the old starter-filled sprite produced) before baking, instead of relying on the removed pre-filled box. TC 2.3-2.8 build the shared Base + Texture sheets explicitly first (new makeTwoLayerSheets helper: rename layers[1] to Surface_Base, newLayer Surface_Texture with a cel), then run the action under test — setup no longer creates a Texture sheet from a blank sprite, so the setup step moved into the tests; the assertions (smart-object conversion, group conversion, alpha blend, base fallback, sheets + slices) are unchanged. TC 2.5 additionally asserts 6 slices; TC 2.8 now asserts the canvas stays 24x24 with 8x12 face slices instead of expanding to 72x48. TC 4.2/4.3 expectations updated to the divided starting size: unchanged faces 8x12 (not 24x24), canvas 48x60 (not 80x72) and 80x52 (not 112x64); the resized face itself and the face-independence property are unchanged.
- Verified on Linux (final binary): build exit 0 (only the pre-existing unused-variable `bw` warning); four-requirements 14/23 after fix 1 alone (only TC 4.6 newly passing, the other 9 still failing as expected), then 23/23 after fix 2; regression 67/67, adversarial 54/54, texture/layer/panel 14/14 — no regressions. Four-requirements process exit is 139, the known pre-existing exit-time segfault from PR #14 (in-suite 23/23 pass, zero Easymetric frames in it). Linux build used worktree base e55f5ffd3 with the two changed files copied in and restored clean afterwards, so the Windows MSVC build is still needed. Simulation predictions matched both real runs exactly (14/23, then 23/23).
- NOT in this PR: Depth as a separate tool, depth geometry. Windows test still needed: run tests/run_easymetric_tests.ps1 on the MSVC build.

## 2026-11-05 — Panel action buttons replace the six-dot gripper (PR #30, rebased onto #29)

- William, 2026-11-05: the six-dot gripper in each panel header is dead space; replace it with action buttons — symmetry toggle for X and Y, mirror horizontal and vertical, and a Link button tying the panel to animation playback (linked panels follow the playing animation; touching Link to unlink removes that dependency).
- The dots were decorative only (PaneHeader::onPaint drew 3x2 dots centred in the header). They are gone from every header, including Preview and Fields (which get no buttons, title only).
- New, one file (src/app/commands/cmd_easymetric.cpp, +402/-44): a PaneActionButton class (same blue-when-on / slate-when-off family as LinkToggleButton) and five buttons per face header, right-aligned in priority order Link, SX, SY, FH, FV; lower-priority ones hide when the header is too narrow. Labels are abbreviated (SX = Symmetry X, SY = Symmetry Y, FH = Flip Horizontal, FV = Flip Vertical) so all five fit alongside the title tab and, on top faces, the dimension-link button — full "Sym X"/"Sym Y" labels did not fit (Flip V hid on every face, Flip H hid on top faces). The Fields panel still shows the full "Sym X"/"Sym Y" names.
- Sym X / Sym Y: toggles set the same document symmetry bits as the Fields checkboxes, via one shared applySymmetryState (the Fields checkbox lambda now calls it too). All six headers' buttons and both Fields checkboxes show the same state. The studio now also opens showing the document's actual symmetry mode (syncSymmetryButtons) instead of always showing symmetry off.
- Flip H / Flip V: mirror one face's pixels within its UV rect, on the CURRENT frame, on every shared layer that has a cel on that frame (Base, Texture, Depth), in one transaction = one undo (app::cmd::FlipImage). Slice bounds unchanged; preview refreshes via the poll. With the current single-layer head this flips Surface_Base; if Texture/Depth layers exist they flip too.
- Animation Link: per-panel state (m_animLinked, default true; m_animPinnedFrame). EasymetricMultiPaneView implements EditorObserver, registered on all six pane editors (unregistered in the destructor). onAfterFrameChanged: if the changed editor IS the active editor, its frame is user intent — an unlinked active panel adopts it as its new pinned frame, and every LINKED panel follows it via setFrame (no-op when equal, so no loop). If the changed editor is NOT the active editor and is unlinked and not the playback editor, it is restored to its pinned frame — this also catches DocView::setActivePaneEditor, which forces the clicked panel to the old active frame BEFORE making it active, so the restore is flicker-free. Unlinking pins the panel at its current frame; re-linking syncs it immediately to the active editor's frame. Stated limitation: an unlinked panel that IS itself the playback editor still advances during playback (PlayState drives the editor captured at Play start; pinning it would need an upstream timeline/playback change, out of scope). The dimension-link button (linked column dimensions) is untouched and is a different link — do not confuse the two.
- Verified on Linux (final binary): build exit 0; suites 67/67 + 54/54 + 14/14; four-requirements 13/23 — the same 10 failures as the merged head alone (54114e3d6 single-layer change), no new failure. GUI harness (Xvfb, real GUI, 2-frame sprite): all 7 checks pass — Sym X buttons blue on all six headers and a pencil dot mirrors across the face centre; Sym Y buttons blue on all six headers; Flip H moves the Top marker top-left to top-right (400px); Flip V moves the Front marker top-left to bottom-left (400px); Link off on unlink, unlinked Top stays on frame 1 (brown) while linked Front follows to frame 2 (green), re-linking Top syncs it to frame 2 (red).
- NOT in this PR: Depth as a separate tool, depth geometry. Windows test still needed: all five buttons visible on every face header at your window size, Sym X/Y toggle and mirror as you draw, Flip H/V on a face with content, Link unlink/re-link while playing an animation.

## 2026-11-05 — One common draw-only fix for all panel resize/move paths (Draft PR)

- William, 2026-11-05: "Common fix that panel sizing issue and canvas issue. I am tired of mentioning you time after time." Fair — PR #22 (splitter cursor), PR #26 (splitters), PR #27/#28 (slice cursor) each fixed one symptom. This PR audits ALL remaining resize/move paths inside a panel editor and closes them under one rule, in one file: an Easymetric surface panel is DRAW-ONLY. Sizing lives in Fields and the one Preview/Fields splitter; nothing the mouse does inside a panel may resize or move anything.
- Layout half was already fixed (PR #26: splitterGapAt returns 13 only for Preview/Fields, surface columns fixed equal). Slice half was PR #28 (calcHit returns None for panels). What remained, all inherited from StandbyState by the panel editors:
  - Symmetry handles: Decorator::getSymmetryHandles runs first in onMouseDown; handles sit at the ends of the symmetry axis lines, i.e. at face edges. Dragging one moved the document symmetry axis from a canvas edge.
  - Selection transform handles: Decorator draws scale/rotate/skew handles at selection corners/edges when a selection tool is active and a mask is visible; grabbing one started MovingPixelsState with a scale handle.
  - Move tool: the PR #14 Bug 1 redirect (move selected pixels instead of the whole sheet) fired on EVERY Move drag, because with no mask isInsideSelection() is true for the whole cel — and the studio sets the document mask to the active face's UV rect, so the drag moved the whole face's pixels, spilling 20 columns into the neighbour face (harness-proven: 3120 px changed, left strip transparent, brown spilled into Front).
- Fix, one file only (src/app/ui/editor/standby_state.cpp, +100/-5): one helper isEasymetricPanelEditor (clipSpriteBounds non-empty, only panels set it) + isStudioFaceMask (mask bounds == clip bounds means the studio's face mask, not a user selection). Guarded: onMouseDown symmetry-handle block skipped for panels; onMouseDown cel branch — Bug 1 redirect only for a real visible user sub-selection (mask strictly inside the face rect), otherwise swallow; transform-handle block skipped for panels; onSetCursor cel branch — Move cursor only with a real sub-selection, else arrow; Decorator::onSetCursor / postRenderDecorator / getInvalidDecoratoredRegion return early for panels (handles not drawn — a handle you can't grab is a lie; the symmetry axis line itself, drawn elsewhere, stays). EasymetricDepthState inherits it all. Main editor untouched. Moving selected pixels inside a face still works; moving a selection's edges/pixels (normal drawing work) is unchanged.
- Verified on Linux (final binary): build exit 0; suites 67/67 + 54/54 + 14/14; four-requirements 13/23 — the same 10 failures as the merged head alone (54114e3d6 single-layer change), no new failure. GUI harness (Xvfb, real GUI, PNG-diff ground truth): Move drag with no selection = 0 differing pixels (was 3120); marquee shows zero transform-handle pixels at corners and a corner drag changes 0 pixels; edge drag draws a stroke and leaves the Top slice 117x78. NOT verified: symmetry-handle drag in the GUI — symmetry was OFF in the harness session and the Fields Sym X checkbox sits behind the floating Preview window (known harness limit; the guard is the same !isEasymetricPanelEditor pattern as the proven transform-handle guard). Preview/Fields splitter: observed moving in the harness (564 -> 626 -> 564 across drags) but synthetic drag direction mapping was inconsistent (drag starts at the gap edge land inconsistently); PR #26's own harness proved it, and this PR does not touch that code.
- NOT in this PR: panel action buttons (separate PR, next); Depth as a separate tool; depth geometry.
- Windows test still needed: no resize cursor at any face edge/corner, no symmetry/transform handles in panels, Move tool with no selection does nothing in a panel, a real sub-selection still moves with Move, Preview/Fields drag works at your window size.

## 2026-11-05 — Canvas-edge resize cursor in surface panels (PR #27, superseded by clean PR #28)

- William, 2026-11-05, build b9cb6c7c8 (PR #26 merged): the resize cursor STILL appears at the edge of the canvas, e.g. the vertical double-arrow at the bottom edge of the Top face. PR #26 removed the pane splitters, but this cursor was never a splitter — it is the slice-border resize cursor. Every Easymetric face IS a document slice (bounds = face UV rect), and panel editors inherit StandbyState, whose onSetCursor/onMouseDown hit-test slice borders via Editor::calcHit (within 4*guiscale of a border) and show a resize cursor / start MovingSliceState there.
- Proof in the Linux harness (old code): a drag starting at the Top face bottom edge shrank the Top slice from 117x78 to 117x58 in the saved file, and drew nothing. With this fix the same drag leaves the slice at 117x78 and draws a white pencil stroke instead (30/30 pixels on the stroke line).
- Fix, one place: Editor::calcHit returns None when clipSpriteBounds is non-empty. clipSpriteBounds is only set by Easymetric panels, so the main editor is unchanged, slice borders are still drawn, only their hit-test is off. Sizing lives in Fields only, never from a canvas edge.
- Verified on Linux: build exit 0, suites 67/67 + 54/54 + 14/14 + 23/23 on its original base. Note: the merged head also contains 54114e3d6, whose single-layer change breaks 10/23 four-requirements tests on its own (13/23, same failures with and without this PR) — that is a separate issue from 54114e3d6, not from this PR.
- NOT in this PR: Depth as a separate tool, depth geometry.

## 2026-11-05 — Fix crash on state change cursor check + preserve single layer in Easymetric
- Fix Access Violation crash: in `src/app/ui/editor/moving_pixels_state.cpp:453` `MovingPixelsState::onSetCursor()`, added null check for `m_pixelsMovement` before calling `isDragging()`. When leaving state on frame/layer change or command execution, `removePixelsMovement()` resets `m_pixelsMovement` before `notifyGeneralUpdate()` triggers cursor updates while state is still `MovingPixelsState`. Also added null guard in `getCurrentKeyAction()`.
- Fix Easymetric single-layer preservation: in `src/app/commands/cmd_easymetric.cpp`, `setupSharedSurfaceLayers` previously required BOTH `existing.baseLayer && existing.textureLayer`. If user deleted extra/depth layers, it re-ran migration, recreating `Surface_Texture` and `Surface_DEPTH` and overwriting cel 0 of `Surface_Base`. Updated to `existing.baseLayer || existing.textureLayer` and added fallback in `findSharedLayers` to support a single base layer (`Surface_Base`, `Base`, or sole image layer) without adding unneeded layers or replacing existing frames/cels.
- Verified: clean build, all 68 Easymetric E2E tests passing, installed new executable to `%LOCALAPPDATA%\Programs\Aseprite 2.0\aseprite.exe`.

## 2026-11-04 — Remove surface splitters, keep only Preview/Fields splitter (PR, draft)
- William, 2026-11-04: resize cursor kept appearing over surface panels (including at canvas edges and slightly left of a bar), a drag could entangle drawing, and the horizontal/vertical splitters never lined up with where the bar looked to be. Sizing already lives in Fields (W/H/D, Size sliders), so surface splitters are removed entirely: no resize cursor, no drag, no painted bars for vertical boundaries 1-3 or horizontal boundaries 0-2. Surface columns are fixed equal widths, surface rows fixed equal heights.
- Only remaining splitter is Preview/Fields (column 3, horizontal, hit area exactly the painted bar, centre grip). It is not a surface panel, so it cannot entangle a drawing stroke. Linux harness drag test: dragging that bar up shrank Preview and grew Fields, so Fields vertical resizing does work through that bar (previous per-column row splits meant dragging a surface column's bar did NOT move Fields, which is why Fields seemed not to resize).
- Verified on Linux: build exit 0, suites 67/67 + 54/54 + 14/14 + 23/23, harness screenshots show no surface splitter bars and a working Preview/Fields drag. Windows test still needed: no resize cursor over any surface panel while drawing, Fields drag works at your window size.

## 2026-11-04 — Easymetric panel neighbour bleed + preview black background (PR #24, superseded by clean PR #25)
- Panel neighbour strips: in `src/app/ui/editor/editor.cpp` the Easymetric pane clip cover was enlarged by `5*guiscale` beyond the face rect, leaving a band of the shared 3x2 sheet visible around every face. That band is the neighbouring face's pixels (Top's right band is Front, Front's left band is Top, etc.) — exactly William's 2026-11-04 screenshot, where every solid-coloured face had a neighbour-coloured strip on its edges. Fix: cover starts exactly at the face rect edge. `clipSpriteBounds` is only set by Easymetric panels, so no other editor is affected.
- Preview black background: the Preview BG colour/alpha fill is drawn under the preview surface, but the scaled `drawSurface` path draws the image opaque in practice (Linux harness screenshot reproduced it: black rectangle, checkerboard only outside the image bounds, even though image background pixels are alpha 0). So setting BG to #000000 / alpha 0 still showed black. Fix, preview only: `render3DModel` now composites the BG into the preview image itself — background pixels (diff, norm and dept all 0, i.e. no ray hit) get the chosen BG colour blended over a checkerboard, or the checkerboard itself when alpha is 0, never black. Changing the BG picker/alpha now forces a re-render. Baked Iso layers keep the original RGB images unchanged.
- Verified on Linux: build exit 0, suites 67/67 + 54/54 + 14/14 + 23/23, plus a GUI harness screenshot where the preview background is checkerboard, not black, with the model visible against it. Windows test still needed: neighbour strips gone at your face sizes, BG picker + alpha 0/mid/255 in both embedded and floating preview.

# Aseprite 2.0 — HANDOFF (live status)

**Read first if you are a new agent.** Update the checkboxes + Log before you stop working.
Design = [PLAN.md](PLAN.md). Contract = [PROTOCOL.md](PROTOCOL.md). Version storage = [VERSIONING.md](VERSIONING.md).

## Environment facts
- Repo: `c:\Users\leero\Source\Repos\aseprite2` (Windows, PowerShell). Upstream HEAD at fork: `14fe117e8`.
- Build dir `build/` already configured: Ninja, `RelWithDebInfo`, MSVC, `LAF_BACKEND=skia`, `ENABLE_SCRIPTING=ON`, `ENABLE_WEBSOCKET=ON`, `ENABLE_UPDATER=ON`. Output `build/bin/aseprite.exe`.
- PixelLab extension (user-owned, v0.4.130):
  - zip: `C:\Users\leero\Downloads\PixelLab.aseprite-extension`
  - installed in stock Aseprite: `%APPDATA%\Aseprite\extensions\pixellab` (entry `plugin-menu.lua`, `init(plugin)` at ~line 1716 registers `plugin:newCommand`s; package.json contributes ~60 scripts + keys).
- User also has PixelLab **MCP** configured in Antigravity (`C:\Users\leero\.gemini\antigravity-ide\mcp\pixellab`) — useful for headless generation + `import_image`.
- Git status at start: `third_party/libwebp` modified, `third_party/fmt` untracked-content — pre-existing, not ours, leave alone.

## Build & Run
```powershell
# Automated MSVC environment loader + build:
powershell -File tools\aseprite2-build.ps1

# Install to system (%LOCALAPPDATA%\Programs\Aseprite 2.0 + shortcuts + PATH + file associations):
powershell -File tools\install-aseprite2.ps1

# Launch Aseprite 2.0 + MCP bridge together:
.\start-aseprite2.cmd
# Or PowerShell:
powershell -File tools\start-aseprite2.ps1
```

## Status
- [x] **Phase 0** docs (AGENTS.md, docs/aseprite2/*) — fully completed and current
- [x] **Phase 1** branding — completed and verified
  - [x] `src/ver/CMakeLists.txt` version 2.0 (`2.0-dev+<commit>`)
  - [x] `src/ver/info.c` PACKAGE "Aseprite 2.0"
  - [x] `src/main/win/resources_win32.rc` ProductName / FileDescription
  - [x] `src/app/resource_finder.cpp` user dir Aseprite2 (`%APPDATA%\Aseprite2`)
  - [x] builds + `--version` verified
- [x] **Phase 2** versioning C++ — completed and compiled
  - [x] `src/app/versions/version_store.{h,cpp}`: save/list/restore/open/delete
  - [x] `src/app/commands/cmd_version.cpp`: SaveVersion, VersionHistory, RestoreVersion, OpenVersion, DeleteVersion
  - [x] `data/strings/en.ini` & `data/gui.xml` menus + Ctrl+Alt+S shortcut
- [x] **Phase 3** Lua bridge — completed and verified
  - [x] `src/app/script/security.cpp` whitelists aseprite2-bridge
  - [x] `data/extensions/aseprite2-bridge/` bundled extension with PROTOCOL.md v1
  - [x] Help > AI Bridge settings dialog
- [x] **Phase 4** Node MCP server — completed and built
  - [x] `tools/aseprite2-mcp/src/bridge.ts` WebSocket server on 127.0.0.1:8765
  - [x] `tools/aseprite2-mcp/src/index.ts` 25+ MCP tools exposed
  - [x] `tools/aseprite2-mcp/scripts/smoke.mjs` test passed (handshake, token, call correlation)
  - [x] `tools/aseprite2-mcp/README.md` full setup docs
- [x] **Phase 5** PixelLab auto-install + MCP — completed and verified
  - [x] C++ auto-import in `Extensions::autoImportPixelLab()` (checks stock install & downloads)
  - [x] Verified auto-imported into `%APPDATA%\Aseprite2\extensions\pixellab`
  - [x] `pixellab_status` and `pixellab_run` MCP tools
- [x] **Phase 6** Workflow Boosters — completed, built, and installed
  - [x] C++ Visual Diff (`VersionStore::computeVisualDiff` + `VersionDiffWidget` in `File > Version History...`) & `version_diff` MCP tool
  - [x] Native 1x + 4x Contact Preview Sheet Exporter (`export_preview_sheet` MCP tool + `File > Export > Export Preview Sheet (1x + 4x)...`)
  - [x] Rarity Ramp Swapper (`recolor_ramp`), Auto-Outline (`auto_outline`), Batch Shapes (`draw_shapes`), Centered Canvas Expand (`canvas_expand_centered`)
  - [x] Animation Tag & Game Engine Sprite Sheet Exporter (`tag_manage`, `export_spritesheet`)
  - [x] Easymetric 4-Surface Isometric Projection & 2.5D Surface Normal/Depth Generator (`easymetric_project`, `easymetric_template`, `surface_normals_depth` + interactive GUI dialogs under `Edit > FX` and `Edit > Easymetric & 2.5D Maps`)
  - [x] Stage-2 Post-Approval Publisher script (`Working Files/Guidelines/scripts/publish-approved-asset.ps1`)
- [x] **Verification** end-to-end
  - [x] Clean compilation: `aseprite.exe` exits 0 with `Aseprite 2.0 2.0-dev+14fe117e8`
  - [x] PixelLab auto-detected and imported
  - [x] Bridge WebSocket smoke test clean

## Open questions for user (defaults applied meanwhile)
1. Port 8765 OK? (default yes)
2. `lua.exec` default OFF? (default OFF)
3. Restore = replace after auto-backup? (default yes)

## Gotchas / notes
- `get_app_version()` is parsed by `app.version` (Lua) and `ase_format.cpp` (checks "1.1"); keep version semver-like (`2.0`, `2.0-dev`).
- `ver/CMakeLists.txt` git logic previously forced `1.x-` prefix; replaced so builds report `2.0-dev`.
- Lua `WebSocket` is client-only → MCP server must be WS server.
- Aseprite script security prompts on io/WebSocket for untrusted scripts → Phase 3 whitelists bundled bridge in `src/app/script/security.cpp`.
- `build/bin/data/extensions/aseprite2-bridge` mirrored from `data/extensions/aseprite2-bridge` so dev runs immediately find it.

## Log (newest first)
- 2026-11-05 — Codex: Built and reinstalled the current Windows app at
  `b9cb6c7c8`; the installed executable matches the build and remained running
  during a launch check. No regression suites were run in this install check.
- 2026-11-04 — Codex: Pulled `f4439b4c2` and built it with MSVC x64. Windows
  suites passed: Easymetric 68/68, adversarial 54/54, four requirements 23/23,
  texture/layer/panel 14/14, all exit 0. PR #19 was closed as superseded;
  its substantive additions are already in the base. Draft PR #10 conflicts
  with later Easymetric work and still requires integration and validation.
- 2026-11-04 — Codex: Verified the current Windows build at `d6be90e98` with
  `tools/aseprite2-build.ps1` (Ninja reported no work remaining). The resulting
  executable reports `2.0-dev+d6be90e98`. No test suites were run in this check.
- 2026-11-04 — Muse (ULTRON): Preview background changer + floating preview window, DRAFT PR.
  Fields "Preview BG" row: ColorButton colour picker + Alpha slider
  0-255. Alpha 0 (default) = checkerboard as before; 255 = solid
  colour; in-between = colour over checkerboard. Preview-only:
  baked Iso layers and exported .OBJ keep a transparent
  background. Fields "Pop out Preview" button moves the same
  preview widget into a moveable, sizeable floating window;
  "Dock Preview" (or closing the floating window) brings it
  back. Script action EasymetricStudio{ action="previewFloat",
  mode="pop|dock|toggle" } does the same. Suites: 67/67 + 54/54
  + 14/14 + 23/23. NOT yet GUI-verified: in the Linux harness
  the Fields pane is covered by Aseprite's own floating
  Preview window and its scrollbar drag did not scroll, so the
  pop-out click could not be driven headless; Windows click
  test still needed.

- 2026-11-04 — Muse (ULTRON): Horizontal splitter cursor misalignment fixed.
  The horizontal separator was painted centred on the row boundary
  [botY-bh/2, botY+bh/2), while splitterGapAt() hit-tested only
  [botY-bh, botY), so the resize cursor appeared half a gap above
  the visible bar and the lower half of the bar did not resize.
  Fix: paint the bar in the actual gap [botY-bh, botY). Vertical
  bars already matched their gap and are unchanged. Suites:
  67/67 + 54/54 + 14/14 + 23/23. Windows visual check still needed.

- 2026-11-04 — Antigravity (arin-pc): Built fresh Aseprite 2.0 binaries after pulling PR #20 merge and docs update (`100895df5`).
  - Human build: `build/bin/aseprite.exe` (22,031,872 bytes) compiled cleanly via `tools/aseprite2-build.ps1`, reports `2.0-dev+100895df5`.
  - Testing build: `build-test/bin/aseprite.exe` synced and verified against test suites (Easymetric 68/68 PASS, Adversarial 54/54 PASS).
- 2026-11-04 — Codex: Fetched and fast-forwarded local `Aseprite-2.0` to
  `668f9ab27`, retaining the local installation log. Recovery files are in
  `build/pull-recovery-668f9ab`; no build or runtime checks were run.
- 2026-11-04 — Codex: Installed the existing Windows build with
  `tools/install-aseprite2.ps1`. Installed executable matches `build/bin`,
  reports `2.0-dev+e85599bfc`, and remained running during a launch check.
- 2026-11-04 — Muse (ULTRON): Depth-sculpt tool + depth map from Base, DRAFT PR, headless-verified, GUI brush click NOT yet verified.
  Branch fix/easymetric-depth-sculpt, base 2cee30d. Note: this branch
  was built in the same worktree as PR #19, so its snapshot also
  contains PR #19's wheel-zoom revert guard and studio-open crash
  fix; if PR #19 merges first, this PR's remaining diff is only
  the depth work, and it still merges cleanly.
  UI placement chosen by Muse (William said "do it by yourself"):
  Fields "Depth Brush" checkbox + "Plus (bump) / Minus (carve)"
  combo + Brush slider 1-16, under the depth row. Tick it and drag
  in any surface panel; wheel still zooms while it is on.
  (1) Generate Depth from Base (button, also script action
  generateDepthFromBase): starts at 128 = main surface, clipped by
  Base (transparent outside the silhouette), gradient derived from
  the other views' thickness along the face normal, centred on the
  face's own mean thickness — so a flat face is flat 128 and a
  cylinder gives a curved gradient around 128. The centring formula
  is Muse's interpretation; William specified "starts at 128" and
  "gradient from other views", not the exact formula.
  (2) Brush: PLUS whiter, MINUS darker, step = 128 / D, D = pixels
  from surface to box centre along the face normal (Front/Back
  depth/2, Top/Bottom height/2, Right/Left width/2). Black 0 =
  deepest; one more MINUS past black = through-hole, depth
  transparent AND Base pixel cleared. One Tx per drag, so a drag
  is a single undo step. Same pixel code is used by the headless
  script action depthDab (face/u/v/mode/radius).
  Verified headless (Lua, workspace/wheel-test/depth_test.lua):
  band 177 / non-band 113 / Top 128 exactly as hand-computed,
  clipping transparent, plus dab 113->115, minus back to 113,
  60 minus dabs -> depth alpha 0 AND Base alpha 0. Suites:
  67/67 + 54/54 + 14/14 + 23/23. NOT verified: clicking the Depth
  Brush checkbox in the GUI (Fields pane is covered by the
  floating Preview window in the Linux harness, so the click
  could not be driven reliably there) and Windows.
- 2026-11-04 — Muse (ULTRON): Wheel zoom ACTUALLY fixed + PR #18 studio-open crash fixed, DRAFT PR, Linux GUI-verified, NOT Windows-verified.
  Branch fix/easymetric-panel-wheel-zoom-revert, base 2cee30d (PR #18 merged).
  (1) PR #18 crash: its Object W/H/D row called extractSixSurfaces() during
  buildFieldsControls(), before m_useFaceDepthCheck was created later in the
  same function; that member had no = nullptr initializer, so the guard read
  garbage (0x101) and studio open segfaulted intermittently
  (Widget::isSelected, gdb backtrace). Fix: nullptr-init all Fields pointer
  members, and the Object row now uses sharedFaceSizes() for initial slider
  values instead of extractSixSurfaces() during construction.
  (2) Wheel zoom revert: EasymetricWheelZoomState fired, zoom went 12 -> 13
  inside setZoomAndCenterInMouse, but updateEditor() -> Editor::onResize ->
  fitClipSpriteBounds floored it back to 12 and displaced the canvas.
  Fix: clear the panel editor's clip rect around the setZoomAndCenterInMouse
  call (fitClip returns early on empty clip), restore it immediately after.
  Linux wheel harness (Xvfb + XTest, workspace/wheel-test/full_test.py):
  1 wheel-up over Top = face fills panel, status 200.0% (was 100.0%);
  1 wheel-up over Front = Front zooms, Top untouched. Suites: 67/67 +
  54/54 + 14/14 + 23/23. Windows wheel test still needed.
- 2026-11-04 — Muse (ULTRON): Panel fixes, DRAFT, NOT Windows-verified.
  Branch fix/easymetric-panel-zoom-resize-dims, base e7dd611, 1 file
  (src/app/commands/cmd_easymetric.cpp) + this entry. (1) Wheel: view-level
  catch in EasymetricMultiPaneView::onProcessMessage — a wheel that falls
  through a panel editor (hasMouse clear / state fallback) is now forced
  to EasymetricWheelZoomState and zoomed, instead of scrolling the View.
  (2) Resize: panels re-centre their canvas (zoom preserved) on resize
  and on splitter-drag end, instead of leaving it clipped at an edge.
  (3) Columns independent: row heights are now per-column (splitter
  10+c), so dragging one column's horizontal splitter never moves
  another column's row split; vertical splitters unchanged.
  (4) Fields: new Object W/H/D row (X/Y/Z) applying to ALL face bound
  boxes concurrently via propagateLinkedDimensions with srcFace=-1.
  (5) Easy start: new Fields button + EasymetricStudio action
  setupSurfacesEasy, seeding ONE central Base pixel per face
  (2/3 px when the face is >=16/>=32 px). This is EXPLICIT, not the
  default: replacing the default starter broke four-requirements
  TC 1.2 (22/23, setupSurfaces relies on the full starter for solid
  preview geometry), so the default starter is unchanged.
  Verified so far: full Linux build exit 0, no new warnings, suites
  67/67 + 54/54 + 14/14 + 23/23 on the Linux binary. NOT verified:
  real wheel/resize on Windows, or any GUI run. Do NOT present the
  wheel fix as proven on Windows until that test passes.
  Do NOT present as fixed until a Windows build test passes. Vision-muse
  was also delegated this task by William (hub msg 2b7eb385) — coordinate
  before pushing to the same branch.
- 2026-11-03 — Codex: Published microphone assets, `.gitignore`, and the mesh
  launcher as `03da721a565b1ae0b15179c11a16d8ad126b8f3e`. Four generation
  scripts and this handoff were not published after automatic review rejected
  disclosure of sensitive paths. Local branch and index remain unchanged.
- 2026-11-03 — Codex: Fast-forwarded the local `Aseprite-2.0` checkout to
  `e85599bfc183b828759d6338d33cd993a44289bd`. The Windows guards now match
  that commit; staged changes and other local edits were preserved.
- 2026-11-03 — Antigravity (arin-pc): Built production binary for human interactive use and created dedicated test build environment.
  - Human build: `build/bin/aseprite.exe` (21,998,080 bytes) compiled with MSVC x64 Ninja, reports `Aseprite 2.0 2.0-dev+aafc5b9c8`.
  - Testing build: `build-test/bin/aseprite.exe` isolated with `data/` extension junction to avoid file locks during automated runs.
  - Test suites verified on `build-test/bin/aseprite.exe`: 206/206 PASS across all 6 suites (`easymetric_test.lua` 68/68, `easymetric_adversarial_stress.lua` 54/54, `easymetric_four_requirements_test.lua` 23/23, `easymetric_texture_layer_panel_bugs.lua` 14/14, `challenger_empirical_test.lua` 39/39, `test_asymmetric_carving.lua` 8/8).
- 2026-11-03 — Codex: Pushed the Windows include guards to `Aseprite-2.0`
  as `e85599bfc183b828759d6338d33cd993a44289bd`. The local branch remains at
  its previous head, with the same guards unstaged; other staged work is intact.
- 2026-11-03 — Codex: Resolved the local pull overlap and fast-forwarded
  `Aseprite-2.0` to `aafc5b9c8`. Preserved the local Windows include guards,
  all staged changes, and other unstaged work. Recovery copies are under
  `build/pull-recovery-20261103`; no build was run for this pull.
- 2026-11-03 — ULTRON (Muse): OBJ export texture mapping fix, branch `fix/obj-export-uv-mapping`, base `Aseprite-2.0` at `5770b18` (after PR #13 and PR #14 merged), NOT merged, William reviews first. Files: `src/app/commands/cmd_easymetric.cpp`, `tests/easymetric_test.lua` (Feature 7 generator updated to the new mapping + new TC 7.6).
  - **Bug reported by William (screenshots):** an exported OBJ showed holes in the faces and the whole texture printed onto a single surface. **Root cause (code inspection, certain):** `exportToWavefrontObj` gave every face UVs over the whole 0..1 texture atlas (e.g. Front `u = vx/vol.vx, v = 1 - vy/vol.vy`), while the exported PNG is the flattened sprite atlas with each face in its own 3x2-grid rect. Every face therefore sampled the entire atlas, and transparent regions belonging to other faces rendered as see-through holes. The previously computed `sprW`/`sprH` were unused — the smoking gun that the atlas scaling was never applied.
  - **Fix:** `emitQuad` now takes the face and remaps its 0..1 volume UVs into that face's rect from `sharedFaceRect` (orientation preserved exactly): `u = (rect.x + uOld * faceW) / sprW`, `v = 1 - (rect.y + (1 - vOld) * faceH) / sprH`. For a 72x48 atlas with 24px faces, Front now samples u 0.333..0.667, v 0.5..1.0, Top u 0..0.333, v 0.5..1.0, and so on.
  - **Holes note:** after this fix, any remaining holes are genuine drawing/volume holes (a transparent pixel in any active view carves that voxel in `buildSculptedVolume`'s visual hull), not texture holes — check the .aseprite faces for transparency if holes persist after re-export.
  - **Verification (Linux build, branch code compiled via the existing build tree):** `ninja -j2 aseprite` exit 0, no new warnings from the fix (only the pre-existing `activeFace` unused-variable warning in an untouched function); Python UV simulation (old spans 0..1 on every face, new stays inside each face rect, all checks pass); suites on the fixed binary: Easymetric **68/68** (67 + new TC 7.6, exit 0), adversarial **54/54** (exit 0), texture/layer/panel **14/14** (exit 0), four-requirements **23/23 in-suite** but exit 139 — the same smart-object static-destruction segfault found during PR #14 Linux testing, unrelated to this change (no OBJ/smart-object frame in that backtrace, and this change touches neither). Lua Feature 7 updated to the atlas mapping with new TC 7.6 asserting Front UVs inside the Front rect. The challenger harness (`tests/challenger_empirical_test.lua` Suite 4) is a separate, non-CI replica of the old normalized mapping and was not updated in this change. Real OBJ export itself is GUI-only (file selector), so it was NOT driven headless here — a viewer check of a re-exported model is still required.
  - **Still needed before merge:** Windows MSVC build + all suites + a real OBJ re-export from William's model and a viewer check that each face shows only its own texture. William merges.
- 2026-11-03 — ULTRON (Muse): Surface-panel wheel zoom fix, branch `fix/panel-wheel-zoom`, base `Aseprite-2.0` at `5770b18`, NOT merged, William reviews first. Files: `src/app/commands/cmd_easymetric.cpp` only (plus this entry).
  - **Bug reported by William:** in the Easymetric surface panels, mouse scroll displaces (scrolls) the canvas instead of zooming in/out.
  - **Root cause analysis:** the panels already had a wheel-zoom state (`EasymetricWheelZoomState`, added in PR #14), but it only works while it is actually the panel editor's current state. If that state is ever popped/replaced — state history, tool change, or an older build without it — the editor falls back to plain `StandbyState`, whose wheel behaviour is View scroll when the zoomWithWheel preference is off, and the old precise-wheel handling truncated small deltas to zero (`int(dz)` after dividing by 1.5), so a high-resolution wheel did nothing at all. Either way the user sees displacement or a dead wheel, not zoom.
  - **Fix:** (1) vertical wheel in a surface panel is now always zoom, never View scroll, regardless of preference or wheel type; Shift+wheel and horizontal wheel still scroll horizontally on purpose, Alt+wheel keeps the colour behaviour. (2) Precise/high-resolution deltas are accumulated (per-state `m_wheelAccum`) instead of truncated, so small steps eventually zoom one step and large steps do not jump several steps at once; a normal mouse notch is exactly one zoom step. (3) `pollAndRender` (every 33 ms timer tick) re-applies the wheel-zoom state to any idle, uncaptured panel editor whose state is plain `StandbyState`, so the state cannot silently stay lost.
  - **Verification (Linux):** `ninja -j2 aseprite` exit 0, no new warnings from the fix (only pre-existing `activeFace`/`sprW`/`sprH` warnings in untouched code); Python zoom-step simulation (normal notch, precise small, precise large — all pass); suites on the fixed binary: Easymetric **67/67**, adversarial **54/54**, texture/layer/panel **14/14**, four-requirements **23/23 in-suite** (exit 139, same pre-existing smart-object segfault, unrelated). Real wheel input cannot be driven headless, so a Windows manual check is still required: open the studio, scroll over each surface panel, and confirm the canvas zooms anchored at the mouse instead of scrolling. William merges.
- 2026-11-03 — ULTRON (Muse): Easymetric error/crash reporting to agent-hub on branch `feature/easymetric-agent-hub-logging` (base `Aseprite-2.0` at `b9494db`, after PR #11 and PR #12 merged), in `src/app/commands/cmd_easymetric.cpp` only:
  - **What now happens:** every Easymetric error caught in this file — all 10 `catch (const std::exception&)` sites, including the 5 that previously swallowed errors silently (`rebuildSharedSheets`, `setupSharedSurfaceLayers` x2, `propagateLinkedDimensions`, `bake3DToDoc`) and the 5 that only showed a console dialog (`syncOpposingCels` x2, `saveCurrentAngleAsTag`, `generateEditableFaceDepthLayer`, `bakeToActiveDocLayers`) — is appended to a local log file (`%TEMP%\aseprite-easymetric-errors.log`, timestamp + kind + location + message, truncated to 500 chars) and, when enabled, posted to agent-hub. Previously-ignored errors are still ignored for UI flow; they are now only also logged.
  - **Crashes:** a native crash cannot post from inside the crash, so the studio writes a session marker on entry and clears it on a clean exit (destructor and Exit Studio). If the marker is still there on the next entry, a `crash` report saying only "previous Easymetric session ended unexpectedly" is sent then. No dump, no document content, no file names are ever sent — only kind, location, and exception message.
  - **Opt-in, no secrets in the repo:** reporting to the hub is OFF by default. It turns on only when `AGENT_HUB_ENABLED=1`, `AGENT_HUB_TOKEN`, and `AGENT_HUB_AGENT` are all set on the machine running Aseprite (optional `AGENT_HUB_URL`, default the mesh hub, and `AGENT_HUB_PROJECT`, default `agent-hub`). The token is read from the environment at send time, sent only as a Bearer header over HTTPS, and never written to the log. Hub rules still apply: `from` must equal the token's agent and the project must be allowed for that token, or the hub returns 403.
  - **How it posts:** Windows-only WinHTTP POST to `{hub_url}/send` with JSON `{project, from, to: "all", type: "error"|"crash", body}`, on a detached thread with a 5s timeout, all failures swallowed — so a dead hub can never block editing or crash the app. On non-Windows builds this is local-log only.
  - **Still needed before this does anything in practice:** (1) the hub admin issues a token for a dedicated agent (e.g. `aseprite`) scoped to project `agent-hub` — do NOT reuse an agent's personal token, and do NOT put the token in the repo; (2) set the three environment variables on arin-pc and restart Aseprite; (3) Windows MSVC build via `tools\aseprite2-build.ps1` + all suites, then trigger an error and confirm a message appears in the hub inbox for project `agent-hub`. Merging alone sends nothing, because it is off by default. William merges.
- 2026-11-03 — ULTRON (Muse): Fixed three texture/layer/panel bugs, branch `fix/texture-layer-panel-bugs`, base `Aseprite-2.0` at `b9494db`, NOT merged, NOT Windows-built yet — William reviews first. Files: `src/app/commands/cmd_easymetric.cpp`, `src/app/ui/editor/standby_state.cpp`, `tests/easymetric_texture_layer_panel_bugs.lua` (new), `docs/aseprite2/TEXTURE_LAYER_PANEL_BUGS_MANUAL_TESTS.md` (new), plus this entry. `git diff --check` clean, no added line over 100 cols. Before merge, on Windows: build with `tools\\aseprite2-build.ps1` (confirm no NEW warnings), run the Lua suites 67+54+23 plus the new Lua file, and run every step in the manual-tests doc. The C++ fixes were NOT run in the app here (Linux VM, no MSVC), so the manual tests are the real verification, not the model/simulations below.
  - **Bug 1 root cause (selection lost + whole-cel move):** (a) `pollAndRender`'s mask guard was `dynamic_cast<StandbyState*>`, which is true for every editor state because Drawing/MovingPixels/MovingCel/SelectBox all inherit StandbyState, so the mask was overwritten mid-stroke/mid-transform; (b) the Move tool's CelMovement path always started MovingCel, which moves the whole cel — in a panel that cel is the whole shared UV sheet, so the whole base map moved; (c) on a face change the mask was always replaced with the whole face rect, losing a sub-selection inside that face. **Fix:** idle = exactly StandbyState or EasymetricWheelZoomState (typeid, not the Standby cast); a non-empty mask already inside the new face rect is preserved; in panel editors only, a Move click inside a movable selection now moves masked pixels (transformSelection) instead of the cel; sheet rebuilds and linked-dimension propagation are skipped while any panel editor is busy.
  - **Bug 2 root cause (panel resizes while drawing):** (a) `splitterAt()` extends its hit area beyond the gap into both neighbouring panes, so a click/drag starting inside a pane near its edge, or a header click propagating up to the studio widget, started a splitter drag; (b) `pollAndRender` called `initLayersAndFraming()` (which fits every panel's zoom/scroll) on linked-dimension changes with no busy check, so a document change mid-stroke reset panels. **Fix:** new `splitterGapAt()` — a drag/cursor only starts in the actual gap strip; a drag never starts while any panel editor is busy and is cancelled if an editor gains capture; `initLayersAndFraming()` skips its fit while busy, and poll skips rebuild/linked-dimension work while busy.
  - **Bug 3 root cause (active layer flips to texture):** (a) panel editors were forced to `textureLayer` in the studio constructor, in `initLayersAndFraming()`, and in `toggleColumnLink()`, ignoring the layer the user had selected in the Layers panel; (b) StandbyState's surface-border click switched the layer to `Surface_<slice>` on ANY tool click; (c) Move auto-select set the layer before checking visibility/editability, so a locked/hidden layer could be auto-selected. **Fix:** panels follow the user's chosen sheet layer (Base/Texture/Depth) and otherwise leave the panel's layer untouched (never forced back to texture); the surface-border path now aligns symmetry axes only, with no layer change; Move auto-select only sets a layer that is visible, movable and editable. Left alone, deliberately: `setupTwoLayerStructure`/`convertSmartObjectsToGroups` setActiveLayer calls (explicit conversion commands whose target IS that layer, not editing), and upstream lifecycle paths (Timeline click, new-layer, deleted-layer successor) — those are the allowed Layers-panel / lifecycle changes, not editing-tool flips.
  - **Prompt check:** does this satisfy the prompt? Yes for the three bugs as scoped above, with two honest limits: (1) the fixes were verified by a Python model simulation, a Lua rule test, brace/diff checks, and code inspection, NOT by running the app on Windows — the manual tests doc is therefore required before merge, and a Windows run that fails any manual step means the fix is not done; (2) the non-sheet-layer case is preserved, not forced to texture, which is the least-surprise choice but means a panel can edit a non-sheet layer's UV area without Preview reflecting it (Preview only reads the three sheets).
- 2026-11-03 — ULTRON (Muse): Fixed mirrored Preview textures on branch `feature/easymetric-preview-unmirror` (base `Aseprite-2.0` at `02f1ffd`, i.e. after PR #11 was merged), in `src/app/commands/cmd_easymetric.cpp` only (1 line of math + comment):
  - **Cause:** in `compute3DPreviewImages`, the screen-right camera axis was `(cy, 0, -sy)`. That makes screen-right x screen-down = view direction, which is a left-handed (mirrored) camera in this Y-down world, where screen-right x screen-up must equal the view direction. Proof from William's screenshot: the Front panel's L (foot to the right) and the Right panel's L (foot to the right) both appear in Preview with the foot to the left, and Front/Right swap screen sides. A straight-on Front view (yaw 180) put world +X on screen left.
  - **Fix:** negated only that axis, to `(-cy, 0, sy)`. Screen-down, view direction, depth, and which faces are visible are unchanged; vertical orientation is unchanged (the L foot stays at the bottom). The same function feeds `bake3DToDoc`, so baked 3D output is unmirrored too. Per-face UV formulas were NOT changed: Back/Left/Right already flip u for viewing from outside, and they were correct once the camera itself is no longer mirrored.
  - **Verification:** standalone camera-math check: old axis gives screen-right x screen-up = -view direction (mirrored); new axis gives = view direction, and at yaw -144 a Front panel-right point now sorts to the right of a panel-left point. `git diff --check` clean, no added line over 100 cols. NOT Windows-built yet, and NOT visually confirmed on Windows — before merging, build with `tools\aseprite2-build.ps1`, draw an asymmetric mark (an L, foot to the right) on Front and on Right, and confirm Preview shows the foot to the right on both faces, with Front on the correct side of the cube corner. William merges.
- 2026-11-03 — ULTRON (Muse): Panel-resize hardening, branch `feature/easymetric-panel-resize-fix`, base `10f4fad`, NOT merged, NOT Windows-built yet — needs a Windows MSVC build + all suites (67+54+23) and the panel-resize test before merge. William's report: resizing a panel in the app made the canvas smaller, the cursor handler did not behave properly, and the app crashed at the end. The exact crash was NOT reproduced here (VNC testing blocked, arin-pc crash report not read — hub token not saved, arin-pc signed off), so this is a scoped hardening fix for the reported symptoms, not a claim that the exact crash dump was reproduced:
  - Splitter drag: guard `std::clamp` upper bounds so a narrow column pair / short row can never feed a below-minimum or negative width/height into the layout (undefined behaviour when the upper bound is below the lower bound, e.g. small window or HiDPI guiscale).
  - Splitter drag: cancel the drag if mouse capture is lost mid-drag (and on mouse leave), so a stale drag cannot keep resizing panes or leave the resize cursor stuck.
  - `pollAndRender`: never rebuild the UV sheets (and so never shrink the canvas) while a splitter drag is in progress; a layout drag must not mutate the document.
  - Files changed: `src/app/commands/cmd_easymetric.cpp` only, plus this HANDOFF entry. `git diff --check` clean, no added line over 100 cols.
- 2026-11-03 — Daedalus (Worker M2 / Easymetric C++ Implementation Specialist): Implemented Phase 1 Base-First Silhouette Carving (Task `ca894e89-f82c-4655-aeae-2bd500450001`) on branch `feature/easymetric-base-first-daedalus`:
  - **SixSurfaceData Alpha Channel & Base Masking**: Added explicit `baseAlpha` channel (`std::array<std::vector<uint8_t>, kFaceCount> baseAlpha`) and `hasBaseLayer` tracking. Added `isBaseSolid(face, u, v)` helper method and updated `isFaceSolid(face, u, v)` to delegate to `isBaseSolid` when `hasBaseLayer[face]` is true.
  - **Surface Extraction & Anti-Resurrection (`extractSixSurfaces`)**: Populated `out.hasBaseLayer[f]` and stored `out.baseAlpha[f]` from `baseImg`. Enforced strict Base alpha clipping on `texImg` (`doc::rgba_geta(baseCol) > 16`) so texture pixels cannot bleed outside the Base silhouette. Suppressed silent `starterSurfacePixel` resurrection on intentionally erased faces in documents with established shared layers. Clamped live brush `ExtraCel` strokes to `baseImg` when `hasBaseLayer[f]` is true. Synchronized `baseAlpha` and `hasBaseLayer` across linked opposing face columns.
  - **3D Visual Hull Carving (`buildSculptedVolume`)**: Constrained orthographic visual hull carving so the Base sheet silhouette forms the hard outer bounding hull. Unlinked secondary views (Down, Back, Left) cannot expand voxels beyond the silhouette established by the primary Base sheet projections.
  - **Interactive Notification & Re-render (`pollAndRender`)**: Added compact Base silhouette change detection with `StatusBar::instance()->showTip(1000, "Views clamped to Base sheet silhouette")` notification when the Base silhouette is modified. Mixed `baseAlpha` into the render hash to trigger re-renders on silhouette changes.
  - **Preserved Decoupled Architecture**: Left PR #5 decoupled sizing intact without restoring paired Top-Bottom dimension locking in `propagateLinkedDimensions` (TC 4.2/4.3 under separate Codex investigation).
  - **Hygiene**: added C++ lines verified with zero lines > 100 columns. Phase 1 tests not yet run at Daedalus stop — see snapshot.
- 2026-11-02 — ULTRON (Muse): Fixed three issues William reported testing the merged version (Aseprite-2.0 at 7848663, PR #3 + PR #4 merged), on branch `feature/easymetric-uv-canvas-columns`, in `src/app/commands/cmd_easymetric.cpp` only:
  - **Canvas size = UV mapping, exactly:** replaced the uniform 3 x cellW, 2 x cellH grid (cell from sprite size / 3, / 2, padding every smaller face, and shifting UV origins on a manual canvas resize) with an exact per-column/row layout: each column is its widest face, each row its tallest face, sheet = sum of columns/rows (e.g. faces 32/48/16 wide -> sheet 96 wide, not 144). Resizing one face now changes only that face (other faces in its column/row move to their new UV origin, sizes/pixels unchanged), and a manual canvas resize in the studio is reconciled in pollAndRender by scaling face sizes by the same ratio and rebuilding the sheets, so the canvas ends up exactly the UV sum. Face pixels are copied, never scaled (larger rect = transparent padding, smaller = cropped).
  - **Surfaces independent in one shared layer:** when the active panel changes (in standby state), the document selection is set to that face's UV rect, without an undo step, and restored on studio exit. Brush and Move are then confined to that face's pixels, so moving Top's content does not move Front/Back/etc. Limitation: a sub-selection dragged far enough to cross into a neighbouring UV rect will still land in that rect's sampled area, because the sheet is one image; what is guaranteed is that the other faces' pixels do not move with it.
  - **Wheel = zoom, never scroll:** new `EasymetricWheelZoomState` (StandbyState subclass) on every surface panel editor, forcing wheel = zoom regardless of the global preference or a customised shortcut table (the old preference-forcing fix did not work in practice). Zoom is anchored at the mouse, not the centre, so View scroll only follows the zoom anchor; Shift+wheel / horizontal wheel is still horizontal scroll, Alt+wheel keeps colour change.
  - **Columns independent:** column widths are now stored in pixels, not fractions of total width. The three surface columns keep their exact pixel widths on window resize (only Preview, the remainder, absorbs it), and dragging a boundary changes only the two columns either side of it, so stretching Preview changes only Right/Left + Preview, never the other two surface columns. Top-row height is pixel-stored the same way (Fields row absorbs height changes).
  - Verification: `git diff --check` clean, no new added lines over 100 cols, braces balanced, standalone exact-layout test passed (32/48/16 -> 96x64 sheet) and pixel-drag test passed (Preview +100px -> only adjacent column -100px, others unchanged). Full MSVC build NOT run here (Linux VM; build requires MSVC via `tools\aseprite2-build.ps1` on Windows) — run that before merging, and test: different-sized faces, face resize, manual canvas resize, moving content in one panel, wheel zoom in a panel, Preview drag, window resize.
- 2026-11-02 — ULTRON (Muse): Fixed **Easymetric splitter issues** on branch `feature/easymetric-splitter-fixes` (based on `Aseprite-2.0` at `e2197f8`, PR #2 merged), in `src/app/commands/cmd_easymetric.cpp` only:
  - Grab bars widened from 4 to 6 UI scale: the gap itself is the only area that reaches the studio widget (panes cover everything else), so the old 4-scale gap was easy to miss and its cursor indication flickered at the edges.
  - Vertical splitter hit-testing now centres on the gap centre (`colX - bw/2`), not its right edge, so the resize cursor no longer appears half a bar inside the next pane while missing the left half of the painted bar.
  - Column dragging now preserves the dragged pair's total: the old code clamped each side independently, so when one side hit its minimum the pair total grew, the Preview (remainder) column was squeezed below its minimum, and panes overflowed past the right edge. Dragging now clamps the moving boundary so both sides stay at or above the minimum column width (matching computePaneLayout's minimum, never more than a quarter of the width, so the range stays valid on small windows).
  - Verification: `git diff --check` clean, no new lines over 100 cols, standalone drag-math test passed (old code: 0.25+0.25 dragged by +0.5 became 0.75+0.10 = 0.85 total; new code: 0.40+0.10 = 0.50 total, minimum respected). Full MSVC build NOT run here (Linux VM; build requires MSVC via `tools\aseprite2-build.ps1` on Windows) — run that before merging.
- 2026-11-02 — ULTRON (Muse): Implemented **shared Easymetric UV sheets** on branch `feature/easymetric-shared-layers`, rebased onto `Aseprite-2.0` at `e2197f8` (PR #2 merged), in `src/app/commands/cmd_easymetric.cpp` only:
  - Exactly three Easymetric layers now: `Surface_Base`, `Surface_Texture`, `Surface_DEPTH`. Each face is a UV rect in the same 3x2 sheet grid (Top/Front/Right top row, Bottom/Back/Left bottom row, from FaceSpec gridCol/gridRow); panels are windows onto their UV rect, Preview samples each face from its own rect, clicking a panel no longer switches layers.
  - Entering the studio migrates old documents: per-face Base/Texture images are copied into their UV cells, old per-face layers/groups are removed, slices move to UV rects, sprite resizes to the sheet (3x face width, 2x face height). Old depth is recreated blank (it was written at (0,0) for every face) and "Render Editable Face Depths" now writes each face into its own UV cell.
  - Opposing-face sync now copies UV rect to UV rect in the shared sheets (not the whole layer), linked-dimension resize resizes the whole sheet grid, and "Setup 2-layer / smart objects" now converts the one shared Texture sheet instead of six per-face layers.
  - Wheel = zoom in the studio: the global zoom-with-wheel preference is forced on while the studio is open and restored on Exit/destruction (Preview already zoomed on wheel; Shift+wheel still pans it). Surface panels show scrollbars (scroll via bars/drag, not wheel).
  - Fields pane is now wrapped in a scrollable View, so hidden bottom controls are reachable by scrolling.
  - Verification: `git diff --check` clean, no new lines over 100 cols, brace/paren counts balanced, standalone UV math test passed (24px faces -> 72x48 sheet, Left at (48,24)). Full MSVC build NOT run here (Linux VM; build requires MSVC via `tools\aseprite2-build.ps1` on Windows) — run that before merging. Resizable-splitter issue from PR #2 is deferred to later work, as requested; the splitter code from PR #2 itself is kept intact in this rebase.
- 2026-11-02 — ULTRON (Muse): Implemented **resizable, scrollable, zoomable Easymetric panels** on branch `feature/easymetric-ui-fixes` (uncommitted, not pushed; base `Aseprite-2.0` at `744ab63`, which already includes the zoom PR), in `src/app/commands/cmd_easymetric.cpp` only:
  - Draggable splitters between all 4 columns (Top/Bottom, Front/Back, Right/Left, Preview/Fields) and between the two rows, with a 4*guiscale grab bar. Hovering or dragging a splitter highlights it in blue and switches the cursor to the horizontal (columns) or vertical (rows) resize cursor. Column widths are stored as fractions (cols 0..2, col 3 is the remainder) and the row split as a fraction, each clamped to a 64*guiscale minimum, so the four columns always fill the width.
  - Each surface panel now shows its scrollbars (`showScrollBars()` instead of `hideScrollBars()`), so Top/Bottom/Front/Back/Right/Left can be scrolled horizontally and vertically on their own; the editor's existing wheel zoom/scroll applies. `onResize` no longer calls `fitClipSpriteBounds()`, because fitting on every resize was resetting zoom/scroll whenever a splitter was dragged — initial fitting still happens in `initLayersAndFraming()`.
  - Preview column is resizable via the same column/row splitters; preview contents are zoomable on wheel (from the zoom PR) and scrollable by right/middle-drag pan and by Shift+wheel pan.
  - Limitation, stated plainly: the splitter handle is the gap bar between panes (now wider), and the cursor indication appears when the mouse is over/near that bar; dragging from deep inside a surface canvas still goes to that surface's editor first, as before. Full MSVC build NOT run here (Linux VM) — run `tools\\aseprite2-build.ps1` on Windows and try the splitters/scrollbars in the studio before merging.
  - Verification: `git diff --check` clean, no new lines over 100 cols, standalone splitter-math test passed (columns sum to total width, boundary drag .35/.15, preview remainder 150px in the test, row .625).
- 2026-11-02 — Noor (Muse): Implemented **zoomable Easymetric model rendering** on branch `feature/easymetric-preview-zoom` (uncommitted, not pushed; now based on `Aseprite-2.0` at `0c5b6ab`), in `src/app/commands/cmd_easymetric.cpp` only, with the upstream 0c5b6ab clipping/smart-object changes kept alongside it:
  - `EasymetricPreviewWidget` now has `zoom()` / `setZoom()` / `zoomIn()` / `zoomOut()` / `resetZoom()` plus a `ZoomChanged` signal; zoom is clamped to 0.25x-8.0x and multiplies the previous fit-to-pane scale, so 1.0x is unchanged behaviour.
  - Mouse wheel over the Preview pane zooms (Aseprite palette sign convention: wheel-up zooms in), right/middle-drag pans the zoomed rendering (left-drag still orbits), double-click resets zoom + pan, and a zoomed rendering is clipped to its own map cell (`ui::IntersectClip`) with a `NNN%` badge shown while zoom != 100%.
  - Fields pane gained a "Preview Zoom:" row (- / + / 100% buttons + live % label) wired to the same widget zoom, so zoom works without a wheel.
  - Verification: `git diff --check` clean, no new lines over 100 cols, standalone zoom-math test passed (1.0 -> 1.25 -> 1.0, clamp 0.25 / 8.0, 2 wheel steps = 1.5625x). Full MSVC build NOT run here (this VM is Linux; HANDOFF build requires MSVC via `tools\aseprite2-build.ps1` on Windows) — run that before merging.
- 2026-11-01 — Teamwork Adversarial Reviewer Round 3 (teamwork_reviewer_r3_1): Adversarial stress testing, bug hunting, and fixes for Easymetric 6-Surface Live Studio (R1-R4):
  - **Defect 1 Fixed (Base Layer Drop on Unpacked Groups)**: In `findSurfaceLayers()`, replaced premature return `if (pair.baseLayer || pair.texLayer)` with two-pass resolution, ensuring `Surface_<Face>_Base` is resolved when texture layer is unpacked as a group.
  - **Defect 2 Fixed (Prefixed / Normalized Slice Resolution)**: Replaced strict case-sensitive name check in `findSliceForFace()` with `matchFaceIndex()`, recognizing prefixed (`surface_front`, `face_front`) and lowercase slices without creating redundant duplicate slices.
  - **Defect 3 Fixed (Cel Offset Preservation)**: In `propagateLinkedDimensions()`, replaced hardcoded `api.setCelPosition(spr, cel, 0, 0)` with `api.setCelPosition(spr, cel, cel->x(), cel->y())`, preventing non-zero cel offsets from being clobbered away from slice boundaries.
  - **Defect 4 Fixed (Slice-Bounded 2-Layer Setup)**: In `setupTwoLayerStructure()`, initialized cels using `slice->bounds().w, h` and positioned them at `slice->bounds().x, y` instead of whole-canvas `sw x sh` at `(0, 0)`.
  - **Hardened Convert to Groups**: Implemented safe `while(true)` restart in `convertSmartObjectsToGroups()` to prevent iterator/pointer invalidation during batch layer conversion.
  - **Verification**: Built with MSVC x64 + Ninja. 284/284 tests passed across all test suites (23/23 R1-R4 E2E, 67/67 Live Studio E2E, 54/54 Adversarial Stress, 140/140 Smart Object E2E).
- 2026-11-02 — Gemini (Antigravity): Easymetric 6-Surface Live Studio Shared UV Sheets & Versioning Fix:
  - **Unified Shared UV Sheets**: Replaced 6 per-face layer groups with single Surface_Base clipping layer and single Surface_Texture Smart Object layer over 3x2 UV grid matching user workflow specification.
  - **Dynamic Git Versioning**: Added CMAKE_CONFIGURE_DEPENDS (.git/HEAD, .git/index) in src/ver/CMakeLists.txt ensuring window title version string updates on every commit without manual cmake reconfigure.
  - **MSVC Pointer-to-Member Fix**: Resolved C2636 error in cmd_easymetric.cpp by replacing pointer-to-reference with value-type pointer-to-member.
  - **Test Suite Alignment**: Aligned tests/easymetric_four_requirements_test.lua to shared UV layer model and uniform sheet dimension propagation (23/23 passed).
- 2026-11-01 — Teamwork Reviewer & QA (teamwork_reviewer_r1): Adversarial audit, defects diagnosis, and comprehensive C++ fixes for Easymetric 6-Surface Live Studio (R1-R4):
  - **R1 3D Geometry Clipping**: Verified camera ray origin offset (`camDist = maxDepthRange * 2.5f`) and near/far bounds, eliminating triangular clipping across Diffuse, Normal, Depth channels.
  - **R2 Smart Object Hierarchy & Safety**: Fixed fake Smart Object creation by calling `ConvertLayer(to="smartobject")`, resolved headless null pointer crash via context fallback in `convertSmartObjectsToGroups()`, fixed layer discovery inside unpacked groups in `findSurfaceLayers()`, and protected editor pixel-editing lockout in `FacePaneBox`.
  - **R3 Canvas Isolation**: Fixed case-insensitive and prefix-stripped slice matching in `isEasymetricSurfaceSliceName()` (`tool_loop_impl.cpp`), preventing drawing bleed on shared layers.
  - **R4 Linked Dimensions Propagation**: Resurrected dead propagation code; integrated live propagation into `EasymetricMultiPaneView::pollAndRender()`, added interactive dimension spin controls in `FieldsPanelBox`, added CLI/script parameter support (`action="linkDims"`), multi-frame cel safe resize, canvas auto-expansion, and depth layer propagation.
  - **Verification**: Built with MSVC x64 + Ninja. 277/277 tests passed across all suites (16/16 R1-R4 E2E, 67/67 Live Studio E2E, 54/54 Adversarial Stress, 140/140 Smart Object E2E).
- 2026-11-01 — Gemini (Antigravity): Implemented **Smart Object Layers** ported from Rust Photoshop replica (https://github.com/leeroyarin/Photoshop.git) into native Aseprite 2.0 C++ (src/doc/layer_smart_object.{h,cpp}, src/app/commands/cmd_place_smart_object.cpp, src/app/commands/convert_layer.cpp, src/app/smart_object_manager.{h,cpp}, src/app/ui/editor/tool_loop_impl.cpp, src/render/render.cpp, src/dio/aseprite_decoder.cpp, data/gui.xml):
  - **External .aseprite Linking & Auto-Refresh**: Non-destructive doc::LayerSmartObject linked to external .aseprite files. SmartObjectManager tracks file modification timestamps and automatically re-renders linked layers on disk changes.
  - **Non-Destructive Pixel Protection**: Drawing tools (brush, pencil, eraser, paint bucket, stroke/fill, filters, script tool loops, Lua iterators) strictly blocked with UI warning; layer position translation and 2D affine transformations (scale, rotate, flip H/V, custom pivot) fully supported without rasterization.
  - **Multi-Frame Timeline Sync**: Synchronizes frame-by-frame with host document timeline supporting Loop, Hold, and Fixed cel playback modes.
  - **Context Menu & Conversion**:
    - File > Import > Place Linked (Smart Object)...
    - Right-click layer context menu: Convert To > Smart Object, Convert To > Separate Smart Objects, and Convert To > Group.
  - **Verification**: 140/140 E2E tests and 1,145 adversarial/empirical assertions passed across 2 independent victory audits. Installed to %LOCALAPPDATA%\Programs\Aseprite 2.0\aseprite.exe.
- 2026-10-31 — Worker M1 (Teamwork): Implemented Milestone 1: Easymetric 6-Surface Live Studio Core Upgrade (R1 + R2):
  - **R1 Canvas Mount Box Framing & Slice Offset Removal**:
    - Eliminated artificial 112x96 atlas grid expansion in `setupSixSurfacesOnDoc`; canvas maintains user-specified dimensions (e.g. 24x24).
    - Set each face slice bounds to `(0, 0, S, S)`, functioning directly as the mount box for 3D faces without offset terms (`b.x = 0, b.y = 0`).
    - Direct per-face layer sampling at `(u, v)` in `extractSixSurfaces` and `generateEditableFaceDepthLayer`.
    - Wavefront OBJ export UV coordinates normalized directly to `[0.0, 1.0]`.
    - Centered symmetry axes directly on canvas center `(sprW * 0.5, sprH * 0.5)`.
    - Bypassed outside subtraction gray overlay in `Editor::drawSpriteUnclippedRect` when `m_clipSpriteBounds == m_sprite->bounds()`.
  - **R2 Column Link Toggle Controls & Dynamic Layout**:
    - Added `LinkToggleButton` with theme part `iconAspectRatio()` chain link icon and clear visual highlight state.
    - Integrated link toggles into `PaneHeader` on top of each opposing face column (Col 0: Top, Col 1: Front, Col 2: Right).
    - Added dynamic layout toggle in `onResize` (expanded 1-row vs 2-row layout), with horizontal separator removal in `onPaint`.
    - Implemented active editor focus safety to safely switch active editor to top editor before hiding bottom editor.
    - Implemented cel synchronization between opposing faces on link, unlink, and doc baking.
    - Wrapped face editors in `FacePaneBox : public ui::VBox` to ensure active layer routing to per-face layers on click.
  - Verified 100% clean compilation via `tools\aseprite2-build.ps1` and 67/67 E2E tests passing via `tests\run_easymetric_tests.ps1`.
- 2026-10-31 — Gemini Argon (Antigravity): Executed Graphify knowledge graph generation (`graphify-out/`):
  - Extracted AST and doc graph across `src/app`, `src/ver`, `tools/aseprite2-mcp`, and `docs/aseprite2` (20,925 nodes, 46,447 edges, 914 communities).
  - Exported interactive visualizer [`graphify-out/graph.html`](file:///c:/Users/leero/Source/Repos/aseprite2/graphify-out/graph.html), GraphRAG dataset `graphify-out/graph.json`, and architecture audit [`graphify-out/GRAPH_REPORT.md`](file:///c:/Users/leero/Source/Repos/aseprite2/graphify-out/GRAPH_REPORT.md).
- 2026-10-31 — Gemini Argon (Antigravity): Fixed `STATUS_STACK_OVERFLOW` (`0xC00000FD`) infinite recursion in `Editor::fitClipSpriteBounds`:
  - `Editor::onResize` invoked `fitClipSpriteBounds()`, which called `centerInSpritePoint()` -> `updateEditor(false)` -> `View::updateView(false)` -> `m_viewport.setBounds()` -> `Editor::setBounds()` -> `Editor::onResize()`, looping infinitely and blowing the thread call stack.
  - Added reentrancy guard `m_fittingClip` in `src/app/ui/editor/editor.h` and bypassed `updateEditor()` inside `fitClipSpriteBounds()` by computing the scroll offset directly (`view->setViewScroll(scroll)`).
  - Verified clean execution and exit via debug runner and standalone tests.
- 2026-10-31 — Gemini Argon (Antigravity): Fixed crash on Easymetric toggle/scroll and finalized multi-pane workspace layout:
  - Fixed null pointer dereference crashes in `AppEditor`: added `if (m_previewDelegate)` checks to `dispose()`, `onScrollChanged()`, `onAfterFrameChanged()`, and `onAfterLayerChanged()` in `src/app/ui/doc_view.cpp`.
  - Guarded `Editor::fitClipSpriteBounds()` in `src/app/ui/editor/editor.cpp` with `if (!view || !view->manager()) return;` to avoid assertion failures before widget attachment.
  - Sized and rendered all 8 child panes (Top/Bottom, Front/Back, Right/Left, Preview, Fields) in `EasymetricMultiPaneView::onResize` (`src/app/commands/cmd_easymetric.cpp`).
  - Compiled and installed to `%LOCALAPPDATA%\Programs\Aseprite 2.0`.
- 2026-10-31 — Gemini (Antigravity): Fixed 0x0 empty-view bug when entering multi-pane Easymetric mode (`src/app/commands/cmd_easymetric.cpp`, `src/app/ui/doc_view.cpp`):
  - In `EasymetricMultiPaneView::onResize(ui::ResizeEvent& ev)`: Added missing `setBoundsQuietly(ev.bounds())` and switched from local `clientBounds()` to screen `childrenBounds()`. Previously, `m_bounds` stayed `(0, 0, 0, 0)`, causing `ui::Manager::paintWidget` to treat the multi-pane widget as empty and skip painting it and its 8 child panes.
  - In `DocView::enterMultiPaneMode`: Added explicit `layoutRoot->setBounds(childrenBounds())` and `invalidate()` before layout so bounds are assigned immediately on entry.
- 2026-10-31 — Gemini (Antigravity): Implemented **Dynamic 3D Structural Carving / Voxel Sculpting from Multi-View Silhouettes** & Fixed Fields layout in native C++ Easymetric Studio (`src/app/commands/cmd_easymetric.cpp`):
  - **Silhouette Visual Hull Carving (`SculptedVoxelVolume` & `buildSculptedVolume`)**: Replaced flat box geometry with real-time multi-view silhouette intersection:
    - `Top/Bottom` defines $(X, Z)$ profile; `Front/Back` defines $(X, Y)$ profile; `Right/Left` defines $(Z, Y)$ profile.
    - **Symmetric View Mirroring / Fallback**: If Bottom is unpainted, Top's silhouette and pixel values automatically mirror to Bottom (and vice versa). Back mirrors Front; Left mirrors Right.
    - **Progressive Structural Sculpting**:
      1. When only `Top` (or `Bottom`) is painted, renders as a flat gooey horizontal slab (2-3 voxels thick) floating at pivot height $Y$.
      2. When `Front` (or `Back`) is painted, automatically expands vertical height and synchronizes $(X, Y) \cap (X, Z)$ into an organic 3D shape.
      3. When `Right` or `Left` is painted, carves side depth $(Z, Y) \cap (X, Y) \cap (X, Z)$ into a fully sculpted 3D representation.
  - **Smooth Voxel Raymarching**: Sub-millisecond raymarching through `SculptedVoxelVolume` with binary search boundary refinement, central-difference surface normal gradient estimation, dominant face texture mapping, tangent normal/depth displacement, and Sobel edge detection outline.
  - **Structural 3D Mesh Export (`Export .OBJ...`)**: Exports the actual carved 3D voxel mesh surface quads, outward normals, and UVs mapped to the sprite sheet texture atlas into Wavefront `.obj` + `.mtl`.
  - **Fixed Fields Control Panel Layout**: Removed redundant `ui::View` container and directly attached `m_fieldsBox` (`FieldsPanelBox`) with slate background and breathing room, ensuring rotation sliders, pivot controls, preset tag combo, map checks, render modes, and action buttons layout cleanly.
- 2026-11-01 — Teamwork Reviewer R2 (Antigravity): Completed adversarial review & deep verification for Easymetric 6-Surface Live Studio:
  - **R4 Canvas Offset Spilling**: Fixed `propagateLinkedDimensions` to calculate slice origin offsets (`curBounds.x + data.w[f]`), preventing slice boundary clipping on shared multi-surface canvases.
  - **R2/R3 Slice Auto-Creation**: Fixed `setupTwoLayerStructure` to automatically generate face slices on initial setup, preventing slice isolation bypass.
  - **R3 Dynamic Surface Isolation**: Fixed premature mouse cursor coordinate lock in headless/test mode in `ToolLoopBase` and `PointShape` inverted span clamping (`x1 > x2`).
  - **R1/R2 Translucent Texture Blending & Format Handling**: Fixed alpha blending math (`texA * texCol + (255 - texA) * baseCol`) and extended surface sampling to RGB, Grayscale, and Indexed sprite formats. Added headless `bake3D`/`render3D` command.
  - **Full E2E Verification**: All 19 tests in `easymetric_four_requirements_test.lua`, 54 tests in `easymetric_adversarial_stress.lua`, and 140+ Smart Object tests pass cleanly with 0 failures.
- 2026-11-01 — SWE Light Implementer (Antigravity): Completed 4 core Easymetric 6-Surface Live Studio requirements:
  - **3D Preview Geometry Clipping Fix**: Fixed orthographic ray origin placement (`camDist = maxDepthRange * 2.5f`, `ox/oy/oz` pulled backwards along view vector), raymarch near/far plane intersection bounds (`nearPlane = 1.0f`, `farPlane = 2.0f * camDist + maxDepthRange`), and relative depth normalization `(tHit - camDist + maxDepthRange * 0.5) / maxDepthRange`.
  - **Two-Layer Smart Object & Clipping Structure**: Implemented base layer alpha clipping mask (`baseAlpha <= 16` masked out, `baseAlpha > 16` uses texture override or falls back to base), supporting `Surface_<Face>_Base` and `Surface_<Face>_Texture` smart objects/layers, plus one-click setup and unpack to groups.
  - **Canvas Boundary Drawing Isolation**: Isolated drawing tools (pencil, brush, floodfill) on shared layers across Easymetric surfaces (`TOP`, `DOWN`, `FRONT`, `BACK`, `RIGHT`, `LEFT`), constraining scanlines and floodfill bounds strictly to the originating slice bounds.
  - **Linked Surface Dimension Dependencies**: Propagated 3D cube geometric dimension links (`Top/Down (w,h) -> X=w, Z=h`, `Front/Back (w,h) -> X=w, Y=h`, `Right/Left (w,h) -> Z=w, Y=h`), dynamically resizing slice keys and cels across frames.
- 2026-10-31 — Gemini Flash (Antigravity): Replaced floating studio window with fully integrated **DocView Multi-Pane Workspace Layout** matching user UI reference (`src/app/commands/cmd_easymetric.cpp`, `src/app/ui/doc_view.{h,cpp}`, `src/app/ui/editor/editor.{h,cpp}`, `src/app/ui/editor/standby_state.cpp`, `src/app/ui_context.cpp`):
  - **Zero Floating Popups**: Entire sprite tab splits directly into 4 columns x 2 rows (`Top/Bottom | Front/Back | Right/Left | Preview & Fields`).
  - **6 Dedicated Surface Viewports**: Each pane hosts an interactive `Editor` with hidden scrollbars, auto-fitting zoom/scroll to its surface slice, and automatic layer selection (`Surface_<NAME>`) on click.
  - **Live Preview & Fields Panel**: Column 4 provides real-time 3D model render on checkerboard background (top) and beige `#d6cfc2` Fields control panel (bottom) with rotation, pitch, pivot, tag dropdown, map toggles, render types, outline, per-face depth editing, baking, and `.obj` export.
  - Toggle `Alt+F7` or `Exit Studio` switches seamlessly between standard single editor and integrated 8-pane Easymetric layout.
- 2026-10-31 — Gemini Argon (Antigravity): Restructured native C++ **Easymetric 6-Surface Live Studio** (`src/app/commands/cmd_easymetric.cpp`, `src/app/ui/editor/editor.cpp`) to match the 4-column (`Top/Bottom | Front/Back | Right/Left | Preview & Fields`) reference layout:
  - **3×2 Surface Column Grid + Folder-Tab Headers**: Left column = `Top` (`TOP`) & `Bottom` (`DOWN`); Middle column = `Front` (`FRONT`) & `Back` (`BACK`); Right column = `Right` (`RIGHT`) & `Left` (`LEFT`), rendered with slate-blue folder-tab headers, `:::` grip dots, symmetry lines, and resize handles.
  - **4th Column `Preview` + `Fields` Panel**: Top half renders the real-time 3D model on an Aseprite checkerboard viewport (`Preview` folder tab); bottom half (`Fields` folder tab, warm beige `#d6cfc2` container) provides:
    1. **Y-Axis Rotation** (`-180°..+180°`), **Camera Angle** (`-75°..+75°`), and **Pivot X/Y/Z** sliders (`Center` / `Base`).
    2. **Tag / Preset Angle Dropdown** (`ui::ComboBox`) + **`Save Tag`** button (reads/applies sprite `doc::Tag`s and camera presets).
    3. **Map Toggles** (`Diffuse`, `Normal`, `Depth` — single or split multi-map preview) and **Render Mode Dropdown** (`Textured (Lit)`, `Textured (Unlit)`, `Colored (Faces)`, `Normal Shaded`, `Depth Shaded`, `Wireframe + Tex`).
    4. **`Stroke Outline`** toggle, **`Cutouts`**, **`Lock Borders`**, and **`Sym X` / `Sym Y` / `Center Sym`**.
    5. **Editable Per-Face Depth Maps**: **`Render Face Depths (Edit)`** generates/populates the `Surface_DEPTH` layer across all 6 surface boxes so users can paint/sculpt grayscale height maps per face, with live 3D normal/depth relief via **`Use Face Depth Layer`**.
    6. **`Render`** button (bakes `Iso_Diffuse`, `Iso_Normal`, `Iso_Depth` layers) and **`Export .OBJ...`** button (exports Wavefront `.obj` + `.mtl` + `.png` texture atlas).
- 2026-10-30 — Gemini Argon (Antigravity): Enhanced native C++ **Easymetric 6-Surface Live Studio** (`src/app/commands/cmd_easymetric.cpp`, `src/app/ui/editor/editor.cpp`, `src/app/ui/editor/standby_state.cpp`):
  - **Per-Surface Layers & Draggable/Lockable Borders**: Creates dedicated per-surface layers (`Surface_FRONT`, `Surface_BACK`, `Surface_LEFT`, `Surface_RIGHT`, `Surface_TOP`, `Surface_DOWN`) and visual surface borders with 8 resize handles + live `WxH` header pills (`FRONT 24x24`). Borders can be dragged/resized directly on the canvas even while drawing tools (Pencil/Brush) are active (`Editor::calcHit` & `StandbyState::onMouseDown`), or locked via `Lock Borders` or layer lock.
  - **Per-Surface Controllable Symmetry Lines**: Renders internal X/Y symmetry guide lines inside each surface border and integrates with Aseprite's real symmetry engine (`Sym X`, `Sym Y`, `Center Sym`, and auto-centering `docPref.symmetry.xAxis`/`yAxis` on whichever surface border you paint inside).
  - **Independent Surface Size & Shape in 3D Model**: Resizing any surface border (`FRONT`, `BACK`, `LEFT`, `RIGHT`, `TOP`, `DOWN`) dynamically updates that surface's independent dimensions (`w[f] × h[f]`) in the real-time 3D Model, Normal Map, and Depth Map.
  - **True-Color Texture Rendering**: Fixed Windows Skia BGRA channel order via `convert_image_to_surface` and balanced lighting (`0.84..1.00`) so painted textures (e.g. yellow) display in true color.
- 2026-10-30 — Gemini Argon (Antigravity): Built native C++ **Easymetric 6-Surface Live Studio** (`src/app/commands/cmd_easymetric.cpp`, `src/app/commands/commands_list.h`, `src/app/ui/editor/editor.cpp`, `data/gui.xml`, `data/strings/en.ini`). Toggling `EasymetricStudio` (`View > Easymetric 6-Surface Mode (Real-Time)`, `Sprite > Easymetric 6-Surface Mode (Real-Time)`, or `Alt+F7`) segregates the sprite canvas into 6 color-coded, labeled surface screens (`FRONT`, `BACK`, `LEFT`, `RIGHT`, `TOP`, `DOWN` with floating header badges rendered in `Editor::drawSlices`) and opens a non-modal real-time 3D preview & control dock. Uses a 30 FPS FNV-1a hash-gated C++ ray-slab 3D renderer (including live `ExtraCel` mid-stroke pixel capture) to update the 3D Diffuse Model, Camera-Space Tangent Normal Map, and Z-Buffer Depth Map in real time while painting, with Y-Axis Rotation (`-180°..+180°`), Camera Pitch (`-75°..+75°`), mouse-drag orbit, 3D Pivot Point (`X/Y/Z`) sliders + crosshair, and 1-click bake to `Iso_Diffuse`, `Iso_Normal`, and `Iso_Depth` layers.
- 2026-10-29 — Gemini (Antigravity): Documented Aseprite 2.0 version controlling system (manifest.json, Ctrl+Alt+S, thumbnails, MCP tools), MCP server architecture (port 8765, PROTOCOL.md v1, 34 tools, security.cpp whitelist, transaction isolation), and multi-frame visual diff comparison (with cello/sax animation frame strips and rarity progression) in the public AI Session Results showcase (`Ai session results/public/index.html`). Created `AGENTS.md` protocol in `Ai session results`.
- 2026-10-29 — Gemini Argon (Antigravity): Created `.agents/skills/cpp-aseprite-optimization/SKILL.md` and applied C++ engine optimizations across `src/app/versions/version_store.cpp` and `src/app/commands/cmd_version.cpp`: scanline row-pointer (`uint32_t* __restrict`) diff loop with `std::memcmp` identical-row fast-path, bitwise luma/alpha math (`>> 2`, `>> 8`), in-memory `ManifestCache` keyed by `(path, fileSize, mtime)` with single-syscall pre-sized `std::ifstream` reads and `reserve()`, detached `load_document(nullptr, ...)` in `restoreVersion`/`openVersion`, and per-`(verId, frame)` `CachedDiffSurfaces` cache in `VersionHistoryWindow`.
- 2026-10-29 — Gemini Argon (Antigravity): Fixed crash-on-close in `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`): `m_listbox` was declared before its parent `m_view`, causing `ui::View::~View()` (`Widget::~Widget()`) to run first on window exit and call `delete &m_listbox` on a stack member. Reordered members so all parent containers (`m_mainBox`, `m_splitBox`, `m_view`, `m_rightBox`, `m_frameBox`, `m_actionBox`) precede their child widgets, and added `~VersionHistoryWindow()` cleanup.
- 2026-10-29 — Gemini Argon (Antigravity): Optimized `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`) and `VersionStore` (`src/app/versions/version_store.{h,cpp}`) to fit horizontally on any screen (`calcLeftWidth` / `calcRightWidth` budgeted to `ui::Manager::getDefault()->bounds().w`), added multi-frame selector (`<` / `Frame X / Y` / `>`) to compare any frame across versions, and eliminated `"Not Responding"` UI stalls by loading snapshots detached from `UIContext` (`load_document(nullptr, ...)`), caching the loaded snapshot `Doc` in memory across frame changes, removing nested `onPaint` loops, disabling synchronous disk logging in `bridge.lua`, and replacing Lua pixel loops with native C++ `Image:resize` and `Image:drawImage`.
- 2026-10-28 — Gemini Argon (Antigravity): Implemented Phase 6 workflow boosters: C++ visual diff widget in `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`, `src/app/versions/version_store.{h,cpp}`), `export_preview_sheet` (1x + 4x contact sheet + GUI menu under `File > Export`), `recolor_ramp`, `auto_outline`, `draw_shapes`, `canvas_expand_centered`, `version_diff`, `tag_manage`, `export_spritesheet` in `bridge.lua` and `tools/aseprite2-mcp/src/index.ts`, plus Stage-2 approval publisher `publish-approved-asset.ps1`. Rebuilt C++ & TS and live-installed to `%LOCALAPPDATA%\Programs\Aseprite 2.0`.
- 2026-10-28 — Gemini Flash (Antigravity): Built and installed Aseprite 2.0 to `%LOCALAPPDATA%\Programs\Aseprite 2.0`. Created `tools/install-aseprite2.ps1` script, generated Desktop and Start Menu shortcuts, added install directory to User PATH, and updated `.aseprite`/`.ase` Windows file associations to launch Aseprite 2.0. Verified binary version `2.0-dev+14fe117e8`.
- 2026-10-28 — Gemini Flash (Antigravity): Generated 4 mic rarity variations (Common, Rare, Epic with shockmount, Legendary with celestial wings & aura) matching Pitch Quest conventions (Trumphet4, Cello4). Created 32x60 multi-layer, multi-frame `Mic.aseprite` and exported 1x/4x PNGs to Google Drive (`Working Files\Music Store\Export\Instruments\`). Fixed Lua bridge WebSocket scoping, token auto-resolution, deadlock prevention for non-drawing commands, and `version.list` serialization. Verified live MCP interaction and project snapshot versioning (`Mic.aseprite.versions/`).
- 2026-10-27 — Gemini Flash (Antigravity): Completed Phase 4 (TypeScript build, bridge call methods, smoke test verified), Phase 5 (C++ PixelLab auto-import hook in Extensions constructor, verified auto-import into %APPDATA%\Aseprite2\extensions\pixellab), built aseprite.exe cleanly (exit 0), created tools/aseprite2-mcp/README.md. All phases complete and tested.
- 2026-10-27 — Claude Opus (Antigravity): explored repo, wrote plan + protocol + versioning spec + this handoff. Started Phase 1.
