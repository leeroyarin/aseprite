import os
import sys
from pathlib import Path

# Set up paths and environment
script_dir = Path(__file__).resolve().parent
token_file = script_dir / ".agent_token_arin_pc"

if not token_file.exists():
    print(f"[Error] Token file not found: {token_file}", file=sys.stderr)
    sys.exit(1)

token = token_file.read_text(encoding="utf-8").strip()
if not token:
    print("[Error] Token file is empty", file=sys.stderr)
    sys.exit(1)

os.environ["AGENT_NAME"] = "arin-pc"
os.environ["AGENT_TOKEN"] = token
os.environ["PROJECTS"] = "agent-hub,aseprite2"

sys.path.insert(0, str(script_dir))
import mesh_client

if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else "listen"
    if action == "listen":
        sys.argv = ["mesh_client.py", "listen"]
        mesh_client.main()
    elif action == "status":
        sys.argv = ["mesh_client.py", "status"]
        mesh_client.main()
    else:
        sys.argv = ["mesh_client.py"] + sys.argv[1:]
        mesh_client.main()
