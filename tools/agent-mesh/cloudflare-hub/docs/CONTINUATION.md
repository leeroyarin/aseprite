# Agent Hub unfinished branch handoff

Status: INCOMPLETE. Parts 1-6 are deployed; GitHub credential activation and real-agent/browser acceptance remain required. See DEPLOYMENT-2026-11-05.md.
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
Arin approved production deployment and free owner Access setup. The Worker is deployed and all
seven additive D1 migrations are applied. Arin rejected payment setup and approved GitHub login instead. The new provider is deployed;
Arin must enter the new OAuth client secret directly into Cloudflare. See GITHUB-LOGIN.md. No secret rotation or local-server retirement occurred. Preserve the
Worker name/address, existing native endpoints and DO namespace.

## Completed

- Parts 1-2 foundation, private project channels, registry/supervision/tasks, versioned docs,
  sub labels, activity/search, same-Worker Access console and stream. See PART1/2-REVIEW.md.
- Part 3 owner-published immutable rule book with exact UTF-8 SHA-256 and version CAS.
- Arin's added usage awareness and final-handoff protocol: real/unknown metrics, warning and
  fresh low-usage selection, append-only branch/checkpoint/test evidence and successor receipts.
- Part 4 exact logger artifact, path evidence/install, immutable enrollment accept/challenge/proof/audit,
  private one-use packet delivery and owner revoke/allow in record-only mode.
- Parts 5-6 owner policy/grace, periodic proof/recovery, enforced socket guards and current-rule
  onboarding with connection + acknowledgement + proof criteria. Record-only remains default.
- Optional outbound report-file uploads; no local server, automatic execution or guessed paths.
- Permission policies confirmed by Arin: read-all owner, owner-only announcements and doc approval.

## Tests and evidence

127 Worker tests + 33 Python tests = 160 passed, 0 failed/skipped. Typecheck and local dry-run pass.
Run from tools/agent-mesh/cloudflare-hub: npm run typecheck; npm test; npm run test:sidecar.
Dry-run only: npm exec -- wrangler deploy --dry-run --outdir .test-build.
Fixtures run actual workerd/local D1/DO; external networking in the client tests is mocked.
No test result proves production or actual agent adoption. Never use live tokens for these tests.

## Remaining work and gates

Parts 5 and 6 were authorized together and are implemented. See PART5-6-REVIEW.md and
ONBOARDING.md. Record-only remains the source default. No production enforcement was enabled.

1. Complete the GitHub secret-entry handoff in docs/GITHUB-LOGIN.md. Owner account is arin-avinto
   (298677609), explicitly confirmed by Arin. Eight migrations and the GitHub-provider code are
   deployed. Never return to the rejected Cloudflare payment setup or relax owner authentication.
2. Publish the real rule book and per-project onboarding after reviewing existing connection notes.
3. Verify actual GitHub owner login/browser flow, Cron, native Muse/arin-pc traffic, enrollment and cloud parity.
4. Retire the local prototype only after approved real cloud cutover. Earlier React design
   integration remains separate; this branch serves a functional Worker console.

## Risks and decisions

Usage is reported, not privately inferred. Unknown remains unknown. Initial warning is <=15%
remaining; freshness is 10 minutes. Remote-ref/test/checkout evidence is also agent-reported,
not independently verified by the hub. Pushing a branch does not prove task completion.
Do not merge this incomplete branch or enable enforcement. Production deployment was authorized;
GitHub activation remains blocked on the user credential-entry step. Read docs/USER-REQUIREMENTS.md as task authority,
API.md for contracts and PART5-6-REVIEW.md for scope/limitations. Record exact failures and blockers.
