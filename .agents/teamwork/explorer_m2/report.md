# Technical Strategy & Architecture Analysis Report: Task ca894e89
**Component**: Easymetric 6-Surface Live Studio & 3D Visual Hull Engine  
**Target File**: `src/app/commands/cmd_easymetric.cpp`  
**Milestone**: Phase 1 Base-First Silhouette Carving (Task `ca894e89-f82c-4655-aeae-2bd500450001`)  
**Author**: Explorer M2 (`teamwork_explorer`)  
**Date**: 2026-11-03  

---

## 1. Executive Summary

Task `ca894e89-f82c-4655-aeae-2bd500450001` mandates that:
> *"Base sheet defines the silhouette; other views are clamped to it.  
> Do NOT restore paired Top-Bottom dimension locking in this phase — TC 4.2/4.3 from the four-requirements suite is being investigated separately by arin-pc-codex, and PR #5 decoupling is the current design.  
> Test: (a) simple box, all views match Base; (b) change Base silhouette, other views follow or warn — never silently break the silhouette."*

This report provides a comprehensive architectural inspection of `src/app/commands/cmd_easymetric.cpp`, identifying the exact mechanics of `extractSixSurfaces` (lines 1829–2083) and `buildSculptedVolume` (lines 340–476), pinpointing how silhouette leakage and silent fallback bugs currently occur, and establishing the exact C++ modification strategy to enforce Base-first silhouette clamping while preserving PR #5 column/surface decoupling.

---

## 2. In-Depth Inspection of Existing Architecture

### 2.1 Surface Extraction & Alpha Masking (`extractSixSurfaces`, lines 1829–2083)

1. **Shared Sheet Resolution (`findSharedLayers`, lines 629–643)**:
   - Easymetric documents utilize three shared UV sheets across all 6 faces:
     - `Surface_Base`: The master silhouette and structural base sheet.
     - `Surface_Texture`: The texture/Smart Object overlay sheet.
     - `Surface_DEPTH`: The 2.5D displacement/normal depth map sheet.
   - Slices (`TOP`, `DOWN`, `FRONT`, `BACK`, `RIGHT`, `LEFT`) define the face UV rects on the shared canvas according to `kFaceSpecs` (Top/Bottom = Col 0, Front/Back = Col 1, Right/Left = Col 2).

2. **Per-Face Pixel Extraction & Alpha Blending (lines 1941–1987)**:
   - For each face `f` in `0..5`:
     ```cpp
     uint32_t baseCol = getRgbaPixel(baseImg, canvasX - baseOx, canvasY - baseOy);
     uint32_t texCol = getRgbaPixel(texImg, canvasX - texOx, canvasY - texOy);
     ```
   - If both `baseImg` and `texImg` exist:
     - If `doc::rgba_geta(baseCol) > 16`:
       - If texture alpha `aT == 255`: `finalCol = texCol`
       - If `aT > 0`: alpha-blends `texCol` over `baseCol`
       - If `aT == 0`: falls back to `baseCol`
     - If `doc::rgba_geta(baseCol) <= 16`:
       - `finalCol = 0` (clipped out)

3. **Architectural Vulnerabilities in Current Extraction**:
   - **Vulnerability 1: Unclipped Texture Fallback (line 1975)**:
     ```cpp
     else if (texImg) {
       finalCol = texCol;
     }
     ```
     If `baseImg` is missing, has no cel on the current frame, or is temporarily unallocated, `texImg` is taken raw with zero silhouette clipping. Texture pixels can freely spill outside any silhouette.
   - **Vulnerability 2: Silent Resurrection of Starter Pixels (lines 2057–2063)**:
     ```cpp
     if (!hasPixels) {
       for (int v = 0; v < fh; ++v) {
         for (int u = 0; u < fw; ++u) {
           out.pixels[f][v * fw + u] = starterSurfacePixel(f, u, v, fw, fh);
         }
       }
     }
     ```
     When a user deliberately erases or cuts out the silhouette on `Surface_Base`, `hasPixels` becomes false. Line 2057 intercepts this and silently repopulates the face with `starterSurfacePixel` (the default colored calibration box). This directly violates Directive 3: *"never silently break the silhouette"*.
   - **Vulnerability 3: ExtraCel (Live Brush) Silhouette Bleed (lines 1989–2040)**:
     When editing `texLyr` without a valid `baseImg`, extra-cel strokes are painted directly into `out.pixels` without masking.

