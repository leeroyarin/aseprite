import {before,after,it} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {build} from 'esbuild';
import {Miniflare,Log,LogLevel} from 'miniflare';
let mf,db,providerCalls=0,lastExchange,flowCount=0;
const origin='https://worker.test';
const owner='12345'; // Local fixture only; no shell or production credentials.
const cookieOf=(response,name)=>response.headers.get('set-cookie')?.match(new RegExp(name+'=([^;, ]+)'))?.[1];
const flowCookie=v=>'__Host-hub_oauth='+v;
const sessionCookie=v=>'__Host-hub_session='+v;
async function api(path,options={}){return mf.dispatchFetch(origin+path,{redirect:'manual',...options});}
async function start(){
 const r=await api('/api/v2/auth/github/login',{headers:{'CF-Connecting-IP':'192.0.2.'+(++flowCount)}});assert.equal(r.status,303);
 const auth=new URL(r.headers.get('location'));
 return {state:auth.searchParams.get('state'),cookie:flowCookie(cookieOf(r,'__Host-hub_oauth')),auth};
}
async function finish(flow,code='owner'){return api('/api/v2/auth/github/callback?state='+flow.state+'&code='+code,{headers:{Cookie:flow.cookie}});}
before(async()=>{
 const bundle=await build({entryPoints:['src/index.ts'],bundle:true,write:false,format:'esm',platform:'browser',target:'es2022'});
 mf=new Miniflare({modules:true,script:bundle.outputFiles[0].text,compatibilityDate:'2024-11-01',bindings:{HUB_TOKEN:'local-admin-fixture',PORTAL_ORIGIN:origin,PORTAL_AUTH_PROVIDER:'github',GITHUB_OWNER_ID:owner,GITHUB_CLIENT_ID:'fixture-client',GITHUB_CLIENT_SECRET:'fixture-secret'},durableObjects:{HUB_DO:{className:'AgentMeshHub',useSQLite:true}},d1Databases:['HUB_DB'],log:new Log(LogLevel.ERROR),outboundService:async request=>{
  providerCalls++;
  if(request.url==='https://github.com/login/oauth/access_token'){
   lastExchange=new URLSearchParams(await request.text());
   return Response.json({access_token:lastExchange.get('code'),token_type:'bearer',scope:lastExchange.get('code')==='scoped'?'repo':''});
  }
  assert.equal(request.url,'https://api.github.com/user');
  return Response.json({id:request.headers.get('Authorization')==='Bearer outsider'?67890:Number(owner),login:'fixture-owner'});
 }});
 db=await mf.getD1Database('HUB_DB');
 for(const name of ['0001_v2_transport.sql','0002_foundation.sql','0003_project_features.sql','0004_search_backfill.sql','0005_governance.sql','0006_enrollment.sql','0007_enforcement.sql','0008_portal_auth.sql']){
  const sql=await readFile('migrations/'+name,'utf8');
  for(const statement of sql.replace(/^--.*$/gm,'').split(name.startsWith('0004')?/;\s*(?=INSERT)/i:/;\s*(?=CREATE)/i).filter(s=>s.trim()))await db.prepare(statement).run();
 }
});
after(async()=>{await mf?.dispose();});
it('login page serves no-store security headers and identity-only button',async()=>{const r=await api('/api/v2/auth/login');assert.equal(r.status,200);assert.equal(r.headers.get('Cache-Control'),'no-store');assert.match(r.headers.get('Content-Security-Policy'),/frame-ancestors 'none'/);assert.match(await r.text(),/Continue with GitHub/);});
it('legacy admin inbox location leads to protected portal and then sign-in',async()=>{let r=await api('/admin/inbox');assert.equal(r.headers.get('Location'),'/api/v2/admin/portal');r=await api('/api/v2/admin/portal');assert.equal(r.headers.get('Location'),'/api/v2/auth/login');});
it('unauthenticated admin API is denied, bearer and Access headers cannot bypass GitHub',async()=>{const r=await api('/api/v2/admin/session',{headers:{Authorization:'Bearer local-admin-fixture','Cf-Access-Jwt-Assertion':'fake'}});assert.equal(r.status,401);});
it('authorization uses PKCE, fixed callback, no repo scope and secure browser cookie',async()=>{const r=await api('/api/v2/auth/github/login');const auth=new URL(r.headers.get('location'));assert.equal(auth.origin,'https://github.com');assert.equal(auth.searchParams.get('scope'),'');assert.equal(auth.searchParams.get('code_challenge_method'),'S256');assert.equal(auth.searchParams.get('redirect_uri'),origin+'/api/v2/auth/github/callback');assert.match(r.headers.get('set-cookie'),/HttpOnly; Secure; SameSite=Lax/);});
it('callback without matching browser cookie never calls provider',async()=>{const f=await start(),n=providerCalls;const r=await api('/api/v2/auth/github/callback?state='+f.state+'&code=owner');assert.equal(r.status,400);assert.equal(providerCalls,n);});
it('forged state and verifier rejected without provider requests',async()=>{const f=await start(),n=providerCalls;f.cookie=flowCookie('a'.repeat(43));assert.equal((await finish(f)).status,400);assert.equal(providerCalls,n);});
it('expired authorization state cannot be used',async()=>{const f=await start();await db.prepare('UPDATE portal_oauth_states SET expires_at=0 WHERE consumed_at IS NULL').run();assert.equal((await finish(f)).status,400);});
it('wrong account denied despite successful GitHub authorization',async()=>{const f=await start();assert.equal((await finish(f,'outsider')).status,403);});
it('repository-scoped provider tokens rejected',async()=>{const f=await start();assert.equal((await finish(f,'scoped')).status,403);});
it('owner callback creates hashed-only session and one-time state; replay denied',async()=>{
 const f=await start(),r=await finish(f);assert.equal(r.status,303);assert.equal(r.headers.get('location'),'/api/v2/admin/portal');
 const raw=cookieOf(r,'__Host-hub_session');assert.equal(raw.length,43);assert.equal(lastExchange.get('client_secret'),'fixture-secret');assert.equal(lastExchange.get('code_verifier'),f.cookie.split('=')[1]);
 const rows=await db.prepare('SELECT * FROM portal_sessions').all();assert.equal(rows.results.length,1);assert.notEqual(rows.results[0].session_hash,raw);assert.ok(!JSON.stringify(rows.results).includes('fixture-secret'));
 assert.equal((await finish(f)).status,400);
 const session=await api('/api/v2/admin/session',{headers:{Cookie:sessionCookie(raw)}});assert.equal(session.status,200);assert.equal((await session.json()).actor,'arin');
});
it('session forgery, duplicates, expired and revoked sessions denied',async()=>{
 const fake=sessionCookie('x'.repeat(43));assert.equal((await api('/api/v2/admin/session',{headers:{Cookie:fake}})).status,401);
 const r=await finish(await start());assert.equal(r.status,303);const raw=cookieOf(r,'__Host-hub_session');
 assert.equal((await api('/api/v2/admin/session',{headers:{Cookie:sessionCookie(raw)+'; '+sessionCookie(raw)}})).status,401);
 await db.prepare('UPDATE portal_sessions SET expires_at=0').run();assert.equal((await api('/api/v2/admin/session',{headers:{Cookie:sessionCookie(raw)}})).status,401);
});
it('cookie-authenticated owner writes require an explicit same-origin Origin',async()=>{
 const r=await finish(await start());assert.equal(r.status,303);const cookie=sessionCookie(cookieOf(r,'__Host-hub_session'));
 assert.equal((await api('/api/v2/admin/rulebook',{method:'POST',headers:{Cookie:cookie,'Content-Type':'application/json'},body:'{}'})).status,403);
 assert.equal((await api('/api/v2/auth/logout',{method:'POST',headers:{Cookie:cookie,Origin:'https://evil.test'}})).status,403);
 const logout=await api('/api/v2/auth/logout',{method:'POST',headers:{Cookie:cookie,Origin:origin}});assert.equal(logout.status,303);
 assert.equal((await api('/api/v2/admin/session',{headers:{Cookie:cookie}})).status,401);
});
it('login rate limit bounds anonymous state creation',async()=>{for(let i=0;i<10;i++){const r=await api('/api/v2/auth/github/login',{headers:{'CF-Connecting-IP':'192.0.2.45'}});assert.equal(r.status,303);}assert.equal((await api('/api/v2/auth/github/login',{headers:{'CF-Connecting-IP':'192.0.2.45'}})).status,429);});
it('cross-site sign-in cannot allocate state',async()=>{assert.equal((await api('/api/v2/auth/github/login',{headers:{'Sec-Fetch-Site':'cross-site'}})).status,403);});
