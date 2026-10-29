// Smoke test for Aseprite 2.0 MCP & Bridge
import { AsepriteBridge } from "../dist/bridge.js";
import { WebSocket } from "ws";
import * as assert from "assert";

console.log("[smoke] Starting Aseprite 2.0 MCP bridge test...");

const bridge = new AsepriteBridge(8765);
bridge.start();

const token = bridge.getToken();
console.log(`[smoke] Bridge active on port 8765, token: ${token.substring(0, 8)}...`);

// Connect test client simulating in-app Lua extension
const ws = new WebSocket("ws://127.0.0.1:8765");

let receivedWelcome = false;

ws.on("open", () => {
  console.log("[smoke] Client connected, sending hello handshake...");
  ws.send(JSON.stringify({
    type: "hello",
    protocol: 1,
    token: token,
    app: "Aseprite 2.0",
    version: "2.0-dev",
    bridge: "1.0.0",
    capabilities: ["versions", "pixellab", "lua.exec"]
  }));
});

ws.on("message", (data) => {
  const msg = JSON.parse(data.toString());
  console.log("[smoke] Received from bridge:", msg.type);

  if (msg.type === "welcome") {
    receivedWelcome = true;
    assert.strictEqual(bridge.isConnected(), true, "Bridge should report connected");
    console.log("[smoke] Handshake verified! Testing request dispatch...");

    // Send a request through bridge
    bridge.call("sprite.info", {}).then((res) => {
      console.log("[smoke] bridge.call resolved:", JSON.stringify(res));
    });

    // Respond as Lua extension
    // We expect bridge to send a 'request' frame
  } else if (msg.type === "request") {
    console.log(`[smoke] Mocking response for method: ${msg.method} (id=${msg.id})`);
    ws.send(JSON.stringify({
      type: "response",
      id: msg.id,
      ok: true,
      result: {
        width: 64,
        height: 64,
        colorMode: "rgb",
        filename: "test.aseprite",
        layers: [{ name: "Layer 1", visible: true, opacity: 255 }]
      }
    }));
  }
});

// Run verification with timeout
setTimeout(async () => {
  try {
    assert.strictEqual(receivedWelcome, true, "Handshake failed");
    console.log("[smoke] All bridge protocol checks passed successfully!");
    ws.close();
    bridge.stop();
    process.exit(0);
  } catch (err) {
    console.error("[smoke] Test failed:", err);
    ws.close();
    bridge.stop();
    process.exit(1);
  }
}, 1500);
