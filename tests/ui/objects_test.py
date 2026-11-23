"""Easymetric objects in the studio (2026-11-17): focus, panels on the focused object's band,
painting only that object, focus switches never resize, timeline pick, per-object linked columns,
faded other objects, undo of New Object, the guided New object dialog.
Run: python tests/ui/objects_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
sys.path.insert(0, HERE)
from uitest import UiTest, UiTestError  # noqa: E402
import fields_test as F  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


SETUP = '''
local s = Sprite(48, 32)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
app.command.EasymetricStudio{ action = "blockLayout", width = 16, height = 16, depth = 16, r = 220, g = 30, b = 30 }
app.command.EasymetricStudio{ action = "modelSize", width = 16, height = 32, depth = 16 }
app.command.EasymetricStudio()
'''


def obj(st, key):
    return next((o for o in st["objects"] if o["key"] == key), None)


def studio_face(st, name):
    return next(f for f in st["studio"]["faces"] if f["name"] == name)


def paint(t, name, u0, u1, v):
    """Pencil stroke across pixels u0..u1, row v, of the FOCUSED object's view `name`."""
    st = t.state()
    fs = studio_face(st, name)
    scr, uv = fs["faceScreen"], fs["uv"]
    sx, sy = scr[2] / float(uv[2]), scr[3] / float(uv[3])
    y = scr[1] + (v + 0.5) * sy
    t.drag(scr[0] + (u0 + 0.5) * sx, y, scr[0] + (u1 + 0.5) * sx, y, steps=6)
    t.sleep(800)


