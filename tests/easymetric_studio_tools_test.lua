-- =============================================================================
-- Easymetric studio tools tests (headless): face flip, depth tools.
-- Run: build\bin\aseprite.exe -b --script tests\easymetric_studio_tools_test.lua
-- Everything is asserted from pixel values / the machine-readable state, never
-- from a screenshot. UI behaviour is covered by tests/ui/*.py.
-- =============================================================================
local suite = { passed = 0, failed = 0, currentSuite = "Default", failures = {} }
local function describe(name, fn)
  suite.currentSuite = name
  print(string.format("\n======================================================\n%s\n======================================================", name))
  fn()
end
local function it(name, fn)
  local ok, err = pcall(fn)
  if ok then suite.passed = suite.passed + 1; print("  [PASS] " .. name)
  else suite.failed = suite.failed + 1; table.insert(suite.failures, { suite = suite.currentSuite, test = name, err = err }); print("  [FAIL] " .. name .. " -- " .. tostring(err)) end
end
local function assert_eq(a, b, msg) if a ~= b then error(msg .. " (expected " .. tostring(b) .. ", got " .. tostring(a) .. ")") end end
local function assert_true(c, msg) if not c then error(msg) end end

local function layer(spr, name) for _, l in ipairs(spr.layers) do if l.name == name then return l end end end
local function px(img, x, y)
  local c = img:getPixel(x, y)
  return app.pixelColor.rgbaR(c) .. "," .. app.pixelColor.rgbaG(c) .. "," .. app.pixelColor.rgbaB(c) .. "," .. app.pixelColor.rgbaA(c)
end
local function stateOf(spr)
  app.activeSprite = spr
  local tmp = app.fs.joinPath(app.fs.tempPath, "easymetric_state_test.json")
  app.command.EasymetricStudio{ action = "state", out = tmp }
  local f = io.open(tmp, "r"); local txt = f:read("a"); f:close(); os.remove(tmp)
  return json.decode(txt)
end
local function faceHash(spr, face, sheet) return stateOf(spr).faces[face][sheet].hash end

-- 72x60 sprite -> 24x30 faces. TOP uv=(0,0), FRONT uv=(24,0), RIGHT=(48,0), DOWN=(0,30)...
local function makeSprite()
  local spr = Sprite(72, 60)
  app.activeSprite = spr
  app.command.EasymetricStudio{ action = "setupSurfaces" }
  local img = layer(spr, "Surface_Base"):cel(1).image
  for y = 0, 3 do for x = 0, 3 do img:drawPixel(x, y, Color{ r = 255, g = 0, b = 0 }) end end        -- TOP top-left red
  for y = 26, 29 do for x = 20, 23 do img:drawPixel(x, y, Color{ r = 0, g = 0, b = 255 }) end end    -- TOP bottom-right blue
  for y = 0, 3 do for x = 44, 47 do img:drawPixel(x, y, Color{ r = 0, g = 255, b = 0 }) end end      -- FRONT top-right green
  return spr, img
end

describe("Flip face (FH / FV)", function()
  it("F1: flip horizontal mirrors the face left/right", function()
    local spr, img = makeSprite()
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    assert_eq(px(img, 0, 0), "0,0,0,0", "old red corner cleared")
    assert_eq(px(img, 23, 0), "255,0,0,255", "red now top-right")
    assert_eq(px(img, 0, 29), "0,0,255,255", "blue now bottom-left")
    spr:close()
  end)

  it("F2: flip vertical mirrors the face top/bottom", function()
    local spr, img = makeSprite()
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "v" }
    assert_eq(px(img, 0, 29), "255,0,0,255", "red now bottom-left")
    assert_eq(px(img, 23, 0), "0,0,255,255", "blue now top-right")
    spr:close()
  end)

  it("F3: flipping twice is the identity", function()
    local spr = makeSprite()
    local before = faceHash(spr, "TOP", "base")
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    assert_true(faceHash(spr, "TOP", "base") ~= before, "one flip changes the face")
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    assert_eq(faceHash(spr, "TOP", "base"), before, "two flips restore it")
    spr:close()
  end)

  it("F4: other faces are untouched", function()
    local spr, img = makeSprite()
    local front = faceHash(spr, "FRONT", "base")
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    assert_eq(faceHash(spr, "FRONT", "base"), front, "FRONT unchanged by a TOP flip")
    assert_eq(px(img, 47, 0), "0,255,0,255", "FRONT pixel still in place")
    spr:close()
  end)

  it("F5: the Depth sheet is flipped too", function()
    local spr = makeSprite()
    app.command.EasymetricStudio{ action = "generateDepthFromBase" }
    local dimg = layer(spr, "Surface_DEPTH"):cel(1).image
    dimg:drawPixel(1, 1, Color{ r = 10, g = 10, b = 10 })
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    assert_eq(px(dimg, 22, 1), "10,10,10,255", "depth pixel mirrored to x=22")
    spr:close()
  end)

  it("F6: one flip = one undo step", function()
    local spr = makeSprite()
    local before = faceHash(spr, "TOP", "base")
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    app.undo()
    assert_eq(faceHash(spr, "TOP", "base"), before, "a single undo restores the face")
    spr:close()
  end)

  it("F7: a cel that does not cover the face is grown, not clipped", function()
    local spr = Sprite(72, 60)
    app.activeSprite = spr
    app.command.EasymetricStudio{ action = "setupSurfaces" }
    local tex = spr:newLayer(); tex.name = "Surface_Texture"
    local small = Image(8, 8)
    small:clear(Color{ r = 9, g = 9, b = 9 })
    spr:newCel(tex, 1, small, Point(10, 3))          -- face TOP x 10..17
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h" }
    local img = tex:cel(1).image
    local c = tex:cel(1)
    local function at(x, y) local ix, iy = x - c.position.x, y - c.position.y
      if ix < 0 or iy < 0 or ix >= img.width or iy >= img.height then return "0,0,0,0" end
      return px(img, ix, iy) end
    assert_eq(at(13, 3), "9,9,9,255", "x 10 -> 13 .. x 17 -> 6: pixel (13,3) is inside the mirrored block")
    assert_eq(at(6, 3), "9,9,9,255", "mirrored block's left edge")
    assert_eq(at(19, 3), "0,0,0,0", "nothing left behind outside the mirrored block")
    spr:close()
  end)

  it("F8: only the requested frame is flipped", function()
    local spr = makeSprite()
    spr:newFrame(1)
    local base = layer(spr, "Surface_Base")
    base:cel(2).image:drawPixel(0, 0, Color{ r = 1, g = 2, b = 3 })
    app.command.EasymetricStudio{ action = "flipFace", face = "top", axis = "h", frame = "2" }
    assert_eq(px(base:cel(1).image, 0, 0), "255,0,0,255", "frame 1 untouched")
    assert_eq(px(base:cel(2).image, 23, 0), "1,2,3,255", "frame 2 flipped")
    spr:close()
  end)
end)

describe("Ghost hints (missing content on perpendicular surfaces)", function()
  local function has(list, v) for _, x in ipairs(list) do if x == v then return true end end return false end
  local function ghostOf(spr, face) return stateOf(spr).faces[face].ghost end
  local function paintBase(spr, x0, y0, x1, y1)
    local img = layer(spr, "Surface_Base"):cel(1).image
    for y = y0, y1 do for x = x0, x1 do img:drawPixel(x, y, Color{ r = 255, g = 255, b = 255 }) end end
  end
  local function fresh()
    local spr = Sprite(72, 60)
    app.activeSprite = spr
    app.command.EasymetricStudio{ action = "setupSurfaces" }
    return spr
  end

  it("G1: an empty object has no ghosts", function()
    local spr = fresh()
    for _, f in ipairs({ "TOP", "DOWN", "FRONT", "BACK", "RIGHT", "LEFT" }) do
      assert_eq(#ghostOf(spr, f).cols + #ghostOf(spr, f).rows, 0, f .. " has no ghost")
    end
    spr:close()
  end)

  it("G2: a rectangle on Front asks Top/Down for its columns and Right/Left for its rows", function()
    local spr = fresh()
    paintBase(spr, 24 + 5, 10, 24 + 9, 14)               -- Front u 5..9, v 10..14
    local top = ghostOf(spr, "TOP")
    for c = 5, 9 do assert_true(has(top.cols, c), "Top column " .. c .. " missing") end
    assert_true(not has(top.cols, 4) and not has(top.cols, 10), "Top bands are exactly the rectangle's width")
    assert_true(has(ghostOf(spr, "DOWN").cols, 7), "Down column 7 missing")
    for r = 10, 14 do
      assert_true(has(ghostOf(spr, "RIGHT").rows, r), "Right row " .. r .. " missing")
      assert_true(has(ghostOf(spr, "LEFT").rows, r), "Left row " .. r .. " missing")
    end
    assert_eq(#ghostOf(spr, "FRONT").cols + #ghostOf(spr, "FRONT").rows, 0, "the source surface has no ghost")
    assert_eq(#ghostOf(spr, "BACK").cols + #ghostOf(spr, "BACK").rows, 0, "the opposite surface is not compared")
    spr:close()
  end)

  it("G3: drawing the matching shape on Top removes the Top ghost", function()
    local spr = fresh()
    paintBase(spr, 24 + 5, 10, 24 + 9, 14)
    paintBase(spr, 5, 0, 9, 29)                           -- Top: columns 5..9, all depth
    assert_eq(#ghostOf(spr, "TOP").cols, 0, "Top columns are now covered")
    spr:close()
  end)

  it("G4: mirrored surfaces map correctly (Back flips X, Right flips Z)", function()
    local spr = fresh()
    paintBase(spr, 24 + 2, 30 + 3, 24 + 2, 30 + 3)        -- Back (u 2, v 3) -> X index 21, Y row 3
    assert_true(has(ghostOf(spr, "TOP").cols, 21), "Back u=2 -> Top column 21")
    assert_true(has(ghostOf(spr, "LEFT").rows, 3), "Back v=3 -> Left row 3")
    local spr2 = fresh()
    paintBase(spr2, 48 + 2, 5, 48 + 2, 5)                 -- Right (u 2, v 5) -> Z flipped, Y row 5
    assert_true(has(ghostOf(spr2, "TOP").rows, 26), "Right u=2 -> Top row 26")
    assert_true(has(ghostOf(spr2, "DOWN").rows, 3), "Right u=2 -> Down row 3 (Down flips Z)")
    assert_true(has(ghostOf(spr2, "FRONT").rows, 5), "Right v=5 -> Front row 5")
    spr:close(); spr2:close()
  end)
end)

print("\n======================================================")
print("     STUDIO TOOLS TEST SUITE EXECUTION SUMMARY      ")
print("======================================================")
print(string.format(" TOTAL:  %3d Passed, %3d Failed (Total %d)", suite.passed, suite.failed, suite.passed + suite.failed))
print("======================================================")
if suite.failed > 0 then error(string.format("Studio Tools Test Suite FAILED: %d test(s) failed.", suite.failed)) end
print("ALL STUDIO TOOLS TESTS PASSED SUCCESSFULLY.")
