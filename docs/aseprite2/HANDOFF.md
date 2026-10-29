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
- 2026-10-29 — Gemini (Antigravity): Documented Aseprite 2.0 version controlling system (manifest.json, Ctrl+Alt+S, thumbnails, MCP tools), MCP server architecture (port 8765, PROTOCOL.md v1, 34 tools, security.cpp whitelist, transaction isolation), and multi-frame visual diff comparison (with cello/sax animation frame strips and rarity progression) in the public AI Session Results showcase (`Ai session results/public/index.html`). Created `AGENTS.md` protocol in `Ai session results`.
- 2026-10-29 — Gemini Argon (Antigravity): Created `.agents/skills/cpp-aseprite-optimization/SKILL.md` and applied C++ engine optimizations across `src/app/versions/version_store.cpp` and `src/app/commands/cmd_version.cpp`: scanline row-pointer (`uint32_t* __restrict`) diff loop with `std::memcmp` identical-row fast-path, bitwise luma/alpha math (`>> 2`, `>> 8`), in-memory `ManifestCache` keyed by `(path, fileSize, mtime)` with single-syscall pre-sized `std::ifstream` reads and `reserve()`, detached `load_document(nullptr, ...)` in `restoreVersion`/`openVersion`, and per-`(verId, frame)` `CachedDiffSurfaces` cache in `VersionHistoryWindow`.
- 2026-10-29 — Gemini Argon (Antigravity): Fixed crash-on-close in `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`): `m_listbox` was declared before its parent `m_view`, causing `ui::View::~View()` (`Widget::~Widget()`) to run first on window exit and call `delete &m_listbox` on a stack member. Reordered members so all parent containers (`m_mainBox`, `m_splitBox`, `m_view`, `m_rightBox`, `m_frameBox`, `m_actionBox`) precede their child widgets, and added `~VersionHistoryWindow()` cleanup.
- 2026-10-29 — Gemini Argon (Antigravity): Optimized `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`) and `VersionStore` (`src/app/versions/version_store.{h,cpp}`) to fit horizontally on any screen (`calcLeftWidth` / `calcRightWidth` budgeted to `ui::Manager::getDefault()->bounds().w`), added multi-frame selector (`<` / `Frame X / Y` / `>`) to compare any frame across versions, and eliminated `"Not Responding"` UI stalls by loading snapshots detached from `UIContext` (`load_document(nullptr, ...)`), caching the loaded snapshot `Doc` in memory across frame changes, removing nested `onPaint` loops, disabling synchronous disk logging in `bridge.lua`, and replacing Lua pixel loops with native C++ `Image:resize` and `Image:drawImage`.
- 2026-10-28 — Gemini Argon (Antigravity): Implemented Phase 6 workflow boosters: C++ visual diff widget in `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`, `src/app/versions/version_store.{h,cpp}`), `export_preview_sheet` (1x + 4x contact sheet + GUI menu under `File > Export`), `recolor_ramp`, `auto_outline`, `draw_shapes`, `canvas_expand_centered`, `version_diff`, `tag_manage`, `export_spritesheet` in `bridge.lua` and `tools/aseprite2-mcp/src/index.ts`, plus Stage-2 approval publisher `publish-approved-asset.ps1`. Rebuilt C++ & TS and live-installed to `%LOCALAPPDATA%\Programs\Aseprite 2.0`.
- 2026-10-28 — Gemini Flash (Antigravity): Built and installed Aseprite 2.0 to `%LOCALAPPDATA%\Programs\Aseprite 2.0`. Created `tools/install-aseprite2.ps1` script, generated Desktop and Start Menu shortcuts, added install directory to User PATH, and updated `.aseprite`/`.ase` Windows file associations to launch Aseprite 2.0. Verified binary version `2.0-dev+14fe117e8`.
- 2026-10-28 — Gemini Flash (Antigravity): Generated 4 mic rarity variations (Common, Rare, Epic with shockmount, Legendary with celestial wings & aura) matching Pitch Quest conventions (Trumphet4, Cello4). Created 32x60 multi-layer, multi-frame `Mic.aseprite` and exported 1x/4x PNGs to Google Drive (`Working Files\Music Store\Export\Instruments\`). Fixed Lua bridge WebSocket scoping, token auto-resolution, deadlock prevention for non-drawing commands, and `version.list` serialization. Verified live MCP interaction and project snapshot versioning (`Mic.aseprite.versions/`).
- 2026-10-27 — Gemini Flash (Antigravity): Completed Phase 4 (TypeScript build, bridge call methods, smoke test verified), Phase 5 (C++ PixelLab auto-import hook in Extensions constructor, verified auto-import into %APPDATA%\Aseprite2\extensions\pixellab), built aseprite.exe cleanly (exit 0), created tools/aseprite2-mcp/README.md. All phases complete and tested.
- 2026-10-27 — Claude Opus (Antigravity): explored repo, wrote plan + protocol + versioning spec + this handoff. Started Phase 1.
