# Aseprite 2.0 Bridge Protocol (v1)

Contract between **MCP server** (`tools/aseprite2-mcp`, WS *server*) and **Lua bridge** (`data/extensions/aseprite2-bridge`, WS *client*). Both sides MUST follow this file. Bump `protocol` number on breaking change.

## Transport
- WebSocket, text frames, UTF-8 JSON, one message per frame.
- URL: `ws://127.0.0.1:<port>/` — port default `8765`.
- Server binds `127.0.0.1` only. One bridge connection at a time; new valid hello replaces old.

## Token
- File `<userdir>/mcp/token` (plain text, single line, 32+ hex chars).
  - `<userdir>`: `$ASEPRITE_USER_FOLDER` if set, else Windows `%APPDATA%\Aseprite2`, macOS `~/Library/Application Support/Aseprite 2.0`, Linux `${XDG_CONFIG_HOME:-~/.config}/aseprite2`.
  - Lua: `app.fs.userConfigPath` gives `<userdir>`.
- Whoever needs it first and finds it missing creates it. Server re-reads file on every hello (so order of startup does not matter).
- Env `ASEPRITE2_MCP_TOKEN` on server overrides file.

## Messages

### hello (bridge → server, first message)
```json
{"type":"hello","protocol":1,"token":"<hex>","app":"Aseprite 2.0","version":"2.0","bridge":"1.0.0","capabilities":["versions","pixellab","lua.exec"]}
```
Server replies `{"type":"welcome","protocol":1,"server":"aseprite2-mcp/1.0.0"}` or `{"type":"reject","reason":"bad_token"|"protocol"}` then closes.
`capabilities` lists only enabled features (`lua.exec` only if user allowed it; `pixellab` only if extension installed; `versions` if C++ commands exist).

### request (server → bridge)
```json
{"type":"request","id":"r42","method":"sprite.info","params":{}}
```

### response (bridge → server)
```json
{"type":"response","id":"r42","ok":true,"result":{}}
{"type":"response","id":"r42","ok":false,"error":{"code":"not_found","message":"Layer 'foo' not found"}}
```
Error codes: `bad_params`, `not_found`, `no_sprite`, `disabled`, `unsupported`, `failed`, `timeout` (server-generated).
Server timeout default 30 s (`pixellab.run` and `sprite.open` 120 s).

### event (bridge → server, optional, unsolicited)
```json
{"type":"event","name":"active_sprite_changed","data":{"sprite":"C:/art/hero.aseprite"}}
```
Names: `active_sprite_changed`, `sprite_saved`, `version_saved`. Server may ignore.

### ping
Either side may send `{"type":"ping"}`; peer answers `{"type":"pong"}`.

## Common param conventions
- `sprite` (optional): absolute filename, or `"#<n>"` index into `app.sprites` (0-based), or omitted = active sprite. Error `no_sprite` if none.
- `layer`: name (first match, depth-first, groups searched) or `"#<n>"` stack index from `sprite.layers` flattened (0 = bottom). Omitted = active layer.
- `frame`: **1-based** frame number (matches Aseprite UI/Lua). Omitted = active frame.
- `color`: `"#RRGGBB"` / `"#RRGGBBAA"` string, or palette index integer (indexed sprites).
- Images: `{"png_base64": "..."}`; bridge decodes via `Image{fromFile=}` using temp file in `app.fs.tempPath`.
- Coordinates: sprite pixels, origin top-left.

## Methods

### app
| method | params | result |
|---|---|---|
| `app.status` | – | `{version, protocol, activeSprite, sprites:[{filename,index,width,height,frames,layers,modified}], capabilities}` |
| `app.list_commands` | `{filter?}` | `{commands:[string]}` (Aseprite command ids usable with app.command) |
| `app.run_command` | `{name, params?:object}` | `{ok:true}` — runs `app.command[name](params)` |
| `app.undo` / `app.redo` | `{steps?=1}` | `{ok:true}` |

### sprite
| method | params | result |
|---|---|---|
| `sprite.list` | – | same array as `app.status.sprites` |
| `sprite.info` | `{sprite?}` | `{filename,width,height,colorMode,frames:[{n,duration}],layers:[{index,name,visible,opacity,blendMode,isGroup,parent}],tags:[{name,from,to,aniDir}],palette:{size}}` |
| `sprite.new` | `{width,height,colorMode?="rgb"|"indexed"|"gray",filename?}` | `{filename,index}` |
| `sprite.open` | `{path}` | `{filename,index}` |
| `sprite.save` | `{sprite?, path?}` | `{filename}` (path ⇒ Save As) |
| `sprite.export` | `{sprite?, path, scale?=1, frame?}` | `{path}` — copy save (png/gif/…) without changing doc filename |
| `sprite.close` | `{sprite?, force?=false}` | `{ok}` |
| `sprite.set_active` | `{sprite}` | `{ok}` |
| `sprite.resize` | `{sprite?, width, height, method?="nearest"}` | `{ok}` |
| `sprite.canvas_size` | `{sprite?, left?,top?,right?,bottom?}` | `{ok}` |
| `sprite.canvas_expand_centered` | `{sprite?, width, height}` | `{ok, oldWidth, oldHeight, width, height, offsetX, offsetY}` |
| `sprite.export_preview_sheet` | `{sprite?, path?, scale?=4, mode?="frames"\|"layers", include1x?=true, exportIndividual?=false, columns?=4, returnBase64?=false}` | `{ok, path, width, height, scale, itemCount, individuals, png_base64}` |
| `sprite.export_spritesheet` | `{sprite?, texturePath, dataPath?, type?="rows"\|"columns"\|"packed", trim?=false, padding?=0}` | `{ok, texturePath, dataPath}` |

