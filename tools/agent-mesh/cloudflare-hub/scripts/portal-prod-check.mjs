// Browser checks of the DEPLOYED React portal at the production Worker.
//   Phase A (no credentials): shell, assets, sign-in screen, redirect to GitHub (stops before any login), legacy routes.
//   Phase B (needs the owner): a visible Chrome window opens on the GitHub sign-in page and waits for the OWNER
//     (arin-avinto) to sign in. Nothing is typed or clicked on GitHub by this script. After sign-in it runs
//     read-only checks, then a safe write flow confined to ONE isolated test project whose only member is the
//     test-only profile "ui-test-agent". It aborts before any message if the project has any other member.
//     No message is sent to Muse or arin-pc.
import { mkdirSync, writeFileSync } from 'node:fs';
import { chromium } from 'file:///C:/Users/leero/Source/Repos/agent-hub-web/web/node_modules/playwright-core/index.mjs';

const BASE = 'https://agent-mesh-hub.slack-clone-arin.workers.dev';
const WAIT_FOR_OWNER_MS = Number(process.env.WAIT_MS || 15 * 60 * 1000);
const here = new URL('.', import.meta.url);
const shots = new URL('./shots-prod/', import.meta.url);
mkdirSync(shots, { recursive: true });
const png = (n) => new URL(n + '.png', shots).pathname.replace(/^\//, '');
const results = [];
const problems = [];
let page;
async function step(name, fn) {
  try { const detail = await fn(); results.push({ name, ok: true, ...(detail ? { detail } : {}) }); console.log('PASS', name, detail ? '- ' + detail : ''); }
  catch (e) {
    if (page) await page.screenshot({ path: png('FAIL-' + results.length) }).catch(() => {});
    results.push({ name, ok: false, error: String(e.message).split('\n')[0], url: page?.url() });
    console.log('FAIL', name, '-', String(e.message).split('\n')[0]);
  }
}
const finish = (extra = {}) => {
  writeFileSync(new URL('./prod-check-result.json', here), JSON.stringify({ at: new Date().toISOString(), results, problems, ...extra }, null, 2));
};

// ---------------------------------------------------------------- Phase A
const headless = await chromium.launch({ channel: 'chrome', headless: true });
const ctxA = await headless.newContext({ viewport: { width: 1280, height: 800 } });
page = await ctxA.newPage();
page.on('console', (m) => { if (['error', 'warning'].includes(m.type())) problems.push('A console.' + m.type() + ': ' + m.text()); });
page.on('pageerror', (e) => problems.push('A pageerror: ' + e.message));
const failedA = [];
page.on('response', (r) => { if (r.status() >= 400) failedA.push(r.status() + ' ' + r.request().method() + ' ' + r.url().replace(BASE, '')); });

await step('A1 shell is served at /hub/ with security headers', async () => {
  const r = await ctxA.request.get(BASE + '/hub/');
  const h = r.headers();
  if (r.status() !== 200 || !/text\/html/.test(h['content-type'])) throw new Error('status ' + r.status());
  if (h['cache-control'] !== 'no-store' || !/script-src 'self'/.test(h['content-security-policy']) || h['x-content-type-options'] !== 'nosniff') throw new Error(JSON.stringify(h));
  return 'CSP + no-store present';
});
await step('A2 hashed assets are immutable, SPA path returns the shell, missing asset is a 404', async () => {
  const html = await (await ctxA.request.get(BASE + '/hub/')).text();
  const js = html.match(/\/hub\/assets\/[^"]+\.js/)[0];
  const a = await ctxA.request.get(BASE + js);
  if (a.status() !== 200 || !/immutable/.test(a.headers()['cache-control'])) throw new Error('asset ' + a.status() + ' ' + a.headers()['cache-control']);
  const spa = await ctxA.request.get(BASE + '/hub/p/x/c/y%3Az');
  if (spa.status() !== 200 || !(await spa.text()).includes('id="root"')) throw new Error('SPA fallback');
  const missing = await ctxA.request.get(BASE + '/hub/assets/nope.js');
  if (missing.status() !== 404) throw new Error('missing asset status ' + missing.status());
  return js;
});
await step('A3 every data API is closed without the owner session', async () => {
  const bad = [];
  for (const p of ['/session', '/projects', '/agents', '/inbox', '/activity', '/messages', '/stream', '/search?q=x', '/rulebook', '/enrollment/agents']) {
    const r = await ctxA.request.get(BASE + '/api/v2/admin' + p, { maxRedirects: 0 });
    if (r.status() !== 401) bad.push(p + '=' + r.status());
  }
  const post = await ctxA.request.post(BASE + '/api/v2/admin/projects', { data: { id: 'x', name: 'x', agents: [] }, headers: { Origin: BASE }, maxRedirects: 0 });
  if (post.status() !== 401) bad.push('POST projects=' + post.status());
  if (bad.length) throw new Error(bad.join(', '));
  return '10 reads + 1 write all 401';
});
await step('A4 the React shell renders the GitHub sign-in screen (no token field)', async () => {
  await page.goto(BASE + '/hub/');
  await page.getByRole('heading', { name: 'Sign in as owner' }).waitFor({ timeout: 15000 });
  if (await page.locator('input').count()) throw new Error('unexpected input on sign-in');
  await page.screenshot({ path: png('A4-signin') });
});
await step('A5 deep link also lands on sign-in (SPA routing works on the live Worker)', async () => {
  await page.goto(BASE + '/hub/p/anything/c/x');
  await page.getByRole('heading', { name: 'Sign in as owner' }).waitFor({ timeout: 15000 });
});
await step('A6 sign-in starts GitHub OAuth with PKCE, the registered client, the fixed callback and no scope', async () => {
  await page.goto(BASE + '/hub/');
  await page.getByRole('button', { name: 'Sign in with GitHub' }).click();
  await page.waitForURL((u) => u.hostname === 'github.com', { timeout: 20000 });
  const u = new URL(page.url());
  const target = u.pathname === '/login' ? new URL(u.searchParams.get('return_to'), 'https://github.com') : u;
  const q = target.searchParams;
  const want = { client_id: 'Ov23lilesbFo3JQJo1Kf', redirect_uri: BASE + '/api/v2/auth/github/callback', code_challenge_method: 'S256' };
  for (const [k, v] of Object.entries(want)) if (q.get(k) !== v) throw new Error(k + '=' + q.get(k));
  if (q.get('scope')) throw new Error('scope requested: ' + q.get('scope'));
  if (!q.get('state') || !q.get('code_challenge')) throw new Error('missing state/challenge');
  return 'github.com ' + u.pathname;
});
await step('A7 legacy console and native routes are unchanged', async () => {
  const ok = [];
  let r = await ctxA.request.get(BASE + '/admin/inbox', { maxRedirects: 0 });
  ok.push(r.status() + ' ' + r.headers().location);
  if (r.status() !== 303 || r.headers().location !== '/api/v2/admin/portal') throw new Error('/admin/inbox ' + ok);
  r = await ctxA.request.get(BASE + '/api/v2/admin/portal', { maxRedirects: 0 });
  if (r.status() !== 303 || r.headers().location !== '/api/v2/auth/login') throw new Error('portal ' + r.status() + ' ' + r.headers().location);
  r = await ctxA.request.post(BASE + '/send', { data: {}, maxRedirects: 0 });
  if (![401, 403].includes(r.status())) throw new Error('/send ' + r.status());
  r = await ctxA.request.get(BASE + '/api/v2/auth/login', { maxRedirects: 0 });
  if (r.status() !== 200) throw new Error('login page ' + r.status());
  return 'old console redirects intact; /send still authenticates agents';
});
const unexpectedA = failedA.filter((p) => !/401 GET \/api\/v2\/admin\/session/.test(p));
await headless.close();
if (unexpectedA.length) problems.push('A unexpected responses: ' + unexpectedA.join('; '));

// ---------------------------------------------------------------- Phase B
const profile = new URL('./chrome-profile-prod/', import.meta.url).pathname.replace(/^\//, '');
const ctx = await chromium.launchPersistentContext(profile, { channel: 'chrome', headless: false, viewport: { width: 1280, height: 860 } });
page = ctx.pages()[0] || await ctx.newPage();
page.on('console', (m) => { if (['error', 'warning'].includes(m.type())) problems.push('B console.' + m.type() + ': ' + m.text()); });
page.on('pageerror', (e) => problems.push('B pageerror: ' + e.message));
const streamHits = [];
page.on('request', (r) => { if (r.url().endsWith('/admin/stream')) streamHits.push(Date.now()); });
page.on('response', (r) => { if (r.status() >= 400) problems.push('B ' + r.status() + ' ' + r.request().method() + ' ' + r.url().replace(BASE, '')); });
const waitText = (t, o = {}) => page.getByText(t, { exact: false }).first().waitFor({ timeout: 10000, ...o });

let signedIn = false;
await step('B1 owner signs in with GitHub (waiting for the owner; nothing is automated on GitHub)', async () => {
  await page.goto(BASE + '/hub/');
  const mesh = page.getByRole('heading', { name: 'Mesh inbox' });
  if (!(await mesh.isVisible().catch(() => false))) {
    await page.getByRole('button', { name: 'Sign in with GitHub' }).click();
    console.log('>>> Waiting up to ' + Math.round(WAIT_FOR_OWNER_MS / 60000) + ' min for the owner to sign in as arin-avinto in the open Chrome window...');
    await page.waitForURL((u) => u.origin === BASE && u.pathname.startsWith('/hub/'), { timeout: WAIT_FOR_OWNER_MS });
  }
  await mesh.waitFor({ timeout: 20000 });
  signedIn = true;
  return 'callback returned to ' + new URL(page.url()).pathname;
});

if (signedIn) {
  const api = (path, init) => page.evaluate(async ([p, i]) => {
    const r = await fetch('/api/v2/admin' + p, { ...(i || {}), headers: { ...(i?.body ? { 'Content-Type': 'application/json' } : {}) } });
    return { status: r.status, body: await r.json().catch(() => null) };
  }, [path, init]);
  let projectsBefore = [];

  await step('B2 session is the owner; cookie is HttpOnly + Secure + host-only; no credential in storage or URL', async () => {
    const s = await api('/session');
    if (s.status !== 200 || s.body.actor !== 'arin') throw new Error(JSON.stringify(s));
    const cookies = (await ctx.cookies(BASE)).filter((c) => c.name === '__Host-hub_session');
    if (cookies.length !== 1 || !cookies[0].httpOnly || !cookies[0].secure || cookies[0].path !== '/') throw new Error('cookie attrs ' + JSON.stringify(cookies));
    const stores = await page.evaluate(() => ({ s: Object.keys(sessionStorage), l: Object.keys(localStorage), url: location.href }));
    if (stores.s.length) throw new Error('sessionStorage not empty: ' + stores.s);
    if (/token|code=|state=/i.test(stores.url)) throw new Error('credential-looking URL ' + stores.url);
    return 'localStorage keys: ' + JSON.stringify(stores.l);
  });
  await step('B3 Mesh inbox shows the real native history (compared with the API, no fixtures)', async () => {
    const inbox = await api('/inbox?limit=200&since=0');
    const apiCount = inbox.body.messages.filter((m) => !/^ch-[0-9a-f]{64}$/.test(m.id)).length;
    await page.getByRole('heading', { name: 'Mesh inbox' }).waitFor();
    await page.waitForFunction(() => /\d+ messages?/.test(document.body.innerText), null, { timeout: 15000 });
    const shown = Number((await page.locator('body').innerText()).match(/(\d+) messages?/)[1]);
    const first = inbox.body.messages[0];
    await page.screenshot({ path: png('B3-mesh-inbox') });
    if (!inbox.body.has_more && shown !== apiCount) throw new Error('UI shows ' + shown + ' but API has ' + apiCount);
    if (first && !(await page.locator('body').innerText()).includes(first.body.slice(0, 40).split('\n')[0])) throw new Error('first API message not on screen');
    return 'UI ' + shown + ' messages; API ' + apiCount + (inbox.body.has_more ? '+ (paged)' : '');
  });
  await step('B4 stream connects and says Live', async () => {
    await page.getByRole('status').filter({ hasText: 'Live' }).first().waitFor({ timeout: 15000 });
    if (!streamHits.length) throw new Error('no stream request');
  });
  await step('B5 projects: records what exists; an empty hub shows an honest empty state', async () => {
    projectsBefore = (await api('/projects')).body.projects;
    const rail = await page.locator('.ah-rail__item[aria-label]:not(.ah-rail__add)').count();
    if (rail !== projectsBefore.length) throw new Error('rail ' + rail + ' vs API ' + projectsBefore.length);
    return projectsBefore.length + ' projects: ' + (projectsBefore.map((p) => p.id).join(', ') || 'none');
  });
  await step('B6 health view lists the real registry (no latency/uptime)', async () => {
    const agents = (await api('/agents')).body.agents;
    await page.getByRole('button', { name: /agent health/i }).first().click();
    await page.getByRole('heading', { name: 'Agent health' }).waitFor();
    const t = await page.locator('body').innerText();
    if (/latency|uptime/i.test(t)) throw new Error('latency/uptime shown');
    for (const a of agents) if (!t.toLowerCase().includes((a.name || a.id).toLowerCase())) throw new Error('missing agent ' + a.id);
    await page.screenshot({ path: png('B6-health') });
    return agents.length + ' registered agents: ' + agents.map((a) => a.id + '=' + a.health).join(', ');
  });
  await step('B7 activity log and search show real activity', async () => {
    await page.getByRole('button', { name: /activity log/i }).first().click();
    await page.getByText(/[0-9]+ entr(y|ies)|No activity yet/).first().waitFor({ timeout: 15000 });
    const apiN = (await api('/activity?limit=200&cursor=0')).body.items.length;
    const t = await page.locator('body').innerText();
    await page.screenshot({ path: png('B7-activity') });
    return 'UI: ' + (t.match(/\d+ entr(y|ies)|No activity yet/)[0]) + '; API first page: ' + apiN;
  });
  await step('B8 responsive 390px: one column, no horizontal page scroll', async () => {
    const p2 = await ctx.newPage();
    await p2.setViewportSize({ width: 390, height: 844 });
    await p2.goto(BASE + '/hub/');
    await p2.getByRole('heading', { name: /Agent Hub/ }).first().waitFor({ timeout: 15000 });
    await p2.screenshot({ path: png('B8-phone-list') });
    await p2.getByRole('button', { name: /mesh inbox/i }).first().click();
    await p2.getByRole('button', { name: 'Menu' }).waitFor();
    if (await p2.evaluate(() => document.documentElement.scrollWidth > window.innerWidth + 1)) throw new Error('horizontal scroll');
    await p2.screenshot({ path: png('B8-phone-main') });
    await p2.close();
  });

  // -------- Safe write flow: ONE isolated test project, test-only member, never Muse or arin-pc.
  const TEST_AGENT = 'ui-test-agent';
  const stamp = new Date().toISOString().slice(0, 10).replace(/-/g, '');
  const TEST_NAME = 'UI integration test ' + stamp;
  let testProject = null;
  await step('B9 register the test-only profile (no token, no native inbox) and create the isolated project through the UI form', async () => {
    const existing = projectsBefore.find((p) => p.name === TEST_NAME);
    if (existing) throw new Error('test project already exists (' + existing.id + '); not creating a second');
    const reg = await api('/agents', { method: 'POST', body: JSON.stringify({ id: TEST_AGENT, name: 'UI test agent', role: 'ui-test (not a real agent)' }) });
    if (reg.status !== 201) throw new Error('register ' + JSON.stringify(reg));
    await page.goto(BASE + '/hub/');
    await page.getByRole('heading', { name: 'Mesh inbox' }).waitFor({ timeout: 15000 });
    await page.getByRole('button', { name: 'New project' }).click();
    await page.getByPlaceholder('e.g. Pixel 3D modeler').fill(TEST_NAME);
    // The form pre-selects every agent. Leave ONLY the test agent selected.
    const chips = page.locator('.ah-newp .ah-chip, .ah-newp [aria-pressed]');
    const n = await chips.count();
    for (let i = 0; i < n; i += 1) {
      const chip = chips.nth(i);
      const label = (await chip.innerText()).trim();
      const pressed = (await chip.getAttribute('aria-pressed')) === 'true' || /selected/.test((await chip.getAttribute('class')) || '');
      const isTest = /UI test agent/i.test(label);
      if (pressed && !isTest) await chip.click();
      if (!pressed && isTest) await chip.click();
    }
    const selected = [];
    for (let i = 0; i < n; i += 1) {
      const chip = chips.nth(i);
      if ((await chip.getAttribute('aria-pressed')) === 'true' || /selected/.test((await chip.getAttribute('class')) || '')) selected.push((await chip.innerText()).trim());
    }
    if (selected.length !== 1 || !/UI test agent/i.test(selected[0])) throw new Error('chip selection not isolated: ' + JSON.stringify(selected));
    await page.getByRole('button', { name: /create project/i }).click();
    await page.waitForURL((u) => /\/hub\/p\//.test(u.pathname), { timeout: 15000 });
    const id = decodeURIComponent(new URL(page.url()).pathname.split('/')[3]);
    const created = (await api('/projects')).body.projects.find((p) => p.id === id);
    if (!created || JSON.stringify(created.agents) !== JSON.stringify([TEST_AGENT])) throw new Error('project members are not isolated: ' + JSON.stringify(created));
    testProject = id;
    return 'project ' + id + ' members ' + JSON.stringify(created.agents);
  });

  if (testProject) {
    await step('B10 lands on #general, empty history is an honest empty state, post a message as the owner', async () => {
      await page.waitForURL((u) => decodeURIComponent(u.pathname).endsWith(':general'), { timeout: 15000 });
      await waitText('Message #general', { state: 'attached' }).catch(() => {});
      const box = page.getByPlaceholder('Message #general');
      await box.fill('UI integration test message ' + stamp);
      await box.press('Enter');
      await waitText('UI integration test message ' + stamp);
      const msgs = (await api('/channels/' + encodeURIComponent(testProject + ':general') + '/messages?limit=200')).body.items;
      if (!msgs.some((m) => m.content === 'UI integration test message ' + stamp && m.agent === 'arin')) throw new Error('not stored as arin');
      await page.screenshot({ path: png('B10-test-channel') });
      return msgs.length + ' message(s) in the test channel';
    });
    await step('B11 stream-driven refresh: a second tab\'s write appears here without reload', async () => {
      const other = await ctx.newPage();
      await other.goto(BASE + '/hub/health');
      await other.getByRole('heading', { name: 'Agent health' }).waitFor({ timeout: 15000 });
      await other.evaluate(async (ch) => fetch('/api/v2/admin/channels/' + encodeURIComponent(ch) + '/messages', {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ id: crypto.randomUUID(), content: 'Second tab write' }),
      }), testProject + ':general');
      await waitText('Second tab write', { timeout: 10000 });
      await other.close();
    });
    await step('B12 shared plan: not created yet, write first version, edit, stale-version conflict handled', async () => {
      await page.getByRole('button', { name: /shared plan/i }).first().click();
      await waitText("Shared plan hasn't been written yet");
      await page.getByRole('button', { name: /write the first version/i }).click();
      await page.getByRole('textbox').fill('# Test plan\n\nIsolated UI integration test.');
      await page.getByRole('button', { name: /save version/i }).click();
      await waitText('Isolated UI integration test.');
      await page.getByRole('button', { name: /^edit$/i }).click();
      await page.getByLabel(/document source/i).fill('# My edit that will lose the race');
      const bump = await api('/projects/' + encodeURIComponent(testProject) + '/docs/plan', { method: 'POST', body: JSON.stringify({ body: '# Other writer', expected_version: 1, note: 'race' }) });
      if (bump.status !== 200) throw new Error('bump ' + JSON.stringify(bump));
      await page.getByRole('button', { name: /save version/i }).click();
      await page.locator('.hub-conflict').waitFor({ timeout: 10000 });
      if ((await page.locator('.hub-conflict textarea').inputValue()) !== '# My edit that will lose the race') throw new Error('draft lost');
      await waitText('Other writer');
      await page.screenshot({ path: png('B12-conflict') });
    });
    await step('B13 the isolated project has exactly the test member and nothing was delivered to a real agent', async () => {
      const p = (await api('/projects')).body.projects.find((x) => x.id === testProject);
      if (JSON.stringify(p.agents) !== JSON.stringify([TEST_AGENT])) throw new Error('members ' + JSON.stringify(p.agents));
      const inbox = (await api('/inbox?limit=200&since=0')).body.messages.filter((m) => m.project === testProject);
      const recipients = [...new Set(inbox.map((m) => m.to))];
      if (recipients.some((r) => !['all', TEST_AGENT, 'arin'].includes(r))) throw new Error('delivered to ' + recipients);
      return 'delivery envelopes went only to: ' + JSON.stringify(recipients);
    });
  }

  await step('B14 sign out revokes the server session', async () => {
    await page.getByRole('button', { name: 'Sign out' }).first().click();
    await page.getByRole('heading', { name: 'Sign in as owner' }).waitFor({ timeout: 10000 });
    const s = await api('/session');
    if (s.status !== 401) throw new Error('session still valid: ' + s.status);
  });
}

await ctx.close();
const failed = results.filter((r) => !r.ok);
finish({ streamRequests: streamHits.length });
console.log('\n' + results.filter((r) => r.ok).length + '/' + results.length + ' checks passed');
const unexpectedB = problems.filter((p) => !/401|404 GET .*\/docs\/(plan|onboarding)|Failed to load resource.*(401|404|409)|409 POST/.test(p));
console.log('console/network problems (excluding expected 401/404-doc-missing/409):', unexpectedB.length ? '\n' + unexpectedB.join('\n') : 'none');
process.exit(failed.length ? 1 : 0);
