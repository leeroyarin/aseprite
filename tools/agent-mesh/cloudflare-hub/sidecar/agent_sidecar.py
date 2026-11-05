#!/usr/bin/env python3
"""Outbound-only Agent Hub client. Save hub messages as data; never execute them.

Port of the earlier sidecar's environment-only auth, redaction and backoff.
No local HTTP server, tunnel, command interpreter, or automatic acknowledgement.
Optional explicitly supplied usage/handoff JSON files can be uploaded as evidence;
this client never discovers private tool usage, creates commits, or pushes Git.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sys
import time
import urllib.request
from urllib.error import HTTPError
from urllib.parse import urlencode, urlsplit, quote
from helpers import check_no_cli_token, get_env_token, compute_backoff_delay, redact_payload

HUB_URL = "https://agent-mesh-hub.slack-clone-arin.workers.dev"
MAX_RESPONSE = 8 * 1024 * 1024

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("Hub redirects are refused")

def validate_url(url):
    parsed = urlsplit(url)
    if (parsed.scheme != "https" or parsed.netloc != urlsplit(HUB_URL).netloc
            or parsed.path not in ("", "/") or parsed.query or parsed.fragment):
        raise ValueError("Use the existing HTTPS hub address")
    return url.rstrip("/")

def fetch_page(token, project, cursor, opener=None):
    query = urlencode({"projects": project, "since": cursor, "limit": 100})
    req = urllib.request.Request(HUB_URL + "/api/v2/inbox?" + query,
                                 headers={"Authorization": "Bearer " + token})
    client = opener or urllib.request.build_opener(NoRedirect())
    recovery = False
    try:
        response = client.open(req, timeout=30)
    except HTTPError as error:
        if error.code != 403:
            raise
        error.close()
        recovery = True
        response = client.open(urllib.request.Request(HUB_URL + "/api/v2/enrollment/challenges", headers={"Authorization": "Bearer " + token}), timeout=30)
    with response:
        raw = response.read(MAX_RESPONSE + 1)
    if len(raw) > MAX_RESPONSE:
        raise ValueError("Hub response too large")
    page = json.loads(raw)
    if not isinstance(page, dict) or not isinstance(page.get("messages"), list):
        raise ValueError("Invalid inbox response")
    if any(not isinstance(m, dict) or not isinstance(m.get("id"), str) for m in page["messages"]):
        raise ValueError("Invalid message")
    if recovery:
        page.update(latest_id=cursor if cursor != "0" else None, has_more=False, enrollment_only=True)
    return page

def post_report(token, project, report_path, kind, task=None, opener=None):
    """Upload explicit agent-produced evidence; never infer counters or execute Git."""
    if kind not in ("usage", "handoff") or (kind == "handoff" and not task):
        raise ValueError("Invalid report kind or task")
    with Path(report_path).open("rb") as source:
        raw = source.read(65537)
    if len(raw) > 65536:
        raise ValueError("Report too large")
    payload = json.loads(raw)
    if not isinstance(payload, dict):
        raise ValueError("Report must be an object")
    safe = redact_payload(payload, extra_secret=token)
    path = "/api/v2/projects/" + quote(project, safe="")
    path += "/usage" if kind == "usage" else "/tasks/" + quote(task, safe="") + "/handoffs"
    body = json.dumps(safe, ensure_ascii=False).encode("utf-8")
    if len(body) > 65536:
        raise ValueError("Report too large")
    request = urllib.request.Request(HUB_URL + path, data=body, method="POST",
        headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    client = opener or urllib.request.build_opener(NoRedirect())
    with client.open(request, timeout=30) as response:
        data = response.read(MAX_RESPONSE + 1)
    if len(data) > MAX_RESPONSE:
        raise ValueError("Response too large")
    result = json.loads(data)
    if not isinstance(result, dict):
        raise ValueError("Invalid report response")
    return result

def post_enrollment(token, report_path, phase, opener=None):
    """Send an explicit acceptance/challenge/proof file; never run the logger."""
    if phase not in ("accept", "challenges", "proofs"):
        raise ValueError("Invalid enrollment phase")
    with Path(report_path).open("rb") as source:
        raw = source.read(65537)
    if len(raw) > 65536:
        raise ValueError("Enrollment report too large")
    payload = json.loads(raw)
    if not isinstance(payload, dict):
        raise ValueError("Enrollment report must be an object")
    body = json.dumps(redact_payload(payload, extra_secret=token), ensure_ascii=False).encode("utf-8")
    if len(body) > 65536:
        raise ValueError("Enrollment report too large")
    request = urllib.request.Request(HUB_URL + "/api/v2/enrollment/" + phase, data=body, method="POST",
        headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    client = opener or urllib.request.build_opener(NoRedirect())
    with client.open(request, timeout=30) as response:
        raw = response.read(MAX_RESPONSE + 1)
    if len(raw) > MAX_RESPONSE:
        raise ValueError("Enrollment response too large")
    result = json.loads(raw)
    if not isinstance(result, dict):
        raise ValueError("Invalid enrollment response")
    # An HTTP success may still contain a recorded failed proof.
    if phase == "proofs" and result.get("success") is not True:
        raise ValueError("Enrollment proof was recorded as failed")
    return result

def save_page(path, page, token):
    """Append and fsync before advancing cursor. Retry after crash deduplicates IDs."""
    path = Path(path)
    seen = set()
    if path.exists():
        with path.open(encoding="utf-8") as source:
            for line in source:
                seen.add(json.loads(line)["id"])
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with path.open("a", encoding="utf-8") as target:
        for message in page["messages"]:
            safe = redact_payload(message, extra_secret=token)
            if safe["id"] in seen:
                continue
            # No parsing or dispatch based on message text or type.
            target.write(json.dumps(safe, ensure_ascii=False) + "\n")
            seen.add(safe["id"])
            count += 1
        target.flush()
        os.fsync(target.fileno())
    return count

def poll_once(project, inbox, cursor_path, token, opener=None):
    cursor_path = Path(cursor_path)
    cursor = json.loads(cursor_path.read_text(encoding="utf-8"))["cursor"] if cursor_path.exists() else "0"
    page = fetch_page(token, project, cursor, opener)
    count = save_page(inbox, page, token)
    last = page.get("latest_id")
    if last:
        cursor_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = cursor_path.with_name(cursor_path.name + ".pending")
        temporary.write_text(json.dumps({"cursor": last}), encoding="utf-8")
        temporary.replace(cursor_path)
    return count, bool(page.get("has_more"))

class QuietParser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError("Invalid client options")

def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    check_no_cli_token(args)
    parser = QuietParser(description=__doc__)
    parser.add_argument("--project", required=True)
    parser.add_argument("--inbox", required=True, type=Path, help="Explicit hub-data JSONL path, separate from tool chats")
    parser.add_argument("--cursor", required=True, type=Path)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--usage-report", type=Path, help="Explicit agent-produced usage JSON; missing counters remain unknown")
    parser.add_argument("--handoff-report", type=Path, help="Explicit handoff JSON; this client never commits or pushes")
    parser.add_argument("--task", help="Task id for --handoff-report")
    parser.add_argument("--enrollment-accept", type=Path)
    parser.add_argument("--enrollment-challenge", type=Path)
    parser.add_argument("--enrollment-proof", type=Path)
    options = parser.parse_args(args)
    if bool(options.handoff_report) != bool(options.task):
        raise ValueError("Handoff report and task must be specified together")
    token = get_env_token()
    validate_url(os.environ.get("HUB_URL", HUB_URL))
    if options.inbox.resolve() == options.cursor.resolve():
        raise ValueError("Inbox and cursor must be separate files")
    failures = 0
    while True:
        try:
            _, more = poll_once(options.project, options.inbox, options.cursor, token)
            failures = 0
            report_failed = False
            for report_path, kind in [(options.usage_report, "usage"), (options.handoff_report, "handoff")]:
                if report_path:
                    try:
                        post_report(token, options.project, report_path, kind, task=options.task)
                    except Exception:
                        print("Evidence report failed; inbox polling remains available", file=sys.stderr)
                        report_failed = True
            for report_path, phase in [(options.enrollment_accept, "accept"), (options.enrollment_challenge, "challenges"), (options.enrollment_proof, "proofs")]:
                if report_path:
                    try:
                        post_enrollment(token, report_path, phase)
                    except Exception:
                        print("Enrollment report failed; inbox polling remains available", file=sys.stderr)
                        report_failed = True
            if options.once: return 1 if report_failed else 0
            if not more: time.sleep(15)
        except Exception:
            # Never render exception text, URLs, payloads or credentials.
            print("Hub poll failed; saved history retained", file=sys.stderr)
            if options.once: return 1
            time.sleep(compute_backoff_delay(min(failures, 8)))
            failures += 1

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError):
        print("Invalid client configuration; check paths and environment", file=sys.stderr)
        raise SystemExit(2)
