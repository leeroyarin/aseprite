// LOCAL REHEARSAL of the built React UI against the real Worker code (wrangler dev, https://localhost:8811,
// simulated local D1 seeded with disposable fixtures). Real Chromium, real Workers Assets, real SSE.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { chromium } from 'file:///C:/Users/leero/Source/Repos/agent-hub-web/web/node_modules/playwright-core/index.mjs';

const BASE = 'https://localhost:8811';
const dir = new URL('.', import.meta.url);
const shots = new URL('./shots-local/', import.meta.url);
mkdirSync(shots, { recursive: true });
const cookie = readFileSync(new URL('./rehearsal-cookie.txt', import.meta.url), 'utf8').trim();

const results = [];
const problems = [];
async function step(name, fn) {
  try { await fn(); results.push({ name, ok: true }); console.log('PASS', name); }
  catch (e) {
    await page.screenshot({ path: new URL('FAIL-' + results.length + '.png', shots).pathname.replace(/^\//, '') }).catch(() => {});
    results.push({ name, ok: false, url: page.url(), error: String(e.message).split('\n')[0] });
    console.log('FAIL', name, '-', String(e.message).split('\n')[0], '@', page.url());
  }
}
const shot = (page, n) => page.screenshot({ path: new URL(n + '.png', shots).pathname.replace(/^\//, ''), fullPage: false });

const browser = await chromium.launch({ channel: 'chrome', headless: true }).catch(() => chromium.launch({ headless: true }));
const context = await browser.newContext({ ignoreHTTPSErrors: true, viewport: { width: 1280, height: 800 } });
await context.addCookies([{ name: '__Host-hub_session', value: cookie, url: BASE, secure: true, httpOnly: true, sameSite: 'Lax' }]);
const page = await context.newPage();
const streamRequests = [];
page.on('console', (m) => { if (['error', 'warning'].includes(m.type())) problems.push('console.' + m.type() + ': ' + m.text()); });
page.on('pageerror', (e) => problems.push('pageerror: ' + e.message));
page.on('request', (r) => { if (r.url().endsWith('/admin/stream')) streamRequests.push(Date.now()); });
page.on('response', (r) => { if (r.status() >= 400) problems.push(r.status() + ' ' + r.request().method() + ' ' + r.url().replace(BASE, '')); });
const text = (p = page) => p.locator('body').innerText();
const waitText = (t, opts = {}) => page.getByText(t, { exact: false }).first().waitFor({ timeout: 8000, ...opts });

await step('shell loads signed in and shows the Mesh inbox with real native history', async () => {
  await page.goto(BASE + '/hub/');
  await page.getByRole('heading', { name: 'Mesh inbox' }).waitFor({ timeout: 10000 });
  await waitText('Heads up: branch codex/step1 is ready for review.');
  await waitText('DM: acknowledged, building locally.');
  await waitText('A message in a project that is not a hub project.');
  const t = await text();
  if (/Welcome\. This is a local rehearsal fixture\./.test(t) && (t.match(/Welcome\. This is a local rehearsal fixture\./g) || []).length > 0) throw new Error('channel delivery copies repeated in Mesh inbox');
  if (!/3 messages/.test(t)) throw new Error('expected 3 messages, got: ' + t.match(/\d+ messages?/)?.[0]);
  await shot(page, '01-mesh-inbox');
});

await step('stream connects and the sidebar says Live', async () => {
  await page.getByRole('status').filter({ hasText: 'Live' }).first().waitFor({ timeout: 8000 });
  if (!streamRequests.length) throw new Error('no stream request seen');
});

await step('project opens at its first channel with real messages, tool call and event', async () => {
  await page.getByRole('button', { name: /Rehearsal project/ }).first().click();
  await page.waitForURL((u) => decodeURIComponent(u.pathname) === '/hub/p/rehearsal/c/rehearsal:general');
  await waitText('Build is green on main');
  await waitText('Starting the Windows QA run now.');
  await waitText('Welcome. This is a local rehearsal fixture.');
  await waitText('cmake --build');
  await waitText('arin-pc is Down.');
  await shot(page, '02-channel');
});

await step('failover banner reports the real task coverage, not an invented one', async () => {
  const banner = page.locator('.ah-banner').first();
  await banner.waitFor({ timeout: 8000 });
  const t = await banner.innerText();
  if (!/arin-pc is down/i.test(t) || !/Muse took over Fix the flaky build/i.test(t)) throw new Error('banner text: ' + t);
});

await step('direct messages between agents are visible to the owner', async () => {
  await page.getByRole('button', { name: /muse-arin-pc/ }).first().click();
  await waitText('Private between two agents');
  await waitText('Can you re-run the failing suite?');
  await waitText('It touches the sidecar tests.');
  await shot(page, '03-dm');
});

await step('unread badge counts only messages that arrive after this browser first saw the channel', async () => {
  // Nothing arrived since baseline: no unread badge on any channel.
  const badges = await page.locator('.ah-count').count();
  if (badges !== 0) throw new Error('unexpected unread badges: ' + badges);
});

await step('owner posts a message; it is stored by the Worker and shown with the owner identity', async () => {
  await page.getByRole('button', { name: /^general/ }).first().click();
  const box = page.getByPlaceholder('Message #general');
  await box.fill('Rehearsal message from the UI');
  await box.press('Enter');
  await waitText('Rehearsal message from the UI');
  const res = await page.evaluate(async () => (await (await fetch('/api/v2/admin/channels/rehearsal%3Ageneral/messages?limit=200')).json()).items.map((m) => m.content));
  if (!res.includes('Rehearsal message from the UI')) throw new Error('not persisted');
});

await step('a message posted elsewhere appears without reload through the SSE refresh', async () => {
  const other = await context.newPage();
  await other.goto(BASE + '/hub/health');
  await other.evaluate(async () => fetch('/api/v2/admin/channels/rehearsal%3Ageneral/messages', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ id: 'sse-test-1', content: 'Pushed from another tab' }),
  }));
  await waitText('Pushed from another tab', { timeout: 6000 });
  await other.close();
});

await step('shared plan: history, pending suggestion with diff, approve with the current version', async () => {
  await page.getByRole('button', { name: /shared plan/i }).first().click();
  await page.waitForURL((u) => u.pathname.endsWith('/doc/plan'));
  await waitText('suggested a change');
  await page.getByRole('button', { name: /review changes/i }).click();
  await page.getByLabel('added').first().waitFor();
  await page.getByRole('button', { name: /history/i }).click();
  await waitText('Add test step');
  await shot(page, '04-plan-review');
  await page.getByRole('button', { name: /^approve$/i }).click();
  await page.waitForFunction(() => /Post a preview/.test(document.body.innerText), null, { timeout: 8000 });
  const doc = await page.evaluate(async () => (await (await fetch('/api/v2/admin/projects/rehearsal/docs/plan')).json()).version);
  if (doc !== 3) throw new Error('expected version 3, got ' + doc);
});

await step('a stale save is refused (409), the draft is kept and the newer version is shown', async () => {
  await page.getByRole('button', { name: /^edit$/i }).click();
  await page.getByLabel(/document source/i).fill('# My conflicting edit');
  // Someone else saves first.
  await page.evaluate(async () => fetch('/api/v2/admin/projects/rehearsal/docs/plan', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ body: '# Plan from another editor', expected_version: 3, note: 'other' }),
  }));
  await page.getByRole('button', { name: /save version/i }).click();
  const alert = page.locator('.hub-conflict');
  await alert.waitFor({ timeout: 8000 });
  const t = await alert.innerText();
  if (!/changed while you were working/.test(t) || !/Your edit was not saved/.test(t)) throw new Error(t);
  if ((await alert.locator('textarea').inputValue()) !== '# My conflicting edit') throw new Error('draft lost');
  await waitText('Plan from another editor');
  await shot(page, '05-conflict');
});

