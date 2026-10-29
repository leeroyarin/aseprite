// Aseprite 2.0 MCP Bridge - WebSocket Server
import { WebSocketServer, WebSocket } from "ws";
import * as fs from "fs";
import * as path from "path";
import * as os from "os";
import * as crypto from "crypto";
export class AsepriteBridge {
    wss = null;
    activeWs = null;
    pendingRequests = new Map();
    reqSeq = 0;
    port;
    capabilities = [];
    constructor(port = 8765) {
        this.port = parseInt(process.env.ASEPRITE2_MCP_PORT || String(port), 10);
    }
    getToken() {
        if (process.env.ASEPRITE2_MCP_TOKEN) {
            return process.env.ASEPRITE2_MCP_TOKEN;
        }
        const appData = process.env.APPDATA || (process.platform === "darwin"
            ? path.join(os.homedir(), "Library", "Application Support")
            : path.join(os.homedir(), ".config"));
        const userDir = process.env.ASEPRITE_USER_FOLDER || path.join(appData, "Aseprite2");
        const mcpDir = path.join(userDir, "mcp");
        const tokenPath = path.join(mcpDir, "token");
        try {
            if (fs.existsSync(tokenPath)) {
                const t = fs.readFileSync(tokenPath, "utf-8").trim();
                if (t.length > 0)
                    return t;
            }
            fs.mkdirSync(mcpDir, { recursive: true });
            const newToken = crypto.randomBytes(16).toString("hex");
            fs.writeFileSync(tokenPath, newToken, "utf-8");
            return newToken;
        }
        catch {
            return "aseprite2_default_token";
        }
    }
    start() {
        if (this.wss)
            return;
        this.wss = new WebSocketServer({ host: "127.0.0.1", port: this.port });
        this.wss.on("connection", (ws) => {
            let authenticated = false;
            ws.on("message", (raw) => {
                try {
                    const msg = JSON.parse(raw.toString("utf-8"));
                    if (msg.type === "hello") {
                        const expectedToken = this.getToken();
                        if (msg.token === expectedToken) {
                            authenticated = true;
                            if (this.activeWs && this.activeWs !== ws) {
                                this.activeWs.close();
                            }
                            this.activeWs = ws;
                            this.capabilities = msg.capabilities || [];
                            ws.send(JSON.stringify({
                                type: "welcome",
                                protocol: 1,
                                server: "aseprite2-mcp/1.0.0"
                            }));
                        }
                        else {
                            ws.send(JSON.stringify({ type: "reject", reason: "bad_token" }));
                            ws.close();
                        }
                        return;
                    }
                    if (!authenticated) {
                        ws.close();
                        return;
                    }
                    if (msg.type === "pong") {
                        return;
                    }
                    if (msg.type === "response") {
                        const pending = this.pendingRequests.get(msg.id);
                        if (pending) {
                            clearTimeout(pending.timer);
                            this.pendingRequests.delete(msg.id);
                            if (msg.ok) {
                                pending.resolve(msg.result);
                            }
                            else {
                                pending.reject(new Error(msg.error?.message || "Command failed"));
                            }
                        }
                    }
                }
                catch (e) {
                    // ignore malformed frame
                }
            });
            ws.on("close", () => {
                if (this.activeWs === ws) {
                    this.activeWs = null;
                }
            });
        });
    }
    isConnected() {
        return this.activeWs !== null && this.activeWs.readyState === WebSocket.OPEN;
    }
    getConnectedCapabilities() {
        return this.capabilities;
    }
    async call(method, params = {}, timeoutMs = 30000) {
        if (!this.isConnected()) {
            throw new Error("Aseprite 2.0 is not connected. Make sure Aseprite 2.0 is running.");
        }
        const id = `req_${++this.reqSeq}_${Date.now()}`;
        const payload = JSON.stringify({
            type: "request",
            id,
            method,
            params
        });
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pendingRequests.delete(id);
                reject(new Error(`Request timed out after ${timeoutMs}ms: ${method}`));
            }, timeoutMs);
            this.pendingRequests.set(id, { resolve, reject, timer });
            this.activeWs.send(payload);
        });
    }
    stop() {
        if (this.wss) {
            this.wss.close();
            this.wss = null;
        }
        this.activeWs = null;
    }
}
