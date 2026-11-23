-- =============================================================================
-- Easymetric objects (2026-11-17, Arin: "separate objects like head and body,
-- each in its own layer"). Object 1 = the existing sheets + slices (never
-- moved); every other object is a group "Object: <name>" with its own sheets,
-- six cells of the shared model size in its own band, and a
-- rank. The preview shows every visible object.
-- Run: build\bin\aseprite.exe -b --script tests\easymetric_objects_test.lua
-- =============================================================================
local suite = { passed = 0, failed = 0 }
local function it(name, fn)
  local ok, err = pcall(fn)
  if ok then suite.passed = suite.passed + 1; print("  [PASS] " .. name)
  else suite.failed = suite.failed + 1; print("  [FAIL] " .. name .. " -- " .. tostring(err)) end
end
local function check(c, msg) if not c then error(msg, 2) end end

local function E(t) app.command.EasymetricStudio(t) end
local function readJson(action, extra)
  local tmp = app.fs.joinPath(app.fs.tempPath, "easymetric_objects_test.json")
  local t = { action = action, out = tmp }
  for k, v in pairs(extra or {}) do t[k] = v end
  E(t)
  local f = io.open(tmp, "r"); local txt = f:read("a"); f:close(); os.remove(tmp)
  return json.decode(txt)
end
local function state() return readJson("state") end
local function stats(yaw, pitch) return readJson("previewStats", { yaw = yaw, pitch = pitch }) end
local function objectOf(st, name)
  for _, o in ipairs(st.objects) do if o.name == name or o.key == name then return o end end
end
local function hitsOf(ps, key)
  for _, o in ipairs(ps.objects) do if o.key == key then return o.px end end
  return nil
end
local function topLayer(spr, name) for _, l in ipairs(spr.layers) do if l.name == name then return l end end end
local function sliceCount(spr) local n = 0 for _ in ipairs(spr.slices) do n = n + 1 end return n end

-- A 16x16x16 body filled red in Object 1.
local function bodySprite(w, h, d)
  local spr = Sprite(48, 32)
  app.activeSprite = spr
  E{ action = "setupSurfaces" }
  E{ action = "blockLayout", width = w or 16, height = h or 16, depth = d or 16, r = 220, g = 30, b = 30 }
  return spr
end

local function checkBox(actual, w, h, d, label)
  check(actual and actual[1] == w and actual[2] == h and actual[3] == d,
        label .. ": " .. table.concat(actual or {}, ","))
end

-- Leave room above the root block, so the two objects occupy distinct space.
local function addHead()
  E{ action = "modelSize", width = 16, height = 32, depth = 16, mode = "keep" }
  E{ action = "newObject", name = "Head", block = true, bw = 8, bh = 8, bd = 8,
     place = "above", r = 30, g = 30, b = 220 }
end

-- Counts red / blue pixels and their mean screen y in the baked Diffuse.
local function colours(spr, yaw, pitch)
  app.activeSprite = spr
  E{ action = "bake3D", yaw = yaw, pitch = pitch }
  local img = topLayer(spr, "Iso_Diffuse"):cel(1).image
  local r = { n = 0, y = 0, minY = 1e9, maxY = -1 }
  local b = { n = 0, y = 0, minY = 1e9, maxY = -1 }
  for p in img:pixels() do
    local c = p()
    local R, G, B = app.pixelColor.rgbaR(c), app.pixelColor.rgbaG(c), app.pixelColor.rgbaB(c)
    local t = (R > 120 and B < 90 and G < 90) and r or ((B > 120 and R < 90 and G < 120) and b or nil)
    if t then
      t.n = t.n + 1; t.y = t.y + p.y
      t.minY = math.min(t.minY, p.y); t.maxY = math.max(t.maxY, p.y)
    end
  end
  if r.n > 0 then r.y = r.y / r.n end
  if b.n > 0 then b.y = b.y / b.n end
  return r, b
end

print("Easymetric objects")

