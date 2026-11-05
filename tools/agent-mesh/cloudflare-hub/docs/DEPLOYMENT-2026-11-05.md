# Agent Hub production deployment — 2026-11-05

Status: backend deployed; owner portal activation and real-agent acceptance incomplete.

## Release

- Arin authorized deployment and free owner-only Access setup in chat.
- URL: https://agent-mesh-hub.slack-clone-arin.workers.dev
- Source: 0eddfe862ac814a2d30ce9fea4f11e3065051343 on codex/agent-hub-v2-step1.
- Worker version: e08e087c-bdc7-4077-b7db-d811e689ed7f.
- Final deployment: 045491e8-30a8-45d0-b7df-36501169535e (100%).
- Prior version for rollback reference: 1912f91d-da69-4f6a-a497-05b5968295a2.
- Existing DO namespace preserved: d1956624161e401a9ebde3300feb2418.
- D1 agent-hub-v2: 92a8cd91-1178-4fdd-84c2-6dcab3d2e046; migrations 0001–0007 applied, no seed data.
- Cron installed: */5 * * * *. PORTAL_ORIGIN is the same Worker origin.
- Deployed with --keep-vars; both existing secret bindings preserved. No tokens changed.
- No merge, local server retirement or enforcement activation.

Pinned Wrangler 3.114.17 does not understand the new durable_objects.code_update_strategy setting.
The initial outer Worker deploy temporarily reached old coordinator code (v2 returned 404).
Completed using the documented deployments REST API with the same version at 100% and
code_update_strategy.mode=immediate. This may reconnect active sockets. Future deployment tooling
must send that field explicitly or support the new flag; the TOML entry alone is insufficient with v3.
Cloudflare reference: https://developers.cloudflare.com/changelog/post/2026-10-30-durable-objects-deferred-code-updates/

## Verification

Pre-release: 113 Worker + 33 Python = 146 passed, zero failures/skips; typecheck, exact artifact
and dry-run passed. No implementation change since those checks; only production TOML/docs changed.

Live HTTP final checks (9 successful expectations, plus 1 expected scope denial):
- Root 200.
- Missing-auth /connections and /inbox 401; /api/v2/inbox 401.
- Admin settings and portal 503 with Access-not-configured error (fail closed).
- Existing admin credential: /connections 200 and /inbox?since=0&limit=1 200, one real history item.
- Existing agent credential: /api/v2/inbox?since=0&limit=1 200, one history item, API 2-part6.
- Requesting wildcard projects with a project-scoped agent credential correctly returned 403.
- No live messages sent or agent sockets claimed for testing. Connections count was zero.
- Legacy mirroring reports legacy_sync_pending=true; old history remains intact and copies in batches.
- API metadata confirms preserved DO namespace, two secret bindings, D1 binding and Cron schedule.
- Read-only D1 settings query returned no policy rows: source fallback is record_only, not enforce.

Initial Python-default User-Agent requests got edge 403; explicit application User-Agent reached
Worker as above. These edge rejections were not interpreted as application results.

## Owner action and remaining acceptance

Cloudflare Zero Trust Free onboarding reached a form requiring payment details, terms acceptance
and authorization for overage charges. No payment details entered, no terms accepted, no Access app
created. Form is left open in Brave for Arin. Account API OAuth lacks Access permissions; do not
create new tokens or use a master agent token to bypass portal authentication.

After Arin completes activation, create an owner-only self-hosted Access app protecting only
agent-mesh-hub.slack-clone-arin.workers.dev/api/v2/admin/*, allow arin@avinto.no only, and configure
ACCESS_TEAM_DOMAIN, ACCESS_AUD and PORTAL_ADMIN_EMAIL. Keep /send, /inbox, /connect and
/connections outside Access. Admin portal stays locked until actual configuration is supplied.

Still unverified: actual owner login, browser console/SSE, real Muse/arin-pc send/receive/reconnect,
actual scheduled Cron execution, live enrollment/adoption, rulebook/onboarding publishing, full
history mirror completion and cloud cutover parity. No live fixture projects or rules were invented.
Earlier React frontend at the separate agent-hub Worker was not deployed or replaced.
