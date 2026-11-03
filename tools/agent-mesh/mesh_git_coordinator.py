#!/usr/bin/env python3
"""
Mesh Git Coordinator for Aseprite 2.0 / Pitch Quest.
Coordinates cross-agent tasks with Muse (Linux VM), executes git workflows,
runs validations, and replies via Agent Mesh.
"""

import argparse
import datetime
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

# Fallback to winreg on Windows if env vars not set in subshell
if sys.platform == "win32":
    try:
        import winreg
        def get_reg_env(name):
            try:
                key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment")
                val, _ = winreg.QueryValueEx(key, name)
                winreg.CloseKey(key)
                return str(val).strip()
            except Exception:
                return None
    except ImportError:
        def get_reg_env(name):
            return None
else:
    def get_reg_env(name):
        return None

REPO_ROOT = Path(__file__).resolve().parents[2]
PROCESSED_FILE = REPO_ROOT / ".mesh_processed_tasks.json"


def get_config():
    hub_url = os.environ.get("HUB_URL") or get_reg_env("HUB_URL") or "https://agent-mesh-hub.slack-clone-arin.workers.dev"
    agent_name = os.environ.get("AGENT_NAME") or get_reg_env("AGENT_NAME") or "arin-pc-daedalus-antigravity-leeroyarin"
    agent_token = os.environ.get("AGENT_TOKEN") or get_reg_env("AGENT_TOKEN") or ""
    projects_raw = os.environ.get("PROJECTS") or get_reg_env("PROJECTS") or "agent-hub"
    projects = [p.strip() for p in projects_raw.split(",") if p.strip()]

    if not agent_token:
        print("[Error] AGENT_TOKEN is required in environment or User registry", file=sys.stderr)
        sys.exit(1)

    return {
        "hub_url": hub_url.rstrip("/"),
        "agent": agent_name,
        "token": agent_token,
        "projects": projects,
        "primary_project": projects[0] if projects else "agent-hub",
    }


def load_processed_ids() -> set:
    if PROCESSED_FILE.exists():
        try:
            data = json.loads(PROCESSED_FILE.read_text(encoding="utf-8"))
            return set(data.get("processed", []))
        except Exception:
            return set()
    return set()


def mark_processed_id(msg_id: str):
    processed = load_processed_ids()
    processed.add(msg_id)
    try:
        PROCESSED_FILE.write_text(json.dumps({"processed": list(processed)}, indent=2), encoding="utf-8")
    except Exception as e:
        print(f"[Warning] Could not save processed task state: {e}", file=sys.stderr)


def run_cmd(cmd_list, cwd=REPO_ROOT, check=False):
    """Run a shell / system command safely and return (returncode, stdout, stderr)."""
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


# Git Actions
class GitOps:
    @staticmethod
    def fetch(remote="origin"):
        return run_cmd(["git", "fetch", remote])

    @staticmethod
    def current_branch():
        _, out, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        return out

    @staticmethod
    def current_sha():
        _, out, _ = run_cmd(["git", "rev-parse", "HEAD"])
        return out

    @staticmethod
    def is_clean():
        _, out, _ = run_cmd(["git", "status", "--porcelain"])
        return len(out) == 0

    @staticmethod
    def status():
        code, out, err = run_cmd(["git", "status", "-s"])
        return out

    @staticmethod
    def checkout(branch):
        GitOps.fetch()
        # Check if local branch exists
        code, out, _ = run_cmd(["git", "branch", "--list", branch])
        if out:
            return run_cmd(["git", "checkout", branch])
        # Check if remote branch exists
        code, out, _ = run_cmd(["git", "branch", "-r", "--list", f"origin/{branch}"])
        if out:
            return run_cmd(["git", "checkout", "-b", branch, f"origin/{branch}"])
        return run_cmd(["git", "checkout", branch])

    @staticmethod
    def pull(remote="origin", branch=None):
        cmd = ["git", "pull", remote]
        if branch:
            cmd.append(branch)
        return run_cmd(cmd)

    @staticmethod
    def create_feature_branch(branch_name, start_point="origin/Aseprite-2.0"):
        GitOps.fetch()
        return run_cmd(["git", "checkout", "-b", branch_name, start_point])

    @staticmethod
    def check_diff():
        return run_cmd(["git", "diff", "--check"])

    @staticmethod
    def diff_stat(base="origin/Aseprite-2.0"):
        return run_cmd(["git", "diff", f"{base}...HEAD", "--stat"])


# Mesh Messaging
def send_mesh_message(to, body, msg_type="result", reply_to=None, project=None):
    cfg = get_config()
    url = f"{cfg['hub_url']}/send"
    proj = project or cfg["primary_project"]

    payload = {
        "id": str(uuid.uuid4()),
        "project": proj,
        "from": cfg["agent"],
        "to": to,
        "type": msg_type,
        "body": body,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    }
    if reply_to:
        payload["reply_to"] = reply_to

    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers={
            "Authorization": f"Bearer {cfg['token']}",
            "Content-Type": "application/json",
            "User-Agent": f"AgentMeshCoordinator/{cfg['agent']}",
        },
        method="POST"
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        res = json.loads(resp.read().decode("utf-8"))
        return res


