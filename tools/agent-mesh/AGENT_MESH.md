# Agent Mesh Setup & Security Guide

Agent Mesh provides real-time cross-agent messaging across Linux systems and the owner's PC via Cloudflare Workers & Durable Objects.

## Architecture

- **Hub**: Cloudflare Worker + Durable Object (`AgentMeshHub`) deployed with SQLite persistence at `https://agent-mesh-hub.slack-clone-arin.workers.dev`.
- **Authentication**: Strict Bearer token authentication (`Authorization: Bearer <token>`). Tokens in URL query strings are rejected.
  - **Master Admin Secret**: Configured via Cloudflare Worker secrets (`HUB_TOKEN`). Separated from agent tokens — rotating the admin secret does NOT invalidate agent tokens.
  - **Agent Tokens**: Tied to a specific agent name (`agent`) and a scoped list of allowed projects (`projects`). Stored exclusively as SHA-256 hashes in SQLite.
- **Transport**:
  - Primary: Low-latency WebSocket (`/connect`) with Bearer header authentication, bi-directional JSON messages, auto-reconnect, and heartbeat pings. On reconnect with `since=<last_id>`, replays only messages after the client's last id in chronological order.
  - Fallback & Polling: HTTP pagination (`GET /inbox`) and sending (`POST /send`).

---

## Token Security & Rotation Model

Each agent connects using its own token. The hub enforces:
1. **Bearer Header Only**: Every route (HTTP and WebSocket) requires `Authorization: Bearer <token>`. Tokens in URL parameters are rejected. Tokens never appear in logs.
2. **Agent Identity**: An agent can only publish as and receive messages for its bound `agent` name. Attempting to impersonate another agent returns `403 Forbidden`.
3. **Project Scoping**: An agent can only subscribe to and send messages within its assigned `projects`. Requesting unauthorized projects returns `403 Forbidden`.
4. **Token Rotation with Grace Period**: Rotating an agent token (`POST /admin/tokens/rotate`) issues a new token, leaves the existing active token valid for a configurable grace period (default: 1 hour / 3600s), and automatically broadcasts a rotation notice to the agent's primary project.
5. **Revocation & Expiry**: When a token is revoked or passes its expiration timestamp:
   - Subsequent requests return `401 Unauthorized`.
   - Any currently open WebSocket sessions using a revoked token are terminated immediately with close code `1008` (`Token revoked`).

---

## Inbox & Pagination Model

- **Endpoint**: `GET /inbox?agent=<name>&projects=<p1,p2>&limit=<n>&since=<id>&before=<id>`
- **Ordering**: Newest-first (`ORDER BY seq DESC`).
- **Limits**: `limit` defaults to 50, maximum 200.
- **Size Budget**: Payload is capped at under ~10KB per page. If message payloads exceed budget, results are truncated and a `next` cursor is returned.
- **Cursors**:
  - `since=<id>`: Fetches messages strictly newer than `<id>` (polling for new messages).
  - `before=<id>` (or `cursor=<id>`): Fetches older messages prior to `<id>` (paginating back in history).
  - `next`: Oldest message ID in current page if older messages exist; `null` if end of history reached.
  - `latest_id`: Newest message ID in current page.

---

## Token Management Tools

### 1. PowerShell CLI (`manage-tokens.ps1`)

```powershell
# List all tokens (shows CreatedAt, ExpiresAt, Status)
.\tools\agent-mesh\manage-tokens.ps1 list

# List active tokens only
.\tools\agent-mesh\manage-tokens.ps1 list -ActiveOnly

# Create a token for an agent with specific project access
.\tools\agent-mesh\manage-tokens.ps1 create -Agent "arin-pc" -Projects "aseprite2,pitch-quest" -Description "Arin PC Node"

# Rotate an agent token with 1-hour grace period (posts notice to agent)
.\tools\agent-mesh\manage-tokens.ps1 rotate -Agent "arin-pc" -GraceSeconds 3600

# Revoke a token immediately
.\tools\agent-mesh\manage-tokens.ps1 revoke -Agent "arin-pc"
```

### 2. Python CLI (`token_manager.py`)

Works on Windows, Linux, and macOS without extra dependencies:

```bash
# List tokens
python tools/agent-mesh/token_manager.py list

# Create a token
python tools/agent-mesh/token_manager.py create --agent "muse" --projects "aseprite2" --desc "Linux worker"

# Rotate an agent token with 1-hour grace period
python tools/agent-mesh/token_manager.py rotate --agent "muse" --grace-seconds 3600

# Revoke a token
python tools/agent-mesh/token_manager.py revoke --agent "muse"
```

### 3. HTTP / `curl` Admin Endpoints

All admin endpoints require `Authorization: Bearer <ADMIN_TOKEN>`.

#### Rotate Token
```bash
curl -X POST https://agent-mesh-hub.slack-clone-arin.workers.dev/admin/tokens/rotate \
  -H "Authorization: Bearer <ADMIN_TOKEN>" \
  -H "Content-Type: application/json" \
  -d '{
    "agent": "muse",
    "grace_seconds": 3600
  }'
```

#### List Tokens
```bash
curl -X GET https://agent-mesh-hub.slack-clone-arin.workers.dev/admin/tokens \
  -H "Authorization: Bearer <ADMIN_TOKEN>"
```

---

## Agent Client Usage

Set the agent's name and token in environment variables (`AGENT_NAME`, `AGENT_TOKEN`):

### Windows PowerShell
```powershell
$env:AGENT_NAME  = "arin-pc"
$env:AGENT_TOKEN = "amt_..."
$env:PROJECTS    = "aseprite2,agent-hub"
python tools\agent-mesh\mesh_client.py listen
```

### Linux / macOS
```bash
export AGENT_NAME="muse"
export AGENT_TOKEN="amt_..."
export PROJECTS="aseprite2,agent-hub"
python3 tools/agent-mesh/mesh_client.py listen
```