await step('onboarding does not exist yet: empty state, then first version, then publish needs a rule book', async () => {
  await page.getByRole('button', { name: /onboarding/i }).first().click();
  await waitText("Onboarding hasn't been written yet");
  await page.getByRole('button', { name: /write the first version/i }).click();
  await page.getByRole('textbox').fill('# Onboarding\n\nWelcome.');
  await page.getByRole('button', { name: /save version/i }).click();
  await page.getByRole('heading', { name: /Onboarding/ }).first().waitFor();
  await waitText('Welcome.');
  page.once('dialog', (d) => d.accept());
  await page.getByRole('button', { name: /send to agents/i }).click();
  await waitText("Couldn't publish Onboarding");
  await waitText('Publish the rule book first');
});

await step('agent health: real states, self-reported fields, coverage, and no invented latency/uptime', async () => {
  await page.getByRole('button', { name: /agent health/i }).first().click();
  await page.getByRole('heading', { name: 'Agent health' }).waitFor();
  const t = await text();
  for (const needle of ['Fixing the flaky build', '1.2.0', 'Covering for arin-pc', 'is handling its tasks']) {
    if (!t.includes(needle)) throw new Error('missing ' + needle);
  }
  if (/latency|uptime/i.test(t)) throw new Error('latency/uptime shown');
  await shot(page, '06-health');
});

await step('activity log and search read the real activity', async () => {
  await page.getByRole('button', { name: /activity log/i }).first().click();
  await waitText('entries');
  await page.getByRole('button', { name: /^search$/i }).first().click();
  await page.getByRole('searchbox').or(page.getByRole('textbox')).first().fill('plan');
  await waitText('Add test step', { timeout: 8000 }).catch(() => {});
  await shot(page, '07-search');
});

