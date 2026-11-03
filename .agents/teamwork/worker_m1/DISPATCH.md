## 2026-11-03T04:06:14Z
You are Worker M1 (Runner Implementation Specialist).

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

You MUST read the Original Request file first:
`e:\Work\Development\aseprite\.agents\teamwork\ORIGINAL_REQUEST.md`

Your Working Directory: `e:\Work\Development\aseprite\.agents\teamwork\worker_m1`
Your Identity: teamwork_worker
Your Parent / Orchestrator Conversation ID: `f47682be-6cc6-4df9-bbe4-491c1979bf04`
Input Analysis: Read `e:\Work\Development\aseprite\.agents\teamwork\explorer_survey_2\report.md` for exact gap analysis and recommendations.

File Ownership:
You EXCLUSIVELY own:
- `tools/agent-mesh/mesh_subagent.py`
- `tools/agent-mesh/test_mesh_subagent.py`
Do NOT modify any files in `src/` or `third_party/`.

Implementation Requirements:
1. `tools/agent-mesh/mesh_subagent.py`:
   - In `TaskStore`: Add explicit persistence for `NEW` tasks. When unacknowledged tasks are listed/ingested from the inbox in `MeshClient.list_pending_muse_tasks()`, persist them into `TaskStore` with status `NEW` (including sender, timestamp, body snippet, and subject).
   - In `MeshClient`: Add `send_progress(task_id, note)` method. Dispatches a message to Muse with `reply_to=task_id`, `type="status"`, body `f"[progress] {note}"` (or formatted progress note). This allows interim progress reporting without locking or colliding with the single-ACK anti-loop guard.
   - In CLI subcommands: Add `progress` command: `progress <task_id> --note "..."` to invoke `mesh.send_progress()`.
   - In `cmd_daemon`: Add `--once` flag (run a single polling tick and return 0), `--max-ticks N` flag, and wrap loop polling in a `try...except Exception as e:` block with exponential backoff on connection/HTTP errors so transient network drops do not crash the daemon.
   - Ensure the single-ACK anti-loop guard remains strictly enforced (never send duplicate ACKs for acknowledged/in-progress/completed tasks).
2. `tools/agent-mesh/test_mesh_subagent.py`:
   - Expand the test suite with new unit tests covering:
     - `test_daemon_single_tick_mode` (verifies `--once` flag exits 0 after 1 tick).
     - `test_daemon_ingests_and_records_new_state` (verifies unacknowledged tasks are persisted as `NEW` on disk).
     - `test_progress_message_format_and_dispatch` (verifies `send_progress` sends proper payload and doesn't interfere with ACK state).
     - `test_task_store_corrupted_json_recovery` (verifies robust fallback if state JSON is corrupted/empty).
3. Verification:
   - Run `python tools/agent-mesh/test_mesh_subagent.py` and verify 100% of tests pass (all 23+ tests passing, 0 failures).
   - Run `python tools/agent-mesh/mesh_subagent.py audit` and confirm exit code 0.
   - Run `python tools/agent-mesh/mesh_subagent.py daemon --once` to verify single-tick operation.
4. Deliverable:
   Write your handoff report to `e:\Work\Development\aseprite\.agents\teamwork\worker_m1\handoff.md` with:
   - Observation: files modified, tests run, exact output
   - Logic Chain: rationale for changes made
   - Caveats: edge cases handled
   - Conclusion: summary of R1 completion
   - Verification Method: commands and pass results
   Then notify orchestrator (`f47682be-6cc6-4df9-bbe4-491c1979bf04`) via `send_message`.
