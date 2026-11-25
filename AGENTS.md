# AGENTS.md — Aseprite 2.0 fork

This repo = Aseprite source being extended into **Aseprite 2.0**:
AI-agent control via MCP, PixelLab extension integration, built-in project versioning.

**Any agent/model continuing work: read these first, in order.**

1. [docs/aseprite2/HANDOFF.md](docs/aseprite2/HANDOFF.md) — current status, what is done, what is next, gotchas. **Update it before you stop.**
2. [docs/aseprite2/PLAN.md](docs/aseprite2/PLAN.md) — architecture + phase plan + decisions.
3. [docs/aseprite2/PROTOCOL.md](docs/aseprite2/PROTOCOL.md) — WebSocket JSON contract between MCP server (Node) and in-app Lua bridge. Contract is shared by Phase 3 and Phase 4; change both sides + this doc together.
4. [docs/aseprite2/VERSIONING.md](docs/aseprite2/VERSIONING.md) — on-disk format of `<name>.versions/`.
5. [docs/aseprite2/RECORDER.md](docs/aseprite2/RECORDER.md) — the local session recorder. When the user reports "it did X, I expected Y", read their latest recording first: `python tools/usage/summarize_session.py --notes`.
6. [docs/aseprite2/OBJECTS.md](docs/aseprite2/OBJECTS.md) — Easymetric objects (head/body as separate parts): data model, the object scope the helpers consult, composite preview.

## Rules for agents

- Keep changes scoped to phases in PLAN.md. Mark progress in HANDOFF.md (checkboxes + "Log" section, newest first, with date + model name).
- C++ style: follow `.clang-format` (2-space indent, 100 cols). New files need the Aseprite header comment block (see any `src/app/*.cpp`).
- Build: `cmake --build build` from repo root (Ninja, RelWithDebInfo, MSVC). Must open "x64 Native Tools" env or run via `build.cmd`-style vcvars if `cl` not on PATH. See HANDOFF.md "Build".
- Do NOT commit the PixelLab extension or any of its files (proprietary, contains license secret). It is installed at runtime from the user's own copy.
- Do NOT touch `third_party/` (submodules) except when a phase explicitly says so.
- New user-visible strings go in `data/strings/en.ini`; new menu items in `data/gui.xml`.
- **Use graphify to explore the repo**: if you are not already familiar with this repository (or the area you're touching), use the `graphify` skill (`/graphify`, or `graphify query` against `graphify-out/` if it exists) to map the codebase before reading raw files or making changes. If no graph exists yet, build one first (`graphify .`). After modifying code, run `graphify update .` to keep it current.
- **No Redundant Acknowledgments**: When another agent confirms receipt ("got it", "understood", "acknowledged"), do NOT reply with redundant confirmations ("got your message", "got it", "acknowledged", "noted"). Eliminate ack ping-pong; only send messages that deliver new technical findings, changes, or direct questions.

## Agent Mesh (Cross-Agent Communication)
- Hub lives under `tools/agent-mesh/` and `tools/agent-mesh/cloudflare-hub/`.
- Auth is Bearer header only (`Authorization: Bearer <token>`). Token-in-URL is rejected.
- Admin secret and agent tokens are env-only (`ADMIN_TOKEN`, `HUB_TOKEN`, `AGENT_TOKEN`). Never print, save, or commit tokens.
