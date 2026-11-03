# Handoff Report: Explorer M2 (Easymetric Base Silhouette Clamping Architecture)

**Working Directory**: `e:\Work\Development\aseprite\.agents\teamwork\explorer_m2`  
**Identity**: `teamwork_explorer`  
**Recipient**: Orchestrator (`f47682be-6cc6-4df9-bbe4-491c1979bf04`)  
**Task Reference**: `ca894e89-f82c-4655-aeae-2bd500450001` (Phase 1 Base-first silhouette carving)  
**Date**: 2026-11-03  

---

## 1. Observation

1. **`cmd_easymetric.cpp` Architecture & Entry Points**:
   - `src/app/commands/cmd_easymetric.cpp:604–643`: Shared UV sheets are resolved via `findSharedLayers(spr)`, returning pointers to `Surface_Base`, `Surface_Texture`, and `Surface_DEPTH`.
   - `src/app/commands/cmd_easymetric.cpp:1829–2083`: `extractSixSurfaces(Doc* doc, doc::frame_t frame, SixSurfaceData& out, bool useFaceDepth, DocView* docView, const std::array<bool, 3>* colLinked)`.
   - `src/app/commands/cmd_easymetric.cpp:1950–1980`: Alpha masking between `baseImg` and `texImg`:
     ```cpp
     if (baseImg && texImg) {
       if (doc::rgba_geta(baseCol) > 16) {
         // texture blended or overrides base
       }
     }
     else if (texImg) {
       finalCol = texCol; // Line 1975: Unclipped texture if baseImg is null!
     }
     ```
   - `src/app/commands/cmd_easymetric.cpp:2057–2063`: Silent starter pixel overwrite:
     ```cpp
     if (!hasPixels) {
       for (int v = 0; v < fh; ++v) {
         for (int u = 0; u < fw; ++u) {
           out.pixels[f][v * fw + u] = starterSurfacePixel(f, u, v, fw, fh);
         }
       }
     }
     ```
     Observed directly: If a user erases all pixels on `Surface_Base` for a face to create a cut-out or empty view, `hasPixels` becomes false, causing `extractSixSurfaces` to silently regenerate the default starter calibration box (`starterSurfacePixel`).
   - `src/app/commands/cmd_easymetric.cpp:340–476`: `buildSculptedVolume(const SixSurfaceData& data, int pivX, int pivY, int pivZ)` computes 3D voxel volume `vol` of size up to $64 \times 64 \times 64$.
   - Lines 420–466: In `buildSculptedVolume`, orthographic visual hull carving uses:
     `topSolid = (hasTop ? (topSolid || downSolid) : downSolid)`
     `frontSolid = (hasFront ? (frontSolid || backSolid) : backSolid)`
     `sideSolid = (hasRight ? (sideSolid || leftSolid) : leftSolid)`
     `if (solid) vol.setSolid(x, y, z, true);`
     When opposing faces are unlinked, the union `(topSolid || downSolid)` can expand the voxel hull beyond the primary Base sheet silhouette.
   - `src/app/commands/cmd_easymetric.cpp:1052–1078`: `propagateLinkedDimensions` resizes **only** `srcFace` (`targetW[srcFace] = newW; targetH[srcFace] = newH;`) under the PR #5 exact decoupled UV layout.

2. **Standing Task & Test Directives**:
   - Assignment `ca894e89-f82c-4655-aeae-2bd500450001`: *"Base sheet defines the silhouette; other views are clamped to it. Do NOT restore paired Top-Bottom dimension locking in this phase — TC 4.2/4.3 from the four-requirements suite is being investigated separately by arin-pc-codex, and PR #5 decoupling is the current design. Test: (a) simple box, all views match Base; (b) change Base silhouette, other views follow or warn — never silently break the silhouette."*
   - Muse directive `9ec4e074`: *"PR scope: your PR into Aseprite-2.0 must contain ONLY src/app/commands/cmd_easymetric.cpp + HANDOFF entry... differential hygiene audit git diff -U0 HEAD -- 'src/*' under 100 columns."*

---

## 2. Logic Chain

1. **Premise 1 (Master Silhouette Rule)**:
   Per Directive `ca894e89`, `Surface_Base` is the definitive master silhouette sheet.
   *Observation reference*: Observation 1 shows that currently, if `baseImg` has no cel or is null, line 1975 falls back to `finalCol = texCol` with zero clamping. Furthermore, line 2057 silently resurrects `starterSurfacePixel` when a face has no pixels.
   *Inference 1*: `extractSixSurfaces` must strictly clamp `texImg` to the `baseImg` alpha boundary when `Surface_Base` exists in the sprite, and must suppress `starterSurfacePixel` generation whenever the document has established shared layers.

