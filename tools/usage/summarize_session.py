"""Readable timeline of an Aseprite 2.0 session recording (docs/aseprite2/RECORDER.md).

  python tools/usage/summarize_session.py                 # latest session
  python tools/usage/summarize_session.py --list          # all sessions with size / event count
  python tools/usage/summarize_session.py <session dir>   # a specific one
  python tools/usage/summarize_session.py --notes         # only marked problems, with what led up to them

Default root: %APPDATA%/Aseprite2/usage (override with --root or ASEPRITE_USER_FOLDER).
"""
import argparse
import json
import os
import sys


def default_root():
    user = os.environ.get("ASEPRITE_USER_FOLDER")
    if user:
        return os.path.join(user, "usage")
    return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "Aseprite2", "usage")


def sessions(root):
    if not os.path.isdir(root):
        return []
    return sorted(os.path.join(root, d) for d in os.listdir(root)
                  if os.path.isfile(os.path.join(root, d, "events.jsonl")))


def load(session):
    out = []
    with open(os.path.join(session, "events.jsonl"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass  # a line cut off by a crash
    return out


def where(e):
    parts = []
    ed = e.get("editor")
    if ed:
        face = ed.get("face")
        sp = ed.get("sprite")
        parts.append("%s at (%d,%d) zoom %g" % (face or "canvas", sp[0], sp[1], ed.get("zoom", 1)))
    elif e.get("widget"):
        parts.append(e["widget"])
    return " on " + ", ".join(parts) if parts else ""


def describe(e):
    t = e.get("type")
    mods = "+".join(e.get("mods", []))
    mods = mods + "+" if mods else ""
    if t == "session_start":
        return "SESSION START %s %s screen %s" % (e.get("app"), e.get("version"), e.get("screen"))
    if t == "session_end":
        return "SESSION END"
    if t in ("mouse_down", "mouse_up", "double_click"):
        verb = {"mouse_down": "press", "mouse_up": "release", "double_click": "double-click"}[t]
        return "%s%s %s%s" % (mods, e.get("button"), verb, where(e))
    if t == "drag":
        return "  drag%s" % where(e)
    if t == "wheel":
        return "%swheel %s%s" % (mods, e.get("delta"), where(e))
    if t == "key":
        foc = " (in %s)" % e["focus"] if e.get("focus") else ""
        return "key %s%s" % (e.get("key"), foc)
    if t == "command":
        p = " %s" % json.dumps(e["params"]) if e.get("params") else ""
        return "COMMAND %s (%s)%s" % (e.get("name"), e.get("id"), p)
    if t == "edit":
        return "edit: %s" % e.get("label")
    if t == "tool":
        return "tool -> %s" % e.get("tool")
    if t == "view":
        return "view %szoom %g scroll %s" % ((e["face"] + " ") if e.get("face") else "", e.get("zoom"), e.get("scroll"))
    if t == "site":
        c = e.get("context", {})
        return "now on %s layer %s frame %s" % (os.path.basename(c.get("file", "") or "?"), c.get("layer"), c.get("frame"))
    if t == "shot":
        return "  [screenshot %s, after %s]" % (e.get("file"), e.get("reason"))
    if t == "note":
        return ">>> PROBLEM MARKED: %r  context %s" % (e.get("text"), json.dumps(e.get("context", {})))
    if t in ("recorder_on", "recorder_off"):
        return t.replace("_", " ").upper()
    if t == "magnify":
        return "pinch %g" % e.get("amount", 0)
    return json.dumps(e)


def stamp(e):
    ms = e.get("t", 0)
    return "%02d:%02d.%03d %s" % (ms // 60000, (ms // 1000) % 60, ms % 1000, e.get("time", ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session", nargs="?")
    ap.add_argument("--root", default=default_root())
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--notes", action="store_true", help="only marked problems and the 25 events before each")
    ap.add_argument("--no-drag", action="store_true", help="hide drag samples")
    a = ap.parse_args()

    all_sessions = sessions(a.root)
    if a.list:
        for s in all_sessions:
            size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(s) for f in fs)
            ev = load(s)
            notes = sum(1 for e in ev if e.get("type") == "note")
            print("%s  %6.1f MB  %5d events  %d notes" % (os.path.basename(s), size / 1e6, len(ev), notes))
        return 0
    session = a.session or (all_sessions[-1] if all_sessions else None)
    if not session:
        print("no recordings under", a.root)
        return 1
    ev = load(session)
    print("# %s (%d events)" % (session, len(ev)))
    keep = range(len(ev))
    if a.notes:
        idx = set()
        for i, e in enumerate(ev):
            if e.get("type") == "note":
                idx.update(range(max(0, i - 25), i + 2))
        keep = sorted(idx)
    last = None
    typing = None  # [first event, text, focus] while consecutive printable keys go to one field
    def flush():
        if typing:
            print("%s  typed %r (in %s)" % (stamp(typing[0]), typing[1], typing[2]))
    for i in keep:
        e = ev[i]
        if a.no_drag and e.get("type") == "drag":
            last = i
            continue
        if last is not None and i != last + 1:
            flush(); typing = None
            print("   ...")
        last = i
        if e.get("type") == "key" and e.get("char") and not e.get("mods") and "+" not in e.get("key", "+"[:0]):
            if typing and typing[2] == e.get("focus"):
                typing[1] += e["char"]
            else:
                flush()
                typing = [e, e["char"], e.get("focus")]
            continue
        if e.get("type") == "shot" and typing:
            continue  # screenshots taken while typing add nothing
        flush(); typing = None
        print("%s  %s" % (stamp(e), describe(e)))
    flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
