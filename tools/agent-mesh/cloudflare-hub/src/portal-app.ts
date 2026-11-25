/**
 * The React owner portal (agent-hub-web) is served from this Worker's static assets under /hub/.
 *
 * Authentication decision: the files are PUBLIC and unauthenticated. They are the compiled UI
 * only: no data, no secrets, no per-user content, and the source is not confidential. Every byte of
 * data comes from /api/v2/admin/*, which verifies the owner's session cookie on each request
 * (see github-auth.ts). A signed-out visitor therefore gets the shell, which asks the session
 * route, receives 401, and shows "Sign in with GitHub". Keeping the shell public is also what lets
 * the app show a clear sign-in screen when a session expires mid-use instead of a redirect loop.
 */
export const PORTAL_APP_PREFIX = "/hub";

// Fonts come from Google Fonts (the design system's bundle.css imports them). Everything else is
// same-origin. Inline styles are needed because React and the design system set style attributes.
const SHELL_CSP = "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; " +
  "font-src https://fonts.gstatic.com; img-src 'self' data:; connect-src 'self'; object-src 'none'; " +
  "frame-ancestors 'none'; base-uri 'none'; form-action 'self'";

type AssetsEnv = { ASSETS?: Fetcher } & Record<string, unknown>;

function problem(status: number, error: string, extra: Record<string, string> = {}) {
  return Response.json({ error }, { status, headers: { "Cache-Control": "no-store", ...extra } });
}

export async function portalApp(request: Request, env: AssetsEnv): Promise<Response | null> {
  const url = new URL(request.url);
  const path = url.pathname;
  if (path !== PORTAL_APP_PREFIX && !path.startsWith(PORTAL_APP_PREFIX + "/")) return null;
  if (path === PORTAL_APP_PREFIX) {
    return new Response(null, { status: 308, headers: { Location: PORTAL_APP_PREFIX + "/" + url.search, "Cache-Control": "no-store" } });
  }
  if (request.method !== "GET" && request.method !== "HEAD") return problem(405, "Method not allowed", { Allow: "GET, HEAD" });
  if (!env.ASSETS) return problem(503, "Owner portal assets are not deployed");

  const headers = new Headers(request.headers);
  headers.delete("Cookie");
  headers.delete("Authorization");
  let response = await env.ASSETS.fetch(new Request(url.toString(), { method: request.method, headers }));

  // Client-side routes (/hub/p/<project>/c/<channel>, ...) have no file: answer with the app shell.
  // A missing file (a path with an extension) stays a 404 so a broken asset URL is never HTML.
  const last = path.slice(path.lastIndexOf("/") + 1);
  let shell = path === PORTAL_APP_PREFIX + "/";
  if (response.status === 404 && !/\.[A-Za-z0-9]+$/.test(last)) {
    response = await env.ASSETS.fetch(new Request(url.origin + PORTAL_APP_PREFIX + "/", { method: request.method, headers }));
    shell = true;
  }
  if (response.status >= 300 && response.status < 400) return response;

  const out = new Response(response.body, response);
  out.headers.set("X-Content-Type-Options", "nosniff");
  out.headers.set("Referrer-Policy", "no-referrer");
  if (shell || (response.headers.get("Content-Type") || "").startsWith("text/html")) {
    out.headers.set("Content-Security-Policy", SHELL_CSP);
    out.headers.set("Cache-Control", "no-store");
  } else if (path.startsWith(PORTAL_APP_PREFIX + "/assets/") && response.ok) {
    out.headers.set("Cache-Control", "public, max-age=31536000, immutable"); // content-hashed by the build
  } else {
    out.headers.set("Cache-Control", "no-cache");
  }
  return out;
}
