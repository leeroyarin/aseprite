"""Pure outbound helpers reused from the local sidecar. No server or execution code."""
from __future__ import annotations
import os
import random
import re
from typing import Any

RE_PRIVATE_KEY = re.compile(
    r"-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY(?: BLOCK)?-----[\s\S]*?-----END (?:[A-Z0-9 ]+ )?PRIVATE KEY(?: BLOCK)?-----"
)

RE_VNC_URL = re.compile(r"(vnc://(?:([^:\s/@]*):)?)([^@\s/]+)(@)", re.IGNORECASE)

RE_GENERIC_URI = re.compile(
    r"((\b[a-zA-Z][a-zA-Z0-9+.-]*://)(?:([^:\s/@]*):))([^@\s/]+)(@)", re.IGNORECASE
)

RE_CLI_FLAG_QUOTED = re.compile(
    r'(?<!\S)(--?(?:pass(?:wd|word)|api[-_]?key|secret|token)(?:=|\s+))(["\'])(.+?)\2',
    re.IGNORECASE,
)

RE_CLI_FLAG_UNQUOTED = re.compile(
    r'(?<!\S)(--?(?:pass(?:wd|word)|api[-_]?key|secret|token)(?:=|\s+))(?!\[REDACTED\])([^\s"\';,]+)',
    re.IGNORECASE,
)

RE_OPENAI_KEY = re.compile(r"\bsk-(?:proj-|admin-|svcacct-)?[A-Za-z0-9_-]{16,}\b")

RE_ANTHROPIC_KEY = re.compile(r"\bsk-ant-[A-Za-z0-9_-]{16,}\b")

RE_GOOGLE_KEY = re.compile(r"\bAIza[0-9A-Za-z_-]{28,45}\b")

RE_GITHUB_TOKEN = re.compile(
    r"\b(?:gh[pousr]_[A-Za-z0-9_]{24,60}|github_pat_[A-Za-z0-9_]{24,100})\b"
)

RE_SLACK_TOKEN = re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{16,}\b")

RE_HUB_TOKEN = re.compile(r"\b(?:ahub_(?:admin|agent)_|amt_|agt[-_])[A-Za-z0-9_-]{8,}\b")

RE_JWT = re.compile(r"\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\b")

RE_BEARER = re.compile(
    r'\b(Bearer\s+)(?!\[REDACTED\])[A-Za-z0-9\-._~+/=]{12,}(?=[\s"\';,}\]]|$)',
    re.IGNORECASE,
)

SECRET_KEYS_PATTERN = (
    r"(?:[a-zA-Z0-9_]*_)?"
    r"(?:password|passwd|pwd|secret|token|vnc_password|vnc_pw|client_secret|"
    r"cf[-_]?access[-_]?client[-_]?secret|"
    r"access_token|auth_token|api_key|apikey|agent_hub_token|hub_token|private_key)"
)

RE_ASSIGN_QUOTED = re.compile(
    rf'(?<![A-Za-z0-9_])((?:["\']?{SECRET_KEYS_PATTERN}["\']?\s*[:=]\s*)(["\']))(?!\[REDACTED\])(.+?)\2',
    re.IGNORECASE,
)

RE_ASSIGN_UNQUOTED = re.compile(
    rf'(\b{SECRET_KEYS_PATTERN}\s*[:=]\s*)(?!\[REDACTED\])([^\s"\';,}}\]\)]+)',
    re.IGNORECASE,
)

