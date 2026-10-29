import { WebSocket } from "ws";
import * as fs from "fs";
import * as path from "path";

const tokenPath = path.join(process.env.APPDATA, "Aseprite2", "mcp", "token");
let token = "aseprite2_default_token";
if (fs.existsSync(tokenPath)) {
  token = fs.readFileSync(tokenPath, "utf-8").trim();
}

console.log("[test] Connecting to Aseprite 2.0 WebSocket server on 8765...");
const ws = new WebSocket("ws://127.0.0.1:8765");

ws.on("open", () => {
  console.log("[test] Connected! Authenticating...");
  ws.send(JSON.stringify({
    type: "hello",
    protocol: 1,
    token: token,
    app: "test-client",
    version: "1.0",
    bridge: "1.0",
    capabilities: []
  }));
});

ws.on("message", (data) => {
  const msg = JSON.parse(data.toString());
  console.log("[test] Message:", msg.type, JSON.stringify(msg));

  if (msg.type === "welcome") {
    console.log("[test] Welcome received! Requesting sprite.info...");
    ws.send(JSON.stringify({
      type: "request",
      id: "req_info",
      method: "sprite.info",
      params: {}
    }));
  } else if (msg.type === "response" && msg.id === "req_info") {
    console.log("[test] Sprite Info Result:", JSON.stringify(msg.result, null, 2));
    ws.close();
    process.exit(0);
  }
});

setTimeout(() => {
  console.log("[test] Timeout");
  ws.close();
  process.exit(1);
}, 4000);
