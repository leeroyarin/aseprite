/** Owner GitHub OAuth. Provider tokens are used once in memory, never persisted. */
export interface GithubConfig {
  PORTAL_AUTH_PROVIDER?: string;
  PORTAL_ORIGIN?: string;
  GITHUB_CLIENT_ID?: string;
  GITHUB_CLIENT_SECRET?: string;
  GITHUB_OWNER_ID?: string;
  HUB_DB?: D1Database;
}
const PREFIX='/api/v2/auth';
const SESSION='__Host-hub_session';
const FLOW='__Host-hub_oauth';
const SESSION_MS=8*60*60*1000;
const FLOW_MS=10*60*1000;
export class GithubAuthFailure extends Error {
  constructor(public status:number,message:string){super(message);}
}
function random(){return b64(crypto.getRandomValues(new Uint8Array(32)));}
function b64(bytes:Uint8Array){return btoa(String.fromCharCode(...bytes)).replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'');}
async function hash(value:string){return b64(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(value))));}
function cookie(request:Request,name:string):string|undefined {
  const values=(request.headers.get('Cookie')||'').split(';').map(v=>v.trim()).filter(v=>v.startsWith(name+'='));
  if(values.length!==1)return;
  const value=values[0].slice(name.length+1);
  return /^[A-Za-z0-9_-]{43}$/.test(value)?value:undefined;
}
function setCookie(name:string,value:string,seconds:number){return `${name}=${value}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${seconds}`;}
function response(body:string|null,status=200,extra:Record<string,string>={}){
  return new Response(body,{status,headers:{'Cache-Control':'no-store','Referrer-Policy':'no-referrer','X-Content-Type-Options':'nosniff','Content-Security-Policy':"default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'",...extra}});
}
function redirect(path:string,extra:Record<string,string>={}){return response(null,303,{Location:path,...extra});}
function configured(env:GithubConfig):asserts env is GithubConfig & {HUB_DB:D1Database;PORTAL_ORIGIN:string;GITHUB_CLIENT_ID:string;GITHUB_CLIENT_SECRET:string;GITHUB_OWNER_ID:string}{
  if(env.PORTAL_AUTH_PROVIDER!=='github'||!env.HUB_DB||!env.GITHUB_CLIENT_ID||!env.GITHUB_CLIENT_SECRET||!/^\d+$/.test(env.GITHUB_OWNER_ID||''))throw new GithubAuthFailure(503,'GitHub owner sign-in is not configured');
  const origin=new URL(env.PORTAL_ORIGIN||'');
  if(origin.protocol!=='https:'||origin.origin!==env.PORTAL_ORIGIN)throw new GithubAuthFailure(503,'Portal origin is not configured');
}
function sameOrigin(request:Request,origin:string){
  if(request.headers.get('Origin')!==origin)throw new GithubAuthFailure(403,'A same-origin request is required');
}
export async function verifyGithubSession(request:Request,env:GithubConfig):Promise<void>{
  configured(env);
  if(new URL(request.url).origin!==env.PORTAL_ORIGIN)throw new GithubAuthFailure(403,'Portal origin mismatch');
  if(!['GET','HEAD','OPTIONS'].includes(request.method))sameOrigin(request,env.PORTAL_ORIGIN);
  const raw=cookie(request,SESSION);
  if(!raw)throw new GithubAuthFailure(401,'Sign in with GitHub to open the owner portal');
  const row=await env.HUB_DB.prepare('SELECT owner_id,expires_at,revoked_at FROM portal_sessions WHERE session_hash=?').bind(await hash(raw)).first<{owner_id:string;expires_at:number;revoked_at:number|null}>();
  if(!row||row.owner_id!==env.GITHUB_OWNER_ID||row.expires_at<=Date.now()||row.revoked_at!==null)throw new GithubAuthFailure(401,'Your portal session has expired; sign in again');
}
async function providerJson(url:string,init:RequestInit):Promise<Record<string,unknown>>{
  const r=await fetch(url,{...init,redirect:'manual',signal:AbortSignal.timeout(15000)});
  if(!r.ok)throw new GithubAuthFailure(502,'GitHub sign-in could not be completed');
  return await r.json() as Record<string,unknown>;
}
export async function githubAuthRoute(request:Request,env:GithubConfig):Promise<Response|null>{
  const url=new URL(request.url),path=url.pathname;
  if(!path.startsWith(PREFIX+'/'))return null;
  try {
    if(env.PORTAL_AUTH_PROVIDER!=='github')return response('Owner sign-in is unavailable',404);
    if(path===PREFIX+'/login'&&request.method==='GET'){
      const ready=!!(env.GITHUB_CLIENT_ID&&env.GITHUB_CLIENT_SECRET&&env.GITHUB_OWNER_ID&&env.HUB_DB);
      return response(`<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Sign in · Agent Hub</title><style>body{font:18px system-ui;background:#10141c;color:#edf0f5;max-width:600px;margin:15vh auto;padding:24px}a{display:inline-block;padding:14px;background:#344e79;color:white;border-radius:8px}</style><h1>Agent Hub</h1><p>Sign in with the owner’s GitHub account to open the hub.</p>${ready?'<a href="/api/v2/auth/github/login">Continue with GitHub</a>':'<p>Owner sign-in setup is awaiting its GitHub application credential.</p>'}</html>`,ready?200:503,{'Content-Type':'text/html; charset=utf-8'});
    }
    configured(env);
    if(url.origin!==env.PORTAL_ORIGIN)throw new GithubAuthFailure(403,'Portal origin mismatch');
    if(path===PREFIX+'/github/login'&&request.method==='GET'){
      if(request.headers.get('Sec-Fetch-Site')==='cross-site')throw new GithubAuthFailure(403,'Start sign-in from the portal');
      // Bound unauthenticated writes; Cloudflare sets this header at the edge.
      const ip=request.headers.get('CF-Connecting-IP')||'unknown';
      const bucket=await hash(ip+':'+Math.floor(Date.now()/FLOW_MS));
      const rate=await env.HUB_DB.prepare('INSERT INTO portal_login_limits(bucket,attempts) VALUES(?,1) ON CONFLICT(bucket) DO UPDATE SET attempts=attempts+1 RETURNING attempts').bind(bucket).first<{attempts:number}>();
      if(!rate||rate.attempts>10)throw new GithubAuthFailure(429,'Too many sign-in attempts; try again in ten minutes');
      const state=random(),verifier=random();
      await env.HUB_DB.prepare('INSERT INTO portal_oauth_states(state_hash,verifier_hash,created_at,expires_at) VALUES(?,?,?,?)').bind(await hash(state),await hash(verifier),Date.now(),Date.now()+FLOW_MS).run();
      const auth=new URL('https://github.com/login/oauth/authorize');
      auth.search=new URLSearchParams({client_id:env.GITHUB_CLIENT_ID,redirect_uri:env.PORTAL_ORIGIN+PREFIX+'/github/callback',scope:'',state,code_challenge:await hash(verifier),code_challenge_method:'S256',allow_signup:'false'}).toString();
      return redirect(auth.href,{'Set-Cookie':setCookie(FLOW,verifier,FLOW_MS/1000)});
    }
    if(path===PREFIX+'/github/callback'&&request.method==='GET'){
      const state=url.searchParams.get('state'),code=url.searchParams.get('code'),verifier=cookie(request,FLOW);
      if(!state||!/^[A-Za-z0-9_-]{43}$/.test(state)||!verifier||!code||code.length>512||url.searchParams.getAll('state').length!==1||url.searchParams.getAll('code').length!==1)throw new GithubAuthFailure(400,'Sign-in attempt is invalid or expired; start again');
      // Atomically consume before any provider request: callbacks cannot be replayed.
      const used=await env.HUB_DB.prepare('UPDATE portal_oauth_states SET consumed_at=? WHERE state_hash=? AND verifier_hash=? AND consumed_at IS NULL AND expires_at>? RETURNING state_hash').bind(Date.now(),await hash(state),await hash(verifier),Date.now()).first();
      if(!used)throw new GithubAuthFailure(400,'Sign-in attempt is invalid or expired; start again');
      const token=await providerJson('https://github.com/login/oauth/access_token',{method:'POST',headers:{Accept:'application/json','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({client_id:env.GITHUB_CLIENT_ID,client_secret:env.GITHUB_CLIENT_SECRET,code,redirect_uri:env.PORTAL_ORIGIN+PREFIX+'/github/callback',code_verifier:verifier}).toString()});
      if(typeof token.access_token!=='string'||token.token_type!=='bearer'||(typeof token.scope==='string'&&token.scope.trim()))throw new GithubAuthFailure(403,'GitHub identity-only authorization is required');
      const owner=await providerJson('https://api.github.com/user',{headers:{Authorization:'Bearer '+token.access_token,Accept:'application/vnd.github+json','User-Agent':'Agent-Hub-Owner-Login','X-GitHub-Api-Version':'2022-11-28'}});
      if(String(owner.id)!==env.GITHUB_OWNER_ID)throw new GithubAuthFailure(403,'This GitHub account is not the portal owner');
      const session=random(),now=Date.now();
      await env.HUB_DB.prepare('INSERT INTO portal_sessions(session_hash,owner_id,created_at,expires_at) VALUES(?,?,?,?)').bind(await hash(session),env.GITHUB_OWNER_ID,now,now+SESSION_MS).run();
      const result=redirect('/api/v2/admin/portal',{'Set-Cookie':setCookie(SESSION,session,SESSION_MS/1000)});
      result.headers.append('Set-Cookie',setCookie(FLOW,'',0));
      return result;
    }
    if(path===PREFIX+'/logout'&&request.method==='POST'){
      sameOrigin(request,env.PORTAL_ORIGIN);
      const raw=cookie(request,SESSION);
      if(raw)await env.HUB_DB.prepare('UPDATE portal_sessions SET revoked_at=? WHERE session_hash=? AND revoked_at IS NULL').bind(Date.now(),await hash(raw)).run();
      return redirect(PREFIX+'/login',{'Set-Cookie':setCookie(SESSION,'',0)});
    }
    return response('Not found',404);
  }catch(error){
    const failure=error instanceof GithubAuthFailure?error:new GithubAuthFailure(502,'Owner sign-in could not be completed; start again');
    // Never include upstream responses, callback codes, cookies or credentials in logs/errors.
    return response(failure.message,failure.status,{'Content-Type':'text/plain; charset=utf-8','Set-Cookie':setCookie(FLOW,'',0)});
  }
}