---

### 2.2 3D Visual Hull Silhouette Carving (`buildSculptedVolume`, lines 340–476)

1. **Voxel Grid & Extents**:
   - Voxel grid dimensions `(vol.vx, vol.vy, vol.vz)` are derived from the maximum dimensions of the six faces (clamped to `[8, 64]`).
   - Dimension mapping:
     - $X$ corresponds to width of Front, Back, Top, Bottom.
     - $Y$ corresponds to height of Front, Back, Left, Right.
     - $Z$ corresponds to width of Left, Right and height of Top, Bottom.

2. **Orthographic Visual Hull Carving**:
   - A voxel at `(x, y, z)` starts as `solid = true`.
   - **Top-Down ($Y$ axis projection)**:
     Maps `(x, z)` to `(uTop, vTop)` in `kFaceTop`.
     `topSolid = data.isFaceSolid(kFaceTop, uTop, vTop)`.
     If `!data.colLinked[0] && hasDown`:
       `topSolid = (hasTop ? (topSolid || downSolid) : downSolid)`.
     If `!topSolid`: `solid = false`.
   - **Front-Back ($Z$ axis projection)**:
     Maps `(x, y)` to `(uFront, vFront)` in `kFaceFront`.
     `frontSolid = data.isFaceSolid(kFaceFront, uFront, vFront)`.
     If `!data.colLinked[1] && hasBack`:
       `frontSolid = (hasFront ? (frontSolid || backSolid) : backSolid)`.
     If `!frontSolid`: `solid = false`.
   - **Right-Left ($X$ axis projection)**:
     Maps `(z, y)` to `(uRight, vRight)` in `kFaceRight`.
     `sideSolid = data.isFaceSolid(kFaceRight, uRight, vRight)`.
     If `!data.colLinked[2] && hasLeft`:
       `sideSolid = (hasRight ? (sideSolid || leftSolid) : leftSolid)`.
     If `!sideSolid`: `solid = false`.
   - If `solid == true`: voxel is committed `vol.setSolid(x, y, z, true)`.

3. **Architectural Vulnerabilities in Current Carving**:
   - **Vulnerability 1: Unbounded Union of Opposing Faces**:
     When opposing faces are unlinked (`!colLinked`), using `topSolid || downSolid` means an unconstrained secondary face (e.g. Down) can expand the visual hull far beyond the Base sheet silhouette of the primary face (Top).
   - **Vulnerability 2: No Explicit Base Silhouette Channel in `SixSurfaceData`**:
     `SixSurfaceData` currently aggregates only `pixels[f]` (RGBA composite). `isFaceSolid` checks `rgba_geta(sampleSymmetric(face, u, v)) > 16`. If texture pixels leak through due to missing base cels, the 3D visual hull is carved using leaked texture pixels rather than the true Base silhouette.

---

### 2.3 PR #5 Decoupling vs Dimension Locking (TC 4.2 / 4.3)

- **PR #5 Architecture (`6e2cf5026` / `a70ab7b92`)**:
  - Replaced the old rigid uniform grid ($3 \times \text{cellW}, 2 \times \text{cellH}$) with exact per-column and per-row layouts:
    - $\text{colW}[c] = \max(w[\text{row0}], w[\text{row1}])$
    - $\text{rowH}[r] = \max(h[\text{col0}], h[\text{col1}], h[\text{col2}])$
    - $\text{sheetW} = \text{colW}[0] + \text{colW}[1] + \text{colW}[2]$
    - $\text{sheetH} = \text{rowH}[0] + \text{rowH}[1]$
  - Resizing one face (via `propagateLinkedDimensions`, lines 1052–1078) resizes **only** that face (`targetW[srcFace] = newW; targetH[srcFace] = newH;`).
  - Columns and rows are fully decoupled.
- **Directive Mandate**:
  - Do **NOT** restore paired dimension locking across columns in `propagateLinkedDimensions`.
  - TC 4.2 and TC 4.3 in `tests/easymetric_four_requirements_test.lua` expect old uniform locking and are handled separately by `arin-pc-codex`.
  - The Base-first silhouette implementation must operate strictly within the PR #5 decoupled model.

