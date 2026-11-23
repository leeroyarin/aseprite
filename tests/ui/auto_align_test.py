"""Auto-align (Phase 1): a stroke in a view that is a few px off the reference shifts that view.
Fixture = the user's Body Model case: a 22 px wide head block, the Right view 2 px off in depth and
the Bottom view 1 px off in width. Faces: Top/Bottom 64x64, Front/Back/Right/Left 64x128.
Run: python tests/ui/auto_align_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


# Model block: X (width) 20..41, Y (height) 3..23, Z (depth) 20..41.
# View pixel ranges per face from the ghost/renderer axis table:
#   Top   u=X     v=Z        Bottom u=X   v=63-Z
#   Front u=X     v=Y        Back   u=63-X v=Y
#   Right u=63-Z  v=Y        Left   u=Z    v=Y
# The defaults below put Right at cols 20..41 (correct: 22..43) and Bottom at cols 21..42 (correct:
# 20..41), exactly the offsets found in the user's file.
FIXTURE = '''
local s = Sprite(192, 256)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
app.command.EasymetricStudio{ action = "linkDims", face = "front", width = 64, height = 128 }
app.command.EasymetricStudio{ action = "linkDims", face = "top", width = 64, height = 64 }
app.command.EasymetricStudio{ action = "linkDims", face = "down", width = 64, height = 64 }
'''

DRAW = '''
local s = app.activeSprite
local st = { faces = FACES }
local base
for _, l in ipairs(s.layers) do if l.name == "Surface_Base" then base = l end end
local cel = base:cel(1)
if not cel then cel = s:newCel(base, 1) end
local img = Image(s.width, s.height, s.colorMode)
img:drawImage(cel.image, cel.position)
local hair = Color{ r = 34, g = 32, b = 52 }
local function block(name, c0, c1, r0, r1)
  local uv = st.faces[name].uv
  for v = r0, r1 do for u = c0, c1 do img:drawPixel(uv[1] + u, uv[2] + v, hair) end end
end
block("TOP",   20, 41, 20, 41)
block("FRONT", 20, 41, 3, 23)
block("BACK",  22, 43, 3, 23)
block("LEFT",  20, 41, 3, 23)
block("RIGHT", RIGHT_C0, RIGHT_C1, 3, 23)
block("DOWN",  DOWN_C0, DOWN_C1, 22, 43)
cel.image = img
cel.position = Point(0, 0)
app.preferences.easymetric.auto_align = AUTO_ALIGN
app.preferences.easymetric.auto_align_limit = 4
app.preferences.easymetric.show_cutouts = false
app.fgColor = Color{ r = 255, g = 0, b = 0 }
app.tool = "pencil"
app.command.EasymetricStudio()
'''

# Prints "<FACE> cols a-b rows c-d red N" for every face (Base occupancy; red counted on all sheets).
RANGES = '''
local s = app.activeSprite
local st = { faces = FACES }
local function img_of(name)
  for _, l in ipairs(s.layers) do
    if l.name == name and l:cel(1) then return l:cel(1).image, l:cel(1).position end
  end
end
local bimg, bpos = img_of("Surface_Base")
local timg, tpos = img_of("Surface_Texture")
local function px(img, pos, x, y)
  if not img then return 0 end
  x, y = x - pos.x, y - pos.y
  if x < 0 or y < 0 or x >= img.width or y >= img.height then return 0 end
  return img:getPixel(x, y)
end
for _, name in ipairs({"TOP", "DOWN", "FRONT", "BACK", "RIGHT", "LEFT"}) do
  local uv = st.faces[name].uv
  local c0, c1, r0, r1, red = 999, -1, 999, -1, 0
  for v = 0, uv[4] - 1 do for u = 0, uv[3] - 1 do
    local c = px(bimg, bpos, uv[1] + u, uv[2] + v)
    if app.pixelColor.rgbaA(c) > 16 then
      if u < c0 then c0 = u end; if u > c1 then c1 = u end
      if v < r0 then r0 = v end; if v > r1 then r1 = v end
    end
    for _, cc in ipairs({ c, px(timg, tpos, uv[1] + u, uv[2] + v) }) do
      if app.pixelColor.rgbaA(cc) > 16 and app.pixelColor.rgbaR(cc) == 255 and app.pixelColor.rgbaG(cc) == 0 then red = red + 1 end
    end
  end end
  print(name, c0, c1, r0, r1, red)
end
'''

# Prints 1 when LEFT is exactly RIGHT mirrored horizontally on Base, else the first mismatch.
MIRROR = '''
local s = app.activeSprite
local st = { faces = FACES }
local cel
for _, l in ipairs(s.layers) do if l.name == "Surface_Base" then cel = l:cel(1) end end
local img, pos = cel.image, cel.position
local function px(x, y)
  x, y = x - pos.x, y - pos.y
  if x < 0 or y < 0 or x >= img.width or y >= img.height then return 0 end
  local c = img:getPixel(x, y)
  return app.pixelColor.rgbaA(c) > 16 and c or 0
end
local r, l = st.faces.RIGHT.uv, st.faces.LEFT.uv
for v = 0, r[4] - 1 do for u = 0, r[3] - 1 do
  if px(r[1] + u, r[2] + v) ~= px(l[1] + (l[3] - 1 - u), l[2] + v) then print("mismatch", u, v) return end
end end
print(1)
'''


def faces_lua(t):
    """The face rects as a Lua table literal: { TOP = { uv = {x, y, w, h} }, ... }."""
    st = t.state()
    return "{ " + ", ".join("%s = { uv = { %s } }" % (n, ", ".join(str(int(v)) for v in f["uv"]))
                            for n, f in st["faces"].items()) + " }"


def with_faces(t, code):
    return code.replace("FACES", faces_lua(t))


def fixture(right=(20, 41), down=(21, 42), auto=True):
    return (DRAW.replace("RIGHT_C0", str(right[0])).replace("RIGHT_C1", str(right[1]))
            .replace("DOWN_C0", str(down[0])).replace("DOWN_C1", str(down[1]))
            .replace("AUTO_ALIGN", "true" if auto else "false"))


def ranges(t):
    out = {}
    for line in t.lua(with_faces(t, RANGES)).splitlines():
        p = line.split()
        if len(p) == 6:
            out[p[0]] = tuple(int(x) for x in p[1:])
    return out


def face(st, name):
    return next(f for f in st["studio"]["faces"] if f["name"] == name)


def stroke(t, name, u0, u1, v):
    """Pencil stroke across face pixels u0..u1 at row v of view `name` (inside the block)."""
    st = t.state()
    fs = face(st, name)["faceScreen"]
    uv = st["faces"][name]["uv"]
    sx = fs[2] / float(uv[2])
    sy = fs[3] / float(uv[3])
    y = fs[1] + (v + 0.5) * sy
    t.drag(fs[0] + (u0 + 0.5) * sx, y, fs[0] + (u1 + 0.5) * sx, y, steps=6)
    t.sleep(700)  # the poll (33 ms) runs the alignment once the stroke is over


def last_align(t):
    return t.state()["studio"].get("lastAlign")


def wait_render(t):
    t.sleep(1500)
    return t.state()["studio"]["previewCutoutPx"]


def open_app(t, script):
    t.lua(FIXTURE)
    t.lua(with_faces(t, script))
    t.sleep(1500)


def case_offsets():
    with UiTest(timeout=90) as t:
        open_app(t, fixture())
        r0 = ranges(t)
        check("fixture: Right at cols 20-41 (2 px off)", r0["RIGHT"][:2] == (20, 41), r0["RIGHT"])
        check("fixture: Bottom at cols 21-42 (1 px off)", r0["DOWN"][:2] == (21, 42), r0["DOWN"])
        cut0 = wait_render(t)
        check("fixture: the preview has carved surface pixels", cut0 > 0, cut0)

        stroke(t, "RIGHT", 28, 34, 15)
        a = last_align(t) or {}
        r1 = ranges(t)
        check("Right stroke: Right shifted to cols 22-43", r1["RIGHT"][:4] == (22, 43, 3, 23), r1["RIGHT"])
        check("Right stroke: one shift of +2 columns", a.get("applied") and a.get("du") == 2 and a.get("dv") == 0, a)
        check("Right stroke: message says aligned +2 px depth",
              "Right view aligned" in a.get("message", "") and "+2 px depth" in a.get("message", ""), a)
        check("Right stroke: the stroke moved with the view", r1["RIGHT"][4] > 0, r1["RIGHT"])
        check("Right stroke: no other view moved",
              all(r1[n] == r0[n] for n in ("TOP", "DOWN", "FRONT", "BACK", "LEFT")), (r0, r1))

        stroke(t, "DOWN", 26, 32, 30)
        a = last_align(t) or {}
        r2 = ranges(t)
        check("Bottom stroke: Bottom shifted to cols 20-41", r2["DOWN"][:4] == (20, 41, 22, 43), r2["DOWN"])
        check("Bottom stroke: shift of -1 column", a.get("du") == -1 and a.get("dv") == 0, a)
        cut2 = wait_render(t)
        check("after both strokes the preview has no carved surface pixels", cut2 == 0, cut2)

        # Undo removes the nudge only: the stroke stays.
        t.key("z", mods="ctrl")
        t.sleep(500)
        r3 = ranges(t)
        check("undo: Bottom back at cols 21-42", r3["DOWN"][:2] == (21, 42), r3["DOWN"])
        check("undo: the Bottom stroke is kept", r3["DOWN"][4] > 0, r3["DOWN"])
        check("undo: Right stays aligned", r3["RIGHT"][:2] == (22, 43), r3["RIGHT"])


def case_reference():
    with UiTest(timeout=90) as t:
        open_app(t, fixture())
        r0 = ranges(t)
        stroke(t, "FRONT", 26, 32, 15)
        a = last_align(t) or {}
        r1 = ranges(t)
        check("Front (reference) stroke: nothing moved",
              all(r1[n][:4] == r0[n][:4] for n in r0), (r0, r1))
        notes = " | ".join(a.get("notes", []))
        check("Front stroke: readout names Bottom's width offset", "Bottom: width offset +1 px" in notes, a)


def case_size_and_limit():
    with UiTest(timeout=90) as t:
        open_app(t, fixture(right=(18, 41)))  # 24 px deep vs Top's 22
        r0 = ranges(t)
        stroke(t, "RIGHT", 28, 34, 15)
        a = last_align(t) or {}
        r1 = ranges(t)
        check("size mismatch: Right not moved", r1["RIGHT"][:4] == r0["RIGHT"][:4], (r0["RIGHT"], r1["RIGHT"]))
        check("size mismatch: readout 'Right: depth 24 px, Top says 22 px'",
              "Right: depth 24 px, Top says 22 px" in a.get("notes", []), a)
    with UiTest(timeout=90) as t:
        open_app(t, fixture(right=(16, 37)))  # 6 px off, over the 4 px limit
        stroke(t, "RIGHT", 24, 30, 15)
        a = last_align(t) or {}
        r1 = ranges(t)
        check("over the limit: Right not moved", r1["RIGHT"][:2] == (16, 37), r1["RIGHT"])
        check("over the limit: readout says so", any("auto-align limit" in n for n in a.get("notes", [])), a)


def case_linked():
    with UiTest(timeout=90) as t:
        open_app(t, fixture())
        links = sorted(t.find(cls="LinkToggleButton"), key=lambda w: w["rect"][0])
        x, y, w, h = links[2]["rect"]
        t.click(x + w / 2, y + h / 2)  # link Right / Left
        t.sleep(1200)
        stroke(t, "RIGHT", 28, 34, 15)
        a = last_align(t) or {}
        r1 = ranges(t)
        check("linked: Right aligned", a.get("applied") and r1["RIGHT"][:2] == (22, 43), (a, r1["RIGHT"]))
        m = t.lua(with_faces(t, MIRROR)).strip()
        check("linked: Left is the mirrored copy of Right after the shift", m == "1", m)


def case_toggle_off():
    with UiTest(timeout=90) as t:
        open_app(t, fixture(auto=False))
        r0 = ranges(t)
        st = t.state()["studio"]
        check("toggle off is reported", st.get("autoAlign") is False, st.get("autoAlign"))
        stroke(t, "RIGHT", 28, 34, 15)
        r1 = ranges(t)
        check("toggle off: nothing moves", r1["RIGHT"][:4] == r0["RIGHT"][:4], (r0["RIGHT"], r1["RIGHT"]))
        check("toggle off: the stroke itself landed", r1["RIGHT"][4] > 0, r1["RIGHT"])


def main():
    case_offsets()
    case_reference()
    case_size_and_limit()
    case_linked()
    case_toggle_off()
    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
