import type { V2Principal } from "./v2";
import type { AgentMessage } from "./index";
import { redact } from "./redaction";
import { canReadChannel, canPostChannel, type HubIdentity } from "./permissions";
import { PORTAL_HTML, PORTAL_JS } from "./portal";

import { FeatureFailure } from "./errors";
export { FeatureFailure } from "./errors";
import { Governance, USAGE_FRESH_MS } from "./governance";
type Data = Record<string, unknown>;
interface Project { id:string; name:string; description:string; agents_json:string; roles_json:string; created_at:number }
interface Channel { id:string; project_id:string; type:"announce"|"group"|"dm"; name:string; topic:string; members_json:string }
interface Agent { id:string; name:string; role:string; last_seen:number; health:string; status_json:string; checked_at:number; registered_at:number }
interface Task { id:string; project_id:string; title:string; owner:string; assignee:string; state:string }
interface Doc { project_id:string; doc_id:string; title:string; purpose:string; body:string; version:number; updated_by:string; updated_at:number }
interface Suggestion {id:string; project_id:string; doc_id:string; author:string; body:string; status:string}
const fail=(status:number,text:string):never=>{throw new FeatureFailure(status,text);};
function str(body:Data,key:string,fallback?:string,max=200):string {
  const v=body[key]??fallback;
  if(typeof v!=="string"||!v.trim()||v.length>max||/[\x00-\x08\x0b\x0c\x0e-\x1f]/.test(v)) return fail(400,"Invalid "+key);
  return v.trim();
}
function list(body:Data,key:string):string[] {
  const v=body[key];
  if(!Array.isArray(v)||v.length>100||v.some(x=>typeof x!=="string"||!x||x.length>100||/[\s\x00-\x1f]/.test(x))) return fail(400,"Invalid "+key);
  return [...new Set(v as string[])];
}
function page(url:URL){
  const n=url.searchParams.get("limit")??"50",c=url.searchParams.get("cursor")??"0";
  if(!/^\d+$/.test(n)||Number(n)<1||Number(n)>200||!/^\d+$/.test(c)||!Number.isSafeInteger(Number(c))) return fail(400,"Invalid page");
  return {limit:Number(n),cursor:Number(c)};
}
function paged<T>(rows:T[],limit:number,cursor:(row:T)=>number){
  const items=rows.slice(0,limit);return {items,has_more:rows.length>limit,next_cursor:rows.length>limit?String(cursor(items[items.length-1])):null};
}
export class ProjectFeatures {
  private governanceService?: Governance;
  private get governance(): Governance {
    return this.governanceService ||= new Governance(this.db,
      async (id,p)=>{await this.project(id,p);},
      (statements)=>this.write(statements),
      (project,actor,type,title,body)=>this.event(project,actor,type,title,body),
      async (project,id,body)=>this.notice(project,id,body));
  }
  private async notice(project:string,id:string,body:string):Promise<D1PreparedStatement[]> {
    const c=await this.statement("SELECT * FROM channels WHERE project_id=? AND name='general' AND type='group'",project).first<Channel>();
    if(!c)return [];
    const now=Date.now();
    return [this.statement("INSERT OR IGNORE INTO messages(id,project_id,channel_id,kind,agent,content,ts,timestamp) VALUES(?,?,?,?,?,?,?,?)",id,project,c.id,"event","hub-supervisor",body,now,new Date(now).toISOString()),...await this.enqueue(c,id,"hub-supervisor",body,null,"event")];
  }
  private streams=new Set<ReadableStreamDefaultController<Uint8Array>>();
  constructor(private db:D1Database,private state:DurableObjectState,
    private deliver:(msg:AgentMessage)=>Promise<void>){}
  private statement(sql:string,...args:(string|number|null)[]){return this.db.prepare(sql).bind(...args);}
  private async rows<T>(sql:string,...args:(string|number|null)[]){return (await this.statement(sql,...args).all<T>()).results;}
  private identity(p:V2Principal):HubIdentity{return {role:p.isAdmin?"admin":"agent",agent:p.agent!,actor:p.isAdmin?"arin":p.agent!};}
  private owner(p:V2Principal){if(!p.isAdmin)fail(403,"Only Arin may do this");}
  private async project(id:string,p:V2Principal):Promise<Project>{
    const project=await this.statement("SELECT * FROM projects WHERE id=?",id).first<Project>();
    if(!project) return fail(404,"Project not found");
    if(!p.isAdmin&&(!(p.allowedProjects?.has("*")||p.allowedProjects?.has(id))||!JSON.parse(project.agents_json).includes(p.agent)))fail(403,"Project membership required");
    return project;
  }
  private async channel(id:string,p:V2Principal,post=false):Promise<Channel>{
    const c=await this.statement("SELECT * FROM channels WHERE id=?",id).first<Channel>();
    if(!c)return fail(404,"Channel not found");await this.project(c.project_id,p);
    const policy={type:c.type,members:JSON.parse(c.members_json) as string[]};
    if(!(post?canPostChannel:canReadChannel)(this.identity(p),policy))fail(403,"Channel membership or posting permission required");
    return c;
  }
  private event(project:string,actor:string,type:string,title:string,body="",channel:string|null=null){
    const id=crypto.randomUUID(),now=Date.now();
    return [this.statement("INSERT INTO activity_log(id,project_id,ts,time,agent,thread_id,kind,event_type,title,body) VALUES(?,?,?,?,?,?,?,?,?,?)",id,project,now,new Date(now).toISOString(),actor,channel,"event",type,title,body),
      this.statement("INSERT INTO search_fts(project_id,entry_id,kind,thread_id,agent,ts,title,body) VALUES(?,?,?,?,?,?,?,?)",project,id,"activity",channel,actor,now,title,body)];
  }
  notifyTransport(){this.notify();}
  private notify(){for(const stream of this.streams){try{stream.enqueue(new TextEncoder().encode("event: refresh\ndata: {}\n\n"));}catch{this.streams.delete(stream);}}}
  private async write(statements:D1PreparedStatement[]){await this.db.batch(statements);this.notify();}
  async touch(agent:string,now=Date.now()){
    // A name is an authenticated parent identity; no automatic subagent registration.
    await this.statement("INSERT INTO agent_registry(id,name,last_seen,registered_at) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET last_seen=MAX(last_seen,excluded.last_seen)",agent,agent,now,now).run();
  }
  async flush(){
    const pending=await this.rows<{id:string;envelope:string}>("SELECT id,envelope FROM feature_outbox WHERE delivered=0 ORDER BY rowid LIMIT 100");
    for(const row of pending){await this.deliver(JSON.parse(row.envelope));await this.statement("UPDATE feature_outbox SET delivered=1 WHERE id=?",row.id).run();}
  }
  private async enqueue(c:Channel,id:string,actor:string,body:string,sub:string|null,type="message",timestamp=new Date().toISOString()){
    const members=JSON.parse(c.members_json) as string[];
    // Private recipient envelopes keep old native inboxes and project sockets from
    // seeing other channels. The normalized message remains one immutable entry.
    const statements:D1PreparedStatement[]=[];
    for(const recipient of [...new Set([...members,actor])].filter(x=>x!=="arin"&&x!=="hub-supervisor")){
      const hash=await crypto.subtle.digest("SHA-256",new TextEncoder().encode(JSON.stringify([id,recipient])));
      const envelopeId="ch-"+Array.from(new Uint8Array(hash),x=>x.toString(16).padStart(2,"0")).join("");
      const msg:AgentMessage={id:envelopeId,project:c.project_id,from:actor,to:recipient,type,body,timestamp,subject:c.name,...(sub?{sub}:{})};
      statements.push(this.statement("INSERT OR IGNORE INTO feature_outbox(id,envelope) VALUES(?,?)",envelopeId,JSON.stringify(msg)));
    }
    return statements;
  }
  private async post(c:Channel,p:V2Principal,b:Data){
    const id=str(b,"id",undefined),content=str(b,"content",undefined,32768),actor=this.identity(p).actor;
    if(new TextEncoder().encode(content).byteLength>32768)fail(413,"Message body too large");
    const sub=b.sub==null?null:str(b,"sub",undefined,80);
    const old=await this.statement("SELECT * FROM messages WHERE id=?",id).first<{channel_id:string;agent:string;content:string;sub:string|null}>();
    if(old){if(old.channel_id!==c.id||old.agent!==actor||old.content!==content||old.sub!==sub)fail(409,"Message id conflict");await this.flush();return Response.json({id,duplicate:true});}
    const now=Date.now(),stamp=new Date(now).toISOString();
    await this.write([this.statement("INSERT INTO messages(id,project_id,channel_id,kind,agent,content,ts,timestamp,sub) VALUES(?,?,?,?,?,?,?,?,?)",id,c.project_id,c.id,"message",actor,content,now,stamp,sub),
      ...await this.enqueue(c,id,actor,content,sub,"message",stamp),...this.event(c.project_id,actor,"message","Message in "+c.name,content,c.id)]);
    await this.flush();return Response.json({id,duplicate:false},{status:201});
  }
  async sweep(now:number){
    // Cron ticks are idempotent by five-minute bucket; repeated delivery cannot
    // turn Late into Down. Misses derive from server-observed liveness times.
    const bucket=Math.floor(now/300000)*300000;
    const connected=new Set<string>();
    for(const ws of this.state.getWebSockets())if(ws.readyState===1){const a=ws.deserializeAttachment() as {agent?:string;isAdmin?:boolean}|null;if(a?.agent&&!a.isAdmin)connected.add(a.agent);}
    for(const agent of connected)await this.touch(agent,now);
    const agents=await this.rows<Agent>("SELECT * FROM agent_registry ORDER BY id");
    const usage=await this.rows<{agent:string;project_id:string;received_at:number;handoff_recommended:number}>("SELECT u.* FROM usage_reports u WHERE seq=(SELECT MAX(v.seq) FROM usage_reports v WHERE v.project_id=u.project_id AND v.agent=u.agent)");
    const usable=(agent:string,project:string)=>!usage.some(u=>u.agent===agent&&u.project_id===project&&u.handoff_recommended===1&&now-u.received_at<=USAGE_FRESH_MS&&now>=u.received_at);
    const health=new Map<string,string>();
    for(const a of agents){const missed=connected.has(a.id)?0:Math.max(0,a.last_seen ? Math.floor((bucket-a.last_seen)/300000) : Math.max(1,Math.floor((bucket-a.registered_at)/300000)+1));health.set(a.id,missed===0?"Healthy":missed===1?"Late":"Down");}
    const projects=await this.rows<Project>("SELECT * FROM projects ORDER BY id");
    for(const a of agents){
      if(a.checked_at>=bucket)continue;
      const status=health.get(a.id)!;
      for(const project of projects){
        const members=JSON.parse(project.agents_json) as string[];if(!members.includes(a.id))continue;
        const ops:D1PreparedStatement[]=[];
        if(status!==a.health){
          const c=await this.statement("SELECT * FROM channels WHERE project_id=? AND name='general' AND type='group'",project.id).first<Channel>();
          const body=a.id+" is "+status+".";
          ops.push(...this.event(project.id,"hub-supervisor","health",body));
          if(c){const id="health-"+bucket+"-"+a.id+"-"+project.id;
            ops.push(this.statement("INSERT OR IGNORE INTO messages(id,project_id,channel_id,kind,agent,content,ts,timestamp) VALUES(?,?,?,?,?,?,?,?)",id,project.id,c.id,"event","hub-supervisor",body,now,new Date(now).toISOString()),...await this.enqueue(c,id,"hub-supervisor",body,null,"event"));}
        }
        const tasks=await this.rows<Task>("SELECT * FROM tasks WHERE project_id=? AND state='open' AND (owner=? OR assignee=?)",project.id,a.id,a.id);
        for(const task of tasks){
          const ownerHealth=health.get(task.owner);
          let assign=task.assignee;
          if(ownerHealth==="Healthy")assign=task.owner;
          else if(ownerHealth==="Down"&&(task.assignee===task.owner||health.get(task.assignee)!=="Healthy")){
            // Same project, prefer the same role, stable id order. No eligible peer
            // means an explicit unassigned warning instead of invented coverage.
            const owner=agents.find(x=>x.id===task.owner);
            assign=agents.find(x=>members.includes(x.id)&&health.get(x.id)==="Healthy"&&usable(x.id,project.id)&&x.id!==task.owner&&(!owner?.role||x.role===owner.role))?.id||agents.find(x=>members.includes(x.id)&&health.get(x.id)==="Healthy"&&usable(x.id,project.id)&&x.id!==task.owner)?.id||task.owner;
          }
          if(status!==a.health && ownerHealth==="Down" && assign===task.owner && task.owner===a.id)ops.push(...this.event(project.id,"hub-supervisor","task.uncovered",task.title+": no eligible project peer available (healthy, without a fresh low-usage report)"));
          if(assign!==task.assignee){
            const handoff=await this.statement("SELECT id,branch,commit_hash,push_state FROM task_handoffs WHERE task_id=? ORDER BY seq DESC LIMIT 1",task.id).first<{id:string;branch:string;commit_hash:string|null;push_state:string}>();
            const evidence=handoff?"Handoff "+handoff.id+"; branch "+handoff.branch+"; commit "+(handoff.commit_hash||"unknown")+"; "+handoff.push_state+" (agent-reported).":"No handoff checkpoint has been reported.";
            ops.push(this.statement("UPDATE tasks SET assignee=? WHERE id=?",assign,task.id),this.statement("INSERT INTO task_events(task_id,ts,actor,event,assignee) VALUES(?,?,?,?,?)",task.id,now,"hub-supervisor",assign===task.owner?"returned":"covered",assign),...this.event(project.id,"hub-supervisor","task.coverage",task.title+" assigned to "+assign,evidence),...await this.notice(project.id,"coverage:"+bucket+":"+task.id+":"+assign,"Task "+task.id+" assigned to "+assign+". "+evidence+" Inspect the branch and acknowledge the checked-out handoff before continuing."));
          }
        }
        if(ops.length)await this.write(ops);
      }
      await this.statement("UPDATE agent_registry SET health=?,checked_at=?,last_seen=CASE WHEN ? THEN MAX(last_seen,?) ELSE last_seen END WHERE id=?",status,bucket,connected.has(a.id)?1:0,now,a.id).run();
    }
    await this.flush();return Response.json({checked:agents.length,at:bucket});
  }
  async route(request:Request,p:V2Principal,b:Data={}):Promise<Response>{
    const url=new URL(request.url),admin=p.isAdmin,actor=this.identity(p).actor;
    let path:string[];try{path=url.pathname.replace(/^\/api\/v2(?:\/admin)?/,"").split("/").filter(Boolean).map(decodeURIComponent);}catch{return fail(400,"Invalid path encoding");}
    const get=request.method==="GET",post=request.method==="POST";
    if(!get&&!post)fail(405,"Method not allowed");
    await this.flush();
    const governed=await this.governance.route(request,p,b,path);
    if(governed){await this.flush();return governed;}
    if(path[0]==="portal"&&admin&&get&&(path.length===1||(path.length===2&&path[1]==="app.js")))return new Response(path[1]==="app.js"?PORTAL_JS:PORTAL_HTML,{headers:{"Content-Type":path[1]==="app.js"?"text/javascript; charset=utf-8":"text/html; charset=utf-8","Content-Security-Policy":"default-src 'self'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'","X-Content-Type-Options":"nosniff"}});
    if(path[0]==="stream"&&admin&&get&&path.length===1){
      const {cursor}=page(url);let controller:ReadableStreamDefaultController<Uint8Array>;
      let heartbeat:ReturnType<typeof setInterval>,end:ReturnType<typeof setTimeout>;
      const cleanup=()=>{clearInterval(heartbeat);clearTimeout(end);if(controller)this.streams.delete(controller);};
      const stream=new ReadableStream<Uint8Array>({start:c=>{controller=c;this.streams.add(c);c.enqueue(new TextEncoder().encode("event: connected\ndata: "+JSON.stringify({cursor})+"\n\n"));heartbeat=setInterval(()=>{try{c.enqueue(new TextEncoder().encode(": heartbeat\n\n"));}catch{cleanup();}},15000);end=setTimeout(()=>{cleanup();try{c.close();}catch{}},60000);},cancel:cleanup});
      return new Response(stream,{headers:{"Content-Type":"text/event-stream","Cache-Control":"no-store","X-Accel-Buffering":"no"}});
    }
    if(path[0]==="projects"&&path.length===1){
      if(get){const all=await this.rows<Project>("SELECT * FROM projects ORDER BY id");const visible=[];for(const x of all){try{await this.project(x.id,p);visible.push({...x,agents:JSON.parse(x.agents_json),desc:x.description});}catch(e){if(!(e instanceof FeatureFailure)||e.status!==403)throw e;}}return Response.json({projects:visible});}
      this.owner(p);const id=str(b,"id"),name=str(b,"name"),agents=list(b,"agents"),description=typeof b.desc==="string"?b.desc.slice(0,2000):"";
      if(await this.statement("SELECT id FROM projects WHERE id=?",id).first())fail(409,"Project exists");
      const now=Date.now(),members=JSON.stringify(agents),ops=[this.statement("INSERT INTO projects(id,name,description,agents_json,created_at) VALUES(?,?,?,?,?)",id,name,description,members,now)];
      for(const [key,type] of [["general","group"],["announcements","announce"]] as const)ops.push(this.statement("INSERT INTO channels(id,project_id,type,name,members_json,created_at) VALUES(?,?,?,?,?,?)",id+":"+key,id,type,key,members,now));
      ops.push(...this.event(id,actor,"project.created",name));await this.write(ops);return Response.json({id},{status:201});
    }
    if(path[0]==="projects"&&path[1]){
      const project=await this.project(path[1],p),members=JSON.parse(project.agents_json) as string[],id=project.id;
      if(path[2]==="channels"&&path.length===3){
        if(get){const all=await this.rows<Channel>("SELECT * FROM channels WHERE project_id=? ORDER BY id",id);return Response.json({channels:all.filter(c=>canReadChannel(this.identity(p),{type:c.type,members:JSON.parse(c.members_json)})).map(c=>({...c,members:JSON.parse(c.members_json)}))});}
        this.owner(p);const channelId=str(b,"id"),name=str(b,"name"),type=str(b,"type"),selected=list(b,"members");
        if(!["group","dm","announce"].includes(type)||selected.some(x=>!members.includes(x))||!selected.length||(type==="dm"&&selected.length!==2))fail(400,"Invalid channel membership or type");
        if(await this.statement("SELECT id FROM channels WHERE id=?",channelId).first())fail(409,"Channel exists");
        await this.write([this.statement("INSERT INTO channels(id,project_id,type,name,members_json,created_at) VALUES(?,?,?,?,?,?)",channelId,id,type,name,JSON.stringify(selected),Date.now()),...this.event(id,actor,"channel.created",name,"",channelId)]);return Response.json({id:channelId},{status:201});
      }
      if(path[2]==="tasks"&&(path.length===3||(path.length===4&&!get))){
        if(get){const tasks=await this.rows<Task>("SELECT * FROM tasks WHERE project_id=? ORDER BY created_at,id",id);const enriched=[];for(const task of tasks){const handoff=await this.statement("SELECT id,author,branch,commit_hash,push_state,work_state,received_at FROM task_handoffs WHERE task_id=? ORDER BY seq DESC LIMIT 1",task.id).first();enriched.push({...task,handoff,evidence_status:handoff?"agent_reported":null});}return Response.json({tasks:enriched});}
        if(path[3]){const task=await this.statement("SELECT * FROM tasks WHERE id=? AND project_id=?",path[3],id).first<Task>();if(!task)fail(404,"Task not found");if(!admin&&task!.assignee!==actor)fail(403,"Task assignee required");if(b.state!=="done")fail(400,"Only done is accepted");await this.write([this.statement("UPDATE tasks SET state='done' WHERE id=?",path[3]),this.statement("INSERT INTO task_events(task_id,ts,actor,event,assignee) VALUES(?,?,?,?,?)",path[3],Date.now(),actor,"done",task!.assignee),...this.event(id,actor,"task.done",task!.title)]);return Response.json({ok:true});}
        this.owner(p);const taskId=str(b,"id"),owner=str(b,"owner"),title=str(b,"title");if(!members.includes(owner))fail(400,"Task owner must belong to project");
        if(await this.statement("SELECT id FROM tasks WHERE id=?",taskId).first())fail(409,"Task exists");
        await this.write([this.statement("INSERT INTO tasks(id,project_id,title,owner,assignee,created_at) VALUES(?,?,?,?,?,?)",taskId,id,title,owner,owner,Date.now()),this.statement("INSERT INTO task_events(task_id,ts,actor,event,assignee) VALUES(?,?,?,?,?)",taskId,Date.now(),actor,"created",owner),...this.event(id,actor,"task.created",title)]);return Response.json({id:taskId},{status:201});
      }
      if(path[2]==="docs"&&["plan","onboarding"].includes(path[3]))return this.document(project,path.slice(3),p,b,get,url);
      if(path[2]==="activity"||path[2]==="search"){
        const {limit,cursor}=page(url);const visible=admin?"1=1":"((a.event_type!=\"mesh.message\" AND (a.thread_id IS NULL OR EXISTS (SELECT 1 FROM channels c,json_each(c.members_json) m WHERE c.id=a.thread_id AND m.value=?))) OR (a.event_type=\"mesh.message\" AND (json_extract(a.metadata_json,\"$.to\")=\"all\" OR json_extract(a.metadata_json,\"$.to\")=? OR a.agent=?)))";
        const binds:(string|number)[]=[id,cursor];if(!admin)binds.push(p.agent!,p.agent!,p.agent!);
        let search="";
        if(path[2]==="search"){const q=url.searchParams.get("q")||"";if(!q.trim()||q.length>200)fail(400,"Search text required (1-200 characters)");search=" AND a.id IN (SELECT entry_id FROM search_fts WHERE search_fts MATCH ?)";binds.push('"'+q.replace(/"/g,'""')+'"');}
        const rows=await this.rows<{seq:number}>("SELECT a.* FROM activity_log a WHERE a.project_id=? AND a.seq>? AND "+visible+search+" ORDER BY a.seq LIMIT ?",...binds,limit+1);return Response.json(paged(rows,limit,x=>x.seq));
      }
    }
    if(path[0]==="channels"&&path[1]&&path[2]==="messages"&&path.length===3){
      const c=await this.channel(path[1],p,post);if(post)return this.post(c,p,b);
      const {limit,cursor}=page(url);const rows=await this.rows<{seq:number}>("SELECT rowid AS seq,* FROM messages WHERE channel_id=? AND rowid>? ORDER BY rowid LIMIT ?",c.id,cursor,limit+1);return Response.json(paged(rows,limit,x=>x.seq));
    }
    if(path[0]==="messages"&&admin&&get&&path.length===1){const {limit,cursor}=page(url);const rows=await this.rows<{seq:number}>("SELECT rowid AS seq,* FROM messages WHERE rowid>? ORDER BY rowid LIMIT ?",cursor,limit+1);return Response.json(paged(rows,limit,x=>x.seq));}
    if(admin&&get&&path.length===1&&["activity","search"].includes(path[0])){
      const {limit,cursor}=page(url);let query="",args:(string|number)[]=[cursor];
      if(path[0]==="search"){const q=url.searchParams.get("q")||"";if(!q.trim()||q.length>200)fail(400,"Search text required");query=" AND a.id IN (SELECT entry_id FROM search_fts WHERE search_fts MATCH ?)";args.push('"'+q.replace(/"/g,'""')+'"');}
      const rows=await this.rows<{seq:number}>("SELECT a.* FROM activity_log a WHERE a.seq>?"+query+" ORDER BY a.seq LIMIT ?",...args,limit+1);return Response.json(paged(rows,limit,x=>x.seq));
    }
    if(path[0]==="agents"&&path.length===3&&path[2]==="reports"&&get){
      if(!admin&&path[1]!==p.agent)fail(403,"Status report owner required");
      const {limit,cursor}=page(url);const rows=await this.rows<{seq:number}>("SELECT * FROM status_reports WHERE agent=? AND seq>? ORDER BY seq LIMIT ?",path[1],cursor,limit+1);return Response.json(paged(rows,limit,x=>x.seq));
    }
    if(path[0]==="agents"&&(path.length===1||(path.length===2&&path[1]==="status"&&!get))){
      if(get){const all=await this.rows<Agent>("SELECT * FROM agent_registry ORDER BY id");if(admin)return Response.json({agents:all.map(a=>({...a,status:JSON.parse(a.status_json)}))});const projects=await this.rows<Project>("SELECT * FROM projects");const peers=new Set([p.agent!]);for(const x of projects){try{await this.project(x.id,p);for(const a of JSON.parse(x.agents_json))peers.add(a);}catch(e){if(!(e instanceof FeatureFailure))throw e;}}return Response.json({agents:all.filter(a=>peers.has(a.id)).map(({id,name,role,last_seen,health})=>({id,name,role,last_seen,health}))});}
      if(path[1]==="status"){if(admin)fail(403,"An agent identity is required");const report=JSON.stringify(b);if(report.length>8192)fail(413,"Status report too large");await this.db.batch([this.statement("INSERT INTO status_reports(agent,ts,body) VALUES(?,?,?)",actor,Date.now(),report),this.statement("UPDATE agent_registry SET status_json=? WHERE id=?",report,actor)]);await this.write(this.event("_hub",actor,"agent.status",actor+" reported status"));return Response.json({ok:true});}
      this.owner(p);const agent=str(b,"id"),name=str(b,"name"),role=str(b,"role");if(["arin","hub","admin","hub-supervisor"].includes(agent))fail(400,"Reserved identity");await this.statement("INSERT INTO agent_registry(id,name,role,registered_at) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,role=excluded.role",agent,name,role,Date.now()).run();await this.write(this.event("_hub",actor,"agent.registered",agent+" / "+role));return Response.json({id:agent},{status:201});
    }
    return fail(404,"Unknown versioned feature route");
  }
  private async document(project:Project,path:string[],p:V2Principal,b:Data,get:boolean,url:URL):Promise<Response>{
    const id=project.id,docId=path[0],actor=this.identity(p).actor;
    const current=await this.statement("SELECT * FROM docs WHERE project_id=? AND doc_id=?",id,docId).first<Doc>();
    if(get){
      if(path.length>2 || (path.length===2&&!["revisions","suggestions","acks"].includes(path[1])))fail(404,"Unknown document route");
      if(path[1]==="acks"){
        const {limit,cursor}=page(url);const rows=await this.rows<{seq:number}>("SELECT rowid AS seq,* FROM doc_acks WHERE project_id=? AND doc_id=? AND rowid>?"+(p.isAdmin?"":" AND agent_id=?")+" ORDER BY rowid LIMIT ?",id,docId,cursor,...(p.isAdmin?[]:[actor]),limit+1);return Response.json(paged(rows,limit,x=>x.seq));
      }
      if(path[1]==="revisions"){const {limit,cursor}=page(url);const rows=await this.rows<{rev:number}>("SELECT * FROM doc_revisions WHERE project_id=? AND doc_id=? AND rev>? ORDER BY rev LIMIT ?",id,docId,cursor,limit+1);return Response.json(paged(rows,limit,x=>x.rev));}
      if(path[1]==="suggestions")return Response.json({suggestions:await this.rows("SELECT * FROM doc_suggestions WHERE project_id=? AND doc_id=? ORDER BY ts,id",id,docId)});
      if(!current)fail(404,"Document not yet created");return Response.json(current);
    }
    if(path[1]==="ack"){
      if(p.isAdmin)fail(400,"Agent acknowledgement required");const version=b.version;if(!Number.isInteger(version))fail(400,"Version required");
      const revision=await this.statement("SELECT id FROM doc_revisions WHERE project_id=? AND doc_id=? AND rev=?",id,docId,version as number).first();if(!revision)fail(404,"Version not found");
      const existing=await this.statement("SELECT acked_at FROM doc_acks WHERE project_id=? AND doc_id=? AND version=? AND agent_id=?",id,docId,version as number,actor).first();
      if(!existing)await this.write([this.statement("INSERT INTO doc_acks(project_id,doc_id,version,agent_id,acked_at) VALUES(?,?,?,?,?)",id,docId,version as number,actor,Date.now()),...this.event(id,actor,"doc.acknowledged",docId+" version "+version)]);return Response.json({ok:true});
    }
    if(path[1]==="suggestions"&&!path[2]){
      if(p.isAdmin)fail(403,"Agents suggest; Arin edits directly");if(!current)fail(404,"Document not found");const body=str(b,"body",undefined,32768),note=str(b,"note","Suggested revision",2000),suggestion=crypto.randomUUID(),now=Date.now();
      await this.write([this.statement("INSERT INTO doc_suggestions(id,project_id,doc_id,author,ts,time,note,body) VALUES(?,?,?,?,?,?,?,?)",suggestion,id,docId,actor,now,new Date(now).toISOString(),note,body),...this.event(id,actor,"doc.suggested",docId)]);return Response.json({id:suggestion},{status:201});
    }
    this.owner(p);
    let body:string,note=str(b,"note","Document revision",2000),suggestion:Suggestion|null=null;
    if(path[1]==="suggestions"&&path[2]){
      suggestion=await this.statement("SELECT * FROM doc_suggestions WHERE id=? AND project_id=? AND doc_id=?",path[2],id,docId).first<Suggestion>();if(!suggestion)fail(404,"Suggestion not found");if(suggestion!.status!=="pending")fail(409,"Suggestion already decided");
      if(!["approve","reject"].includes(path[3]))fail(400,"Approve or reject required");
      if(path[3]==="reject"){await this.write([this.statement("UPDATE doc_suggestions SET status='rejected',decided_by=?,decided_at=? WHERE id=?",actor,Date.now(),suggestion!.id),this.statement("INSERT INTO suggestion_decisions(id,suggestion_id,actor,decision,ts) VALUES(?,?,?,?,?)",crypto.randomUUID(),suggestion!.id,actor,"rejected",Date.now()),...this.event(id,actor,"doc.rejected",docId)]);return Response.json({ok:true});}
      body=suggestion!.body;note="Approved suggestion "+suggestion!.id;
    }else if(path[1]==="restore"){
      if(!Number.isInteger(b.version))fail(400,"Version required");const old=await this.statement("SELECT body FROM doc_revisions WHERE project_id=? AND doc_id=? AND rev=?",id,docId,b.version as number).first<{body:string}>();if(!old)fail(404,"Version not found");body=old!.body;note="Restored version "+b.version;
    }else if(path.length===1)body=str(b,"body",undefined,32768);else return fail(404,"Unknown document route");
    if(b.expected_version!==(current?.version||0))fail(409,"Document changed; reload current version");
    const version=(current?.version||0)+1,now=Date.now();const ops=[this.statement("INSERT INTO doc_revisions(id,project_id,doc_id,rev,ts,time,author,note,body) VALUES(?,?,?,?,?,?,?,?,?)",crypto.randomUUID(),id,docId,version,now,new Date(now).toISOString(),actor,note,body),this.statement("INSERT INTO docs(project_id,doc_id,title,purpose,version,body,updated_by,updated_at) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(project_id,doc_id) DO UPDATE SET version=excluded.version,body=excluded.body,updated_by=excluded.updated_by,updated_at=excluded.updated_at",id,docId,docId,docId,version,body,actor,now),...this.event(id,actor,"doc.revised",docId+" version "+version,body)];
    if(suggestion)ops.push(this.statement("UPDATE doc_suggestions SET status='approved',decided_by=?,decided_at=? WHERE id=?",actor,now,suggestion.id),this.statement("INSERT INTO suggestion_decisions(id,suggestion_id,actor,decision,ts) VALUES(?,?,?,?,?)",crypto.randomUUID(),suggestion.id,actor,"approved",now));
    await this.write(ops);return Response.json({version,body});
  }
}
