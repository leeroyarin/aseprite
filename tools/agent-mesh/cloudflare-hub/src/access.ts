/** Verify the portal's Cloudflare Access JWT; never trust identity headers alone. */
export interface AccessConfig {
  ACCESS_TEAM_DOMAIN?: string;
  ACCESS_AUD?: string;
  PORTAL_ADMIN_EMAIL?: string;
}
export class AccessFailure extends Error {
  constructor(public status: number, message: string) { super(message); }
}
function bytes(part: string): Uint8Array {
  if (!/^[A-Za-z0-9_-]+$/.test(part)) throw new Error();
  const raw=atob(part.replace(/-/g,"+").replace(/_/g,"/").padEnd(Math.ceil(part.length/4)*4,"="));
  return Uint8Array.from(raw,c=>c.charCodeAt(0));
}
function decode(part: string): Record<string,unknown> {
  const value=JSON.parse(new TextDecoder("utf-8",{fatal:true,ignoreBOM:false}).decode(bytes(part)));
  if (!value || typeof value!=="object" || Array.isArray(value)) throw new Error();
  return value;
}
export async function verifyPortalAccess(request: Request, env: AccessConfig): Promise<void> {
  if (!env.ACCESS_TEAM_DOMAIN || !/^[a-z0-9-]+\.cloudflareaccess\.com$/.test(env.ACCESS_TEAM_DOMAIN) ||
      !env.ACCESS_AUD || !env.PORTAL_ADMIN_EMAIL)
    throw new AccessFailure(503,"Portal Access authentication is not configured");
  const token=request.headers.get("Cf-Access-Jwt-Assertion");
  if (!token || token.length>16384) throw new AccessFailure(401,"A verified Cloudflare Access identity is required");
  let stage="decode";
  try {
    const parts=token.split(".");
    if (parts.length!==3) throw new Error();
    const header=decode(parts[0]), claims=decode(parts[1]);
    const now=Math.floor(Date.now()/1000);
    const issuer="https://"+env.ACCESS_TEAM_DOMAIN;
    if (header.alg!=="RS256" || typeof header.kid!=="string" ||
        claims.iss!==issuer || typeof claims.exp!=="number" || !Number.isFinite(claims.exp) || claims.exp<=now ||
        (claims.nbf!==undefined && (typeof claims.nbf!=="number" || claims.nbf>now)) ||
        (claims.iat!==undefined && (typeof claims.iat!=="number" || claims.iat>now+60)) ||
        typeof claims.sub!=="string" || !claims.sub ||
        !(typeof claims.aud==="string" ? claims.aud===env.ACCESS_AUD : Array.isArray(claims.aud) && claims.aud.includes(env.ACCESS_AUD))) throw new Error();
    stage="certificates";
    const certs=await fetch(issuer+"/cdn-cgi/access/certs",{redirect:"manual"});
    if (!certs.ok) throw new Error();
    const jwks=await certs.json() as {keys?: (JsonWebKey & {kid?:string;alg?:string;use?:string})[]};
    if (!Array.isArray(jwks.keys) || jwks.keys.length>20) throw new Error();
    const jwk=jwks.keys.find(key=>key.kid===header.kid && key.kty==="RSA" && (!key.alg || key.alg==="RS256") && (!key.use || key.use==="sig"));
    if (!jwk) throw new Error();
    stage="public-key";
    const key=await crypto.subtle.importKey("jwk",jwk,{name:"RSASSA-PKCS1-v1_5",hash:"SHA-256"},false,["verify"]);
    stage="signature";
    if (!await crypto.subtle.verify("RSASSA-PKCS1-v1_5",key,bytes(parts[2]),new TextEncoder().encode(parts[0]+"."+parts[1]))) throw new Error();
    if (typeof claims.email!=="string" || claims.email.toLowerCase()!==env.PORTAL_ADMIN_EMAIL.toLowerCase())
      throw new AccessFailure(403,"This Access identity is not the portal administrator");
  } catch (error) {
    if (error instanceof AccessFailure) throw error;
    console.warn("Access assertion rejected at verification stage",stage);
    throw new AccessFailure(401,"Cloudflare Access identity could not be verified");
  }
}
