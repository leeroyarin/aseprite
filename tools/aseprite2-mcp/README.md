# Aseprite 2.0 MCP Server

Model Context Protocol (MCP) server for **Aseprite 2.0**.
Allows AI agents (Antigravity, Claude, Cursor, etc.) to inspect and control a live Aseprite 2.0 instance and leverage PixelLab.

## Architecture

```
AI Agent (MCP client)
      │
      │  stdio (MCP JSON-RPC)
      ▼
tools/aseprite2-mcp (Node.js/TypeScript MCP Server)
      │
      │  WebSocket (127.0.0.1:8765, token-authenticated)
      ▼
Aseprite 2.0 (bundled Lua extension: aseprite2-bridge)
      │
      ├─► C++ Core & VersionStore (<file>.versions/)
      └─► PixelLab extension
```

## Prerequisites

- Node.js 18+
- Built Aseprite 2.0 binary (`build/bin/aseprite.exe`)

## Build

```bash
cd tools/aseprite2-mcp
npm install
npm run build
```

## Running the Server

```bash
node tools/aseprite2-mcp/dist/index.js
```

Or run the smoke test:
```bash
node tools/aseprite2-mcp/scripts/smoke.mjs
```

## Client Configurations

### 1. Antigravity IDE
Add to your workspace or global MCP configuration:

```json
{
  "mcpServers": {
    "aseprite2": {
      "command": "node",
      "args": [
        "c:/Users/leero/Source/Repos/aseprite2/tools/aseprite2-mcp/dist/index.js"
      ],
      "env": {
        "ASEPRITE2_EXE": "c:/Users/leero/Source/Repos/aseprite2/build/bin/aseprite.exe"
      }
    }
  }
}
```

### 2. Claude Desktop (`%APPDATA%\Claude\claude_desktop_config.json`)

```json
{
  "mcpServers": {
    "aseprite2": {
      "command": "node",
      "args": [
        "C:\\Users\\leero\\Source\\Repos\\aseprite2\\tools\\aseprite2-mcp\\dist\\index.js"
      ],
      "env": {
        "ASEPRITE2_EXE": "C:\\Users\\leero\\Source\\Repos\\aseprite2\\build\\bin\\aseprite.exe"
      }
    }
  }
}
```

### 3. Cursor (`.cursor/mcp.json`)

```json
{
  "mcpServers": {
    "aseprite2": {
      "command": "node",
      "args": [
        "tools/aseprite2-mcp/dist/index.js"
      ]
    }
  }
}
```

## Available MCP Tools

### Application & Lifecycle
- `status`: Check if Aseprite 2.0 is connected via WebSocket and view active sprites.
- `launch_aseprite`: Launch Aseprite 2.0 GUI executable.
- `run_command`: Run any native Aseprite command by ID (e.g. `Undo`, `CanvasSize`).
- `exec_lua`: Run arbitrary Lua code (when enabled in Help > AI Bridge).

### Sprites & Canvas
- `sprite_info`: Get canvas dimensions, layers, frames, tags, color mode.
- `sprite_new`: Create a new canvas with specified dimensions.
- `sprite_open`: Open an existing sprite file.
- `sprite_save`: Save active sprite.
- `sprite_export`: Export sprite to PNG/GIF.
- `sprite_resize`: Resize canvas/sprite.
- `canvas_expand_centered`: Expand canvas dimensions while automatically centering existing cels.
- `export_preview_sheet`: Generate a Pitch Quest contact preview sheet (1x actual + 4x nearest-neighbor upscale on bordered checkered cards).
- `export_spritesheet`: Export game-engine ready sprite sheet PNG and optional JSON metadata array.
- `sprite_close`: Close active sprite.

### Layers, Frames & Tags
- `layer_add`: Add a new standard layer or layer group.
- `frame_add`: Add a new animation frame.
- `tag_manage`: Add, delete, or list animation frame tags (`Idle`, `Play`, `Glow`).

### Pixels, Drawing & Rarity Tools
- `draw_pixels`: Draw an array of `{x, y, color}` pixels onto active cel.
- `draw_shapes`: Draw batch geometric primitives (`line`, `rect`, `fill_rect`, `ellipse`, `fill_ellipse`, `flood_fill`) in one transaction.
- `recolor_ramp`: Swap color ramps across a layer/cel (fast Rare/Epic/Legendary tier generation from Common base).
- `auto_outline`: Generate 1px outer outline or glow aura around opaque pixels on a cel.
- `cel_get_image`: Extract cel image or flattened frame as base64 PNG.
- `cel_put_image`: Paste a base64 PNG into a cel.
- `import_image`: Load an external image file onto an active sprite layer.
- `palette_get`: Retrieve current color palette.
- `palette_set`: Update palette colors.

### Project Versioning & Visual Diff
- `version_save`: Save a named snapshot version of the current project (`<file>.versions/`).
- `version_list`: List all version snapshots for the current project.
- `version_diff`: Compare a saved version against current canvas; returns pixel diff stats and highlighted visual diff PNG.
- `version_restore`: Restore a previous version (auto-backups current state first).
- `version_open`: Open a previous version as a copy in a new tab.
- `version_delete`: Delete a version snapshot.

### PixelLab AI Integration
- `pixellab_status`: Verify if PixelLab extension is installed and active.
- `pixellab_run`: Trigger PixelLab generation dialogs.
