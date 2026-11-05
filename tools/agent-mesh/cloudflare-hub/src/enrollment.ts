import type { V2Principal } from "./v2";
import { FeatureFailure } from "./errors";
import { redact } from "./redaction";
import { LOGGER_SOURCE } from "./logger-artifact";
import type { AgentMessage } from "./index";

type Data=Record<string,unknown>;
type Bind=string|number|null;
interface Rule {version:number;sha256:string}
interface Audit {seq:number;id:string;agent:string;kind:string;at:number;payload_json:string;rulebook_version:number|null;rulebook_sha256:string|null}
interface Acceptance {id:string;agent:string;rulebook_version:number;rulebook_sha256:string;accepted_at:string;event_id:string;request_hash:string}
interface Challenge {id:string;agent:string;acceptance_id:string;project_id:string;code_hash:string;script_sha256:string;chat_path:string;commands_path:string;discovery_json:string;created_at:number;expires_at:number;message_id:string;event_id:string;request_hash:string}
interface Proof {id:string;agent:string;challenge_id:string;success:number;reason:string;evidence_json:string;request_hash:string}
const fail=(status:number,message:string):never=>{throw new FeatureFailure(status,message);};
const hashPattern=/^[a-f0-9]{64}$/;
async function digest(value:string){return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(value))),x=>x.toString(16).padStart(2,"0")).join("");}
function text(b:Data,key:string,max=200):string{const v=b[key];if(typeof v!=="string"||!v.trim()||v.length>max||/[\x00-\x1f]/.test(v))return fail(400,"Invalid "+key);return v;}
function allowed(b:Data,keys:string[]){if(Object.keys(b).some(k=>!keys.includes(k)))fail(400,"Unknown enrollment field");}
function canonicalPath(value:unknown):string{
  if(typeof value!=="string"||!value||value.length>2048||/[\x00-\x1f]/.test(value)||value.includes("[REDACTED]"))return fail(400,"Invalid folder path");
  const path=value.replace(/\\/g,"/").replace(/\/+/g,"/").replace(/\/$/,"");
  if(!/^(?:[A-Za-z]:\/|\/)/.test(path)||path.split("/").some(s=>s==="."||s==="..")||path===""||/^[A-Za-z]:$/.test(path))return fail(400,"An absolute non-root folder path is required");
  return /^[A-Za-z]:/.test(path)?path.toLowerCase():path;
}
function separated(chat:unknown,commands:unknown){const a=canonicalPath(chat),b=canonicalPath(commands);if(a===b||a.startsWith(b+"/")||b.startsWith(a+"/"))fail(400,"Chat and commands folders must be separate");return [a,b];}
export class Enrollment {
  constructor(private db:D1Database,
    private project:(id:string,p:V2Principal)=>Promise<void>,
    private write:(statements:D1PreparedStatement[])=>Promise<void>,
    private activity:(agent:string,kind:string,title:string,actor?:string)=>D1PreparedStatement[]){ }
  private stmt(sql:string,...args:Bind[]){return this.db.prepare(sql).bind(...args);}
  private async rows<T>(sql:string,...args:Bind[]){return (await this.stmt(sql,...args).all<T>()).results;}
  private async head(){return this.stmt("SELECT version,sha256 FROM rulebook_revisions ORDER BY version DESC LIMIT 1").first<Rule>();}
  private audit(id:string,agent:string,actor:string,kind:string,rule:Rule|null,payload:Data){return this.stmt("INSERT INTO enrollment_audit(id,agent,actor,kind,at,rulebook_version,rulebook_sha256,payload_json) VALUES(?,?,?,?,?,?,?,?)",id,agent,actor,kind,Date.now(),rule?.version??null,rule?.sha256??null,JSON.stringify(payload));}
  private ownerEvent(agent:string){return this.stmt("SELECT * FROM enrollment_audit WHERE agent=? AND kind IN ('revoke','allow') ORDER BY seq DESC LIMIT 1",agent).first<Audit>();}
  async settings(){return await this.stmt("SELECT * FROM enrollment_settings ORDER BY version DESC LIMIT 1").first<{version:number;enforcement_mode:string;grace_until:number;proof_interval_ms:number}>()||{version:0,enforcement_mode:"record_only",grace_until:0,proof_interval_ms:86400000};}
  async permitted(agent:string){const policy=await this.settings();if(policy.enforcement_mode!=="enforce"||Date.now()<policy.grace_until)return true;const status=await this.status(agent);return status.status==="Enrolled";}
  private statusSQL(from:string){return `SELECT n.agent,
    (SELECT json_object('version',version,'sha256',sha256) FROM rulebook_revisions ORDER BY version DESC LIMIT 1) AS rule_json,
    (SELECT json_object('seq',seq,'kind',kind,'at',at) FROM enrollment_audit WHERE agent=n.agent AND kind IN ('revoke','allow') ORDER BY seq DESC LIMIT 1) AS owner_json,
    (SELECT json_object('seq',e.seq,'rulebook_version',a.rulebook_version,'rulebook_sha256',a.rulebook_sha256,'accepted_at',a.accepted_at) FROM enrollment_audit e JOIN enrollment_acceptances a ON a.event_id=e.id WHERE e.agent=n.agent AND e.kind='accept' ORDER BY e.seq DESC LIMIT 1) AS acceptance_json,
    (SELECT json_object('id',p.id,'success',p.success,'reason',p.reason,'at',e.at) FROM enrollment_audit e JOIN enrollment_proofs p ON p.event_id=e.id WHERE e.agent=n.agent AND e.kind='proof' ORDER BY e.seq DESC LIMIT 1) AS proof_json,
    (SELECT json_object('seq',e.seq,'at',e.at,'script_sha256',c.script_sha256,'rulebook_version',a.rulebook_version,'rulebook_sha256',a.rulebook_sha256) FROM enrollment_audit e JOIN enrollment_proofs p ON p.event_id=e.id JOIN enrollment_challenges c ON c.id=p.challenge_id JOIN enrollment_acceptances a ON a.id=c.acceptance_id WHERE e.agent=n.agent AND e.kind='proof' AND p.success=1 ORDER BY e.seq DESC LIMIT 1) AS success_json
    FROM (${from}) n`;}
  private renderStatus(row:{agent:string;rule_json:string|null;owner_json:string|null;acceptance_json:string|null;proof_json:string|null;success_json:string|null},policy={enforcement_mode:"record_only",grace_until:0,proof_interval_ms:86400000}){
    const rule:Rule|null=JSON.parse(row.rule_json||"null"),owner:Audit|null=JSON.parse(row.owner_json||"null");
    const acceptance:(Acceptance&{seq:number})|null=JSON.parse(row.acceptance_json||"null");
    const proof:(Proof&{at:number})|null=JSON.parse(row.proof_json||"null");
    const success:{seq:number;at:number;script_sha256:string;rulebook_version:number;rulebook_sha256:string}|null=JSON.parse(row.success_json||"null");
    let state="Not enrolled";
    if(owner?.kind==="revoke")state="Revoked";
    else if(success&&success.seq>(owner?.seq||0)&&success.rulebook_version===rule?.version&&success.rulebook_sha256===rule?.sha256)state="Enrolled";
    else if(acceptance&&acceptance.seq>(owner?.seq||0))state=acceptance.rulebook_version===rule?.version&&acceptance.rulebook_sha256===rule?.sha256?"Pending":"Needs re-acceptance";
    else if(success&&success.seq>(owner?.seq||0))state="Needs re-acceptance";
    const due=!!success&&Date.now()-success.at>policy.proof_interval_ms;if(state==="Enrolled"&&due)state="Pending";
    return {agent:row.agent,status:state,mode:policy.enforcement_mode,proof_due:due,access_restricted:policy.enforcement_mode==="enforce"&&Date.now()>=policy.grace_until&&state!=="Enrolled",current_rulebook:rule,accepted_version:acceptance?.rulebook_version??null,accepted_hash:acceptance?.rulebook_sha256??null,accepted_at:acceptance?.accepted_at??null,last_proof:proof?{id:proof.id,success:!!proof.success,reason:proof.reason,at:proof.at}:null,script_sha256:success?.script_sha256??null,owner_action:owner?{kind:owner.kind,at:owner.at}:null};
  }
  async status(agent:string){
    const row=await this.stmt(this.statusSQL("SELECT ? AS agent"),agent).first<Parameters<Enrollment["renderStatus"]>[0]>();
    return this.renderStatus(row!,await this.settings());
  }
  async route(request:Request,p:V2Principal,b:Data,path:string[]):Promise<Response|null>{
    if(path[0]!=="enrollment")return null;
    const get=request.method==="GET",agent=p.agent!,actor=p.isAdmin?"arin":agent,url=new URL(request.url);
    if(path.length===2&&path[1]==="settings"){
      if(!p.isAdmin)fail(403,"Owner settings required");const current=await this.settings();if(get)return Response.json(current);
      allowed(b,["expected_version","enforcement_mode","grace_until","proof_interval_ms"]);
      const grace=b.grace_until??0,interval=b.proof_interval_ms??current.proof_interval_ms;
      if(b.expected_version!==current.version)fail(409,"Settings changed; reload");
      if(typeof b.enforcement_mode!=="string"||!["record_only","enforce"].includes(b.enforcement_mode)||typeof grace!=="number"||!Number.isSafeInteger(grace)||grace<0||typeof interval!=="number"||!Number.isSafeInteger(interval)||interval<60000||interval>2592000000)fail(400,"Invalid enrollment settings");
      await this.write([this.stmt("INSERT INTO enrollment_settings(version,enforcement_mode,grace_until,proof_interval_ms,actor,at) VALUES(?,?,?,?,?,?)",current.version+1,String(b.enforcement_mode),grace as number,interval as number,"arin",Date.now()),...this.activity("hub","enrollment.settings","Enrollment policy updated","arin")]);return Response.json(await this.settings());
    }
    if(path.length===2&&path[1]==="logger.py"&&get)return new Response(LOGGER_SOURCE,{headers:{"Content-Type":"text/x-python; charset=utf-8","Content-Disposition":"attachment; filename=hub_command_log.py","X-Script-SHA256":await digest(LOGGER_SOURCE)}});
    if(path.length===2&&path[1]==="logger"&&get)return Response.json({filename:"hub_command_log.py",sha256:await digest(LOGGER_SOURCE),download:p.isAdmin?"/api/v2/admin/enrollment/logger.py":"/api/v2/enrollment/logger.py",bytes:new TextEncoder().encode(LOGGER_SOURCE).byteLength,network_calls:false,executes_commands:false});
    if(path.length===2&&path[1]==="onboarding"&&get){
      const project=url.searchParams.get('project')||'';await this.project(project,p);
      const document=await this.stmt("SELECT * FROM docs WHERE project_id=? AND doc_id='onboarding'",project).first();if(!document)fail(404,"Onboarding not published");return Response.json({document,mode:(await this.settings()).enforcement_mode});
    }
    if(path.length===2&&path[1]==="challenges"&&get){
      if(p.isAdmin)fail(403,"Agent recovery inbox required");
      const rows=await this.rows<{envelope:string}>("SELECT o.envelope FROM enrollment_challenges c JOIN feature_outbox o ON o.id=c.message_id JOIN enrollment_audit e ON e.id=c.event_id WHERE c.agent=? ORDER BY e.seq DESC LIMIT 1",agent);
      return Response.json({messages:rows.map(x=>JSON.parse(x.envelope))});
    }
    if(path.length===2&&path[1]==="status"&&get){if(p.isAdmin)fail(400,"Use the owner agent-status route");return Response.json(await this.status(agent));}
    if(path[1]==="agents"&&p.isAdmin){
      if(path.length===2&&get){const rawLimit=url.searchParams.get("limit")??"50",cursor=url.searchParams.get("cursor")??"";if(!/^\d+$/.test(rawLimit)||Number(rawLimit)<1||Number(rawLimit)>200)fail(400,"Invalid page");const limit=Number(rawLimit);
        const policy=await this.settings();const agents=await this.rows<Parameters<Enrollment["renderStatus"]>[0]>(this.statusSQL("SELECT id AS agent FROM (SELECT id FROM agent_registry UNION SELECT agent AS id FROM enrollment_audit) WHERE id>? ORDER BY id LIMIT ?"),cursor,limit+1);return Response.json({items:agents.slice(0,limit).map(x=>this.renderStatus(x,policy)),has_more:agents.length>limit,next_cursor:agents.length>limit?agents[limit-1].agent:null,mode:policy.enforcement_mode});}
      if(path.length===3&&get)return Response.json(await this.status(path[2]));
      if(path.length===4&&["revoke","allow"].includes(path[3])&&!get){allowed(b,["id","reason"]);const id=text(b,"id"),reason=text(b,"reason",2000),who=path[2];if(!who||who.length>200||/[\s\x00-\x1f]/.test(who)||redact(who)!==who)fail(400,"Invalid agent");const kind=path[3];const old=await this.stmt("SELECT * FROM enrollment_audit WHERE id=?",id).first<Audit>();
        if(old){if(old.agent!==who||old.kind!==kind||JSON.parse(old.payload_json).reason!==reason)fail(409,"Owner event id conflict");return Response.json({duplicate:true,...await this.status(who)});}
        await this.write([this.audit(id,who,actor,kind,await this.head(),{reason}),...this.activity(who,"enrollment."+kind,"Enrollment "+kind+" recorded for "+who,actor)]);return Response.json({duplicate:false,...await this.status(who)},{status:201});}
    }
    if(path.length===2&&path[1]==="audit"&&get){
      const who=p.isAdmin?url.searchParams.get("agent"):agent,limit=Number(url.searchParams.get("limit")||50),cursor=Number(url.searchParams.get("cursor")||0);if(!Number.isInteger(limit)||limit<1||limit>200||!Number.isSafeInteger(cursor)||cursor<0)fail(400,"Invalid page");
      const rows=await this.rows<Audit>("SELECT * FROM enrollment_audit WHERE seq>?"+(who?" AND agent=?":"")+" ORDER BY seq LIMIT ?",cursor,...(who?[who]:[]),limit+1);return Response.json({items:rows.slice(0,limit),has_more:rows.length>limit,next_cursor:rows.length>limit?String(rows[limit-1].seq):null});
    }
    if(p.isAdmin)fail(403,"Enrollment evidence must come from the agent identity");
    if(path.length!==2||get)return fail(404,"Unknown enrollment route");
    if(path[1]==="accept"){
      allowed(b,["id","name","rulebook_version","rulebook_sha256","accepted_at"]);const id=text(b,"id"),name=text(b,"name"),accepted=text(b,"accepted_at",100);if(name!==agent)fail(403,"Acceptance identity must match token");
      if(!Number.isFinite(Date.parse(accepted))||Date.parse(accepted)>Date.now()+60000)fail(400,"Invalid acceptance time");
      const fingerprint=await digest(JSON.stringify([agent,b.rulebook_version,b.rulebook_sha256,accepted]));const old=await this.stmt("SELECT * FROM enrollment_acceptances WHERE id=?",id).first<Acceptance>();
      if(old){if(old.agent!==agent||old.request_hash!==fingerprint)fail(409,"Acceptance id conflict");return Response.json({id,duplicate:true,...await this.status(agent)});}
      const rule=await this.head();if(!rule||rule.version!==b.rulebook_version||rule.sha256!==b.rulebook_sha256)return fail(409,"Read and accept the current rule book first");
      await this.write([this.audit("accept:"+id,agent,actor,"accept",rule,{accepted_at:accepted}),this.stmt("INSERT INTO enrollment_acceptances(id,agent,rulebook_version,rulebook_sha256,accepted_at,event_id,request_hash) VALUES(?,?,?,?,?,?,?)",id,agent,rule.version,rule.sha256,accepted,"accept:"+id,fingerprint)]);
      return Response.json({id,duplicate:false,...await this.status(agent)},{status:201});
    }
    if(path[1]==="challenges"){
      allowed(b,["id","acceptance_id","project","script_sha256","chat_path","commands_path","discovery"]);
      const id=text(b,"id"),acceptanceId=text(b,"acceptance_id"),project=text(b,"project");await this.project(project,p);
      separated(b.chat_path,b.commands_path);const chat=text(b,"chat_path",2048),commands=text(b,"commands_path",2048);
      const discovery=b.discovery as Data;if(!discovery||Array.isArray(discovery)||typeof discovery!=="object"||typeof discovery.tool!=="string"||typeof discovery.method!=="string"||!Array.isArray(discovery.evidence_paths)||!discovery.evidence_paths.length||discovery.evidence_paths.length>20||discovery.evidence_paths.some(x=>typeof x!=="string"||x.length>2048)||!Number.isFinite(Date.parse(String(discovery.verified_at))))fail(400,"Actual chat-path discovery evidence is required");
      allowed(discovery,["chat_path","tool","method","evidence_paths","verified_at","session_metadata_verified"]);
      text(discovery,"tool");text(discovery,"method",1000);
      for(const evidencePath of discovery.evidence_paths as string[])canonicalPath(evidencePath);
      if(canonicalPath(discovery.chat_path)!==canonicalPath(chat))fail(400,"Discovery path does not match installation");
      const script=await digest(LOGGER_SOURCE);if(b.script_sha256!==script)fail(400,"Install the current logger artifact");
      const fingerprint=await digest(JSON.stringify([agent,acceptanceId,project,script,chat,commands,discovery]));const old=await this.stmt("SELECT * FROM enrollment_challenges WHERE id=?",id).first<Challenge>();
      if(old){if(old.agent!==agent||old.request_hash!==fingerprint)fail(409,"Challenge id conflict");return Response.json({id,message_id:old.message_id,expires_at:new Date(old.expires_at).toISOString(),duplicate:true});}
      const acceptance=await this.stmt("SELECT a.*,e.seq FROM enrollment_acceptances a JOIN enrollment_audit e ON e.id=a.event_id WHERE a.id=? AND a.agent=?",acceptanceId,agent).first<Acceptance&{seq:number}>();
      const rule=await this.head(),owner=await this.ownerEvent(agent);if(!acceptance||!rule||acceptance.rulebook_version!==rule.version||acceptance.rulebook_sha256!==rule.sha256||acceptance.seq<=(owner?.seq||0))return fail(409,"Current acceptance required after the last owner action");
      if(owner?.kind==="revoke")fail(409,"Owner must allow a fresh enrollment attempt");
      const code=Array.from(crypto.getRandomValues(new Uint8Array(32)),x=>x.toString(16).padStart(2,"0")).join(""),now=Date.now(),expires=now+5*60*1000,messageId="enrollment:"+crypto.randomUUID();
      const message:AgentMessage={id:messageId,project,from:"hub-supervisor",to:agent,type:"enrollment.challenge",timestamp:new Date(now).toISOString(),body:JSON.stringify({challenge_id:id,code,rulebook_sha256:rule.sha256,script_sha256:script,expires_at:new Date(expires).toISOString()}),subject:"enrollment challenge"};
      await this.write([this.audit("challenge:"+id,agent,actor,"challenge",rule,{project,acceptance_id:acceptanceId,script_sha256:script,chat_path:chat,commands_path:commands,discovery,expires_at:expires}),this.stmt("INSERT INTO enrollment_challenges(id,agent,acceptance_id,project_id,code_hash,script_sha256,chat_path,commands_path,discovery_json,created_at,expires_at,message_id,event_id,request_hash) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",id,agent,acceptanceId,project,await digest(code),script,chat,commands,JSON.stringify(discovery),now,expires,messageId,"challenge:"+id,fingerprint),this.stmt("INSERT INTO feature_outbox(id,envelope) VALUES(?,?)",messageId,JSON.stringify(message))]);
      return Response.json({id,message_id:messageId,expires_at:new Date(expires).toISOString(),duplicate:false},{status:201});
    }
    if(path[1]==="proofs")return this.prove(agent,b);
    return fail(404,"Unknown enrollment route");
  }
  private async prove(agent:string,b:Data){
    allowed(b,["id","challenge_id","code","script_sha256","chat_path","commands_path","rulebook_sha256","message_id","log_entry_sha256","logged_at"]);
    const id=text(b,"id"),challengeId=text(b,"challenge_id");const fingerprint=await digest(JSON.stringify([agent,...["challenge_id","code","script_sha256","chat_path","commands_path","rulebook_sha256","message_id","log_entry_sha256","logged_at"].map(k=>b[k]??null)]));
    const old=await this.stmt("SELECT * FROM enrollment_proofs WHERE id=?",id).first<Proof>();if(old){if(old.agent!==agent||old.request_hash!==fingerprint)fail(409,"Proof id conflict");return Response.json({id,success:!!old.success,reason:old.reason,duplicate:true,...await this.status(agent)});}
    const c=await this.stmt("SELECT * FROM enrollment_challenges WHERE id=? AND agent=?",challengeId,agent).first<Challenge>();if(!c)fail(404,"Challenge not found");
    const consumed=await this.stmt("SELECT proof_id FROM enrollment_consumed WHERE challenge_id=?",challengeId).first();
    const rule=await this.head(),owner=await this.ownerEvent(agent);const latest=await this.stmt("SELECT c.id,e.seq FROM enrollment_challenges c JOIN enrollment_audit e ON e.id=c.event_id WHERE c.agent=? ORDER BY e.seq DESC LIMIT 1",agent).first<{id:string;seq:number}>();
    const acceptance=await this.stmt("SELECT * FROM enrollment_acceptances WHERE id=?",c!.acceptance_id).first<Acceptance>();
    let reason="ok";
    if(consumed)reason="already_used";
    else if(owner?.kind==="revoke")reason="revoked";
    else if(latest?.id!==c!.id)reason="superseded";
    else if(owner&&latest!.seq<=owner.seq)reason="owner_reset";
    else if(Date.now()>=c!.expires_at)reason="expired";
    else if(!rule||rule.version!==acceptance!.rulebook_version||rule.sha256!==acceptance!.rulebook_sha256||b.rulebook_sha256!==rule.sha256)reason="rulebook_changed";
    else if(typeof b.code!=="string"||await digest(b.code)!==c!.code_hash)reason="wrong_code";
    else if(b.script_sha256!==c!.script_sha256)reason="wrong_script_hash";
    else {try{separated(b.chat_path,b.commands_path);if(canonicalPath(b.chat_path)!==canonicalPath(c!.chat_path)||canonicalPath(b.commands_path)!==canonicalPath(c!.commands_path))reason="folder_mismatch";}catch{reason="invalid_folders";}}
    if(reason==="ok"&&(b.message_id!==c!.message_id||typeof b.log_entry_sha256!=="string"||!hashPattern.test(b.log_entry_sha256)||!Number.isFinite(Date.parse(String(b.logged_at)))||Date.parse(String(b.logged_at))<c!.created_at-60000||Date.parse(String(b.logged_at))>Date.now()+60000))reason="invalid_log_receipt";
    if(reason==="ok"){
      const outbox=await this.stmt("SELECT envelope FROM feature_outbox WHERE id=?",c!.message_id).first<{envelope:string}>();
      if(!outbox)reason="invalid_log_receipt";
      else {
        const message=JSON.parse(outbox.envelope) as AgentMessage;
        const record={message_id:c!.message_id,project:c!.project_id,reply_to:message.reply_to??null,sender:"hub-supervisor",status:"received",text:message.body,time:b.logged_at};
        const ordered=Object.fromEntries(Object.entries(record).sort(([a],[z])=>a.localeCompare(z)));
        if(await digest(JSON.stringify(ordered))!==b.log_entry_sha256)reason="wrong_log_receipt";
      }
    }
    // Codes are only in the private expiring challenge message, not duplicated in audit/proof rows.
    const evidence=Object.fromEntries(Object.entries(b).filter(([k])=>k!=="code"));const payload={challenge_id:challengeId,success:reason==="ok",reason,...evidence};
    const ops=[this.audit("proof:"+id,agent,agent,"proof",acceptance?{version:acceptance.rulebook_version,sha256:acceptance.rulebook_sha256}:null,payload),this.stmt("INSERT INTO enrollment_proofs(id,challenge_id,agent,success,reason,evidence_json,event_id,request_hash) VALUES(?,?,?,?,?,?,?,?)",id,challengeId,agent,reason==="ok"?1:0,reason,JSON.stringify(evidence),"proof:"+id,fingerprint)];
    if(!consumed)ops.push(this.stmt("INSERT INTO enrollment_consumed(challenge_id,proof_id) VALUES(?,?)",challengeId,id));
    ops.push(...this.activity(agent,"enrollment.proof","Enrollment proof "+reason+" for "+agent));await this.write(ops);
    return Response.json({id,success:reason==="ok",reason,duplicate:false,...await this.status(agent)},{status:201});
  }
}
