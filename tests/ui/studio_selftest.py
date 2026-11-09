"""Self-test of the in-app UI test server + Easymetric studio state JSON.

Run:  python tests/ui/studio_selftest.py          (from the repo root; build/bin/aseprite.exe)
Exit code 0 = all passed. Everything is asserted from structured output, never from pixels.
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError, rect_center  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


def face(state, name):
    return next(f for f in state["studio"]["faces"] if f["name"] == name)


def stats(state, name, sheet):
    return state["faces"][name][sheet]


def main():
    with UiTest(timeout=45) as t:
        t.dismiss_dialogs()
        # PixelLab (user extension) may open a panel; it is irrelevant to the studio.
        # T1 server alive, UI tree reachable
        ui = t.ui()
        check("T1 widget tree has the main window",
              any(w["cls"].endswith("MainWindow") for w in ui["windows"]))

        # T2 headless-style state works while the app is running
        out = t.lua("""
          local s = Sprite(72, 60)
          app.activeSprite = s
          app.command.EasymetricStudio{ action = "setupSurfaces" }
          print(#s.layers)
        """).strip()
        st = t.state()
        check("T2 state: sprite + six faces + sheets", st["sprite"]["width"] == 72 and
              len(st["faces"]) == 6 and st["hasSurfaces"], out)
        check("T2b face UV rects tile the sheet", st["faces"]["TOP"]["uv"] == [0, 0, 24, 30])

        # T3 open the studio via the command (the in-app equivalent of Alt+F7)
        t.lua("app.command.EasymetricStudio()")
        t.sleep(600)
        st = t.state()
        studio = st["studio"]
        check("T3 studio open", studio["open"] is True)
        check("T3b six editor rects, all non-empty",
              all(f.get("viewport") and f["viewport"][2] > 50 and f["viewport"][3] > 50
                  for f in studio["faces"]), [f.get("viewport") for f in studio["faces"]])
        check("T3c header buttons found by name (flipH, flipV, symX, symY, grid)",
              all({b["name"] for b in f["buttons"]} >= {"flipH", "flipV", "symX", "symY", "grid"}
                  for f in studio["faces"]))

        # T4 real mouse drag inside the Top panel paints pixels (first click only activates)
        top = face(st, "TOP")
        cx, cy = rect_center(top["faceScreen"])
        t.click(cx, cy)            # activate panel
        t.sleep(500)
        before = t.state()
        t.drag(cx - 12, cy - 6, cx + 12, cy + 6, steps=10)
        t.sleep(400)
        after = t.state()
        changed = [k for k in ("base", "texture", "depth")
                   if k in after["faces"]["TOP"] and
                   after["faces"]["TOP"][k]["hash"] != before["faces"]["TOP"].get(k, {}).get("hash")]
        check("T4 drag in Top panel changes a Top sheet", bool(changed), changed)
        others_same = all(after["faces"][n].get(k, {}).get("hash") == before["faces"][n].get(k, {}).get("hash")
                          for n in ("DOWN", "FRONT", "BACK", "RIGHT", "LEFT")
                          for k in ("base", "texture", "depth"))
        check("T4b the other faces are untouched", others_same)

        # T5 undo through the real keyboard shortcut
        t.key("z", mods="ctrl")
        t.sleep(500)
        undone = t.state()
        check("T5 Ctrl+Z restores the Top sheet",
              all(undone["faces"]["TOP"].get(k, {}).get("hash") == before["faces"]["TOP"].get(k, {}).get("hash")
                  for k in ("base", "texture", "depth")))

        # T6 button by name: press Grid via its bounds, popup window appears, Cancel closes it
        gridb = next(b for b in top["buttons"] if b["name"] == "grid")
        t.click(*rect_center(gridb["bounds"]))
        t.sleep(500)
        popups = t.find(text="Confirm")
        check("T6 Grid button opens its pop-up (Confirm button visible)", len(popups) >= 1)
        if popups:
            cancel = t.find(text="Cancel")
            t.click(*rect_center(cancel[-1]["rect"]))
            t.sleep(400)
        check("T6b pop-up closed", len(t.find(text="Confirm")) == 0)

        # T7 splitter drag between column 0 and 1 widens column 0 and the cursor shows a resize arrow
        st = t.state()["studio"]
        col0 = st["columnsPx"][0]
        hdr = face(t.state(), "TOP")["header"]
        bx = hdr[0] + hdr[2] + 3          # the gap between column 0 and column 1
        by = hdr[1] + 60
        cur = t.move(bx, by)["cursor"]
        check("T7 cursor over a panel border is a resize cursor", cur.startswith("size"), cur)
        t.drag(bx, by, bx + 20, by, steps=6)
        t.sleep(500)
        col0b = t.state()["studio"]["columnsPx"][0]
        check("T7b dragging the border widens column 0", col0b > col0, (col0, col0b))

        # T8 screenshot of the app window
        shot = os.path.join(t.workdir, "shot.png")
        r = t.screenshot(shot)
        check("T8 screenshot written", os.path.exists(shot) and os.path.getsize(shot) > 10000, r)

        # T9 exit studio through the command; state says closed
        t.lua("app.command.EasymetricStudio()")
        t.sleep(600)
        check("T9 studio closed", t.state()["studio"]["open"] is False)

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
