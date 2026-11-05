import { ProjectFeatures, FeatureFailure } from "./features";
import { verifyPortalAccess, AccessFailure, type AccessConfig } from "./access";
import type { AgentMessage } from "./index";

export interface V2Principal {
  isValid: boolean;
  isAdmin: boolean;
  agent?: string;
  allowedProjects?: Set<string>;
  status?: number;
}

interface V2Env extends AccessConfig {
  HUB_DB?: D1Database;
  HUB_TOKEN?: string;
  ADMIN_TOKEN?: string;
}
interface Row {
  seq: number;
  id: string;
  project: string;
  from_agent: string;
  to_agent: string;
  type: string;
  reply_to: string | null;
  body: string;
  timestamp: string;
  subject: string;
  request_hash: string;
  source: string;
  sub?: string | null;
}
interface LegacyRow extends Record<string, SqlStorageValue> {
  seq: number; id: string; project: string; from_agent: string; to_agent: string;
  type: string; reply_to: string | null; body: string; timestamp: string; subject: string | null;
}

export const LIMITS = { requestBytes: 65536, bodyBytes: 32768, sendsPerMinute: 60,
  inboxPerMinute: 120, defaultPage: 50, maxPage: 200, mirrorBatch: 100 } as const;
const encoder = new TextEncoder();
class Failure extends Error {
  constructor(public status: number, message: string, public retryAfter?: number) { super(message); }
}

export { redact } from "./redaction";
import { redact } from "./redaction";

function message(row: Row | LegacyRow): AgentMessage {
  return { id: row.id, project: row.project, from: row.from_agent, to: row.to_agent,
    type: row.type, reply_to: row.reply_to, body: row.body, timestamp: row.timestamp,
    subject: row.subject || `proj.${row.project}.${row.type}`, ...(row.sub ? {sub:String(row.sub)} : {}) };
}
async function digest(msg: AgentMessage): Promise<string> {
  const bytes = encoder.encode(JSON.stringify([msg.project,msg.from,msg.to,msg.type,msg.reply_to || null,msg.body,msg.subject,...(msg.sub ? [msg.sub] : [])]));
  const hash = await crypto.subtle.digest("SHA-256", bytes);
  return Array.from(new Uint8Array(hash), b => b.toString(16).padStart(2,"0")).join("");
}
async function readBytes(request: Request): Promise<Uint8Array> {
  const length=request.headers.get("Content-Length");
  if (length && Number(length)>LIMITS.requestBytes) throw new Failure(413,"Message request is too large");
  const reader=request.body?.getReader();
  if (!reader) throw new Failure(400,"A JSON message is required");
  const chunks: Uint8Array[]=[];
  let size=0;
  try {
    for (;;) {
      const part=await reader.read();
      if (part.done) break;
      size+=part.value.byteLength;
      if (size>LIMITS.requestBytes) {
        await reader.cancel();
        throw new Failure(413,"Message request is too large");
      }
      chunks.push(part.value);
    }
  } finally { reader.releaseLock(); }
  const bytes=new Uint8Array(size);
  let offset=0;
  for (const chunk of chunks) { bytes.set(chunk,offset);offset+=chunk.byteLength; }
  return bytes;
}
async function readBody(request: Request, charge: () => void): Promise<Record<string,unknown>> {
  if (request.headers.get("Content-Type")?.split(";")[0].trim().toLowerCase()!=="application/json")
    throw new Failure(415,"Content-Type must be application/json");
  const bytes=await readBytes(request);
  charge();
  try {
    const parsed=JSON.parse(new TextDecoder("utf-8",{fatal:true,ignoreBOM:false}).decode(bytes));
    if (!parsed || Array.isArray(parsed) || typeof parsed!=="object") throw new Error();
    return parsed;
  } catch { throw new Failure(400,"Invalid JSON message"); }
}
function field(body: Record<string,unknown>, key: string, fallback?: string): string {
  const value = body[key] ?? fallback;
  if (typeof value !== "string" || !value.trim() || value.length > 200 || /[\x00-\x1f]/.test(value))
    throw new Failure(400,`Invalid ${key}`);
  return value.trim();
}

