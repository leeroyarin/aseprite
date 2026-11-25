import {before,after,it} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {build} from 'esbuild';
import {Miniflare,Log,LogLevel} from 'miniflare';
// The React owner portal is served from static assets at /hub/. These tests use a fake ASSETS
// binding that mimics Workers Assets (html_handling auto-trailing-slash, 404 for missing files);
// the fixtures are local only: no live tokens, no GitHub, no production data.
let mf,withoutAssets,db,assetRequests=[];
const origin='https://worker.test';
const owner='12345';
const files={
 '/hub/':{type:'text/html; charset=utf-8',body:'<!doctype html><title>Agent Hub</title><div id="root"></div>'},
 '/hub/assets/index-abc123.js':{type:'text/javascript',body:'console.log("app")'},
 '/hub/ds/bundle.js':{type:'text/javascript',body:'window.AgentHub={}'},
 '/hub/favicon.svg':{type:'image/svg+xml',body:'<svg/>'},
};
async function fakeAssets(request){
 assetRequests.push({url:request.url,cookie:request.headers.get('Cookie'),authorization:request.headers.get('Authorization')});
 const path=new URL(request.url).pathname;
 if(path==='/hub/index.html')return new Response(null,{status:307,headers:{Location:'/hub/'}}); // what Workers Assets does
 const file=files[path];
 return file?new Response(request.method==='HEAD'?null:file.body,{headers:{'Content-Type':file.type}}):new Response('Not found',{status:404});
}
const api=(path,options={})=>mf.dispatchFetch(origin+path,{redirect:'manual',...options});
const cookieOf=(response,name)=>response.headers.get('set-cookie')?.match(new RegExp(name+'=([^;, ]+)'))?.[1];
const options=extra=>({modules:true,compatibilityDate:'2024-11-01',bindings:{HUB_TOKEN:'local-admin-fixture',PORTAL_ORIGIN:origin,PORTAL_AUTH_PROVIDER:'github',GITHUB_OWNER_ID:owner,GITHUB_CLIENT_ID:'fixture-client',GITHUB_CLIENT_SECRET:'fixture-secret'},durableObjects:{HUB_DO:{className:'AgentMeshHub',useSQLite:true}},d1Databases:['HUB_DB'],log:new Log(LogLevel.ERROR),
 outboundService:async request=>request.url==='https://github.com/login/oauth/access_token'?Response.json({access_token:'owner',token_type:'bearer',scope:''}):Response.json({id:Number(owner),login:'fixture-owner'}),...extra});
before(async()=>{
 const bundle=await build({entryPoints:['src/index.ts'],bundle:true,write:false,format:'esm',platform:'browser',target:'es2022'});
 const script=bundle.outputFiles[0].text;
 mf=new Miniflare({...options({serviceBindings:{ASSETS:fakeAssets}}),script});
 withoutAssets=new Miniflare({...options({}),script});
 db=await mf.getD1Database('HUB_DB');
 for(const name of ['0001_v2_transport.sql','0002_foundation.sql','0003_project_features.sql','0004_search_backfill.sql','0005_governance.sql','0006_enrollment.sql','0007_enforcement.sql','0008_portal_auth.sql']){
  const sql=await readFile('migrations/'+name,'utf8');
  for(const statement of sql.replace(/^--.*$/gm,'').split(name.startsWith('0004')?/;\s*(?=INSERT)/i:/;\s*(?=CREATE)/i).filter(s=>s.trim()))await db.prepare(statement).run();
 }
});
after(async()=>{await mf?.dispose();await withoutAssets?.dispose();});

