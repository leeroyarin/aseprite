#!/usr/bin/env python3
"""Append-only command data logger. No command execution, network, or normal chat writes.

Install using verified chat-path evidence and a separate explicit commands folder.
Proof reads an existing challenge packet and logs receipt; another agent client sends it.
"""
from __future__ import annotations
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import sys
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

MAX_BYTES = 65536
STATUSES = {"received", "started", "done", "failed"}
COMMAND_TYPES = {"command", "enrollment.challenge"}

def stamp():
    return datetime.now(timezone.utc).isoformat()

def sha(data):
    return hashlib.sha256(data).hexdigest()

def load_json(path):
    with Path(path).open("rb") as f:
        data=f.read(MAX_BYTES+1)
    if len(data)>MAX_BYTES:
        raise ValueError("Input too large")
    value=json.loads(data)
    if not isinstance(value,dict):
        raise ValueError("JSON object required")
    return value

def separated(chat,commands):
    if chat==commands or chat in commands.parents or commands in chat.parents:
        raise ValueError("Chat and commands directories must be separate")

def safe_path(path):
    value=str(path)
    if redact_text(value)!=value or "[REDACTED]" in value:
        raise ValueError("Credential-shaped path")
    return value

def discover_codex(root):
    root=Path(root).resolve(strict=True)
    directory=root/"sessions"
    if not directory.is_dir():
        raise ValueError("No sessions directory found; research this tool's real storage")
    for file in itertools.islice(directory.rglob("*.jsonl"),200):
        if file.is_symlink():
            continue
        try:
            with file.open("rb") as f:
                first=f.readline(MAX_BYTES+1)
            if len(first)>MAX_BYTES:
                continue
            meta=json.loads(first)
            if meta.get("type")=="session_meta":
                # Only metadata validates the path. Do not copy any chat payload.
                return {"chat_path":safe_path(directory.resolve()),"tool":"codex",
                    "method":"observed session_meta header","evidence_paths":[safe_path(file)],
                    "verified_at":stamp(),"session_metadata_verified":True}
        except (OSError,ValueError):
            continue
    raise ValueError("Session metadata not verified; research the actual chat path")

def install(chat_path,commands_path,evidence_file):
    chat=Path(chat_path).resolve(strict=True)
    commands=Path(commands_path).resolve()
    if not chat.is_dir():
        raise ValueError("Chat directory must already exist")
    separated(chat,commands)
    evidence=load_json(evidence_file)
    if Path(evidence.get("chat_path","")).resolve()!=chat or not evidence.get("tool") or not evidence.get("method") or not evidence.get("evidence_paths"):
        raise ValueError("Chat discovery evidence does not match")
    for item in evidence["evidence_paths"]:
        if not isinstance(item,str) or not Path(item).exists():
            raise ValueError("Discovery evidence path does not exist")
    safe_path(chat);safe_path(commands)
    commands.mkdir(parents=True,exist_ok=True)
    script=commands/"hub_command_log.py"
    contents=Path(__file__).read_bytes()
    if script.exists():
        if script.is_symlink() or script.read_bytes()!=contents:
            raise ValueError("Existing script differs; preserve it and choose a new commands folder")
    else:
        with script.open("xb") as f:
            f.write(contents);f.flush();os.fsync(f.fileno())
    manifest={"chat_path":str(chat),"commands_path":str(commands),"script_sha256":sha(contents),
        "discovery":redact_payload(evidence),"installed_at":stamp()}
    config=commands/"manifest.json"
    if config.is_symlink():
        raise ValueError("Unsafe manifest path")
    if config.exists():
        old=load_json(config)
        if any(old.get(k)!=manifest[k] for k in ["chat_path","commands_path","script_sha256"]):
            raise ValueError("Existing manifest differs; preserve it")
        manifest=old
    else:
        with config.open("x",encoding="utf-8") as f:
            json.dump(manifest,f,ensure_ascii=False);f.flush();os.fsync(f.fileno())
    return manifest

def configuration(path):
    config=load_json(path)
    chat=Path(config["chat_path"]).resolve(strict=True)
    commands=Path(config["commands_path"]).resolve(strict=True)
    separated(chat,commands)
    if Path(path).resolve()!=commands/"manifest.json":
        raise ValueError("Manifest must be in its commands folder")
    script=commands/"hub_command_log.py"
    if script.is_symlink() or sha(script.read_bytes())!=config["script_sha256"]:
        raise ValueError("Installed script hash differs")
    return config

@contextmanager
def locked(commands):
    lock=commands/"command-log.lock"
    fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    try:
        os.close(fd)
        yield
    finally:
        lock.unlink()

