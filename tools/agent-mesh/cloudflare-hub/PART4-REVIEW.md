# Part 4 checkpoint - 2026-11-05 - Codex

## Result and scope

Arin approved Part 3 and the 15% usage warning / 10-minute freshness defaults and asked to
continue. Part 4 implements read/accept/install/prove enrollment and its standalone logger.
Source branch remains codex/agent-hub-v2-step1. Stop for Arin's check before Part 5 enforcement.
No production deploy, Access/D1 resource change, secret/token change or live-agent write occurred.
The Worker name/address, root version and native routes remain unchanged.

## What changed

- Header-authenticated current-rule acceptance with identity/hash/time checks, permanent consent
  records and duplicate-id handling. Only agents submit their own enrollment evidence.
- Exact downloadable logger source/hash, deterministic generation and stale-artifact test guard;
  Windows checkout preserves LF bytes. The installed copy proves its own byte hash.
- Actual Codex storage research on this PC used a session_meta header and verified
  C:/Users/leero/.codex/sessions. No chat text was copied. Other machines/tools must research their own.
- Installer requires observed path evidence and disjoint chat/commands folders, preserves existing
  different scripts/config/history, and creates a separate commands JSONL log. No PC server or tunnel.
- Logger records explicit command data only, redacts before append, fsyncs receipts, deduplicates
  received IDs and appends status transitions. It neither reads normal chat packets nor executes
  command text; it has no network imports/calls. Locks/corruption/conflicts fail without rewriting.
- Legacy-id collision guard runs before D1 projection; challenge envelope ids are independently
  unpredictable, so a preused client request id cannot shadow earlier native history.
- Private five-minute, 32-random-byte one-use challenge travels through existing inbox/WebSocket.
  Proof checks parent, current rules, exact script hash, folder claims, message id, expiry, owner state
  and canonical receipt hash. Failed attempts are consumed/recorded; identical retries are idempotent.
- Append-only enrollment audit, acceptances, challenges, proofs and consumption records; private
  own/owner reads. Status pages derive state in one query rather than one query per agent.
- Owner revoke/allow records, sticky Revoked and fresh acceptance/proof after allow. Enrollment
  revocation does not revoke credentials or disconnect native clients in record-only mode.
- Owner console status controls/banner; explicit enrollment report files in the outbound client.
  A failed proof/upload cannot stop inbox polling. The client never auto-installs/runs the logger.

## Routes added

Under /api/v2, or signed owner /api/v2/admin:
- GET /enrollment/logger and /enrollment/logger.py.
- POST /enrollment/accept, /enrollment/challenges and /enrollment/proofs (agent only).
- GET /enrollment/status (own), /enrollment/audit (own or owner all/filter).
- Owner GET /enrollment/agents and /enrollment/agents/:agent.
- Owner POST /enrollment/agents/:agent/revoke and /allow, with explicit reason.
API.md and docs/ENROLLMENT.md contain field shapes, lifecycle, paths and client commands.

## Verification

- 109 Node tests passed, 0 failed/skipped: 89 existing + 20 enrollment tests. Actual workerd,
  local D1 and SQLite DOs; disposable credentials and genuine signed Access fixtures.
- The integration downloads the actual Worker artifact, discovers fixture session metadata,
  installs the actual Python script in a separate folder, runs that installed script to log/prove,
  and submits its exact receipt to the Worker. It is not just a mocked hash comparison.
- 32 Python tests passed, 0 failed/skipped: 16 outbound-client and 16 standalone-logger tests.
  Client networking is mocked. Logger tests exercise real files and installed subprocesses,
  append/dedup/status transitions, untouched chats, secrets, corruption/locks, installation/hash,
  expired proofs, static credential-safe errors and absence of execution/network entrypoints.
- Total: 141 tests passed. TypeScript typecheck, artifact consistency and Wrangler dry-run pass.
  No upload occurred. Pinned Wrangler 3.114.17 retained; no production config edits.
- New tests include unenrolled/failed/revoked record-only send/read/connect; wrong-script and expired
  failure auditing; private delivery; wrong actor/paths/code/receipt; replay/supersession; re-acceptance;
  owner reset; status/audit pagination; credential-shaped metadata and immutable history.
- git diff --check passes. No Aseprite C++/submodule changes or build were needed.

## Not verified / honest limitations

- No actual Muse/arin-pc installation/enrollment, production D1 migration, Cloudflare Access browser
  round trip, live Cron or deployed UI. Only this PC's chat-directory evidence was directly researched.
- Hash/path/receipt claims are remotely supplied. The hub checks consistency with its exact artifact
  and packet, not physical device files, script execution or future obedience. The isolated test did
  execute the script, but a dishonest remote agent can fabricate consistent local evidence.
- The real rule book has not been published to production; no live agent was accepted or revoked.
- Mode is hardwired record_only in this part. Part 5 still must add owner-only settings/grace,
  periodic fresh proof, enforce-only restrictions and revoke-close tests. Enforce remains off.
- Part 6 final onboarding (including existing Connect your agent) and actual cloud/agent parity remain
  required. The old local prototype is not retired. The earlier React design is still separate.
- A command has one received record; later status transitions append separate events for its same id.
  No existing JSONL line is edited or deleted. This resolves dedup plus append-only status history.

## Arin's check

Review Part 4 before Part 5. Challenge TTL is five minutes; failed attempts consume the code and
require a fresh challenge. Enrollment remains record-only. No operational setup is required from
Arin yet; production resources/deployment and real agent adoption stay separately approval-gated.

## Reproduce

npm run build:logger; npm run typecheck; npm test; npm run test:sidecar;
npm exec -- wrangler deploy --dry-run --outdir .test-build.
Never run the guarded live mutation scripts or remote migrations for these local tests.
