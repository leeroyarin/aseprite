"""UI tests of the Easymetric depth tools (toolbar tools + panel strokes), asserted from
structured output only. Run: python tests/ui/depth_tools_test.py   (exit 0 = all passed)."""
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


SETUP = '''
local s = Sprite(72, 60)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
for _, l in ipairs(s.layers) do
  if l.name == "Surface_Base" then l:cel(1).image:clear(Color{ r = 255, g = 255, b = 255, a = 255 }) end
end
app.command.EasymetricStudio{ action = "generateDepthFromBase" }
app.fgColor = Color{ r = 200, g = 200, b = 200 }
app.bgColor = Color{ r = 40, g = 40, b = 40 }
'''


def depth_at(t, x, y):
    out = t.lua('''
      local s = app.activeSprite
      for _, l in ipairs(s.layers) do if l.name == "Surface_DEPTH" then
        print(app.pixelColor.rgbaR(l:cel(1).image:getPixel(%d, %d))) end end''' % (x, y))
    return int(out.strip())


def face(st, name):
    return next(f for f in st["studio"]["faces"] if f["name"] == name)


def face_px(st, name, u, v):
    f = face(st, name)
    fs, zoom = f["faceScreen"], f["zoom"]
    return (int(fs[0] + (u + 0.5) * zoom), int(fs[1] + (v + 0.5) * zoom))


def tool_rect(st, tool_id):
    for tr in st["toolbar"]["tools"]:
        if tr["id"] == tool_id:
            return tr["rect"]
    return None


def main():
    with UiTest(timeout=45) as t:
        t.lua(SETUP)
        st = t.state()
        check("T0 toolbar has no depth tools while the studio is closed",
              tool_rect(st, "depth_brush") is None and st["toolbar"]["studioGroupVisible"] is False)
        t.lua("app.command.EasymetricStudio()")
        t.sleep(700)
        st = t.state()
        check("T1 opening the studio shows the depth tool group", st["toolbar"]["studioGroupVisible"] is True
              and tool_rect(st, "depth_brush") is not None, st["toolbar"])

        # T2 pick the depth brush by clicking its toolbar button, like a human
        t.click(*rect_center(tool_rect(st, "depth_brush")))
        t.sleep(500)
        st = t.state()
        check("T2 clicking the toolbar button selects the Depth Brush",
              st["tool"] == "depth_brush" and st["studio"]["depthTool"] == "brush", (st["tool"], st["studio"].get("depthTool")))

        # activate the TOP panel (the first click in a panel only activates it)
        top_c = face_px(st, "TOP", 12, 15)
        t.click(*top_c)
        t.sleep(500)
        far0 = depth_at(t, 1, 1)
        d0 = depth_at(t, 12, 15)
        t.click(*top_c)
        t.sleep(400)
        d1 = depth_at(t, 12, 15)
        D = 15        # FRONT face height 30 / 2
        step = round(128.0 / D)
        check("T3 left click with the Depth Brush raises the surface by one step", d1 == d0 + step, (d0, d1, step))
        check("T3b the rest of the sheet is untouched far from the brush", depth_at(t, 1, 1) == far0,
              (far0, depth_at(t, 1, 1)))

        t.click(*top_c, button="right")
        t.sleep(400)
        d2 = depth_at(t, 12, 15)
        check("T4 right click lowers it by one step", d2 == d1 - step, (d1, d2))

        # T5 a drag changes each pixel at most once per stroke
        a, b = face_px(st, "TOP", 4, 24), face_px(st, "TOP", 20, 24)
        before = depth_at(t, 12, 24)
        t.drag(a[0], a[1], b[0], b[1], steps=30)
        t.sleep(400)
        after = depth_at(t, 12, 24)
        check("T5 one drag stroke = exactly one step per pixel", after == before + step, (before, after))

        # T6 one Ctrl+Z undoes the whole stroke
        t.key("z", mods="ctrl")
        t.sleep(400)
        check("T6 one undo reverts the whole drag", depth_at(t, 12, 24) == before, depth_at(t, 12, 24))

        # T7 pencil via the toolbar strip (press on the group, slide, release)
        st = t.state()
        grp = tool_rect(st, "depth_brush")
        gx, gy = rect_center(grp)
        icon_w = grp[2]
        # The strip pops out beside the toolbar: [brush][pencil][fill]. Slide left across
        # it (it opens to the left when the toolbar is docked on the right) until the tool is
        # the pencil, exactly what a hand would do.
        st = t.state()
        for k in (2, 1, 3):
            t.call("mouse_down", x=gx, y=gy)
            t.sleep(300)
            t.call("mouse_move", x=gx - k * (icon_w - 2), y=gy)
            t.call("mouse_move", x=gx - k * (icon_w - 2) - 1, y=gy)
            t.sleep(250)
            t.call("mouse_up", x=gx - k * (icon_w - 2) - 1, y=gy)
            t.sleep(500)
            st = t.state()
            if st["tool"] == "depth_pencil":
                break
        check("T7 sliding onto the strip selects the Depth Pencil", st["tool"] == "depth_pencil", st["tool"])

        p = face_px(st, "TOP", 8, 8)
        t.click(*p)
        t.sleep(500)
        check("T8 pencil paints the foreground brightness (200)", depth_at(t, 8, 8) in range(198, 203), depth_at(t, 8, 8))
        t.click(*p, button="right")
        t.sleep(400)
        check("T8b right click paints the background brightness (40)", depth_at(t, 8, 8) in range(38, 43), depth_at(t, 8, 8))

        # T9 fill
        t.lua('app.tool = "depth_fill"; app.fgColor = Color{ r = 90, g = 90, b = 90 }')
        t.sleep(500)
        st = t.state()
        check("T9 the studio follows a tool change from script", st["studio"]["depthTool"] == "fill", st["studio"].get("depthTool"))
        t.click(*face_px(st, "FRONT", 3, 3))      # first click activates FRONT
        t.sleep(500)
        t.click(*face_px(st, "FRONT", 3, 3))
        t.sleep(500)
        check("T10 fill floods the connected equal-depth area", depth_at(t, 24 + 12, 15) in range(88, 93) and depth_at(t, 24 + 3, 3) in range(88, 93),
              (depth_at(t, 36, 15), depth_at(t, 27, 3)))
        check("T10b fill stays inside the face", depth_at(t, 12, 15) != 90)

        # T11 closing the studio restores the normal tool and hides the group
        t.lua("app.command.EasymetricStudio()")
        t.sleep(600)
        st = t.state()
        check("T11 closing the studio hides the depth group and restores a normal tool",
              st["toolbar"]["studioGroupVisible"] is False and st["tool"] == "pencil", (st["tool"], st["toolbar"]["studioGroupVisible"]))

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