/** One serialized coordinator shares the existing legacy DO's authenticated identity
 * and sockets. D1 owns the append-only v2 ledger; DO SQLite is a compatibility projection.
 * Only /api/v2 requests construct this class or touch these extra bookkeeping tables.
 */
export class V2Transport {
  private queue: Promise<void> = Promise.resolve();
  private featuresService?: ProjectFeatures;
  private get features(): ProjectFeatures {
    return this.featuresService ||= new ProjectFeatures(this.db,this.state,async msg=>{
      this.prepareBookkeeping();
      await this.db.batch([this.insert(msg,await digest(msg),"v2")]);
      const row=await this.db.prepare("SELECT * FROM mesh_messages WHERE id=?").bind(msg.id).first<Row>();
      if(!row || row.request_hash!==await digest(msg))throw new Failure(409,"Delivery id conflict");
      this.projectToLegacy(msg,true);
    });
  }
  changed(){this.featuresService?.notifyTransport();}
  async touch(agent:string):Promise<void> { if(this.env.HUB_DB) {try{await this.features.touch(agent);}catch{console.warn("[Registry liveness unavailable]");}} }
  constructor(private state: DurableObjectState, private env: V2Env,
    private authenticate: (r: Request) => Promise<V2Principal>) {}

  async fetch(request: Request): Promise<Response> {
    const previous = this.queue;
    let unlock!: () => void;
    this.queue = new Promise(resolve => { unlock = resolve; });
    await previous;
    try { return await this.route(request); }
    catch (error) {
      if (request.body && !request.body.locked) {
        try { await request.body.cancel(); } catch { /* Body may already be consumed. */ }
      }
      const failure = error instanceof Failure || error instanceof FeatureFailure ? error : new Failure(503,"Versioned transport is temporarily unavailable");
      return Response.json({error:failure.message},{status:failure.status,
        headers:{"Cache-Control":"no-store",...(failure instanceof Failure && failure.retryAfter ? {"Retry-After":String(failure.retryAfter)} : {})}});
    } finally { unlock(); }
  }
  private get sql(): SqlStorage {
    const sql = (this.state.storage as DurableObjectStorage & {sql?: SqlStorage}).sql;
    if (!sql) throw new Failure(503,"Versioned transport requires SQLite Durable Object storage");
    return sql;
  }
  private get db(): D1Database {
    if (!this.env.HUB_DB) throw new Failure(503,"Versioned D1 binding is not configured");
    return this.env.HUB_DB;
  }
  private prepareBookkeeping(): void {
    if(!this.sql.exec<{name:string}>("PRAGMA table_info(messages)").toArray().some(c=>c.name==="sub"))this.sql.exec("ALTER TABLE messages ADD COLUMN sub TEXT");
    this.sql.exec("CREATE TABLE IF NOT EXISTS hub_v2_rates (agent TEXT PRIMARY KEY, minute INTEGER NOT NULL, sends INTEGER NOT NULL, reads INTEGER NOT NULL)");
    this.sql.exec("CREATE TABLE IF NOT EXISTS hub_v2_deliveries (id TEXT PRIMARY KEY, delivered INTEGER NOT NULL DEFAULT 0)");
  }
  private consume(agent: string, send: boolean): void {
    const minute = Math.floor(Date.now()/60000);
    this.state.storage.transactionSync(() => {
      this.sql.exec("INSERT INTO hub_v2_rates(agent,minute,sends,reads) VALUES(?,?,0,0) ON CONFLICT(agent) DO UPDATE SET minute=excluded.minute,sends=0,reads=0 WHERE hub_v2_rates.minute != excluded.minute",agent,minute);
      const row = this.sql.exec<{sends:number;reads:number}>("SELECT sends,reads FROM hub_v2_rates WHERE agent=?",agent).one();
      if ((send ? row.sends : row.reads) >= (send ? LIMITS.sendsPerMinute : LIMITS.inboxPerMinute))
        throw new Failure(429,"Per-agent request limit exceeded",60-Math.floor(Date.now()/1000)%60);
      this.sql.exec(send ? "UPDATE hub_v2_rates SET sends=sends+1 WHERE agent=?" : "UPDATE hub_v2_rates SET reads=reads+1 WHERE agent=?",agent);
    });
  }
  private insert(msg: AgentMessage, hash: string, source: string): D1PreparedStatement {
    return this.db.prepare("INSERT OR IGNORE INTO mesh_messages(id,project,from_agent,to_agent,type,reply_to,body,timestamp,subject,request_hash,source,sub) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)")
      .bind(msg.id,msg.project,msg.from,msg.to,msg.type,msg.reply_to || null,msg.body,msg.timestamp,msg.subject || "",hash,source,msg.sub || null);
  }
  private async mirrorLegacy(): Promise<boolean> {
    const cursor = await this.state.storage.get<number>("hub:v2:legacy-cursor") || 0;
    const rows = this.sql.exec<LegacyRow>("SELECT * FROM messages WHERE seq>? ORDER BY seq LIMIT ?",cursor,LIMITS.mirrorBatch).toArray();
    if (rows.length) {
      const statements: D1PreparedStatement[] = [];
      for (const row of rows) {
        // Control traffic is not history. The legacy source is only read, never altered.
        if (row.project.toLowerCase()==="system" || ["ping","pong"].includes(row.type.toLowerCase())) continue;
        const msg = message(row);
        msg.body = redact(msg.body,[this.env.HUB_TOKEN || "",this.env.ADMIN_TOKEN || ""]);
        msg.subject = redact(msg.subject || "");
        statements.push(this.insert(msg,await digest(msg),"legacy"));
      }
      if (statements.length) await this.db.batch(statements);
      await this.state.storage.put("hub:v2:legacy-cursor",rows[rows.length-1].seq);
    }
    return this.sql.exec<{n:number}>("SELECT count(*) AS n FROM messages WHERE seq>?",rows.length ? rows[rows.length-1].seq : cursor).one().n > 0;
  }
  private projectAllowed(project: string, principal: V2Principal): boolean {
    return !!principal.allowedProjects?.has("*") || !!principal.allowedProjects?.has(project);
  }
  private async route(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const path = url.pathname;
    if(path==="/api/v2/internal/sweep" && url.hostname==="supervisor.internal") {
      this.prepareBookkeeping();
      return this.features.sweep(Number(request.headers.get("X-Hub-Scheduled-Time"))||Date.now());
    }
    if ((path.endsWith("/send") && request.method!=="POST") || (path.endsWith("/inbox") && request.method!=="GET"))
      throw new Failure(405,"Method not allowed");
    let principal: V2Principal;
    if (path.startsWith("/api/v2/admin/")) {
      if (url.searchParams.has("token")) throw new Failure(401,"URL credentials are not accepted");
      try { await verifyPortalAccess(request,this.env); }
      catch (error) {
        if (error instanceof AccessFailure) throw new Failure(error.status,error.message);
        throw new Failure(401,"Cloudflare Access identity could not be verified");
      }
      principal={isValid:true,isAdmin:true,agent:"hub",allowedProjects:new Set(["*"])};
      if (path==="/api/v2/admin/session") {
        if (request.method!=="GET") throw new Failure(405,"Method not allowed");
        return Response.json({identity:"hub",actor:"arin",role:"admin"});
      }
    } else {
      if (url.searchParams.has("token") || !/^Bearer\s+\S+$/i.test(request.headers.get("Authorization") || ""))
        throw new Failure(401,"A Bearer token in the Authorization header is required");
      principal=await this.authenticate(request);
      if (!principal.isValid) throw new Failure(principal.status || 401,"Invalid agent credentials");
      if (principal.isAdmin || !principal.agent || ["hub","arin","admin","hub-supervisor"].includes(principal.agent))
        throw new Failure(403,"Portal administration requires a verified Access identity");
    }
    // Fail before touching the compatibility ledger when D1 has not been provisioned.
    this.db;
    this.prepareBookkeeping();
    if(!principal.isAdmin) await this.touch(principal.agent!);
    const transport=["/api/v2/send","/api/v2/inbox","/api/v2/admin/send","/api/v2/admin/inbox"].includes(path);
    if(!transport){
      let input:Record<string,unknown>={};
      if(request.method==="POST") {
        input=await readBody(request,()=>this.consume(principal.agent!,true));
        const known=[(request.headers.get("Authorization")||"").replace(/^Bearer\s+/i,""),request.headers.get("Cf-Access-Jwt-Assertion")||"",this.env.HUB_TOKEN||"",this.env.ADMIN_TOKEN||""];
        const clean=(v:unknown,key=""):unknown=>{
          if(/^(?:[a-zA-Z0-9_]*_)?(?:password|passwd|pwd|secret|token|api[_-]?key|apikey|authorization|access[_-]?token|refresh[_-]?token|client[_-]?secret|private[_-]?key|cf[-_]?access[-_]?client[-_]?secret)$/i.test(key))return "[REDACTED]";
          if(typeof v==="string"){const redacted=redact(v,known);if(["id","owner","members","agents","sub"].includes(key)&&redacted!==v)throw new Failure(400,"Credentials in routing metadata");return redacted;}
          if(Array.isArray(v))return v.map(x=>clean(x,key));
          if(v&&typeof v==="object")return Object.fromEntries(Object.entries(v).map(([k,x])=>[redact(k,known),clean(x,k)]));return v;
        };input=clean(input) as Record<string,unknown>;
      } else this.consume(principal.agent!,false);
      return this.features.route(request,principal,input);
    }
    if (path.endsWith("/send")) {
      // Consume bounded bytes before rejecting for rate, so a rejected upload does
      // not poison the following request on an HTTP/1 keep-alive connection.
      const input = await readBody(request,()=>this.consume(principal.agent!,true));
      return this.send(request,principal,input);
    }
    this.consume(principal.agent!,false);
    return this.inbox(url,principal);
  }
  private async send(request: Request, principal: V2Principal, input: Record<string,unknown>): Promise<Response> {
    const allowed = new Set(["id","project","from","to","type","reply_to","body","timestamp","subject","verify","sub"]);
    if (Object.keys(input).some(key=>!allowed.has(key))) throw new Failure(400,"Unknown message field");
    const actor=principal.isAdmin ? "arin" : principal.agent!;
    const project=field(input,"project"), from=field(input,"from",actor);
    if (from!==actor || !this.projectAllowed(project,principal)) throw new Failure(403,"Message identity or project is not allowed");
    const type=field(input,"type","task");
    const recipient=field(input,"to","all");
    const to=recipient.toLowerCase()==="all" || recipient==="*" ? "all" : recipient;
    if (project.toLowerCase()==="system" || ["ping","pong"].includes(type.toLowerCase())) throw new Failure(400,"Control messages cannot be sent as history");
    if (typeof input.body!=="string" || !input.body.trim()) throw new Failure(400,"A nonempty string body is required");
    if (encoder.encode(input.body).byteLength>LIMITS.bodyBytes) throw new Failure(413,"Message body is too large");
    const stamp=input.timestamp===undefined ? new Date().toISOString() : field(input,"timestamp");
    if (!Number.isFinite(Date.parse(stamp))) throw new Failure(400,"Invalid timestamp");
    const secret=(request.headers.get("Authorization") || "").replace(/^Bearer\s+/i,"");
    const known=[secret,request.headers.get("Cf-Access-Jwt-Assertion") || "",this.env.HUB_TOKEN || "",this.env.ADMIN_TOKEN || ""];
    const msg: AgentMessage={id:field(input,"id",crypto.randomUUID()),project,from,to,type,
      reply_to:input.reply_to==null ? null : field(input,"reply_to"),body:redact(input.body,known),
      ...(input.sub==null ? {} : {sub:field(input,"sub")}),timestamp:stamp,subject:redact(field(input,"subject",`proj.${project}.${type}`),known)};
    if(msg.sub && msg.sub.length>80)throw new Failure(400,"Sub label is limited to 80 characters");
    // Reject an ID collision before writing D1, even if legacy history has not
    // yet been mirrored. An untrusted client cannot shadow an older message.
    const legacy=this.sql.exec<LegacyRow>("SELECT * FROM messages WHERE id=?",msg.id).toArray()[0];
    if (legacy && (legacy.from_agent!==from || legacy.project!==project || legacy.to_agent!==to ||
        legacy.type!==type || (legacy.sub || null)!==(msg.sub || null) || redact(legacy.body,known)!==msg.body))
      throw new Failure(409,"Message id conflicts with legacy history");
    if (await this.mirrorLegacy())
      throw new Failure(503,"Legacy history synchronization is pending; retry the same message id",1);
    for (const value of [msg.id,msg.project,msg.from,msg.to,msg.type,msg.reply_to || "",msg.timestamp,msg.sub || ""]) {
      // Routing identifiers cannot be rewritten without breaking identity/cursors.
      // Reject credential-shaped metadata rather than persisting a secret there.
      if (redact(value,known)!==value) throw new Failure(400,"Credentials are not allowed in message metadata");
    }
    const hash=await digest(msg);
    await this.db.batch([this.insert(msg,hash,"v2")]);
    const stored=await this.db.prepare("SELECT * FROM mesh_messages WHERE id=?").bind(msg.id).first<Row>();
    if (!stored || stored.from_agent!==from || stored.project!==project || stored.request_hash!==hash)
      throw new Failure(409,"Message id is already used by a different message");
    const receipt=this.sql.exec<{delivered:number}>("SELECT delivered FROM hub_v2_deliveries WHERE id=?",msg.id).toArray()[0];
    if (receipt?.delivered) return Response.json({ok:true,id:msg.id,duplicate:true});
    this.projectToLegacy(message(stored));
    return Response.json({ok:true,id:msg.id,duplicate:!!legacy});
  }
  private projectToLegacy(msg: AgentMessage, channelDelivery=false): void {
    const old=this.sql.exec<LegacyRow>("SELECT * FROM messages WHERE id=?",msg.id).toArray()[0];
    if (old && (old.from_agent!==msg.from || old.project!==msg.project || old.to_agent!==msg.to || old.body!==msg.body || (old.sub || null)!==(msg.sub || null)))
      throw new Failure(409,"Message id conflicts with legacy history");
    this.state.storage.transactionSync(()=>{
      if(channelDelivery)this.sql.exec("INSERT OR IGNORE INTO hub_v2_channel_deliveries(id) VALUES(?)",msg.id);
      this.sql.exec("INSERT OR IGNORE INTO messages(id,project,from_agent,to_agent,type,reply_to,body,timestamp,subject,created_at_ms,sub) VALUES(?,?,?,?,?,?,?,?,?,?,?)",msg.id,msg.project,msg.from,msg.to,msg.type,msg.reply_to || null,msg.body,msg.timestamp,msg.subject || null,Date.now(),msg.sub || null);
      this.sql.exec("INSERT OR IGNORE INTO hub_v2_deliveries(id,delivered) VALUES(?,0)",msg.id);
    });
    if (!old) {
      const serialized=JSON.stringify(msg);
      for (const socket of this.state.getWebSockets()) {
        const session=socket.deserializeAttachment() as {agent:string;isAdmin:boolean;subscribedProjects:string[]} | null;
        if (socket.readyState===1 && session && (session.isAdmin || session.subscribedProjects.includes("*") || session.subscribedProjects.includes(msg.project)) &&
            (session.isAdmin || (channelDelivery ? msg.to===session.agent : msg.to==="all" || msg.to===session.agent || msg.from===session.agent))) {
          try { socket.send(serialized); } catch { /* Persisted history remains available by inbox. */ }
        }
      }
    }
    this.sql.exec("UPDATE hub_v2_deliveries SET delivered=1 WHERE id=?",msg.id);
    this.changed();
  }
  private async inbox(url: URL, principal: V2Principal): Promise<Response> {
    const rawLimit=url.searchParams.get("limit");
    if (rawLimit!==null && !/^\d+$/.test(rawLimit)) throw new Failure(400,"limit must be an integer from 1 to 200");
    const limit=rawLimit===null ? LIMITS.defaultPage : Number(rawLimit);
    if (!Number.isInteger(limit) || limit<1 || limit>LIMITS.maxPage) throw new Failure(400,"limit must be an integer from 1 to 200");
    const requested=(url.searchParams.get("projects") || "").split(",").map(p=>p.trim()).filter(Boolean);
    const projects=requested.length ? [...new Set(requested)] : [...principal.allowedProjects || []];
    if (!projects.length || projects.some(p=>!this.projectAllowed(p,principal))) throw new Failure(403,"Project is not allowed");
    const requestedAgent=url.searchParams.get("agent");
    if (!principal.isAdmin && requestedAgent && requestedAgent!==principal.agent) throw new Failure(403,"Inbox identity is not allowed");
    const moreLegacy=await this.mirrorLegacy();
    const scope=projects.includes("*") ? "1=1" : `project IN (${projects.map(()=>"?").join(",")})`;
    const bindings: string[]=projects.includes("*") ? [] : projects;
    const visibility=principal.isAdmin ? scope : `${scope} AND (to_agent='all' OR to_agent=? OR from_agent=?) AND (NOT EXISTS (SELECT 1 FROM feature_outbox o WHERE o.id=mesh_messages.id) OR to_agent=?)`;
    const visibleBindings=principal.isAdmin ? bindings : [...bindings,principal.agent!,principal.agent!,principal.agent!];
    const since=(url.searchParams.get("since") || "0").trim();
    let cursor=0;
    if (since!=="0") {
      const found=await this.db.prepare(`SELECT seq FROM mesh_messages WHERE id=? AND ${visibility}`).bind(since,...visibleBindings).first<{seq:number}>();
      if (!found) throw new Failure(400,"Invalid inbox cursor");
      cursor=found.seq;
    }
    const result=await this.db.prepare(`SELECT * FROM mesh_messages WHERE seq>? AND ${visibility} ORDER BY seq ASC LIMIT ?`).bind(cursor,...visibleBindings,limit+1).all<Row>();
    const page=result.results.slice(0,limit).map(message);
    const more=result.results.length>limit || moreLegacy;
    const last=page.length ? page[page.length-1].id : since==="0" ? null : since;
    return Response.json({messages:page,latest_id:last,has_more:more,next_cursor:more ? last || "0" : null,legacy_sync_pending:moreLegacy});
  }
}

