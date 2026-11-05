# Agent Hub unfinished branch handoff

Status: INCOMPLETE. Parts 1-4 are implemented and tested; Parts 5-6 remain required.
Source repository: https://github.com/leeroyarin/aseprite
Work branch: codex/agent-hub-v2-step1
Baseline tag: agent-mesh-hub-v2.1.0-baseline-20261105 at 70fcbdd51 (source baseline only).

## Verify the actual checkpoint

The exact commit cannot be embedded in a file contained in that same commit. Read the final
chat's verified push result and independently inspect the remote branch before continuing:
`git ls-remote origin refs/heads/codex/agent-hub-v2-step1`.
Inspect that full commit, branch diff and working tree. Do not assume this document proves a push.
Preserve concurrent work and do not reset the shared Aseprite checkout or alter third_party.

The implementation lives in tools/agent-mesh/cloudflare-hub. Original shared checkout has
pre-existing third_party/fmt content and .wrangler/; those are unrelated and excluded.
No production deploy, D1 provisioning/migration, Access change, secret rotation or local server
retirement has occurred. The Worker name/address and existing native endpoints must stay intact.

## Completed

- Parts 1-2 foundation, private project channels, registry/supervision/tasks, versioned docs,
  sub labels, activity/search, same-Worker Access console and stream. See PART1/2-REVIEW.md.
- Part 3 owner-published immutable rule book with exact UTF-8 SHA-256 and version CAS.
- Arin's added usage awareness and final-handoff protocol: real/unknown metrics, warning and
  fresh low-usage selection, append-only branch/checkpoint/test evidence and successor receipts.
- Part 4 exact logger artifact, path evidence/install, immutable enrollment accept/challenge/proof/audit,
  private one-use packet delivery and owner revoke/allow in record-only mode.
- Optional outbound report-file uploads; no local server, automatic execution or guessed paths.
- Permission policies confirmed by Arin: read-all owner, owner-only announcements and doc approval.

## Tests and evidence

109 Worker tests + 32 Python tests = 141 passed, 0 failed/skipped. Typecheck and local dry-run pass.
Run from tools/agent-mesh/cloudflare-hub: npm run typecheck; npm test; npm run test:sidecar.
Dry-run only: npm exec -- wrangler deploy --dry-run --outdir .test-build.
Fixtures run actual workerd/local D1/DO; external networking in the client tests is mocked.
No test result proves production or actual agent adoption. Never use live tokens for these tests.

## Remaining work and gates

1. Part 4 enrollment/logger/challenges are implemented; review PART4-REVIEW.md and docs/ENROLLMENT.md.
   Arin approved Part 3 and the 15%/10-minute defaults. Wait for the Part 4 check.
2. After that check, Part 5 is record_only default, owner settings/grace, append-only enrollment audit,
   periodic proof and enforce tests only in isolation. Never enable enforce in production.
3. Stop for check; Part 6 onboarding includes actual rule-book version/hash, connected + acknowledged
   document + successful challenge criteria, and preserves Connect your agent.
4. Obtain separate approval for production config/resources/deploy. Verify real Access browser flow,
   actual Muse/arin-pc traffic, live Cron and parity before retiring the local prototype.
5. Earlier React design integration is outstanding. The Worker owner console currently provides
   working API views; no claim of a visual port or browser/Access end-to-end validation is made.

## Risks and decisions

Usage is reported, not privately inferred. Unknown remains unknown. Initial warning is <=15%
remaining; freshness is 10 minutes. Remote-ref/test/checkout evidence is also agent-reported,
not independently verified by the hub. Pushing a branch does not prove task completion.
Do not merge or deploy this incomplete branch. Read docs/USER-REQUIREMENTS.md as task authority,
API.md for contracts and PART4-REVIEW.md for scope/limitations. Record exact failures and blockers.
