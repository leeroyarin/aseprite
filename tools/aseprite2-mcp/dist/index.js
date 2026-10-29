#!/usr/bin/env node
// Aseprite 2.0 MCP Server
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { CallToolRequestSchema, ListToolsRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import { AsepriteBridge } from "./bridge.js";
import { spawn } from "child_process";
import * as path from "path";
import * as fs from "fs";
const bridge = new AsepriteBridge();
bridge.start();
const server = new Server({
    name: "aseprite2-mcp",
    version: "1.0.0"
}, {
    capabilities: {
        tools: {}
    }
});
const TOOLS = [
    {
        name: "status",
        description: "Check if Aseprite 2.0 is connected via WebSocket, list active sprites, and view capabilities.",
        inputSchema: { type: "object", properties: {} }
    },
    {
        name: "launch_aseprite",
        description: "Launch the Aseprite 2.0 application executable if not already running.",
        inputSchema: {
            type: "object",
            properties: {
                file: { type: "string", description: "Optional file path to open on launch" }
            }
        }
    },
    {
        name: "sprite_info",
        description: "Get detailed information about the active sprite (dimensions, layers, frames, tags, color mode).",
        inputSchema: {
            type: "object",
            properties: {
                sprite: { type: "string", description: "Optional filename or #index (defaults to active sprite)" }
            }
        }
    },
    {
        name: "sprite_new",
        description: "Create a new sprite canvas in Aseprite 2.0.",
        inputSchema: {
            type: "object",
            properties: {
                width: { type: "number", description: "Canvas width in pixels (e.g. 32, 64)" },
                height: { type: "number", description: "Canvas height in pixels (e.g. 32, 64)" },
                colorMode: { type: "string", enum: ["rgb", "indexed", "gray"], description: "Color mode (default 'rgb')" },
                filename: { type: "string", description: "Optional initial filename" }
            },
            required: ["width", "height"]
        }
    },
    {
        name: "sprite_open",
        description: "Open an existing sprite file into Aseprite 2.0.",
        inputSchema: {
            type: "object",
            properties: {
                path: { type: "string", description: "Absolute path to file (.aseprite, .png, .gif, etc.)" }
            },
            required: ["path"]
        }
    },
    {
        name: "sprite_save",
        description: "Save the active sprite to disk.",
        inputSchema: {
            type: "object",
            properties: {
                path: { type: "string", description: "Optional destination path (if Save As)" }
            }
        }
    },
    {
        name: "sprite_export",
        description: "Export the active sprite to a flattened image or animation (e.g. .png, .gif).",
        inputSchema: {
            type: "object",
            properties: {
                path: { type: "string", description: "Target export path" }
            },
            required: ["path"]
        }
    },
    {
        name: "sprite_close",
        description: "Close the active sprite.",
        inputSchema: { type: "object", properties: {} }
    },
    {
        name: "sprite_resize",
        description: "Resize / resample the active sprite.",
        inputSchema: {
            type: "object",
            properties: {
                width: { type: "number" },
                height: { type: "number" }
            },
            required: ["width", "height"]
        }
    },
    {
        name: "layer_add",
        description: "Add a new layer or group to the active sprite.",
        inputSchema: {
            type: "object",
            properties: {
                name: { type: "string", description: "Layer name" },
                group: { type: "boolean", description: "Create a group folder layer" }
            }
        }
    },
    {
        name: "frame_add",
        description: "Add a new animation frame to the active sprite.",
        inputSchema: {
            type: "object",
            properties: {
                duration: { type: "number", description: "Frame duration in seconds (e.g. 0.1)" }
            }
        }
    },
    {
        name: "cel_get_image",
        description: "Get base64-encoded PNG image of the active or specified cel or flattened canvas frame.",
        inputSchema: {
            type: "object",
            properties: {
                flatten: { type: "boolean", description: "If true, renders whole flattened sprite at frame instead of single cel" },
                layer: { type: "string", description: "Layer name or #index" },
                frame: { type: "number", description: "Frame number (1-based)" }
            }
        }
    },
    {
        name: "cel_put_image",
        description: "Place a base64-encoded PNG image into a cel in the active sprite.",
        inputSchema: {
            type: "object",
            properties: {
                png_base64: { type: "string", description: "Base64-encoded PNG image" },
                layer: { type: "string", description: "Target layer name" },
                newLayer: { type: "string", description: "Optional new layer name to create for this image" },
                frame: { type: "number", description: "Frame number (1-based)" },
                x: { type: "number", description: "X coordinate (default 0)" },
                y: { type: "number", description: "Y coordinate (default 0)" }
            },
            required: ["png_base64"]
        }
    },
    {
        name: "import_image",
        description: "Import an image file from disk (e.g. generated by PixelLab AI or another tool) into the active sprite.",
        inputSchema: {
            type: "object",
            properties: {
                filePath: { type: "string", description: "Absolute file path to image (.png, .jpg, etc.)" },
                layerName: { type: "string", description: "Optional name of layer to place image onto" },
                x: { type: "number", description: "X position" },
                y: { type: "number", description: "Y position" }
            },
            required: ["filePath"]
        }
    },
    {
        name: "draw_pixels",
        description: "Draw pixel dots directly onto the active sprite cel.",
        inputSchema: {
            type: "object",
            properties: {
                pixels: {
                    type: "array",
                    items: {
                        type: "object",
                        properties: {
                            x: { type: "number" },
                            y: { type: "number" },
                            color: { type: "string", description: "Hex color (#RRGGBB or #RRGGBBAA)" }
                        },
                        required: ["x", "y", "color"]
                    }
                }
            },
            required: ["pixels"]
        }
    },
    {
        name: "palette_get",
        description: "Get the color palette of the active sprite.",
        inputSchema: { type: "object", properties: {} }
    },
    {
        name: "palette_set",
        description: "Set the color palette of the active sprite from an array of hex colors.",
        inputSchema: {
            type: "object",
            properties: {
                colors: { type: "array", items: { type: "string" } }
            },
            required: ["colors"]
        }
    },
    {
        name: "version_save",
        description: "Save a new project version snapshot of the current file with an optional note and author tag.",
        inputSchema: {
            type: "object",
            properties: {
                note: { type: "string", description: "Description or note about what changed in this version" },
                author: { type: "string", description: "Author name or 'agent' (default 'agent')" }
            }
        }
    },
    {
        name: "version_list",
        description: "List all saved project versions for the current file.",
        inputSchema: { type: "object", properties: {} }
    },
    {
        name: "version_restore",
        description: "Restore a previously saved version (automatically creates an auto-backup version first).",
        inputSchema: {
            type: "object",
            properties: {
                id: { type: "number", description: "Version ID to restore" }
            },
            required: ["id"]
        }
    },
    {
        name: "version_open",
        description: "Open a previously saved version as a copy in a new tab.",
        inputSchema: {
            type: "object",
            properties: {
                id: { type: "number", description: "Version ID to open" }
            },
            required: ["id"]
        }
    },
    {
        name: "version_delete",
        description: "Delete a saved version snapshot from the versions history.",
        inputSchema: {
            type: "object",
            properties: {
                id: { type: "number", description: "Version ID to delete" }
            },
            required: ["id"]
        }
    },
    {
        name: "pixellab_status",
        description: "Check if the official PixelLab extension is installed in Aseprite 2.0.",
        inputSchema: { type: "object", properties: {} }
    },
    {
        name: "pixellab_run",
        description: "Launch a PixelLab extension command inside Aseprite 2.0.",
        inputSchema: {
            type: "object",
            properties: {
                command: { type: "string", description: "PixelLab command name" }
            },
            required: ["command"]
        }
    },
    {
        name: "run_command",
        description: "Run any built-in Aseprite command by name.",
        inputSchema: {
            type: "object",
            properties: {
                name: { type: "string", description: "Command ID (e.g. 'Undo', 'CanvasSize', 'UndoHistory')" },
                params: { type: "object", description: "Optional command parameters" }
            },
            required: ["name"]
        }
    },
    {
        name: "exec_lua",
        description: "Execute arbitrary Lua code inside Aseprite 2.0 (if enabled in AI Bridge settings).",
        inputSchema: {
            type: "object",
            properties: {
                code: { type: "string", description: "Lua script source code" }
            },
            required: ["code"]
        }
    },
    {
        name: "export_preview_sheet",
        description: "Generate a Pitch Quest contact preview sheet showing all frames or layers at 1x actual size and nearest-neighbor upscaled (e.g. 4x) on a bordered checkered grid.",
        inputSchema: {
            type: "object",
            properties: {
                path: { type: "string", description: "Optional output PNG path (defaults to <sprite>_preview_sheet.png in Working Files)" },
                scale: { type: "number", description: "Nearest-neighbor upscale factor (default 4)" },
                mode: { type: "string", enum: ["frames", "layers"], description: "Render across animation frames or individual layers" },
                include1x: { type: "boolean", description: "Include 1x actual size sprite beside scaled version (default true)" },
                exportIndividual: { type: "boolean", description: "Also export individual 1x and scaled PNGs per item (default false)" },
                columns: { type: "number", description: "Max grid columns (default 4)" },
                returnBase64: { type: "boolean", description: "Return base64-encoded PNG in response" }
            }
        }
    },
    {
        name: "canvas_expand_centered",
        description: "Expand or resize the sprite canvas while automatically centering existing pixel content (prevents clipping on ornate wings/auras).",
        inputSchema: {
            type: "object",
            properties: {
                width: { type: "number", description: "New canvas width in pixels" },
                height: { type: "number", description: "New canvas height in pixels" }
            },
            required: ["width", "height"]
        }
    },
    {
        name: "recolor_ramp",
        description: "Swap one or more color ramps across a layer/cel (ideal for generating Rare, Epic, and Legendary rarity tiers from a Common base sprite).",
        inputSchema: {
            type: "object",
            properties: {
                layer: { type: "string", description: "Target layer name or #index (defaults to active layer)" },
                frame: { type: "number", description: "1-based frame number (defaults to active frame)" },
                allFrames: { type: "boolean", description: "Apply ramp swap across all frames on the layer (default false)" },
                mapping: {
                    type: "array",
                    description: "List of {from, to, tolerance} hex color replacements",
                    items: {
                        type: "object",
                        properties: {
                            from: { type: "string", description: "Source hex color (#RRGGBB)" },
                            to: { type: "string", description: "Target replacement hex color (#RRGGBB or #RRGGBBAA)" },
                            tolerance: { type: "number", description: "RGB Manhattan distance tolerance (default 0 for exact match)" }
                        },
                        required: ["from", "to"]
                    }
                }
            },
            required: ["mapping"]
        }
    },
    {
        name: "auto_outline",
        description: "Automatically generate a 1px outer outline or glow aura around opaque pixels on a cel.",
        inputSchema: {
            type: "object",
            properties: {
                layer: { type: "string", description: "Target layer name or #index" },
                frame: { type: "number", description: "1-based frame number" },
                color: { type: "string", description: "Outline hex color (default '#1A1A22')" },
                corners: { type: "boolean", description: "Include 8-way diagonal corners (default false for crisp 4-way pixel outline)" }
            }
        }
    },
    {
        name: "draw_shapes",
        description: "Draw batch geometric pixel art primitives (line, rect, fill_rect, ellipse, fill_ellipse, flood_fill) in a single undo transaction.",
        inputSchema: {
            type: "object",
            properties: {
                layer: { type: "string", description: "Target layer name or #index" },
                frame: { type: "number", description: "1-based frame number" },
                shapes: {
                    type: "array",
                    items: {
                        type: "object",
                        properties: {
                            type: { type: "string", enum: ["line", "rect", "fill_rect", "ellipse", "fill_ellipse", "flood_fill"] },
                            x: { type: "number" },
                            y: { type: "number" },
                            w: { type: "number" },
                            h: { type: "number" },
                            x1: { type: "number" },
                            y1: { type: "number" },
                            x2: { type: "number" },
                            y2: { type: "number" },
                            color: { type: "string", description: "Hex color (#RRGGBB or #RRGGBBAA)" }
                        },
                        required: ["type", "color"]
                    }
                }
            },
            required: ["shapes"]
        }
    },
    {
        name: "version_diff",
        description: "Compare a saved project version against the current sprite and return pixel change statistics plus a highlighted visual diff PNG.",
        inputSchema: {
            type: "object",
            properties: {
                id: { type: "number", description: "Saved version ID to compare against current canvas" },
                frame: { type: "number", description: "1-based frame number to compare (default 1)" }
            },
            required: ["id"]
        }
    },
    {
        name: "tag_manage",
        description: "Add, delete, or list animation frame tags (e.g. 'Idle', 'Play', 'Glow') on the active sprite.",
        inputSchema: {
            type: "object",
            properties: {
                action: { type: "string", enum: ["add", "delete", "list"], description: "Action to perform" },
                name: { type: "string", description: "Animation tag name" },
                from: { type: "number", description: "Start frame number (1-based)" },
                to: { type: "number", description: "End frame number (1-based)" },
                aniDir: { type: "string", enum: ["forward", "reverse", "pingpong"], description: "Playback direction" }
            },
            required: ["action"]
        }
    },
    {
        name: "export_spritesheet",
        description: "Export a game-engine ready sprite sheet PNG and optional JSON metadata array.",
        inputSchema: {
            type: "object",
            properties: {
                texturePath: { type: "string", description: "Output PNG sprite sheet file path" },
                dataPath: { type: "string", description: "Optional output JSON metadata file path" },
                type: { type: "string", enum: ["rows", "columns", "packed", "horizontal", "vertical"], description: "Sheet layout (default 'rows')" },
                trim: { type: "boolean", description: "Trim transparent borders (default false)" },
                padding: { type: "number", description: "Padding between frames in pixels (default 0)" }
            },
            required: ["texturePath"]
        }
    }
];
server.setRequestHandler(ListToolsRequestSchema, async () => {
    return { tools: TOOLS };
});
server.setRequestHandler(CallToolRequestSchema, async (request) => {
    const { name, arguments: args } = request.params;
    try {
        if (name === "status") {
            const connected = bridge.isConnected();
            if (!connected) {
                return {
                    content: [
                        {
                            type: "text",
                            text: JSON.stringify({
                                connected: false,
                                message: "Aseprite 2.0 is not currently connected. Launch Aseprite 2.0 to connect."
                            }, null, 2)
                        }
                    ]
                };
            }
            const st = await bridge.call("app.status");
            return { content: [{ type: "text", text: JSON.stringify(st, null, 2) }] };
        }
        if (name === "launch_aseprite") {
            const installedPath = process.env.LOCALAPPDATA
                ? path.join(process.env.LOCALAPPDATA, "Programs", "Aseprite 2.0", "aseprite.exe")
                : "";
            const devPath = path.resolve(process.cwd(), "build/bin/aseprite.exe");
            const exePath = process.env.ASEPRITE2_EXE ||
                (installedPath && fs.existsSync(installedPath) ? installedPath : devPath);
            const launchArgs = args?.file ? [String(args.file)] : [];
            const child = spawn(exePath, launchArgs, { detached: true, stdio: "ignore" });
            child.unref();
            return {
                content: [
                    {
                        type: "text",
                        text: `Launched Aseprite 2.0 from: ${exePath}`
                    }
                ]
            };
        }
        if (name === "import_image") {
            const filePath = String(args?.filePath);
            if (!fs.existsSync(filePath)) {
                throw new Error(`File does not exist: ${filePath}`);
            }
            const raw = fs.readFileSync(filePath);
            const b64 = raw.toString("base64");
            const res = await bridge.call("cel.put_image", {
                png_base64: b64,
                newLayer: args?.layerName || path.basename(filePath),
                x: args?.x || 0,
                y: args?.y || 0
            });
            return { content: [{ type: "text", text: JSON.stringify(res, null, 2) }] };
        }
        // Method map to bridge handlers
        const methodMap = {
            sprite_info: "sprite.info",
            sprite_new: "sprite.new",
            sprite_open: "sprite.open",
            sprite_save: "sprite.save",
            sprite_export: "sprite.export",
            sprite_close: "sprite.close",
            sprite_resize: "sprite.resize",
            export_preview_sheet: "sprite.export_preview_sheet",
            canvas_expand_centered: "sprite.canvas_expand_centered",
            export_spritesheet: "sprite.export_spritesheet",
            layer_add: "layer.add",
            frame_add: "frame.add",
            tag_manage: "tag.manage",
            cel_get_image: "cel.get_image",
            cel_put_image: "cel.put_image",
            draw_pixels: "draw.pixels",
            recolor_ramp: "draw.recolor_ramp",
            auto_outline: "draw.auto_outline",
            draw_shapes: "draw.shapes",
            palette_get: "palette.get",
            palette_set: "palette.set",
            version_save: "version.save",
            version_list: "version.list",
            version_restore: "version.restore",
            version_open: "version.open",
            version_delete: "version.delete",
            version_diff: "version.diff",
            pixellab_status: "pixellab.status",
            pixellab_run: "pixellab.run",
            run_command: "app.run_command",
            exec_lua: "lua.exec"
        };
        const bridgeMethod = methodMap[name];
        if (bridgeMethod) {
            const res = await bridge.call(bridgeMethod, args || {});
            return { content: [{ type: "text", text: JSON.stringify(res, null, 2) }] };
        }
        throw new Error(`Unknown tool: ${name}`);
    }
    catch (err) {
        return {
            isError: true,
            content: [{ type: "text", text: `Error executing ${name}: ${err.message}` }]
        };
    }
});
async function main() {
    const transport = new StdioServerTransport();
    await server.connect(transport);
}
main().catch((err) => {
    console.error("Fatal error starting Aseprite 2.0 MCP server:", err);
    process.exit(1);
});
