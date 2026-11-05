import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, sep } from 'node:path';
import { build } from 'esbuild';
import { Miniflare, Log, LogLevel } from 'miniflare';
import { generateKeyPairSync, createSign } from 'node:crypto';

const portal='https://agent-hub.slack-clone-arin.workers.dev';
// Disposable local fixtures only. Do not load .dev.vars, shell credentials, or live tokens.
const admin='local-fixture-admin-only';
let mf, db, workerScript, tokens={};
async function migrate(database) {
  for(const name of ['0001_v2_transport.sql','0002_foundation.sql','0003_project_features.sql','0004_search_backfill.sql','0005_governance.sql']) {
    const migration=await readFile('migrations/'+name,'utf8');
    for(const statement of migration.replace(/^--.*$/gm,'').split(name.startsWith('0004') ? /;\s*(?=INSERT)/i : /;\s*(?=CREATE)/i).filter(s=>s.trim())) await database.prepare(statement).run();
  }
}
let privateKey, publicJwk;
const accessDomain='fixture-team.cloudflareaccess.com';
const accessAudience='local-fixture-audience';
const owner='owner-fixture@example.test';
function accessJwt(changes={}, headerChanges={}) {
  const now=Math.floor(Date.now()/1000);
  const header=Buffer.from(JSON.stringify({alg:'RS256',kid:'fixture-key',typ:'JWT',...headerChanges})).toString('base64url');
  const claims=Buffer.from(JSON.stringify({iss:'https://'+accessDomain,aud:[accessAudience],sub:'fixture-owner',email:owner,iat:now,exp:now+300,...changes})).toString('base64url');
  const signed=header+'.'+claims;
  return signed+'.'+createSign('RSA-SHA256').update(signed).sign(privateKey).toString('base64url');
}
async function adminApi(path,options={}) {
  return api(path,{token:null,...options,headers:{'Cf-Access-Jwt-Assertion':accessJwt(),...options.headers}});
}
async function api(path, {token=tokens.alice,method='GET',body,headers={}}={}) {
  const response=await mf.dispatchFetch('https://worker.test'+path,{method,headers:{...(token?{Authorization:'Bearer '+token}:{}),...(body!==undefined?{'Content-Type':'application/json'}:{}),...headers},body:body===undefined?undefined:typeof body==='string'?body:JSON.stringify(body)});
  if(response.status===101)return response;
  const bytes=await response.arrayBuffer();
  return new Response(response.status===204?null:bytes,{status:response.status,headers:response.headers});
}
async function send(input={},options={}) {
  return api('/api/v2/send',{method:'POST',body:{id:crypto.randomUUID(),project:'alpha',from:'alice',body:'literal message',...input},...options});
}
async function inbox(query='',options={}) { const r=await api('/api/v2/inbox?projects=alpha&since=0'+query,options); return [r,await r.json()]; }
function once(socket) {
  return new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error('Expected WebSocket delivery')),3000);
    socket.addEventListener('message',event=>{clearTimeout(timer);resolve(JSON.parse(event.data));},{once:true});
  });
}
async function connect(agent) {
  const response=await api('/connect?agent='+agent+'&projects=alpha',{token:tokens[agent],headers:{Upgrade:'websocket'}});
  assert.equal(response.status,101);
  const ws=response.webSocket;ws.accept();return ws;
}

