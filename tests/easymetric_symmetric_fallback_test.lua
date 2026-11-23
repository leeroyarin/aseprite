-- =============================================================================
-- Empty-face fallback is mirrored (2026-11-16).
-- An empty face is shown with its opposite face's pixels. Like a real object, a
-- mark on the left seen from the front must be on the RIGHT seen from behind
-- (and Top/Bottom the same in depth). Before the fix the opposite face was
-- sampled at the same (u, v), so the mark stayed on the same side.
-- Run: build\bin\aseprite.exe -b --script tests\easymetric_symmetric_fallback_test.lua
-- =============================================================================
local suite = { passed = 0, failed = 0 }
local function it(name, fn)
  local ok, err = pcall(fn)
  if ok then suite.passed = suite.passed + 1; print("  [PASS] " .. name)
  else suite.failed = suite.failed + 1; print("  [FAIL] " .. name .. " -- " .. tostring(err)) end
end
local function assert_true(c, msg) if not c then error(msg) end end

local function layer(spr, name) for _, l in ipairs(spr.layers) do if l.name == name then return l end end end

-- 72x60 sprite -> 24x30 faces: TOP uv (0,0), FRONT (24,0), RIGHT (48,0), DOWN (0,30), BACK (24,30), LEFT (48,30).
-- Paints the left half of `face` red and the right half blue, on Base; all other faces stay empty.
local function makeSprite(fx, fy, fw, fh)
  local spr = Sprite(72, 60)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "setupSurfaces" }
  local cel = layer(spr, "Surface_Base"):cel(1)
  local img = Image(spr.width, spr.height, spr.colorMode)
  local red, blue = Color{ r = 230, g = 20, b = 20 }, Color{ r = 20, g = 20, b = 230 }
  for v = 0, fh - 1 do for u = 0, fw - 1 do
    img:drawPixel(fx + u, fy + v, u < fw / 2 and red or blue)
  end end
  cel.image = img
  cel.position = Point(0, 0)
  return spr
end

-- Mean screen x of red and of blue pixels in the baked Diffuse at (yaw, pitch).
local function sides(spr, yaw, pitch)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "bake3D", yaw = yaw, pitch = pitch }
  local img = layer(spr, "Iso_Diffuse"):cel(1).image
  local rx, rn, bx, bn = 0, 0, 0, 0
  for it in img:pixels() do
    local c = it()
    local r, g, b, a = app.pixelColor.rgbaR(c), app.pixelColor.rgbaG(c), app.pixelColor.rgbaB(c), app.pixelColor.rgbaA(c)
    if a > 0 and r > 2 * g + 20 and r > 2 * b + 20 then rx = rx + it.x; rn = rn + 1 end
    if a > 0 and b > 2 * r + 20 and b > 2 * g + 20 then bx = bx + it.x; bn = bn + 1 end
  end
  assert_true(rn > 0 and bn > 0, string.format("both colours visible at yaw %d (red %d, blue %d)", yaw, rn, bn))
  return rx / rn, bx / bn
end

print("\nEmpty-face fallback is mirrored")

it("Front only: seen from behind, the red half swaps sides (Back = Front mirrored)", function()
  local spr = makeSprite(24, 0, 24, 30)
  local fr, fb = sides(spr, 0, 0)
  local br, bb = sides(spr, 180, 0)
  assert_true((fr < fb) ~= (br < bb),
    string.format("front: red %.1f blue %.1f; back: red %.1f blue %.1f (same side = unmirrored)", fr, fb, br, bb))
  spr:close()
end)

it("Right only: seen from the left, the red half swaps sides (Left = Right mirrored)", function()
  local spr = makeSprite(48, 0, 24, 30)
  local rr, rb = sides(spr, -90, 0)
  local lr, lb = sides(spr, 90, 0)
  assert_true((rr < rb) ~= (lr < lb),
    string.format("right: red %.1f blue %.1f; left: red %.1f blue %.1f", rr, rb, lr, lb))
  spr:close()
end)

print("\n======================================================")
print(string.format(" TOTAL:  %3d Passed, %3d Failed (Total %d)", suite.passed, suite.failed, suite.passed + suite.failed))
print("======================================================")
if suite.failed > 0 then error(string.format("Symmetric fallback suite FAILED: %d test(s) failed.", suite.failed)) end
