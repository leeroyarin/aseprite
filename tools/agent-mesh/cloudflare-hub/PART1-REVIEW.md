# Part 1 review checkpoint - 2026-11-05 - Codex

## Result

Part 1 source and local runtime verification are complete. Stop here for Arin's check.
Do not start Part 2, merge, deploy, provision a production D1 binding, or change Access/secrets/tokens.

- Branch: codex/agent-hub-v2-step1, isolated worktree under .codex/worktrees/agent-hub-v2-step1/aseprite2.
- Local baseline tag: agent-mesh-hub-v2.1.0-baseline-20261105 at 70fcbdd51.
  This tags the source baseline; the live root independently advertised 2.1.0. No claim of a
  byte-for-byte production artifact/state backup is made, and the tag was not pushed.
- Production Worker name/address and wrangler.toml are unchanged.
- No local backend server, tunnel, secret material, or seed data was introduced.

## Added routes

POST /api/v2/send; GET /api/v2/inbox; GET /api/v2/admin/session;
POST /api/v2/admin/send; GET /api/v2/admin/inbox, plus CORS preflight.
See API.md for shapes, status codes, limits, and cursor semantics.

## Compatibility and security

Existing HTTP send/inbox, header-authenticated /connect, and /connections pass local regressions.
The portal's signed Access identity is hub; its emitted author is arin. Owner can read all DMs;
agent inbox and fan-out remain identity/project/recipient filtered. Shared channel/doc policies
are tested but wait for Part 2 route integration. No enrollment restriction exists in this part.
Browser CORS was intentionally tightened to the portal origin. Secret-shaped new text is
redacted on both old and new transport entry paths; raw payload/exception/close-reason logging
was removed. Old live mutation test scripts now require explicit approval and redact credentials;
they were NOT run. No existing token was created/revoked/rotated on production.

## Verification

- 39 tests passed, 0 failed, 0 skipped across runtime, persistence, schema and policy tests,
  using real workerd, local D1 and SQLite DOs for integration.
- Both historical live-mutation scripts were checked to refuse execution by default (2/2 guards).
- TypeScript typecheck passed.
- Wrangler 3.114.17 dry-run bundling passed (67.84 KiB before final documentation-only updates).
  No upload occurred. The existing locked toolchain was retained; Miniflare/esbuild were declared
  directly at their already-locked versions, not upgraded.
- Tests cover legacy transport, header WS auth, duplicate/conflicting IDs, cursor order, bounded
  sync, size/UTF-8/chunked uploads, rate limits, CORS, append-only/FK constraints, restart persistence,
  secret redaction, genuine RSA Access verification and spoof/expiry/audience/signature rejection,
  hub/arin identity, DM read-all versus exclusion, and channel/document permission matrices.

## Not verified / outstanding

- No production deployment, production D1 migration/backfill, Access policy setup, or live write test.
- Actual Muse/arin-pc clients against the modified Worker were not exercised; equivalent isolated
  native-client request/WS contracts were tested instead.
- No Part 2-6 routes, Cron supervision, portal stream/assets, rule book, enrollment proof, logger,
  record-only/enforce toggle or onboarding updates yet. Their required tests remain pending.
- The old local prototype has not been used by this implementation or retired. Retirement is gated
  by the owner's requirement for Worker feature/test parity; do not replace it with another PC server.

## Arin's check / decisions

1. Review Part 1 and the proposed 32-KiB body / 64-KiB request, 60-send / 120-inbox per-minute limits.
2. Confirm portal origin (current default is the existing agent-hub portal) and the Access team,
   audience and owner email to configure later. Never paste a token in chat.
3. Production D1 creation/binding, Access changes and deployment remain separately approval-gated.
4. Only after this check, authorize Part 2 in the specified feature order.

## Reproduce safely

From tools/agent-mesh/cloudflare-hub: npm ci; npm run typecheck; npm test.
The test runner supplies only disposable local credentials and fixtures and performs no cloud
mutations. The legacy live test scripts are not part of npm test. The dry-run uses the original
wrangler.toml; production needs the reviewed D1 binding before v2 can become operational.
