#!/usr/bin/env python3
"""
Mesh Subagent for Aseprite 2.0 / Pitch Quest.
Coordinates cross-agent tasks with Muse (Linux VM), executes git operations,
runs verification builds and Lua test suites, updates HANDOFF.md, and
communicates results over Cloudflare Agent Mesh.

Usage:
  python mesh_subagent.py status
  python mesh_subagent.py tasks [--all]
  python mesh_subagent.py ack <task_id> [--status accepted|blocked] [--note "note"] [--force]
  python mesh_subagent.py checkout-task <task_id> [--branch <branch>] [--base <ref>]
  python mesh_subagent.py audit
  python mesh_subagent.py build [--target aseprite]
  python mesh_subagent.py test [--suite all|regression|adversarial|four-req]
  python mesh_subagent.py sync [--branch <branch>] [--remote origin]
  python mesh_subagent.py push-pr [--branch <branch>] [--base Aseprite-2.0] [--title <title>]
  python mesh_subagent.py report-result <task_id> [--notes <notes>] [--dry-run] [--skip-build] [--skip-test]
  python mesh_subagent.py handoff --summary "..." [--detail "..."] [--agent "..."]
  python mesh_subagent.py daemon [--interval 15] [--auto-ack]
"""

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any

# Windows registry fallback for user environment variables
def get_winreg_env(name: str) -> Optional[str]:
    if sys.platform != "win32":
        return None
    try:
        import winreg
        for root in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
            subpath = "Environment" if root == winreg.HKEY_CURRENT_USER else r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
            try:
                key = winreg.OpenKey(root, subpath)
                val, _ = winreg.QueryValueEx(key, name)
                winreg.CloseKey(key)
                if val:
                    return str(val).strip()
            except Exception:
                continue
        return None
    except Exception:
        return None

REPO_ROOT = Path(__file__).resolve().parents[2]
PROCESSED_FILE = REPO_ROOT / ".mesh_processed_tasks.json"
DEFAULT_HUB_URL = "https://agent-mesh-hub.slack-clone-arin.workers.dev"
DEFAULT_AGENT_NAME = "arin-pc-daedalus-antigravity-leeroyarin"
DEFAULT_PROJECT = "agent-hub"


class Config:
    def __init__(self, hub_url: str, agent: str, token: str, projects: List[str]):
        self.hub_url = hub_url.rstrip("/")
        self.agent = agent
        self.token = token
        self.projects = projects
        self.primary_project = projects[0] if projects else DEFAULT_PROJECT

    @classmethod
    def load(cls, args=None) -> "Config":
        hub_url = (
            getattr(args, "url", None)
            or os.environ.get("HUB_URL")
            or get_winreg_env("HUB_URL")
            or DEFAULT_HUB_URL
        )
        agent = (
            getattr(args, "agent", None)
            or os.environ.get("AGENT_NAME")
            or get_winreg_env("AGENT_NAME")
            or DEFAULT_AGENT_NAME
        )
        token = (
            getattr(args, "token", None)
            or os.environ.get("AGENT_TOKEN")
            or get_winreg_env("AGENT_TOKEN")
            or os.environ.get("HUB_TOKEN")
            or get_winreg_env("HUB_TOKEN")
            or ""
        )
        projects_raw = (
            getattr(args, "projects", None)
            or os.environ.get("PROJECTS")
            or get_winreg_env("PROJECTS")
            or DEFAULT_PROJECT
        )
        projects = [p.strip() for p in projects_raw.split(",") if p.strip()]

        if not token:
            print("[Error] AGENT_TOKEN is required in environment or User registry", file=sys.stderr)
            sys.exit(1)

        return cls(hub_url, agent, token, projects)


