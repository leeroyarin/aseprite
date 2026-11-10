"""UI checks of the ghost hints toggle and the Fields cleanup (no per-face XY/YZ size row).
Run: python tests/ui/ghost_test.py"""
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
for _, l in ipairs(s.layers) do
  if l.name == "Surface_Base" then
    local img = l:cel(1).image
    for y = 10, 14 do for x = 29, 33 do img:drawPixel(x, y, Color{ r = 255, g = 255, b = 255 }) end end
  end
end
app.command.EasymetricStudio()
'''


def face(st, name):
    return next(f for f in st["faces"] if f["name"] == name) if "faces" in st else None


def main():
    with UiTest(timeout=45) as t:
        t.lua(SETUP)
        t.sleep(900)
        st = t.state()
        check("G1 ghost hints are on by default", st["studio"]["ghostOn"] is True)
        top = st["faces"]["TOP"]["ghost"]
        check("G2 the state reports the missing Top columns", top["cols"] == [5, 6, 7, 8, 9], top)

        shot = os.path.join(t.workdir, "ghost_on.png")
        t.screenshot(shot)
        check("G3 screenshot with ghosts written", os.path.getsize(shot) > 10000)

        btn = t.find(text="Ghost")
        check("G4 the Preview header has a Ghost toggle", len(btn) >= 1)
        t.click(*rect_center(btn[0]["rect"]))
        t.sleep(500)
        st = t.state()
        check("G5 clicking it turns the hints off", st["studio"]["ghostOn"] is False, st["studio"]["ghostOn"])
        chk = t.find(text="Ghost hints")
        check("G6 the Fields checkbox follows", len(chk) >= 1 and not chk[0]["selected"], chk)
        # (A fresh profile shows Aseprite's own mini Preview window over the Fields pane, so the
        # checkbox cannot be clicked there; flip the header toggle again and check the checkbox follows.)
        t.click(*rect_center(btn[0]["rect"]))
        t.sleep(500)
        chk = t.find(text="Ghost hints")
        check("G7 the header toggle turns them back on and the checkbox follows",
              t.state()["studio"]["ghostOn"] is True and chk[0]["selected"])

        check("G8 the per-face 'Apply Size' (XY / YZ resizing) row is gone", len(t.find(text="Apply Size")) == 0)
        check("G9 the three-dimension 'Apply Object Size' remains", len(t.find(text="Apply Object Size")) == 1)

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
