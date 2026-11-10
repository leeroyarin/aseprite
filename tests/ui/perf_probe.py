"""Performance probe (not a pass/fail test unless --assert): measures UI-thread stalls and wall
time while painting, depth sculpting and switching layers/tools in the Easymetric studio.

    python tests/ui/perf_probe.py [--size 128] [--assert]

`uiMaxGapMs` = longest gap between two UI timer ticks during the command (15 ms when idle).
Anything above ~100 ms is felt as lag.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, rect_center  # noqa: E402

SIZE = 128
for i, a in enumerate(sys.argv):
    if a == "--size":
        SIZE = int(sys.argv[i + 1])
ASSERT = "--assert" in sys.argv

SETUP = '''
local s = Sprite(%d, %d)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
for _, l in ipairs(s.layers) do
  if l.name == "Surface_Base" then l:cel(1).image:clear(Color{ r = 220, g = 120, b = 60, a = 255 }) end
end
app.command.EasymetricStudio{ action = "generateDepthFromBase" }
local tex = s:newLayer(); tex.name = "Surface_Texture"
s:newCel(tex, 1, Image(s.width, s.height), Point(0, 0))
app.fgColor = Color{ r = 30, g = 200, b = 90 }
''' % (SIZE * 3, SIZE * 2)

worst = {}


def face(st, name):
    return next(f for f in st["studio"]["faces"] if f["name"] == name)


def report(name, res, wall, limit_ms=250):
    gap = res.get("uiMaxGapMs", 0)
    worst[name] = gap
    print("%-34s wall %6.0f ms   longest UI stall %6.0f ms %s" % (name, wall * 1000, gap, "" if gap < limit_ms else "  <-- LAG"))


def main():
    with UiTest(timeout=120) as t:
        t.lua(SETUP)
        t.lua("app.command.EasymetricStudio()")
        t.sleep(1500)
        st = t.state()
        f = face(st, "TOP")
        fs = f["faceScreen"]
        zoom = f["zoom"]
        print("face %dx%d, zoom %.1f, panel face on screen %s" % (SIZE, SIZE, zoom, fs))
        # activate the Top panel
        t.click(fs[0] + 4, fs[1] + 4)
        t.sleep(600)
        x0, y0 = fs[0] + int(fs[2] * 0.15), fs[1] + int(fs[3] * 0.5)
        x1, y1 = fs[0] + int(fs[2] * 0.85), fs[1] + int(fs[3] * 0.5)

        # 1. normal pencil stroke in a panel (preview re-renders while painting)
        t.lua('app.tool = "pencil"')
        t.sleep(300)
        t0 = time.time()
        r = t.drag(x0, y0, x1, y1, steps=80, stepMs=16)
        report("pencil drag (80 moves)", r, time.time() - t0)
        t.sleep(500)

        # 2. depth brush stroke
        t.lua('app.tool = "depth_brush"')
        t.sleep(400)
        t0 = time.time()
        r = t.drag(x0, y0 + 20, x1, y1 + 20, steps=80, stepMs=16)
        report("depth brush drag (80 moves)", r, time.time() - t0)
        t.sleep(500)

        # 3. depth pencil + fill
        t.lua('app.tool = "depth_pencil"')
        t.sleep(300)
        t0 = time.time()
        r = t.drag(x0, y0 + 40, x1, y1 + 40, steps=80, stepMs=16)
        report("depth pencil drag (80 moves)", r, time.time() - t0)
        t.lua('app.tool = "depth_fill"')
        t.sleep(300)
        t0 = time.time()
        r = t.click(fs[0] + 10, fs[1] + 10)
        report("depth fill click", r, time.time() - t0)
        t.sleep(500)

        # 4. layer switching (what the Layers panel does) and tool switching
        t.lua('app.tool = "pencil"')
        t.sleep(300)
        for name in ("Surface_Texture", "Surface_Base", "Surface_DEPTH", "Surface_Texture"):
            t0 = time.time()
            r = t.call("lua", code='for _, l in ipairs(app.activeSprite.layers) do if l.name == "%s" then app.activeLayer = l end end' % name)
            report("switch active layer -> " + name, r, time.time() - t0)
            t.sleep(300)
        for tool in ("depth_brush", "pencil", "depth_fill", "eraser", "pencil"):
            t0 = time.time()
            r = t.call("lua", code='app.tool = "%s"' % tool)
            report("switch tool -> " + tool, r, time.time() - t0)
            t.sleep(300)

        # 5. brush right after a layer switch (the user's "switch layers and brush smoothly")
        t.lua('for _, l in ipairs(app.activeSprite.layers) do if l.name == "Surface_Texture" then app.activeLayer = l end end')
        t.sleep(400)
        t0 = time.time()
        r = t.drag(x0, y0 - 30, x1, y1 - 30, steps=80, stepMs=16)
        report("pencil drag on Texture layer", r, time.time() - t0)

    if ASSERT:
        bad = {k: v for k, v in worst.items() if v > 250}
        print("\nFAIL: stalls over 250 ms: %s" % bad if bad else "\nOK: no stall over 250 ms")
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