before(async()=>{
  const bundled=await build({entryPoints:['src/index.ts'],bundle:true,write:false,format:'esm',platform:'browser',target:'es2022'});
  workerScript=bundled.outputFiles[0].text;
  const pair=generateKeyPairSync('rsa',{modulusLength:2048});privateKey=pair.privateKey;
  publicJwk={...pair.publicKey.export({format:'jwk'}),kid:'fixture-key',alg:'RS256',use:'sig'};
  mf=new Miniflare({modules:true,script:workerScript,compatibilityDate:'2024-11-01',
    bindings:{HUB_TOKEN:admin,PORTAL_ORIGIN:portal,ACCESS_TEAM_DOMAIN:accessDomain,ACCESS_AUD:accessAudience,PORTAL_ADMIN_EMAIL:owner},
    outboundService:async(request)=>{assert.equal(request.url,'https://'+accessDomain+'/cdn-cgi/access/certs');return new Response(JSON.stringify({keys:[publicJwk]}),{headers:{'Content-Type':'application/json'}});},durableObjects:{HUB_DO:{className:'AgentMeshHub',useSQLite:true}},
    d1Databases:['HUB_DB'],log:new Log(LogLevel.ERROR)});
  db=await mf.getD1Database('HUB_DB');
  await migrate(db);
  for(const [agent,projects] of [['alice',['alpha']],['bob',['alpha']],['outsider',['beta']],['wild',['*']]]) {
    const response=await api('/tokens',{token:admin,method:'POST',body:{agent,projects}});
    assert.equal(response.status,201);
    const result=await response.json(); tokens[agent]=result.token.token;
    assert.ok(typeof tokens[agent]==='string','Local token fixture initialized');
  }
});
after(async()=>{await mf?.dispose();});

async function startRateWindow(){
  // The production limit is a fixed UTC minute. Start with time to finish the
  // burst so crossing a legitimate reset cannot make an overflow assertion flaky.
  const left=60000-Date.now()%60000;
  if(left<15000)await new Promise(resolve=>setTimeout(resolve,left+100));
}

