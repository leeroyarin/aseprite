"""Panel viewport: a panel keeps its zoom and scroll through selection/move/drop and the Zoom command.
Sprite 192x256 -> faces 64x128 (the user's Body Model layout).
Run: python tests/ui/viewport_test.py"""
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
local s = Sprite(192, 256)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
app.fgColor = Color{ r = 255, g = 0, b = 0 }
app.tool = "pencil"
app.command.EasymetricStudio()
'''


def face(st, name):
    return next(f for f in st["studio"]["faces"] if f["name"] == name)


def main():
    with UiTest(timeout=60) as t:
        t.lua(SETUP)
        t.sleep(1200)
        f0 = face(t.state(), "FRONT")
        cx, cy = rect_center(f0["faceScreen"])

        # Wheel zoom in once, then remember the state.
        t.wheel(cx, cy, dy=-1)
        t.sleep(400)
        f1 = face(t.state(), "FRONT")
        check("wheel zoom changes zoom", f1["zoom"] != f0["zoom"], (f0["zoom"], f1["zoom"]))

        # Draw, then press M (marquee), drag inside the face (moves the selection), drop with Enter.
        t.drag(cx - 10, cy, cx + 10, cy, steps=6)
        t.sleep(300)
        before = face(t.state(), "FRONT")
        t.drag(cx - 6, cy - 6, cx + 6, cy + 6, steps=6)
        t.sleep(500)
        during = face(t.state(), "FRONT")
        check("zoom kept while moving a selection", abs(during["zoom"] - before["zoom"]) < 1e-9,
              (before["zoom"], during["zoom"]))
        check("view kept while moving a selection",
              during["faceScreen"][2:] == before["faceScreen"][2:], (before["faceScreen"], during["faceScreen"]))
        t.key("Enter")
        t.sleep(500)
        after = face(t.state(), "FRONT")
        check("zoom kept after dropping", abs(after["zoom"] - before["zoom"]) < 1e-9,
              (before["zoom"], after["zoom"]))

        # Zoom command: Ctrl+= zooms in, Ctrl+- zooms out, the zoom really changes.
        t.move(cx, cy)
        t.key("=", mods="ctrl")
        t.sleep(400)
        zin = face(t.state(), "FRONT")
        check("Ctrl+= zooms in", zin["zoom"] > after["zoom"], (after["zoom"], zin["zoom"]))
        t.key("-", mods="ctrl")
        t.sleep(400)
        zout = face(t.state(), "FRONT")
        check("Ctrl+- zooms out", zout["zoom"] < zin["zoom"], (zin["zoom"], zout["zoom"]))

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
