"""Fields panel cleanup (2026-11-17): removed duplicate rows, typed W/H/D numbers that accept 1..3,
click-selects-all, values kept in sync, Easy Start, Start from block resets depth.
Run: python tests/ui/fields_test.py"""
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
s:newEmptyFrame(2)
s:newEmptyFrame(3)
app.command.EasymetricStudio()
'''


def size_fields(t):
    """The three W/H/D NumberFields in the Model size row, left to right."""
    label = t.find(text="Model size  W")
    if not label:
        return []
    ly = label[0]["rect"][1] + label[0]["rect"][3] // 2
    fields = [w for w in t.find(cls="NumberField") if w["rect"][1] <= ly <= w["rect"][1] + w["rect"][3]]
    return sorted(fields, key=lambda w: w["rect"][0])


def close_mini_preview(t):
    """A fresh profile opens Aseprite's own mini Preview window over the Fields pane."""
    for w in t.find(cls="PreviewEditorWindow"):
        t.lua("app.command.TogglePreview()")
        t.sleep(300)
        return


def reveal(t, rect):
    """Scroll the Fields pane until `rect` is inside its viewport; returns the fresh rect via the caller."""
    vp = t.state()["studio"].get("fieldsViewport")
    if not vp:
        return
    vx, vy, vw, vh = vp
    for _ in range(30):
        x, y, w, h = rect()
        if y >= vy and y + h <= vy + vh:
            return
        # Left edge (labels): sliders / combo boxes in the middle take the wheel themselves.
        t.wheel(vx + 3, vy + vh // 2, dy=1 if y + h > vy + vh else -1)
        t.sleep(80)


def click_field(t, i):
    reveal(t, lambda: size_fields(t)[i]["rect"])
    x, y, w, h = size_fields(t)[i]["rect"]
    t.click(x + w // 2, y + h // 2)
    t.sleep(150)


def click_text(t, text):
    reveal(t, lambda: t.find(text=text)[0]["rect"])
    t.click_widget(text=text)


def face_size(t, name):
    uv = t.state()["faces"][name]["uv"]
    return int(uv[2]), int(uv[3])


def main():
    with UiTest(timeout=60) as t:
        t.lua(SETUP)
        t.sleep(1200)
        close_mini_preview(t)
        for gone in ("Sym X", "Sym Y", "Center Sym", "Lock Borders", "Link Dims", "Pop out Column",
                     "Ghost hints", "At X", "Move"):
            check("removed: %s" % gone, len(t.find(text=gone)) == 0)
        for kept in ("Stroke Outline", "Cutouts", "Model size  W", "Apply Model Size", "Pop out"):
            check("kept: %s" % kept, len(t.find(text=kept)) >= 1)

        fields = size_fields(t)
        check("three W/H/D number fields", len(fields) == 3, fields)
        check("fields show the model size (24 x 30 x 24)", [f["text"] for f in fields] == ["24", "30", "24"],
              [f["text"] for f in fields])

        # Typing into W through the Lua-free path: focus, then a plain click selects the whole value.
        click_field(t, 0)
        t.type("3")          # must replace "24", not append
        t.sleep(150)
        txt = size_fields(t)[0]["text"]
        check("a plain click selects the value, typing 3 replaces it (not 4, not 243)", txt == "3", txt)
        t.type("2")
        txt = size_fields(t)[0]["text"]
        check("typing 3 then 2 gives 32 (no clamp to 4 while typing)", txt == "32", txt)

        # Apply with a size below the old minimum of 4.
        for i, val in enumerate(("3", "5", "2")):
            click_field(t, i)
            t.type(val)
        click_text(t, "Apply Model Size")
        t.wait_for(lambda: t.find(text="Stretch drawings"))
        check("resize alert offers Stretch, Keep and Cancel",
              all(t.find(text=choice) for choice in ("Stretch drawings", "Keep pixels", "Cancel")))
        t.click_widget(text="Cancel")
        t.sleep(300)
        check("Cancel leaves the model size intact", face_size(t, "FRONT") == (24, 30))
        for i, val in enumerate(("3", "5", "2")):
            click_field(t, i)
            t.type(val)
        frame = t.state()["sprite"]["frame"]
        click_field(t, 0)
        t.key("enter")
        t.wait_for(lambda: t.find(text="Keep pixels"))
        t.click_widget(text="Keep pixels")
        t.sleep(1200)
        check("Enter applies without changing the frame", t.state()["sprite"]["frame"] == frame,
              t.state()["sprite"]["frame"])
        check("Apply makes Front 3 x 5", face_size(t, "FRONT") == (3, 5), face_size(t, "FRONT"))
        check("Apply makes Right 2 x 5", face_size(t, "RIGHT") == (2, 5), face_size(t, "RIGHT"))
        check("Apply makes Top 3 x 2", face_size(t, "TOP") == (3, 2), face_size(t, "TOP"))

        # Undo restores the size, and the fields follow (they used to keep stale values).
        t.key("z", mods="ctrl")
        t.sleep(1200)
        txt = [f["text"] for f in size_fields(t)]
        check("after undo the fields follow the real size", txt == ["24", "30", "24"], txt)

        # Easy Start: one centred pixel per face.
        click_text(t, "Easy Start (1 pixel)")
        t.sleep(1200)
        st = t.state()
        opaque = {n: st["faces"][n]["base"]["opaque"] for n in st["faces"]}
        check("Easy Start leaves exactly one pixel on every face", all(v == 1 for v in opaque.values()), opaque)

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