---

## 3. Precise C++ Code Modification Strategy

To fulfill Task `ca894e89`, the C++ modifications are organized into four coordinated changes in `src/app/commands/cmd_easymetric.cpp`:

```
┌────────────────────────────────────────────────────────────────────────┐
│                      cmd_easymetric.cpp Architecture                   │
├────────────────────────────────┬───────────────────────────────────────┤
│ 1. SixSurfaceData              │ - Add baseAlpha[kFaceCount]           │
│    (lines 213–338)             │ - Add hasBaseLayer[kFaceCount]        │
│                                │ - Add isBaseSolid(face, u, v)         │
├────────────────────────────────┼───────────────────────────────────────┤
│ 2. extractSixSurfaces          │ - Store explicit Base alpha mask      │
│    (lines 1829–2083)           │ - Strictly clamp texImg to Base mask  │
│                                │ - Eliminate silent starter pixel seed │
│                                │ - Clamp ExtraCel brush preview to Base│
├────────────────────────────────┼───────────────────────────────────────┤
│ 3. buildSculptedVolume         │ - Carve visual hull using Base mask   │
│    (lines 340–476)             │ - Clamp secondary views to Base hull  │
│                                │ - Prevent unlinked union hull bleed   │
├────────────────────────────────┼───────────────────────────────────────┤
│ 4. pollAndRender               │ - Detect Base silhouette modifications│
│    (lines 3730–3820)           │ - Display non-intrusive StatusBar tip │
└────────────────────────────────┴───────────────────────────────────────┘
```

### 3.1 Modification 1: `SixSurfaceData` Explicit Base Silhouette Channel
**Target Location**: `src/app/commands/cmd_easymetric.cpp`, lines 213–338.

1. **Add Fields**:
   ```cpp
   std::array<std::vector<uint8_t>, kFaceCount> baseAlpha;
   std::array<bool, kFaceCount> hasBaseLayer = { false, false, false, false, false, false };
   ```
2. **Update `init()` and `setFaceSize()`**:
   - `init(int faceSize)`:
     ```cpp
     baseAlpha[f].assign(S * S, 0);
     hasBaseLayer.fill(false);
     ```
   - `setFaceSize(int face, int fw, int fh)`:
     ```cpp
     baseAlpha[face].assign(fw * fh, 0);
     ```
3. **Add `isBaseSolid(int face, int u, int v) const`**:
   ```cpp
   bool isBaseSolid(int face, int u, int v) const
   {
     if (face < 0 || face >= kFaceCount) return false;
     const int fw = w[face];
     const int fh = h[face];
     if (fw <= 0 || fh <= 0 || baseAlpha[face].empty()) return false;
     u = std::clamp(u, 0, fw - 1);
     v = std::clamp(v, 0, fh - 1);
     return baseAlpha[face][v * fw + u] > 16;
   }
   ```
4. **Harden `isFaceSolid()`**:
   ```cpp
   bool isFaceSolid(int face, int u, int v) const
   {
     if (hasBaseLayer[face]) {
       return isBaseSolid(face, u, v);
     }
     return doc::rgba_geta(sampleSymmetric(face, u, v)) > 16;
   }
   ```

---

### 3.2 Modification 2: `extractSixSurfaces` Strict Clamping & Anti-Resurrection
**Target Location**: `src/app/commands/cmd_easymetric.cpp`, lines 1829–2083.

1. **Record Base Layer Existence & Alpha Channel (around line 1940)**:
   ```cpp
   const bool faceHasBase = (baseImg != nullptr);
   out.hasBaseLayer[f] = faceHasBase;
   ```
