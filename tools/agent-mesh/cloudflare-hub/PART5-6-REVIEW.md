# Combined Parts 5 and 6 review - 2026-11-05 - Codex

The user explicitly requested both final phases together. Source implementation is complete
for this checkpoint; production release and real-agent validation remain separate approvals.
Branch: codex/agent-hub-v2-step1. No merge, production deploy, remote migration, token change or
live-agent message was performed. Production wrangler.toml and hub name/address stay unchanged.

## Part 5

- Empty/default state is record_only. Owner-only immutable versioned settings add enforce/grace
  and proof interval (default 24 hours). Portal controls require an explicit owner load/save.
- Valid unenrolled/failed/revoked agents retain access in record_only. Enforce after grace checks
  native send/inbox/connect, authenticated work routes and sockets. Rule-book/enrollment recovery
  stays reachable. Owner revoke initiates close 4003 and removes the active server connection.
- Before-fan-out and supervisor checks reconcile expired/grace-ended sockets. No production
  setting was enabled. Tests restore record_only in their disposable local databases.
- Due proofs become Pending and the existing scheduled supervisor issues fresh private challenges,
  using existing acceptance/path/script records. Owner/rule/logger changes require actual new
  acceptance/install evidence, never an automatic claim. No history or credentials are deleted.
- Outbound recovery reads the private enrollment inbox on a denied work inbox and keeps its normal
  cursor intact. Explicit enrollment files remain deliberate uploads, never automatic execution.

## Part 6

- Owner publication creates a real onboarding revision from the current published rule book,
  embedding its actual version/hash/text and enrollment steps. It explains path discovery, separate
  command logs, non-execution, record-only expectations, future enforcement and the honest limit.
- Existing custom Connect your agent content is retained; new installations get the cloud-only
  HTTPS/WebSocket/header instructions. Owner should review old custom environment examples.
- Onboarded requires an observed token-authenticated connection, current document acknowledgement,
  active current-rule proof and current rules in the published document. Positive and negative
  cases are tested. No unpublished draft or acknowledgement alone claims onboarded status.

## Added routes

- GET/POST /api/v2/admin/enrollment/settings.
- GET /api/v2/enrollment/challenges (private recovery).
- GET /api/v2/enrollment/onboarding?project=ID (pre-enrollment document read).
- GET /api/v2/projects/:project/onboarding.
- POST /api/v2/admin/projects/:project/onboarding/publish.
- GET /api/v2/projects/:project/onboarding/status (self; owner supports agent query).
See API.md for fields, cursors, controls and failure semantics. Migration 0007 is additive,
with append-only settings and a connection observation projection; it seeds no real data.

## Checks

113 Node/runtime tests and 33 Python tests pass: 146 total, 0 failed/skipped. TypeScript typecheck,
artifact consistency and Wrangler dry-run packaging pass. Existing tests cover all earlier phases.
The combined tests add enforce/grace/revoke, actual fresh renewal receipt, positive/negative
onboarding and retained custom connection text. Source uses real workerd/local D1/DO plus installed
Python subprocesses; outbound Python network tests are mocked. No Aseprite C++ build is relevant.

## Still not verified / not released

- Real Cloudflare Access/browser/cookies, actual rejected WebSocket handshakes, production Cron,
  D1 migrations/backfill, Muse/arin-pc installation/adoption and production writes.
- Rejected Upgrade test transport on pinned Miniflare produces ECONNRESET; the pre-upgrade guard
  is asserted through /connect, while accepted live sessions exercise revoke closure server-side.
- Source tests prove code behavior in isolation, not real agent obedience or remote filesystem/Git
  claims. The earlier React design remains separate from the functional Worker console.
- The local prototype is not retired: approved deployed-cloud parity and actual agent cutover are
  still required first. This checkpoint does not authorize that operational retirement or deployment.

## Release decisions

No implementation phase approval remains between Parts 5 and 6. Arin must separately approve
production resources/configuration/deployment. Before release, supply approved D1 binding and
Access configuration, retain native route access at the edge, review the portal origin, apply all
additive migrations and keep mode record_only. Publish the real rule book and onboarding through
the owner identity, then test actual agents and the browser. Do not enable enforce by default.

Reproduce: npm run typecheck; npm test; npm run test:sidecar;
npm exec -- wrangler deploy --dry-run --outdir .test-build. No live tokens are required.