describe('Part 1 foundation in workerd with real D1/DO',()=>{
  it('starts the ported schema without sample projects, messages, docs or activity',async()=>{
    for(const table of ['projects','channels','messages','docs','doc_revisions','activity_log','mesh_messages'])assert.equal((await db.prepare('SELECT count(*) AS n FROM '+table).first()).n,0);
  });
  it('keeps root version and legacy send/inbox operational',async()=>{
    const root=await api('/',{token:null});assert.equal((await root.json()).version,'2.1.0');
    const r=await api('/send',{method:'POST',body:{id:'legacy-first',project:'alpha',from:'alice',body:'legacy history'}});
    assert.equal(r.status,200);
    const poll=await api('/inbox?agent=alice&projects=alpha&since=0&limit=1');
    const result=await poll.json();assert.equal(poll.status,200);assert.equal(result.messages.length,1);
    assert.equal(result.messages[0].id,'legacy-first');
  });
  it('accepts existing WebSocket header auth and keeps /connections response',async()=>{
    const ws=await connect('alice');
    const r=await api('/connections',{token:admin});const result=await r.json();
    assert.equal(r.status,200);assert.ok(result.by_agent.alice);
    ws.close(1000,'done');
  });
  it('rejects missing or query-string credentials on versioned routes',async()=>{
    assert.equal((await api('/api/v2/inbox',{token:null})).status,401);
    assert.equal((await api('/api/v2/inbox?token=not-a-real-token')).status,401);
    assert.equal((await api('/api/v2/inbox',{token:'invalid-local-fixture'})).status,401);
  });
  it('does not impersonate the future Access portal using the legacy master token',async()=>{
    assert.equal((await api('/api/v2/inbox',{token:admin})).status,403);
  });
  it('enforces token identity and project scope',async()=>{
    assert.equal((await send({from:'bob'})).status,403);
    assert.equal((await send({project:'beta'})).status,403);
    assert.equal((await api('/api/v2/inbox?projects=beta')).status,403);
    assert.equal((await api('/api/v2/inbox?projects=alpha&agent=bob')).status,403);
  });
  it('sends v2 messages through the unchanged legacy inbox and WebSocket',async()=>{
    const ws=await connect('bob');const received=once(ws);
    const r=await send({id:'v2-fanout',to:'bob'});assert.equal(r.status,200);
    assert.equal((await received).id,'v2-fanout');
    const legacy=await api('/inbox?agent=bob&projects=alpha&since=0',{token:tokens.bob});
    assert.ok((await legacy.json()).messages.some(m=>m.id==='v2-fanout'));
    ws.close(1000,'done');
  });
  it('drops identical retries without another row or another fan-out',async()=>{
    const ws=await connect('bob');let count=0;ws.addEventListener('message',()=>count++);
    const input={id:'dedup',to:'bob',body:'one event only'};
    const received=once(ws);assert.equal((await send(input)).status,200);await received;
    const retry=await send(input);assert.equal((await retry.json()).duplicate,true);
    await new Promise(resolve=>setTimeout(resolve,30));assert.equal(count,1);
    assert.equal((await db.prepare('SELECT count(*) AS n FROM mesh_messages WHERE id=?').bind('dedup').first()).n,1);
    ws.close(1000,'done');
  });
  it('rejects a conflicting duplicate rather than modifying history',async()=>{
    await send({id:'conflict',body:'original'});
    assert.equal((await send({id:'conflict',body:'changed'})).status,409);
    assert.equal((await db.prepare('SELECT body FROM mesh_messages WHERE id=?').bind('conflict').first()).body,'original');
  });
  it('cannot shadow an unmirrored legacy message with the same id',async()=>{
    await api('/send',{method:'POST',body:{id:'legacy-collision',project:'alpha',from:'alice',body:'real older message'}});
    assert.equal((await send({id:'legacy-collision',body:'replacement'})).status,409);
    assert.equal(await db.prepare('SELECT id FROM mesh_messages WHERE id=?').bind('legacy-collision').first(),null);
  });
  it('treats since=0 as the beginning and returns no more than limit',async()=>{
    const [r,result]=await inbox('&limit=2');assert.equal(r.status,200);
    assert.equal(result.messages.length,2);assert.equal(result.messages[0].id,'legacy-first');
    assert.equal(result.latest_id,result.messages.at(-1).id);assert.equal(result.has_more,true);
  });
  it('pages by last delivered id without skipping or repeating messages',async()=>{
    let cursor='0';const seen=[];
    for(let i=0;i<20;i++) {
      const r=await api('/api/v2/inbox?projects=alpha&limit=2&since='+encodeURIComponent(cursor));
      assert.equal(r.status,200);const page=await r.json();seen.push(...page.messages.map(m=>m.id));
      if(!page.has_more)break;cursor=page.next_cursor;
    }
    assert.equal(new Set(seen).size,seen.length);
    assert.ok(seen.includes('legacy-first')&&seen.includes('dedup'));
  });
  it('rejects invalid limits and unknown cursors',async()=>{
    for(const limit of ['0','201','NaN','1.5','1e2']) assert.equal((await api('/api/v2/inbox?limit='+limit)).status,400);
    assert.equal((await api('/api/v2/inbox?since=does-not-exist')).status,400);
  });
  it('filters direct messages and hides private cursor existence',async()=>{
    await send({id:'private-to-bob',to:'bob'});
    const [,alice]=await inbox();assert.ok(alice.messages.some(m=>m.id==='private-to-bob'));
    const r=await api('/api/v2/inbox?projects=alpha',{token:tokens.wild});
    assert.ok(!(await r.json()).messages.some(m=>m.id==='private-to-bob'));
    assert.equal((await api('/api/v2/inbox?projects=alpha&since=private-to-bob',{token:tokens.wild})).status,400);
  });
  it('redacts credentials before either durable history or WebSocket fan-out',async()=>{
    const body='password=private-value --token "another-private" https://user:uri-private@example.test '+tokens.alice;
    const ws=await connect('bob');const received=once(ws);
    assert.equal((await send({id:'redacted',to:'bob',body})).status,200);
    const wire=await received;const stored=await db.prepare('SELECT body FROM mesh_messages WHERE id=?').bind('redacted').first();
    for(const value of ['private-value','another-private','uri-private',tokens.alice]) {
      assert.ok(!wire.body.includes(value),'Wire message must redact secrets');
      assert.ok(!stored.body.includes(value),'D1 history must redact secrets');
    }
    ws.close(1000,'done');
  });
  it('keeps arbitrary message text as literal data',async()=>{
    const body='echo hello; ordinary text, not an instruction to the hub';
    await send({id:'literal',body});
    assert.equal((await db.prepare('SELECT body FROM mesh_messages WHERE id=?').bind('literal').first()).body,body);
  });
  it('enforces UTF-8 body size and total request size',async()=>{
    assert.equal((await send({body:'€'.repeat(11000)})).status,413);
    assert.equal((await send({body:'x'.repeat(70000)})).status,413);
    assert.equal((await send({body:'x'.repeat(32768)})).status,200);
  });
  it('rejects malformed JSON, wrong types and unknown fields',async()=>{
    assert.equal((await api('/api/v2/send',{method:'POST',body:'not JSON'})).status,400);
    assert.equal((await send({body:{text:'not a string'}})).status,400);
    assert.equal((await send({unknown_field:'not accepted'})).status,400);
    assert.equal((await send({type:'ping'})).status,400);
  });
  it('allows only the configured portal origin on versioned routes',async()=>{
    const allowed=await api('/api/v2/inbox',{headers:{Origin:portal}});
    assert.equal(allowed.headers.get('Access-Control-Allow-Origin'),portal);
    const denied=await api('/api/v2/inbox',{headers:{Origin:'https://untrusted.test'}});
    assert.equal(denied.status,403);assert.equal(denied.headers.get('Access-Control-Allow-Origin'),null);
    const server=await api('/api/v2/inbox');assert.equal(server.headers.get('Access-Control-Allow-Origin'),null);
  });
  it('validates preflight method and headers without wildcard CORS',async()=>{
    const r=await api('/api/v2/send',{method:'OPTIONS',token:null,headers:{Origin:portal,'Access-Control-Request-Method':'POST','Access-Control-Request-Headers':'authorization, content-type'}});
    assert.equal(r.status,204);assert.equal(r.headers.get('Access-Control-Allow-Origin'),portal);
    assert.equal((await api('/api/v2/send',{method:'OPTIONS',token:null,headers:{Origin:portal,'Access-Control-Request-Method':'DELETE'}})).status,403);
  });
  it('preserves append-only D1 message history',async()=>{
    await assert.rejects(db.prepare('DELETE FROM mesh_messages WHERE id=?').bind('literal').run());
    await assert.rejects(db.prepare('UPDATE mesh_messages SET body=? WHERE id=?').bind('replacement','literal').run());
  });
  it('rejects credential-shaped identifiers instead of storing tokens as metadata',async()=>{
    assert.equal((await send({id:tokens.alice})).status,400);
    assert.equal((await send({reply_to:'password=not-for-storage'})).status,400);
    assert.ok(await db.prepare('SELECT id FROM mesh_messages WHERE id=?').bind(tokens.alice).first()===null,'Credential-shaped metadata must not be persisted');
  });
  it('rejects a revoked agent token without changing production credentials',async()=>{
    const created=await api('/tokens',{token:admin,method:'POST',body:{agent:'revoked-local',projects:['alpha']}});
    const token=(await created.json()).token.token;
    assert.equal((await api('/tokens/revoke',{token:admin,method:'POST',body:{agent:'revoked-local'}})).status,200);
    assert.equal((await api('/api/v2/inbox',{token})).status,401);
  });
  it('enforces the request cap on chunked uploads without Content-Length',async()=>{
    const chunk=new TextEncoder().encode('x'.repeat(35000));
    const body=new ReadableStream({start(controller){controller.enqueue(chunk);controller.enqueue(chunk);controller.close();}});
    const r=await mf.dispatchFetch('https://worker.test/api/v2/send',{method:'POST',headers:{Authorization:'Bearer '+tokens.alice,'Content-Type':'application/json'},body,duplex:'half'});
    assert.equal(r.status,413);await r.text();
  });
  it('bounds inbox requests separately from sends',async()=>{
    await startRateWindow();
    const created=await api('/tokens',{token:admin,method:'POST',body:{agent:'read-rate',projects:['alpha']}});
    const token=(await created.json()).token.token;
    for(let i=0;i<120;i++)assert.equal((await api('/api/v2/inbox?limit=1',{token})).status,200);
    assert.equal((await api('/api/v2/inbox?limit=1',{token})).status,429);
    assert.equal((await send({from:'read-rate'},{token})).status,200);
  });
  it('bounds legacy synchronization and pages every older event without loss',async()=>{
    const created=await api('/tokens',{token:admin,method:'POST',body:{agent:'batch-local',projects:['batch-project']}});
    const token=(await created.json()).token.token;
    for(let i=0;i<105;i++)assert.equal((await api('/send',{token,method:'POST',body:{id:'batch-'+i,project:'batch-project',from:'batch-local',body:'fixture event '+i}})).status,200);
    let cursor='0', seen=[], pending=false;
    for(let i=0;i<10;i++){
      const r=await api('/api/v2/inbox?projects=batch-project&limit=200&since='+cursor,{token});assert.equal(r.status,200);
      const page=await r.json();pending ||= page.legacy_sync_pending;seen.push(...page.messages.map(m=>m.id));
      if(!page.has_more)break;cursor=page.next_cursor;
    }
    assert.ok(pending);assert.equal(new Set(seen).size,105);assert.equal(seen.length,105);
  });
  it('verifies a signed Access JWT as identity hub and actor arin',async()=>{
    const r=await adminApi('/api/v2/admin/session');assert.equal(r.status,200);
    assert.deepEqual(await r.json(),{identity:'hub',actor:'arin',role:'admin'});
  });
  it('rejects identity headers without a verified Access JWT',async()=>{
    assert.equal((await api('/api/v2/admin/session',{token:admin,headers:{'Cf-Access-Authenticated-User-Email':owner}})).status,401);
    assert.equal((await api('/api/v2/admin/session')).status,401);
  });
  it('rejects expired, wrong-audience, wrong-issuer and tampered Access assertions',async()=>{
    for(const changes of [{exp:1},{aud:['wrong-audience']},{iss:'https://wrong-team.cloudflareaccess.com'},{nbf:9999999999}]) {
      const r=await adminApi('/api/v2/admin/session',{headers:{'Cf-Access-Jwt-Assertion':accessJwt(changes)}});assert.equal(r.status,401);
    }
    const jwt=accessJwt();const parts=jwt.split('.');parts[1]=Buffer.from(JSON.stringify({email:owner,exp:9999999999})).toString('base64url');
    assert.equal((await adminApi('/api/v2/admin/session',{headers:{'Cf-Access-Jwt-Assertion':parts.join('.')}})).status,401);
  });
  it('rejects a correctly signed Access identity belonging to another person',async()=>{
    assert.equal((await adminApi('/api/v2/admin/session',{headers:{'Cf-Access-Jwt-Assertion':accessJwt({email:'other-fixture@example.test'})}})).status,403);
  });
  it('rejects invalid signatures even when all signed claim values look valid',async()=>{
    const parts=accessJwt().split('.');parts[2]=(parts[2][0]==='A'?'B':'A')+parts[2].slice(1);
    assert.equal((await adminApi('/api/v2/admin/session',{headers:{'Cf-Access-Jwt-Assertion':parts.join('.')}})).status,401);
    for(const header of [{alg:'none'},{kid:'unknown-key'}])assert.equal((await adminApi('/api/v2/admin/session',{headers:{'Cf-Access-Jwt-Assertion':accessJwt({},header)}})).status,401);
  });
  it('delivers portal messages as arin through the same legacy inbox and WebSocket',async()=>{
    const ws=await connect('bob');const received=once(ws);
    const r=await adminApi('/api/v2/admin/send',{method:'POST',body:{id:'portal-from-arin',project:'alpha',to:'bob',body:'Portal fixture message'}});
    assert.equal(r.status,200);const wire=await received;assert.equal(wire.from,'arin');assert.equal(wire.id,'portal-from-arin');
    const legacy=await api('/inbox?projects=alpha&since=0',{token:tokens.bob});
    assert.ok((await legacy.json()).messages.some(m=>m.id==='portal-from-arin'&&m.from==='arin'));
    assert.equal((await send({from:'arin'})).status,403);ws.close(1000,'done');
  });
  it('lets only the Access administrator read all transport DMs with pagination',async()=>{
    const r=await adminApi('/api/v2/admin/inbox?since=0&limit=200');assert.equal(r.status,200);
    const result=await r.json();assert.ok(result.messages.some(m=>m.id==='private-to-bob'));
    const page=await adminApi('/api/v2/admin/inbox?since=0&limit=1');assert.equal((await page.json()).messages.length,1);
    assert.equal((await api('/api/v2/admin/inbox',{token:admin})).status,401);
  });
  it('applies portal-only browser CORS without disrupting native legacy clients',async()=>{
    assert.equal((await api('/inbox?projects=alpha',{headers:{Origin:'https://untrusted.test'}})).status,403);
    assert.equal((await api('/connections',{token:admin})).status,200);
    const r=await api('/inbox?projects=alpha',{headers:{Origin:portal}});assert.equal(r.headers.get('Access-Control-Allow-Origin'),portal);
    assert.equal((await api('/inbox?projects=alpha')).headers.get('Access-Control-Allow-Origin'),null);
  });
  it('redacts newly received legacy secrets without rewriting earlier history',async()=>{
    assert.equal((await api('/send',{method:'POST',body:{id:'legacy-redacted',project:'alpha',from:'alice',body:'password=legacy-private'}})).status,200);
    const poll=await api('/inbox?projects=alpha&since=0');const page=await poll.json();
    assert.ok(!page.messages.find(m=>m.id==='legacy-redacted').body.includes('legacy-private'));
    assert.equal(page.messages.find(m=>m.id==='legacy-first').body,'legacy history');
  });
  it('returns bounded per-agent rate limits while legacy endpoints remain unaffected',async()=>{
    await startRateWindow();
    // A separate local fixture avoids making this test depend on earlier requests.
    const created=await api('/tokens',{token:admin,method:'POST',body:{agent:'rate-agent',projects:['alpha']}});
    const rateToken=(await created.json()).token.token;
    for(let i=0;i<60;i++) assert.equal((await send({id:'rate-'+i,from:'rate-agent'},{token:rateToken})).status,200);
    const limited=await send({from:'rate-agent'},{token:rateToken});assert.equal(limited.status,429);
    assert.ok(Number(limited.headers.get('Retry-After'))>0);
    assert.equal((await api('/send',{token:rateToken,method:'POST',body:{project:'alpha',from:'rate-agent',body:'legacy unaffected'}})).status,200);
  });
});