2. **Strict Clamping in Pixel Loop (lines 1941–1987)**:
   ```cpp
   const uint8_t bA = doc::rgba_geta(baseCol);
   out.baseAlpha[f][v * fw + u] = bA;

   uint32_t finalCol = 0;
   if (faceHasBase) {
     // Base sheet strictly defines the silhouette boundary:
     // If base pixel is transparent (bA <= 16), content is clamped to 0.
     if (bA > 16) {
       if (texImg) {
         const int aT = doc::rgba_geta(texCol);
         if (aT == 255) {
           finalCol = texCol;
         }
         else if (aT > 0) {
           const int invA = 255 - aT;
           finalCol = doc::rgba(
             (doc::rgba_getr(texCol) * aT + doc::rgba_getr(baseCol) * invA) / 255,
             (doc::rgba_getg(texCol) * aT + doc::rgba_getg(baseCol) * invA) / 255,
             (doc::rgba_getb(texCol) * aT + doc::rgba_getb(baseCol) * invA) / 255,
             bA);
         }
         else {
           finalCol = baseCol;
         }
       }
       else {
         finalCol = baseCol;
       }
     }
     // When bA <= 16: finalCol remains 0 (clamped to Base silhouette)
   }
   else if (texImg) {
     finalCol = texCol;
   }
   ```

3. **Harden ExtraCel Brush Preview (lines 1989–2040)**:
   Ensure that when `baseImg` is present, brush strokes on `texLyr` are strictly masked:
   ```cpp
   if (faceHasBase) {
     const int bx = canvasX - baseOx;
     const int by = canvasY - baseOy;
     const uint32_t bc = getRgbaPixel(baseImg, bx, by);
     if (doc::rgba_geta(bc) <= 16) {
       continue; // Clamped by Base silhouette
     }
   }
   ```

4. **Eliminate Silent Starter Pixel Resurrection (lines 2057–2063)**:
   ```cpp
   // Only populate starter pixels if the sprite has NO established shared layers
   // and NO base/texture layers (pure virgin document initialization).
   if (!hasPixels && !useShared && !baseLyr && !texLyr) {
     for (int v = 0; v < fh; ++v) {
       for (int u = 0; u < fw; ++u) {
         out.pixels[f][v * fw + u] = starterSurfacePixel(f, u, v, fw, fh);
       }
     }
   }
   ```
   *Rationale*: If the document has `Surface_Base`, an empty face is an intentional cutout/erasure. Overwriting it with `starterSurfacePixel` silently breaks the user's silhouette.

5. **Opposing Face Synchronization (lines 2067–2080)**:
   When `colLinked[c]` is true, mirror `baseAlpha` and `hasBaseLayer`:
   ```cpp
   out.baseAlpha[btmFace] = out.baseAlpha[topFace];
   out.hasBaseLayer[btmFace] = out.hasBaseLayer[topFace];
   ```

---

### 3.3 Modification 3: `buildSculptedVolume` Base Silhouette Visual Hull Carving
**Target Location**: `src/app/commands/cmd_easymetric.cpp`, lines 360–476.

1. **Base-First Projection Intersection**:
   In each of the three projections (TopDown, FrontBack, RightLeft):
   - Sample `topSolid`, `frontSolid`, `sideSolid` using `data.isFaceSolid()` (which delegates to `isBaseSolid()` when `hasBaseLayer` is true).
   - In unlinked mode (`!data.colLinked`), secondary views must **not** union-expand beyond the primary Base silhouette:
     ```cpp
     if (activeTopDown) {
       const int uTop = std::clamp(
         int((float(x) + 0.5f) / float(vol.vx) * data.w[kFaceTop]), 0, data.w[kFaceTop] - 1);
       const int vTop = std::clamp(
         int((float(z) + 0.5f) / float(vol.vz) * data.h[kFaceTop]), 0, data.h[kFaceTop] - 1);
       bool topSolid = data.isFaceSolid(kFaceTop, uTop, vTop);

       if (!data.colLinked[0] && hasDown) {
         const int uDown = std::clamp(
           int((float(x) + 0.5f) / float(vol.vx) * data.w[kFaceDown]), 0, data.w[kFaceDown] - 1);
         const int vDown = std::clamp(
           int((float(vol.vz - 1 - z) + 0.5f) / float(vol.vz) * data.h[kFaceDown]),
           0, data.h[kFaceDown] - 1);
         const bool downSolid = data.isFaceSolid(kFaceDown, uDown, vDown);
         // Clamped: If primary face Top has Base silhouette, Down cannot expand outside it
         if (data.hasBaseLayer[kFaceTop]) {
           topSolid = topSolid && (hasTop ? true : downSolid);
         }
         else {
           topSolid = (hasTop ? (topSolid || downSolid) : downSolid);
         }
       }
       if (!topSolid) solid = false;
     }
     ```
   - Apply matching logic symmetrically to `FrontBack` and `RightLeft`.
   - As a result:
     - The 3D carved hull is strictly bounded by the intersection of all Base silhouettes.
     - Secondary views (texture, secondary cels) can never leak solid voxels outside the Base hull.