### layer / frame / tag
| method | params | result |
|---|---|---|
| `layer.add` | `{sprite?, name?, group?:bool, parent?}` | `{index,name}` |
| `layer.delete` | `{sprite?, layer}` | `{ok}` |
| `layer.set` | `{sprite?, layer, name?, visible?, opacity?, blendMode?, locked?}` | `{ok}` |
| `layer.set_active` | `{sprite?, layer}` | `{ok}` |
| `frame.add` | `{sprite?, after?, copy?=false}` | `{n}` |
| `frame.delete` | `{sprite?, frame}` | `{ok}` |
| `frame.set` | `{sprite?, frame, duration}` (seconds) | `{ok}` |
| `frame.set_active` | `{sprite?, frame}` | `{ok}` |
| `tag.manage` | `{sprite?, action="add"\|"delete"\|"list", name?, from?, to?, aniDir?="forward"\|"reverse"\|"pingpong"}` | `{ok, tags?}` |

### cel / drawing
| method | params | result |
|---|---|---|
| `cel.get_image` | `{sprite?, layer?, frame?, flatten?=false, scale?=1}` | `{png_base64,width,height,x,y}` — `flatten` = whole visible sprite at frame |
| `cel.put_image` | `{sprite?, layer?, frame?, png_base64, x?=0, y?=0, mode?="replace"|"blend", newLayer?:string}` | `{ok,layer}` |
| `cel.clear` | `{sprite?, layer?, frame?}` | `{ok}` |
| `draw.pixels` | `{…target, pixels:[{x,y,color}]}` | `{count}` |
| `draw.recolor_ramp` | `{…target, allFrames?=false, mapping:[{from,to,tolerance?=0}]}` | `{ok, replacedPixels}` |
| `draw.auto_outline` | `{…target, color?="#1A1A22", corners?=false}` | `{ok, addedPixels}` |
| `draw.shapes` | `{…target, shapes:[{type:"line"\|"rect"\|"fill_rect"\|"ellipse"\|"fill_ellipse"\|"flood_fill", x?,y?,w?,h?,x1?,y1?,x2?,y2?,color}]}` | `{ok, drawnShapes}` |
`…target` = `sprite?, layer?, frame?`.

### palette / selection
| method | params | result |
|---|---|---|
| `palette.get` | `{sprite?}` | `{colors:["#RRGGBBAA",…]}` |
| `palette.set` | `{sprite?, colors:[…]}` | `{ok}` |
| `selection.set` | `{sprite?, x,y,w,h, mode?="replace"|"add"|"subtract"}` | `{ok}` |
| `selection.clear` | `{sprite?}` | `{ok}` |

### version (needs capability `versions`)
| method | params | result |
|---|---|---|
| `version.save` | `{sprite?, note?, author?="agent"}` | `{id, file}` |
| `version.list` | `{sprite?}` | `{versions:[manifest entry]}` (see VERSIONING.md) |
| `version.restore` | `{sprite?, id}` | `{ok, backupId}` |
| `version.open` | `{sprite?, id}` | `{filename,index}` (opens copy in new tab) |
| `version.delete` | `{sprite?, id}` | `{ok}` |
| `version.diff` | `{sprite?, id, frame?=1}` | `{id, width, height, totalPixels, changedPixels, addedPixels, removedPixels, modifiedPixels, diff_png_base64}` |
Server may also read `manifest.json` directly for `version.list` when bridge offline.

### pixellab (needs capability `pixellab`)
| method | params | result |
|---|---|---|
| `pixellab.status` | – | `{installed, version, commands:n}` |
| `pixellab.list_commands` | – | `{commands:[{id,title,group}]}` |
| `pixellab.run` | `{command}` | `{ok}` — opens PixelLab dialog/tool; human may need to finish |

### lua (needs capability `lua.exec`)
| method | params | result |
|---|---|---|
| `lua.exec` | `{code}` | `{result: <json-serializable return value>, output: "<captured print output>"}` |

### easymetric & 2.5D surface maps (needs capabilities `easymetric`, `normals_depth`)
| method | params | result |
|---|---|---|
| `surface.generate_normals_depth` | `{sprite?, layer?, frame?=1, bevelRadius?=6.0, reliefWeight?=0.45, normalStrength?=3.2, generateLitPreview?=false}` | `{ok, width, height, depthLayer, normalLayer, litLayer?}` |
| `easymetric.create_template` | `{sprite?, frame?=1, boxSize?=24, boxHeight?=28}` | `{ok, boxSize, boxHeight, layers}` |
| `easymetric.project_4surfaces` | `{sprite?, frame?=1, topLayer?, leftLayer?, rightLayer?, frontLayer?, boxSize?=24, boxHeight?=28, bumpStrength?=2.0, generateNormal?=true, generateDepth?=true, autoOutline?=true, ditherSides?=true, hideSourceLayers?=false}` | `{ok, width, height, boxSize, boxHeight, diffuseLayer, normalLayer?, depthLayer?}` |
