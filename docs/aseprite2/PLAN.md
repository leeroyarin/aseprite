# Aseprite 2.0 — Plan

Status of each phase lives in [HANDOFF.md](HANDOFF.md). This file = design. Change design here, not in HANDOFF.

## Goals
1. MCP server so AI agents (Antigravity, Claude Desktop/Code, Cursor, …) drive a **running** Aseprite 2.0.
2. PixelLab Aseprite extension (user-owned, proprietary) auto-installed into 2.0 and reachable by agents.
3. Save multiple versions of same project file; browse/restore/open/delete them.
4. Rebrand as "Aseprite 2.0" without clashing with a stock Aseprite install.

## Decisions (locked unless user says otherwise)
| Topic | Decision |
|---|---|
| PixelLab | Use user's own `.aseprite-extension`; auto-install at first launch; expose its commands through MCP. Never committed. |
| Versioning | Native C++ (`src/app/versions/`), commands `SaveVersion`, `VersionHistory`, `RestoreVersion`, `OpenVersion`, `DeleteVersion`. Storage `<dir>/<name>.versions/` (see VERSIONING.md). |
| MCP server | Node.js + TypeScript, `@modelcontextprotocol/sdk`, stdio transport. Location `tools/aseprite2-mcp/`. |
| Transport to app | Lua `WebSocket` in Aseprite is client-only ⇒ MCP server hosts WS server on `127.0.0.1:8765`; bundled Lua extension connects in, auto-reconnects. Token auth. |
| Branding | Name "Aseprite 2.0", version "2.0" (`2.0-dev` from git builds), exe stays `aseprite.exe`, user config dir `%APPDATA%\Aseprite2` / `~/.config/aseprite2` / `~/Library/Application Support/Aseprite 2.0`. |
| Port | 8765 (env `ASEPRITE2_MCP_PORT` overrides on server; pref overrides on bridge). |
| `lua.exec` | Exists, **disabled by default**; user enables in Help > AI Bridge. |
| Restore | Auto-snapshot current state as a version (note "Auto-backup before restore"), then replace current document content from the chosen version. |

## Architecture

```
AI agent ──MCP stdio──> tools/aseprite2-mcp (Node/TS)
                              │  WS server 127.0.0.1:8765, token
                              ▼
            data/extensions/aseprite2-bridge (Lua, bundled, trusted)
                              │  Aseprite Lua API + app.command.*
                              ▼
                 Aseprite 2.0 C++ core  ── VersionStore ──> <name>.versions/
                              │
                              └── PixelLab extension commands (plugin:newCommand)
```

## Phases

### Phase 1 — Branding
- `src/ver/CMakeLists.txt`: base version `2.0`; git builds → `2.0-dev`.
- `src/ver/info.c`: `PACKAGE "Aseprite 2.0"`.
- `src/main/win/resources_win32.rc`: FileDescription / ProductName.
- `src/app/resource_finder.cpp`: user dir names (see table).
- Note `app.cpp` CLI-only warning compares exe title to app name (only non-Skia builds) — harmless.
- Note `ase_format.cpp:166` inspects version chars for "1.1" — fine with "2.0".
- Scripts check `app.version`; 2.0 > 1.3 so extensions (PixelLab) requiring ≥1.3 keep working.

### Phase 2 — Versioning (C++)
- `src/app/versions/version_store.{h,cpp}`: list/save/restore/delete; manifest JSON via bundled `json11` (already in third_party, used by app).
- Save path: write snapshot with existing file-op machinery as a *copy* (doc filename + dirty flag unchanged). Thumbnail PNG 128px max.
- Commands in `src/app/commands/cmd_*_version*.cpp`, register in `src/app/commands/commands_list.h`, add to `src/app/CMakeLists.txt`.
- UI: `data/widgets/version_history.xml` (or code-built window) + strings in `data/strings/en.ini` + menu entries in `data/gui.xml` File menu, key `Ctrl+Alt+S` for SaveVersion (verify no conflict in gui.xml `<keyboard>`).
- Params so scripts/MCP can call: `SaveVersion{note, author, ui=false}`, `RestoreVersion{id}`, `OpenVersion{id}`, `DeleteVersion{id}`.
- Unsaved (no file) doc → SaveVersion asks Save As first (UI) / returns error (no UI).

