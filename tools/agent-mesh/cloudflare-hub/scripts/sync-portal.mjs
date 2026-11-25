// Copies the production build of the React owner portal (the agent-hub-web project) into
// web-dist/hub/, which wrangler.toml serves as static assets under /hub/.
//
//   node scripts/sync-portal.mjs <path-to-agent-hub-web/web> [--build]
//   AGENT_HUB_WEB_DIR=<path> npm run build:portal
//
// --build runs `npm ci --no-audit --no-fund` (only if node_modules is missing) and `npm run build`
// in the web project first. The build must have been made with base "/hub/" (the web project's
// vite.config.js does this); the script refuses a build with any other base so asset URLs cannot
// silently break. web-dist/ is generated and git-ignored.
import { spawnSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const web = resolve(args.find((a) => !a.startsWith('--')) || process.env.AGENT_HUB_WEB_DIR || '');
if (!args.length && !process.env.AGENT_HUB_WEB_DIR) {
  console.error('Pass the agent-hub-web/web directory or set AGENT_HUB_WEB_DIR.');
  process.exit(2);
}
if (!existsSync(join(web, 'package.json'))) {
  console.error('Not a web project: ' + web);
  process.exit(2);
}

const run = (cmd, a) => {
  const r = spawnSync(cmd, a, { cwd: web, stdio: 'inherit', shell: process.platform === 'win32' });
  if (r.status !== 0) process.exit(r.status || 1);
};
if (args.includes('--build')) {
  if (!existsSync(join(web, 'node_modules'))) run('npm', ['ci', '--no-audit', '--no-fund']);
  run('npm', ['run', 'build']);
}

const dist = join(web, 'dist');
const index = join(dist, 'index.html');
if (!existsSync(index)) {
  console.error('No build found at ' + dist + '. Run with --build or `npm run build` in the web project.');
  process.exit(2);
}
const html = readFileSync(index, 'utf8');
const refs = [...html.matchAll(/(?:src|href)="([^"]+)"/g)].map((m) => m[1]).filter((u) => u.startsWith('/'));
if (!refs.length || refs.some((u) => !u.startsWith('/hub/'))) {
  console.error('The build does not use the /hub/ base path: ' + refs.join(', '));
  process.exit(2);
}

// Empty the target's contents rather than the directories themselves: on Windows a running
// `wrangler dev` holds a watch on web-dist, and removing the directory fails with EPERM.
const target = join(root, 'web-dist', 'hub');
mkdirSync(target, { recursive: true });
for (const entry of readdirSync(target)) rmSync(join(target, entry), { recursive: true, force: true });
cpSync(dist, target, { recursive: true });

const count = (dir) => readdirSync(dir).reduce((n, f) => {
  const p = join(dir, f);
  return n + (statSync(p).isDirectory() ? count(p) : 1);
}, 0);
console.log('Portal build copied to ' + target + ' (' + count(target) + ' files).');