export async function fetchV2(request: Request, portalOrigin: string,
  forward: (request:Request)=>Promise<Response>): Promise<Response> {
  const origin=request.headers.get("Origin");
  if (origin && origin!==portalOrigin) return Response.json({error:"Origin is not allowed"},{status:403});
  if (request.method==="OPTIONS") {
    const method=request.headers.get("Access-Control-Request-Method");
    const headers=(request.headers.get("Access-Control-Request-Headers") || "").toLowerCase().split(",").map(s=>s.trim()).filter(Boolean);
    if (!origin || !["GET","POST"].includes(method || "") || headers.some(h=>!["authorization","content-type","cf-access-jwt-assertion"].includes(h)))
      return Response.json({error:"Preflight is not allowed"},{status:403});
    return new Response(null,{status:204,headers:{"Access-Control-Allow-Origin":portalOrigin,"Access-Control-Allow-Methods":"GET, POST, OPTIONS","Access-Control-Allow-Headers":"Authorization, Content-Type, Cf-Access-Jwt-Assertion","Vary":"Origin"}});
  }
  let response: Response;
  if (request.method==="POST") {
    try {
      // Bound the upload before crossing the Worker-to-DO streaming boundary.
      // Neither a forged Content-Length nor a chunked upload can bypass this cap.
      const bytes=await readBytes(request);
      response=await forward(new Request(request,{body:bytes}));
    } catch (error) {
      const status=error instanceof Failure || error instanceof FeatureFailure ? error.status : 400;
      response=Response.json({error:status===413 ? "Message request is too large" : "Invalid message request"},{status});
    }
  } else response=await forward(request);
  const headers=new Headers(response.headers);
  headers.delete("Access-Control-Allow-Origin");
  if (origin) headers.set("Access-Control-Allow-Origin",portalOrigin);
  headers.set("Vary","Origin");
  headers.set("Cache-Control","no-store");
  headers.set("X-Hub-API-Version","2-part3");
  return new Response(response.body,{status:response.status,headers});
}
