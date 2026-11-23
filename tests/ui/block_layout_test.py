"""Phase 2: "Start from a block". Enter W/H/D once; every view gets a correctly sized, correctly placed
block, rounded once in model space, so mismatched margins cannot exist.
Run: python tests/ui/block_layout_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
sys.path.insert(0, HERE)
from uitest import UiTest, UiTestError  # noqa: E402
import auto_align_test as A  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


# Model axis shown by each view's u / v and whether it is flipped (the renderer / ghost table).
AXES = {
    "TOP":   (("X", False), ("Z", False)),
    "DOWN":  (("X", False), ("Z", True)),
    "FRONT": (("X", False), ("Y", False)),
    "BACK":  (("X", True),  ("Y", False)),
    "RIGHT": (("Z", True),  ("Y", False)),
    "LEFT":  (("Z", False), ("Y", False)),
}


def model_extents(t):
    """{face: {axis: (first, len, cell)}} in MODEL pixels (flip undone), from the Base sheet."""
    st = t.state()
    out = {}
    r = A.ranges(t)
    for face, (c0, c1, r0, r1, _red) in r.items():
        uv = st["faces"][face]["uv"]
        out[face] = {}
        for (axis, flip), (lo, hi, size) in zip(AXES[face], ((c0, c1, uv[2]), (r0, r1, uv[3]))):
            first = size - 1 - hi if flip else lo
            out[face][axis] = (first, hi - lo + 1, size)
    return out


def layout(t, w, h, d, cw=None, ch=None, cd=None):
    t.lua("app.command.EasymetricStudio{ action = 'blockLayout', width = %d, height = %d, depth = %d%s }" % (
        w, h, d, "" if cw is None else ", cellW = %d, cellH = %d, cellD = %d" % (cw, ch, cd)))
    t.sleep(400)


def consistent(ext, block, cell):
    """Every view shows the block at the same model offset per axis, with the right size and cell."""
    bad = []
    for axis, b, c in zip("XYZ", block, cell):
        offs = set()
        for face, axes in ext.items():
            if axis in axes:
                first, ln, size = axes[axis]
                if ln != b or size != c:
                    bad.append((face, axis, "len/cell", ln, size))
                offs.add(first)
        if len(offs) != 1:
            bad.append((axis, "offsets differ", sorted(offs)))
    return bad


def case_even(t):
    layout(t, 22, 21, 22, 64, 128, 64)
    ext = model_extents(t)
    check("even: all views show the block at one model offset, right sizes",
          not consistent(ext, (22, 21, 22), (64, 128, 64)), consistent(ext, (22, 21, 22), (64, 128, 64)))
    # Height 21 in 128: the odd leftover (107) is rounded once: origin = 53 in EVERY view that shows Y.
    check("even: height origin is (128 - 21) // 2 = 53 in Front and Right",
          ext["FRONT"]["Y"][0] == 53 and ext["RIGHT"]["Y"][0] == 53, (ext["FRONT"], ext["RIGHT"]))
    check("even: width margins are equal (21 px) left and right",
          ext["FRONT"]["X"][0] == 21 and 64 - 21 - 22 == 21, ext["FRONT"])


def case_odd(t):
    layout(t, 22, 21, 22, 63, 127, 63)
    ext = model_extents(t)
    bad = consistent(ext, (22, 21, 22), (63, 127, 63))
    check("odd cells: every view agrees (no 20/22-style mismatch)", not bad, bad)
    check("odd cells: the odd pixel goes to the same side of the model in every view",
          ext["FRONT"]["X"][0] == ext["TOP"]["X"][0] == ext["BACK"]["X"][0] == 20, (ext["FRONT"], ext["TOP"], ext["BACK"]))
    # Right and Back are flipped views: the leftover shows on the other side of THEIR panels.
    r = A.ranges(t)
    check("odd cells: flipped Back sits at view columns 21-42, Front at 20-41",
          r["BACK"][:2] == (21, 42) and r["FRONT"][:2] == (20, 41), (r["BACK"], r["FRONT"]))


def case_grow(t):
    layout(t, 40, 30, 50, 8, 8, 8)  # cells smaller than the block: they grow to fit
    ext = model_extents(t)
    check("cells grow to the block when too small",
          not consistent(ext, (40, 30, 50), (40, 30, 50)), consistent(ext, (40, 30, 50), (40, 30, 50)))


def case_depth_reset(t):
    # An old depth map (generated, then carved) must not survive a new block.
    layout(t, 22, 21, 22, 64, 128, 64)
    t.lua("app.command.EasymetricStudio{ action = 'generateDepthFromBase' }")
    t.lua("app.command.EasymetricStudio{ action = 'depthDab', face = 'front', u = 30, v = 60, radius = 4, mode = 'minus' }")
    layout(t, 10, 12, 14, 64, 128, 64)
    st = t.state()
    bad = {}
    for n, f in st["faces"].items():
        d = f.get("depth") or {}
        area = int(f["uv"][2]) * int(f["uv"][3])
        if d.get("opaque") != area or d.get("sum") != 384 * area:
            bad[n] = (d.get("opaque"), d.get("sum"), area)
    check("after Start from block every face's depth is flat grey 128", not bad, bad)


def case_preview_and_hints(t):
    layout(t, 22, 21, 22, 64, 128, 64)
    t.lua("app.command.EasymetricStudio()")
    t.sleep(2500)
    st = t.state()["studio"]
    check("generated layout: no ghost hints anywhere",
          all(not f["ghost"]["cols"] and not f["ghost"]["rows"] for f in t.state()["faces"].values()),
          {n: f["ghost"] for n, f in t.state()["faces"].items()})
    check("generated layout: no carved surface voxels in the preview", st["previewCutoutPx"] == 0,
          st["previewCutoutPx"])
    # A stroke in Right: nothing needs aligning.
    A.stroke(t, "RIGHT", 28, 34, 15)
    check("generated layout: auto-align has nothing to do", not (A.last_align(t) or {}).get("applied"),
          A.last_align(t))


def case_undo(t):
    layout(t, 22, 21, 22, 64, 128, 64)
    before = model_extents(t)
    layout(t, 10, 10, 10, 64, 128, 64)
    t.key("z", mods="ctrl")
    t.sleep(500)
    after = model_extents(t)
    check("undo restores the previous block", after == before, (before["FRONT"], after["FRONT"]))


def main():
    with UiTest(timeout=90) as t:
        t.lua(A.FIXTURE)
        t.sleep(500)
        case_even(t)
        case_odd(t)
        case_grow(t)
        case_undo(t)
        case_depth_reset(t)
        case_preview_and_hints(t)
    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
