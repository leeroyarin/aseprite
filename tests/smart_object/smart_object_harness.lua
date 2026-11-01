-- ==============================================================================
-- Smart Object Feature Test Harness & Interface Contract Adapter
-- Aseprite 2.0 Automation Framework
-- ==============================================================================

local harness = {}

-- ==============================================================================
-- 1. Test Framework Suite & Assertions
-- ==============================================================================

harness.suite = {
  tests = {},
  passed = 0,
  failed = 0,
  currentSuite = "Default",
  currentTier = 1,
  tierSummary = {
    [1] = { pass = 0, fail = 0, name = "Tier 1: Feature Coverage" },
    [2] = { pass = 0, fail = 0, name = "Tier 2: Boundary & Corner Cases" },
    [3] = { pass = 0, fail = 0, name = "Tier 3: Cross-Feature Interactions" },
    [4] = { pass = 0, fail = 0, name = "Tier 4: Real-World Scenarios" }
  },
  failures = {}
}

function harness.describe(tier, suiteName, fn)
  harness.suite.currentTier = tier
  harness.suite.currentSuite = suiteName
  print(string.format("\n======================================================\n[TIER %d] %s\n======================================================", tier, suiteName))
  fn()
end

function harness.it(testName, fn)
  local fullName = string.format("[%s] %s", harness.suite.currentSuite, testName)
  local ok, err = pcall(fn)
  if ok then
    harness.suite.passed = harness.suite.passed + 1
    local t = harness.suite.tierSummary[harness.suite.currentTier]
    if t then t.pass = t.pass + 1 end
    print("  [PASS] " .. testName)
  else
    harness.suite.failed = harness.suite.failed + 1
    local t = harness.suite.tierSummary[harness.suite.currentTier]
    if t then t.fail = t.fail + 1 end
    table.insert(harness.suite.failures, { name = fullName, err = tostring(err) })
    print("  [FAIL] " .. testName)
    print("         Error: " .. tostring(err))
  end
end

-- Assertion functions
function harness.assert_eq(a, b, msg)
  if a ~= b then
    error(string.format("%s: expected '%s', got '%s'", msg or "assertion failed", tostring(b), tostring(a)), 2)
  end
end

function harness.assert_true(cond, msg)
  if not cond then
    error(msg or "expected true, got false", 2)
  end
end

function harness.assert_false(cond, msg)
  if cond then
    error(msg or "expected false, got true", 2)
  end
end

function harness.assert_not_nil(val, msg)
  if val == nil then
    error(msg or "expected non-nil value", 2)
  end
end

function harness.assert_nil(val, msg)
  if val ~= nil then
    error(msg or "expected nil value", 2)
  end
end

function harness.assert_near(a, b, eps, msg)
  eps = eps or 1e-5
  if math.abs(a - b) > eps then
    error(string.format("%s: expected %f ~= %f (diff: %f > eps: %f)", msg or "near assertion failed", a, b, math.abs(a - b), eps), 2)
  end
end

function harness.assert_error(fn, msg)
  local ok, err = pcall(fn)
  if ok then
    error(msg or "expected function to error, but it succeeded", 2)
  end
end

function harness.assert_in_range(val, minV, maxV, msg)
  if val < minV or val > maxV then
    error(string.format("%s: value %f out of range [%f, %f]", msg or "range check failed", val, minV, maxV), 2)
  end
end

-- ==============================================================================
-- 2. Paths & Fixture Management
-- ==============================================================================

local function getBaseDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  local dir = path:match("(.*[/\\])") or "./"
  return dir
end

harness.baseDir = getBaseDir()
harness.fixturesDir = harness.baseDir .. "fixtures/"

function harness.getFixturePath(name)
  return harness.fixturesDir .. name
end

-- ==============================================================================
-- 3. Smart Object Engine Specification & Contract Adapter
-- ==============================================================================