def log_command(config,message,status="received"):
    if status not in STATUSES:
        raise ValueError("Invalid status")
    if message.get("type") not in COMMAND_TYPES:
        return {"ignored":True}
    for key in ["id","project","from"]:
        value=message.get(key)
        if not isinstance(value,str) or not value or len(value)>200 or any(ord(c)<32 for c in value) or redact_text(value)!=value:
            raise ValueError("Invalid command metadata")
    reply=message.get("reply_to")
    if reply is not None and (not isinstance(reply,str) or len(reply)>200 or any(ord(c)<32 for c in reply) or redact_text(reply)!=reply):
        raise ValueError("Invalid reply metadata")
    text=message.get("body")
    if not isinstance(text,str):
        raise ValueError("Command text must be a string")
    text=redact_text(text)
    if len(text.encode())>32768:
        raise ValueError("Command text too large")
    record={"time":stamp(),"project":message["project"],"sender":message["from"],
        "message_id":message["id"],"reply_to":redact_payload(message.get("reply_to")),"text":text,"status":status}
    commands=Path(config["commands_path"])
    path=commands/"commands.jsonl"
    if path.is_symlink() or path.resolve().parent!=commands.resolve():
        raise ValueError("Unsafe log path")
    with locked(commands):
        previous=[]
        if path.exists():
            with path.open(encoding="utf-8") as f:
                for line in f:
                    row=json.loads(line)  # Never repair/delete corrupt history silently.
                    if row.get("message_id")==record["message_id"]:
                        previous.append(row)
        for old in previous:
            if any(old.get(k)!=record.get(k) for k in ["project","sender","message_id","reply_to","text"]):
                raise ValueError("Message id conflict")
            if old["status"]==status:
                return {"duplicate":True,"record":old}
        last=previous[-1]["status"] if previous else None
        if (last is None and status!="received") or (last=="received" and status!="started") or (last=="started" and status not in {"done","failed"}) or last in {"done","failed"}:
            raise ValueError("Invalid append-only status transition")
        with path.open("a",encoding="utf-8") as f:
            f.write(json.dumps(record,ensure_ascii=False)+"\n");f.flush();os.fsync(f.fileno())
    return {"duplicate":False,"record":record}

def proof(config,message):
    commands=Path(config["commands_path"])
    if Path(__file__).resolve()!=commands/"hub_command_log.py":
        raise ValueError("Run the installed script to create proof")
    if message.get("type")!="enrollment.challenge" or message.get("from")!="hub-supervisor":
        raise ValueError("Expected the hub challenge packet")
    challenge=json.loads(message["body"])
    required=["challenge_id","code","rulebook_sha256","script_sha256","expires_at"]
    if any(not isinstance(challenge.get(k),str) or not challenge[k] for k in required):
        raise ValueError("Incomplete challenge")
    expiry=datetime.fromisoformat(challenge["expires_at"].replace("Z","+00:00"))
    if expiry<=datetime.now(timezone.utc) or challenge["script_sha256"]!=config["script_sha256"]:
        raise ValueError("Expired challenge or wrong installed script")
    received=log_command(config,message,"received")["record"]
    return {"id":"proof:"+challenge["challenge_id"],"challenge_id":challenge["challenge_id"],
        "code":challenge["code"],"script_sha256":sha(Path(__file__).read_bytes()),
        "chat_path":config["chat_path"],"commands_path":config["commands_path"],
        "rulebook_sha256":challenge["rulebook_sha256"],"message_id":message["id"],
        "log_entry_sha256":sha(json.dumps(received,ensure_ascii=False,sort_keys=True,separators=(",",":")).encode()),
        "logged_at":received["time"]}

class QuietParser(argparse.ArgumentParser):
    def error(self,message):
        raise ValueError("Invalid logger options")

def main(argv=None):
    parser=QuietParser(description=__doc__)
    sub=parser.add_subparsers(dest="action",required=True)
    discover=sub.add_parser("discover-codex");discover.add_argument("--codex-home",required=True)
    setup=sub.add_parser("install");setup.add_argument("--chat-dir",required=True);setup.add_argument("--commands-dir",required=True);setup.add_argument("--evidence-file",required=True)
    for name in ["log","proof"]:
        cmd=sub.add_parser(name);cmd.add_argument("--config",required=True);cmd.add_argument("--message-file",required=True)
        if name=="log":cmd.add_argument("--status",choices=sorted(STATUSES),default="received")
    args=parser.parse_args(argv)
    if args.action=="discover-codex":result=discover_codex(args.codex_home)
    elif args.action=="install":result=install(args.chat_dir,args.commands_dir,args.evidence_file)
    else:
        config=configuration(args.config)
        message_path=Path(args.message_file).resolve(strict=True)
        chat=Path(config["chat_path"])
        if message_path==chat or chat in message_path.parents:
            raise ValueError("Do not read tool chats as command packets")
        message=load_json(message_path)
        result=proof(config,message) if args.action=="proof" else log_command(config,message,args.status)
        if args.action=="log":result={k:v for k,v in result.items() if k!="record"}
    print(json.dumps(result,ensure_ascii=False))
    return 0

if __name__=="__main__":
    try:
        raise SystemExit(main())
    except (ValueError,OSError,KeyError,TypeError):
        print("Logger input or installation invalid; existing history preserved",file=sys.stderr)
        raise SystemExit(2)
