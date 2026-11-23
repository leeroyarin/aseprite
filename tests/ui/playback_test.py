"""Playback in the studio (Arin, 2026-11-18): "it ended up creating an animation for single
surface only, which was the top, and the animation isn't stopping". All six panels must play the
same frame, studio actions (switching / creating objects) stop playback, and Enter is always a
plain Play/Stop afterwards.
Run: python tests/ui/playback_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError  # noqa: E402
import fields_test as F  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


SETUP = '''
local s = Sprite(72, 60)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
app.command.EasymetricStudio{ action = "blockLayout", width = 16, height = 16, depth = 16 }
s:newFrame(); s:newFrame(); s:newFrame()
app.frame = s.frames[1]
app.command.EasymetricStudio{ action = "newObject", name = "Head", block = true, bw = 4, bh = 4, bd = 4 }
app.command.EasymetricStudio()
'''


def panels(t):
    return t.state()["studio"]["faces"]


def frames(t):
    return [f.get("frame") for f in panels(t)]


def playing(t):
    return [f["name"] for f in panels(t) if f.get("playing")]


def main():
    with UiTest(timeout=60) as t:
        t.lua(SETUP)
        t.sleep(1200)
        F.close_mini_preview(t)

        t.key("enter")
        same = True
        seen = set()
        for _ in range(6):
            t.sleep(130)
            fr = frames(t)
            seen.update(fr)
            same = same and len(set(fr)) == 1
        check("Enter plays: every panel shows the same frame while playing", same and len(seen) > 1,
              (seen, frames(t)))
        check("exactly one panel drives playback", len(playing(t)) == 1, playing(t))

        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Object 1' }")
        t.sleep(500)
        fr1 = frames(t)
        t.sleep(400)
        check("switching object stops playback", not playing(t) and frames(t) == fr1, (playing(t), fr1, frames(t)))

        t.key("enter")
        t.sleep(500)
        check("Enter starts playback again", len(playing(t)) == 1, playing(t))
        t.key("enter")
        t.sleep(400)
        fr2 = frames(t)
        t.sleep(400)
        check("Enter stops it", not playing(t) and frames(t) == fr2, (playing(t), fr2, frames(t)))

        t.key("enter")
        t.sleep(400)
        t.lua("app.command.EasymetricStudio{ action = 'newObject', name = 'Arm', block = true, bw = 4, bh = 4, bd = 4 }")
        t.sleep(600)
        fr3 = frames(t)
        t.sleep(400)
        check("New object stops playback", not playing(t) and frames(t) == fr3 and len(set(fr3)) == 1,
              (playing(t), fr3, frames(t)))

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