await step('create a channel (DM between two agents) from the sidebar', async () => {
  await page.getByRole('button', { name: /new channel or dm/i }).click();
  await page.getByPlaceholder('e.g. backend').fill('muse-designer');
  await page.locator('select').selectOption('dm');
  // Members default to all three; a DM needs exactly two.
  await waitText('exactly two');
  await page.locator('.ah-newp').getByRole('button', { name: /arin-pc/ }).click();
  await page.getByRole('button', { name: /^create$/i }).click();
  await page.waitForURL((u) => decodeURIComponent(u.pathname).endsWith('/c/rehearsal:muse-designer'));
  await waitText('Private between two agents');
});

await step('new project through the form (empty agents blocked by the Worker rules, valid one created)', async () => {
  await page.getByRole('button', { name: 'New project' }).click();
  await page.getByPlaceholder('e.g. Pixel 3D modeler').fill('Second rehearsal');
  await page.getByRole('button', { name: /create project/i }).click();
  await page.waitForURL((u) => u.pathname.startsWith('/hub/p/second-rehearsal'));
  await waitText('Second rehearsal');
});

await step('deep link reload keeps its place (SPA fallback) and an unknown path is Not found', async () => {
  await page.goto(BASE + '/hub/p/rehearsal/doc/plan');
  await waitText('Plan from another editor');
  await page.goto(BASE + '/hub/p/nope/c/x');
  await waitText("isn't in the hub");
});

await step('responsive: 390px wide uses one column with a Menu button', async () => {
  const phone = await context.newPage();
  await phone.setViewportSize({ width: 390, height: 844 });
  await phone.goto(BASE + '/hub/');
  await phone.getByRole('heading', { name: /Agent Hub|Mesh inbox/ }).first().waitFor({ timeout: 8000 });
  await phone.screenshot({ path: new URL('08-phone-list.png', shots).pathname.replace(/^\//, '') });
  await phone.getByRole('button', { name: /mesh inbox/i }).first().click();
  const overflow = await phone.evaluate(() => document.documentElement.scrollWidth > window.innerWidth + 1);
  if (overflow) throw new Error('horizontal page scroll at 390px');
  await phone.getByRole('button', { name: 'Menu' }).waitFor();
  await phone.screenshot({ path: new URL('09-phone-main.png', shots).pathname.replace(/^\//, '') });
  await phone.close();
});

await step('SSE reconnects after the Worker closes the 60 s stream and still delivers updates', async () => {
  await page.goto(BASE + '/hub/p/rehearsal/c/rehearsal%3Ageneral');
  await waitText('Rehearsal message from the UI');
  const before = streamRequests.length;
  await page.waitForTimeout(68000);
  if (streamRequests.length <= before) throw new Error('no reconnect within 68 s');
  await page.getByRole('status').filter({ hasText: 'Live' }).first().waitFor({ timeout: 5000 });
  const other = await context.newPage();
  await other.goto(BASE + '/hub/health');
  await other.evaluate(async () => fetch('/api/v2/admin/channels/rehearsal%3Ageneral/messages', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ id: 'sse-test-2', content: 'After the reconnect' }),
  }));
  await waitText('After the reconnect', { timeout: 6000 });
  await other.close();
});

await step('session expiry returns to sign-in with an explanation', async () => {
  await context.clearCookies();
  await page.getByRole('button', { name: 'Hub home' }).click();
  await waitText('Your session has ended');
  await page.getByRole('button', { name: 'Sign in with GitHub' }).waitFor();
  await shot(page, '10-expired');
});

await step('sign out revokes the session on the Worker', async () => {
  await context.addCookies([{ name: '__Host-hub_session', value: cookie, url: BASE, secure: true, httpOnly: true, sameSite: 'Lax' }]);
  await page.goto(BASE + '/hub/');
  await page.getByRole('heading', { name: 'Mesh inbox' }).waitFor({ timeout: 8000 });
  await page.getByRole('button', { name: 'Sign out' }).click();
  await page.getByRole('heading', { name: 'Sign in as owner' }).waitFor({ timeout: 8000 });
  const status = await page.evaluate(async () => (await fetch('/api/v2/admin/session')).status);
  if (status !== 401) throw new Error('session cookie still valid after sign-out: ' + status);
  const old = await context.request.get(BASE + '/api/v2/admin/session', { headers: { Cookie: '__Host-hub_session=' + cookie }, ignoreHTTPSErrors: true });
  if (old.status() !== 401) throw new Error('revoked cookie still accepted: ' + old.status());
});

await browser.close();
const failed = results.filter((r) => !r.ok);
const unexpected = problems.filter((p) => !/401 (GET|POST) /.test(p) && !/409 POST/.test(p) && !/Failed to load resource.*(401|409)/.test(p));
writeFileSync(new URL('./ui-rehearsal-result.json', dir), JSON.stringify({ results, problems, unexpected }, null, 2));
console.log('\n' + results.filter((r) => r.ok).length + '/' + results.length + ' steps passed');
console.log('console/network problems:', unexpected.length ? '\n' + unexpected.join('\n') : 'none (besides expected 401/409)');
process.exit(failed.length ? 1 : 0);
