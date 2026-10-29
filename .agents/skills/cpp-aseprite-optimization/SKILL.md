---
name: cpp-aseprite-optimization
description: "High-performance C++ optimization patterns for Aseprite 2.0: scanline row-pointer SIMD/auto-vectorization, branchless RGBA pixel math, offscreen detached Doc loading, manifest & frame surface caching, and zero-stall UI painting."
---

# /cpp-aseprite-optimization

Actionable C++ performance patterns for **Aseprite 2.0** (MSVC x64, Skia `LAF_BACKEND`, `doc::Image`, `ui::Widget`, and `VersionStore`). Apply these rules whenever writing or optimizing C++ code in `src/`.

---

## 1. Scanline Row-Pointer Iteration (`__restrict` + `std::memcmp`)

- **Anti-Pattern**: Calling `doc::get_pixel(img, x, y)` or `doc::get_pixel_fast<RgbTraits>(img, x, y)` inside an inner `for (int x ...)` loop recomputes `y * rowstride + x * sizeof(color_t)` on every pixel and blocks MSVC auto-vectorization due to potential pointer aliasing.
- **Optimization**:
  1. Fetch scanline row pointers once per `y` iteration using `reinterpret_cast<const uint32_t* __restrict>(img->getPixelAddress(0, y))`.
  2. When comparing two equal-width RGBA scanlines (e.g., in `VersionStore::computeVisualDiff`), run `std::memcmp(curRow, verRow, width * sizeof(uint32_t)) == 0` first. If the entire scanline is identical, process the whole row with a tight, branch-free loop that MSVC vectorizes with AVX2/SSE2.

```cpp
const uint32_t* __restrict curRow =
  reinterpret_cast<const uint32_t*>(curImg->getPixelAddress(0, y));
const uint32_t* __restrict verRow =
  reinterpret_cast<const uint32_t*>(verImg->getPixelAddress(0, y));
uint32_t* __restrict dstRow =
  reinterpret_cast<uint32_t*>(diffImg->getPixelAddress(0, y));
```

---

## 2. Fast Bitwise Luma & Alpha Math (No Division in Inner Loops)

- **Anti-Pattern**: Computing `(r + g + b) / 3` and `(a * 90) / 255` per pixel uses integer division instructions in the inner loop.
- **Optimization**:
  - Replace `(r + g + b) / 3` with fast perceptual luma shift `((r * 77 + g * 150 + b * 29) >> 8)` or `((r + (g << 1) + b) >> 2)`.
  - Replace `(a * 90) / 255` with `(a * 90) >> 8`.

---

## 3. Detached Offscreen `Doc` Loading (`load_document(nullptr, path)`)

- **Anti-Pattern**: Passing the active `UIContext*` to `load_document(ctx, path)` for background inspection, thumbnail generation, or version restore preparation. `load_document` calls `doc->setContext(ctx)`, which attaches the temporary `Doc` to the live workspace, creates a UI tab, fires observer callbacks, and freezes or corrupts the UI if called inside a modal dialog.
- **Optimization**:
  - Always pass `nullptr` (`load_document(nullptr, path)`) when loading a `.aseprite` file offscreen.
  - In `VersionStore::restoreVersion`, load the snapshot with `load_document(nullptr, snapshotPath)` *before* destroying the active document, then attach `restoredDoc->setContext(ctx)` once at the end.

---

## 4. Two-Level Caching (Manifest Stat Cache & Per-Frame Surface Cache)

- **Anti-Pattern**: Re-reading and re-parsing `manifest.json` from disk (especially on network/cloud drives like Google Drive `I:\`) on every `listVersions` call, or re-rendering `os::Surface` previews when stepping back and forth between already-inspected frames/versions.
- **Optimization**:
  - **Manifest Cache**: Cache the parsed `VersionManifest` in memory keyed by `(manifestPath, fileSize, mtime)` so unchanged manifests return in `O(1)` with zero disk I/O.
  - **Dialog Frame/Version Surface Cache**: Inside modal dialogs like `VersionHistoryWindow`, cache `{verSurf, curSurf, diffSurf, stats}` keyed by `(versionId, frame)` so switching between frames (`1..N`) or revisiting versions takes `0.0ms`.

---

## 5. Pre-Sized File Reads & `std::vector::reserve`

- **Anti-Pattern**: Reading JSON/files via `std::stringstream ss; ss << file.rdbuf();` and pushing to `std::vector` without `reserve()`.
- **Optimization**:
  - Open with `std::ios::binary | std::ios::ate`, allocate `std::string content(size, '\0')` once, and read in a single `file.read(&content[0], size)` syscall.
  - Always call `vec.reserve(jsonArray.size())` before populating vectors.

---

## 6. `ui::Widget` Stack Destruction Order & Zero-Loop `onPaint`

- **Destruction Order Rule**: `ui::Widget::~Widget()` deletes all remaining pointers in `m_children`. When declaring stack-allocated child widgets inside a `ui::Window` subclass, **parent containers MUST be declared before their child widgets** (`m_view` before `m_listbox`, `m_actionBox` before its `ui::Button`s) so C++ reverse-order destruction removes children from `m_parent->m_children` before the parent destructor runs.
- **Zero-Loop `onPaint` Rule**: Never run nested `for (y) for (x)` loops calling `g->fillRect` inside `Widget::onPaint(ui::PaintEvent&)`. Use constant-time rect fills or pre-rendered `os::SurfaceRef` blits.
