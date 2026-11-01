-- Aseprite 2.0 AI Bridge Extension
-- Implements WebSocket client for MCP server integration (PROTOCOL.md v1)

local bridge = {
  version = "1.0.0",
  protocol = 1,
  port = 8765,
  ws = nil,
  connected = false,
  allow_lua_exec = true,
  debug = false,
  timer = nil,
  token = nil
}

local function log_debug(msg)
  if not bridge.debug then return end
  local f = io.open("c:/Users/leero/Source/Repos/aseprite2/bridge_debug.log", "a")
  if f then
    f:write(os.date() .. ": " .. tostring(msg) .. "\n")
    f:close()
  end
end
log_debug("bridge.lua loaded into Lua engine")

-- Fast Lua 5.3+ bitwise Base64 encode / decode
local b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local function base64_encode(data)
  if not data or #data == 0 then return "" end
  local t = {}
  local len = #data
  for i = 1, len, 3 do
    local b1, b2, b3 = data:byte(i, i + 2)
    b2 = b2 or 0
    b3 = b3 or 0
    local n = (b1 << 16) | (b2 << 8) | b3
    table.insert(t, b64chars:sub(((n >> 18) & 63) + 1, ((n >> 18) & 63) + 1))
    table.insert(t, b64chars:sub(((n >> 12) & 63) + 1, ((n >> 12) & 63) + 1))
    if i + 1 <= len then
      table.insert(t, b64chars:sub(((n >> 6) & 63) + 1, ((n >> 6) & 63) + 1))
    else
      table.insert(t, "=")
    end
    if i + 2 <= len then
      table.insert(t, b64chars:sub((n & 63) + 1, (n & 63) + 1))
    else
      table.insert(t, "=")
    end
  end
  return table.concat(t)
end

local b64dec = {}
for i = 1, 64 do b64dec[b64chars:sub(i, i):byte()] = i - 1 end
local function base64_decode(data)
  if not data or #data == 0 then return "" end
  local clean = data:gsub("[^A-Za-z0-9+/=]", "")
  local t = {}
  local len = #clean
  for i = 1, len, 4 do
    local c1, c2, c3, c4 = clean:byte(i, i + 3)
    local v1 = b64dec[c1] or 0
    local v2 = b64dec[c2] or 0
    local v3 = b64dec[c3] or 0
    local v4 = b64dec[c4] or 0
    local n = (v1 << 18) | (v2 << 12) | (v3 << 6) | v4
    table.insert(t, string.char((n >> 16) & 255))
    if c3 ~= 61 then table.insert(t, string.char((n >> 8) & 255)) end
    if c4 ~= 61 then table.insert(t, string.char(n & 255)) end
  end
  return table.concat(t)
end