def main():
    with UiTest(timeout=120) as t:
        t.lua(SETUP)
        t.sleep(1500)
        F.close_mini_preview(t)
        t.lua("app.activeTool = 'pencil'")
        root_h = t.state()["sprite"]["height"]

        # New object while the studio is open: it gets the focus, the panels show its band.
        t.lua("app.command.EasymetricStudio{ action = 'newObject', name = 'Head', block = true, "
              "bw = 8, bh = 8, bd = 8, place = 'above', r = 30, g = 30, b = 220 }")
        t.sleep(1500)
        st = t.state()
        check("new object is focused", st["studio"]["focusedObject"] == "Head", st["studio"].get("focusedObject"))
        uv = studio_face(st, "FRONT")["uv"]
        check("panels show the new object's band at the shared size",
              uv[1] >= root_h and uv[2:] == [16, 32], uv)
        hits = {h["key"]: h["px"] for h in st["studio"]["previewObjects"]}
        check("preview shows both objects", hits.get("", 0) > 0 and hits.get("Head", 0) > 0, hits)

        # Painting in a panel changes only the focused object.
        before = t.state()
        paint(t, "FRONT", 0, 7, 0)
        after = t.state()
        sheet = "base" if after["activeLayer"] == "Surface_Base" else "texture"
        check("painting changes the head (%s)" % sheet,
              obj(after, "Head")["faces"]["FRONT"][sheet]["hash"] != obj(before, "Head")["faces"]["FRONT"][sheet]["hash"],
              studio_face(after, "FRONT").get("faceScreen"))
        check("painting leaves Object 1 alone",
              all(obj(after, "")["faces"][n].get(k) == obj(before, "")["faces"][n].get(k)
                  for n in after["faces"] for k in ("base", "texture")))

        # Faded other objects: where the focused view is empty, the other objects in the same
        # place show through. An empty object centred inside the body shows the body everywhere.
        t.lua("app.command.EasymetricStudio{ action = 'newObject', name = 'Inner' }")
        t.sleep(1500)
        st = t.state()
        check("other objects are drawn faded in the panels", st["studio"]["otherObjectsPx"] > 0,
              st["studio"]["otherObjectsPx"])
        F.click_text(t, "Show others")
        t.sleep(1200)
        st = t.state()
        check("Show others off: nothing drawn", st["studio"]["otherObjectsPx"] == 0 and
              not st["studio"]["showOtherObjects"], st["studio"]["otherObjectsPx"])
        F.click_text(t, "Show others")
        t.sleep(1200)
        t.lua("app.command.EasymetricStudio{ action = 'deleteObject', object = 'Inner' }")
        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Head' }")
        t.sleep(1200)

        # Per-object linked columns: link Top/Bottom on the head only.
        links = sorted(t.find(cls="LinkToggleButton"), key=lambda w: w["rect"][0])
        if links:
            x, y, w, h = links[0]["rect"]
            t.click(x + w // 2, y + h // 2)
            t.sleep(800)
        check("head: Top/Bottom linked", t.state()["studio"]["colLinked"][0] is True, t.state()["studio"]["colLinked"])

        # Focus switches never resize anything.
        boxes = {o["key"]: o["box"] for o in t.state()["objects"]}
        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Object 1' }")
        t.sleep(1000)
        st = t.state()
        check("focus Object 1", st["studio"]["focusedObject"] == "", st["studio"]["focusedObject"])
        check("Object 1 columns are not linked", st["studio"]["colLinked"][0] is False, st["studio"]["colLinked"])
        check("panels back on Object 1's cells", studio_face(st, "FRONT")["uv"][1] == 0, studio_face(st, "FRONT")["uv"])
        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Head' }")
        t.sleep(1000)
        st = t.state()
        check("head keeps its linked column", st["studio"]["colLinked"][0] is True, st["studio"]["colLinked"])
        check("switching focus resized nothing", {o["key"]: o["box"] for o in st["objects"]} == boxes,
              ({o["key"]: o["box"] for o in st["objects"]}, boxes))

        # Picking Object 1's sheet in the timeline focuses Object 1.
        t.lua("for _, l in ipairs(app.activeSprite.layers) do if l.name == 'Surface_Base' then "
              "app.activeLayer = l end end")
        t.sleep(1000)
        check("timeline pick focuses Object 1", t.state()["studio"]["focusedObject"] == "",
              t.state()["studio"]["focusedObject"])
        t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Head' }")
        t.sleep(800)

        # Undo New Object with the studio open: back to Object 1, no crash. (Undo the paint first.)
        for _ in range(10):  # paint, Inner, delete Inner, link sync ... then New object
            if len(t.state()["objects"]) == 1:
                break
            t.key("z", mods="ctrl")
            t.sleep(500)
        t.sleep(1200)
        st = t.state()
        check("after undoing New Object the studio is on Object 1", st["studio"]["open"] and
              st["studio"]["focusedObject"] == "" and len(st["objects"]) == 1,
              (st["studio"].get("focusedObject"), len(st["objects"])))

        # Stretch the root's centred 16x16x16 pixels from the original full-size setup
        # by first making its current model match that block.
        t.lua("app.command.EasymetricStudio{ action = 'modelSize', width = 16, height = 16, depth = 16 }")
        t.sleep(800)
        F.click_field(t, 1)
        t.type("32")
        F.click_text(t, "Apply Model Size")
        t.wait_for(lambda: t.find(text="Stretch drawings"))
        t.click_widget(text="Stretch drawings")
        t.sleep(1200)
        st = t.state()
        check("dialog Stretch fills all six views",
              st["modelSize"] == [16, 32, 16] and
              all(f["base"]["opaque"] == f["uv"][2] * f["uv"][3] for f in st["faces"].values()),
              {n: (f["base"]["opaque"], f["uv"][2:]) for n, f in st["faces"].items()})

        # The dialog starts with only a name and an optional block.
        F.click_text(t, "New object...")
        t.wait_for(lambda: t.find(text="Create"))
        block = "Start with a block (flat depth)"
        check("dialog explains that size is shared",
              any("Uses the model size 16 x 32 x 16" in (w.get("text") or "") for w in t.find(cls="Label")))
        field_count = len(t.find(cls="NumberField"))
        check("dialog hides block dimensions by default", not t.find(text="Block W"))
        t.click_widget(text=block)
        check("checking the block option reveals three block dimensions",
              bool(t.find(text="Block W")) and len(t.find(cls="NumberField")) == field_count + 3)
        t.click_widget(text=block)
        t.click_widget(text="Create")
        t.sleep(1500)
        st = t.state()
        new = obj(st, st["studio"]["focusedObject"])
        check("dialog created and focused a new object",
              len(st["objects"]) == 2 and new is not None and new["key"] != "",
              (len(st["objects"]), st["studio"].get("focusedObject")))
        check("dialog default creates an empty object at model size",
              new is not None and new["box"] == st["modelSize"] and new["offset"] == [0, 0, 0] and
              all(f["base"]["opaque"] == 0 for f in new["faces"].values()))

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
