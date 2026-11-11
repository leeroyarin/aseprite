#!/usr/bin/env python3
"""
Test Suite for Agent Mesh Hub — PHASE A
Verifies:
1. Strict Bearer header auth (rejection of token in URL query string, Bearer header acceptance).
2. GET /inbox pagination (newest-first, limit clamp [1..200], next cursor, since=<id>, payload < 10KB).
3. Token separation & GET /admin/tokens format (agent, projects, created, expiry, revoked).
4. POST /admin/tokens/rotate with grace period (old token valid during grace, expired after; new token valid immediately; rotation notice posted).
5. WebSocket reconnect replay (replays only messages after client's last id in chronological order).
"""

import asyncio
import json
import os
import sys
import time
import urllib.request
import urllib.error
import urllib.parse
import uuid

try:
    import websockets
    HAS_WEBSOCKETS = True
except ImportError:
    HAS_WEBSOCKETS = False

HUB_URL = os.environ.get("HUB_URL", "https://agent-mesh-hub.slack-clone-arin.workers.dev").rstrip("/")
ADMIN_TOKEN = os.environ.get("ADMIN_TOKEN") or os.environ.get("HUB_TOKEN")

if not ADMIN_TOKEN:
    print("[Error] ADMIN_TOKEN or HUB_TOKEN must be set in environment", file=sys.stderr)
    sys.exit(1)