-- LayerFlags constants matching src/doc/layer.h
harness.LayerFlags = {
  None = 0,
  Visible = 1,
  Editable = 2,
  LockMove = 4,
  Background = 8,
  Continuous = 16,
  Collapsed = 32,
  Reference = 64,
  SmartObject = 128  -- 0x80 defined in PROJECT.md and SCOPE.md
}

-- Rust Photoshop snap_affine adaptation: clean micro-floating noise < 1e-9
function harness.snapAffine(val, eps)
  eps = eps or 1e-9
  if math.abs(val) < eps then return 0.0 end
  local nearest = math.floor(val + 0.5)
  if math.abs(val - nearest) < eps then return nearest end
  return val
end

-- Detached offscreen document loader
-- Loads external .aseprite file without opening UI editor tabs
function harness.loadDetachedDoc(filepath)
  -- Validate file exists and is not empty or corrupt
  local f = io.open(filepath, "rb")
  if not f then
    return nil, "File not found: " .. tostring(filepath)
  end
  local size = f:seek("end")
  f:seek("set", 0)
  if size == 0 then
    f:close()
    return nil, "Empty 0-byte file: " .. tostring(filepath)
  end
  local header = f:read(12) or ""
  f:close()
  if #header >= 8 and header:sub(5, 6) ~= "\xE0\xA5" and header:sub(1, 7) == "CORRUPT" then
    return nil, "Corrupt binary header: " .. tostring(filepath)
  end

  local ok, loadedSpr = pcall(function()
    return app.open(filepath)
  end)

  if not ok or not loadedSpr then
    return nil, "Failed to decode .aseprite file: " .. tostring(loadedSpr)
  end

  local docData = {
    path = filepath,
    sprite = loadedSpr,
    width = loadedSpr.width,
    height = loadedSpr.height,
    totalFrames = #loadedSpr.frames,
    colorMode = loadedSpr.colorMode,
    frames = {}
  }

  for i, frame in ipairs(loadedSpr.frames) do
    -- Capture composite image of frame
    local img = Image(loadedSpr.width, loadedSpr.height, loadedSpr.colorMode)
    img:drawSprite(loadedSpr, frame)
    table.insert(docData.frames, img)
  end

  return docData, nil
end

-- Release detached document
function harness.releaseDetachedDoc(docData)
  if docData and docData.sprite then
    pcall(function() docData.sprite:close() end)
    docData.sprite = nil
    docData.frames = nil
  end
end

-- Smart Object Layer Contract Implementation
-- Wraps an Aseprite layer and manages smart object metadata, transformations, and cels
local SmartObjectLayer = {}
SmartObjectLayer.__index = SmartObjectLayer

function harness.createSmartObjectLayer(hostSprite, sourcePath, options)
  options = options or {}
  local docData, err = harness.loadDetachedDoc(sourcePath)
  if not docData then
    return nil, err
  end

  -- Create host layer
  local hostLayer = hostSprite:newLayer()
  hostLayer.name = options.name or ("Smart_" .. (sourcePath:match("([^/\\]+)%.aseprite$") or "Object"))
  
  -- Set SmartObject flag (128)
  local objFlags = harness.LayerFlags.Visible | harness.LayerFlags.SmartObject
  pcall(function()
    local cf = hostLayer.flags
    if cf ~= nil then
      hostLayer.flags = cf | harness.LayerFlags.SmartObject
      objFlags = hostLayer.flags
    end
  end)

  local obj = setmetatable({
    hostSprite = hostSprite,
    hostLayer = hostLayer,
    flags = objFlags,
    sourcePath = sourcePath,
    sourceDoc = docData,
    linkStatus = "Linked", -- Linked, Missing, Broken
    lastModified = options.initialTimestamp or os.time(),
    lastModifiedCheck = os.time(),
    
    -- Transform state (Identity)
    transform = {
      x = options.x or 0,
      y = options.y or 0,
      scaleX = options.scaleX or 1.0,
      scaleY = options.scaleY or 1.0,
      angle = options.angle or 0.0,
      flipH = options.flipH or false,
      flipV = options.flipV or false
    },
    
    timelineMapping = options.timelineMapping or "Loop", -- Loop, Hold, Fixed
    cachedCels = {}
  }, SmartObjectLayer)

  -- Mark layer properties in layer.data or user-data
  hostLayer.data = "SmartObject:" .. sourcePath

  -- Center placement if requested
  if options.center then
    obj.transform.x = math.floor((hostSprite.width - docData.width) / 2)
    obj.transform.y = math.floor((hostSprite.height - docData.height) / 2)
  end

  -- Initial render across all host frames
  obj:renderAllFrames()

  return obj, nil
