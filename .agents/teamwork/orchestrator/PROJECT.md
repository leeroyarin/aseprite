# Project: Autonomous Agent Mesh Subagent Runner & Live Coordination

## Architecture
- **Agent Mesh Hub Client**: Python-based runner (`tools/agent-mesh/mesh_subagent.py`) interacting with Cloudflare Worker Hub (`https://agent-mesh-hub.slack-clone-arin.workers.dev`) on project `agent-hub`.
- **Identity & Auth**: Windows Registry (`HKCU:\Environment`) fallback for `AGENT_TOKEN` and `AGENT_NAME` (`arin-pc-daedalus-antigravity-leeroyarin`).
- **Anti-Loop Architecture**: State store (`.mesh_processed_tasks.json`) tracking `NEW`, `ACKNOWLEDGED`, `IN_PROGRESS`, `COMPLETED`. Non-task filtering (pings, status, results ignored for ACK triggers). Duplicate ACK veto in `MeshClient.send_ack()`.
- **Easymetric Base-First Silhouette**: In `src/app/commands/cmd_easymetric.cpp`, `extractSixSurfaces` and `buildSculptedVolume` constrain/clamp secondary views and texture projections to the silhouette defined by `Surface_Base`.
- **Isolation Boundary**: The PR into `Aseprite-2.0` on `feature/easymetric-base-first-daedalus` strictly includes ONLY `src/app/commands/cmd_easymetric.cpp` and `docs/aseprite2/HANDOFF.md`. Tooling (`tools/agent-mesh/` and `.agents/`) is local to the workspace.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | NEW Task Persistence | Ingested inbox tasks written to disk as NEW before ACK/checkout | M1 | Survey / R1 |
| 2 | Daemon Single-Tick & Resiliency | Support `--once` and `--max-ticks` with exception backoff | M1 | Survey / R1 |
| 3 | Intermediate Progress Messaging | `progress` subcommand and `send_progress()` without ACK lock | M1 | Survey / R1 |
| 4 | Anti-Loop Regression Tests | Expand unit tests covering new daemon & progress features (100% pass) | M1 | Survey / R1 |
| 5 | Base-First Silhouette Carving | Clamp non-base surfaces to Surface_Base silhouette in `cmd_easymetric.cpp` | M2 | Survey / R2 |
| 6 | Line-Length Differential Hygiene | Ensure all modified lines in `src/` are <= 100 columns | M2 | Survey / R4 |
| 7 | HANDOFF.md Documentation | Prepend Phase 1 log entry in `docs/aseprite2/HANDOFF.md` | M2 | Survey / R3 |
| 8 | MSVC C++ Build Execution | Compile modified sources using CMake/Ninja with MSVC toolchain | M3 | Survey / R4 |
| 9 | Test Suite Verification | Run 100% passing tests (`test_mesh_subagent.py` and Easymetric tests) | M3 | Survey / R4 |
| 10 | Git Commit & Branch Isolation | Commit only `cmd_easymetric.cpp` and `HANDOFF.md` with task ca894e89 reference | M4 | Survey / R3 |
| 11 | GitHub PR Creation | Open PR targeting `Aseprite-2.0` from `feature/easymetric-base-first-daedalus` | M4 | Survey / R3 |
| 12 | Agent Mesh Hub Completion Payload | Single final `result` message to Muse via Hub API with full metrics | M4 | Survey / R2 |
| 13 | Forensic Integrity Audit | Independent verification of zero cheating, genuine C++ logic, and hygiene | M5 | Survey / R4 |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | Subagent Runner Enhancements & Anti-Loop | `tools/agent-mesh/mesh_subagent.py`, `tools/agent-mesh/test_mesh_subagent.py` | None | DONE |
| 2 | Phase 1 Base-First Silhouette Carving | `src/app/commands/cmd_easymetric.cpp`, `docs/aseprite2/HANDOFF.md` | M1 | DONE |
| 3 | MSVC Build & Test Verification | MSVC build compilation, test suite execution, differential hygiene | M2 | HANDOVER (deferred) |
| 4 | Git Commit, PR & Hub Coordination | Git commit, branch push, `gh pr create`, Muse completion payload | M3 | HANDOVER (deferred) |
| 5 | Forensic Integrity Audit | Full forensic audit and gate verification | M4 | DONE (CLEAN on M1 & M2) |

## Interface Contracts
### `mesh_subagent.py` ↔ Agent Mesh Hub
- `POST /send`:
  - `type: "status"`: `[accepted] ...` for ACK; `[progress] ...` for progress updates.
  - `type: "result"`: Final report with build, test, and PR summary.
  - `reply_to`: UUID of original task message.
- `GET /inbox`:
  - Query: `agent=<name>&projects=agent-hub&since=0&limit=100`.
  - Filters: Discard processed tasks, non-task pings/pongs, ignore messages not directed to this agent.

### `cmd_easymetric.cpp` Base Silhouette Clamping
- `extractSixSurfaces(doc::Sprite* spr, ...)`:
  - Base silhouette mask defines valid pixels.
  - Pixels on non-base surfaces exceeding the base projection are clamped to base alpha boundary.
- `buildSculptedVolume(const SixSurfaceData& data, ...)`:
  - TopDown/FrontBack/RightLeft visual hull intersection maintains Base as the definitive silhouette.

## Code Layout
- `tools/agent-mesh/mesh_subagent.py`: Subagent runner and CLI tool
- `tools/agent-mesh/test_mesh_subagent.py`: Subagent test suite
- `src/app/commands/cmd_easymetric.cpp`: Easymetric 3D projection and silhouette carving
- `docs/aseprite2/HANDOFF.md`: Project status and changelog
- `.agents/teamwork/`: Agent coordination metadata ONLY
