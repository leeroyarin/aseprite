---
name: agent-mesh-subagent
description: "Autonomous Agent Mesh Subagent protocol and operational workflows for Aseprite 2.0: cross-agent coordination with Muse (Linux VM), task ingestion, single ACK confirmation (anti-loop), hygiene auditing, Git branch/PR automation, Windows MSVC verification, HANDOFF.md synchronization, and standardized result reporting."
---

# /agent-mesh-subagent

Operational guidelines and automation patterns for the **Aseprite 2.0 / Pitch Quest** Agent Mesh Subagent. Use these protocols whenever coordinating tasks across Linux (`muse`) and Windows (`arin-pc`, `arin-pc-daedalus-antigravity-leeroyarin`).

---

## 1. Network Topology & Identity

- **Cloudflare Agent Mesh Hub**: `https://agent-mesh-hub.slack-clone-arin.workers.dev`
- **Primary Project Channel**: `agent-hub`
- **Node Identities**:
  - `muse`: Linux development VM (coordinator & implementation partner)
  - `arin-pc`: Windows test runner host
  - `arin-pc-daedalus-antigravity-leeroyarin`: Windows subagent / coordinator node
  - `arin-pc-codex`: Codex analysis peer
- **Environment & Credentials Resolution**:
  - Checks process environment (`os.environ`) first.
  - On Windows, automatically falls back to `HKCU:\Environment` (`winreg`) for:
    - `AGENT_NAME`: Current agent identifier.
    - `AGENT_TOKEN`: Token beginning with `amt_...`.
    - `HUB_URL`: Base URL of the Cloudflare Worker hub.
    - `PROJECTS`: Comma-separated allowed projects (e.g., `agent-hub`).
- **HTTP User-Agent Requirement**:
  - Always set `User-Agent: AgentMeshSubagent/<agent>` on all requests. Cloudflare WAF drops default Python `urllib` requests with `403 Forbidden`.

---

## 2. Security & Repository Hygiene Policy

All agents operating in this repository must strictly adhere to:
1. **Never Commit Secret Tokens**:
   - Never commit or stage tokens (`amt_*`, `gho_*`, `HUB_TOKEN`, `AGENT_TOKEN`).
   - Run `python tools/agent-mesh/mesh_subagent.py audit` before committing.
2. **Never Touch `third_party/`**:
   - Submodules (`third_party/libwebp`, `third_party/fmt`, etc.) are frozen.
3. **Never Commit PixelLab Files**:
   - Proprietary PixelLab extension files (`PixelLab.aseprite-extension`, `%APPDATA%\Aseprite\extensions\pixellab`) must never be added to git.
4. **Code Formatting Rules**:
   - C++: 2-space indentation, max 100 columns (`.clang-format`).
   - Clean git diff: no trailing whitespace or unresolved merge conflict markers.

---

## 3. Communication Protocol with Muse

### Silent Protocol Messages
- Ping and pong messages (`type: ping`, `type: pong`) and `project: system` messages are handled silently by the WebSocket/polling engine. They are never logged, never appended to task inboxes, and never wake the AI.

### Single ACK Confirmation Rule (Anti-Loop Policy)
- When a task, handoff, or message requires acknowledgment from `muse` or `William`:
  - Reply **ONCE** to confirm receipt with `reply_to=<task_id>` and a one-line status:
    `[status] <note>` (e.g., `[accepted] Task received. Preparing branch...`).
  - **DO NOT** send repeated "got it" messages.
  - **DO NOT** enter an auto-acknowledgement loop.
  - Final task results must be reported in **ONE** comprehensive result message per phase when work is finished.

---

## 4. Git Coordination Workflow

When a task is received from `muse`:
1. **Inspect and Parse Directive**:
   - Read suggested branch (e.g., `feature/easymetric-base-first-daedalus`) and base commit/branch (e.g., `b0f0578` / `origin/Aseprite-2.0`).
2. **Checkout Base & Create Feature Branch**:
   - Never work directly on `feature/agent-mesh-hub`, `codex/baseline-b0f0578`, or `Aseprite-2.0`.
   - Run: `python tools/agent-mesh/mesh_subagent.py checkout-task <task_id>`
3. **Execute Implementation & Local Audits**:
   - Verify hygiene: `python tools/agent-mesh/mesh_subagent.py audit`.
4. **Push & Open Pull Request**:
   - Push to `origin`: `git push -u origin <branch>`
   - Create PR targeting `Aseprite-2.0`: `gh pr create --base Aseprite-2.0 --head <branch> --title ... --body ...`
   - **Never merge the PR yourself**: William merges.

---

## 5. Verification & Testing Protocol

Standing requirement from William: **All tests must be run properly on a Windows build**. Partial passes are not accepted.

1. **Build Step**:
   - Run MSVC build: `powershell -ExecutionPolicy Bypass -File tools\aseprite2-build.ps1`
   - Must exit 0 and produce `build/bin/aseprite.exe`.
2. **Test Suites**:
   - **Regression Suite**: `tests/easymetric_test.lua` (67 tests)
   - **Adversarial Suite**: `tests/easymetric_adversarial_stress.lua` (54 tests)
   - **Four-Requirements Suite**: `tests/easymetric_four_requirements_test.lua` (23 tests)
3. **Reporting Criteria**:
   - Pass/fail counts, total durations, and exact steps/snippets to reproduce any failure.

---

## 6. HANDOFF.md Synchronization

Before stopping or delivering final results, record progress in `docs/aseprite2/HANDOFF.md` under `## Log (newest first)`:

```markdown
- YYYY-MM-DD — <AgentName>: <Summary of change / milestone>
  - <Detail 1: branch and commit SHA>
  - <Detail 2: Windows MSVC build exit code and timing>
  - <Detail 3: test suite pass/fail counts>
  - <Detail 4: PR URL or reproduction details>
```

---

## 7. Mesh Subagent CLI Commands

Run from repo root using `python tools/agent-mesh/mesh_subagent.py <subcommand>`:

| Command | Description |
|---|---|
| `status` | Display hub connection, git branch, commit SHA, working tree cleanliness, active in-progress tasks, and pending Muse tasks. |
| `tasks [--all]` | Show detailed list of unhandled tasks received from `muse` (or all with `--all`). |
| `ack <task_id> [--status accepted\|blocked] [--note "..."] [--force]` | Send exactly one ACK confirmation message to Muse (anti-loop protected; use `--force` to override). |
| `checkout-task <task_id> [--branch <name>] [--base <ref>]` | Parse task, fetch origin, and create the designated feature branch. |
| `audit` | Check diff and untracked files for whitespace errors, leaked tokens, `third_party/`, or `PixelLab` changes, and added lines > 100 cols. |
| `build [--target aseprite]` | Execute MSVC x64 build via `tools\aseprite2-build.ps1`. |
| `test [--suite all\|regression\|adversarial\|four-req]` | Run Lua test suites and report pass/fail metrics. |
| `sync [--branch <branch>] [--remote origin]` | Synchronize branch with remote (fetch, checkout, and pull). |
| `push-pr [--branch <branch>] [--base <base>]` | Push branch to origin and open GitHub pull request via `gh`. |
| `report-result <task_id> [--notes "..."] [--dry-run] [--skip-build] [--skip-test]` | Format and deliver final single result message back to Muse. |
| `handoff --summary "..." [--detail "..."]` | Prepend structured log entry to `docs/aseprite2/HANDOFF.md`. |
| `daemon [--interval 15] [--auto-ack]` | Run background polling loop for autonomous task intake (auto-ack disabled by default for loop prevention). |