def redact_text(
    text: str | None,
    extra_secret: str | list[str | None] | tuple[str | None, ...] | None = None,
) -> str | None:
    """Redact credentials, API keys, passwords, and tokens from string content."""
    if not isinstance(text, str) or not text:
        return text
    result = text
    secrets_list: list[str] = []
    if isinstance(extra_secret, str):
        secrets_list.append(extra_secret)
    elif isinstance(extra_secret, (list, tuple)):
        secrets_list.extend(s for s in extra_secret if isinstance(s, str) and s)
    for sec in secrets_list:
        if len(sec) >= 6 and sec in result:
            result = result.replace(sec, "[REDACTED]")
    if "-----BEGIN" in result and "-----END" in result:
        result = RE_PRIVATE_KEY.sub("[REDACTED]", result)
    result = RE_VNC_URL.sub(r"\1[REDACTED]\4", result)
    result = RE_GENERIC_URI.sub(r"\1[REDACTED]\5", result)
    result = RE_CLI_FLAG_QUOTED.sub(r"\1\2[REDACTED]\2", result)
    result = RE_CLI_FLAG_UNQUOTED.sub(r"\1[REDACTED]", result)
    result = RE_ANTHROPIC_KEY.sub("[REDACTED]", result)
    result = RE_OPENAI_KEY.sub("[REDACTED]", result)
    result = RE_GOOGLE_KEY.sub("[REDACTED]", result)
    result = RE_GITHUB_TOKEN.sub("[REDACTED]", result)
    result = RE_SLACK_TOKEN.sub("[REDACTED]", result)
    result = RE_HUB_TOKEN.sub("[REDACTED]", result)
    result = RE_JWT.sub("[REDACTED]", result)
    result = RE_BEARER.sub(r"\1[REDACTED]", result)
    result = RE_ASSIGN_QUOTED.sub(r"\1[REDACTED]\2", result)
    result = RE_ASSIGN_UNQUOTED.sub(r"\1[REDACTED]", result)
    return result

def redact_payload(
    val: Any,
    extra_secret: str | list[str | None] | tuple[str | None, ...] | None = None,
) -> Any:
    """Recursively redact secrets from dicts, lists, and strings."""
    if isinstance(val, str):
        return redact_text(val, extra_secret=extra_secret)
    if isinstance(val, list):
        return [redact_payload(item, extra_secret=extra_secret) for item in val]
    if isinstance(val, dict):
        out: dict[str, Any] = {}
        for k, v in val.items():
            if isinstance(k, str) and re.search(rf"^{SECRET_KEYS_PATTERN}$", k, re.IGNORECASE):
                out[redact_text(k, extra_secret=extra_secret)] = "[REDACTED]" if v else v
            else:
                out[redact_text(k, extra_secret=extra_secret) if isinstance(k,str) else k] = redact_payload(v, extra_secret=extra_secret)
        return out
    return val

def check_no_cli_token(argv: list[str]) -> None:
    """Refuse to run if a token or Cloudflare Access credential is passed on the command line."""
    for arg in argv:
        lower = arg.lower()
        if (
            lower
            in (
                "--token",
                "-t",
                "--agent-hub-token",
                "--secret",
                "--cf-access-client-secret",
                "--cf-access-client-id",
            )
            or lower.startswith("--token=")
            or lower.startswith("-t=")
            or lower.startswith("--cf-access-client-secret=")
            or lower.startswith("--cf-access-client-id=")
            or lower.startswith("agent_hub_token=")
            or lower.startswith("cf_access_client_secret=")
            or lower.startswith("cf_access_client_id=")
            or lower.startswith("ahub_agent_")
            or lower.startswith("ahub_admin_")
        ):
            raise ValueError(
                "SECURITY VIOLATION: Never pass AGENT_HUB_TOKEN or Cloudflare Access credentials "
                "as command-line arguments. Set environment variables instead."
            )

def get_env_token(env: dict[str, str] | None = None) -> str:
    """Load AGENT_HUB_TOKEN exclusively from environment variables."""
    source = env if env is not None else os.environ
    tok = source.get("AGENT_HUB_TOKEN", "").strip()
    if not tok:
        raise ValueError("Missing required environment variable: AGENT_HUB_TOKEN")
    return tok

def compute_backoff_delay(
    attempt: int,
    *,
    base: float = 1.0,
    cap: float = 60.0,
    jitter_ratio: float = 0.25,
    rng: random.Random | None = None,
) -> float:
    """Compute exponential backoff delay with jitter."""
    exp = min(cap, base * (2 ** max(0, int(attempt))))
    r = rng.random() if rng is not None else random.random()
    jitter = exp * jitter_ratio * r
    return min(cap, exp + jitter)
