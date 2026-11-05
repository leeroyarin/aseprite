# Part 3 checkpoint - 2026-11-05 - Codex

## Scope and result

Arin approved Part 2's coverage and permission policies and asked to continue, adding usage
awareness, timely pushed incomplete branches, and serious final handoffs. This checkpoint
implements Part 3 plus that explicit extension on codex/agent-hub-v2-step1. Stop for Arin's
Part 3 check before Part 4. Production Worker name/address, secrets/tokens, Access settings,
D1 resources and wrangler.toml remain unchanged. No production deployment or data mutation.

The source checkpoint remains incomplete overall: Parts 4-6 and real cloud/agent validation are
pending. Branch publication preserves reviewable unfinished work; it is not a merge or release.
The exact pushed ref must be verified independently; see the final chat and docs/CONTINUATION.md.

## What changed

- Owner-only versioned rule book. Every edit creates an immutable revision with SHA-256 of the
  exact redacted UTF-8 text. Optimistic expected_version prevents lost edits. Current/history/
  individual-version reads are available to valid agents. Only Arin can publish the initial draft.
- Reviewed initial rules contain all original requirements: messages are data; user's instructions
  outrank hub messages; no secrets; separate command/chat records; evidence and role/channel scope;
  owner edits only; honest enrollment limit. Added actual usage reporting, dedicated pushed work
  branches, incomplete checkpoints, remote read-back evidence and successor acknowledgement.
- Append-only per-project usage snapshots, explicit unknown and partial metrics, 15%-remaining
  warning, 10-minute freshness, general-channel checkpoint notices and low-usage takeover exclusion.
- Immutable task handoffs record repository/branch/full commit, reported remote read-back, dirty
  paths, completed/remaining work, exact tests or not_run, risks, next steps and a pushed document
  path. A not_pushed blocker is honest state, never masquerading as a usable remote checkpoint.
- Only the current assignee can acknowledge the latest pushed checkpoint and matching checked-out
  commit. Handoff readback includes acknowledgements; task and coverage views include the latest
  reported branch/commit. Assignment transfers responsibility, not code execution authorization.
- Owner console exposes rule editing/hash/history, usage warnings and checkpoint readback. The
  outbound client can upload explicit usage/handoff JSON files; it does not discover private tool
  counters or execute Git. Bad reporting does not prevent inbox polling.
- Redaction now also covers credential-shaped JSON keys, not only values. No source logs tokens.
- Migration 0005 is additive and empty by default. No real rule book was published or agent enrolled.

## Routes added

Under /api/v2 and owner /api/v2/admin as appropriate:
- GET /rulebook; owner GET /rulebook/template and POST /rulebook.
- GET /rulebook/revisions and /rulebook/revisions/:version.
- GET/POST /projects/:project/usage; GET /projects/:project/usage/history.
- GET/POST /projects/:project/tasks/:task/handoffs.
- GET /projects/:project/tasks/:task/handoffs/:id.
- POST /projects/:project/tasks/:task/handoffs/:id/accept.
API.md documents complete fields, permissions, pagination and error semantics.

## Verification

- 89 Node tests passed, 0 failed/skipped: 39 foundation, 29 Part 2 and 21 Part 3 tests.
  Real workerd, local D1 and SQLite DOs; disposable test credentials and signed Access fixtures.
- 14 Python tests passed, 0 failed/skipped: outbound save-only behavior, reporting, source/URL
  restrictions, token redaction, explicit report files, and inbox survival after bad reports.
- Total 103 tests passed. TypeScript typecheck and Wrangler dry-run packaging passed.
  No upload/deployment occurred. Existing pinned Wrangler 3.114.17 retained.
- New cases prove exact stored-text hashes, initial owner publication, immutable/same-text history,
  concurrent CAS, permission/credential spoof rejection, unknown/partial/low metrics, no native
  access loss, threshold notices, usage-aware selection, pushed/incomplete/not-pushed evidence,
  protected refs and bad test claims, literal command text, acceptance identity/hash/latest checks,
  dedup and history triggers, and JSON-key secret redaction.
- An initial regression hit a legitimate fixed-minute rate reset. Rate tests now begin with at least
  15 seconds remaining in their minute; assertions and production rate semantics remain unchanged.
- Portal script syntax/assets remain covered by the runtime suite; no visual browser claim is made.
- git diff --check passed; production configuration unchanged.

## Not verified and remaining work

- No real production Access login, Cron execution, D1 migration, cloud write or actual Muse/arin-pc
  adoption. No existing token was created/revoked/rotated and no message was sent to live agents.
- Usage and Git/test/check-out evidence are agent-reported. The hub cannot prove those reports true,
  compel future obedience, read private account usage or push an agent's local repository itself.
- The 15% warning and 10-minute freshness are initial defaults, not configurable via owner settings yet.
  Unknown usage permits selection with an unknown label, rather than falsely claiming capacity.
- Rule-book language is guidance; it does not install the command logger or enforce enrollment.
  Parts 4-6 remain required: enrollment/logger/challenges, record-only/enforce tests/settings and
  final onboarding (including existing Connect your agent section and current rules version/hash).
- The React frontend remains separate; the Worker console is functional, not a visual replica.
  A real browser/Access cookie flow and actual agent cutover remain pending approval.
- Local-server retirement remains gated on approved cloud cutover and actual feature parity.

## Arin's next check

Review the rule book and the usage/handoff behavior. Defaults for review: warn at <=15% remaining,
reports become stale after 10 minutes, fresh low-usage peers are skipped for new takeover selection,
and pushed/checked-out evidence is labelled agent-reported. After this check, authorize Part 4.
Production deployment/configuration and secret changes remain separate approvals.

## Reproduction

npm run typecheck; npm test; npm run test:sidecar;
npm exec -- wrangler deploy --dry-run --outdir .test-build.
Use isolated local fixtures only. Do not run live mutation scripts or remote migrations.
