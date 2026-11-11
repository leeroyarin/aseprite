import type { V2Principal } from "./v2";
import { FeatureFailure } from "./errors";
import { INITIAL_RULEBOOK } from "./rulebook";

type Data = Record<string, unknown>;
type Bind = string | number | null;
interface Rule { version:number; sha256:string; body:string; author:string; created_at:number; note:string }
interface Task { id:string; project_id:string; owner:string; assignee:string; state:string }
interface Usage { seq:number; id:string; project_id:string; agent:string; received_at:number; metrics_json:string; context_remaining_percent:number|null; budget_remaining_percent:number|null; handoff_recommended:number; source:string; unavailable_reason:string|null; request_hash:string }
interface Handoff { seq:number; id:string; task_id:string; project_id:string; author:string; push_state:string; commit_hash:string|null; branch:string; repository:string; body_json:string; request_hash:string; acceptances_json?:string }
export const USAGE_WARNING_REMAINING_PERCENT=15;
export const USAGE_FRESH_MS=10*60*1000;
const fail=(status:number,message:string):never=>{throw new FeatureFailure(status,message);};
const utf8=new TextEncoder();
function text(b:Data,key:string,max=200,empty=false):string {
  const value=b[key];
  if(typeof value!=="string"||(!empty&&!value.trim())||utf8.encode(value).byteLength>max||/[\x00-\x08\x0b\x0c\x0e-\x1f]/.test(value))return fail(400,"Invalid "+key);
  return value;
}
function identifier(b:Data,key:string):string {
  const value=text(b,key,200);
  if(/[\s\x00-\x1f]/.test(value))return fail(400,"Invalid "+key);
  return value;
}
function fields(b:Data,allowed:string[]){if(Object.keys(b).some(x=>!allowed.includes(x)))fail(400,"Unknown field");}
function strings(b:Data,key:string,nonempty=false):string[] {
  const value=b[key];
  if(!Array.isArray(value)||value.length>50||(nonempty&&!value.length))return fail(400,"Invalid "+key);
  return value.map(x=>text({value:x},"value",2000));
}
function numeric(b:Data,key:string,integer=false):number|null {
  const value=b[key];if(value===undefined||value===null)return null;
  if(typeof value!=="number"||!Number.isFinite(value)||value<0||value>Number.MAX_SAFE_INTEGER||(integer&&!Number.isInteger(value)))return fail(400,"Invalid "+key);
  return value;
}
function pagination(url:URL){
  const limit=url.searchParams.get("limit")??"50",cursor=url.searchParams.get("cursor")??"0";
  if(!/^\d+$/.test(limit)||Number(limit)<1||Number(limit)>200||!/^\d+$/.test(cursor)||!Number.isSafeInteger(Number(cursor)))return fail(400,"Invalid page");
  return {limit:Number(limit),cursor:Number(cursor)};
}
function paged<T extends {seq:number}>(all:T[],limit:number){const items=all.slice(0,limit);return {items,has_more:all.length>limit,next_cursor:all.length>limit?String(items[items.length-1].seq):null};}
async function hash(value:string){return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",utf8.encode(value))),x=>x.toString(16).padStart(2,"0")).join("");}
export class Governance {
  constructor(private db:D1Database,
    private project:(id:string,p:V2Principal)=>Promise<void>,
    private write:(statements:D1PreparedStatement[])=>Promise<void>,
    private event:(project:string,actor:string,type:string,title:string,body:string)=>D1PreparedStatement[],
    private notice:(project:string,id:string,body:string)=>Promise<D1PreparedStatement[]>){ }
  private stmt(sql:string,...args:Bind[]){return this.db.prepare(sql).bind(...args);}
  private async rows<T>(sql:string,...args:Bind[]){return (await this.stmt(sql,...args).all<T>()).results;}
  private owner(p:V2Principal){if(!p.isAdmin)fail(403,"Only Arin edits the rule book");}
  private usage(row:Usage){return {...row,metrics:JSON.parse(row.metrics_json),handoff_recommended:!!row.handoff_recommended,stale:Date.now()-row.received_at>USAGE_FRESH_MS,reported_by_agent:true};}
  private handoff(row:Handoff){return {...row,details:JSON.parse(row.body_json),acceptances:JSON.parse(row.acceptances_json||"[]"),evidence_status:"agent_reported",remote_verified_by_hub:false};}
  async route(request:Request,p:V2Principal,b:Data,path:string[]):Promise<Response|null>{
    const url=new URL(request.url),get=request.method==="GET",actor=p.isAdmin?"arin":p.agent!;
    if(path[0]==="rulebook"){
      if(path.length===2&&path[1]==="template"&&get){this.owner(p);return Response.json({body:INITIAL_RULEBOOK,sha256:await hash(INITIAL_RULEBOOK),published:false});}
      if(path.length===2&&path[1]==="revisions"&&get){
        const {limit,cursor}=pagination(url);const rules=await this.rows<Rule>("SELECT * FROM rulebook_revisions WHERE version>? ORDER BY version LIMIT ?",cursor,limit+1);
        const items=rules.slice(0,limit);return Response.json({items,has_more:rules.length>limit,next_cursor:rules.length>limit?String(items[items.length-1].version):null});
      }
      if(path.length===3&&path[1]==="revisions"&&get){if(!/^\d+$/.test(path[2]))fail(400,"Invalid version");const row=await this.stmt("SELECT * FROM rulebook_revisions WHERE version=?",Number(path[2])).first<Rule>();if(!row)fail(404,"Rule book version not found");return Response.json(row);}
      if(path.length!==1)return fail(404,"Unknown rule book route");
      const current=await this.stmt("SELECT * FROM rulebook_revisions ORDER BY version DESC LIMIT 1").first<Rule>();
      if(get){if(!current)fail(404,"Arin has not published the rule book");return Response.json(current);}
      this.owner(p);fields(b,["body","expected_version","note"]);
      if(b.expected_version!==(current?.version??0))fail(409,"Rule book changed; reload the current version");
      const body=b.body===undefined&&!current?INITIAL_RULEBOOK:text(b,"body",32768);
      const version=(current?.version??0)+1,sha256=await hash(body),now=Date.now(),note=b.note===undefined?"Owner revision":text(b,"note",2000);
      await this.write([this.stmt("INSERT INTO rulebook_revisions(version,sha256,body,author,created_at,note) VALUES(?,?,?,?,?,?)",version,sha256,body,actor,now,note),...this.event("_hub",actor,"rulebook.revised","Rule book version "+version,"SHA-256 "+sha256)]);
      return Response.json({version,sha256,body,author:actor,created_at:now,note},{status:201});
    }
    if(path[0]!=="projects"||!path[1])return null;
    if(path[2]!=="usage"&&!(path[2]==="tasks"&&path[3]&&path[4]==="handoffs"))return null;
    const project=path[1];await this.project(project,p);
    if(path[2]==="usage"){
      if(path.length===4&&path[3]==="history"&&get){const {limit,cursor}=pagination(url);const agent=p.isAdmin?url.searchParams.get("agent"):actor;
        const rows=await this.rows<Usage>("SELECT * FROM usage_reports WHERE project_id=? AND seq>?"+(agent?" AND agent=?":"")+" ORDER BY seq LIMIT ?",project,cursor,...(agent?[agent]:[]),limit+1);return Response.json(paged(rows.map(x=>this.usage(x)),limit));}
      if(path.length!==3)return fail(404,"Unknown usage route");
      if(get){const rows=await this.rows<Usage>("SELECT u.* FROM usage_reports u WHERE project_id=? AND seq=(SELECT MAX(v.seq) FROM usage_reports v WHERE v.project_id=u.project_id AND v.agent=u.agent) ORDER BY agent",project);
        return Response.json({reports:rows.map(x=>this.usage(x)),warning_remaining_percent:USAGE_WARNING_REMAINING_PERCENT,fresh_for_ms:USAGE_FRESH_MS,missing_agents:"unknown"});}
      if(p.isAdmin)fail(403,"Agents report their own usage");
      fields(b,["id","source","context_used_tokens","context_limit_tokens","budget_remaining_percent","unavailable_reason"]);
      const id=identifier(b,"id"),source=text(b,"source",200),used=numeric(b,"context_used_tokens",true),limit=numeric(b,"context_limit_tokens",true),budget=numeric(b,"budget_remaining_percent");
      if(limit===0||(limit!==null&&used!==null&&used>limit)||(budget!==null&&budget>100))fail(400,"Invalid usage range");
      const remaining=used!==null&&limit!==null?100*(limit-used)/limit:null;
      const unknown=used===null&&limit===null&&budget===null;
      const reason=b.unavailable_reason===undefined?null:text(b,"unavailable_reason",2000);
      if(unknown&&!reason)fail(400,"Unknown usage requires an unavailable_reason");
      const recommended=(remaining!==null&&remaining<=USAGE_WARNING_REMAINING_PERCENT)||(budget!==null&&budget<=USAGE_WARNING_REMAINING_PERCENT);
      const metrics={context_used_tokens:used,context_limit_tokens:limit,budget_remaining_percent:budget};
      const fingerprint=await hash(JSON.stringify([project,actor,source,metrics,reason]));
      const old=await this.stmt("SELECT * FROM usage_reports WHERE id=?",id).first<Usage>();
      if(old){if(old.agent!==actor||old.project_id!==project||old.request_hash!==fingerprint)fail(409,"Usage id conflict");return Response.json({...this.usage(old),duplicate:true});}
      const previous=await this.stmt("SELECT * FROM usage_reports WHERE project_id=? AND agent=? ORDER BY seq DESC LIMIT 1",project,actor).first<Usage>();
      const now=Date.now(),ops=[this.stmt("INSERT INTO usage_reports(id,project_id,agent,received_at,metrics_json,context_remaining_percent,budget_remaining_percent,handoff_recommended,source,unavailable_reason,request_hash) VALUES(?,?,?,?,?,?,?,?,?,?,?)",id,project,actor,now,JSON.stringify(metrics),remaining,budget,recommended?1:0,source,reason,fingerprint),...this.event(project,actor,"usage.reported",actor+" usage: "+(unknown?"unknown":recommended?"handoff recommended":"reported"),"")];
      if(recommended&&(!previous?.handoff_recommended||now-previous.received_at>USAGE_FRESH_MS))ops.push(...await this.notice(project,"usage:"+id,actor+" reports low remaining usage. Handoff recommended: a pushed work branch, commit and final handoff evidence are needed. No command is executed by this notice."));
      await this.write(ops);const row=await this.stmt("SELECT * FROM usage_reports WHERE id=?",id).first<Usage>();return Response.json({...this.usage(row!),duplicate:false},{status:201});
    }
    const task=await this.stmt("SELECT * FROM tasks WHERE id=? AND project_id=?",path[3],project).first<Task>();
    if(!task)return fail(404,"Task not found");
    if(path.length===6&&get){
      const row=await this.stmt("SELECT h.*, (SELECT json_group_array(json_object('agent',a.agent,'received_at',a.received_at,'checked_out_commit',a.checked_out_commit)) FROM handoff_acceptances a WHERE a.handoff_id=h.id) AS acceptances_json FROM task_handoffs h WHERE h.id=? AND h.task_id=? AND h.project_id=?",path[5],task.id,project).first<Handoff>();
      if(!row)return fail(404,"Handoff not found");return Response.json(this.handoff(row));
    }
    if(path.length===7&&path[6]==="accept"&&!get)return this.accept(task,p,b,path[5]);
    if(path.length!==5)return fail(404,"Unknown handoff route");
    if(get){const {limit,cursor}=pagination(url);const rows=await this.rows<Handoff>("SELECT h.*, (SELECT json_group_array(json_object('agent',a.agent,'received_at',a.received_at,'checked_out_commit',a.checked_out_commit)) FROM handoff_acceptances a WHERE a.handoff_id=h.id) AS acceptances_json FROM task_handoffs h WHERE h.project_id=? AND h.task_id=? AND h.seq>? ORDER BY h.seq LIMIT ?",project,task.id,cursor,limit+1);return Response.json(paged(rows.map(x=>this.handoff(x)),limit));}
    if(p.isAdmin||(actor!==task.owner&&actor!==task.assignee))fail(403,"Only the task owner or current assignee reports a handoff");
    fields(b,["id","work_state","push_state","repository","branch","commit","remote_ref_commit","remote_ref_checked_at","push_evidence","push_blocker","working_tree_clean","dirty_paths","handoff_path","summary","completed","remaining","tests","risks","next_steps"]);
    const id=identifier(b,"id"),state=text(b,"work_state"),push=text(b,"push_state"),repository=text(b,"repository",1024),branch=text(b,"branch",200);
    if(!["incomplete","complete","blocked"].includes(state)||!["pushed","not_pushed"].includes(push))fail(400,"Invalid work or push state");
    let repo:URL;try{repo=new URL(repository);}catch{return fail(400,"Repository must be an HTTPS URL");}
    if(repo.protocol!=="https:"||repo.username||repo.password||repo.search||repo.hash||!repo.hostname||repo.pathname==="/")fail(400,"Invalid repository URL");
    if(/[\s~^:?*\[\]\\\x00-\x1f]/.test(branch)||branch.includes("..")||branch.includes("@{")||branch.startsWith("-")||branch.startsWith("/")||branch.endsWith("/")||branch.endsWith(".")||branch.endsWith(".lock")||branch.split("/").some(x=>!x||x.startsWith("."))||["main","master","production","release","HEAD","refs"].includes(branch)||branch.startsWith("refs/"))fail(400,"Use a dedicated work branch, not a protected branch or invalid ref");
    const commit=b.commit==null?null:text(b,"commit",64);
    if(commit&&!/^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(commit))fail(400,"A full commit hash is required");
    let remote:string|null=null,checked:string|null=null,evidence:string|null=null,blocker:string|null=null;
    if(push==="pushed"){
      remote=text(b,"remote_ref_commit",64);checked=text(b,"remote_ref_checked_at",100);evidence=text(b,"push_evidence",4000);
      if(!commit||remote!==commit||!Number.isFinite(Date.parse(checked))||Date.parse(checked)>Date.now()+60000)fail(400,"Pushed handoff requires matching commit and remote read-back evidence");
    }else{blocker=text(b,"push_blocker",2000);if(b.remote_ref_commit!=null||b.remote_ref_checked_at!=null||b.push_evidence!=null)fail(400,"Not-pushed handoff cannot claim remote proof");}
    if(typeof b.working_tree_clean!=="boolean")fail(400,"working_tree_clean must be explicit");
    const dirty=b.dirty_paths===undefined?[]:strings(b,"dirty_paths");if((!b.working_tree_clean&&!dirty.length)||(b.working_tree_clean&&dirty.length))fail(400,"Dirty paths must match the working-tree report");
    const handoffPath=text(b,"handoff_path",1024);if(/^(?:[a-zA-Z]:|[\\/])/.test(handoffPath)||handoffPath.split(/[\\/]/).includes(".."))fail(400,"Handoff path must be repository-relative");
    const completed=strings(b,"completed"),remaining=strings(b,"remaining",state!=="complete"),risks=strings(b,"risks"),next=strings(b,"next_steps",true),summary=text(b,"summary",4000);
    if(!Array.isArray(b.tests)||!b.tests.length||b.tests.length>30)fail(400,"Test evidence must include a result or explicit not_run");
    const tests=(b.tests as unknown[]).map((t:unknown)=>{if(!t||typeof t!=="object"||Array.isArray(t))return fail(400,"Invalid test evidence");const v=t as Data;fields(v,["command","status","passed","failed","details"]);const status=text(v,"status"),command=text(v,"command",2000),details=text(v,"details",4000),passed=numeric(v,"passed",true),failed=numeric(v,"failed",true);
      if(!["passed","failed","not_run"].includes(status)||(status==="passed"&&(passed===null||failed!==0))||(status==="failed"&&(passed===null||failed===null||failed===0))||(status==="not_run"&&(passed!==null||failed!==null)))fail(400,"Test status must agree with exact counts or not_run");return {command,status,passed,failed,details};});
    const details={work_state:state,push_state:push,repository,branch,commit,remote_ref_commit:remote,remote_ref_checked_at:checked,push_evidence:evidence,push_blocker:blocker,working_tree_clean:b.working_tree_clean,dirty_paths:dirty,handoff_path:handoffPath,summary,completed,remaining,tests,risks,next_steps:next};
    const body=JSON.stringify(details),fingerprint=await hash(JSON.stringify([project,task.id,actor,details]));
    const old=await this.stmt("SELECT * FROM task_handoffs WHERE id=?",id).first<Handoff>();
    if(old){if(old.author!==actor||old.project_id!==project||old.task_id!==task.id||old.request_hash!==fingerprint)fail(409,"Handoff id conflict");return Response.json({...this.handoff(old),duplicate:true});}
    await this.write([this.stmt("INSERT INTO task_handoffs(id,task_id,project_id,author,received_at,work_state,push_state,repository,branch,commit_hash,body_json,request_hash) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",id,task.id,project,actor,Date.now(),state,push,repository,branch,commit,body,fingerprint),...this.event(project,actor,"task.handoff","Handoff for "+task.id+": "+state+", "+push,"Branch "+branch+"; commit "+(commit||"unknown")+"; evidence agent-reported"),...await this.notice(project,"handoff:"+id,"Handoff recorded for task "+task.id+" by "+actor+". Branch "+branch+"; commit "+(commit||"unknown")+"; "+push+" (agent-reported). Read the handoff record and verify the checkpoint before continuing.")]);
    const row=await this.stmt("SELECT * FROM task_handoffs WHERE id=?",id).first<Handoff>();return Response.json({...this.handoff(row!),duplicate:false},{status:201});
  }
  private async accept(task:Task,p:V2Principal,b:Data,id:string){
    if(p.isAdmin||p.agent!==task.assignee)fail(403,"Only the current task assignee acknowledges continuation");
    fields(b,["id","checked_out_commit","notes"]);const requestId=identifier(b,"id"),commit=text(b,"checked_out_commit",64),notes=text(b,"notes",4000);
    const handoff=await this.stmt("SELECT * FROM task_handoffs WHERE id=? AND task_id=? AND project_id=?",id,task.id,task.project_id).first<Handoff>();
    if(!handoff)fail(404,"Handoff not found");
    const latest=await this.stmt("SELECT id FROM task_handoffs WHERE task_id=? ORDER BY seq DESC LIMIT 1",task.id).first<{id:string}>();
    if(latest?.id!==id||handoff!.push_state!=="pushed"||commit!==handoff!.commit_hash)fail(409,"Use the latest pushed checkpoint and report its checked-out commit");
    const existing=await this.stmt("SELECT * FROM handoff_acceptances WHERE handoff_id=? AND agent=?",id,p.agent!).first<{id:string;checked_out_commit:string;notes:string}>();
    if(existing){if(existing.checked_out_commit!==commit||existing.notes!==notes||existing.id!==requestId)fail(409,"Acceptance already recorded");return Response.json({ok:true,duplicate:true,evidence_status:"agent_reported"});}
    if(await this.stmt("SELECT id FROM handoff_acceptances WHERE id=?",requestId).first())fail(409,"Acceptance id conflict");
    await this.write([this.stmt("INSERT INTO handoff_acceptances(id,handoff_id,agent,received_at,checked_out_commit,notes) VALUES(?,?,?,?,?,?)",requestId,id,p.agent!,Date.now(),commit,notes),this.stmt("INSERT INTO task_events(task_id,ts,actor,event,assignee) VALUES(?,?,?,?,?)",task.id,Date.now(),p.agent!,"handoff.accepted",p.agent!),...this.event(task.project_id,p.agent!,"handoff.accepted","Continuation acknowledged for "+task.id,"Handoff "+id+"; checked-out commit "+commit+" (agent-reported)")]);
    return Response.json({ok:true,duplicate:false,evidence_status:"agent_reported"},{status:201});
  }
}