---

### 3.4 Modification 4: GUI Notification on Silhouette Clamping (`pollAndRender`)
**Target Location**: `src/app/commands/cmd_easymetric.cpp`, lines 3730–3824.

1. **Silhouette Tracking**:
   Compute a compact hash of `out.baseAlpha` across all faces.
2. **User Feedback**:
   When the Base silhouette changes and secondary views are clamped, notify via `StatusBar`:
   ```cpp
   if (baseSilhouetteChanged) {
     if (auto* sb = StatusBar::instance())
       sb->showTip(1000, "Views clamped to Base sheet silhouette");
   }
   ```
   This satisfies test requirement (b): *"change Base silhouette, other views follow or warn — never silently break the silhouette."*

---

## 4. Line-Length & Differential Hygiene Rules

Per `AGENTS.md` and `PROJECT.md`:
1. **Line Width**: Every modified line in `src/app/commands/cmd_easymetric.cpp` must be $\le 100$ columns.
2. **Indentation**: Exactly 2 spaces per indentation level.
3. **No Header/Signature Breaking**: Maintain exact public and private function signatures.
4. **Zero Shadowing**: Verify no shadowed local variables (`colLinked`, `spr`, `doc`).
5. **Differential Hygiene Command**:
   ```bash
   git diff -U0 HEAD -- "src/*"
   ```
   Must pass with zero lines exceeding 100 columns.

---

## 5. Verification & Test Plan

| Test ID | Category | Method / Command | Expected Result |
|---------|----------|------------------|-----------------|
| **T1** | Box Silhouette | Load simple box asset; verify all 6 views match Base sheet bounds | All views render solid box matching Base dimensions |
| **T2** | Base Silhouette Cutout | Erase corner on `Surface_Base` (Front view) | Texture on Front clips instantly; 3D volume carves away corner; `StatusBar` confirms clamping |
| **T3** | Anti-Resurrection | Erase entire face on `Surface_Base` | Face stays empty/transparent; `starterSurfacePixel` is NOT resurrected |
| **T4** | Texture Clamping | Paint on `Surface_Texture` outside `Surface_Base` boundary | Texture pixels outside Base silhouette are clamped to transparent (alpha = 0) |
| **T5** | Easymetric Regression | `powershell -File tests\run_easymetric_tests.ps1` | 67 / 67 passed |
| **T6** | Adversarial Stress | `powershell -File tests\run_easymetric_tests.ps1 -TestScript tests\easymetric_adversarial_stress.lua` | 54 / 54 passed |
| **T7** | Four Requirements | `powershell -File tests\run_easymetric_tests.ps1 -TestScript tests\easymetric_four_requirements_test.lua` | 21 / 23 passed (TC 4.2/4.3 scoped for codex) |
| **T8** | Subagent Anti-Loop | `python tools/agent-mesh/test_mesh_subagent.py` | 19 / 19 passed |
| **T9** | Differential Hygiene | `git diff -U0 HEAD -- "src/*"` | Zero lines > 100 columns |
| **T10** | MSVC C++ Build | `powershell -File tools\aseprite2-build.ps1` | Exit code 0, binary `build/bin/aseprite.exe` generated |

---

## 6. Conclusion

The proposed C++ strategy directly addresses all facets of Task `ca894e89`:
- It makes `Surface_Base` the definitive master silhouette mask in `extractSixSurfaces`.
- It eliminates the silent `starterSurfacePixel` resurrection bug.
- It bounds the 3D visual hull carving in `buildSculptedVolume` to the Base silhouette.
- It leaves PR #5 column/surface decoupling completely intact without prematurely touching TC 4.2/4.3.
- It adheres strictly to Aseprite C++ formatting and hygiene rules.