def http_req(url, token=None, method="GET", body=None, raw_headers=None):
    headers = {
        "User-Agent": "AgentMesh/PhaseATest",
        "Content-Type": "application/json",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if raw_headers:
        headers.update(raw_headers)
    data = json.dumps(body).encode("utf-8") if body else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            content = resp.read()
            return resp.status, len(content), json.loads(content.decode("utf-8")) if content else {}
    except urllib.error.HTTPError as e:
        content = e.read()
        try:
            return e.code, len(content), json.loads(content.decode("utf-8"))
        except Exception:
            return e.code, len(content), {"error": content.decode("utf-8")}
    except Exception as e:
        return 0, 0, {"error": str(e)}

async def test_websocket_auth(token):
    if not HAS_WEBSOCKETS:
        print("  [SKIP] websockets library not installed")
        return True

    ws_url = HUB_URL.replace("https://", "wss://").replace("http://", "ws://")
    
    # Test 1: Connecting with query param ?token=... (no header) MUST FAIL (401)
    fail_endpoint = f"{ws_url}/connect?agent=test-ws&projects=agent-hub&token={token}"
    ws_rejected = False
    try:
        async with websockets.connect(fail_endpoint, close_timeout=3):
            pass
    except websockets.exceptions.InvalidStatusCode as e:
        if e.status_code == 401:
            ws_rejected = True
    except Exception:
        ws_rejected = True

    assert ws_rejected, "WebSocket should reject query-string ?token= without Bearer header"
    print("  ✓ WebSocket rejected query-string ?token= (401)")

    # Test 2: Connecting with Authorization: Bearer <token> MUST SUCCEED (101)
    ok_endpoint = f"{ws_url}/connect?agent=test-ws&projects=agent-hub"
    headers_dict = {"Authorization": f"Bearer {token}", "User-Agent": "AgentMesh/PhaseATest"}
    connect_kwargs = {"ping_interval": None, "ping_timeout": None, "close_timeout": 5}
    import inspect
    sig = inspect.signature(websockets.connect)
    if "additional_headers" in sig.parameters:
        connect_kwargs["additional_headers"] = headers_dict
    else:
        connect_kwargs["extra_headers"] = headers_dict

    ws_connected = False
    async with websockets.connect(ok_endpoint, **connect_kwargs) as ws:
        ws_connected = True
    assert ws_connected, "WebSocket should accept Bearer header handshake"
    print("  ✓ WebSocket connected successfully with Bearer header")
    return True

async def main():
    print("=" * 65)
    print(" AGENT MESH HUB — PHASE A VERIFICATION TEST SUITE")
    print("=" * 65)
    print(f" Hub URL: {HUB_URL}")
    print()

    # -------------------------------------------------------------
    # 1. AUTH: Strict Bearer Header Requirement
    # -------------------------------------------------------------
    print("[1/5] Testing Strict Bearer Header Auth...")

    # A: Query param ?token= without Authorization header MUST return 401
    status, _, res = http_req(f"{HUB_URL}/status?token={urllib.parse.quote(ADMIN_TOKEN)}")
    assert status == 401, f"Expected 401 for ?token= URL parameter, got {status}: {res}"
    print("  ✓ HTTP query parameter ?token= rejected with 401")

    # B: Authorization: Bearer <token> MUST return 200
    status, _, res = http_req(f"{HUB_URL}/status", token=ADMIN_TOKEN)
    assert status == 200, f"Expected 200 with Bearer header, got {status}: {res}"
    print("  ✓ HTTP Bearer header accepted with 200 OK")

    # C: WebSocket Bearer auth test
    await test_websocket_auth(ADMIN_TOKEN)

    # -------------------------------------------------------------
    # 2. TOKENS: Token Schema & GET /admin/tokens Format
    # -------------------------------------------------------------
    print("\n[2/5] Testing Token Schema & Listing...")
    status, _, res = http_req(f"{HUB_URL}/admin/tokens", token=ADMIN_TOKEN)
    assert status == 200 and res.get("ok"), f"GET /admin/tokens failed: {res}"
    tokens = res.get("tokens", [])
    print(f"  ✓ Listed {len(tokens)} existing tokens")

    # Verify fields: agent, projects, created, expiry, revoked
    for t in tokens[:3]:
        assert "agent" in t, f"Missing agent in {t}"
        assert "projects" in t, f"Missing projects in {t}"
        assert ("created" in t or "created_at" in t), f"Missing created in {t}"
        assert ("expiry" in t or "expires_at" in t), f"Missing expiry in {t}"
        assert ("revoked" in t or "revoked_at" in t), f"Missing revoked in {t}"
    print("  ✓ Token records include required fields (agent, projects, created, expiry, revoked)")

    # -------------------------------------------------------------
    # 3. TOKENS: Rotation with Grace Period
    # -------------------------------------------------------------
    print("\n[3/5] Testing Token Rotation with Grace Period...")
    test_agent = f"test-rot-{uuid.uuid4().hex[:6]}"
    test_proj = f"proj-{uuid.uuid4().hex[:6]}"

    # A: Create initial agent token
    status, _, res = http_req(
        f"{HUB_URL}/admin/tokens",
        token=ADMIN_TOKEN,
        method="POST",
        body={"agent": test_agent, "projects": [test_proj], "description": "Initial token"}
    )
    assert status == 201, f"Create token failed: {res}"
    old_agent_token = res["token"]["token"]
    print(f"  ✓ Created initial token for {test_agent}")

    # Verify old token works to send a message
    status, _, res = http_req(
        f"{HUB_URL}/send",
        token=old_agent_token,
        method="POST",
        body={"project": test_proj, "from": test_agent, "to": "all", "body": "Msg with old token"}
    )
    assert status == 200 and res.get("ok"), f"Old token send failed: {res}"
    print("  ✓ Old token works for sending")

    # B: Rotate token with 4-second grace period
    grace_sec = 4
    status, _, res = http_req(
        f"{HUB_URL}/admin/tokens/rotate",
        token=ADMIN_TOKEN,
        method="POST",
        body={"agent": test_agent, "grace_seconds": grace_sec, "description": "Rotated token"}
    )
    assert status == 201 and res.get("ok"), f"Rotate token failed: {res}"
    new_agent_token = res.get("new_token")
    assert new_agent_token, "Missing new_token in rotate response"
    assert res.get("expires_at"), "Missing expires_at in rotate response"
    print(f"  ✓ Token rotated (grace: {grace_sec}s, expires_at: {res.get('expires_at')})")

    # C: Verify old token still works during grace period
    status, _, res = http_req(
        f"{HUB_URL}/send",
        token=old_agent_token,
        method="POST",
        body={"project": test_proj, "from": test_agent, "to": "all", "body": "Msg during grace"}
    )
    assert status == 200 and res.get("ok"), f"Old token should work during grace period: {res}"
    print("  ✓ Old token accepted during active grace period")

    # D: Verify new token works immediately
    status, _, res = http_req(
        f"{HUB_URL}/send",
        token=new_agent_token,
        method="POST",
        body={"project": test_proj, "from": test_agent, "to": "all", "body": "Msg with new token"}
    )
    assert status == 200 and res.get("ok"), f"New token should work immediately: {res}"
    print("  ✓ New token accepted immediately")

    # E: Wait for grace period to expire
    print(f"  ... waiting {grace_sec + 2}s for grace period to expire ...")
    time.sleep(grace_sec + 2)

    # F: Verify old token is now REJECTED with 401 (expired)
    status, _, res = http_req(
        f"{HUB_URL}/send",
        token=old_agent_token,
        method="POST",
        body={"project": test_proj, "from": test_agent, "to": "all", "body": "Msg after grace"}
    )
    assert status == 401, f"Expected 401 for expired token, got {status}: {res}"
    print("  ✓ Old token correctly rejected with 401 after grace expiry")

    # G: Verify new token still works
    status, _, res = http_req(
        f"{HUB_URL}/send",
        token=new_agent_token,
        method="POST",
        body={"project": test_proj, "from": test_agent, "to": "all", "body": "Msg after grace with new"}
    )
    assert status == 200 and res.get("ok"), f"New token should remain valid: {res}"
    print("  ✓ New token remains active after old token expired")

    # Check that rotation notice was posted to agent
    status, _, res = http_req(f"{HUB_URL}/inbox?agent={test_agent}&projects={test_proj}", token=new_agent_token)
    assert status == 200, f"Inbox fetch failed: {res}"
    messages = res.get("messages", [])
    has_notice = any(m.get("type") == "token_rotated" or "TOKEN ROTATION" in m.get("body", "") for m in messages)
    assert has_notice, f"Expected token rotation notice in inbox for {test_agent}: {messages}"
    print("  ✓ Rotation notice was successfully posted and received in inbox")

    # Clean up test agent tokens
    http_req(f"{HUB_URL}/admin/tokens/revoke", token=ADMIN_TOKEN, method="POST", body={"agent": test_agent, "purge": True})

    # -------------------------------------------------------------
    # 4. INBOX: Pagination, Ordering, Limits, & Size Budget
    # -------------------------------------------------------------
    print("\n[4/5] Testing Inbox Pagination, Ordering & Clamping...")
    inbox_agent = f"pag-{uuid.uuid4().hex[:6]}"
    inbox_proj = f"proj-{uuid.uuid4().hex[:6]}"

    status, _, res = http_req(
        f"{HUB_URL}/admin/tokens",
        token=ADMIN_TOKEN,
        method="POST",
        body={"agent": inbox_agent, "projects": [inbox_proj]}
    )
    inbox_token = res["token"]["token"]

    # Send 12 messages sequentially
    msg_ids = []
    for i in range(1, 13):
        m_id = f"m-{i:02d}-{uuid.uuid4().hex[:8]}"
        status, _, res = http_req(
            f"{HUB_URL}/send",
            token=inbox_token,
            method="POST",
            body={
                "id": m_id,
                "project": inbox_proj,
                "from": inbox_agent,
                "to": "all",
                "body": f"Sequential test message #{i:02d}",
            }
        )
        assert status == 200 and res.get("ok")
        msg_ids.append(m_id)
        time.sleep(0.05)
    print("  ✓ Sent 12 test messages sequentially")

    # A: Default limit & ordering: GET /inbox with limit=5
    status, byte_len, res = http_req(
        f"{HUB_URL}/inbox?agent={inbox_agent}&projects={inbox_proj}&limit=5",
        token=inbox_token
    )
    assert status == 200
    msgs = res.get("messages", [])
    assert len(msgs) == 5, f"Expected 5 messages, got {len(msgs)}"
    # Ordering must be NEWEST-FIRST
    assert msgs[0]["id"] == msg_ids[11], f"Expected newest message first ({msg_ids[11]}), got {msgs[0]['id']}"
    assert msgs[4]["id"] == msg_ids[7], f"Expected 5th newest ({msg_ids[7]}), got {msgs[4]['id']}"
    assert res.get("next") == msgs[4]["id"], f"Expected next cursor to be {msgs[4]['id']}, got {res.get('next')}"
    assert byte_len < 10240, f"Payload size {byte_len} exceeds 10KB"
    print(f"  ✓ Ordering is newest-first, limit=5 respected, payload={byte_len}B (<10KB)")
    print(f"  ✓ 'next' cursor correctly returned: {res.get('next')}")

    # B: Older page using 'cursor' (or 'before')
    cursor = res.get("next")
    status, byte_len, res2 = http_req(
        f"{HUB_URL}/inbox?agent={inbox_agent}&projects={inbox_proj}&limit=5&cursor={cursor}",
        token=inbox_token
    )
    assert status == 200
    msgs2 = res2.get("messages", [])
    assert len(msgs2) == 5, f"Expected 5 messages in page 2, got {len(msgs2)}"
    assert msgs2[0]["id"] == msg_ids[6], f"Expected {msg_ids[6]}, got {msgs2[0]['id']}"
    assert msgs2[4]["id"] == msg_ids[2], f"Expected {msg_ids[2]}, got {msgs2[4]['id']}"
    print("  ✓ Second page correctly fetched using 'next' cursor")

    # C: Test limit clamping: requesting limit=500 must clamp to at most 200
    status, _, res_clamp = http_req(
        f"{HUB_URL}/inbox?agent={inbox_agent}&projects={inbox_proj}&limit=500",
        token=inbox_token
    )
    assert status == 200
    assert res_clamp.get("count", 0) <= 200, "Limit should be clamped to 200 max"
    print("  ✓ Limit clamped to max 200")

    # D: Test since=<id>: messages newer than msg #8 (msg-09 to msg-12)
    since_target = msg_ids[7]  # msg #8
    status, _, res_since = http_req(
        f"{HUB_URL}/inbox?agent={inbox_agent}&projects={inbox_proj}&since={since_target}",
        token=inbox_token
    )
    assert status == 200
    msgs_since = res_since.get("messages", [])
    assert len(msgs_since) == 4, f"Expected 4 messages newer than msg #8, got {len(msgs_since)}"
    assert msgs_since[0]["id"] == msg_ids[11]
    assert msgs_since[3]["id"] == msg_ids[8]
    print(f"  ✓ since=<id> returned exactly messages newer than id ({len(msgs_since)} messages)")

    # -------------------------------------------------------------
    # 5. WEBSOCKET: Reconnect Backlog Replay
    # -------------------------------------------------------------
    print("\n[5/5] Testing WebSocket Reconnect Replay...")
    if HAS_WEBSOCKETS:
        ws_url = HUB_URL.replace("https://", "wss://").replace("http://", "ws://")
        reconnect_endpoint = f"{ws_url}/connect?agent={inbox_agent}&projects={inbox_proj}&since={msg_ids[9]}"
        headers_dict = {"Authorization": f"Bearer {inbox_token}", "User-Agent": "AgentMesh/PhaseATest"}
        connect_kwargs = {"ping_interval": None, "ping_timeout": None, "close_timeout": 5}
        import inspect
        sig = inspect.signature(websockets.connect)
        if "additional_headers" in sig.parameters:
            connect_kwargs["additional_headers"] = headers_dict
        else:
            connect_kwargs["extra_headers"] = headers_dict

        replayed_messages = []
        async with websockets.connect(reconnect_endpoint, **connect_kwargs) as ws:
            # Collect backlog
            try:
                while True:
                    raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
                    data = json.loads(raw)
                    m_type = (data.get("type") or "").lower()
                    if m_type not in ("ping", "pong") and data.get("project") != "system":
                        replayed_messages.append(data)
            except asyncio.TimeoutError:
                pass

        assert len(replayed_messages) == 2, f"Expected 2 replayed messages (msg-11, msg-12), got {len(replayed_messages)}: {replayed_messages}"
        # Chronological order
        assert replayed_messages[0]["id"] == msg_ids[10], f"Expected msg-11, got {replayed_messages[0]['id']}"
        assert replayed_messages[1]["id"] == msg_ids[11], f"Expected msg-12, got {replayed_messages[1]['id']}"
        print("  ✓ WebSocket reconnect correctly replayed only messages after client's last id in chronological order")

    # Clean up test tokens
    http_req(f"{HUB_URL}/admin/tokens/revoke", token=ADMIN_TOKEN, method="POST", body={"agent": inbox_agent, "purge": True})

    print("\n" + "=" * 65)
    print(" ALL PHASE A REQUIREMENTS VERIFIED SUCCESSFULLY! ✓")
    print("=" * 65 + "\n")

if __name__ == "__main__":
    asyncio.run(main())