def fetch_inbox_messages(since="0"):
    cfg = get_config()
    projects_arg = ",".join(cfg["projects"])
    url = f"{cfg['hub_url']}/inbox?agent={urllib.parse.quote(cfg['agent'])}&projects={projects_arg}&since={since}"
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {cfg['token']}",
            "User-Agent": f"AgentMeshCoordinator/{cfg['agent']}",
        },
        method="GET"
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        res = json.loads(resp.read().decode("utf-8"))
        return res.get("messages", [])


def list_pending_muse_tasks():
    """Returns unhandled task/chat messages from muse."""
    processed = load_processed_ids()
    messages = fetch_inbox_messages(since="0")
    cfg = get_config()

    pending = []
    for msg in messages:
        msg_id = msg.get("id")
        sender = msg.get("from")
        recipient = msg.get("to")
        msg_type = (msg.get("type") or "").lower()

        if msg_id in processed:
            continue
        if sender != "muse":
            continue
        # Directed to us, arin-pc, or all
        if recipient in (cfg["agent"], "arin-pc", "all"):
            pending.append(msg)
    return pending


def cmd_status(args):
    cfg = get_config()
    branch = GitOps.current_branch()
    sha = GitOps.current_sha()
    clean = GitOps.is_clean()
    pending = list_pending_muse_tasks()

    print("\n" + "=" * 60)
    print(f" Mesh Git Coordinator Status — [{cfg['agent']}]")
    print("=" * 60)
    print(f" Repository Root: {REPO_ROOT}")
    print(f" Current Branch:  {branch}")
    print(f" Current Commit:  {sha[:10]} ({sha})")
    print(f" Working Tree:    {'CLEAN' if clean else 'DIRTY'}")
    if not clean:
        print(f" Changes:\n{GitOps.status()}")
    print("-" * 60)
    print(f" Hub Endpoint:    {cfg['hub_url']}")
    print(f" Projects:        {', '.join(cfg['projects'])}")
    print(f" Pending Muse Tasks: {len(pending)}")
    for p in pending:
        print(f"   • [{p.get('id')[:8]}] ({p.get('type')}) {p.get('body')[:70]}...")
    print("=" * 60 + "\n")


def cmd_list_tasks(args):
    pending = list_pending_muse_tasks()
    if not pending:
        print("No pending tasks from Muse.")
        return
    print(f"\nFound {len(pending)} pending task(s) from Muse:\n")
    for msg in pending:
        print(f"ID:        {msg.get('id')}")
        print(f"Timestamp: {msg.get('timestamp')}")
        print(f"To:        {msg.get('to')}")
        print(f"Type:      {msg.get('type')}")
        print(f"Body:\n{msg.get('body')}\n")
        print("-" * 50)


def cmd_sync_branch(args):
    branch = args.branch
    print(f"Fetching origin and syncing branch '{branch}'...")
    code, out, err = GitOps.checkout(branch)
    if code != 0:
        print(f"Failed to checkout {branch}: {err}", file=sys.stderr)
        sys.exit(code)
    print(f"Successfully on branch {GitOps.current_branch()} at commit {GitOps.current_sha()[:10]}")


def cmd_reply(args):
    res = send_mesh_message(
        to=args.to,
        body=args.body,
        msg_type=args.type or "result",
        reply_to=args.reply_to,
        project=args.project
    )
    if args.reply_to:
        mark_processed_id(args.reply_to)
    print(f"Reply sent successfully: {res}")


def main():
    parser = argparse.ArgumentParser(description="Mesh Git Coordinator for Aseprite 2.0")
    subparsers = parser.add_subparsers(dest="command")

    subparsers.add_parser("status", help="Show current git & coordinator status")
    subparsers.add_parser("tasks", help="List pending tasks from Muse")

    sync_parser = subparsers.add_parser("sync", help="Sync branch from origin")
    sync_parser.add_argument("--branch", required=True, help="Branch name to sync")

    reply_parser = subparsers.add_parser("reply", help="Send a reply to an agent")
    reply_parser.add_argument("--to", default="muse", help="Recipient agent")
    reply_parser.add_argument("--type", default="result", help="Message type (result, chat, task)")
    reply_parser.add_argument("--reply-to", required=True, help="Message ID replying to")
    reply_parser.add_argument("--body", required=True, help="Message body")
    reply_parser.add_argument("--project", default=None, help="Project channel")

    args = parser.parse_args()
    if not args.command or args.command == "status":
        cmd_status(args)
    elif args.command == "tasks":
        cmd_list_tasks(args)
    elif args.command == "sync":
        cmd_sync_branch(args)
    elif args.command == "reply":
        cmd_reply(args)


if __name__ == "__main__":
    main()
