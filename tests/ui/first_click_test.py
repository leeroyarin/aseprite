"""Quality: a stroke in a panel that is NOT the active one must draw immediately (no 'first click only
activates the panel' dead click), and must draw only inside that panel's face.
Run: python tests/ui/first_click_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError, rect_center  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


SETUP = '''
local s = Sprite(72, 60)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
app.fgColor = Color{ r = 255, g = 0, b = 0 }
app.tool = "pencil"
app.command.EasymetricStudio()
'''


def face(st, name):
    return next(f for f in st["studio"]["faces"] if f["name"] == name)


def total(st, name):
    f = st["faces"][name]
    return sum(f[k]["opaque"] for k in ("base", "texture", "depth") if k in f)


def main():
    with UiTest(timeout=45) as t:
        t.lua(SETUP)
        t.sleep(900)
        st = t.state()
        for name in ("FRONT", "BACK", "LEFT"):
            f = face(st, name)
            fs = f["faceScreen"]
            before = t.state()
            cx, cy = rect_center(fs)
            t.drag(cx - 10, cy, cx + 10, cy, steps=8)       # the very first interaction with this panel
            t.sleep(500)
            after = t.state()
            check("first stroke in %s draws" % name, total(after, name) > total(before, name),
                  (total(before, name), total(after, name)))
            others = [n for n in ("TOP", "DOWN", "FRONT", "BACK", "RIGHT", "LEFT") if n != name]
            check("... and only in %s" % name,
                  all(total(after, n) == total(before, n) for n in others))

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
