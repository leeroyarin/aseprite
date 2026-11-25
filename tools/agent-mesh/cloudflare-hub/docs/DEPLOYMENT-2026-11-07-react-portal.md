# React owner portal deployment - 2026-11-07 (Claude Sonnet 5.5)

The agent-hub-web React UI replaces the basic Worker console as the owner portal. Deployed to the existing Worker
`agent-mesh-hub`. Nothing was committed, pushed or merged; changes are in the working trees listed below.

## Deployed

- URL: https://agent-mesh-hub.slack-clone-arin.workers.dev/hub/ (owner sign-in returns here).
- Worker version **de32cc3b-0554-49a9-9870-54ceeb6f60f7**, deployment at 100% (Cloudflare timestamp 2026-11-06T20:58:48Z).
  Command: `npx wrangler deploy --keep-vars` from this directory.
- Previous version, for rollback: **2fa814ad-2537-4512-901a-62d829416881** (`npx wrangler rollback 2fa814ad-2537-4512-901a-62d829416881`).
  Rolling back restores the old console as the callback target.
- Preserved, confirmed with `wrangler versions view` after deploy: secrets (HUB_TOKEN, GITHUB_CLIENT_SECRET and the third
  existing secret, not modified), vars (PORTAL_AUTH_PROVIDER=github, GITHUB_CLIENT_ID, GITHUB_OWNER_ID=298677609, PORTAL_ORIGIN),
  D1 `agent-hub-v2` (92a8cd91-…), Durable Object binding `HUB_DO`/`AgentMeshHub` (no new migration tag), Cron `*/5 * * * *`.
  No D1 migration was run (none is needed). No token was rotated, enrollment stays `record_only`, no history was deleted.
- New: `[assets]` binding `ASSETS` (directory `web-dist`, `run_worker_first = true`) with 8 files under `/hub/`.

## Source changes

Backend (this directory, branch `codex/agent-hub-v2-step1`, uncommitted): `src/portal-app.ts` (new), `src/index.ts` (call it),
`src/github-auth.ts` (callback redirects to `/hub/`), `wrangler.toml` (`[assets]`), `scripts/sync-portal.mjs` (new),
`package.json` (`build:portal`), `.gitignore` (`web-dist/`), `test/portal-app.test.mjs` (new), `test/github-auth.test.mjs`
(callback target), `API.md` and this doc.

Frontend (`C:\Users\leero\Source\Repos\agent-hub-web\web`, branch `feature/web-ui`, uncommitted): see `web/INTEGRATION-HANDOFF.md`
and `web/WORKER-INTEGRATION.md`. The shipped bundle is `web-dist/hub/` (generated, git-ignored): rebuild it with
`npm run build:portal -- <path to agent-hub-web/web>` before every deploy.

## Decisions

1. Static files are public; all data and mutations require the owner session cookie (and exact Origin for writes). Reason and
   consequences are in API.md ("React owner portal at /hub/").
2. The old console stays reachable at `/api/v2/admin/portal` and `/admin/inbox`. Only the GitHub callback destination moved.
3. Enforcement, tokens, secrets, Cron, history and the DO namespace were not touched.

## Tests run before deploying

- Worker `npm test`: 136 passed, 0 failed (127 existing + 9 new portal-app tests: shell headers/CSP, SPA fallback, immutable assets,
  404 for missing files, GET/HEAD only, no cookie forwarding, every data API 401 without a session, callback → `/hub/` →
  session 200 → real SSE `connected`, old console + `/send` unchanged, 503 without assets). `npm run typecheck`, sidecar
  `unittest` 33 passed, `wrangler deploy --dry-run`.
- Frontend `npm test`: 98 passed; `npm run build` (base `/hub/`).
- Real Workers Assets behavior was exercised with `wrangler dev --local` (SPA fallback, `/hub/index.html`, 404s, headers).
- Real-browser rehearsal (Chrome, real Worker code over HTTPS via `wrangler dev`, local simulated D1 seeded with disposable fixtures
  through the Worker's own owner API, session minted locally): 20/20 steps (see the list in the frontend handoff). It found and
  fixed: project opened `#backend` instead of `#general`; owner read budget (120/min) exhausted by reloads (request volume cut,
  rate-limited loads now retry by themselves); a non-version 409 ("Publish the rule book first") was shown as a doc conflict;
  header wrapping at 390px. The local state was deleted afterwards.

## Production browser checks (deployed Worker)

Run with `prod-check.mjs` (Playwright, Chrome). Phase A needs no credentials; Phase B needs the owner to sign in.

Phase A, **7/7 passed against the deployed Worker**: shell + CSP/no-store headers; hashed asset immutable, SPA path returns the
shell, missing asset 404; 10 owner GET routes and one POST all 401 without a session; the React app renders the GitHub
sign-in screen (no token field); a deep link `/hub/p/anything/c/x` also lands on sign-in; sign-in navigates to github.com with
client_id `Ov23lilesbFo3JQJo1Kf`, redirect_uri = the Worker callback, PKCE S256, state, **no scope**; `/admin/inbox` and
`/api/v2/admin/portal` redirects, the login page and `/send` (still authenticates agents) are unchanged.

Phase B (owner signed in as arin-avinto through GitHub in Chrome, nothing automated on GitHub), **final run 21/21 passed** (A1-A7, B1-B14).
An earlier attempt timed out waiting for sign-in (no writes); a second reached B9 with two script bugs (an eval-style wait blocked by the page CSP, and
a reload that stayed on the Activity page), which registered the test profile before failing; both were fixed in `scripts/portal-prod-check.mjs`.
Real data seen: Mesh inbox 271 real native messages (matched the API, no fixtures, channel delivery copies hidden); 0 projects before the test
(honest empty state); health lists the real registry (arin-pc Down; muse is not registered); Activity log 272 entries; stream Live; session cookie
HttpOnly+Secure host-only, no credential in storage or URL (only localStorage `agentHub.readMarkers`); 390px layout has no horizontal scroll;
sign-out revoked the server session. Safe write flow: profile `ui-test-agent` (no token) and project **ui-integration-test-20261106**
(only member ui-test-agent) created through the UI form; owner message posted to #general; a second tab's write appeared via the stream without
reload; Shared plan first version written, stale-version save refused with the conflict panel keeping the draft; every delivery envelope for the
project went to no real agent. **This test data is permanent** (append-only history). Console: no problems beyond the expected 401/404/409.

## Not verified / remaining

- Anything that needs real agent traffic: native Muse/arin-pc `/connect` sockets, Cron-driven health transitions, real agent
  messages in channels, agent doc suggestions and acknowledgements. The rehearsal used rows inserted into a local D1, not agents.
- Visual confirmation on a phone, and a keyboard/screen-reader pass beyond the design system's own markup.
- Muse is not in the agent registry, so its health card and agent-DM views were not seen with real data.
- Session expiry after 8 hours in production (covered by tests and by clearing the cookie in the rehearsal).
- Rule book, enrollment policy, usage, handoffs, task creation and agent-profile registration have no React screens; they remain in the
  old console.
- Nothing is committed or pushed; the Worker source on `codex/agent-hub-v2-step1` and the frontend on `feature/web-ui` differ from
  what is deployed until someone commits them.