### Phase 3 — Lua bridge extension
- `data/extensions/aseprite2-bridge/package.json` + `bridge.lua` (+ modules). Installed with other bundled extensions (check `data/CMakeLists.txt` / install rules copy whole `data/extensions`).
- Implements PROTOCOL.md. Every mutating handler runs inside `app.transaction("AI: <method>", fn)`.
- Menu Help > "AI Bridge…" dialog: status, connect/disconnect, port, allow lua.exec.
- C++: `src/app/script/security.cpp` — treat scripts under `<data>/extensions/aseprite2-bridge/` as trusted (skip ask-access dialog) so WS + token-file I/O need no prompt.

### Phase 4 — MCP server (Node/TS)
- `tools/aseprite2-mcp/` : `package.json`, `tsconfig.json`, `src/index.ts` (MCP), `src/bridge.ts` (WS server + request/response correlation, timeouts), `src/tools/*.ts`, `README.md` with client configs.
- One MCP tool per PROTOCOL method family (names like `sprite_info`, `draw_pixels`, `version_save`). `cel_get_image` returns MCP `image` content.
- Extra server-side tools: `status` (connected?), `launch_aseprite` (spawn exe from `ASEPRITE2_EXE` or repo `build/bin/aseprite.exe`), `import_image` (file/URL/base64 → server reads bytes → `cel.put_image`).
- Resources: `aseprite://sprite/active` (JSON info), `aseprite://versions/{path}`.

### Phase 5 — PixelLab
- C++ first-run hook (after extensions load, GUI only): if no extension named `pixellab` installed, try in order:
  1. pref `aseprite2.pixellab_path` (file or folder)
  2. `%APPDATA%\Aseprite\extensions\pixellab` (stock Aseprite install) — copy folder
  3. `%USERPROFILE%\Downloads\PixelLab.aseprite-extension` — `Extensions::installCompressedExtension`
  Ask user once (Alert) before installing; remember answer in pref.
- Bridge methods `pixellab.status`, `pixellab.list_commands`, `pixellab.run`. Commands discovered from extension `plugin-menu.lua` registrations (bridge records commands whose plugin is `pixellab`; fallback: static list in bridge).
- Limitation: PixelLab tools are dialog-driven; agent can open them, human completes params. Headless gen → PixelLab MCP + `import_image`.

### Phase 6 — Workflow Boosters (Pitch Quest & Pixel Art Pipeline)
- **Visual Version Diff (C++ + MCP)**: `VersionStore::computeVisualDiff` and `VersionDiffWidget` inside `VersionHistoryWindow` (`src/app/commands/cmd_version.cpp`) showing side-by-side Snapshot vs Current vs Pixel Diff (emerald = added, crimson = removed, amber = modified) + `version_diff` MCP tool.
- **Native Preview Sheet Exporter (1x + 4x)**: `sprite.export_preview_sheet` in `bridge.lua`, MCP tool `export_preview_sheet`, and in-app menu `File > Export > Export Preview Sheet (1x + 4x)...`.
- **High-Level Pixel Art Primitives**: `recolor_ramp` (rarity palette swapper), `auto_outline` (1px outer outline/aura), `draw_shapes` (line, rect, fill_rect, ellipse, fill_ellipse, flood_fill), `canvas_expand_centered`.
- **Game Engine Sprite Sheet & Animation Tag Tools**: `tag_manage` and `export_spritesheet` MCP tools.
- **System Installer**: `tools/install-aseprite2.ps1` with live hot-swap support for `%LOCALAPPDATA%\Programs\Aseprite 2.0`, Start Menu & Desktop shortcuts, User PATH, and `.aseprite`/`.ase` file associations.

### Verification
- `cmake --build build` after each C++ phase.
- Run `build/bin/aseprite.exe`: title "Aseprite 2.0 v2.0…", `%APPDATA%\Aseprite2` created.
- Versioning manual test (3 versions, history, restore, open copy, delete).
- MCP: `npx @modelcontextprotocol/inspector node tools/aseprite2-mcp/dist/index.js`; smoke script `tools/aseprite2-mcp/scripts/smoke.mjs`.