it('serves the app shell at /hub/ with no-store, a restrictive CSP and nosniff; /hub redirects to /hub/',async()=>{
 const r=await api('/hub/');assert.equal(r.status,200);assert.match(r.headers.get('Content-Type'),/text\/html/);
 assert.equal(r.headers.get('Cache-Control'),'no-store');
 const csp=r.headers.get('Content-Security-Policy');
 assert.match(csp,/default-src 'self'/);assert.match(csp,/script-src 'self'/);assert.match(csp,/frame-ancestors 'none'/);assert.match(csp,/connect-src 'self'/);
 assert.doesNotMatch(csp,/script-src[^;]*unsafe/);assert.doesNotMatch(csp,/script-src[^;]*\*/);
 assert.equal(r.headers.get('X-Content-Type-Options'),'nosniff');
 assert.match(await r.text(),/id="root"/);
 const bare=await api('/hub?x=1');assert.equal(bare.status,308);assert.equal(bare.headers.get('Location'),'/hub/?x=1');
});
it('client-side routes (SPA navigation) answer with the shell, including encoded channel ids',async()=>{
 for(const path of ['/hub/p/my-proj','/hub/p/my-proj/c/my-proj%3Ageneral','/hub/p/my-proj/doc/plan','/hub/health','/hub/new']){
  const r=await api(path);assert.equal(r.status,200,path);assert.match(await r.text(),/id="root"/,path);
 }
});
it('hashed assets are immutable, unhashed ones revalidate, and a missing file is a real 404 (never HTML)',async()=>{
 const hashed=await api('/hub/assets/index-abc123.js');assert.equal(hashed.status,200);assert.equal(hashed.headers.get('Cache-Control'),'public, max-age=31536000, immutable');assert.match(hashed.headers.get('Content-Type'),/javascript/);
 assert.equal(hashed.headers.get('Content-Security-Policy'),null);
 assert.equal((await api('/hub/ds/bundle.js')).headers.get('Cache-Control'),'no-cache');
 const missing=await api('/hub/assets/index-old.js');assert.equal(missing.status,404);assert.doesNotMatch(await missing.text(),/id="root"/);
 assert.equal((await api('/hub/favicon.svg')).status,200);
});
it('only GET and HEAD are served; HEAD has no body',async()=>{
 const post=await api('/hub/',{method:'POST',body:'x'});assert.equal(post.status,405);assert.equal(post.headers.get('Allow'),'GET, HEAD');
 const head=await api('/hub/',{method:'HEAD'});assert.equal(head.status,200);assert.equal(await head.text(),'');
});
it('never forwards cookies or credentials to the asset store',async()=>{
 assetRequests=[];await api('/hub/',{headers:{Cookie:'__Host-hub_session='+'a'.repeat(43),Authorization:'Bearer x'}});
 assert.ok(assetRequests.length>0);for(const r of assetRequests){assert.equal(r.cookie,null);assert.equal(r.authorization,null);}
});
it('the shell is public but every data API still requires the owner session',async()=>{
 assert.equal((await api('/hub/')).status,200); // explicit decision: compiled UI only, no data
 for(const path of ['/api/v2/admin/session','/api/v2/admin/projects','/api/v2/admin/agents','/api/v2/admin/inbox','/api/v2/admin/activity','/api/v2/admin/stream','/api/v2/admin/messages']){
  const r=await api(path);assert.equal(r.status,401,path);
 }
 const bearer=await api('/api/v2/admin/projects',{headers:{Authorization:'Bearer local-admin-fixture'}});assert.equal(bearer.status,401);
});
it('GitHub callback lands on the React app, whose session route then answers 200 with the cookie',async()=>{
 const start=await api('/api/v2/auth/github/login',{headers:{'CF-Connecting-IP':'192.0.2.9'}});assert.equal(start.status,303);
 const state=new URL(start.headers.get('location')).searchParams.get('state');
 const flow='__Host-hub_oauth='+cookieOf(start,'__Host-hub_oauth');
 const done=await api('/api/v2/auth/github/callback?state='+state+'&code=owner',{headers:{Cookie:flow}});
 assert.equal(done.status,303);assert.equal(done.headers.get('Location'),'/hub/');
 const cookie='__Host-hub_session='+cookieOf(done,'__Host-hub_session');
 const session=await api('/api/v2/admin/session',{headers:{Cookie:cookie}});assert.equal(session.status,200);
 assert.equal((await api('/api/v2/admin/projects',{headers:{Cookie:cookie}})).status,200);
 assert.deepEqual((await (await api('/api/v2/admin/projects',{headers:{Cookie:cookie}})).json()).projects,[]);
 // Same-origin SSE: the cookie is enough, and the stream is an owner-only event stream.
 const stream=await api('/api/v2/admin/stream',{headers:{Cookie:cookie}});assert.equal(stream.status,200);assert.match(stream.headers.get('Content-Type'),/text\/event-stream/);
 const reader=stream.body.getReader();const first=new TextDecoder().decode((await reader.read()).value);assert.match(first,/^event: connected/);await reader.cancel();
});
it('the basic console stays available at its old URL and legacy routes are untouched',async()=>{
 const legacy=await api('/api/v2/admin/portal');assert.equal(legacy.status,303);assert.equal(legacy.headers.get('Location'),'/api/v2/auth/login');
 assert.equal((await api('/admin/inbox')).headers.get('Location'),'/api/v2/admin/portal');
 assert.equal((await api('/api/v2/auth/login')).status,200);
 assert.equal((await api('/send',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})).status,401); // native route still authenticates agents
});
it('a deployment without static assets fails closed with a clear 503 instead of an HTML page',async()=>{
 const r=await withoutAssets.dispatchFetch(origin+'/hub/',{redirect:'manual'});assert.equal(r.status,503);
 assert.match((await r.json()).error,/not deployed/);
});