2. **Premise 2 (3D Visual Hull Clamping Rule)**:
   In `buildSculptedVolume`, visual hull carving intersects the TopDown, FrontBack, and RightLeft orthographic silhouettes.
   *Observation reference*: Lines 420–466 show that `SixSurfaceData` only checks `out.pixels`, which can conflate texture and base. When opposing faces are unlinked, the union expansion `(topSolid || downSolid)` allows secondary faces to blow out the silhouette past the Base sheet boundary.
   *Inference 2*: `SixSurfaceData` must carry an explicit `baseAlpha` channel and `hasBaseLayer` flag. In `buildSculptedVolume`, each orthographic projection must be bounded by the Base silhouette, preventing secondary unlinked faces from expanding outside the Base projection.

3. **Premise 3 (Preservation of PR #5 Decoupling)**:
   Directive `ca894e89` explicitly forbids restoring paired dimension locking across columns.
   *Observation reference*: Lines 1052–1078 show PR #5 decoupled sizing.
   *Inference 3*: All silhouette clamping logic must be confined to pixel-level alpha masking (`extractSixSurfaces`) and voxel-level visual hull carving (`buildSculptedVolume`), without altering `propagateLinkedDimensions` or `sharedLayout`.

4. **Premise 4 (Feedback & Warning Requirement)**:
   Directive test (b) requires: *"change Base silhouette, other views follow or warn — never silently break the silhouette."*
   *Observation reference*: `StatusBar::instance()->showTip(...)` is established and standard across `cmd_easymetric.cpp`.
   *Inference 4*: `pollAndRender` should detect Base silhouette modifications and notify via `StatusBar::instance()->showTip(1000, "Views clamped to Base sheet silhouette")`.

---

## 3. Caveats

1. **TC 4.2 / 4.3 Dimension Locking**: As explicitly instructed, this implementation does NOT modify `propagateLinkedDimensions` to lock Top and Down dimensions. `tests/easymetric_four_requirements_test.lua` will continue reporting 21/23 passing as expected until `arin-pc-codex` concludes its separate investigation.
2. **Sub-selection across UV rects**: Moving a sub-selection across face UV rect boundaries on the shared canvas remains physically bounded by the single sheet image, but the newly enforced Base silhouette mask prevents any dragged pixels from rendering outside the target face's Base boundary.
3. **Read-Only Scope**: Explorer M2 has performed read-only architectural design and analysis. No source code files in `src/` have been altered.

---

## 4. Conclusion

A complete, precise, MSVC-compliant C++ implementation plan has been formulated in `report.md`:
1. **`SixSurfaceData`**: Add `baseAlpha` and `hasBaseLayer` arrays and `isBaseSolid()` method.
2. **`extractSixSurfaces`**: Enforce strict Base alpha clipping on `texImg`, eliminate the silent `starterSurfacePixel` overwrite on erased faces, and clamp live brush `ExtraCel` strokes to `baseImg`.
3. **`buildSculptedVolume`**: Bind visual hull carving to the Base silhouette mask, preventing unlinked secondary views from expanding the 3D volume beyond the Base projection.
4. **`pollAndRender`**: Add status notification when views are clamped to the Base silhouette.
5. **Hygiene**: All changes fit cleanly within the 100-column and 2-space indentation limit.

---

## 5. Verification Method

To verify the proposed implementation once written by the Worker:

1. **MSVC Build Pass**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\aseprite2-build.ps1
   ```
   *Expected*: Exit code 0, binary `build/bin/aseprite.exe` produced.

2. **Easymetric Regression Suite (67 tests)**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tests\run_easymetric_tests.ps1
   ```
   *Expected*: 67 / 67 tests passing.

3. **Adversarial Stress Suite (54 tests)**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tests\run_easymetric_tests.ps1 -TestScript tests\easymetric_adversarial_stress.lua
   ```
   *Expected*: 54 / 54 tests passing.

4. **Four Requirements Baseline (21 tests pass, TC 4.2/4.3 scoped)**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tests\run_easymetric_tests.ps1 -TestScript tests\easymetric_four_requirements_test.lua
   ```
   *Expected*: 21 passed, 2 failed (TC 4.2/4.3).

5. **Differential Hygiene Audit**:
   ```bash
   git diff -U0 HEAD -- "src/*"
   ```
   *Expected*: Zero lines exceeding 100 columns.