# ---------------------------------------------------------------------------
# Task State Management
# ---------------------------------------------------------------------------
class TaskStore:
    @staticmethod
    def load() -> Dict[str, Any]:
        if PROCESSED_FILE.exists():
            try:
                data = json.loads(PROCESSED_FILE.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    return {"processed": [], "records": {}}
                if not isinstance(data.get("processed"), list):
                    data["processed"] = []
                if not isinstance(data.get("records"), dict):
                    data["records"] = {}
                return data
            except Exception:
                return {"processed": [], "records": {}}
        return {"processed": [], "records": {}}

    @staticmethod
    def save(data: Dict[str, Any]):
        try:
            PROCESSED_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")
        except Exception as e:
            print(f"[Warning] Could not save task state: {e}", file=sys.stderr)

    @classmethod
    def is_processed(cls, task_id: str) -> bool:
        """Returns True if the task has completed full verification/reporting."""
        data = cls.load()
        if task_id in data.get("processed", []):
            return True
        status = data.get("records", {}).get(task_id, {}).get("status")
        return status in ("COMPLETED", "PROCESSED", "DONE")

    @classmethod
    def is_acknowledged(cls, task_id: str) -> bool:
        """Returns True if this task has already been acknowledged or is in progress/completed."""
        data = cls.load()
        if task_id in data.get("processed", []):
            return True
        rec = data.get("records", {}).get(task_id, {})
        status = rec.get("status")
        return status in ("ACKNOWLEDGED", "IN_PROGRESS", "COMPLETED", "PROCESSED", "DONE") or bool(rec.get("ack_sent"))

    @classmethod
    def is_in_progress(cls, task_id: str) -> bool:
        rec = cls.get_record(task_id) or {}
        return rec.get("status") == "IN_PROGRESS"

    @classmethod
    def get_record(cls, task_id: str) -> Optional[Dict[str, Any]]:
        data = cls.load()
        return data.get("records", {}).get(task_id)

    @classmethod
    def get_in_progress_tasks(cls) -> Dict[str, Dict[str, Any]]:
        data = cls.load()
        records = data.get("records", {})
        return {tid: r for tid, r in records.items() if r.get("status") == "IN_PROGRESS"}

    @classmethod
    def record_new_task(
        cls,
        task_id: str,
        sender: str = "",
        timestamp: str = "",
        body_snippet: str = "",
        subject: str = "",
        body: str = "",
        meta: Optional[Dict[str, Any]] = None,
    ):
        data = cls.load()
        if "records" not in data:
            data["records"] = {}
        rec = data["records"].get(task_id, {})
        # If task already acknowledged, in progress or completed, do not downgrade
        if rec.get("status") in ("ACKNOWLEDGED", "IN_PROGRESS", "COMPLETED", "PROCESSED", "DONE"):
            return

        snippet = body_snippet or (body[:120].strip() if body else "")
        subj = subject or (body.splitlines()[0][:80] if body else "")

        rec["status"] = "NEW"
        rec["sender"] = sender
        rec["timestamp"] = timestamp or datetime.datetime.now(datetime.timezone.utc).isoformat()
        rec["body_snippet"] = snippet
        rec["subject"] = subj
        rec["updated_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        if meta:
            rec.update(meta)
        data["records"][task_id] = rec
        cls.save(data)

    @classmethod
    def record_progress(cls, task_id: str, note: str, message_id: Optional[str] = None):
        data = cls.load()
        if "records" not in data:
            data["records"] = {}
        rec = data["records"].get(task_id, {})
        rec["last_progress_note"] = note
        if message_id:
            rec["last_progress_id"] = message_id
        now_iso = datetime.datetime.now(datetime.timezone.utc).isoformat()
        rec["last_progress_at"] = now_iso
        rec["updated_at"] = now_iso
        data["records"][task_id] = rec
        cls.save(data)

    @classmethod
    def record_state(cls, task_id: str, status: str, meta: Optional[Dict[str, Any]] = None):
        data = cls.load()
        if "records" not in data:
            data["records"] = {}
        rec = data["records"].get(task_id, {})
        # Guard against downgrading from active/completed to NEW
        if status == "NEW" and rec.get("status") in ("ACKNOWLEDGED", "IN_PROGRESS", "COMPLETED", "PROCESSED", "DONE"):
            return
        rec["status"] = status
        rec["updated_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        if status in ("ACKNOWLEDGED", "IN_PROGRESS", "COMPLETED"):
            rec["ack_sent"] = True
        if meta:
            rec.update(meta)
        data["records"][task_id] = rec
        if status in ("COMPLETED", "PROCESSED", "DONE"):
            processed_set = set(data.get("processed", []))
            processed_set.add(task_id)
            data["processed"] = list(processed_set)
        cls.save(data)


# ---------------------------------------------------------------------------
# Subprocess / Shell Runner
# ---------------------------------------------------------------------------
def run_cmd(cmd_list: List[str], cwd: Path = REPO_ROOT, check: bool = False) -> Tuple[int, str, str]:
    """Execute a system command safely and return (returncode, stdout, stderr)."""
    p = subprocess.Popen(
        cmd_list,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace"
    )
    stdout, stderr = p.communicate()
    if check and p.returncode != 0:
        raise RuntimeError(f"Command failed ({p.returncode}): {' '.join(cmd_list)}\n{stderr}")
    return p.returncode, stdout.strip(), stderr.strip()


# ---------------------------------------------------------------------------
# Git Operations
# ---------------------------------------------------------------------------
class GitOps:
    @staticmethod
    def fetch(remote: str = "origin") -> Tuple[int, str, str]:
        return run_cmd(["git", "fetch", "--prune", remote])

    @staticmethod
    def current_branch() -> str:
        code, out, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        return out if code == 0 else ""

    @staticmethod
    def current_sha() -> str:
        code, out, _ = run_cmd(["git", "rev-parse", "HEAD"])
        return out if code == 0 else ""

    @staticmethod
    def is_clean() -> bool:
        _, out, _ = run_cmd(["git", "status", "--porcelain"])
        return len(out) == 0

    @staticmethod
    def status_porcelain() -> str:
        _, out, _ = run_cmd(["git", "status", "--porcelain"])
        return out

    @staticmethod
    def checkout(ref: str) -> Tuple[int, str, str]:
        GitOps.fetch()
        # Check local branch
        code, out, _ = run_cmd(["git", "branch", "--list", ref])
        if out:
            return run_cmd(["git", "checkout", ref])
        # Check remote branch
        remote_ref = ref if ref.startswith("origin/") else f"origin/{ref}"
        clean_local_name = ref.replace("origin/", "")
        code, out, _ = run_cmd(["git", "branch", "-r", "--list", remote_ref])
        if out:
            return run_cmd(["git", "checkout", "-b", clean_local_name, remote_ref])
        return run_cmd(["git", "checkout", ref])

    @staticmethod
    def pull(remote: str = "origin", branch: Optional[str] = None) -> Tuple[int, str, str]:
        cmd = ["git", "pull", remote]
        if branch:
            cmd.append(branch)
        return run_cmd(cmd)

    @staticmethod
    def sync(branch: Optional[str] = None, remote: str = "origin") -> Tuple[int, str, str]:
        """Synchronize target branch: fetch, checkout, and pull latest commits if remote branch exists."""
        target_branch = branch or GitOps.current_branch()
        fetch_code, fetch_out, fetch_err = GitOps.fetch(remote)
        if fetch_code != 0:
            return fetch_code, fetch_out, f"Fetch failed: {fetch_err}"

        checkout_code, checkout_out, checkout_err = GitOps.checkout(target_branch)
        if checkout_code != 0:
            return checkout_code, checkout_out, f"Checkout failed: {checkout_err}"

        # Check if remote branch exists on origin
        r_code, r_out, _ = run_cmd(["git", "branch", "-r", "--list", f"{remote}/{target_branch}"])
        if not r_out:
            return 0, f"Branch '{target_branch}' is local (not yet on remote '{remote}'). Fetch and checkout clean.", ""

        pull_code, pull_out, pull_err = GitOps.pull(remote, target_branch)
        return pull_code, pull_out, pull_err

    @staticmethod
    def create_feature_branch(branch_name: str, start_point: str = "origin/Aseprite-2.0") -> Tuple[int, str, str]:
        GitOps.fetch()
        # If branch already exists locally, switch to it
        code, out, _ = run_cmd(["git", "branch", "--list", branch_name])
        if out:
            return run_cmd(["git", "checkout", branch_name])

        # If branch already exists on remote origin and no custom start_point was provided
        code, out, _ = run_cmd(["git", "branch", "-r", "--list", f"origin/{branch_name}"])
        if out and start_point == "origin/Aseprite-2.0":
            return run_cmd(["git", "checkout", "-b", branch_name, f"origin/{branch_name}"])

        return run_cmd(["git", "checkout", "-b", branch_name, start_point])

    @staticmethod
    def check_diff() -> Tuple[int, str, str]:
        return run_cmd(["git", "diff", "--check"])

    @staticmethod
    def diff_stat(base: str = "origin/Aseprite-2.0") -> Tuple[int, str, str]:
        return run_cmd(["git", "diff", f"{base}...HEAD", "--stat"])

    @staticmethod
    def commit(message: str, add_paths: Optional[List[str]] = None) -> Tuple[int, str, str]:
        if add_paths:
            for p in add_paths:
                code, _, err = run_cmd(["git", "add", p])
                if code != 0:
                    return code, "", err
        else:
            run_cmd(["git", "add", "-u"])
        return run_cmd(["git", "commit", "-m", message])

    @staticmethod
    def push(branch: str, remote: str = "origin", force: bool = False) -> Tuple[int, str, str]:
        cmd = ["git", "push", "-u", remote, branch]
        if force:
            cmd.append("--force-with-lease")
        return run_cmd(cmd)

    @staticmethod
    def audit_hygiene(config_token: Optional[str] = None) -> Tuple[bool, List[str]]:
        """
        Enforce Aseprite 2.0 hygiene policies:
        1. No whitespace / conflict markers (git diff --check)
        2. No tokens (amt_*, gho_*, raw token) in diff or untracked files
        3. No modifications or additions inside third_party/ (tracked or untracked)
        4. No PixelLab proprietary files added (tracked or untracked)
        5. Check line lengths in src/ <= 100 cols (ONLY newly added lines in diff)
        """
        issues: List[str] = []

        # 1. git diff --check (whitespace and conflict markers)
        code, out, err = GitOps.check_diff()
        if code != 0 or out:
            issues.append(f"git diff --check failed:\n{out or err}")

        # Gather tracked modified files and untracked files
        _, tracked_changed, _ = run_cmd(["git", "diff", "--name-only", "HEAD"])
        tracked_list = [f.strip().replace("\\", "/") for f in tracked_changed.splitlines() if f.strip()]

        _, status_porcelain, _ = run_cmd(["git", "status", "--porcelain"])
        untracked_list: List[str] = []
        for line in status_porcelain.splitlines():
            line_str = line.strip()
            if line_str.startswith("??"):
                untracked_list.append(line_str[2:].strip().replace("\\", "/"))

        all_touched_files = tracked_list + untracked_list

        # 2. Check third_party and pixellab additions/modifications
        for f_clean in all_touched_files:
            if f_clean.startswith("third_party/"):
                issues.append(f"Forbidden modification or addition in third_party/: {f_clean}")
            if "pixellab" in f_clean.lower():
                issues.append(f"Forbidden PixelLab file modification or addition: {f_clean}")

        # 3. Token pattern scanner
        token_regex = re.compile(r"amt_[a-zA-Z0-9]{20,}|gho_[a-zA-Z0-9]{20,}")

        # (a) Check added lines in git diff HEAD
        _, diff_text, _ = run_cmd(["git", "diff", "HEAD"])
        for line in diff_text.splitlines():
            if line.startswith("+") and not line.startswith("+++"):
                for match in token_regex.finditer(line):
                    issues.append(f"Potential secret token leaked in diff: {match.group(0)[:8]}...")
                if config_token and len(config_token) >= 8 and config_token in line:
                    issues.append(f"Configured secret token leaked in diff: {config_token[:8]}...")

        # (b) Check untracked text files
        binary_extensions = {".exe", ".dll", ".obj", ".png", ".jpg", ".jpeg", ".zip", ".ase", ".aseprite", ".tar", ".gz"}
        for f_rel in untracked_list:
            if f_rel.endswith("test_mesh_subagent.py") or f_rel.startswith("tests/"):
                continue
            p = REPO_ROOT / f_rel
            if p.is_file() and p.suffix.lower() not in binary_extensions and p.stat().st_size < 1024 * 1024:
                try:
                    content = p.read_text(encoding="utf-8", errors="replace")
                    for match in token_regex.finditer(content):
                        issues.append(f"Secret token leaked in untracked file {f_rel}: {match.group(0)[:8]}...")
                    if config_token and len(config_token) >= 8 and config_token in content:
                        issues.append(f"Configured token leaked in untracked file {f_rel}: {config_token[:8]}...")
                except Exception:
                    pass

        # 4. Check line lengths in src/ <= 100 cols (ONLY on newly added lines in diff)
        # Using git diff -U0 HEAD -- "src/*" parses exact added lines without penalizing legacy code
        code, src_diff, _ = run_cmd(["git", "diff", "-U0", "HEAD", "--", "src/"])
        if code == 0 and src_diff:
            current_file = ""
            for line in src_diff.splitlines():
                if line.startswith("+++ b/"):
                    current_file = line[6:].strip()
                elif line.startswith("+") and not line.startswith("+++"):
                    added_line = line[1:]  # strip leading '+'
                    if current_file.startswith("src/") and (current_file.endswith(".cpp") or current_file.endswith(".h") or current_file.endswith(".c")):
                        if len(added_line) > 100:
                            issues.append(f"Line length > 100 cols in added code of {current_file} (length: {len(added_line)}): {added_line[:40]}...")
                            if len(issues) > 15:
                                break

        return len(issues) == 0, issues

    @staticmethod
    def create_pr(branch: str, base: str = "Aseprite-2.0", title: str = "", body: str = "") -> Tuple[int, str]:
        """Create pull request using GitHub CLI (gh) if available."""
        if not shutil.which("gh"):
            return 1, "GitHub CLI 'gh' is not installed on PATH"

        cmd = [
            "gh", "pr", "create",
            "--base", base,
            "--head", branch,
            "--title", title or f"feat: {branch}",
            "--body", body or f"Automated pull request for {branch} into {base}"
        ]
        code, out, err = run_cmd(cmd)
        combined = f"{out}\n{err}".strip()
        if code == 0:
            return 0, out.strip()

        # If PR already exists, try to extract existing PR URL
        existing_match = re.search(r"https:\/\/github\.com\/[^\s]+\/pull\/\d+", combined)
        if existing_match:
            return 0, f"PR already exists: {existing_match.group(0)}"

        return code, err.strip() or out.strip()


# ---------------------------------------------------------------------------
# Agent Mesh Hub Communication
# ---------------------------------------------------------------------------
class MeshClient:
    def __init__(self, cfg: Config):
        self.cfg = cfg

    def _http_request(
        self, endpoint: str, method: str = "GET", body: Optional[Dict[str, Any]] = None, max_retries: int = 2
    ) -> Tuple[int, Dict[str, Any]]:
        url = f"{self.cfg.hub_url}{endpoint}"
        headers = {
            "Authorization": f"Bearer {self.cfg.token}",
            "User-Agent": f"AgentMeshSubagent/{self.cfg.agent}",
            "Content-Type": "application/json",
        }
        data = json.dumps(body).encode("utf-8") if body else None

        for attempt in range(max_retries + 1):
            req = urllib.request.Request(url, data=data, headers=headers, method=method)
            try:
                with urllib.request.urlopen(req, timeout=15) as resp:
                    content = resp.read().decode("utf-8")
                    return resp.status, json.loads(content) if content else {}
            except urllib.error.HTTPError as e:
                err_content = e.read().decode("utf-8")
                try:
                    parsed_err = json.loads(err_content)
                except Exception:
                    parsed_err = {"error": err_content}
                if e.code in (502, 503, 504) and attempt < max_retries:
                    time.sleep(1.0 * (attempt + 1))
                    continue
                return e.code, parsed_err
            except Exception as e:
                if attempt < max_retries:
                    time.sleep(1.0 * (attempt + 1))
                    continue
                return 0, {"error": str(e)}
        return 0, {"error": "Request failed after retries"}

    def send_message(
        self,
        to: str,
        body: str,
        msg_type: str = "result",
        reply_to: Optional[str] = None,
        project: Optional[str] = None
    ) -> Dict[str, Any]:
        proj = project or self.cfg.primary_project
        payload = {
            "id": str(uuid.uuid4()),
            "project": proj,
            "from": self.cfg.agent,
            "to": to,
            "type": msg_type,
            "body": body,
            "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        }
        if reply_to:
            payload["reply_to"] = reply_to

        status, res = self._http_request("/send", method="POST", body=payload)
        if status == 200 and res.get("ok"):
            return res
        raise RuntimeError(f"Failed to send mesh message (HTTP {status}): {res.get('error', res)}")

    def fetch_inbox(self, since: str = "0", limit: int = 100) -> List[Dict[str, Any]]:
        projects_arg = ",".join(self.cfg.projects)
        qs = urllib.parse.urlencode({
            "agent": self.cfg.agent,
            "projects": projects_arg,
            "since": since,
            "limit": str(limit),
        })
        status, res = self._http_request(f"/inbox?{qs}", method="GET")
        if status == 200:
            return res.get("messages", [])
        print(f"[Warning] Failed to fetch inbox (HTTP {status}): {res.get('error', res)}", file=sys.stderr)
        return []

    def get_status(self) -> Dict[str, Any]:
        status, res = self._http_request("/status", method="GET")
        return res if status == 200 else {"error": res}

    def list_pending_muse_tasks(self, include_active: bool = False) -> List[Dict[str, Any]]:
        """
        Lists tasks from Muse.
        If include_active=False (default), returns only unacknowledged new tasks.
        If include_active=True, returns uncompleted tasks (unacknowledged + in_progress).
        """
        messages = self.fetch_inbox(since="0")
        pending: List[Dict[str, Any]] = []

        for msg in messages:
            msg_id = msg.get("id")
            sender = (msg.get("from") or "").lower()
            recipient = msg.get("to")
            msg_type = (msg.get("type") or "").lower()

            if not msg_id or TaskStore.is_processed(msg_id):
                continue
            if not ("muse" in sender or "william" in sender):
                continue
            if msg_type not in ("task", "chat", "directive", "instruction"):
                continue

            # Check if directed to us, arin-pc, or broadcast all
            is_recipient = recipient in (self.cfg.agent, "arin-pc", "all")
            if not is_recipient:
                continue

            # Explicit persistence for NEW tasks when unacknowledged
            if not TaskStore.is_acknowledged(msg_id):
                raw_body = msg.get("body") or ""
                subject = msg.get("subject") or (raw_body.splitlines()[0][:80] if raw_body else "")
                snippet = raw_body[:120].strip() if raw_body else ""
                TaskStore.record_new_task(
                    task_id=msg_id,
                    sender=msg.get("from") or "",
                    timestamp=msg.get("timestamp") or datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    body_snippet=snippet,
                    subject=subject,
                )

            if not include_active and TaskStore.is_acknowledged(msg_id):
                continue

            pending.append(msg)

        return pending

    def list_unacknowledged_tasks(self) -> List[Dict[str, Any]]:
        return self.list_pending_muse_tasks(include_active=False)

    def send_ack(self, task_id: str, status: str = "accepted", note: str = "", force: bool = False) -> Dict[str, Any]:
        """
        Sends exactly ONE acknowledgement message to Muse per directive.
        Format: [status] <note>
        Enforces Anti-Loop rule: refuses duplicate ACKs unless force=True.
        """
        if not force and TaskStore.is_acknowledged(task_id):
            rec = TaskStore.get_record(task_id) or {}
            print(f"[Notice] Task {task_id} already acknowledged (status: {rec.get('status')}). Skipping duplicate ACK per anti-loop policy.")
            return {"ok": True, "id": rec.get("ack_message_id", "cached"), "skipped": True}

        body = f"[{status.lower()}] {note if note else 'Task received and active.'}"
        res = self.send_message(
            to="muse",
            body=body,
            msg_type="status",
            reply_to=task_id,
            project=self.cfg.primary_project
        )
        TaskStore.record_state(task_id, "ACKNOWLEDGED", {
            "ack_note": note,
            "ack_status": status,
            "ack_message_id": res.get("id"),
        })
        return res

    def send_progress(self, task_id: str, note: str) -> Dict[str, Any]:
        """
        Dispatches an interim progress message to Muse.
        Format: [progress] <note>
        Allows interim progress reporting without locking or colliding with the single-ACK anti-loop guard.
        """
        body = f"[progress] {note.strip() if note else 'Work in progress.'}"
        res = self.send_message(
            to="muse",
            body=body,
            msg_type="status",
            reply_to=task_id,
            project=self.cfg.primary_project
        )
        TaskStore.record_progress(task_id, note=note, message_id=res.get("id"))
        return res


# ---------------------------------------------------------------------------
# Build & Test Runner
# ---------------------------------------------------------------------------
class BuildAndTestRunner:
    @staticmethod
    def run_msvc_build(target: str = "aseprite") -> Dict[str, Any]:
        """Runs the Windows MSVC build via tools\\aseprite2-build.ps1 or cmake/ninja."""
        build_script = REPO_ROOT / "tools" / "aseprite2-build.ps1"
        start_t = time.time()

        if build_script.exists():
            cmd = ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(build_script), "-Target", target]
        else:
            cmd = ["cmake", "--build", "build", "--target", target]

        code, stdout, stderr = run_cmd(cmd)
        duration = round(time.time() - start_t, 2)
        aseprite_bin = REPO_ROOT / "build" / "bin" / "aseprite.exe"

        return {
            "success": (code == 0 and aseprite_bin.exists()),
            "exit_code": code,
            "duration_s": duration,
            "bin_exists": aseprite_bin.exists(),
            "output": stdout if code == 0 else f"{stdout}\n{stderr}",
        }

    @staticmethod
    def parse_test_output(stdout: str, stderr: str) -> Dict[str, Any]:
        """Parses pass/fail counts, total duration, and failure diagnostics from Lua test run."""
        combined = f"{stdout}\n{stderr}"
        passed = 0
        failed = 0

        # Pattern 1: Standard summary line: TOTAL:  XX Passed,  YY Failed (Total ZZ)
        m_total = re.search(r"TOTAL:\s*(\d+)\s+Passed,\s*(\d+)\s+Failed", combined, re.IGNORECASE)
        # Pattern 2: XX passed, YY failed
        m_passed_failed = re.search(r"(\d+)\s+passed,\s*(\d+)\s+failed", combined, re.IGNORECASE)
        # Pattern 3: XX/YY passed
        m_fraction = re.search(r"(\d+)/(\d+)\s+(?:tests?\s+)?pass(?:ed)?", combined, re.IGNORECASE)

        if m_total:
            passed = int(m_total.group(1))
            failed = int(m_total.group(2))
        elif m_passed_failed:
            passed = int(m_passed_failed.group(1))
            failed = int(m_passed_failed.group(2))
        elif m_fraction:
            passed = int(m_fraction.group(1))
            total = int(m_fraction.group(2))
            failed = total - passed
        else:
            # Fallback: Count individual PASS and FAIL result markers
            pass_count = len(re.findall(r"\[PASS\]|:\s*PASS\b", combined))
            fail_count = len(re.findall(r"\[FAIL\]|:\s*FAIL\b", combined))
            if pass_count > 0 or fail_count > 0:
                passed = pass_count
                failed = fail_count

        # Extract timing
        t_match = re.search(r"finished in\s+([\d\.]+)\s+seconds", combined, re.IGNORECASE)
        duration = float(t_match.group(1)) if t_match else 0.0

        # Extract failure lines
        failure_snippets = []
        for line in combined.splitlines():
            line_str = line.strip()
            if any(term in line_str.lower() for term in ["fail", "error", "assert", "violation"]) and "0 failed" not in line_str.lower():
                failure_snippets.append(line_str)

        return {
            "passed": passed,
            "failed": failed,
            "duration_s": duration,
            "failures": failure_snippets[:10],
        }

    @staticmethod
    def run_lua_test(test_script_rel: str) -> Dict[str, Any]:
        """Runs an individual Lua test script using tests\\run_easymetric_tests.ps1."""
        runner_ps1 = REPO_ROOT / "tests" / "run_easymetric_tests.ps1"
        test_file = REPO_ROOT / test_script_rel

        if not test_file.exists():
            return {
                "success": False,
                "exit_code": 1,
                "script": test_script_rel,
                "error": f"Test script not found: {test_script_rel}",
            }

        start_t = time.time()
        cmd = [
            "powershell",
            "-ExecutionPolicy", "Bypass",
            "-File", str(runner_ps1),
            "-TestScript", test_script_rel,
        ]
        code, stdout, stderr = run_cmd(cmd)
        duration = round(time.time() - start_t, 2)
        parsed = BuildAndTestRunner.parse_test_output(stdout, stderr)
        parsed["duration_s"] = parsed["duration_s"] or duration
        parsed["script"] = test_script_rel
        parsed["exit_code"] = code
        parsed["success"] = (code == 0 and parsed["failed"] == 0)
        parsed["raw_output"] = stdout[:2000]
        return parsed

    @staticmethod
    def run_all_suites() -> Dict[str, Any]:
        """Runs the three primary Easymetric test suites."""
        suites = [
            ("regression", "tests/easymetric_test.lua"),
            ("adversarial", "tests/easymetric_adversarial_stress.lua"),
            ("four_requirements", "tests/easymetric_four_requirements_test.lua"),
        ]
        results = {}
        all_passed = True

        for name, path in suites:
            res = BuildAndTestRunner.run_lua_test(path)
            results[name] = res
            if not res.get("success"):
                all_passed = False

        return {
            "all_passed": all_passed,
            "suites": results,
        }


# ---------------------------------------------------------------------------
# HANDOFF.md Management
# ---------------------------------------------------------------------------
class HandoffManager:
    @staticmethod
    def prepend_log_entry(summary: str, details: List[str], agent_label: str = "Daedalus (Subagent)") -> bool:
        handoff_path = REPO_ROOT / "docs" / "aseprite2" / "HANDOFF.md"
        if not handoff_path.exists():
            return False

        today = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
        lines = handoff_path.read_text(encoding="utf-8").splitlines()

        entry_lines = [
            f"- {today} — {agent_label}: {summary}",
        ]
        for d in details:
            entry_lines.append(f"  - {d}")

        target_idx = -1
        for idx, line in enumerate(lines):
            if "## Log (newest first)" in line:
                target_idx = idx + 1
                break

        if target_idx != -1:
            new_lines = lines[:target_idx] + entry_lines + lines[target_idx:]
            handoff_path.write_text("\n".join(new_lines) + "\n", encoding="utf-8")
            return True
        return False


# ---------------------------------------------------------------------------
# Task Parser & Formatter
# ---------------------------------------------------------------------------
class TaskParser:
    @staticmethod
    def parse_muse_task(body: str) -> Dict[str, Any]:
        """
        Extracts branch suggestions, base commits, directives, and required checks
        from Muse's structured task prompt. Handles varied formats robustly.
        """
        # Suggested branch
        branch = None
        branch_match = re.search(r"(?:suggested|branch):\s*[`'\"]?([a-zA-Z0-9_\-\/]+)[`'\"]?", body, re.IGNORECASE)
        if branch_match:
            branch = branch_match.group(1).strip()
        else:
            branch_fallback = re.search(r"branch\s+[`'\"]?([a-zA-Z0-9_\-\/]+)[`'\"]?", body, re.IGNORECASE)
            if branch_fallback:
                branch = branch_fallback.group(1).strip()

        # Base ref or commit
        base_ref = "origin/Aseprite-2.0"
        base_match = re.search(r"Checkout:?\s*(?:[a-zA-Z0-9_\-\.]+\s+at\s+)?(?:exactly\s+)?([a-f0-9]{7,40})", body, re.IGNORECASE)
        if base_match:
            base_ref = base_match.group(1).strip()
        else:
            base_kw = re.search(r"base(?:\s+commit)?:\s*[`'\"]?([a-f0-9]{7,40}|[a-zA-Z0-9_\-\.\/]+)[`'\"]?", body, re.IGNORECASE)
            if base_kw:
                base_ref = base_kw.group(1).strip()

        # Check for phase title: TASK [—–-:] Phase X...
        phase_match = re.search(r"TASK\s*[\u2014\u2013\-\:]\s*([^\n\r]+)", body, re.IGNORECASE)
        if not phase_match:
            phase_match = re.search(r"(Phase\s*\d+[^\n\r]*)", body, re.IGNORECASE)
        phase_title = phase_match.group(1).strip() if phase_match else "Agent Mesh Task"

        return {
            "branch": branch,
            "base": base_ref,
            "phase": phase_title,
        }

    @staticmethod
    def format_final_report(
        branch: str,
        commit_sha: str,
        build_res: Dict[str, Any],
        suite_results: Dict[str, Any],
        pr_url: Optional[str] = None,
        notes: Optional[str] = None
    ) -> str:
        """
        Formats report strictly matching Muse's required schema:
        branch, commit SHA, build result, test script paths, pass/fail counts,
        times, exact steps to reproduce any failure, and PR information.
        """
        lines = [
            f"RESULT — Verification & Task Report for branch '{branch}'",
            "",
            f"1. Branch & Commit:",
            f"   • Branch:     {branch}",
            f"   • Commit SHA: {commit_sha}",
            f"   • Remote PR:  {pr_url if pr_url else '(pending or not pushed)'}",
            "",
            f"2. Build Status:",
            f"   • Exit Code:  {build_res.get('exit_code', 'N/A')}",
            f"   • Duration:   {build_res.get('duration_s', 0)}s",
            f"   • Binary:     {'Generated (build/bin/aseprite.exe)' if build_res.get('bin_exists') else 'Not Found'}",
            "",
            "3. Test Suite Execution Results:"
        ]

        suites = suite_results.get("suites", {})
        for name, res in suites.items():
            script = res.get("script", name)
            passed = res.get("passed", 0)
            failed = res.get("failed", 0)
            dur = res.get("duration_s", 0)
            status_str = "PASSED" if res.get("success") else "FAILED"
            lines.append(f"   • [{status_str}] {script}: {passed} passed, {failed} failed ({dur}s)")
            if failed > 0:
                lines.append(f"     Failures:")
                for f_line in res.get("failures", []):
                    lines.append(f"       - {f_line}")

        if notes:
            lines.extend(["", "4. Additional Context & Notes:", f"   {notes}"])

        return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI Command Implementations
# ---------------------------------------------------------------------------
def cmd_status(cfg: Config, args):
    mesh = MeshClient(cfg)
    hub_stat = mesh.get_status()
    pending = mesh.list_unacknowledged_tasks()
    active_in_progress = TaskStore.get_in_progress_tasks()
    clean = GitOps.is_clean()

    print("\n" + "=" * 65)
    print(f" Agent Mesh Subagent Status — [{cfg.agent}]")
    print("=" * 65)
    print(f" Hub Endpoint:     {cfg.hub_url}")
    print(f" Primary Project:  {cfg.primary_project}")
    print(f" Repository Root:  {REPO_ROOT}")
    print(f" Current Branch:   {GitOps.current_branch()}")
    print(f" Current SHA:      {GitOps.current_sha()[:10]}")
    print(f" Working Tree:     {'CLEAN' if clean else 'DIRTY'}")
    if not clean:
        print(f" Working Tree Changes:\n{GitOps.status_porcelain()}")
    print("-" * 65)
    print(f" Active Hub Conns: {hub_stat.get('connections', 'unknown')}")
    print(f" Active In-Progress Tasks ({len(active_in_progress)}):")
    for tid, r in active_in_progress.items():
        print(f"   • [{tid[:8]}] Branch: {r.get('branch')} (base: {r.get('base_ref')})")
    print(f" Pending Unacknowledged Muse Tasks ({len(pending)}):")
    for t in pending:
        b_snippet = t.get("body", "").replace("\n", " ")[:60]
        print(f"   • [{t.get('id')[:8]}] ({t.get('type')}) {b_snippet}...")
    print("=" * 65 + "\n")


def cmd_tasks(cfg: Config, args):
    mesh = MeshClient(cfg)
    include_all = getattr(args, "all", False)
    tasks = mesh.list_pending_muse_tasks(include_active=include_all)

    if not tasks:
        print("No pending tasks from Muse.")
        return

    print(f"\nFound {len(tasks)} task(s) from Muse:\n")
    for msg in tasks:
        t_id = msg.get("id")
        rec = TaskStore.get_record(t_id) or {}
        print(f"Task ID:   {t_id}")
        print(f"Timestamp: {msg.get('timestamp')}")
        print(f"State:     {rec.get('status', 'NEW')}")
        print(f"Recipient: {msg.get('to')}")
        print(f"Type:      {msg.get('type')}")
        print(f"Body:\n{msg.get('body')}")
        print("-" * 65)


def cmd_ack(cfg: Config, args):
    mesh = MeshClient(cfg)
    task_id = args.task_id.strip()
    status = args.status or "accepted"
    note = args.note or ""
    force = getattr(args, "force", False)

    print(f"Sending ACK to Muse for task {task_id} with status '{status}'...")
    res = mesh.send_ack(task_id, status=status, note=note, force=force)
    if res.get("skipped"):
        print(f"Notice: ACK skipped (already sent). Use --force to override.")
    else:
        print(f"ACK successfully dispatched: {res.get('id')}")


def cmd_checkout_task(cfg: Config, args):
    mesh = MeshClient(cfg)
    task_id = args.task_id.strip()

    # Find task message in inbox
    inbox = mesh.fetch_inbox(since="0")
    target_msg = next((m for m in inbox if m.get("id") == task_id), None)
    if not target_msg:
        print(f"[Error] Task {task_id} not found in mesh inbox.", file=sys.stderr)
        sys.exit(1)

    parsed = TaskParser.parse_muse_task(target_msg.get("body", ""))
    branch_name = args.branch or parsed.get("branch") or f"feature/task-{task_id[:8]}"
    base_ref = args.base or parsed.get("base") or "origin/Aseprite-2.0"

    print(f"Preparing task branch: '{branch_name}' off base '{base_ref}'...")
    code, out, err = GitOps.create_feature_branch(branch_name, start_point=base_ref)
    if code != 0:
        print(f"[Error] Failed to checkout branch {branch_name}: {err or out}", file=sys.stderr)
        sys.exit(code)

    TaskStore.record_state(task_id, "IN_PROGRESS", {
        "branch": branch_name,
        "base_ref": base_ref,
        "sha": GitOps.current_sha(),
    })
    print(f"Now on branch '{GitOps.current_branch()}' at commit {GitOps.current_sha()[:10]}")


def cmd_audit(cfg: Config, args):
    print("Auditing working tree against Aseprite 2.0 hygiene policies...")
    clean_audit, issues = GitOps.audit_hygiene(config_token=cfg.token)
    if clean_audit:
        print("PASS: Working tree passes all hygiene checks (no whitespace issues, no token leaks, no third_party/pixellab modifications, line length <= 100).")
    else:
        print(f"FAIL: Found {len(issues)} hygiene issue(s):", file=sys.stderr)
        for issue in issues:
            print(f"  • {issue}", file=sys.stderr)
        sys.exit(1)


def cmd_build(cfg: Config, args):
    print(f"Executing MSVC build for target '{args.target}'...")
    res = BuildAndTestRunner.run_msvc_build(target=args.target)
    print(f"Build Exit Code: {res['exit_code']} (Duration: {res['duration_s']}s)")
    if res["success"]:
        print(f"Build SUCCESS: aseprite.exe verified at build/bin/aseprite.exe")
    else:
        print(f"Build FAILED:\n{res['output']}", file=sys.stderr)
        sys.exit(res["exit_code"] or 1)


def cmd_test(cfg: Config, args):
    suite_opt = getattr(args, "suite", "all")
    if suite_opt == "all":
        print("Running all Easymetric test suites...")
        res = BuildAndTestRunner.run_all_suites()
        for name, s_res in res["suites"].items():
            status = "PASS" if s_res.get("success") else "FAIL"
            print(f"[{status}] {name}: {s_res.get('passed')} passed, {s_res.get('failed')} failed ({s_res.get('duration_s')}s)")
        if not res["all_passed"]:
            sys.exit(1)
    else:
        suite_map = {
            "regression": "tests/easymetric_test.lua",
            "adversarial": "tests/easymetric_adversarial_stress.lua",
            "four-req": "tests/easymetric_four_requirements_test.lua",
        }
        script = suite_map.get(suite_opt, "tests/easymetric_test.lua")
        print(f"Running test suite: {script}...")
        s_res = BuildAndTestRunner.run_lua_test(script)
        status = "PASS" if s_res.get("success") else "FAIL"
        print(f"[{status}] {script}: {s_res.get('passed')} passed, {s_res.get('failed')} failed ({s_res.get('duration_s')}s)")
        if not s_res.get("success"):
            sys.exit(s_res.get("exit_code", 1))


def cmd_sync(cfg: Config, args):
    branch = args.branch or GitOps.current_branch()
    remote = args.remote or "origin"
    print(f"Synchronizing branch '{branch}' with remote '{remote}'...")
    code, out, err = GitOps.sync(branch=branch, remote=remote)
    if code != 0:
        print(f"[Error] Sync failed (exit {code}): {err or out}", file=sys.stderr)
        sys.exit(code)
    print(f"Branch '{branch}' synchronized successfully.")
    if out:
        print(f"Git output:\n{out}")
    print(f"Current SHA:  {GitOps.current_sha()[:10]}")
    print(f"Working Tree: {'CLEAN' if GitOps.is_clean() else 'DIRTY'}")


def cmd_push_pr(cfg: Config, args):
    branch = args.branch or GitOps.current_branch()
    base = args.base or "Aseprite-2.0"
    title = args.title or f"feat: {branch}"
    body = args.body or f"PR for {branch} targeting {base}."

    print(f"Pushing branch '{branch}' to origin...")
    code, out, err = GitOps.push(branch)
    if code != 0:
        print(f"[Error] Push failed: {err or out}", file=sys.stderr)
        sys.exit(code)

    print(f"Opening PR into '{base}'...")
    pr_code, pr_output = GitOps.create_pr(branch=branch, base=base, title=title, body=body)
    if pr_code == 0:
        print(f"PR Status: {pr_output}")
    else:
        print(f"[Warning] PR creation returned code {pr_code}: {pr_output}")


def cmd_report_result(cfg: Config, args):
    mesh = MeshClient(cfg)
    task_id = args.task_id.strip()

    branch = GitOps.current_branch()
    sha = GitOps.current_sha()
    rec = TaskStore.get_record(task_id) or {}
    pr_url = args.pr_url or rec.get("pr_url")

    # Build & test execution
    if getattr(args, "skip_build", False):
        print("Skipping MSVC build (--skip-build specified)...")
        build_res = {"exit_code": 0, "duration_s": 0.0, "bin_exists": (REPO_ROOT / "build/bin/aseprite.exe").exists()}
    else:
        print("Gathering MSVC build verification records...")
        build_res = BuildAndTestRunner.run_msvc_build()

    if getattr(args, "skip_test", False):
        print("Skipping Lua tests (--skip-test specified)...")
        test_res = {"all_passed": True, "suites": {}}
    else:
        print("Running all verification suites...")
        test_res = BuildAndTestRunner.run_all_suites()

    report_text = TaskParser.format_final_report(
        branch=branch,
        commit_sha=sha,
        build_res=build_res,
        suite_results=test_res,
        pr_url=pr_url,
        notes=args.notes
    )

    print("\nReport Preview:\n" + "=" * 55 + f"\n{report_text}\n" + "=" * 55)

    if getattr(args, "dry_run", False):
        print("Dry-run mode: report NOT sent to Hub.")
        return

    print(f"Delivering final report to Muse on '{cfg.primary_project}'...")
    res = mesh.send_message(
        to="muse",
        body=report_text,
        msg_type="result",
        reply_to=task_id,
        project=cfg.primary_project
    )
    TaskStore.record_state(task_id, "COMPLETED", {
        "final_report_id": res.get("id"),
        "sha": sha,
        "branch": branch,
    })
    print(f"Result delivered successfully: {res.get('id')}")


def cmd_handoff(cfg: Config, args):
    summary = args.summary.strip()
    details = args.detail or []
    agent_label = args.agent or "Daedalus (Subagent)"

    success = HandoffManager.prepend_log_entry(summary=summary, details=details, agent_label=agent_label)
    if success:
        print(f"Successfully prepended log entry to docs/aseprite2/HANDOFF.md under ## Log (newest first).")
    else:
        print(f"[Error] Failed to update docs/aseprite2/HANDOFF.md", file=sys.stderr)
        sys.exit(1)


def cmd_progress(cfg: Config, args):
    mesh = MeshClient(cfg)
    task_id = args.task_id.strip()
    note = args.note.strip() if args.note else ""
    if not note:
        print("[Error] Progress note cannot be empty (--note '...')", file=sys.stderr)
        sys.exit(1)

    print(f"Sending progress update to Muse for task {task_id}...")
    res = mesh.send_progress(task_id, note=note)
    print(f"Progress update successfully dispatched: {res.get('id')}")


def cmd_daemon(cfg: Config, args):
    mesh = MeshClient(cfg)
    interval = getattr(args, "interval", 15) or 15
    auto_ack = getattr(args, "auto_ack", False)
    once = getattr(args, "once", False)
    max_ticks = 1 if once else getattr(args, "max_ticks", None)

    print(f"[{cfg.agent}] Starting Mesh Subagent Daemon (interval: {interval}s, auto-ack: {auto_ack}, once: {once}, max_ticks: {max_ticks})...")
    if not once:
        print("Press Ctrl+C to stop.")

    tick_count = 0
    consecutive_errors = 0
    max_backoff = 60.0

    try:
        while True:
            if max_ticks is not None and tick_count >= max_ticks:
                break

            try:
                # ONLY scan for fresh unacknowledged tasks
                pending = mesh.list_unacknowledged_tasks()
                for task in pending:
                    t_id = task.get("id")
                    body = task.get("body", "")
                    print(f"\n[Incoming Task from Muse] id={t_id[:8]} body={body[:60]}...")
                    if auto_ack:
                        if not TaskStore.is_acknowledged(t_id):
                            print(f"Acknowledging task {t_id[:8]}...")
                            mesh.send_ack(t_id, status="accepted", note="Task received by subagent daemon.")
                    else:
                        print(f"Task {t_id[:8]} queued. Run 'python mesh_subagent.py ack {t_id}' to acknowledge.")

                consecutive_errors = 0
                tick_count += 1

                if max_ticks is not None and tick_count >= max_ticks:
                    break

                time.sleep(interval)

            except Exception as e:
                consecutive_errors += 1
                backoff = min(max_backoff, 2.0 ** consecutive_errors)
                print(f"[Warning] Polling error in daemon tick {tick_count + 1} (consecutive: {consecutive_errors}): {e}", file=sys.stderr)
                if once:
                    break
                print(f"[Notice] Backing off for {backoff:.1f}s before next retry...", file=sys.stderr)
                time.sleep(backoff)

    except KeyboardInterrupt:
        print(f"\n[{cfg.agent}] Subagent daemon stopped.")

    print(f"[{cfg.agent}] Subagent daemon finished (ticks completed: {tick_count}).")
    return 0


# ---------------------------------------------------------------------------
# CLI Argument Parser
# ---------------------------------------------------------------------------
def main():
    parent_parser = argparse.ArgumentParser(add_help=False)
    parent_parser.add_argument("--url", help="Agent Mesh Hub URL")
    parent_parser.add_argument("--agent", help="Agent name override")
    parent_parser.add_argument("--token", help="Agent token override")
    parent_parser.add_argument("--projects", help="Comma-separated project list")

    parser = argparse.ArgumentParser(description="Mesh Subagent for Aseprite 2.0", parents=[parent_parser])
    sub = parser.add_subparsers(dest="command")

    # status
    p_stat = sub.add_parser("status", help="Show coordinator, git, and hub status", parents=[parent_parser])
    p_stat.set_defaults(func=cmd_status)

    # tasks
    p_tasks = sub.add_parser("tasks", help="List tasks from Muse", parents=[parent_parser])
    p_tasks.add_argument("--all", action="store_true", help="Include active/in-progress tasks")
    p_tasks.set_defaults(func=cmd_tasks)

    # ack
    p_ack = sub.add_parser("ack", help="Send single ACK message to Muse", parents=[parent_parser])
    p_ack.add_argument("task_id", help="Task message ID to acknowledge")
    p_ack.add_argument("--status", default="accepted", choices=["accepted", "blocked", "done"], help="One-line status")
    p_ack.add_argument("--note", default="", help="Optional note")
    p_ack.add_argument("--force", action="store_true", help="Force send even if already acknowledged")
    p_ack.set_defaults(func=cmd_ack)

    # checkout-task
    p_checkout = sub.add_parser("checkout-task", help="Prepare and checkout branch for Muse task", parents=[parent_parser])
    p_checkout.add_argument("task_id", help="Task message ID")
    p_checkout.add_argument("--branch", help="Branch name override")
    p_checkout.add_argument("--base", help="Base commit or branch override")
    p_checkout.set_defaults(func=cmd_checkout_task)

    # audit
    p_audit = sub.add_parser("audit", help="Run Aseprite 2.0 hygiene audit", parents=[parent_parser])
    p_audit.set_defaults(func=cmd_audit)

    # build
    p_build = sub.add_parser("build", help="Run MSVC build", parents=[parent_parser])
    p_build.add_argument("--target", default="aseprite", help="Build target")
    p_build.set_defaults(func=cmd_build)

    # test
    p_test = sub.add_parser("test", help="Run Lua test suite(s)", parents=[parent_parser])
    p_test.add_argument("--suite", default="all", choices=["all", "regression", "adversarial", "four-req"])
    p_test.set_defaults(func=cmd_test)

    # sync
    p_sync = sub.add_parser("sync", help="Synchronize branch with remote (fetch, checkout, pull)", parents=[parent_parser])
    p_sync.add_argument("--branch", help="Branch name (default: current branch)")
    p_sync.add_argument("--remote", default="origin", help="Remote name (default: origin)")
    p_sync.set_defaults(func=cmd_sync)

    # push-pr
    p_pr = sub.add_parser("push-pr", help="Push branch and open GitHub PR", parents=[parent_parser])
    p_pr.add_argument("--branch", help="Branch name")
    p_pr.add_argument("--base", default="Aseprite-2.0", help="Base branch")
    p_pr.add_argument("--title", help="PR title")
    p_pr.add_argument("--body", help="PR description")
    p_pr.set_defaults(func=cmd_push_pr)

    # report-result
    p_report = sub.add_parser("report-result", help="Deliver final formatted result to Muse", parents=[parent_parser])
    p_report.add_argument("task_id", help="Task message ID")
    p_report.add_argument("--pr-url", help="Pull request URL")
    p_report.add_argument("--notes", help="Optional notes")
    p_report.add_argument("--dry-run", action="store_true", help="Preview report without sending to Hub")
    p_report.add_argument("--skip-build", action="store_true", help="Skip running MSVC build")
    p_report.add_argument("--skip-test", action="store_true", help="Skip running Lua test suites")
    p_report.set_defaults(func=cmd_report_result)

    # progress
    p_prog = sub.add_parser("progress", help="Send interim progress update to Muse", parents=[parent_parser])
    p_prog.add_argument("task_id", help="Task message ID to update")
    p_prog.add_argument("--note", required=True, help="Progress description or status note")
    p_prog.set_defaults(func=cmd_progress)

    # handoff
    p_handoff = sub.add_parser("handoff", help="Prepend progress entry to docs/aseprite2/HANDOFF.md", parents=[parent_parser])
    p_handoff.add_argument("--summary", required=True, help="One-line summary")
    p_handoff.add_argument("--detail", action="append", help="Detail bullet (can specify multiple times)")
    p_handoff.set_defaults(func=cmd_handoff)

    # daemon
    p_daemon = sub.add_parser("daemon", help="Run continuous subagent polling loop", parents=[parent_parser])
    p_daemon.add_argument("--interval", type=int, default=15, help="Polling interval in seconds")
    p_daemon.add_argument("--auto-ack", action="store_true", help="Automatically acknowledge incoming tasks")
    p_daemon.add_argument("--once", action="store_true", help="Run a single polling tick and return 0")
    p_daemon.add_argument("--max-ticks", type=int, default=None, help="Maximum number of polling ticks before exiting")
    p_daemon.set_defaults(func=cmd_daemon)

    args = parser.parse_args()
    cfg = Config.load(args)

    if not args.command or args.command == "status":
        cmd_status(cfg, args)
    else:
        args.func(cfg, args)


if __name__ == "__main__":
    main()