-- Read or create token in <userConfigPath>/mcp/token
local function get_or_create_token()
  if bridge.token then return bridge.token end
  local configDir = app.fs.userConfigPath
  local mcpDir = app.fs.joinPath(configDir, "mcp")
  if not app.fs.isDirectory(mcpDir) then
    app.fs.makeAllDirectories(mcpDir)
  end
  local tokenPath = app.fs.joinPath(mcpDir, "token")
  if app.fs.isFile(tokenPath) then
    local f = io.open(tokenPath, "r")
    if f then
      local t = f:read("*a")
      f:close()
      if t and #t:gsub("%s+", "") > 0 then
        bridge.token = t:gsub("%s+", "")
        return bridge.token
      end
    end
  end

  -- Generate new token
  local chars = "0123456789abcdef"
  local gen = {}
  math.randomseed(os.time())
  for _ = 1, 32 do
    local idx = math.random(1, #chars)
    table.insert(gen, chars:sub(idx, idx))
  end
  bridge.token = table.concat(gen)
  local f = io.open(tokenPath, "w")
  if f then
    f:write(bridge.token)
    f:close()
  end
  return bridge.token
end

-- Helper: resolve sprite
local function resolve_sprite(param)
  if not param then return app.activeSprite end
  if type(param) == "string" and param:sub(1, 1) == "#" then
    local idx = tonumber(param:sub(2))
    if idx and app.sprites[idx + 1] then
      return app.sprites[idx + 1]
    end
  end
  for _, s in ipairs(app.sprites) do
    if s.filename == param then return s end
  end
  return app.activeSprite
end

-- Helper: resolve layer
local function resolve_layer(spr, param)
  if not spr then return nil end
  if not param then return app.activeLayer end
  if type(param) == "string" and param:sub(1, 1) == "#" then
    local idx = tonumber(param:sub(2))
    if idx and spr.layers[idx + 1] then
      return spr.layers[idx + 1]
    end
  end
  for _, l in ipairs(spr.layers) do
    if l.name == param then return l end
  end
  return app.activeLayer
end

-- Helper: find layer strictly by name (returns nil if not found)
local function find_layer_by_name(spr, name)
  if not spr or not name then return nil end
  for _, l in ipairs(spr.layers) do
    if l.name == name then return l end
  end
  return nil
end

-- Helper: parse color
local function parse_color(c)
  if not c then return Color(0, 0, 0, 255) end
  if type(c) == "number" then return Color(c) end
  if type(c) == "string" then
    c = c:gsub("#", "")
    if #c == 6 then
      local r = tonumber(c:sub(1, 2), 16) or 0
      local g = tonumber(c:sub(3, 4), 16) or 0
      local b = tonumber(c:sub(5, 6), 16) or 0
      return Color(r, g, b, 255)
    elseif #c == 8 then
      local r = tonumber(c:sub(1, 2), 16) or 0
      local g = tonumber(c:sub(3, 4), 16) or 0
      local b = tonumber(c:sub(5, 6), 16) or 0
      local a = tonumber(c:sub(7, 8), 16) or 255
      return Color(r, g, b, a)
    end
  end
  return Color(0, 0, 0, 255)
end

-- Method Handlers
local handlers = {}
_G.Aseprite2Bridge = { handlers = handlers }

handlers["app.status"] = function(p)
  local list = {}
  for i, s in ipairs(app.sprites) do
    table.insert(list, {
      index = i - 1,
      filename = s.filename,
      width = s.width,
      height = s.height,
      frames = #s.frames,
      layers = #s.layers,
      modified = s.isModified
    })
  end
  return {
    app = "Aseprite 2.0",
    version = tostring(app.version),
    protocol = bridge.protocol,
    activeSprite = app.activeSprite and app.activeSprite.filename or nil,
    sprites = list,
    capabilities = { "versions", "pixellab", "easymetric", "normals_depth", bridge.allow_lua_exec and "lua.exec" or nil }
  }
end

handlers["app.list_commands"] = function(p)
  local cmds = {
    "NewFile", "OpenFile", "SaveFile", "SaveFileAs", "SaveFileCopyAs",
    "SaveVersion", "VersionHistory", "RestoreVersion", "OpenVersion", "DeleteVersion",
    "Undo", "Redo", "SpriteProperties", "CanvasSize", "SpriteSize",
    "NewLayer", "RemoveLayer", "NewFrame", "RemoveFrame",
    "ExportSpriteSheet", "ImportSpriteSheet",
    "EasymetricProjection", "EasymetricCreateTemplate", "SurfaceNormalsDepth", "ExportPreviewSheet"
  }
  return { commands = cmds }
end

handlers["app.run_command"] = function(p)
  if not p or not p.name then error("Missing command name") end
  local cmdParams = p.params or {}
  app.command[p.name](cmdParams)
  return { ok = true }
end

handlers["app.undo"] = function(p)
  local steps = (p and p.steps) or 1
  for _ = 1, steps do app.command.Undo() end
  return { ok = true }
end

handlers["app.redo"] = function(p)
  local steps = (p and p.steps) or 1
  for _ = 1, steps do app.command.Redo() end
  return { ok = true }
end

handlers["sprite.list"] = function(p)
  local list = {}
  for i, s in ipairs(app.sprites) do
    table.insert(list, {
      index = i - 1,
      filename = s.filename,
      width = s.width,
      height = s.height,
      frames = #s.frames,
      layers = #s.layers,
      modified = s.isModified
    })
  end
  return { sprites = list }
end

handlers["sprite.info"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end

  local frames = {}
  for i, f in ipairs(spr.frames) do
    table.insert(frames, { n = i, duration = f.duration })
  end

  local layers = {}
  for i, l in ipairs(spr.layers) do
    table.insert(layers, {
      index = i - 1,
      name = l.name,
      visible = l.isVisible,
      opacity = l.opacity,
      isGroup = l.isGroup
    })
  end

  local tags = {}
  for _, t in ipairs(spr.tags) do
    table.insert(tags, { name = t.name, from = t.fromFrame.frameNumber, to = t.toFrame.frameNumber })
  end

  return {
    filename = spr.filename,
    width = spr.width,
    height = spr.height,
    colorMode = tostring(spr.colorMode),
    frames = frames,
    layers = layers,
    tags = tags
  }
end

handlers["sprite.new"] = function(p)
  p = p or {}
  local w = p.width or 32
  local h = p.height or 32
  local mode = ColorMode.RGB
  if p.colorMode == "indexed" then mode = ColorMode.INDEXED
  elseif p.colorMode == "gray" or p.colorMode == "grayscale" then mode = ColorMode.GRAY
  end
  local spr = Sprite(w, h, mode)
  if p.filename and #p.filename > 0 then
    spr.filename = p.filename
  end
  app.activeSprite = spr
  return { filename = spr.filename, width = spr.width, height = spr.height }
end

handlers["sprite.open"] = function(p)
  if not p or not p.path then error("Missing path") end
  local spr = app.open(p.path)
  if not spr then error("Failed to open sprite: " .. p.path) end
  app.activeSprite = spr
  return { filename = spr.filename, width = spr.width, height = spr.height }
end

handlers["sprite.save"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  if p and p.path then
    spr:saveAs(p.path)
  else
    spr:saveCopyAs(spr.filename)
  end
  return { filename = spr.filename }
end

handlers["sprite.export"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  if not p or not p.path then error("Missing export path") end
  spr:saveCopyAs(p.path)
  return { path = p.path }
end

handlers["sprite.close"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  spr:close()
  return { ok = true }
end

handlers["sprite.set_active"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("Sprite not found") end
  app.activeSprite = spr
  return { ok = true }
end

handlers["sprite.resize"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  spr:resize(p.width, p.height)
  return { ok = true, width = spr.width, height = spr.height }
end

handlers["sprite.canvas_size"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local left = p.left or 0
  local top = p.top or 0
  local right = p.right or 0
  local bottom = p.bottom or 0
  local newW = spr.width + left + right
  local newH = spr.height + top + bottom
  spr:crop(Rectangle(-left, -top, newW, newH))
  return { ok = true, width = spr.width, height = spr.height }
end

handlers["layer.add"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local layer
  if p and p.group then
    layer = spr:newGroup()
  else
    layer = spr:newLayer()
  end
  if p and p.name then layer.name = p.name end
  app.activeLayer = layer
  return { name = layer.name }
end

handlers["layer.delete"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  local layer = resolve_layer(spr, p and p.layer)
  if not layer then error("Layer not found") end
  spr:deleteLayer(layer)
  return { ok = true }
end

handlers["layer.set"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  local layer = resolve_layer(spr, p and p.layer)
  if not layer then error("Layer not found") end
  if p.name then layer.name = p.name end
  if p.visible ~= nil then layer.isVisible = p.visible end
  if p.opacity ~= nil then layer.opacity = p.opacity end
  return { ok = true }
end

handlers["frame.add"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local frame = spr:newFrame()
  if p and p.duration then frame.duration = p.duration end
  app.activeFrame = frame
  return { frameNumber = frame.frameNumber }
end

handlers["frame.delete"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local f = (p and p.frame) or (app.activeFrame and app.activeFrame.frameNumber)
  if not f or not spr.frames[f] then error("Frame not found") end
  spr:deleteFrame(spr.frames[f])
  return { ok = true }
end

handlers["cel.get_image"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local frIdx = (p and p.frame) or (app.activeFrame and app.activeFrame.frameNumber) or 1
  local fr = spr.frames[frIdx] or spr.frames[1]
  local img

  if p and p.flatten then
    img = Image(spr.spec)
    img:drawSprite(spr, fr)
  else
    local layer = resolve_layer(spr, p and p.layer)
    if not layer then error("No active layer") end
    local cel = layer:cel(fr)
    if not cel then
      return { png_base64 = "", width = 0, height = 0, x = 0, y = 0 }
    end
    img = cel.image
  end

  local tmp = app.fs.joinPath(app.fs.tempPath, "ase2_tmp_" .. os.time() .. ".png")
  img:saveAs(tmp)
  local f = io.open(tmp, "rb")
  if not f then error("Failed to read rendered image") end
  local raw = f:read("*a")
  f:close()
  os.remove(tmp)

  return {
    png_base64 = base64_encode(raw),
    width = img.width,
    height = img.height
  }
end

handlers["cel.put_image"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  if not p or not p.png_base64 then error("Missing png_base64") end

  local raw = base64_decode(p.png_base64)
  local tmp = app.fs.joinPath(app.fs.tempPath, "ase2_put_" .. os.time() .. ".png")
  local f = io.open(tmp, "wb")
  if not f then error("Failed to write temp image") end
  f:write(raw)
  f:close()

  local newImg = Image{ fromFile = tmp }
  os.remove(tmp)
  if not newImg then error("Failed to decode image") end

  local layer
  if p.newLayer then
    layer = spr:newLayer()
    layer.name = p.newLayer
  else
    layer = resolve_layer(spr, p.layer)
  end
  if not layer then error("No layer to place image") end

  local frIdx = p.frame or (app.activeFrame and app.activeFrame.frameNumber) or 1
  local fr = spr.frames[frIdx] or spr.frames[1]
  local x = p.x or 0
  local y = p.y or 0

  spr:newCel(layer, fr, newImg, Point(x, y))
  return { ok = true, layer = layer.name }
end

handlers["draw.pixels"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local layer = resolve_layer(spr, p and p.layer)
  local frIdx = (p and p.frame) or (app.activeFrame and app.activeFrame.frameNumber) or 1
  local fr = spr.frames[frIdx] or spr.frames[1]

  local cel = layer:cel(fr)
  if not cel then
    local img = Image(spr.spec)
    cel = spr:newCel(layer, fr, img, Point(0, 0))
  end

  local img = cel.image
  local count = 0
  if p and p.pixels then
    for _, px in ipairs(p.pixels) do
      local c = parse_color(px.color)
      img:drawPixel(px.x - cel.position.x, px.y - cel.position.y, c)
      count = count + 1
    end
  end
  app.refresh()
  return { count = count }
end

handlers["palette.get"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  local pal = spr.palettes[1]
  local colors = {}
  if pal then
    for i = 0, #pal - 1 do
      local c = pal:getColor(i)
      table.insert(colors, string.format("#%02X%02X%02X%02X", c.red, c.green, c.blue, c.alpha))
    end
  end
  return { colors = colors }
end

handlers["palette.set"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr then error("No active sprite") end
  if not p or not p.colors then error("Missing colors") end
  local pal = spr.palettes[1]
  if not pal then pal = spr:newPalette(#p.colors) end
  pal:resize(#p.colors)
  for i, cHex in ipairs(p.colors) do
    pal:setColor(i - 1, parse_color(cHex))
  end
  return { ok = true }
end

handlers["version.save"] = function(p)
  p = p or {}
  app.command.SaveVersion{
    note = p.note or "",
    author = p.author or "agent",
    ui = false
  }
  return { ok = true }
end

handlers["version.list"] = function(p)
  local spr = resolve_sprite(p and p.sprite)
  if not spr or not spr.filename or #spr.filename == 0 then
    return { versions = {} }
  end
  local versionsDir = spr.filename .. ".versions"
  local manifestPath = app.fs.joinPath(versionsDir, "manifest.json")
  if not app.fs.isFile(manifestPath) then
    return { versions = {} }
  end
  local f = io.open(manifestPath, "r")
  if not f then return { versions = {} } end
  local content = f:read("*a")
  f:close()
  local ok, parsed = pcall(json.decode, content)
  local list = {}
  if ok and parsed and parsed.versions then
    for i = 1, #parsed.versions do
      local v = parsed.versions[i]
      table.insert(list, {
        id = v.id,
        created = tostring(v.created),
        note = tostring(v.note or ""),
        author = tostring(v.author or ""),
        file = tostring(v.file),
        thumbnail = tostring(v.thumbnail or ""),
        width = v.width,
        height = v.height,
        frames = v.frames,
        layers = v.layers,
        bytes = v.bytes
      })
    end
  end
  return { versions = list }
end

handlers["version.restore"] = function(p)
  if not p or not p.id then error("Missing version id") end
  app.command.RestoreVersion{ id = tostring(p.id) }
  return { ok = true }
end

handlers["version.open"] = function(p)
  if not p or not p.id then error("Missing version id") end
  app.command.OpenVersion{ id = tostring(p.id) }
  return { ok = true }
end

handlers["version.delete"] = function(p)
  if not p or not p.id then error("Missing version id") end
  app.command.DeleteVersion{ id = tostring(p.id) }
  return { ok = true }
end

handlers["pixellab.status"] = function(p)
  local extDir = app.fs.joinPath(app.fs.userConfigPath, "extensions", "pixellab")
  local installed = app.fs.isDirectory(extDir)
  return { installed = installed }
end

handlers["pixellab.list_commands"] = function(p)
  return {
    commands = {
      { id = "pixellab_character", title = "Generate Character" },
      { id = "pixellab_animation", title = "Generate Animation" },
      { id = "pixellab_tileset", title = "Generate Tileset" },
      { id = "pixellab_rotate", title = "Generate Rotations" }
    }
  }
end

handlers["pixellab.run"] = function(p)
  if not p or not p.command then error("Missing pixellab command") end
  if app.command[p.command] then
    app.command[p.command]()
    return { ok = true }
  end
  error("PixelLab command not found: " .. tostring(p.command))
end

handlers["lua.exec"] = function(p)
  if not bridge.allow_lua_exec then error("lua.exec is disabled in AI Bridge settings") end
  if not p or not p.code then error("Missing code") end
  local fn, compileErr = load(p.code)
  if not fn then error("Compilation error: " .. tostring(compileErr)) end
  local res = fn()
  return { result = res }
end

-- Helper: Nearest-neighbor upscale of an Image using native C++ Image:resize
local function upscale_image_nn(srcImg, scale)
  scale = math.max(1, math.floor(scale or 1))
  local dst = srcImg:clone()
  if scale > 1 then
    dst:resize(srcImg.width * scale, srcImg.height * scale)
  end
  return dst
end

-- Helper: Fast native C++ alpha blit via Image:drawImage
local function blit_image_alpha(dstImg, srcImg, offsetX, offsetY)
  dstImg:drawImage(srcImg, Point(offsetX, offsetY), 255, BlendMode.NORMAL)
end

-- 1. Native Preview Sheet & Multi-Scale Exporter (1x + 4x contact sheet)
handlers["sprite.export_preview_sheet"] = function(p)
  p = p or {}
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end

  local scale = math.max(1, math.floor(p.scale or 4))
  local include1x = (p.include1x ~= false)
  local pad = math.max(4, math.floor(p.padding or 10))
  local mode = p.mode or ((#spr.frames > 1) and "frames" or "layers")

  local items = {}
  if mode == "layers" and #spr.layers > 1 then
    local savedVis = {}
    for i, l in ipairs(spr.layers) do savedVis[i] = l.isVisible end
    for i, l in ipairs(spr.layers) do
      if not l.isGroup then
        for j, other in ipairs(spr.layers) do other.isVisible = (i == j) end
        local flat = Image(spr.width, spr.height, ColorMode.RGB)
        flat:drawSprite(spr, app.activeFrame or spr.frames[1])
        table.insert(items, { name = l.name, image = flat })
      end
    end
    for i, l in ipairs(spr.layers) do l.isVisible = savedVis[i] end
  else
    for i, fr in ipairs(spr.frames) do
      local flat = Image(spr.width, spr.height, ColorMode.RGB)
      flat:drawSprite(spr, fr)
      table.insert(items, { name = "Frame " .. tostring(i), image = flat })
    end
  end

  if #items == 0 then
    local flat = Image(spr.width, spr.height, ColorMode.RGB)
    flat:drawSprite(spr, spr.frames[1])
    table.insert(items, { name = "Sprite", image = flat })
  end

  local sw = spr.width
  local sh = spr.height
  local scaledW = sw * scale
  local scaledH = sh * scale
  local extra1xW = include1x and (sw + pad) or 0
  local cardW = scaledW + extra1xW + pad * 2
  local cardH = math.max(scaledH, sh) + pad * 2

  local cols = math.min(#items, math.max(1, math.floor(p.columns or 4)))
  local rows = math.ceil(#items / cols)
  local sheetW = cols * cardW + (cols + 1) * pad
  local sheetH = rows * cardH + (rows + 1) * pad

  local sheet = Image(sheetW, sheetH, ColorMode.RGB)
  local bgOuter = app.pixelColor.rgba(20, 20, 28, 255)
  local chkDark = app.pixelColor.rgba(32, 32, 44, 255)
  local chkLight = app.pixelColor.rgba(44, 44, 58, 255)
  local borderCol = app.pixelColor.rgba(82, 88, 115, 255)

  sheet:clear(bgOuter)

  -- Build card background template once and blit in C++ for each item
  local cardTemplate = Image(cardW, cardH, ColorMode.RGB)
  for y = 0, cardH - 1 do
    for x = 0, cardW - 1 do
      if x == 0 or y == 0 or x == cardW - 1 or y == cardH - 1 then
        cardTemplate:drawPixel(x, y, borderCol)
      else
        local cell = ((math.floor(x / 8) + math.floor(y / 8)) % 2 == 0) and chkDark or chkLight
        cardTemplate:drawPixel(x, y, cell)
      end
    end
  end

  for idx, item in ipairs(items) do
    local col = (idx - 1) % cols
    local row = math.floor((idx - 1) / cols)
    local cx = pad + col * (cardW + pad)
    local cy = pad + row * (cardH + pad)

    sheet:drawImage(cardTemplate, Point(cx, cy))

    -- Blit scaled sprite
    local upImg = upscale_image_nn(item.image, scale)
    local upX = cx + pad
    local upY = cy + math.floor((cardH - scaledH) / 2)
    blit_image_alpha(sheet, upImg, upX, upY)

    -- Blit 1x actual size preview beside scaled sprite
    if include1x then
      local oneX = upX + scaledW + pad
      local oneY = cy + pad
      blit_image_alpha(sheet, item.image, oneX, oneY)
    end
  end

  local outPath = p.path
  if not outPath or #outPath == 0 then
    if spr.filename and #spr.filename > 0 then
      local dir = app.fs.filePath(spr.filename)
      local title = app.fs.fileTitle(spr.filename)
      outPath = app.fs.joinPath(dir, title .. "_preview_sheet.png")
    else
      outPath = app.fs.joinPath(app.fs.tempPath, "ase2_preview_sheet.png")
    end
  end

  sheet:saveAs(outPath)

  local exportedIndividuals = {}
  if p.exportIndividual and spr.filename and #spr.filename > 0 then
    local dir = app.fs.filePath(spr.filename)
    local title = app.fs.fileTitle(spr.filename)
    for idx, item in ipairs(items) do
      local safeName = item.name:gsub("[^%w_%-]", "_")
      local p1x = app.fs.joinPath(dir, string.format("%s_%02d_%s_1x.png", title, idx, safeName))
      local pScaled = app.fs.joinPath(dir, string.format("%s_%02d_%s_%dx.png", title, idx, safeName, scale))
      item.image:saveAs(p1x)
      upscale_image_nn(item.image, scale):saveAs(pScaled)
      table.insert(exportedIndividuals, { name = item.name, path1x = p1x, pathScaled = pScaled })
    end
  end

  local b64 = ""
  if p.returnBase64 then
    local f = io.open(outPath, "rb")
    if f then
      b64 = base64_encode(f:read("*a"))
      f:close()
    end
  end

  return {
    ok = true,
    path = outPath,
    width = sheetW,
    height = sheetH,
    scale = scale,
    itemCount = #items,
    individuals = exportedIndividuals,
    png_base64 = b64
  }
end

-- 2. Centered Canvas Expand
handlers["sprite.canvas_expand_centered"] = function(p)
  if not p or not p.width or not p.height then error("Missing width or height") end
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end

  local oldW = spr.width
  local oldH = spr.height
  local newW = math.floor(p.width)
  local newH = math.floor(p.height)
  local padLeft = math.floor((newW - oldW) / 2)
  local padTop = math.floor((newH - oldH) / 2)

  spr:crop(Rectangle(-padLeft, -padTop, newW, newH))
  app.refresh()
  return {
    ok = true,
    oldWidth = oldW,
    oldHeight = oldH,
    width = spr.width,
    height = spr.height,
    offsetX = padLeft,
    offsetY = padTop
  }
end

-- 3. Rarity Ramp Swapper (recolor_ramp)
handlers["draw.recolor_ramp"] = function(p)
  if not p or not p.mapping then error("Missing color mapping array") end
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  local layer = resolve_layer(spr, p.layer)
  if not layer then error("Layer not found") end

  local rules = {}
  for _, m in ipairs(p.mapping) do
    local fc = parse_color(m.from)
    local tc = parse_color(m.to)
    table.insert(rules, {
      fr = fc.red, fg = fc.green, fb = fc.blue,
      tr = tc.red, tg = tc.green, tb = tc.blue, ta = tc.alpha,
      preserveAlpha = (m.preserveAlpha ~= false),
      tolerance = m.tolerance or 0
    })
  end

  local framesToProcess = {}
  if p.allFrames then
    for _, fr in ipairs(spr.frames) do table.insert(framesToProcess, fr) end
  else
    local frIdx = p.frame or (app.activeFrame and app.activeFrame.frameNumber) or 1
    if spr.frames[frIdx] then table.insert(framesToProcess, spr.frames[frIdx]) end
  end

  local replacedCount = 0
  for _, fr in ipairs(framesToProcess) do
    local cel = layer:cel(fr)
    if cel and cel.image then
      local img = cel.image
      for y = 0, img.height - 1 do
        for x = 0, img.width - 1 do
          local px = img:getPixel(x, y)
          local a = app.pixelColor.rgbaA(px)
          if a > 0 then
            local r = app.pixelColor.rgbaR(px)
            local g = app.pixelColor.rgbaG(px)
            local b = app.pixelColor.rgbaB(px)
            for _, rule in ipairs(rules) do
              local dist = math.abs(r - rule.fr) + math.abs(g - rule.fg) + math.abs(b - rule.fb)
              if dist <= rule.tolerance then
                local outA = rule.preserveAlpha and a or rule.ta
                img:drawPixel(x, y, app.pixelColor.rgba(rule.tr, rule.tg, rule.tb, outA))
                replacedCount = replacedCount + 1
                break
              end
            end
          end
        end
      end
    end
  end

  app.refresh()
  return { ok = true, replacedPixels = replacedCount }
end

-- 4. Auto-Outline / Glow Aura Generator
handlers["draw.auto_outline"] = function(p)
  p = p or {}
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  local layer = resolve_layer(spr, p.layer)
  if not layer then error("Layer not found") end

  local frIdx = p.frame or (app.activeFrame and app.activeFrame.frameNumber) or 1
  local fr = spr.frames[frIdx] or spr.frames[1]
  local cel = layer:cel(fr)
  if not cel then error("No cel at target layer/frame") end

  local col = parse_color(p.color or "#1A1A22")
  local outPixel = app.pixelColor.rgba(col.red, col.green, col.blue, col.alpha)
  local includeCorners = (p.corners == true)

  -- Ensure cel image covers full canvas so 1px outer outline never clips at cel bounds
  local fullImg = Image(spr.width, spr.height, spr.colorMode)
  fullImg:drawImage(cel.image, cel.position)

  local dirs = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } }
  if includeCorners then
    table.insert(dirs, { -1, -1 })
    table.insert(dirs, { 1, -1 })
    table.insert(dirs, { -1, 1 })
    table.insert(dirs, { 1, 1 })
  end

  local toDraw = {}
  for y = 0, spr.height - 1 do
    for x = 0, spr.width - 1 do
      local a = app.pixelColor.rgbaA(fullImg:getPixel(x, y))
      if a == 0 then
        local touchesOpaque = false
        for _, d in ipairs(dirs) do
          local nx, ny = x + d[1], y + d[2]
          if nx >= 0 and nx < spr.width and ny >= 0 and ny < spr.height then
            if app.pixelColor.rgbaA(fullImg:getPixel(nx, ny)) > 0 then
              touchesOpaque = true
              break
            end
          end
        end
        if touchesOpaque then
          table.insert(toDraw, { x = x, y = y })
        end
      end
    end
  end

  for _, pt in ipairs(toDraw) do
    fullImg:drawPixel(pt.x, pt.y, outPixel)
  end

  cel.image = fullImg
  cel.position = Point(0, 0)
  app.refresh()
  return { ok = true, addedPixels = #toDraw }
end

-- 5. Batch Shape & Flood-Fill Primitives
handlers["draw.shapes"] = function(p)
  if not p or not p.shapes then error("Missing shapes array") end
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  local layer = resolve_layer(spr, p.layer)
  if not layer then error("Layer not found") end

  local frIdx = p.frame or (app.activeFrame and app.activeFrame.frameNumber) or 1
  local fr = spr.frames[frIdx] or spr.frames[1]
  local cel = layer:cel(fr)
  if not cel then
    cel = spr:newCel(layer, fr, Image(spr.spec), Point(0, 0))
  elseif cel.position.x ~= 0 or cel.position.y ~= 0 or cel.image.width < spr.width or cel.image.height < spr.height then
    local fullImg = Image(spr.spec)
    blit_image_alpha(fullImg, cel.image, cel.position.x, cel.position.y)
    cel.image = fullImg
    cel.position = Point(0, 0)
  end

  local img = cel.image
  local drawnShapes = 0

  local function put_px(x, y, rgbaVal)
    if x >= 0 and x < img.width and y >= 0 and y < img.height then
      img:drawPixel(x, y, rgbaVal)
    end
  end

  for _, s in ipairs(p.shapes) do
    local c = parse_color(s.color or "#FFFFFF")
    local rgbaVal = app.pixelColor.rgba(c.red, c.green, c.blue, c.alpha)
    local stype = s.type or "rect"

    if stype == "line" then
      local x0, y0 = math.floor(s.x1 or 0), math.floor(s.y1 or 0)
      local x1, y1 = math.floor(s.x2 or 0), math.floor(s.y2 or 0)
      local dx = math.abs(x1 - x0)
      local sx = (x0 < x1) and 1 or -1
      local dy = -math.abs(y1 - y0)
      local sy = (y0 < y1) and 1 or -1
      local err = dx + dy
      while true do
        put_px(x0, y0, rgbaVal)
        if x0 == x1 and y0 == y1 then break end
        local e2 = 2 * err
        if e2 >= dy then err = err + dy; x0 = x0 + sx end
        if e2 <= dx then err = err + dx; y0 = y0 + sy end
      end
      drawnShapes = drawnShapes + 1
    elseif stype == "rect" or stype == "fill_rect" then
      local rx = math.floor(s.x or 0)
      local ry = math.floor(s.y or 0)
      local rw = math.floor(s.w or 1)
      local rh = math.floor(s.h or 1)
      local fill = (stype == "fill_rect") or (s.fill == true)
      for y = ry, ry + rh - 1 do
        for x = rx, rx + rw - 1 do
          if fill or x == rx or x == rx + rw - 1 or y == ry or y == ry + rh - 1 then
            put_px(x, y, rgbaVal)
          end
        end
      end
      drawnShapes = drawnShapes + 1
    elseif stype == "ellipse" or stype == "fill_ellipse" then
      local rx = math.floor(s.x or 0)
      local ry = math.floor(s.y or 0)
      local rw = math.max(1, math.floor(s.w or 1))
      local rh = math.max(1, math.floor(s.h or 1))
      local fill = (stype == "fill_ellipse") or (s.fill == true)
      local cx = rx + (rw - 1) / 2.0
      local cy = ry + (rh - 1) / 2.0
      local a = rw / 2.0
      local b = rh / 2.0
      for y = ry, ry + rh - 1 do
        for x = rx, rx + rw - 1 do
          local nx = (x - cx) / a
          local ny = (y - cy) / b
          local d = nx * nx + ny * ny
          if fill then
            if d <= 1.0 then put_px(x, y, rgbaVal) end
          else
            if d <= 1.0 and d >= 0.65 then put_px(x, y, rgbaVal) end
          end
        end
      end
      drawnShapes = drawnShapes + 1
    elseif stype == "flood_fill" then
      local sx = math.floor(s.x or 0)
      local sy = math.floor(s.y or 0)
      if sx >= 0 and sx < img.width and sy >= 0 and sy < img.height then
        local targetCol = img:getPixel(sx, sy)
        if targetCol ~= rgbaVal then
          local q = { { sx, sy } }
          local visited = {}
          local head = 1
          while head <= #q do
            local pt = q[head]
            head = head + 1
            local px, py = pt[1], pt[2]
            local key = py * img.width + px
            if not visited[key] and px >= 0 and px < img.width and py >= 0 and py < img.height then
              visited[key] = true
              if img:getPixel(px, py) == targetCol then
                img:drawPixel(px, py, rgbaVal)
                table.insert(q, { px - 1, py })
                table.insert(q, { px + 1, py })
                table.insert(q, { px, py - 1 })
                table.insert(q, { px, py + 1 })
              end
            end
          end
        end
        drawnShapes = drawnShapes + 1
      end
    end
  end

  app.refresh()
  return { ok = true, drawnShapes = drawnShapes }
end

-- 6. Version Visual Diff via Bridge
handlers["version.diff"] = function(p)
  if not p or not p.id then error("Missing version id") end
  local spr = resolve_sprite(p.sprite)
  if not spr or not spr.filename or #spr.filename == 0 then
    error("Active sprite must be saved to disk to compare versions")
  end

  local versionsDir = spr.filename .. ".versions"
  local manifestPath = app.fs.joinPath(versionsDir, "manifest.json")
  if not app.fs.isFile(manifestPath) then error("No versions manifest found") end

  local f = io.open(manifestPath, "r")
  if not f then error("Cannot read manifest.json") end
  local content = f:read("*a")
  f:close()

  local ok, parsed = pcall(json.decode, content)
  if not ok or not parsed or not parsed.versions then error("Invalid manifest.json") end

  local targetFile = nil
  local targetId = tonumber(p.id)
  for i = 1, #parsed.versions do
    local v = parsed.versions[i]
    if tonumber(v.id) == targetId then
      targetFile = tostring(v.file)
      break
    end
  end
  if not targetFile then error("Version #" .. tostring(p.id) .. " not found") end

  local snapPath = app.fs.joinPath(versionsDir, targetFile)
  if not app.fs.isFile(snapPath) then error("Version file missing: " .. snapPath) end

  local frIdx = p.frame or 1
  local curImg = Image(spr.width, spr.height, ColorMode.RGB)
  curImg:drawSprite(spr, spr.frames[math.min(frIdx, #spr.frames)])

  local prevActive = app.activeSprite
  local verSpr = app.open(snapPath)
  if not verSpr then error("Failed to load snapshot sprite") end
  local verImg = Image(verSpr.width, verSpr.height, ColorMode.RGB)
  verImg:drawSprite(verSpr, verSpr.frames[math.min(frIdx, #verSpr.frames)])
  verSpr:close()
  if prevActive then app.activeSprite = prevActive end

  local maxW = math.max(curImg.width, verImg.width)
  local maxH = math.max(curImg.height, verImg.height)
  local diffImg = Image(maxW, maxH, ColorMode.RGB)

  local changed, added, removed, modified = 0, 0, 0, 0
  for y = 0, maxH - 1 do
    for x = 0, maxW - 1 do
      local cCur = (x < curImg.width and y < curImg.height) and curImg:getPixel(x, y) or 0
      local cVer = (x < verImg.width and y < verImg.height) and verImg:getPixel(x, y) or 0
      local aCur = app.pixelColor.rgbaA(cCur)
      local aVer = app.pixelColor.rgbaA(cVer)

      if aCur == 0 and aVer == 0 then
        diffImg:drawPixel(x, y, 0)
      elseif cCur == cVer then
        local r = app.pixelColor.rgbaR(cCur)
        local g = app.pixelColor.rgbaG(cCur)
        local b = app.pixelColor.rgbaB(cCur)
        local gray = math.floor((r + g + b) / 3)
        diffImg:drawPixel(x, y, app.pixelColor.rgba(gray, gray, gray, math.floor(aCur * 0.35)))
      else
        changed = changed + 1
        if aVer == 0 and aCur > 0 then
          added = added + 1
          diffImg:drawPixel(x, y, app.pixelColor.rgba(16, 245, 150, 255))
        elseif aVer > 0 and aCur == 0 then
          removed = removed + 1
          diffImg:drawPixel(x, y, app.pixelColor.rgba(255, 60, 90, 255))
        else
          modified = modified + 1
          diffImg:drawPixel(x, y, app.pixelColor.rgba(255, 190, 20, 255))
        end
      end
    end
  end

  local tmp = app.fs.joinPath(app.fs.tempPath, "ase2_diff_" .. os.time() .. ".png")
  diffImg:saveAs(tmp)
  local df = io.open(tmp, "rb")
  local raw = df and df:read("*a") or ""
  if df then df:close() end
  os.remove(tmp)

  return {
    id = targetId,
    width = maxW,
    height = maxH,
    totalPixels = maxW * maxH,
    changedPixels = changed,
    addedPixels = added,
    removedPixels = removed,
    modifiedPixels = modified,
    diff_png_base64 = base64_encode(raw)
  }
end

-- 7. Animation Tag Management & Game Engine Sprite Sheet Exporter
handlers["tag.manage"] = function(p)
  p = p or {}
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  local action = p.action or "list"

  if action == "add" then
    if not p.name then error("Missing tag name") end
    local fromIdx = math.max(1, math.min(#spr.frames, math.floor(p.from or 1)))
    local toIdx = math.max(fromIdx, math.min(#spr.frames, math.floor(p.to or #spr.frames)))
    local tag = spr:newTag(fromIdx, toIdx)
    tag.name = p.name
    if p.aniDir == "reverse" then tag.aniDir = AniDir.REVERSE
    elseif p.aniDir == "pingpong" then tag.aniDir = AniDir.PING_PONG
    else tag.aniDir = AniDir.FORWARD end
    return { ok = true, name = tag.name, from = fromIdx, to = toIdx }
  elseif action == "delete" then
    if not p.name then error("Missing tag name") end
    for _, t in ipairs(spr.tags) do
      if t.name == p.name then
        spr:deleteTag(t)
        return { ok = true, deleted = p.name }
      end
    end
    error("Tag not found: " .. tostring(p.name))
  else
    local tags = {}
    for _, t in ipairs(spr.tags) do
      table.insert(tags, {
        name = t.name,
        from = t.fromFrame.frameNumber,
        to = t.toFrame.frameNumber
      })
    end
    return { tags = tags }
  end
end

handlers["sprite.export_spritesheet"] = function(p)
  if not p or not p.texturePath then error("Missing texturePath") end
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  app.activeSprite = spr

  local stype = SpriteSheetType.ROWS
  if p.type == "columns" then stype = SpriteSheetType.COLUMNS
  elseif p.type == "packed" then stype = SpriteSheetType.PACKED
  elseif p.type == "horizontal" then stype = SpriteSheetType.HORIZONTAL
  elseif p.type == "vertical" then stype = SpriteSheetType.VERTICAL end

  app.command.ExportSpriteSheet{
    ui = false,
    askOverwrite = false,
    type = stype,
    textureFilename = p.texturePath,
    dataFilename = p.dataPath or "",
    dataFormat = SpriteSheetDataFormat.JSON_ARRAY,
    borderPadding = p.padding or 0,
    shapePadding = p.padding or 0,
    trim = (p.trim == true),
    listLayers = true,
    listTags = true,
    listSlices = true
  }

  return {
    ok = true,
    texturePath = p.texturePath,
    dataPath = p.dataPath or ""
  }
end

-- 8. Surface Normal & Depth Map Generator (SpriteLamp / SDF + Luminance Slope Method)
local function clamp_num(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function clamp_byte(v)
  return math.floor(clamp_num(v, 0.0, 255.0) + 0.5)
end

handlers["surface.generate_normals_depth"] = function(p)
  p = p or {}
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  app.activeSprite = spr
  local frIdx = tonumber(p.frame) or 1
  local frame = spr.frames[math.min(math.max(1, frIdx), #spr.frames)]
  local bevelRadius = math.max(1.0, tonumber(p.bevelRadius) or 6.0)
  local reliefWeight = clamp_num(tonumber(p.reliefWeight) or 0.45, 0.0, 1.0)
  local normalStrength = tonumber(p.normalStrength) or 3.2
  local genLit = (p.generateLitPreview == true)
  local sourceLayerName = p.layer

  local W, H = spr.width, spr.height
  local srcImg = Image(W, H, ColorMode.RGB)

  if sourceLayerName and sourceLayerName ~= "" and sourceLayerName ~= "(All Visible Layers)" then
    local targetLyr = find_layer_by_name(spr, sourceLayerName)
    if targetLyr then
      local cel = targetLyr:cel(frame)
      if cel and cel.image then srcImg:drawImage(cel.image, cel.position) end
    end
  else
    local ignoreNames = {
      ["_DepthMap"] = true,
      ["_NormalMap"] = true,
      ["_LitPreview"] = true,
      ["Iso_Normal"] = true,
      ["Iso_Depth"] = true
    }
    for _, lyr in ipairs(spr.layers) do
      if lyr.isVisible and not ignoreNames[lyr.name] then
        local cel = lyr:cel(frame)
        if cel and cel.image then srcImg:drawImage(cel.image, cel.position) end
      end
    end
  end

  local N = W * H
  local alpha = {}
  local lum = {}
  local distToEdge = {}
  local heightMap = {}
  local minY = H
  local maxY = 0

  for y = 0, H - 1 do
    local rowOff = y * W
    for x = 0, W - 1 do
      local idx = rowOff + x + 1
      local c = srcImg:getPixel(x, y)
      local a = app.pixelColor.rgbaA(c)
      alpha[idx] = a
      if a > 16 then
        if y < minY then minY = y end
        if y > maxY then maxY = y end
        local r = app.pixelColor.rgbaR(c)
        local g = app.pixelColor.rgbaG(c)
        local b = app.pixelColor.rgbaB(c)
        lum[idx] = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
        distToEdge[idx] = 999.0
      else
        lum[idx] = 0.0
        distToEdge[idx] = 0.0
      end
    end
  end

  -- 2-Pass Chamfer Distance Transform (SDF Bevel Profile)
  for y = 0, H - 1 do
    local rowOff = y * W
    for x = 0, W - 1 do
      local idx = rowOff + x + 1
      if alpha[idx] > 16 then
        local d = distToEdge[idx]
        if x > 0 then d = math.min(d, distToEdge[idx - 1] + 1.0) else d = 1.0 end
        if y > 0 then d = math.min(d, distToEdge[idx - W] + 1.0) else d = 1.0 end
        if x > 0 and y > 0 then d = math.min(d, distToEdge[idx - W - 1] + 1.414) end
        if x < (W - 1) and y > 0 then d = math.min(d, distToEdge[idx - W + 1] + 1.414) end
        distToEdge[idx] = d
      end
    end
  end

  for y = H - 1, 0, -1 do
    local rowOff = y * W
    for x = W - 1, 0, -1 do
      local idx = rowOff + x + 1
      if alpha[idx] > 16 then
        local d = distToEdge[idx]
        if x < (W - 1) then d = math.min(d, distToEdge[idx + 1] + 1.0) else d = 1.0 end
        if y < (H - 1) then d = math.min(d, distToEdge[idx + W] + 1.0) else d = 1.0 end
        if x < (W - 1) and y < (H - 1) then d = math.min(d, distToEdge[idx + W + 1] + 1.414) end
        if x > 0 and y < (H - 1) then d = math.min(d, distToEdge[idx + W - 1] + 1.414) end
        distToEdge[idx] = d
      end
    end
  end

  local depthImg = Image(W, H, ColorMode.RGB)
  local normImg = Image(W, H, ColorMode.RGB)
  local litImg = genLit and Image(W, H, ColorMode.RGB) or nil
  local spanY = math.max(1.0, maxY - minY)

  for y = 0, H - 1 do
    local rowOff = y * W
    for x = 0, W - 1 do
      local idx = rowOff + x + 1
      local a = alpha[idx]
      if a <= 16 then
        heightMap[idx] = 0.0
      else
        local normD = math.min(1.0, distToEdge[idx] / bevelRadius)
        local dome = math.sqrt(math.max(0.0, 1.0 - (1.0 - normD) * (1.0 - normD)))
        local isoDepth = ((y - minY) / spanY) * 0.25
        local hVal = clamp_num((dome * (1.0 - reliefWeight)) + (lum[idx] * reliefWeight) + isoDepth, 0.0, 1.0)
        heightMap[idx] = hVal
        local gb = clamp_byte(hVal * 235.0 + 20.0)
        depthImg:drawPixel(x, y, app.pixelColor.rgba(gb, gb, gb, a))
      end
    end
  end

  -- Key light vector for optional studio relighting preview
  local Lx, Ly, Lz = -0.60, 0.65, 0.46
  local Hx, Hy, Hz = Lx, Ly, Lz + 1.0
  local hLen = math.sqrt(Hx * Hx + Hy * Hy + Hz * Hz)
  Hx, Hy, Hz = Hx / hLen, Hy / hLen, Hz / hLen

  for y = 0, H - 1 do
    local rowOff = y * W
    for x = 0, W - 1 do
      local idx = rowOff + x + 1
      local a = alpha[idx]
      if a > 16 then
        local x0 = math.max(0, x - 1)
        local x1 = math.min(W - 1, x + 1)
        local y0 = math.max(0, y - 1)
        local y1 = math.min(H - 1, y + 1)

        local idxL = y * W + x0 + 1
        local idxR = y * W + x1 + 1
        local idxU = y0 * W + x + 1
        local idxD = y1 * W + x + 1

        local hL = (alpha[idxL] > 16) and heightMap[idxL] or 0.0
        local hR = (alpha[idxR] > 16) and heightMap[idxR] or 0.0
        local hU = (alpha[idxU] > 16) and heightMap[idxU] or 0.0
        local hD = (alpha[idxD] > 16) and heightMap[idxD] or 0.0

        local dX = (hL - hR) * normalStrength
        local dY = (hD - hU) * normalStrength
        local dZ = 1.0
        local len = math.sqrt(dX * dX + dY * dY + dZ * dZ)
        local nx, ny, nz = dX / len, dY / len, dZ / len

        local rByte = clamp_byte((nx * 0.5 + 0.5) * 255.0)
        local gByte = clamp_byte((ny * 0.5 + 0.5) * 255.0)
        local bByte = clamp_byte((nz * 0.5 + 0.5) * 255.0)
        normImg:drawPixel(x, y, app.pixelColor.rgba(rByte, gByte, bByte, a))

        if litImg then
          local sc = srcImg:getPixel(x, y)
          local sr = app.pixelColor.rgbaR(sc)
          local sg = app.pixelColor.rgbaG(sc)
          local sb = app.pixelColor.rgbaB(sc)
          local ndotl = math.max(0.0, nx * Lx + ny * Ly + nz * Lz)
          local ndoth = math.max(0.0, nx * Hx + ny * Hy + nz * Hz)
          local spec = (ndoth ^ 18.0) * 0.55
          local diff = 0.32 + ndotl * 0.88
          local lr = clamp_byte(sr * diff * 1.05 + 255.0 * spec * 1.05)
          local lg = clamp_byte(sg * diff * 0.98 + 255.0 * spec * 0.98)
          local lb = clamp_byte(sb * diff * 0.88 + 255.0 * spec * 0.88)
          litImg:drawPixel(x, y, app.pixelColor.rgba(lr, lg, lb, a))
        end
      end
    end
  end

  app.transaction("Generate Surface Normals & Depth", function()
    local depthLyr = find_layer_by_name(spr, "_DepthMap") or spr:newLayer()
    depthLyr.name = "_DepthMap"
    spr:newCel(depthLyr, frame, depthImg, Point(0, 0))

    local normLyr = find_layer_by_name(spr, "_NormalMap") or spr:newLayer()
    normLyr.name = "_NormalMap"
    spr:newCel(normLyr, frame, normImg, Point(0, 0))

    if litImg then
      local litLyr = find_layer_by_name(spr, "_LitPreview") or spr:newLayer()
      litLyr.name = "_LitPreview"
      spr:newCel(litLyr, frame, litImg, Point(0, 0))
    end
  end)

  app.refresh()
  return {
    ok = true,
    width = W,
    height = H,
    depthLayer = "_DepthMap",
    normalLayer = "_NormalMap",
    litLayer = genLit and "_LitPreview" or nil
  }
end

-- Helper: Procedural fallback surface sampler for Easymetric 4-Surface builder
local function easymetric_procedural_pixel(face, u, v, maxU, maxV)
  local rgba = app.pixelColor.rgba
  local isBorder = (u <= 1 or u >= (maxU - 2) or v <= 1 or v >= (maxV - 2))
  local isCornerGold = ((u <= 2 and v <= 2) or (u >= (maxU - 3) and v <= 2) or
                        (u <= 2 and v >= (maxV - 3)) or (u >= (maxU - 3) and v >= (maxV - 3)))
  if isCornerGold then return rgba(251, 191, 36, 255) end
  if isBorder then return rgba(62, 39, 35, 255) end

  if face == "Top" then
    if v >= math.floor(maxV / 2 - 2) and v <= math.floor(maxV / 2 + 1) and u >= 5 and u <= (maxU - 6) then
      return rgba(30, 41, 59, 255)
    end
    if v >= 3 and v <= 5 and u >= 4 and u <= (maxU - 5) then
      if (u % 3) == 0 then return rgba(253, 224, 71, 255) end
      return rgba(120, 53, 15, 255)
    end
    return rgba(93, 64, 55, 255)
  elseif face == "Left" then
    local cx = maxU / 2.0
    local cy1 = maxV * 0.36
    local cy2 = maxV * 0.72
    local d1 = math.sqrt((u - cx) * (u - cx) + (v - cy1) * (v - cy1))
    local d2 = math.sqrt((u - cx) * (u - cx) + (v - cy2) * (v - cy2))
    if v == 3 and u >= 4 and u <= (maxU - 5) then
      return rgba(0, 229, 255, 255)
    end
    if d1 <= 2.2 or d2 <= 2.5 then
      return rgba(148, 163, 184, 255)
    end
    if d1 <= 5.2 or d2 <= 6.2 then
      return rgba(15, 23, 42, 255)
    end
    if ((u + v) % 2) == 0 then return rgba(30, 41, 59, 255) end
    return rgba(51, 65, 85, 255)
  elseif face == "Front" then
    -- Optional front decal / badge overlay (mostly transparent except top emblem)
    if v >= 2 and v <= 5 and u >= math.floor(maxU / 2 - 3) and u <= math.floor(maxU / 2 + 3) then
      return rgba(56, 189, 248, 255)
    end
    return rgba(0, 0, 0, 0)
  else
    -- Right surface
    if v >= 6 and v <= 14 and (v % 2) == 0 and u >= 5 and u <= (maxU - 6) then
      return rgba(30, 27, 24, 255)
    end
    if ((u + v) % 3) == 0 then return rgba(78, 52, 46, 255) end
    return rgba(62, 39, 35, 255)
  end
end

-- Extract cropped non-transparent surface image from a layer cel
local function extract_surface_patch(spr, frame, lyrName)
  if not lyrName or lyrName == "" or lyrName == "(Procedural Default)" or lyrName == "(None)" then
    return nil
  end
  local lyr = find_layer_by_name(spr, lyrName)
  if not lyr then return nil end
  local cel = lyr:cel(frame)
  if not cel or not cel.image then return nil end

  local img = cel.image
  local cw, ch = img.width, img.height
  local minX, minY, maxX, maxY = cw, ch, -1, -1
  for y = 0, ch - 1 do
    for x = 0, cw - 1 do
      if app.pixelColor.rgbaA(img:getPixel(x, y)) > 16 then
        if x < minX then minX = x end
        if y < minY then minY = y end
        if x > maxX then maxX = x end
        if y > maxY then maxY = y end
      end
    end
  end
  if maxX < minX or maxY < minY then return nil end

  local pw = maxX - minX + 1
  local ph = maxY - minY + 1
  local patch = Image(pw, ph, ColorMode.RGB)
  for y = 0, ph - 1 do
    for x = 0, pw - 1 do
      patch:drawPixel(x, y, img:getPixel(minX + x, minY + y))
    end
  end
  return patch
end

local function sample_surface_patch(patch, face, u, v, maxU, maxV)
  if not patch then
    return easymetric_procedural_pixel(face, u, v, maxU, maxV)
  end
  local px = clamp_num(math.floor(u * patch.width / math.max(1, maxU)), 0, patch.width - 1)
  local py = clamp_num(math.floor(v * patch.height / math.max(1, maxV)), 0, patch.height - 1)
  return patch:getPixel(px, py)
end

local function pixel_lum(c)
  local a = app.pixelColor.rgbaA(c)
  if a <= 16 then return 0.0 end
  return (0.299 * app.pixelColor.rgbaR(c) + 0.587 * app.pixelColor.rgbaG(c) + 0.114 * app.pixelColor.rgbaB(c)) / 255.0
end

-- Create 4 editable flat surface template layers (Surface_Top, Surface_Left, Surface_Right, Surface_Front)
handlers["easymetric.create_template"] = function(p)
  p = p or {}
  local spr = resolve_sprite(p.sprite)
  local S = math.max(8, tonumber(p.boxSize) or 24)
  local Hz = math.max(8, tonumber(p.boxHeight) or 28)
  local reqW = S * 2 + 16
  local reqH = S + Hz + 16

  if not spr then
    spr = Sprite(reqW, reqH, ColorMode.RGB)
    app.activeSprite = spr
  end

  local frIdx = tonumber(p.frame) or 1
  local frame = spr.frames[math.min(math.max(1, frIdx), #spr.frames)]

  app.transaction("Create Easymetric 4-Surface Template", function()
    if spr.width < reqW or spr.height < reqH then
      spr:resize(math.max(spr.width, reqW), math.max(spr.height, reqH))
    end

    local faces = {
      { name = "Surface_Top",   face = "Top",   w = S, h = S,  ox = 4,      oy = 4 },
      { name = "Surface_Left",  face = "Left",  w = S, h = Hz, ox = 4,      oy = 4 + S + 4 },
      { name = "Surface_Right", face = "Right", w = S, h = Hz, ox = 4 + S + 4, oy = 4 + S + 4 },
      { name = "Surface_Front", face = "Front", w = S, h = Hz, ox = 4 + S + 4, oy = 4 }
    }

    for _, f in ipairs(faces) do
      local lyr = find_layer_by_name(spr, f.name) or spr:newLayer()
      lyr.name = f.name
      local img = Image(f.w, f.h, ColorMode.RGB)
      for v = 0, f.h - 1 do
        for u = 0, f.w - 1 do
          local col = easymetric_procedural_pixel(f.face, u, v, f.w, f.h)
          if app.pixelColor.rgbaA(col) > 0 then
            img:drawPixel(u, v, col)
          end
        end
      end
      spr:newCel(lyr, frame, img, Point(f.ox, f.oy))
    end
  end)

  app.refresh()
  return {
    ok = true,
    boxSize = S,
    boxHeight = Hz,
    layers = { "Surface_Top", "Surface_Left", "Surface_Right", "Surface_Front" }
  }
end

-- 9. Easymetric 4-Surface Isometric Projection Builder
handlers["easymetric.project_4surfaces"] = function(p)
  p = p or {}
  local spr = resolve_sprite(p.sprite)
  if not spr then error("No active sprite") end
  app.activeSprite = spr

  local frIdx = tonumber(p.frame) or 1
  local frame = spr.frames[math.min(math.max(1, frIdx), #spr.frames)]

  local topPatch = extract_surface_patch(spr, frame, p.topLayer or "Surface_Top")
                   or extract_surface_patch(spr, frame, "Top")
  local leftPatch = extract_surface_patch(spr, frame, p.leftLayer or "Surface_Left")
                    or extract_surface_patch(spr, frame, "Left")
  local rightPatch = extract_surface_patch(spr, frame, p.rightLayer or "Surface_Right")
                     or extract_surface_patch(spr, frame, "Right")
  local frontPatch = extract_surface_patch(spr, frame, p.frontLayer or "Surface_Front")
                     or extract_surface_patch(spr, frame, "Front")

  local defaultS = topPatch and topPatch.width or (leftPatch and leftPatch.width or 24)
  local defaultHz = leftPatch and leftPatch.height or (rightPatch and rightPatch.height or 28)
  local S = math.max(6, tonumber(p.boxSize) or defaultS)
  local Hz = math.max(6, tonumber(p.boxHeight) or defaultHz)
  local bumpStrength = tonumber(p.bumpStrength) or 2.0
  local genNormal = (p.generateNormal ~= false)
  local genDepth = (p.generateDepth ~= false)
  local autoOutline = (p.autoOutline ~= false)
  local ditherSides = (p.ditherSides ~= false)
  local hideSourceLayers = (p.hideSourceLayers == true)

  local reqW = S * 2 + 16
  local reqH = S + Hz + 16
  local outW = math.max(spr.width, reqW)
  local outH = math.max(spr.height, reqH)
  local centerX = math.floor(outW / 2)
  local originY = math.max(4, math.floor((outH - (S + Hz)) / 2))

  local diffImg = Image(outW, outH, ColorMode.RGB)
  local normImg = Image(outW, outH, ColorMode.RGB)
  local deptImg = Image(outW, outH, ColorMode.RGB)

  local faceMap = {}
  for i = 1, outW * outH do faceMap[i] = 0 end

  local rgba = app.pixelColor.rgba
  local rgbaR = app.pixelColor.rgbaR
  local rgbaG = app.pixelColor.rgbaG
  local rgbaB = app.pixelColor.rgbaB
  local rgbaA = app.pixelColor.rgbaA

  local function encode_perturbed_normal(bx, by, bz, patch, face, u, v, maxU, maxV, alpha)
    local lL = pixel_lum(sample_surface_patch(patch, face, math.max(0, u - 1), v, maxU, maxV))
    local lR = pixel_lum(sample_surface_patch(patch, face, math.min(maxU - 1, u + 1), v, maxU, maxV))
    local lU = pixel_lum(sample_surface_patch(patch, face, u, math.max(0, v - 1), maxU, maxV))
    local lD = pixel_lum(sample_surface_patch(patch, face, u, math.min(maxV - 1, v + 1), maxU, maxV))
    local nx = bx + (lL - lR) * bumpStrength * 0.35
    local ny = by + (lD - lU) * bumpStrength * 0.35
    local nz = bz
    local len = math.sqrt(nx * nx + ny * ny + nz * nz)
    if len > 0.0001 then
      nx, ny, nz = nx / len, ny / len, nz / len
    end
    return rgba(
      clamp_byte((nx * 0.5 + 0.5) * 255.0),
      clamp_byte((ny * 0.5 + 0.5) * 255.0),
      clamp_byte((nz * 0.5 + 0.5) * 255.0),
      alpha
    )
  end

  -- 1. Project Top Surface: X_iso = centerX + (u - v), Y_iso = originY + floor((u + v) / 2)
  for u = 0, S - 1 do
    for v = 0, S - 1 do
      local isoX = centerX + (u - v)
      local isoY = originY + math.floor((u + v) / 2.0)
      if isoX >= 0 and isoX < outW and isoY >= 0 and isoY < outH then
        local c = sample_surface_patch(topPatch, "Top", u, v, S, S)
        local a = rgbaA(c)
        if a > 16 then
          local r = clamp_byte(rgbaR(c) * 1.12)
          local g = clamp_byte(rgbaG(c) * 1.12)
          local b = clamp_byte(rgbaB(c) * 1.12)
          diffImg:drawPixel(isoX, isoY, rgba(r, g, b, a))

          local nCol = encode_perturbed_normal(0.0, 0.85, 0.52, topPatch, "Top", u, v, S, S, a)
          normImg:drawPixel(isoX, isoY, nCol)

          local depthVal = (u + v) / (2.0 * S)
          local dByte = clamp_byte(180.0 + depthVal * 70.0)
          deptImg:drawPixel(isoX, isoY, rgba(dByte, dByte, dByte, a))
          faceMap[isoY * outW + isoX + 1] = 1
        end
      end
    end
  end

  -- 2. Project Left Surface: X_iso = centerX + (u - (S - 1)), Y_iso = originY + floor((u + (S - 1)) / 2) + z
  for u = 0, S - 1 do
    for z = 0, Hz - 1 do
      local isoX = centerX + (u - (S - 1))
      local isoY = originY + math.floor((u + (S - 1)) / 2.0) + z
      if isoX >= 0 and isoX < outW and isoY >= 0 and isoY < outH then
        local c = sample_surface_patch(leftPatch, "Left", u, z, S, Hz)
        if frontPatch then
          local fc = sample_surface_patch(frontPatch, "Front", u, z, S, Hz)
          if rgbaA(fc) > 128 then c = fc end
        end
        local a = rgbaA(c)
        if a > 16 then
          diffImg:drawPixel(isoX, isoY, c)
          local nCol = encode_perturbed_normal(-0.78, -0.22, 0.58, leftPatch, "Left", u, z, S, Hz, a)
          normImg:drawPixel(isoX, isoY, nCol)

          local depthVal = 0.5 + (u / (2.0 * S)) - (z / (3.0 * Hz))
          local dByte = clamp_byte(100.0 + depthVal * 120.0)
          deptImg:drawPixel(isoX, isoY, rgba(dByte, dByte, dByte, a))
          faceMap[isoY * outW + isoX + 1] = 2
        end
      end
    end
  end

  -- 3. Project Right Surface: X_iso = centerX + ((S - 1) - v), Y_iso = originY + floor(((S - 1) + v) / 2) + z
  for v = 0, S - 1 do
    for z = 0, Hz - 1 do
      local isoX = centerX + ((S - 1) - v)
      local isoY = originY + math.floor(((S - 1) + v) / 2.0) + z
      if isoX >= 0 and isoX < outW and isoY >= 0 and isoY < outH then
        local c = sample_surface_patch(rightPatch, "Right", v, z, S, Hz)
        local a = rgbaA(c)
        if a > 16 then
          local shade = 0.75
          if ditherSides then
            shade = (((isoX + isoY) % 2) == 0) and 0.78 or 0.70
          end
          local r = clamp_byte(rgbaR(c) * shade)
          local g = clamp_byte(rgbaG(c) * shade)
          local b = clamp_byte(rgbaB(c) * shade)
          diffImg:drawPixel(isoX, isoY, rgba(r, g, b, a))

          local nCol = encode_perturbed_normal(0.78, -0.22, 0.58, rightPatch, "Right", v, z, S, Hz, a)
          normImg:drawPixel(isoX, isoY, nCol)

          local depthVal = 0.5 + (((S - 1) - v) / (2.0 * S)) - (z / (3.0 * Hz))
          local dByte = clamp_byte(90.0 + depthVal * 120.0)
          deptImg:drawPixel(isoX, isoY, rgba(dByte, dByte, dByte, a))
          faceMap[isoY * outW + isoX + 1] = 3
        end
      end
    end
  end

  -- 4. Auto-Outline & Inner Corner Bevel Ridge Highlight
  if autoOutline then
    local outlineCol = rgba(15, 23, 42, 255)
    local outNormCol = rgba(128, 128, 255, 255)
    local outDeptCol = rgba(40, 40, 40, 255)
    local ridgeR, ridgeG, ridgeB = 253, 230, 138

    for y = 1, outH - 2 do
      local rowOff = y * outW
      for x = 1, outW - 2 do
        local idx = rowOff + x + 1
        local f = faceMap[idx]
        if f == 0 then
          if faceMap[idx - 1] > 0 or faceMap[idx + 1] > 0 or faceMap[idx - outW] > 0 or faceMap[idx + outW] > 0 then
            diffImg:drawPixel(x, y, outlineCol)
            normImg:drawPixel(x, y, outNormCol)
            deptImg:drawPixel(x, y, outDeptCol)
          end
        elseif f == 1 and (faceMap[idx + outW] == 2 or faceMap[idx + outW] == 3) then
          local cur = diffImg:getPixel(x, y)
          local r = clamp_byte(rgbaR(cur) * 0.65 + ridgeR * 0.35)
          local g = clamp_byte(rgbaG(cur) * 0.65 + ridgeG * 0.35)
          local b = clamp_byte(rgbaB(cur) * 0.65 + ridgeB * 0.35)
          diffImg:drawPixel(x, y, rgba(r, g, b, 255))
          normImg:drawPixel(x, y, rgba(128, 180, 245, 255))
        end
      end
    end
  end

  app.transaction("Easymetric 4-Surface Projection", function()
    if spr.width < outW or spr.height < outH then
      spr:resize(outW, outH)
    end

    if hideSourceLayers then
      for _, name in ipairs({ "Surface_Top", "Surface_Left", "Surface_Right", "Surface_Front", "Top", "Left", "Right", "Front" }) do
        local sLyr = find_layer_by_name(spr, name)
        if sLyr then sLyr.isVisible = false end
      end
    end

    if genDepth then
      local isoDeptLyr = find_layer_by_name(spr, "Iso_Depth") or spr:newLayer()
      isoDeptLyr.name = "Iso_Depth"
      spr:newCel(isoDeptLyr, frame, deptImg, Point(0, 0))
    end

    if genNormal then
      local isoNormLyr = find_layer_by_name(spr, "Iso_Normal") or spr:newLayer()
      isoNormLyr.name = "Iso_Normal"
      spr:newCel(isoNormLyr, frame, normImg, Point(0, 0))
    end

    local isoDiffLyr = find_layer_by_name(spr, "Iso_Diffuse") or spr:newLayer()
    isoDiffLyr.name = "Iso_Diffuse"
    spr:newCel(isoDiffLyr, frame, diffImg, Point(0, 0))
    app.activeLayer = isoDiffLyr
  end)

  app.refresh()
  return {
    ok = true,
    width = outW,
    height = outH,
    boxSize = S,
    boxHeight = Hz,
    diffuseLayer = "Iso_Diffuse",
    normalLayer = genNormal and "Iso_Normal" or nil,
    depthLayer = genDepth and "Iso_Depth" or nil
  }
end

-- Process incoming JSON request frame
local function process_message(text)
  local ok, req = pcall(json.decode, text)
  if not ok or not req then return end

  if req.type == "ping" then
    if bridge.ws then bridge.ws:sendText(json.encode({ type = "pong" })) end
    return
  end

  if req.type == "welcome" then
    bridge.connected = true
    return
  end

  if req.type == "request" then
    local id = req.id
    local method = req.method
    local params = req.params

    local fn = handlers[method]
    if not fn then
      if bridge.ws then
        bridge.ws:sendText(json.encode({
          type = "response",
          id = id,
          ok = false,
          error = { code = "not_found", message = "Method not found: " .. tostring(method) }
        }))
      end
      return
    end

    local callOk, res = pcall(function()
      local r
      local needsTx = (
        method == "draw.pixels" or
        method == "draw.recolor_ramp" or
        method == "draw.auto_outline" or
        method == "draw.shapes" or
        method == "cel.put_image" or
        method == "palette.set" or
        method == "sprite.canvas_expand_centered" or
        method == "tag.manage" or method == "surface.generate_normals_depth" or method == "easymetric.project_4surfaces"
      )
      if needsTx and app.activeSprite then
        app.transaction("AI: " .. tostring(method), function()
          r = fn(params)
        end)
      else
        r = fn(params)
      end
      return r
    end)

    if bridge.ws then
      if callOk then
        bridge.ws:sendText(json.encode({
          type = "response",
          id = id,
          ok = true,
          result = res or {}
        }))
      else
        bridge.ws:sendText(json.encode({
          type = "response",
          id = id,
          ok = false,
          error = { code = "failed", message = tostring(res) }
        }))
      end
    end
  end
end

-- Connect WebSocket client
local function connect_bridge()
  log_debug("connect_bridge() called")
  if bridge.ws then
    pcall(function() bridge.ws:close() end)
    bridge.ws = nil
  end

  local token = get_or_create_token()
  log_debug("Token retrieved: " .. tostring(token))
  local url = "ws://127.0.0.1:" .. bridge.port .. "/"

  local ws
  ws = WebSocket{
    url = url,
    onreceive = function(msgType, data, err)
      local ok, cbErr = pcall(function()
        log_debug("ws onreceive type=" .. tostring(msgType))
        if msgType == WebSocketMessageType.OPEN then
          bridge.connected = true
          log_debug("WebSocket connected to MCP server! Sending hello...")
          local helloPayload = json.encode({
            type = "hello",
            protocol = 1,
            token = tostring(token),
            app = "Aseprite 2.0",
            version = "2.0-dev",
            bridge = "1.0.0",
            capabilities = { "versions", "pixellab", "lua.exec" }
          })
          log_debug("Sending hello payload: " .. helloPayload)
          ws:sendText(helloPayload)
          log_debug("Hello payload sent successfully!")
        elseif msgType == WebSocketMessageType.TEXT then
          log_debug("Received text message: " .. tostring(data))
          process_message(data)
        elseif msgType == WebSocketMessageType.CLOSE or msgType == WebSocketMessageType.ERROR then
          log_debug("ws closed or error: " .. tostring(err))
          bridge.connected = false
        end
      end)
      if not ok then
        log_debug("Error inside onreceive callback: " .. tostring(cbErr))
      end
    end
  }

  bridge.ws = ws
  log_debug("calling ws:connect()...")
  ws:connect()
  log_debug("ws:connect() called")
end

function init(plugin)
  log_debug("init(plugin) called")
  -- Auto-start connection to MCP server
  connect_bridge()

  -- Menu entry under File > Export > Export Preview Sheet (1x + 4x)...
  plugin:newCommand{
    id = "ExportPreviewSheet",
    title = "Export Preview Sheet (1x + 4x)...",
    group = "file_export",
    onenabled = function()
      return app.activeSprite ~= nil
    end,
    onclick = function()
      if not app.activeSprite then return end
      local dlg = Dialog("Export Preview Sheet (1x + 4x)")
      dlg:number{ id = "scale", label = "Upscale Factor:", text = "4", decimals = 0 }
      dlg:combobox{
        id = "mode",
        label = "Sheet Mode:",
        option = (#app.activeSprite.frames > 1) and "frames" or "layers",
        options = { "frames", "layers" }
      }
      dlg:check{ id = "include1x", text = "Include 1x actual size beside scaled preview", selected = true }
      dlg:check{ id = "exportIndividual", text = "Also export individual 1x & scaled PNG files", selected = false }
      dlg:button{
        text = "&Export",
        focus = true,
        onclick = function()
          local res = handlers["sprite.export_preview_sheet"]{
            scale = tonumber(dlg.data.scale) or 4,
            mode = dlg.data.mode,
            include1x = dlg.data.include1x,
            exportIndividual = dlg.data.exportIndividual
          }
          dlg:close()
          app.alert{
            title = "Preview Sheet Exported",
            text = {
              "Saved preview sheet (" .. tostring(res.width) .. "x" .. tostring(res.height) .. "):",
              tostring(res.path)
            },
            buttons = { "&OK" }
          }
        end
      }
      dlg:button{ text = "&Cancel", onclick = function() dlg:close() end }
      dlg:show()
    end
  }

  -- Menu entry under Help > AI Bridge
  plugin:newCommand{
    id = "AIBridgeSettings",
    title = "AI Bridge Settings...",
    group = "help_about",
    onclick = function()
      local dlg = Dialog("AI Bridge Status")
      dlg:label{ text = "Status: " .. (bridge.connected and "Connected (port " .. bridge.port .. ")" or "Disconnected") }
      dlg:number{ id = "port", label = "Port:", text = tostring(bridge.port) }
      dlg:check{ id = "lua_exec", text = "Allow AI agents to execute arbitrary Lua (lua.exec)", selected = bridge.allow_lua_exec }
      dlg:button{ text = "Reconnect", onclick = function()
        bridge.port = dlg.data.port or 8765
        bridge.allow_lua_exec = dlg.data.lua_exec
        connect_bridge()
        dlg:close()
      end }
      dlg:button{ text = "Close", onclick = function() dlg:close() end }
      dlg:show()
    end
  }
end

function exit(plugin)
  if bridge.ws then
    pcall(function() bridge.ws:close() end)
    bridge.ws = nil
  end
end
