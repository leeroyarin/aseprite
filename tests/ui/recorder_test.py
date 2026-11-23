"""Session recorder: real (injected) input, commands, edits, zoom, a marked problem and screenshots end up
in <user folder>/usage/<session>/events.jsonl + shots/*.png; turning it off stops recording.
Run: python tests/ui/recorder_test.py"""
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


def events(t):
    files = glob.glob(os.path.join(t.user_dir, "usage", "*", "events.jsonl"))
    if not files:
        return [], None
    path = max(files, key=os.path.getmtime)
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()], os.path.dirname(path)


def editor_rect(t):
    m = [w for w in t.find(cls="app::Editor")]
    return max(m, key=lambda w: w["rect"][2] * w["rect"][3])["rect"]


def main():
    # The harness normally disables the recorder; ASEPRITE_RECORD=1 turns it back on.
    with UiTest(timeout=60, env={"ASEPRITE_RECORD": "1"}) as t:
        t.lua("local s = Sprite(64, 64) app.activeSprite = s app.fgColor = Color{ r = 255 } app.tool = 'pencil'")
        t.sleep(600)
        x, y, w, h = editor_rect(t)
        cx, cy = x + w // 2, y + h // 2
        t.drag(cx - 30, cy, cx + 30, cy, steps=8)            # a stroke
        t.sleep(300)
        t.key("b")                                            # a key (pencil shortcut)
        t.wheel(cx, cy, dy=-1)                                # zoom in
        t.sleep(300)
        t.lua("app.command.Flip{ target = \"canvas\", orientation = \"horizontal\" }")                 # a command
        t.sleep(900)                                          # let the throttled screenshot fire

        # Mark a problem through the real shortcut and dialog.
        t.key("m", mods="ctrl+alt")
        t.sleep(500)
        t.type("ghost was wrong")
        t.click_widget(text="Save")
        t.sleep(900)

        ev, session = events(t)
        types = [e.get("type") for e in ev]
        check("a session folder with events.jsonl exists", session is not None, t.user_dir)
        check("session_start is the first line", types[:1] == ["session_start"], types[:3])
        downs = [e for e in ev if e.get("type") == "mouse_down"]
        check("mouse_down recorded with the editor's sprite coordinates",
              any("editor" in e and "sprite" in e["editor"] for e in downs), downs[:1])
        check("drag samples recorded while the button is held", "drag" in types, types)
        check("the stroke's edit is recorded", any(e.get("type") == "edit" for e in ev), types)
        check("the key is recorded", any(e.get("type") == "key" and e.get("key") == "B" for e in ev),
              [e for e in ev if e.get("type") == "key"][:3])
        check("the wheel is recorded", "wheel" in types, types)
        check("the zoom change is recorded as a view event", "view" in types, types)
        check("the command is recorded with its id",
              any(e.get("type") == "command" and e.get("id") == "Flip" for e in ev),
              [e for e in ev if e.get("type") == "command"])
        notes = [e for e in ev if e.get("type") == "note"]
        check("the marked problem is recorded with its text and context",
              notes and notes[-1].get("text") == "ghost was wrong" and "context" in notes[-1], notes)
        shots = [e for e in ev if e.get("type") == "shot"]
        pngs = glob.glob(os.path.join(session or "", "shots", "*.png"))
        check("screenshots are written as PNG files",
              shots and pngs and all(open(p, "rb").read(8) == b"\x89PNG\r\n\x1a\n" for p in pngs),
              (len(shots), len(pngs)))
        check("a screenshot was taken for the note", any(s.get("reason") == "note" for s in shots), shots)
        times = [e.get("t", 0) for e in ev]
        check("timestamps never go backwards", times == sorted(times))

        # Turning it off stops recording.
        t.lua("app.command.ToggleSessionRecorder()")
        t.sleep(300)
        n_off = len(events(t)[0])
        t.drag(cx - 20, cy + 10, cx + 20, cy + 10, steps=5)
        t.key("b")
        t.sleep(900)
        ev2, _ = events(t)
        check("turning it off writes recorder_off", ev2 and ev2[n_off - 1].get("type") == "recorder_off",
              ev2[n_off - 2:n_off] if ev2 else None)
        check("nothing is recorded while it is off", len(ev2) == n_off, ev2[n_off:])

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
