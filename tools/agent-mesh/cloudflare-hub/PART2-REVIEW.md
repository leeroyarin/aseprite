# Part 2 review checkpoint - 2026-11-05 - Codex

## Result

Part 2 is implemented on codex/agent-hub-v2-step1 in the existing isolated worktree.
Stop for Arin's check before Part 3. No commit, merge, push, production deployment, cloud
resource creation, Access policy change or secret/token change was performed.
The Worker name/address, existing root health response and production wrangler.toml remain unchanged.
The local source baseline tag remains agent-mesh-hub-v2.1.0-baseline-20261105 at 70fcbdd51.

## What changed, in the requested order

1. Projects, announcements, groups and DMs; channel membership checks, owner-only announcements,
   paged owner read-all, canonical message IDs with deduplication and durable private fan-out.
   Both old and new native inboxes, and sockets, receive one copy per recipient, including sender.
2. Registry profiles, roles, server-observed last seen, append-only status reports and paged reads.
   Registration creates no token; peers do not get other agents' private status payloads.
3. Five-minute scheduled supervisor source, idempotent checks, Healthy/Late/Down transitions,
   connected-session discovery, open-task coverage/return, immutable assignment events and general
   channel health messages. No eligible peer produces an explicit uncovered activity event.
4. Plan/onboarding documents, optimistic edits, permanent revision history, restore as a new revision,
   agent suggestions, owner approve/reject and per-document/per-version acknowledgements/readback.
5. Optional sub labels on v2 transport and canonical channel messages; parent remains the author
   and credential owner. No subagent credential issuance mechanism was added.
6. Append-only activity and private FTS search, including old native DMs and document revisions;
   additive search backfill leaves original history untouched.
7. Access-protected SSE live invalidations, then a functional portal served from the same Worker
   at /api/v2/admin/portal, with existing mesh inbox and feature views. No sample data or token store.

The local model, permission/API cases and sidecar auth/redaction/backoff helpers were reused.
Only outbound client functionality was ported: no local HTTP API, listener, tunnel or execution.
New migration files 0003/0004 are additive; all fixtures live only in disposable test databases.
No production history was inspected, scrubbed or deleted.

## Routes added

API.md lists every method, shape and permission. Families under /api/v2 (and owner /api/v2/admin):
- /projects; /projects/:id/channels; /channels/:id/messages; owner /messages read-all.
- /agents; agent /agents/status; /agents/:id/reports.
- /projects/:id/tasks and /projects/:id/tasks/:task.
- /projects/:id/docs/:doc, /revisions, /restore, /suggestions, /suggestions/:id/approve|reject,
  /ack and /acks. Documents are plan or onboarding.
- /projects/:id/activity, /projects/:id/search; owner global /activity and /search.
- Owner /stream, /portal and /portal/app.js.
The internal scheduled invocation is not a publicly callable API. Existing transport/session
routes and native /send, /inbox, /connect, /connections remain available.

## Verification

- 68 Node tests passed, 0 failed, 0 skipped: 39 foundation/regression tests plus 29 Part 2 tests.
  Integration uses actual workerd, local D1 and SQLite DOs with disposable credentials and RSA Access
  assertions. Coverage includes membership and DM privacy in HTTP, sockets, activity and search;
  announcement owner checks; retry/collision and sender-copy behavior; paged reads; old-history
  backfill; outbox receipt recovery; server liveness and connected health; Late/Down, coverage,
  duplicate tick and return; documents, decisions, restore, immutable history/acks; sub labels;
  nested-secret redaction, size limits; Access-protected assets, JavaScript syntax and SSE lifecycle.
- 8 Python outbound-client tests passed, 0 failed/skipped; networking mocked. Environment/header
  auth, URL/redirect guard, dedup, redaction, literal command-shaped data, cursor persistence,
  invalid response handling and reused backoff are covered.
- Total: 76 tests passed. TypeScript typecheck passed. Wrangler dry-run packaging passed;
  no upload occurred. Existing Wrangler 3.114.17 retained; upgrade warning was not treated as an
  authorization to change the pinned toolchain. git diff --check passed.

## Not verified / boundaries

- No production D1 migration/backfill, live Access setup, live Cron execution or production write test.
- Actual Muse/arin-pc processes were not pointed at this branch. Equivalent native contracts are
  exercised in isolated runtime tests. The new client was not installed or started on an agent.
- Portal HTML/script/API/SSE were tested, including JavaScript parsing. Browser visual interaction
  and a real Cloudflare Access login/cookie round trip were not tested.
- The portal is a functional same-Worker owner console, not a visual port of the earlier React design.
  The old full /api/state and local-only sidecar API are not claimed as drop-in cloud compatibility.
- The exact old FastAPI/sidecar suites are not all portable: local bind/HTTP and HMAC/execution
  dispatch tests describe the superseded runtime. Cloud permission/feature cases were adapted,
  but complete old-suite parity and cloud cutover remain unproven. The local prototype was not retired.
- New projects/channels and documents are empty until the owner creates real records. Existing
  production records were not removed to satisfy a sample-data claim. Membership lists are set
  at creation; no membership-edit or destructive history endpoint is included.
- Rule book, enrollment, command logger/proof, enforcement setting/banner and final onboarding
  criteria remain Parts 3-6. A document acknowledgement alone does not assert onboarded status.

## Arin's check / decisions

1. Review this Part 2 checkpoint and authorize Part 3 (the versioned rule book) to continue.
2. Confirm the coverage policy: healthy peer in the same project, same role preferred, stable id
   fallback; return still-open tasks when the original owner is healthy. No peer means uncovered.
3. Release configuration remains a separate approval: existing hub-origin CORS, Access team/audience/
   owner identity, D1 binding/migrations and Cron. No token needs to be pasted in chat.
4. Retire the old local runtime only after approved cloud deployment and actual agent/portal parity.

## Reproduce safely

In tools/agent-mesh/cloudflare-hub: npm run typecheck; npm test; npm run test:sidecar;
npm exec -- wrangler deploy --dry-run --outdir .test-build. These checks require no production tokens.
Do not run the guarded historical live mutation scripts, deploy, apply remote migrations or use
production bindings for these tests. No production config file was modified.

Enrollment limit retained for later parts: enrollment shows an agent ran the script and answered
correctly. It cannot prove future obedience. Permissions, limits and rate limits are the safety layer.
