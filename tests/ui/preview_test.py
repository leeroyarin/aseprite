"""The Diffuse preview must render the model for faces larger than 64 px (192x256 sprite -> 64x128 faces).
Run: python tests/ui/preview_test.py"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ui-test"))
from uitest import UiTest, UiTestError  # noqa: E402
import struct  # noqa: E402
import zlib  # noqa: E402

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + str(detail)) if detail and not cond else ""))


# A solid red block in the middle of every face, on Surface_Base AND Surface_Texture.
SETUP = '''
local s = Sprite(192, 256)
app.activeSprite = s
app.command.EasymetricStudio{ action = "setupSurfaces" }
local red = Color{ r = 220, g = 20, b = 20 }
for _, name in ipairs({ "Surface_Base", "Surface_Texture" }) do
  for _, l in ipairs(s.layers) do
    if l.name == name then
      local cel = l:cel(1)
      if cel then
        local img = cel.image
        local ox, oy = cel.position.x, cel.position.y
        for fy = 0, 1 do
          for fx = 0, 2 do
            for y = 24, 103 do
              for x = 12, 51 do
                local px, py = fx * 64 + x - ox, fy * 128 + y - oy
                if px >= 0 and py >= 0 and px < img.width and py < img.height then
                  img:drawPixel(px, py, red)
                end
              end
            end
          end
        end
      end
    end
  end
end
app.command.EasymetricStudio()
'''


def read_png(path):
    """Minimal PNG reader (8-bit RGB/RGBA, non-interlaced): returns (w, h, bytes_per_px, rows)."""
    data = open(path, "rb").read()
    pos, idat, w, h, ct = 8, b"", 0, 0, 2
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, _, ct = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def colored(png, rect, scale):
    w, h, bpp, rows = png
    if rect[0] * scale >= w:  # the screenshot is in logical pixels
        scale = 1
    x0, y0, rw, rh = [int(v * scale) for v in rect]
    n = 0
    for y in range(max(0, y0), min(h, y0 + rh)):
        row = rows[y]
        for x in range(max(0, x0), min(w, x0 + rw)):
            r, g, b = row[x * bpp], row[x * bpp + 1], row[x * bpp + 2]
            if r - g > 60 and r - b > 60:
                n += 1
    return n


def main():
    with UiTest(timeout=60) as t:
        t.lua(SETUP)
        t.sleep(2500)
        st = t.state()
        rect = st["studio"].get("previewRect")
        check("state reports previewRect", rect is not None, st["studio"].keys())
        path = os.path.join(tempfile.gettempdir(), "preview_test.png")
        res = t.screenshot(path)
        scale = (res or {}).get("scale", 1) if isinstance(res, dict) else 1
        n = colored(read_png(path), rect, scale) if rect else 0
        check("preview shows the red model (>200 red px)", n > 200, n)

    failed = [r for r in results if not r[1]]
    print("\n%d/%d passed" % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except UiTestError as e:
        print("UiTestError:", e)
        sys.exit(2)