it("1 legacy: a plain document has exactly one object and no groups", function()
  local spr = bodySprite()
  local st = state()
  check(#st.objects == 1, "objects: " .. #st.objects)
  check(st.objects[1].name == "Object 1" and st.objects[1].key == "", "root name/key")
  check(st.objects[1].box[1] == 16 and st.objects[1].box[2] == 16 and st.objects[1].box[3] == 16, "box")
  for _, l in ipairs(spr.layers) do check(not l.isGroup, "unexpected group " .. l.name) end
  local ps = stats(45, 30)
  check(#ps.objects == 1 and ps.objects[1].px > 0, "single render hits")
  spr:close()
end)

it("2 newObject adds a group with its own 3 sheets in a new band; Object 1 untouched", function()
  local spr = bodySprite()
  local before = state()
  local w, h = spr.width, spr.height
  E{ action = "newObject", name = "Head", width = 8, height = 8, depth = 8, r = 30, g = 30, b = 220 }
  local st = state()
  check(#st.objects == 2, "objects: " .. #st.objects)
  local head = objectOf(st, "Head")
  check(head and head.group == "Object: Head", "group name")
  local grp = topLayer(spr, "Object: Head")
  check(grp and grp.isGroup and #grp.layers == 3, "3 children")
  local names = {}
  for _, l in ipairs(grp.layers) do names[l.name] = true end
  check(names.Surface_Base and names.Surface_Texture and names.Surface_DEPTH, "sheet names")
  check(head.bandY == h, "band starts under Object 1: " .. head.bandY .. " vs " .. h)
  check(spr.width == w and spr.height == h * 2, "canvas " .. spr.width .. "x" .. spr.height)
  checkBox(st.modelSize, 16, 16, 16, "model")
  checkBox(head.box, 16, 16, 16, "head")
  checkBox(head.offset, 0, 0, 0, "offset")
  for face, f in pairs(before.faces) do
    local uv = head.faces[face].uv
    check(uv[1] == f.uv[1] and uv[2] == f.uv[2] + h and
          uv[3] == f.uv[3] and uv[4] == f.uv[4], face .. " must match the root cell")
    check(head.faces[face].base.opaque == 0, face .. " must be empty by default")
  end
  for face, f in pairs(before.faces) do
    check(st.faces[face].base.hash == f.base.hash, "root " .. face .. " base changed")
    check(st.faces[face].uv[1] == f.uv[1] and st.faces[face].uv[2] == f.uv[2], "root " .. face .. " moved")
  end
  check(head.faces.FRONT.uv[2] == h + 0, "head FRONT row 0 of its band")
  spr:close()
end)

it("3 undo of newObject is one step", function()
  local spr = bodySprite()
  local w, h, nsl = spr.width, spr.height, sliceCount(spr)
  E{ action = "newObject", name = "Head", width = 8, height = 8, depth = 8 }
  check(topLayer(spr, "Object: Head"), "created")
  app.undo()
  check(not topLayer(spr, "Object: Head"), "group still there after one undo")
  check(spr.width == w and spr.height == h, "canvas not restored")
  check(sliceCount(spr) == nsl, "slices not restored")
  check(#state().objects == 1, "objects after undo")
  spr:close()
end)

it("4 actions with object= work on that object only (depth created inside its group)", function()
  local spr = bodySprite()
  E{ action = "newObject", name = "Head", width = 8, height = 8, depth = 8 }
  local before = state()
  E{ action = "flipFace", object = "Head", face = "front" }
  E{ action = "generateDepthFromBase", object = "Head" }
  E{ action = "depthDab", object = "Head", face = "front", u = 4, v = 4, radius = 1, mode = "minus" }
  E{ action = "blockLayout", object = "Head", width = 6, height = 6, depth = 6, cellW = 16, cellH = 16, cellD = 16 }
  local st = state()
  for face, f in pairs(before.faces) do
    check(st.faces[face].base.hash == f.base.hash, "root " .. face .. " base changed")
    if f.depth then check(st.faces[face].depth.hash == f.depth.hash, "root " .. face .. " depth changed") end
  end
  local head = objectOf(st, "Head")
  check(head.hasDepthLayer, "head has depth")
  check(head.faces.FRONT.base.opaque == 36, "head block 6x6: " .. head.faces.FRONT.base.opaque)
  local grp = topLayer(spr, "Object: Head")
  local depthInGroup = false
  for _, l in ipairs(grp.layers) do if l.name == "Surface_DEPTH" then depthInGroup = true end end
  check(depthInGroup, "Surface_DEPTH not inside the group")
  local rootDepth = 0
  for _, l in ipairs(spr.layers) do if l.name == "Surface_DEPTH" then rootDepth = rootDepth + 1 end end
  check(rootDepth <= 1, "duplicate root depth layers")
  spr:close()
end)

it("5 Stretch turns Arin's slab into a cube, resizes every object and undoes in one step", function()
  local spr = bodySprite(32, 16, 32)
  E{ action = "newObject", name = "Hand", block = true, bw = 32, bh = 16, bd = 32 }
  E{ action = "modelSize", width = 32, height = 32, depth = 32, mode = "stretch" }
  local st = state()
  checkBox(st.modelSize, 32, 32, 32, "stretched model")
  for _, o in ipairs(st.objects) do
    checkBox(o.box, 32, 32, 32, o.name)
    for name, f in pairs(o.faces) do
      check(f.uv[3] == 32 and f.uv[4] == 32 and f.base.opaque == 32 * 32,
            o.name .. " " .. name .. " did not stretch to full coverage")
    end
  end
  -- Hide the identical root so the projection measures the stretched Hand alone.
  E{ action = "setObject", object = "Object 1", visible = false }
  local hand = stats(180, 0)
  local pixels = hitsOf(hand, "Hand")
  check(pixels and pixels > 0, "stretched hand missing from preview")
  app.undo()
  app.undo()
  local restored = state()
  checkBox(restored.modelSize, 32, 16, 32, "model after one resize undo")
  check(objectOf(restored, "Hand").faces.FRONT.base.opaque == 32 * 16, "hand restored")
  spr:close()
  local cube = bodySprite(32, 32, 32)
  local reference = stats(180, 0)
  check(pixels == hitsOf(reference, "") and hand.outW == reference.outW and
        hand.outH == reference.outH, "stretched preview differs from a directly drawn cube")
  cube:close()
end)

it("6 Keep centres the original pixels with matching opposite views", function()
  local spr = bodySprite()
  E{ action = "newObject", name = "Head", block = true, bw = 16, bh = 16, bd = 16 }
  E{ action = "modelSize", width = 32, height = 32, depth = 32, mode = "keep" }
  local st = state()
  checkBox(st.modelSize, 32, 32, 32, "model")
  for _, o in ipairs(st.objects) do
    local parent = o.key == "" and spr or topLayer(spr, o.group)
    local base
    for _, layer in ipairs(parent.layers) do
      if layer.name == "Surface_Base" then base = layer end
    end
    local cel = base:cel(1)
    for name, f in pairs(o.faces) do
      local x0, y0 = f.uv[1] - cel.position.x, f.uv[2] - cel.position.y
      check(f.uv[3] == 32 and f.uv[4] == 32, o.name .. " " .. name .. " cell size")
      for y = 0, 31 do
        for x = 0, 31 do
          local inside = x >= 8 and x < 24 and y >= 8 and y < 24
          local opaque = app.pixelColor.rgbaA(cel.image:getPixel(x0 + x, y0 + y)) > 0
          check(opaque == inside, ("%s %s wrong margin at %d,%d"):format(o.name, name, x, y))
        end
      end
    end
  end
  spr:close()
end)

it("7 Arin's case: a head on top of the body sits above it and does not cut it", function()
  local spr = bodySprite()
  addHead()
  local red, blue = colours(spr, 180, 0)
  check(blue.n > 0, "no head pixels")
  check(blue.maxY < red.minY + 1, ("head not above body: head y %d..%d, body y %d..%d"):format(
        blue.minY, blue.maxY, red.minY, red.maxY))
  check(red.n > 0, "body missing")
  -- An angled view sees both objects too.
  local ps = stats(45, 30)
  check(hitsOf(ps, "") > 0 and hitsOf(ps, "Head") > 0, "both objects hit at 45/30")
  spr:close()
end)

it("8 coplanar overlap: the smaller object wins by default; rank flips it", function()
  local spr = bodySprite()
  -- A 16x16x4 plate whose front face is the body's front face (z 12..16).
  E{ action = "newObject", name = "Plate", block = true, bw = 16, bh = 16, bd = 4, place = "front",
     r = 30, g = 30, b = 220 }
  local red, blue = colours(spr, 180, 0)
  check(blue.n > red.n * 4, ("plate (smaller) should win: blue %d red %d"):format(blue.n, red.n))
  E{ action = "setObject", object = "Object 1", rank = 1 }
  red, blue = colours(spr, 180, 0)
  check(red.n > blue.n * 4, ("ranked body should win: blue %d red %d"):format(blue.n, red.n))
  check(objectOf(state(), "Object 1").rank == 1, "rank stored")
  spr:close()
end)

it("9 an empty object renders nothing (not a solid box)", function()
  local spr = bodySprite()
  E{ action = "newObject", name = "Empty" }
  local ps = stats(45, 30)
  check(hitsOf(ps, "Empty") == nil, "empty object rendered")
  check(hitsOf(ps, "") > 0, "body missing")
  spr:close()
end)

it("10 a hand canvas resize with objects is restored (not rescaled)", function()
  local spr = bodySprite()
  E{ action = "newObject", name = "Head", block = true, bw = 8, bh = 8, bd = 8 }
  local w, h = spr.width, spr.height
  local head0 = objectOf(state(), "Head")
  app.command.CanvasSize{ ui = false, right = 20, bottom = 30 }
  check(spr.width == w + 20 and spr.height == h + 30, "canvas resize did not happen")
  E{ action = "setupSurfaces" }
  check(spr.width == w and spr.height == h, "canvas " .. spr.width .. "x" .. spr.height)
  local head = objectOf(state(), "Head")
  check(head.faces.FRONT.base.hash == head0.faces.FRONT.base.hash, "head pixels")
  check(head.faces.FRONT.uv[3] == 16, "head not rescaled")
  spr:close()
end)

it("11 save and reopen keeps shared size, artwork and rank; offsets are ignored", function()
  local spr = bodySprite()
  E{ action = "newObject", name = "Head", block = true, bw = 8, bh = 8, bd = 8 }
  E{ action = "setObject", object = "Head", ox = 2, oy = 17, oz = 3, rank = 4 }
  local path = app.fs.joinPath(app.fs.tempPath, "easymetric_objects_test.aseprite")
  spr:saveAs(path)
  spr:close()
  local spr2 = app.open(path)
  app.activeSprite = spr2
  local head = objectOf(state(), "Head")
  check(head, "head lost")
  checkBox(head.offset, 0, 0, 0, "offset after reopen")
  checkBox(head.box, 16, 16, 16, "shared size after reopen")
  check(head.rank == 4, "rank lost")
  check(head.faces.FRONT.base.opaque == 64, "pixels lost")
  spr2:close()
  os.remove(path)
end)

it("12 a hidden object is left out of the preview; deleteObject closes its band", function()
  local spr = bodySprite()
  addHead()
  local h0 = objectOf(state(), "Head").bandY
  E{ action = "setObject", object = "Head", visible = "false" }
  check(hitsOf(stats(45, 30), "Head") == nil, "hidden head rendered")
  E{ action = "setObject", object = "Head", visible = "true" }
  check((hitsOf(stats(45, 30), "Head") or 0) > 0, "shown head missing")
  E{ action = "deleteObject", object = "Head" }
  check(not topLayer(spr, "Object: Head"), "group still there")
  check(spr.height == h0, "canvas not shrunk: " .. spr.height)
  check(#state().objects == 1, "objects")
  app.undo()
  check(topLayer(spr, "Object: Head"), "undo delete")
  spr:close()
end)

it("13 OBJ export writes one part per object, each at its place", function()
  local spr = bodySprite()
  addHead()
  local path = app.fs.joinPath(app.fs.tempPath, "easymetric_objects_test.obj")
  E{ action = "exportObj", out = path }
  local parts, cur = {}, nil
  for line in io.lines(path) do
    local name = line:match("^o (%S+)")
    if name then cur = { minY = 1e9, maxY = -1e9 }; parts[name] = cur
    elseif cur and line:sub(1, 2) == "v " then
      local y = tonumber(line:match("^v %S+ (%S+)"))
      cur.minY = math.min(cur.minY, y); cur.maxY = math.max(cur.maxY, y)
    end
  end
  check(parts.Object_1 and parts.Head, "parts missing")
  check(math.abs(parts.Head.minY - parts.Object_1.maxY) < 0.01,
        ("head should sit on the body: head y %.1f..%.1f, body y %.1f..%.1f"):format(
          parts.Head.minY, parts.Head.maxY, parts.Object_1.minY, parts.Object_1.maxY))
  for _, ext in ipairs({ "obj", "mtl", "png" }) do os.remove(app.fs.joinPath(app.fs.tempPath, "easymetric_objects_test." .. ext)) end
  spr:close()
end)

it("14 a changed object re-renders (the volume cache is keyed by content)", function()
  local spr = bodySprite()
  addHead()
  E{ action = "setObject", object = "Object 1", visible = false }
  local a = assert(hitsOf(stats(45, 30), "Head"))
  E{ action = "blockLayout", object = "Head", width = 4, height = 4, depth = 4,
     cellW = 16, cellH = 32, cellD = 16 }
  local b = assert(hitsOf(stats(45, 30), "Head"))
  check(b > 0 and b < a, ("smaller head should cover fewer pixels: %d -> %d"):format(a, b))
  check(hitsOf(stats(45, 30), "Head") == b, "repeat render must remain stable")
  app.undo()
  check(hitsOf(stats(45, 30), "Head") == a, "undo returns to the original footprint")
  spr:close()
end)

it("15 newObject writes the block on its zero-based frame only", function()
  local spr = bodySprite()
  spr:newEmptyFrame(2)
  spr:newEmptyFrame(3)
  E{ action = "newObject", name = "Hand", block = true, frame = 2 }
  local group = topLayer(spr, "Object: Hand")
  for _, layer in ipairs(group.layers) do
    for frame = 1, 2 do
      check(not layer:cel(frame) or layer:cel(frame).image:isEmpty(),
            layer.name .. " wrote pixels on Lua frame " .. frame)
    end
    if layer.name == "Surface_Base" then
      check(layer:cel(3) and not layer:cel(3).image:isEmpty(), "block missing on Lua frame 3")
    end
  end
  spr:close()
end)

it("16 unifyObjects preserves each legacy object's footprint within 5% and undoes in one step", function()
  local source = app.fs.joinPath(os.getenv("USERPROFILE"), "Downloads",
                                 "Easymetric-Shubha-Model-Test.aseprite")
  local dir = app.fs.joinPath(app.fs.tempPath,
                             ("aseprite-shared-size-%d-%d"):format(os.time(), math.random(1000000000)))
  local copy = app.fs.joinPath(dir, "legacy.aseprite")
  local input = assert(io.open(source, "rb"))
  local bytes = input:read("a")
  input:close()
  check(app.fs.makeDirectory(dir), "could not create fixture directory")
  local spr
  local ok, err = pcall(function()
    local output = assert(io.open(copy, "wb"))
    assert(output:write(bytes))
    output:close()
    spr = assert(app.open(copy))
    app.activeSprite = spr
    local before = state()
    check(#before.objects == 3, "expected the three legacy parts")
    local legacy = false
    for _, o in ipairs(before.objects) do
      if o.offset[1] ~= 0 or o.offset[2] ~= 0 or o.offset[3] ~= 0 then legacy = true end
    end
    check(legacy, "fixture already migrated")
    local old = stats(45, 30)
    check(#old.objects == #before.objects, "expected all legacy objects in the preview")
    E{ action = "unifyObjects" }
    local migrated = state()
    local new = stats(45, 30)
    check(#migrated.objects == #before.objects, "migration dropped an object")
    check(#new.objects == #old.objects, "migration changed the visible object count")
    for _, o in ipairs(migrated.objects) do
      checkBox(o.offset, 0, 0, 0, o.name .. " offset")
      local size = migrated.modelSize
      checkBox(o.box, size[1], size[2], size[3], o.name .. " shared size")
    end
    for _, o in ipairs(old.objects) do
      local px = hitsOf(new, o.key)
      check(px and math.abs(px - o.px) <= o.px * 0.05,
            ("%s coverage: %d -> %s (expected +/-5%%)"):format(o.key, o.px, tostring(px)))
    end
    app.undo()
    for _, o in ipairs(state().objects) do
      local prev = assert(objectOf(before, o.key))
      checkBox(o.offset, prev.offset[1], prev.offset[2], prev.offset[3], o.name .. " undo offset")
      checkBox(o.box, prev.box[1], prev.box[2], prev.box[3], o.name .. " undo size")
    end
  end)
  if spr then spr:close() end
  os.remove(copy)
  app.fs.removeDirectory(dir)
  check(ok, err)
end)

print(string.format("TOTAL: %d Passed, %d Failed", suite.passed, suite.failed))