it('preserves D1 history, hashed identities, duplicate receipts and rate state across runtime restart',async()=>{
  await startRateWindow();
  const folder=await mkdtemp(join(tmpdir(),'mesh-step1-persist-'));
  const options={modules:true,script:workerScript,compatibilityDate:'2024-11-01',bindings:{HUB_TOKEN:admin},
    durableObjects:{HUB_DO:{className:'AgentMeshHub',useSQLite:true}},d1Databases:['HUB_DB'],
    d1Persist:join(folder,'d1'),durableObjectsPersist:join(folder,'do'),log:new Log(LogLevel.ERROR)};
  let runtime=new Miniflare(options);
  const dispatch=async(path,token,body)=>runtime.dispatchFetch('https://worker.test'+path,{method:body?'POST':'GET',headers:{Authorization:'Bearer '+token,...(body?{'Content-Type':'application/json'}:{})},body:body?JSON.stringify(body):undefined});
  try{
    await migrate(await runtime.getD1Database('HUB_DB'));
    assert.equal((await dispatch('/api/v2/admin/session',admin)).status,503);
    const created=await dispatch('/tokens',admin,{agent:'persistent-local',projects:['alpha']});
    const token=(await created.json()).token.token;
    const input={id:'persistent-id',project:'alpha',from:'persistent-local',body:'persistent fixture'};
    assert.equal((await (await dispatch('/api/v2/send',token,input)).json()).duplicate,false);
    await runtime.dispose();runtime=new Miniflare(options);
    assert.equal((await (await dispatch('/api/v2/send',token,input)).json()).duplicate,true);
    const poll=await dispatch('/api/v2/inbox?since=0',token);assert.equal(poll.status,200);
    assert.equal((await poll.json()).messages.length,1);
    for(let i=0;i<58;i++)assert.equal((await dispatch('/api/v2/send',token,{...input,id:'persistent-'+i})).status,200);
    const limited=await dispatch('/api/v2/send',token,{...input,id:'persistent-over-limit'});
    assert.equal(limited.status,429);await limited.text();
  } finally {
    await runtime.dispose();
    assert.ok(resolve(folder).startsWith(resolve(tmpdir())+sep));
    await rm(folder,{recursive:true,force:true});
  }
});


