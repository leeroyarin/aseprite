import { execFileSync } from 'node:child_process';
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, sep } from 'node:path';
import { build } from 'esbuild';
import { Miniflare, Log, LogLevel } from 'miniflare';
import { generateKeyPairSync, createSign, createHash } from 'node:crypto';

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

describe('Part 4 record-only enrollment in real workerd',()=>{
  const post=(path,body,who='alice')=>api('/api/v2/enrollment/'+path,{token:tokens[who],method:'POST',body});
  const owner=(path,body)=>adminApi('/api/v2/admin/enrollment/'+path,body?{method:'POST',body}:{});
  let rule,scriptHash,installed,folder,firstPacket,firstProof;
  const hex=x=>createHash('sha256').update(x).digest('hex');
  const accepted={};
  async function accept(who='alice',id=crypto.randomUUID()){
    const payload={id,name:who,rulebook_version:rule.version,rulebook_sha256:rule.sha256,accepted_at:new Date().toISOString()};
    const r=await post('accept',payload,who);assert.equal(r.status,201);accepted[who]=id;return payload;
  }
  function challengePayload(id,who='alice'){return {id,acceptance_id:accepted[who],project:'alpha',script_sha256:scriptHash,chat_path:installed.chat_path,commands_path:installed.commands_path,discovery:installed.discovery};}
  async function challenge(id,who='alice'){
    const r=await post('challenges',challengePayload(id,who),who);assert.equal(r.status,201);const result=await r.json();
    const inbox=await api('/api/v2/inbox?projects=alpha&since=0',{token:tokens[who]});const packet=(await inbox.json()).messages.find(x=>x.id===result.message_id);assert.ok(packet);return packet;
  }
  function proof(packet,id=crypto.randomUUID()){
    const c=JSON.parse(packet.body),at=new Date().toISOString();const record={time:at,project:packet.project,sender:packet.from,message_id:packet.id,reply_to:packet.reply_to??null,text:packet.body,status:'received'};
    const ordered=Object.fromEntries(Object.entries(record).sort(([a],[b])=>a<b?-1:1));
    return {id,challenge_id:c.challenge_id,code:c.code,script_sha256:scriptHash,chat_path:installed.chat_path,commands_path:installed.commands_path,rulebook_sha256:c.rulebook_sha256,message_id:packet.id,log_entry_sha256:hex(JSON.stringify(ordered)),logged_at:at};
  }
  after(async()=>{if(folder){assert.ok(resolve(folder).startsWith(resolve(tmpdir())+sep));await rm(folder,{recursive:true,force:true});}});
  it('starts empty and leaves unenrolled native send/read/connect working',async()=>{
    for(const table of ['enrollment_audit','enrollment_acceptances','enrollment_challenges','enrollment_proofs','enrollment_consumed'])assert.equal((await db.prepare('SELECT count(*) n FROM '+table).first()).n,0);
    assert.equal((await (await api('/api/v2/enrollment/status')).json()).status,'Not enrolled');
    assert.equal((await send({id:'unenrolled-native'})).status,200);assert.equal((await api('/inbox?agent=alice&projects=alpha&since=0')).status,200);const socket=await connect('alice');socket.close();
    const published=await adminApi('/api/v2/admin/rulebook',{method:'POST',body:{expected_version:0}});assert.equal(published.status,201);rule=await published.json();
    assert.equal((await adminApi('/api/v2/admin/projects',{method:'POST',body:{id:'alpha',name:'Alpha',agents:['alice','bob']}})).status,201);
  });
  it('serves the exact standalone logger and installs against verified fixture chat metadata',async()=>{
    const metadata=await (await api('/api/v2/enrollment/logger')).json();scriptHash=metadata.sha256;
    const download=await api('/api/v2/enrollment/logger.py');assert.equal(download.status,200);const source=await download.text();assert.equal(hex(source),scriptHash);assert.equal(download.headers.get('X-Script-SHA256'),scriptHash);assert.equal(metadata.network_calls,false);
    assert.equal(await readFile('sidecar/hub_command_log.py','utf8'),source);
    folder=await mkdtemp(join(tmpdir(),'mesh-enroll-'));const home=join(folder,'tool-home'),chat=join(home,'sessions');await mkdir(chat,{recursive:true});
    await writeFile(join(chat,'fixture.jsonl'),JSON.stringify({type:'session_meta',payload:{originator:'test'}})+'\n'+JSON.stringify({type:'normal_chat',text:'untouched fixture chat'})+'\n');
    const script=join(folder,'download.py');await writeFile(script,source);
    const evidence=JSON.parse(execFileSync('python',[script,'discover-codex','--codex-home',home],{encoding:'utf8'}));const evidenceFile=join(folder,'discovery.json');await writeFile(evidenceFile,JSON.stringify(evidence));
    installed=JSON.parse(execFileSync('python',[script,'install','--chat-dir',chat,'--commands-dir',join(folder,'commands'),'--evidence-file',evidenceFile],{encoding:'utf8'}));assert.equal(installed.script_sha256,scriptHash);
  });
  it('accepts only the authenticated name and current rule hash, then records Pending',async()=>{
    assert.equal((await post('accept',{id:'wrong-name',name:'bob',rulebook_version:rule.version,rulebook_sha256:rule.sha256,accepted_at:new Date().toISOString()})).status,403);
    assert.equal((await post('accept',{id:'wrong-rule',name:'alice',rulebook_version:rule.version,rulebook_sha256:'f'.repeat(64),accepted_at:new Date().toISOString()})).status,409);
    const payload=await accept();globalThis.acceptPayload=payload;const status=await (await api('/api/v2/enrollment/status')).json();assert.equal(status.status,'Pending');assert.equal(status.accepted_hash,rule.sha256);
  });
  it('deduplicates acceptances without editing history or impersonating via admin',async()=>{
    assert.equal((await (await post('accept',globalThis.acceptPayload)).json()).duplicate,true);
    assert.equal((await post('accept',{...globalThis.acceptPayload,accepted_at:'2020-01-01T00:00:00Z'})).status,409);
    assert.equal((await owner('accept',globalThis.acceptPayload)).status,403);
  });
  it('requires disjoint absolute folders, discovered evidence and the current script',async()=>{
    for(const change of [{chat_path:'relative/path'},{commands_path:installed.chat_path},{commands_path:installed.chat_path+'/commands'},{discovery:{}},{script_sha256:'0'.repeat(64)}])assert.equal((await post('challenges',{...challengePayload(crypto.randomUUID()),...change})).status,400);
  });
  it('delivers a random expiring challenge privately through the existing native inbox and socket',async()=>{
    const socket=await connect('alice'),bob=await connect('bob');let other=0;bob.addEventListener('message',e=>{if(JSON.parse(e.data).type==='enrollment.challenge')other++;});const incoming=once(socket);
    firstPacket=await challenge('first-challenge');assert.equal((await incoming).id,firstPacket.id);const data=JSON.parse(firstPacket.body);assert.ok(/^[a-f0-9]{64}$/.test(data.code));assert.ok(Date.parse(data.expires_at)>Date.now());
    const native=await api('/inbox?agent=alice&projects=alpha&since=0');assert.ok((await native.json()).messages.some(x=>x.id===firstPacket.id));
    const b=await api('/api/v2/inbox?projects=alpha',{token:tokens.bob});assert.ok(!(await b.json()).messages.some(x=>x.id===firstPacket.id));await new Promise(r=>setTimeout(r,20));assert.equal(other,0);socket.close();bob.close();
  });
  it('does not shadow native history when another client preuses the challenge request id',async()=>{
    const legacy=await api('/send',{token:tokens.bob,method:'POST',body:{id:'enrollment:client-selected',project:'alpha',from:'bob',to:'bob',body:'Older native message'}});assert.equal(legacy.status,200);
    const packet=await challenge('client-selected');assert.notEqual(packet.id,'enrollment:client-selected');assert.equal(packet.from,'hub-supervisor');
    const poll=await api('/api/v2/inbox?projects=alpha&since=0',{token:tokens.bob});assert.equal((await poll.json()).messages.find(m=>m.id==='enrollment:client-selected').body,'Older native message');
    // Get a fresh latest challenge for the subsequent installed-script proof.
    firstPacket=await challenge('installed-proof-challenge');
  });
  it('runs the installed script, appends receipt, and enrolls only after its exact proof checks',async()=>{
    const messageFile=join(folder,'challenge.json');await writeFile(messageFile,JSON.stringify(firstPacket));
    firstProof=JSON.parse(execFileSync('python',[join(installed.commands_path,'hub_command_log.py'),'proof','--config',join(installed.commands_path,'manifest.json'),'--message-file',messageFile],{encoding:'utf8'}));
    const response=await post('proofs',firstProof);assert.equal(response.status,201);const result=await response.json();assert.equal(result.success,true,result.reason);assert.equal(result.status,'Enrolled');
    const lines=(await readFile(join(installed.commands_path,'commands.jsonl'),'utf8')).trim().split('\n');assert.equal(lines.length,1);assert.equal(JSON.parse(lines[0]).message_id,firstPacket.id);
  });
  it('makes proof retries idempotent and new replay attempts recorded failures',async()=>{
    assert.equal((await (await post('proofs',firstProof)).json()).duplicate,true);
    const replay=await (await post('proofs',{...firstProof,id:'replayed-proof'})).json();assert.equal(replay.success,false);assert.equal(replay.reason,'already_used');
    const retry=JSON.parse(execFileSync('python',[join(installed.commands_path,'hub_command_log.py'),'proof','--config',join(installed.commands_path,'manifest.json'),'--message-file',join(folder,'challenge.json')],{encoding:'utf8'}));assert.deepEqual(retry,firstProof);
  });
  it('rejects cross-agent proof/status access and filters private audit data',async()=>{
    assert.equal((await post('proofs',{...firstProof,id:'bob-forgery'},'bob')).status,404);
    const audit=await (await api('/api/v2/enrollment/audit?agent=alice',{token:tokens.bob})).json();assert.ok(audit.items.every(x=>x.agent==='bob'));
    assert.equal((await api('/api/v2/admin/enrollment/agents/alice',{token:tokens.bob})).status,401);
  });
  it('records a wrong script hash as failed while preserving native access',async()=>{
    await accept('bob');const packet=await challenge('wrong-script','bob');const r=await post('proofs',{...proof(packet),script_sha256:'e'.repeat(64)},'bob');const result=await r.json();assert.equal(result.success,false);assert.equal(result.reason,'wrong_script_hash');assert.equal(result.status,'Pending');
    assert.equal((await send({id:'after-failed-proof',from:'bob'},{token:tokens.bob})).status,200);assert.equal((await api('/inbox?agent=bob&projects=alpha&since=0',{token:tokens.bob})).status,200);const ws=await connect('bob');ws.close();
  });
  it('records an expired challenge as failed without deleting its history or restricting access',async()=>{
    const original=await db.prepare("SELECT * FROM enrollment_challenges WHERE id='wrong-script'").first();const id='expired-fixture',event='challenge:'+id;
    await db.prepare("INSERT INTO enrollment_audit(id,agent,actor,kind,at,rulebook_version,rulebook_sha256,payload_json) VALUES(?,'bob','bob','challenge',?,?,?,'{}')").bind(event,Date.now()-300001,rule.version,rule.sha256).run();
    await db.prepare('INSERT INTO enrollment_challenges(id,agent,acceptance_id,project_id,code_hash,script_sha256,chat_path,commands_path,discovery_json,created_at,expires_at,message_id,event_id,request_hash) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)').bind(id,'bob',original.acceptance_id,'alpha',original.code_hash,scriptHash,installed.chat_path,installed.commands_path,JSON.stringify(installed.discovery),Date.now()-300001,Date.now()-1,'expired-message',event,'fixture').run();
    const result=await (await post('proofs',{...proof(firstPacket),id:'expired-proof',challenge_id:id},'bob')).json();assert.equal(result.success,false);assert.equal(result.reason,'expired');assert.equal(result.access_restricted,false);
  });
  it('checks folders, one-time codes and the exact log receipt rather than accepting hashes alone',async()=>{
    for(const [id,change,reason] of [['wrong-folder',{commands_path:join(folder,'different')},'folder_mismatch'],['wrong-code',{code:'0'.repeat(64)},'wrong_code'],['wrong-receipt',{log_entry_sha256:'0'.repeat(64)},'wrong_log_receipt']]){const packet=await challenge(id,'bob');const result=await (await post('proofs',{...proof(packet),...change},'bob')).json();assert.equal(result.success,false);assert.equal(result.reason,reason);}
  });
  it('deduplicates challenge issuance and marks superseded challenges unusable',async()=>{
    const old=await challenge('superseded','bob');const newer=await challenge('newer','bob');
    assert.equal((await (await post('challenges',challengePayload('newer','bob'),'bob')).json()).duplicate,true);
    const result=await (await post('proofs',proof(old),'bob')).json();assert.equal(result.reason,'superseded');assert.notEqual(JSON.parse(old.body).code,JSON.parse(newer.body).code);
  });
  it('requires re-acceptance after an owner rule change and fails old-rule challenges',async()=>{
    const pending=await challenge('old-rule','bob');const changed=await adminApi('/api/v2/admin/rulebook',{method:'POST',body:{expected_version:rule.version,body:rule.body+'\nOwner revision.'}});assert.equal(changed.status,201);rule=await changed.json();
    assert.equal((await (await api('/api/v2/enrollment/status')).json()).status,'Needs re-acceptance');
    await accept('alice');assert.equal((await (await api('/api/v2/enrollment/status')).json()).status,'Pending');
    assert.equal((await (await post('proofs',proof(pending),'bob')).json()).reason,'rulebook_changed');
    assert.equal((await post('challenges',challengePayload('without-new-accept','bob'),'bob')).status,409);
  });
  it('records owner revocation without token rotation or record-only disconnection',async()=>{
    const ws=await connect('alice');const r=await owner('agents/alice/revoke',{id:'revoke-alice',reason:'Owner review requested'});assert.equal(r.status,201);const result=await r.json();assert.equal(result.status,'Revoked');assert.equal(result.access_restricted,false);assert.equal(ws.readyState,1);
    assert.equal((await send({id:'revoked-record-only'})).status,200);assert.equal((await api('/api/v2/inbox?projects=alpha')).status,200);ws.close();
    assert.equal((await db.prepare("SELECT agent FROM activity_log WHERE event_type='enrollment.revoke' ORDER BY seq DESC LIMIT 1").first()).agent,'arin');
    const duplicate=await (await owner('agents/alice/revoke',{id:'revoke-alice',reason:'Owner review requested'})).json();assert.equal(duplicate.duplicate,true);
  });
  it('does not let an agent clear its revocation by reaccepting and requires a fresh owner allowance',async()=>{
    await accept('alice');assert.equal((await (await api('/api/v2/enrollment/status')).json()).status,'Revoked');
    assert.equal((await post('challenges',challengePayload('revoked-challenge'))).status,409);
    const allowed=await owner('agents/alice/allow',{id:'allow-alice',reason:'Fresh proof may proceed'});assert.equal((await allowed.json()).status,'Not enrolled');
    assert.equal((await post('challenges',challengePayload('stale-after-allow'))).status,409);
    await accept('alice');const packet=await challenge('fresh-after-allow');const result=await (await post('proofs',proof(packet))).json();assert.equal(result.success,true,result.reason);assert.equal(result.status,'Enrolled');
  });
  it('pages owner statuses and append-only audit while keeping challenge codes out of audit/proof copies',async()=>{
    const page=await (await owner('agents?limit=1')).json();assert.equal(page.items.length,1);assert.equal(page.has_more,true);
    const next=await (await owner('agents?limit=1&cursor='+encodeURIComponent(page.next_cursor))).json();assert.notEqual(next.items[0].agent,page.items[0].agent);
    const audit=await (await owner('audit?limit=2')).json();assert.equal(audit.items.length,2);assert.equal(audit.has_more,true);
    const code=JSON.parse(firstPacket.body).code;for(const row of (await db.prepare('SELECT payload_json FROM enrollment_audit').all()).results)assert.ok(!row.payload_json.includes(code));for(const row of (await db.prepare('SELECT evidence_json FROM enrollment_proofs').all()).results)assert.ok(!row.evidence_json.includes(code));
  });
  it('rejects credential-shaped owner agent paths and unrelated discovery payloads',async()=>{
    assert.equal((await owner('agents/'+encodeURIComponent('amt_'+'x'.repeat(32))+'/revoke',{id:'invalid-agent-revoke',reason:'Fixture rejection'})).status,400);
    const invalid={...challengePayload('invalid-discovery'),discovery:{...installed.discovery,normal_chats:['must not be stored']}};
    assert.equal((await post('challenges',invalid)).status,400);
  });
  it('protects all enrollment records and challenge-consumption receipts against mutation',async()=>{
    for(const table of ['enrollment_audit','enrollment_acceptances','enrollment_challenges','enrollment_proofs','enrollment_consumed']){await assert.rejects(db.prepare('DELETE FROM '+table).run());const key=table==='enrollment_consumed'?'challenge_id':'id';await assert.rejects(db.prepare('UPDATE '+table+" SET "+key+"='changed'").run());}
  });
});