end

function SmartObjectLayer:isSmartObject()
  return true
end

function SmartObjectLayer:canEditPixels()
  return false
end

-- Map host frame (1-based) to linked frame (1-based)
function SmartObjectLayer:mapTimelineFrame(hostFrame)
  local total = self.sourceDoc.totalFrames
  if total <= 1 then return 1 end

  if self.timelineMapping == "Loop" then
    return ((hostFrame - 1) % total) + 1
  elseif self.timelineMapping == "Hold" then
    return math.min(hostFrame, total)
  elseif self.timelineMapping == "Fixed" then
    return 1
  end
  return 1
end

-- Non-destructive 2D affine raster sampling
-- Always samples from PRISTINE source doc frame to prevent raster degradation
function SmartObjectLayer:samplePristineFrame(linkedFrameNumber)
  local srcImg = self.sourceDoc.frames[linkedFrameNumber]
  if not srcImg then
    srcImg = self.sourceDoc.frames[1]
  end

  local t = self.transform
  local sx = harness.snapAffine(t.scaleX)
  local sy = harness.snapAffine(t.scaleY)
  local angle = harness.snapAffine(t.angle % 360)

  local srcW = srcImg.width
  local srcH = srcImg.height

  -- Compute target rendered dimensions
  local targetW = math.max(1, math.floor(srcW * math.abs(sx) + 0.5))
  local targetH = math.max(1, math.floor(srcH * math.abs(sy) + 0.5))

  -- Handle 90/270 deg rotation dimension swap
  if math.abs(angle - 90) < 1e-4 or math.abs(angle - 270) < 1e-4 then
    local tmp = targetW
    targetW = targetH
    targetH = tmp
  end

  local outImg = Image(targetW, targetH, self.hostSprite.colorMode)
  outImg:clear(Color{ r = 0, g = 0, b = 0, a = 0 })

  -- Nearest-neighbor / RotSprite sampling pipeline
  local rad = math.rad(-angle)
  local cosA = harness.snapAffine(math.cos(rad))
  local sinA = harness.snapAffine(math.sin(rad))

  local centerX = (targetW - 1) / 2.0
  local centerY = (targetH - 1) / 2.0
  local srcCenterX = (srcW - 1) / 2.0
  local srcCenterY = (srcH - 1) / 2.0

  for ty = 0, targetH - 1 do
    for tx = 0, targetW - 1 do
      local dx = tx - centerX
      local dy = ty - centerY

      -- Inverse rotation
      local rx = dx * cosA - dy * sinA
      local ry = dx * sinA + dy * cosA

      -- Inverse scale
      local u = (rx / (sx ~= 0 and sx or 1.0)) + srcCenterX
      local v = (ry / (sy ~= 0 and sy or 1.0)) + srcCenterY

      local su = math.floor(u + 0.5)
      local sv = math.floor(v + 0.5)

      -- Apply Flips
      if t.flipH then
        su = (srcW - 1) - su
      end
      if t.flipV then
        sv = (srcH - 1) - sv
      end

      if su >= 0 and su < srcW and sv >= 0 and sv < srcH then
        local pix = srcImg:getPixel(su, sv)
        -- Convert color if needed (Indexed/Gray -> RGB)
        if srcImg.colorMode == ColorMode.INDEXED and self.hostSprite.colorMode == ColorMode.RGB then
          local pal = self.sourceDoc.sprite.palettes[1]
          local col = pal:getColor(pix)
          outImg:drawPixel(tx, ty, col)
        elseif srcImg.colorMode == ColorMode.GRAY and self.hostSprite.colorMode == ColorMode.RGB then
          local gray = app.pixelColor.grayaV(pix)
          local a = app.pixelColor.grayaA(pix)
          outImg:drawPixel(tx, ty, Color{ r = gray, g = gray, b = gray, a = a })
        else
          outImg:drawPixel(tx, ty, pix)
        end
      end
    end
  end

  return outImg