it('enforces the foundation channel and document permission matrix',async()=>{
  const built=await build({entryPoints:['src/permissions.ts'],bundle:true,write:false,format:'esm',platform:'node',target:'es2022'});
  const policy=await import('data:text/javascript;base64,'+Buffer.from(built.outputFiles[0].text).toString('base64'));
  const arin={role:'admin',agent:'hub',actor:'arin'},alice={role:'agent',agent:'alice',actor:'alice'},outsider={role:'agent',agent:'other',actor:'other'};
  const dm={type:'dm',members:['alice','bob']},group={type:'group',members:['alice','bob']},ann={type:'announce',members:['alice','bob']};
  assert.equal(policy.canReadChannel(arin,dm),true);
  assert.equal(policy.canReadChannel(alice,dm),true);
  assert.equal(policy.canReadChannel(outsider,dm),false);
  assert.equal(policy.canPostChannel(outsider,group),false);
  assert.equal(policy.canPostChannel(alice,group),true);
  assert.equal(policy.canPostChannel(alice,ann),false);
  assert.equal(policy.canPostChannel(arin,ann),true);
  assert.equal(policy.canSuggestDocument(alice,['alice','bob']),true);
  assert.equal(policy.canSuggestDocument(outsider,['alice','bob']),false);
  assert.equal(policy.canApproveDocument(alice),false);
  assert.equal(policy.canApproveDocument(arin),true);
  assert.equal(policy.canEditDocument(alice),false);
});


it('keeps document revisions and activity immutable in the ported schema',async()=>{
  await db.prepare("INSERT INTO projects(id,name,created_at) VALUES('schema-fixture','Isolated schema fixture',0)").run();
  await db.prepare("INSERT INTO doc_revisions(id,project_id,doc_id,rev,ts,time,author,note,body) VALUES('schema-r1','schema-fixture','plan',1,0,'1970-01-01','arin','fixture','immutable')").run();
  await db.prepare("INSERT INTO activity_log(id,project_id,ts,time,agent,kind,event_type,title) VALUES('schema-a1','schema-fixture',0,'1970-01-01','arin','event','fixture','immutable')").run();
  for(const table of ['doc_revisions','activity_log']) {
    await assert.rejects(db.prepare('DELETE FROM '+table).run());
    await assert.rejects(db.prepare('UPDATE '+table+" SET id='changed'").run());
  }
  await assert.rejects(db.prepare("INSERT INTO channels(id,project_id,type,name,created_at) VALUES('orphan','missing-project','group','general',0)").run());
});
