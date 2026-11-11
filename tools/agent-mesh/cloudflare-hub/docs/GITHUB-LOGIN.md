# Owner GitHub login — 2026-11-05

Arin approved replacing the payment-gated Cloudflare Access setup with GitHub login and explicitly
selected arin-avinto as the only owner. Authenticated connector profile ID: 298677609. The browser
was signed in to leeroyarin; the OAuth application registration is owned by that browser account.
Application ownership does not grant portal access. Only the configured numeric owner ID does.
No Cloudflare Access subscription, payment details or terms acceptance was submitted.

## Configuration and activation

- PORTAL_AUTH_PROVIDER=github (explicitly selects the new provider; Access verification remains
  available for existing installations without that setting).
- GITHUB_CLIENT_ID=Ov23lilesbFo3JQJo1Kf (public application ID).
- GITHUB_OWNER_ID=298677609 (arin-avinto, confirmed by Arin).
- PORTAL_ORIGIN=https://agent-mesh-hub.slack-clone-arin.workers.dev.
- GITHUB_CLIENT_SECRET was entered by Arin into an encrypted Worker secret, never source,
  a command argument, logs or chat. Existing HUB_TOKEN and other secrets stay unchanged.

GitHub application: https://github.com/settings/applications/3994841
Callback: https://agent-mesh-hub.slack-clone-arin.workers.dev/api/v2/auth/github/callback

Arin completed client-secret generation and encrypted Worker entry, then confirmed done.
Credential presence was verified through binding metadata only; its value was never retrieved.
Sign in as arin-avinto, not the app-registration account leeroyarin.

## Routes and security

- GET /api/v2/auth/login: login page; 503 setup message until credential exists.
- GET /api/v2/auth/github/login: identity-only OAuth authorization redirect; PKCE S256, random
  single-use state, ten-minute browser-bound verifier cookie, per-IP/time-window limit 10 starts.
- GET /api/v2/auth/github/callback: atomically consumes state and verifies GitHub /user numeric ID.
  Rejects any nonempty granted scope, wrong owner, expired/mismatched state or callback replay.
- POST /api/v2/auth/logout: same-origin only; revokes hashed session record and clears cookie.
- GET /admin/inbox: redirects to /api/v2/admin/portal; unauthenticated portal redirects to login.
- All /api/v2/admin/* APIs and assets verify the configured provider. Cookie-authenticated writes
  require an exact matching Origin. Agent/master Bearer tokens cannot become portal sessions.

Sessions last eight hours, use random 256-bit cookies with __Host- prefix, Secure, HttpOnly,
SameSite=Lax and Path=/. D1 stores only session/state/verifier hashes and timestamps; provider
access/refresh tokens remain transient in memory, never logged/persisted or exposed to the portal.
Callback responses have no-store, no-referrer and no external assets. No open redirect parameter.
Provider requests are bounded by a 15-second timeout and reject redirects. No private repo scope.
The additive 0008 migration changes no message history or agent identity/token registry.

## Verification and deployment

127 Worker tests + 33 Python tests = 160 passed, 0 failed/skipped. Includes 14 real-workerd/D1 OAuth
security tests with mocked GitHub responses (no live tokens), all 113 existing Worker regressions,
typecheck, exact logger artifact, generated binding types and production dry-run bundle.

Live Worker version: 4551ae52-ac2b-4db1-8857-bac7c51e86b9.
Deployment: 93958be0-f991-4ff3-b604-6672d96c456e (100%, explicit immediate coordinator update).
Migration 0008 applied to the existing D1. Existing DO namespace and two secret bindings preserved.
Cron unchanged at */5 * * * *; enrollment policy remains record-only.

Seven live read-only endpoint expectations passed: /admin/inbox and portal redirect correctly;
login page, GitHub-start route and admin session fail closed (503 while secret absent); existing
admin /connections and per-agent /api/v2/inbox return 200 with original credentials. No test messages
sent or sockets claimed. At this checkpoint GITHUB_CLIENT_SECRET is absent: actual GitHub exchange,
owner browser session/CSRF/logout/SSE and wrong-account rejection are tested locally only, pending
Arin's credential entry and real login. Do not claim authentication activation is complete yet.

Provider reference: https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps


## Activation verified after Arin completed credential entry

Current dashboard-deployed version: 2fa814ad-2537-4512-901a-62d829416881.
Deployment: 4e5e7f09-3b03-405f-9bd3-ddcef2214254, at 100%.
Metadata confirms provider github, owner ID 298677609, encrypted GITHUB_CLIENT_SECRET presence,
and the preserved DO namespace. No credential value read or logged.

Five live HTTP checks passed: login page 200; GitHub-start 303 to github.com; unauthenticated admin
session 401; existing admin /connections 200; existing per-agent v2 inbox 200.
Real browser flow selected arin-avinto from GitHub's account picker and authorized the registered
app with Public data only. Callback returned to the protected portal successfully. Portal UI
showed Live connection active, empty error text, 50 real mesh history entries and zero project
options (no fabricated projects). This verifies actual client-secret exchange, permitted owner
identity, browser session, authenticated portal assets/data and SSE initialization end to end.
The authenticated portal tab is left open for Arin. No messages sent, policies enabled or projects
created for verification. Live logout, session expiry, wrong-owner rejection and CSRF denial remain
covered by local runtime tests, not additional production mutations. Native agent WebSocket and
scheduled Cron execution acceptance remain separate from this owner-login check.
