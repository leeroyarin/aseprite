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
async function migrate(database, names=['0001_v2_transport.sql','0002_foundation.sql','0003_project_features.sql','0004_search_backfill.sql','0005_governance.sql','0006_enrollment.sql']) {
  for(const name of names) {
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

describe('Part 2 project features in workerd',()=>{
  const adminPost=(path,body)=>adminApi(path,{method:'POST',body});
  const post=(path,body,agent='alice')=>api(path,{method:'POST',body,token:tokens[agent]});
  const project='/projects/alpha';
  const dpath=project+'/docs/plan';
  let suggestion;
  async function sweep(now){const ns=await mf.getDurableObjectNamespace('HUB_DO');return ns.get(ns.idFromName('global-agent-mesh-hub')).fetch('https://supervisor.internal/api/v2/internal/sweep',{headers:{'X-Hub-Scheduled-Time':String(now)}});}
  it('starts without projects, agents, tasks, documents, or sample messages',async()=>{
    for(const t of ['projects','channels','messages','docs','agent_registry','tasks'])assert.equal((await db.prepare('SELECT count(*) n FROM '+t).first()).n,0);
  });
  it('only lets Access owner create a project with general and announcements',async()=>{
    assert.equal((await post('/api/v2/projects',{id:'alpha',name:'Alpha',agents:['alice','bob']})).status,403);
    assert.equal((await adminPost('/api/v2/admin/projects',{id:'alpha',name:'Alpha',agents:['alice','bob']})).status,201);
    const channels=(await (await api('/api/v2/projects/alpha/channels')).json()).channels;
    assert.deepEqual(channels.map(x=>x.type).sort(),['announce','group']);
    assert.equal((await adminPost('/api/v2/admin/projects',{id:'alpha',name:'Changed',agents:['alice']})).status,409);
  });
  it('requires membership even with a wildcard token and filters project lists',async()=>{
    assert.equal((await api('/api/v2/projects/alpha/channels',{token:tokens.wild})).status,403);
    assert.deepEqual((await (await api('/api/v2/projects',{token:tokens.outsider})).json()).projects,[]);
    assert.equal((await api('/api/v2/projects/alpha/channels',{token:tokens.outsider})).status,403);
  });
  it('creates private group and DM channels with valid project members',async()=>{
    for(const [id,type,members] of [['private','group',['alice']],['dm','dm',['alice','bob']]])assert.equal((await adminPost('/api/v2/admin/projects/alpha/channels',{id,name:id,type,members})).status,201);
    assert.equal((await adminPost('/api/v2/admin/projects/alpha/channels',{id:'bad',name:'bad',type:'dm',members:['alice','outsider']})).status,400);
    const list=(await (await api('/api/v2/projects/alpha/channels',{token:tokens.bob})).json()).channels;
    assert.ok(!list.some(c=>c.id==='private'));
  });
  it('allows only Arin to post announcements and delivers author arin natively',async()=>{
    const path='/api/v2/channels/alpha%3Aannouncements/messages';
    assert.equal((await post(path,{id:'denied-ann',content:'no'})).status,403);
    assert.equal((await adminPost(path.replace('/api/v2/','/api/v2/admin/'),{id:'announcement',content:'Owner announcement'})).status,201);
    const poll=await api('/inbox?agent=bob&projects=alpha&since=0',{token:tokens.bob});
    assert.ok((await poll.json()).messages.some(m=>m.from==='arin'&&m.body==='Owner announcement'));
  });
  it('keeps private channel content out of other members inboxes and sockets',async()=>{
    const bob=await connect('bob');const received=[];bob.addEventListener('message',e=>received.push(JSON.parse(e.data)));
    const r=await post('/api/v2/channels/private/messages',{id:'private-msg',content:'Private zebra literal'});assert.equal(r.status,201);
    assert.equal((await api('/api/v2/channels/private/messages',{token:tokens.bob})).status,403);
    const poll=await api('/inbox?agent=bob&projects=alpha&since=0',{token:tokens.bob});assert.ok(!(await poll.json()).messages.some(m=>m.body.includes('Private zebra')));
    await new Promise(r=>setTimeout(r,25));assert.ok(!received.some(m=>m.body?.includes('Private zebra')));bob.close();
  });
  it('deduplicates canonical messages and recipient envelopes and rejects conflicting retries',async()=>{
    const input={id:'dm-id',content:'Shared otter'};assert.equal((await post('/api/v2/channels/dm/messages',input)).status,201);
    assert.equal((await (await post('/api/v2/channels/dm/messages',input)).json()).duplicate,true);
    assert.equal((await post('/api/v2/channels/dm/messages',{...input,content:'changed'})).status,409);
    assert.equal((await db.prepare("SELECT count(*) n FROM messages WHERE id='dm-id'").first()).n,1);
    assert.equal((await db.prepare("SELECT count(*) n FROM feature_outbox WHERE json_extract(envelope,'$.body')='Shared otter'").first()).n,2);
  });
  it('delivers one recipient envelope per channel message to its sender, not one per member',async()=>{
    const alice=await connect('alice');let count=0;alice.addEventListener('message',e=>{if(JSON.parse(e.data).body==='One sender copy')count++;});
    const arrived=once(alice);
    assert.equal((await post('/api/v2/channels/dm/messages',{id:'sender-once',content:'One sender copy'})).status,201);await arrived;
    await new Promise(r=>setTimeout(r,25));assert.equal(count,1);
    const legacy=await api('/inbox?agent=alice&projects=alpha&since=0');assert.equal((await legacy.json()).messages.filter(x=>x.body==='One sender copy').length,1);
    const modern=await api('/api/v2/inbox?projects=alpha');assert.equal((await modern.json()).messages.filter(x=>x.body==='One sender copy').length,1);
    alice.close();
  });
  it('pages normalized channel messages and the owner read-all without repetition',async()=>{
    await post('/api/v2/channels/dm/messages',{id:'dm-id2',content:'Second otter'});
    const first=await (await api('/api/v2/channels/dm/messages?limit=1')).json();assert.equal(first.items.length,1);assert.equal(first.has_more,true);
    const next=await (await api('/api/v2/channels/dm/messages?limit=1&cursor='+first.next_cursor)).json();assert.notEqual(first.items[0].id,next.items[0].id);
    assert.ok((await (await adminApi('/api/v2/admin/messages?limit=200')).json()).items.some(x=>x.id==='private-msg'));
    assert.equal((await api('/api/v2/messages')).status,404);
    assert.equal((await api('/api/v2/channels/dm/messages?limit=201')).status,400);
  });
  it('redacts feature messages, status reports and documents before durable storage',async()=>{
    assert.equal((await post('/api/v2/channels/dm/messages',{id:'secret-feature',content:'password=sample-sensitive-value'})).status,201);
    const row=await db.prepare("SELECT content FROM messages WHERE id='secret-feature'").first();assert.ok(!row.content.includes('sample-sensitive-value'));
    assert.equal((await post('/api/v2/agents/status',{task:'token='+tokens.alice})).status,200);
    const report=await db.prepare("SELECT body FROM status_reports WHERE agent='alice' ORDER BY seq DESC LIMIT 1").first();assert.ok(!report.body.includes(tokens.alice));
    assert.equal((await post('/api/v2/channels/dm/messages',{id:tokens.alice,content:'bad metadata'})).status,400);
  });
  it('registers display names and roles without issuing credentials',async()=>{
    for(const agent of ['alice','bob'])assert.equal((await adminPost('/api/v2/admin/agents',{id:agent,name:agent.toUpperCase(),role:'developer'})).status,201);
    assert.equal((await post('/api/v2/agents',{id:'fake',name:'Fake',role:'owner'})).status,403);
    const list=await (await adminApi('/api/v2/admin/agents')).json();assert.equal(list.agents.find(x=>x.id==='alice').name,'ALICE');
    const publicList=await (await api('/api/v2/agents',{token:tokens.bob})).json();assert.ok(publicList.agents.every(x=>x.status===undefined));
  });
  it('records legacy polls and connected sockets as alive without changing contracts',async()=>{
    const before=Date.now();assert.equal((await api('/inbox?agent=alice&projects=alpha&since=0')).status,200);
    assert.ok((await db.prepare("SELECT last_seen FROM agent_registry WHERE id='alice'").first()).last_seen>=before);
    const socket=await connect('bob');assert.ok((await db.prepare("SELECT last_seen FROM agent_registry WHERE id='bob'").first()).last_seen>=before);socket.close();
  });
  it('marks one missed check Late and two Down, with stable coverage and return',async()=>{
    const now=Math.floor(Date.now()/300000)*300000+300000;
    assert.equal((await adminPost('/api/v2/admin/projects/alpha/tasks',{id:'task-one',title:'Open work',owner:'alice'})).status,201);
    await db.prepare("UPDATE agent_registry SET last_seen=?,checked_at=0,health='Healthy' WHERE id='alice'").bind(now-300000).run();
    await db.prepare("UPDATE agent_registry SET last_seen=?,checked_at=0,health='Healthy' WHERE id='bob'").bind(now).run();
    let r=await sweep(now);assert.equal(r.status,200);await r.text();assert.equal((await db.prepare("SELECT health FROM agent_registry WHERE id='alice'").first()).health,'Late');
    assert.equal((await db.prepare("SELECT assignee FROM tasks WHERE id='task-one'").first()).assignee,'alice');
    await db.prepare("UPDATE agent_registry SET last_seen=? WHERE id='bob'").bind(now+300000).run();
    r=await sweep(now+300000);assert.equal(r.status,200);await r.text();assert.equal((await db.prepare("SELECT health FROM agent_registry WHERE id='alice'").first()).health,'Down');
    assert.equal((await db.prepare("SELECT assignee FROM tasks WHERE id='task-one'").first()).assignee,'bob');
    const events=(await db.prepare("SELECT count(*) n FROM task_events WHERE task_id='task-one'").first()).n;
    await (await sweep(now+300000)).text();assert.equal((await db.prepare("SELECT count(*) n FROM task_events WHERE task_id='task-one'").first()).n,events);
    await db.prepare("UPDATE agent_registry SET last_seen=? WHERE id IN ('alice','bob')").bind(now+600000).run();await (await sweep(now+600000)).text();
    assert.equal((await db.prepare("SELECT assignee FROM tasks WHERE id='task-one'").first()).assignee,'alice');
    const health=await db.prepare("SELECT count(*) n FROM messages WHERE channel_id='alpha:general' AND agent='hub-supervisor'").first();assert.ok(health.n>=3);
  });
  it('keeps a persistent connected agent healthy despite old last_seen',async()=>{
    const socket=await connect('bob');const now=Math.floor(Date.now()/300000)*300000+1200000;
    await db.prepare("UPDATE agent_registry SET last_seen=0,checked_at=0 WHERE id='bob'").run();await (await sweep(now)).text();assert.equal((await db.prepare("SELECT health FROM agent_registry WHERE id='bob'").first()).health,'Healthy');socket.close();
  });
  it('prevents public callers from invoking supervisor ticks',async()=>{assert.equal((await api('/api/v2/internal/sweep')).status,404);});
  it('requires owner and optimistic versions for document edits',async()=>{
    assert.equal((await post('/api/v2'+dpath,{body:'attempt',expected_version:0})).status,403);
    const saved=await adminPost('/api/v2/admin'+dpath,{body:'Initial plan password=doc-secret',expected_version:0});assert.equal(saved.status,200);assert.equal((await saved.json()).version,1);
    assert.equal((await adminPost('/api/v2/admin'+dpath,{body:'stale',expected_version:0})).status,409);
    assert.ok(!(await (await api('/api/v2'+dpath)).json()).body.includes('doc-secret'));
  });
  it('lets members suggest and only Arin approve or reject; decisions persist',async()=>{
    const s=await post('/api/v2'+dpath+'/suggestions',{body:'Approved proposal',note:'Agent recommendation'});assert.equal(s.status,201);suggestion=(await s.json()).id;
    assert.equal((await post('/api/v2'+dpath+'/suggestions/'+suggestion+'/approve',{expected_version:1})).status,403);
    const approval=await adminPost('/api/v2/admin'+dpath+'/suggestions/'+suggestion+'/approve',{expected_version:1});assert.equal(approval.status,200);assert.equal((await approval.json()).version,2);
    assert.equal((await adminPost('/api/v2/admin'+dpath+'/suggestions/'+suggestion+'/reject',{})).status,409);
    const rejected=(await (await post('/api/v2'+dpath+'/suggestions',{body:'Rejected proposal'})).json()).id;
    assert.equal((await adminPost('/api/v2/admin'+dpath+'/suggestions/'+rejected+'/reject',{})).status,200);
    assert.equal((await db.prepare('SELECT count(*) n FROM suggestion_decisions').first()).n,2);
  });
  it('restores documents by creating a new revision; keeps originals immutable',async()=>{
    const restored=await adminPost('/api/v2/admin'+dpath+'/restore',{version:1,expected_version:2});assert.equal(restored.status,200);assert.equal((await restored.json()).version,3);
    const history=await (await api('/api/v2'+dpath+'/revisions?limit=2')).json();assert.equal(history.items.length,2);assert.equal(history.has_more,true);
    assert.equal((await db.prepare("SELECT body FROM doc_revisions WHERE project_id='alpha' AND doc_id='plan' AND rev=2").first()).body,'Approved proposal');
    await assert.rejects(db.prepare("DELETE FROM doc_revisions WHERE project_id='alpha'").run());
  });
  it('records per-document/version acknowledgement and rejects nonexistent versions',async()=>{
    assert.equal((await post('/api/v2'+dpath+'/ack',{version:1})).status,200);assert.equal((await post('/api/v2'+dpath+'/ack',{version:1})).status,200);
    assert.equal((await post('/api/v2'+dpath+'/ack',{version:999})).status,404);
    assert.equal((await db.prepare('SELECT count(*) n FROM doc_acks').first()).n,1);await assert.rejects(db.prepare('DELETE FROM doc_acks').run());
  });
  it('pages status reports and acknowledgements with owner privacy',async()=>{
    const mine=await api('/api/v2/agents/alice/reports?limit=1');assert.equal(mine.status,200);assert.equal((await mine.json()).items.length,1);
    assert.equal((await api('/api/v2/agents/alice/reports',{token:tokens.bob})).status,403);
    assert.equal((await adminApi('/api/v2/admin/agents/alice/reports')).status,200);
    const ownAcks=await (await api('/api/v2'+dpath+'/acks')).json();assert.equal(ownAcks.items.length,1);
    const peerAcks=await (await api('/api/v2'+dpath+'/acks',{token:tokens.bob})).json();assert.equal(peerAcks.items.length,0);
    const allAcks=await (await adminApi('/api/v2/admin'+dpath+'/acks')).json();assert.equal(allAcks.items.length,1);
  });
  it('preserves parent identity with optional sub labels across history and native inbox',async()=>{
    assert.equal((await post('/api/v2/channels/dm/messages',{id:'sub-message',content:'Labeled task',sub:'reviewer'})).status,201);
    const poll=await api('/inbox?agent=bob&projects=alpha&since=0',{token:tokens.bob});assert.ok((await poll.json()).messages.some(m=>m.sub==='reviewer'&&m.from==='alice'));
    assert.equal((await send({id:'direct-sub',to:'bob',sub:'checker'})).status,200);
    assert.ok((await (await api('/api/v2/inbox?projects=alpha',{token:tokens.bob})).json()).messages.some(m=>m.id==='direct-sub'&&m.sub==='checker'));
    assert.equal((await db.prepare("SELECT count(*) n FROM agent_registry WHERE id IN ('reviewer','checker')").first()).n,0);
    assert.equal((await send({id:'direct-sub',to:'bob',sub:'other'})).status,409);
  });
  it('filters activity and search by channel memberships, including private DMs',async()=>{
    const bob=(await (await api('/api/v2/projects/alpha/search?q=zebra',{token:tokens.bob})).json());assert.equal(bob.items.length,0);
    const owner=(await (await adminApi('/api/v2/admin/projects/alpha/search?q=zebra')).json());assert.equal(owner.items.length,1);
    const activity=await (await api('/api/v2/projects/alpha/activity?limit=200',{token:tokens.bob})).json();assert.ok(!activity.items.some(x=>x.thread_id==='private'));
    assert.equal((await api('/api/v2/projects/alpha/search?q='+encodeURIComponent('" OR *'))).status,200);
    await assert.rejects(db.prepare('DELETE FROM activity_log').run());
  });
  it('indexes native DMs without exposing them through activity or search',async()=>{
    assert.equal((await send({id:'native-only',to:'bob',body:'native capybara'})).status,200);
    assert.equal((await api('/send',{token:tokens.bob,method:'POST',body:{id:'bob-self',project:'alpha',from:'bob',to:'bob',body:'hidden wombat'}})).status,200);
    // Inbox sync imports existing native history into the D1 search ledger.
    await adminApi('/api/v2/admin/inbox?projects=alpha');
    const alice=await (await api('/api/v2/projects/alpha/search?q=wombat')).json();assert.equal(alice.items.length,0);
    const owner=await (await adminApi('/api/v2/admin/projects/alpha/search?q=wombat')).json();assert.equal(owner.items.length,1);
    const bob=await (await api('/api/v2/projects/alpha/search?q=capybara',{token:tokens.bob})).json();assert.equal(bob.items.length,1);
    assert.ok((await (await adminApi('/api/v2/admin/activity?limit=200')).json()).items.some(x=>x.id==='mesh:bob-self'));
  });
  it('retries a pending outbox receipt without duplicating durable history',async()=>{
    const row=await db.prepare('SELECT id FROM feature_outbox LIMIT 1').first();
    const before=(await db.prepare('SELECT count(*) n FROM mesh_messages').first()).n;
    await db.prepare('UPDATE feature_outbox SET delivered=0 WHERE id=?').bind(row.id).run();
    assert.equal((await api('/api/v2/projects/alpha/channels')).status,200);
    assert.equal((await db.prepare('SELECT delivered FROM feature_outbox WHERE id=?').bind(row.id).first()).delivered,1);
    assert.equal((await db.prepare('SELECT count(*) n FROM mesh_messages').first()).n,before);
  });
  it('stores nested secret fields safely and enforces feature body and label limits',async()=>{
    assert.equal((await post('/api/v2/agents/status',{details:{password:'unmarked-sensitive',api_key:'unmarked-api'}})).status,200);
    const latest=await db.prepare("SELECT body FROM status_reports WHERE agent='alice' ORDER BY seq DESC LIMIT 1").first();assert.ok(!latest.body.includes('unmarked-sensitive'));assert.ok(!latest.body.includes('unmarked-api'));
    assert.equal((await post('/api/v2/channels/dm/messages',{id:'oversize-unicode',content:'é'.repeat(17000)})).status,413);
    assert.equal((await send({id:'long-sub',sub:'a'.repeat(81)})).status,400);
  });
  it('keeps registered but never-seen agents Late at first check and Down at second',async()=>{
    assert.equal((await adminPost('/api/v2/admin/agents',{id:'new-profile',name:'New profile',role:'test'})).status,201);
    const now=Math.floor(Date.now()/300000)*300000+1500000;
    await db.prepare("UPDATE agent_registry SET registered_at=?,last_seen=0,checked_at=0 WHERE id='new-profile'").bind(now).run();
    await (await sweep(now)).text();assert.equal((await db.prepare("SELECT health FROM agent_registry WHERE id='new-profile'").first()).health,'Late');
    await (await sweep(now+300000)).text();assert.equal((await db.prepare("SELECT health FROM agent_registry WHERE id='new-profile'").first()).health,'Down');
  });
  it('serves a real Access-protected portal and external script from the Worker',async()=>{
    assert.equal((await api('/api/v2/admin/portal',{token:null})).status,401);
    const html=await adminApi('/api/v2/admin/portal');assert.equal(html.status,200);assert.match(html.headers.get('Content-Type'),/text\/html/);assert.match(await html.text(),/Agent Hub/);
    const js=await adminApi('/api/v2/admin/portal/app.js');assert.equal(js.status,200);const text=await js.text();new Function(text);assert.ok(!text.includes('localStorage'));assert.ok(!text.includes('127.0.0.1'));
    assert.equal((await api('/api/v2/admin/portal',{token:admin})).status,401);
  });
  it('provides owner-only live notifications and closes cleanly on cancellation',async()=>{
    assert.equal((await api('/api/v2/stream')).status,404);
    const response=await mf.dispatchFetch('https://worker.test/api/v2/admin/stream',{headers:{'Cf-Access-Jwt-Assertion':accessJwt()}});assert.equal(response.status,200);assert.match(response.headers.get('Content-Type'),/text\/event-stream/);
    const reader=response.body.getReader();assert.match(new TextDecoder().decode((await reader.read()).value),/event: connected/);
    const pending=reader.read();assert.equal((await adminPost('/api/v2/admin/channels/dm/messages',{id:'stream-update',content:'Live update'})).status,201);assert.match(new TextDecoder().decode((await pending).value),/event: refresh/);await reader.cancel();
  });
});

it('backfills earlier D1 transport history without rewriting it, including old timestamp formats',async()=>{
  const isolated=new Miniflare({modules:true,script:workerScript,compatibilityDate:'2024-11-01',d1Databases:['HUB_DB'],durableObjects:{HUB_DO:{className:'AgentMeshHub',useSQLite:true}},log:new Log(LogLevel.ERROR)});
  try {
    const database=await isolated.getD1Database('HUB_DB');
    await migrate(database,['0001_v2_transport.sql','0002_foundation.sql']);
    await database.prepare("INSERT INTO mesh_messages(id,project,from_agent,to_agent,type,body,timestamp,subject,request_hash,source) VALUES('older-id','alpha','alice','bob','task','existing marmot','older non-ISO timestamp','older','fixture-hash','legacy')").run();
    const before=await database.prepare('SELECT * FROM mesh_messages').first();
    await migrate(database,['0003_project_features.sql','0004_search_backfill.sql','0005_governance.sql','0006_enrollment.sql']);
    const after=await database.prepare('SELECT * FROM mesh_messages').first();delete after.sub;assert.deepEqual(after,before);
    assert.equal((await database.prepare("SELECT count(*) n FROM search_fts WHERE search_fts MATCH 'marmot'").first()).n,1);
    assert.equal((await database.prepare('SELECT count(*) n FROM activity_log').first()).n,1);
  } finally {await isolated.dispose();}
});
