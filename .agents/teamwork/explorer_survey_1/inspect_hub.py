import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path("tools/agent-mesh").resolve()))
import mesh_subagent

def main():
    cfg = mesh_subagent.Config.load()
    client = mesh_subagent.MeshClient(cfg)
    messages = client.fetch_inbox(since="0", limit=100)
    
    out_path = Path(".agents/teamwork/explorer_survey_1/all_messages.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(messages, f, indent=2)
    print(f"Saved {len(messages)} messages to {out_path}")

if __name__ == "__main__":
    main()
