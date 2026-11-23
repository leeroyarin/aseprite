"""Delete object (Arin, 2026-11-18): "I want to remove the object, is there an option? and make
sure to fall back to previous one". The Fields "Delete object" button asks first, removes the
focused object, and the studio lands on the object before it in the list (Object 1 when it was
the first). Undo brings it back. Deleting by script, or a vanished group, falls back the same way.
Run: python tests/ui/delete_object_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError  # noqa: E402
import fields_test as F  # noqa: E402  (reveal / click_text / close_mini_preview)

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


SETUP = '''
local s = Sprite(72, 60)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
app.command.EasymetricStudio{ action = "blockLayout", width = 16, height = 16, depth = 16 }
app.command.EasymetricStudio()
'''


def focus(t):
    return t.state()["studio"]["focusedObject"]


def keys(t):
    return [o["key"] for o in t.state()["objects"]]


def press_delete(t, answer):
    F.click_text(t, "Delete object")
    t.sleep(600)
    shown = bool(t.find(text=answer))
    if shown:
        t.click_widget(text=answer)
        t.sleep(1000)
    return shown


def main():
    with UiTest(timeout=60) as t:
        t.lua(SETUP)
        t.sleep(1200)
        F.close_mini_preview(t)
        btn = t.find(text="Delete object")
        check("Fields has a Delete object button", len(btn) == 1, btn)
        check("it is disabled on Object 1 (which cannot be deleted)",
              btn and btn[0].get("enabled") is False, btn)
        for n in ("Arm", "Leg", "Hand"):
            t.lua("app.command.EasymetricStudio{ action = 'newObject', name = '%s', block = true, "
                  "bw = 4, bh = 4, bd = 4 }" % n)
            t.sleep(600)
        check("three objects made", keys(t) == ["", "Arm", "Leg", "Hand"], keys(t))
        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Leg' }")
        t.sleep(600)

        shown = press_delete(t, "Cancel")
        check("Delete asks first; Cancel keeps the object",
              shown and keys(t) == ["", "Arm", "Leg", "Hand"] and focus(t) == "Leg", (keys(t), focus(t)))

        shown = press_delete(t, "Delete")
        check("Delete removes Leg and falls back to the previous object (Arm)",
              shown and keys(t) == ["", "Arm", "Hand"] and focus(t) == "Arm", (keys(t), focus(t)))

        shown = press_delete(t, "Delete")
        check("deleting the first object falls back to Object 1",
              shown and keys(t) == ["", "Hand"] and focus(t) == "", (keys(t), focus(t)))

        t.key("z", mods="ctrl")
        t.sleep(1000)
        check("undo brings the deleted object back", keys(t) == ["", "Arm", "Hand"], keys(t))

        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Hand' }")
        t.sleep(600)
        t.lua("app.command.EasymetricStudio{ action = 'deleteObject', object = 'Hand' }")
        t.sleep(800)
        check("a script deleting the focused object also falls back to the previous one",
              keys(t) == ["", "Arm"] and focus(t) == "Arm", (keys(t), focus(t)))

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