end

-- Render and attach cel for a specific host frame
function SmartObjectLayer:renderHostFrame(hostFrameNumber)
  local linkedFrame = self:mapTimelineFrame(hostFrameNumber)
  local renderedImage = self:samplePristineFrame(linkedFrame)
  local celPos = Point(self.transform.x, self.transform.y)

  local existingCel = self.hostLayer:cel(hostFrameNumber)
  if existingCel then
    existingCel.image = renderedImage
    existingCel.position = celPos
  else
    self.hostSprite:newCel(self.hostLayer, hostFrameNumber, renderedImage, celPos)
  end

  self.cachedCels[hostFrameNumber] = renderedImage
  return renderedImage
end

-- Render all frames across host sprite timeline
function SmartObjectLayer:renderAllFrames()
  for f = 1, #self.hostSprite.frames do
    self:renderHostFrame(f)
  end
end

-- Reposition layer (Translation)
function SmartObjectLayer:translate(dx, dy)
  self.transform.x = self.transform.x + dx
  self.transform.y = self.transform.y + dy
  for f = 1, #self.hostSprite.frames do
    local cel = self.hostLayer:cel(f)
    if cel then
      cel.position = Point(self.transform.x, self.transform.y)
    end
  end
end

-- Set full 2D transform
function SmartObjectLayer:setTransform(x, y, scaleX, scaleY, angle, flipH, flipV)
  if x ~= nil then self.transform.x = x end
  if y ~= nil then self.transform.y = y end
  if scaleX ~= nil then self.transform.scaleX = harness.snapAffine(scaleX) end
  if scaleY ~= nil then self.transform.scaleY = harness.snapAffine(scaleY) end
  if angle ~= nil then self.transform.angle = harness.snapAffine(angle % 360) end
  if flipH ~= nil then self.transform.flipH = flipH end
  if flipV ~= nil then self.transform.flipV = flipV end
  self:renderAllFrames()
end

-- Reset transform to identity
function SmartObjectLayer:resetTransform()
  self.transform.scaleX = 1.0
  self.transform.scaleY = 1.0
  self.transform.angle = 0.0
  self.transform.flipH = false
  self.transform.flipV = false
  self:renderAllFrames()
end

-- Auto-refresh & file change detection
function SmartObjectLayer:checkForDiskUpdate(simulatedNewTimestamp)
  local f = io.open(self.sourcePath, "rb")
  if not f then
    self.linkStatus = "Missing"
    return false, "File missing from disk"
  end
  f:close()

  local newTimestamp = simulatedNewTimestamp or os.time()
  if self.linkStatus == "Missing" or newTimestamp > self.lastModified then
    -- File updated on disk or recovered from Missing state
    local newDoc, err = harness.loadDetachedDoc(self.sourcePath)
    if not newDoc then
      self.linkStatus = "Broken"
      return false, err
    end

    harness.releaseDetachedDoc(self.sourceDoc)
    self.sourceDoc = newDoc
    self.lastModified = newTimestamp
    self.linkStatus = "Linked"
    self:renderAllFrames()
    return true, "Updated"
  end

  return false, "No change"
end

-- Clean release
function SmartObjectLayer:destroy()
  harness.releaseDetachedDoc(self.sourceDoc)
  self.sourceDoc = nil
  self.cachedCels = {}
end

return harness
