---
name: aseprite2-agentic
description: "Best practices, gotchas, and procedures for AI agent control of Aseprite 2.0 via MCP, Lua bridge, PixelLab integration, project versioning, and pixel art asset workflows."
---

# /aseprite2-agentic

Guide and operational reference for AI agents working with Aseprite 2.0, WebSocket MCP bridge, PixelLab extension, project versioning, and game sprite generation.

---

## 1. Core Architecture & Workflow

```
[ AI Agent / LLM ]
       │
       ▼ (MCP protocol - stdio / JSON-RPC)
[ Node MCP Server ] (tools/aseprite2-mcp, port 8765)
       │
       ▼ (WebSocket JSON protocol - PROTOCOL.md v1)
[ Lua AI Bridge ] (data/extensions/aseprite2-bridge/bridge.lua)
       │
       ▼ (In-App Lua API / C++ Commands)
[ Aseprite 2.0 Engine & Version Store ]
```

### Launch Procedure
1. Verify MCP background server is running on port 8765 (`Get-NetTCPConnection -LocalPort 8765`).
2. If down, launch: `Start-Process node -ArgumentList "tools\aseprite2-mcp\dist\index.js" -WorkingDirectory . -WindowStyle Hidden`
3. Launch Aseprite 2.0 GUI with target file via MCP tool `launch_aseprite` or PowerShell script `tools\start-aseprite2.ps1`.
4. Wait 1-2 seconds for GUI initialization and verify connection via MCP tool `status`.

---

## 2. Lua Bridge & Engine Gotchas (Critical)

### A. Closure Scoping in Lua WebSocket Client
- **Problem**: When passing `onreceive` callback inside `WebSocket{ onreceive = ... }`, a local variable defined on the same line (`local ws = WebSocket{...}`) is **nil** inside the callback.
- **Rule**: Always declare `local ws` before the constructor:
  ```lua
  local ws
  ws = WebSocket{
    url = url,
    onreceive = function(msgType, data, err)
      -- ws is now in scope!
      ws:sendText(...)
    end
  }
  ```

### B. Document Lock Deadlocks (`app.transaction`)
- **Problem**: `app.transaction()` acquires an exclusive undo transaction lock on `app.activeDocument`. Commands that save or export (`SaveFileCopyAs`, `SaveVersion`, `SaveFile`, `Sprite:saveAs`) also attempt to lock or wait for the active transaction, causing immediate deadlock and 30s timeout.
- **Rule**: Only wrap pixel/cel drawing operations (`draw.pixels`, `cel.put_image`, `palette.set`) in `app.transaction`. Document-level, versioning, status, and PixelLab operations must execute without `app.transaction`.

### C. JSON Userdata Serialization
- **Problem**: Aseprite's C++ `json.decode` returns a `JsonObj` userdata object. Passing this object directly inside a Lua table to `json.encode` yields `null` because `get_json_value()` does not unpack userdata.
- **Rule**: Unpack C++ `JsonObj` collections into plain Lua tables with native types (`string`, `number`, `boolean`) before sending them across the bridge.

### D. Silent Permissions & Whitelisting
- **Problem**: Stock Aseprite displays modal GUI security alerts for file/socket access (`script_access.xml`), halting non-interactive agent execution.
- **Rule**: `src/app/script/security.cpp` whitelists paths containing `aseprite2-bridge` and `pixellab`. Always place extension scripts inside those paths to run silently.

### E. CLI Batch Mode vs GUI Mode
- In batch mode (`-b`), `Commands::instance()` is not populated (UI only). Commands like `app.command.SaveVersion` or `app.command.SaveFileCopyAs` fail silently.
- For headless CLI exports, use native flags: `aseprite.exe -b input.aseprite --save-as output.png`.
- For interactive agent control, keep GUI open and dispatch via MCP.

---

## 3. PixelLab Extension Rules

1. **Security & Headless Guard**: PixelLab scripts often instantiate UI dialogs unconditionally. If running scripts headlessly, guard dialog creation:
   ```lua
   if not app.isGui then return nil end
   ```
2. **Proprietary Files**: Never commit PixelLab extension binaries, scripts, or license tokens into git (`AGENTS.md`). The engine auto-imports it from the user's local directory into `%APPDATA%\Aseprite2\extensions\pixellab`.
3. **Execution**: Trigger commands via MCP tool `pixellab_run` or in-app command registry.

---

## 4. Project Versioning System

- **Storage Location**: `<Project>.aseprite.versions/` beside the active project file.
  - `manifest.json`: Registry of version IDs, timestamps, notes, author, and byte sizes.
  - `vXXXX_YYYYMMDD-HHMMSS.aseprite`: Full project snapshot.
  - `vXXXX_YYYYMMDD-HHMMSS.png`: 128px nearest-neighbor thumbnail.
- **Control**:
  - Hotkey: `Ctrl+Alt+S`
  - GUI: `File > Save Version...`, `File > Version History...` (Restore, Open Copy, Delete).
  - MCP Tools: `version_save`, `version_list`, `version_restore`, `version_open`, `version_delete`.
- **Precondition**: The document must be saved to a file (`doc->isAssociatedToFile()`). Unsaved documents trigger a "Save As" prompt.

---

## 5. Pixel Art Rarity & Asset Guidelines

When generating rarity tiers for game assets (e.g., Pitch Quest music store assets):

| Rarity | Theme | Color Palette | Key Details |
|---|---|---|---|
| **Common (1)** | Studio / Standard | Charcoal, gunmetal `#25242D`, silver mesh `#AEA19C` | Clean base reference, standard stand. |
| **Rare (2)** | Chrome / Platinum | Sleek platinum `#CBD5E1`, glowing electric cyan `#00E5FF` | High-tech LED indicator ring, neon glow. |
| **Epic (3)** | Vintage Royal Gold | Gold `#FBBF24`, royal amethyst purple `#9333EA`, `#581C87` | Vintage suspension shockmount ring, velvet grip. |
| **Legendary (4)** | Celestial Seraphim | Gold `#FEF08A`, amber bronze `#78350F`, ruby `#EF4444` | Sweeping angelic golden wings, ruby gem, halo sparkle aura. |

- **Canvas Sizing**: Expand canvas (e.g. 20x60 -> 32x60) with centering offset so ornate wings/brackets never clip while preserving vertical alignment.
- **Multi-Layer / Multi-Frame**: Build with 4 dedicated layers (`Common`, `Rare`, `Epic`, `Legendary`) and 4 frames (0.5s duration) for instant playback preview.
- **Upscaling**: Always use nearest-neighbor scaling (4x) for export previews to preserve crisp pixel grid.
