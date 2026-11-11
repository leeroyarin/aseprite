/** Port of the local build's server/redaction.py; no parsing or execution of messages. */
export function redact(text: string, known: string[] = []): string {
  let safe = text;
  for (const secret of known) if (secret && secret.length >= 8) safe = safe.split(secret).join("[REDACTED]");
  const keys = "(?:[a-zA-Z0-9_]*_)?(?:password|passwd|pwd|secret|token|vnc_password|vnc_pw|client_secret|cf[-_]?access[-_]?client[-_]?secret|access_token|auth_token|api[-_]?key|apikey|agent_hub_token|hub_token|private_key)";
  safe = safe
    .replace(/-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY(?: BLOCK)?-----[\s\S]*?-----END (?:[A-Z0-9 ]+ )?PRIVATE KEY(?: BLOCK)?-----/g,"[REDACTED]")
    .replace(/(\b[a-zA-Z][a-zA-Z0-9+.-]*:\/\/[^:\s/@]*:)[^@\s/]+(@)/g,"$1[REDACTED]$2")
    .replace(/(vnc:\/\/)[^@\s/]+(@)/gi,"$1[REDACTED]$2")
    .replace(/(^|\s)(--?(?:pass(?:wd|word)|api[-_]?key|secret|token)(?:=|\s+))(["'])(.*?)\3/gi,"$1$2$3[REDACTED]$3")
    .replace(/(^|\s)(--?(?:pass(?:wd|word)|api[-_]?key|secret|token)(?:=|\s+))(?!\[REDACTED\])([^\s"';,]+)/gi,"$1$2[REDACTED]")
    .replace(/\b(?:sk-(?:proj-|admin-|svcacct-|ant-)?|amt_|ahub_|agt[-_]|gh[pousr]_|github_pat_|xox[baprs]-)[A-Za-z0-9_-]{8,}/g,"[REDACTED]")
    .replace(/\bAIza[0-9A-Za-z_-]{28,45}\b/g,"[REDACTED]")
    .replace(/\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\b/g,"[REDACTED]")
    .replace(/\bBearer\s+(?!\[REDACTED\])[A-Za-z0-9._~+\/=-]+/gi,"Bearer [REDACTED]");
  safe = safe.replace(new RegExp("([\"']?"+keys+"[\"']?\\s*[:=]\\s*)([\"'])(.*?)\\2","gi"),"$1$2[REDACTED]$2");
  return safe.replace(new RegExp("(\\b"+keys+"\\s*[:=]\\s*)(?!\\[REDACTED\\])([^\\s\"';,}\\]\\)]+)","gi"),"$1[REDACTED]");
}
