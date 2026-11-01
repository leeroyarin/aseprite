-- tests/smart_object/tier5_concurrency_watch_adversarial.lua
-- ==============================================================================
-- Tier 5: Adversarial File Watch, Concurrency & Lua Scripting Stress Harness
-- Covers:
--   1. File watch polling lifecycle & active document focus switch (onActiveSiteChange)
--   2. Stale cache invalidation (0-byte, corrupt, deleted, recovered, rapid overwrite)
--   3. Multi-document concurrency with shared detached document caching
--   4. Self-referencing and mutual circular link recursion protection
--   5. Lua scripting API robustness (type errors, boundary inputs, non-smart layer calls)
--   6. Path canonicalization, forward/backward slashes, and relative resolution
--   7. High-density multi-object stress (20 concurrent linked objects with deletion)
-- ==============================================================================

print("================================================================================")
print("  TIER 5 ADVERSARIAL: CONCURRENCY, FILE WATCH & LUA SCRIPTING STRESS HARNESS   ")
print("================================================================================")

local testCount = 0
local passCount = 0

local function assert_eq(actual, expected, msg)
  testCount = testCount + 1
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "value mismatch", tostring(expected), tostring(actual)), 2)
  end
  passCount = passCount + 1
end

local function assert_true(cond, msg)
  testCount = testCount + 1
  if not cond then
    error(string.format("ASSERTION FAILED: %s (expected true)", msg or "condition failed"), 2)
  end
  passCount = passCount + 1
end

local function assert_false(cond, msg)
  testCount = testCount + 1
  if cond then
    error(string.format("ASSERTION FAILED: %s (expected false)", msg or "condition failed"), 2)
  end
  passCount = passCount + 1
end

local function advanceTimestamp()
  local start = os.time()
  while os.time() <= start do
    -- busy wait for 1-second boundary tick
  end
end

local tempDir = app.fs.tempPath or "."
local pPrefix = app.fs.joinPath(tempDir, "tier5_adv_")

local function tempFile(name)
  return pPrefix .. name
end

local cleanupList = {}
local function registerCleanup(path)
  table.insert(cleanupList, path)
end

local function cleanupAll()
  for _, p in ipairs(cleanupList) do
    pcall(function() app.fs.removeFile(p) end)
    pcall(function() os.remove(p) end)
  end
end

cleanupAll()

local function createColoredFile(path, w, h, r, g, b, a)
  a = a or 255
  registerCleanup(path)
  local spr = Sprite(w, h, ColorMode.RGB)
  local img = spr.cels[1].image
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      img:drawPixel(x, y, Color{ r = r, g = g, b = b, a = a })
    end
  end
  spr:saveAs(path)
  spr:close()
end

local function verifyCelPixel(cel, x, y, r, g, b, msg)
  assert_true(cel ~= nil, (msg or "") .. " - cel is nil")
  assert_true(cel.image ~= nil, (msg or "") .. " - cel image is nil")
  local px = cel.image:getPixel(x, y)
  local actualR = app.pixelColor.rgbaR(px)
  local actualG = app.pixelColor.rgbaG(px)
  local actualB = app.pixelColor.rgbaB(px)
  assert_eq(actualR, r, (msg or "") .. " R mismatch")
  assert_eq(actualG, g, (msg or "") .. " G mismatch")
  assert_eq(actualB, b, (msg or "") .. " B mismatch")
end

-- ==============================================================================
-- PART 1: File Watch Lifecycle & Active Site Change (Focus Switching)
-- ==============================================================================
print("\n[PART 1] Testing active site change & document focus auto-refresh...")
do
  local pSrc1 = tempFile("part1_src.aseprite")
  local pHost1 = tempFile("part1_host1.aseprite")
  local pHost2 = tempFile("part1_host2.aseprite")

  -- 1. Create source file on disk: 16x16 Gold (255, 215, 0)
  createColoredFile(pSrc1, 16, 16, 255, 215, 0)

  -- 2. Create Host 1 with placed smart object
  local host1 = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSrc1, x = 0, y = 0, ui = false }
  host1:saveAs(pHost1)
  registerCleanup(pHost1)
  local lay1 = host1.layers[2]
  assert_true(lay1.isSmartObject, "Host 1 layer 2 is smart object")
  verifyCelPixel(lay1:cel(1), 0, 0, 255, 215, 0, "Host 1 initial pixel is Gold")

  -- 3. Create Host 2 (separate document) and make it active
  local host2 = Sprite(32, 32, ColorMode.RGB)
  host2:saveAs(pHost2)
  registerCleanup(pHost2)
  assert_true(app.activeSprite == host2, "Host 2 is currently active document")

  -- 4. Advance time and update pSrc1 on disk to Purple (128, 0, 128)
  advanceTimestamp()
  createColoredFile(pSrc1, 16, 16, 128, 0, 128)

  -- Check state BEFORE switching activeSprite back to host1
  assert_true(lay1.isDiskFileNewer, "Host 1 smart layer detects disk file is newer before focus switch")
  verifyCelPixel(lay1:cel(1), 0, 0, 255, 215, 0, "Host 1 cel is still Gold before focus switch")

  -- 5. Switch active document back to Host 1 (triggering onActiveSiteChange in SmartObjectManager)
  app.activeSprite = host1
  assert_true(app.activeSprite == host1, "Host 1 is active again")

  -- onActiveSiteChange automatically called checkDocUpdates(host1) and reloaded the cel!
  verifyCelPixel(lay1:cel(1), 0, 0, 128, 0, 128, "Host 1 cel automatically updated to Purple via onActiveSiteChange!")
  assert_false(lay1.isDiskFileNewer, "isDiskFileNewer is now false because it was already refreshed")

  host1:close()
  host2:close()
  print("  -> Part 1 PASSED: Active site change and focus detection verified.")
end

-- ==============================================================================
-- PART 2: Stale Cache Invalidation (0-byte, corrupt, deleted, restored)
-- ==============================================================================
print("\n[PART 2] Testing stale cache invalidation across file mutations...")
do
  local pSrc2 = tempFile("part2_cache_inval.aseprite")
  advanceTimestamp()
  createColoredFile(pSrc2, 16, 16, 10, 80, 150)

  local host = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSrc2, x = 0, y = 0, ui = false }
  local lay = host.layers[2]
  verifyCelPixel(lay:cel(1), 0, 0, 10, 80, 150, "Initial cel pixels verified")
  assert_eq(lay.linkStatus, "Linked", "Initial linkStatus is Linked")

  -- Test 2.1: 0-byte file write
  local f0 = io.open(pSrc2, "wb")
  assert_true(f0 ~= nil, "Opened file for 0-byte write")
  f0:close()

  assert_eq(lay.linkStatus, "Broken", "0-byte file must report Broken linkStatus")
  local ok0, err0 = lay:refreshSmartObject(false)
  assert_false(ok0, "0-byte refresh must return false")
  verifyCelPixel(lay:cel(1), 0, 0, 10, 80, 150, "Cels preserved after 0-byte write")

  -- Test 2.2: Corrupt garbage bytes
  local fC = io.open(pSrc2, "wb")
  assert_true(fC ~= nil, "Opened file for garbage write")
  fC:write(string.rep("\x7F\x45\x4C\x46\x00\x00\x00\x00", 32)) -- 256 bytes non-aseprite garbage
  fC:close()

  local okC, errC = lay:refreshSmartObject(true)
  assert_false(okC, "Corrupt file refresh must fail gracefully")
  verifyCelPixel(lay:cel(1), 0, 0, 10, 80, 150, "Cels preserved after corrupt write")

  -- Test 2.3: Deleted file from disk
  pcall(function() app.fs.removeFile(pSrc2) end)
  pcall(function() os.remove(pSrc2) end)
  assert_false(app.fs.isFile(pSrc2), "File deleted from disk")
  assert_eq(lay.linkStatus, "Missing", "Deleted file reports Missing linkStatus")

  local okD = lay:refreshSmartObject(false)
  assert_false(okD, "Deleted file refresh returns false")
  verifyCelPixel(lay:cel(1), 0, 0, 10, 80, 150, "Cels preserved while file deleted")

  -- Test 2.4: Restoration of valid file (Olive: 128, 128, 0)
  advanceTimestamp()
  createColoredFile(pSrc2, 16, 16, 128, 128, 0)
  assert_true(app.fs.isFile(pSrc2), "File restored on disk")

  local okR = lay:refreshSmartObject()
  assert_true(okR, "Refresh succeeds after restoration")
  assert_eq(lay.linkStatus, "Linked", "linkStatus back to Linked")
  verifyCelPixel(lay:cel(1), 0, 0, 128, 128, 0, "Cels updated to Olive after restoration")

  host:close()
  print("  -> Part 2 PASSED: Cache invalidation across 0-byte/corrupt/deleted verified.")
end

-- ==============================================================================
-- PART 3: Multi-Document Concurrency & Shared Detached Documents
-- ==============================================================================
print("\n[PART 3] Testing multi-document concurrency with shared cache...")
do
  local pShared = tempFile("part3_shared.aseprite")
  advanceTimestamp()
  createColoredFile(pShared, 16, 16, 50, 60, 70)

  -- Host A: Scale 1.0, 0 deg
  local hostA = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pShared, x = 0, y = 0, ui = false }
  local layA = hostA.layers[2]

  -- Host B: Scale 2.0, 90 deg
  local hostB = Sprite(64, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pShared, x = 10, y = 10, ui = false }
  local layB = hostB.layers[2]
  layB:setTransform(10, 10, 2.0, 2.0, 90.0, false, false)

  -- Host C: Timeline Mapping "Hold", Frame count = 4
  local hostC = Sprite(32, 32, ColorMode.RGB)
  while #hostC.frames < 4 do
    hostC:newEmptyFrame()
  end
  app.command.PlaceSmartObject{ filename = pShared, x = 0, y = 0, ui = false }
  local layC = hostC.layers[2]
  layC.timelineMapping = "Hold"
  layC:syncTimeline()

  -- Modify shared file on disk to Lime (0, 255, 128)
  advanceTimestamp()
  createColoredFile(pShared, 16, 16, 0, 255, 128)

  -- Global command refresh targeting shared file
  app.command.RefreshSmartObject{ all = true, filename = pShared, force = true }

  -- Verify Host A updated
  verifyCelPixel(layA:cel(1), 0, 0, 0, 255, 128, "Host A updated to Lime")
  assert_eq(layA.scale.x, 1.0, "Host A scale retained at 1.0")

  -- Verify Host B updated and retained 2x scale + 90 deg transform
  assert_eq(layB.scale.x, 2.0, "Host B scale retained at 2.0")
  assert_eq(layB.angle, 90.0, "Host B angle retained at 90 deg")
  verifyCelPixel(layB:cel(1), 0, 0, 0, 255, 128, "Host B cel pixel updated")

  -- Verify Host C updated and retained Hold mapping across all frames
  assert_eq(#layC.cels, 4, "Host C has 4 cels")
  for f = 1, 4 do
    verifyCelPixel(layC:cel(f), 0, 0, 0, 255, 128, "Host C frame " .. f .. " updated")
  end

  -- Close Host A, ensure Host B and Host C remain valid (shared_ptr refcount safety)
  hostA:close()
  assert_eq(layB.linkStatus, "Linked", "Host B layer still Linked after Host A closed")
  assert_eq(layC.linkStatus, "Linked", "Host C layer still Linked after Host A closed")

  hostB:close()
  hostC:close()
  print("  -> Part 3 PASSED: Multi-document concurrent sharing and lifetime verified.")
end

-- ==============================================================================
-- PART 4: Self-Reference & Circular Mutual Link Protection
-- ==============================================================================
print("\n[PART 4] Testing self-reference & circular mutual link protection...")
do
  local pSelf = tempFile("part4_self.aseprite")
  local pCircA = tempFile("part4_circA.aseprite")
  local pCircB = tempFile("part4_circB.aseprite")

  -- Test 4.1: Direct self-placement lockout
  advanceTimestamp()
  local selfSpr = Sprite(32, 32, ColorMode.RGB)
  selfSpr:saveAs(pSelf)
  registerCleanup(pSelf)

  local placeSelfOk = pcall(function()
    app.command.PlaceSmartObject{ filename = pSelf, x = 0, y = 0, ui = false }
  end)
  -- Should fail or not add layer
  assert_eq(#selfSpr.layers, 1, "Self-placement prohibited, layer count remains 1")
  selfSpr:close()

  -- Test 4.2: Mutual circular links between files
  advanceTimestamp()
  createColoredFile(pCircA, 16, 16, 111, 222, 33)
  createColoredFile(pCircB, 16, 16, 44, 55, 66)

  -- File A links File B
  local sprA = Sprite{ fromFile = pCircA }
  app.command.PlaceSmartObject{ filename = pCircB, x = 0, y = 0, ui = false }
  sprA:saveAs(pCircA)
  sprA:close()

  -- File B links File A
  local sprB = Sprite{ fromFile = pCircB }
  app.command.PlaceSmartObject{ filename = pCircA, x = 0, y = 0, ui = false }
  sprB:saveAs(pCircB)
  sprB:close()

  -- Open File A into context and refresh all
  local testHost = Sprite{ fromFile = pCircA }
  app.command.RefreshSmartObject{ all = true, force = true }
  assert_true(true, "Circular mutual reference refresh survived without recursion")

  testHost:close()
  print("  -> Part 4 PASSED: Self-reference and circular link protections verified.")
end

-- ==============================================================================
-- PART 5: Lua Scripting API Boundary & Error Robustness
-- ==============================================================================
print("\n[PART 5] Stress-testing Lua scripting API robustness...")
do
  local pSrc5 = tempFile("part5_lua.aseprite")
  advanceTimestamp()
  createColoredFile(pSrc5, 16, 16, 200, 100, 50)

  local spr = Sprite(32, 32, ColorMode.RGB)
  local regLay = spr.layers[1]

  -- 5.1: Non-smart layer method calls must be safe
  local okRefresh, msg = regLay:refreshSmartObject()
  assert_false(okRefresh, "refreshSmartObject on regular layer returns false")
  assert_eq(msg, "Layer is not a smart object", "Expected non-smart error message")

  regLay:setTransform(0, 0, 1, 1, 0, false, false)
  regLay:resetTransform()
  regLay:translate(5, 5)
  regLay:renderCel(1, 1)
  regLay:renderAllCels()
  regLay:syncTimeline()
  assert_eq(regLay:mapTimelineFrame(1), nil, "mapTimelineFrame on regular layer returns nil")

  -- 5.2: Non-smart layer properties
  assert_false(regLay.isSmartObject, "Regular layer isSmartObject is false")
  assert_eq(regLay.linkStatus, nil, "Regular layer linkStatus is nil")
  assert_false(regLay.isDiskFileNewer, "Regular layer isDiskFileNewer is false")
  assert_eq(regLay.smartObjectFilepath, nil, "Regular layer smartObjectFilepath is nil")
  assert_eq(regLay.smartObjectLastModified, 0, "Regular layer smartObjectLastModified is 0")
  assert_eq(regLay.linkedFrameCount, 0, "Regular layer linkedFrameCount is 0")
  assert_eq(regLay.timelineMapping, nil, "Regular layer timelineMapping is nil")

  -- 5.3: Setting properties on regular layer should throw clean error
  local errPath = pcall(function() regLay.smartObjectFilepath = "test.aseprite" end)
  assert_false(errPath, "Setting smartObjectFilepath on regular layer throws error")

  local errMod = pcall(function() regLay.smartObjectLastModified = 12345 end)
  assert_false(errMod, "Setting smartObjectLastModified on regular layer throws error")

  -- 5.4: Smart layer boundary & type checks
  app.command.PlaceSmartObject{ filename = pSrc5, x = 0, y = 0, ui = false }
  local smartLay = spr.layers[2]
  assert_true(smartLay.isSmartObject, "Smart layer created")

  -- Empty filepath error
  local errEmpty = pcall(function() smartLay.smartObjectFilepath = "" end)
  assert_false(errEmpty, "Empty smartObjectFilepath throws error")

  -- Invalid timeline mapping string defaults gracefully to Loop
  smartLay.timelineMapping = "UnknownMode"
  assert_eq(smartLay.timelineMapping, "Loop", "Unknown timelineMapping defaults to Loop")

  -- Extreme rotation angles
  smartLay.angle = 720.0
  assert_eq(smartLay.angle, 720.0, "Angle 720 accepted")
  smartLay.angle = -45.0
  assert_eq(smartLay.angle, -45.0, "Angle -45 accepted")

  -- Non-uniform scale via table
  smartLay.scale = { x = 0.5, y = 3.0 }
  assert_eq(smartLay.scale.x, 0.5, "Scale X is 0.5")
  assert_eq(smartLay.scale.y, 3.0, "Scale Y is 3.0")

  -- Uniform scale via number
  smartLay.scale = 2.0
  assert_eq(smartLay.scale.x, 2.0, "Uniform scale X is 2.0")
  assert_eq(smartLay.scale.y, 2.0, "Uniform scale Y is 2.0")

  -- Pivot table assignment
  smartLay.pivot = { x = 0.0, y = 1.0 }
  assert_eq(smartLay.pivot.x, 0.0, "Pivot X is 0.0")
  assert_eq(smartLay.pivot.y, 1.0, "Pivot Y is 1.0")

  -- Out of range timeline mapping query
  local lf1 = smartLay:mapTimelineFrame(1)
  assert_eq(lf1, 1, "Host frame 1 maps to linked frame 1")
  local lfHuge = smartLay:mapTimelineFrame(1000)
  assert_eq(lfHuge, 1, "Host frame 1000 maps to linked frame 1 (1-frame source)")

  spr:close()
  print("  -> Part 5 PASSED: Lua scripting API robustness and edge cases verified.")
end

-- ==============================================================================
-- PART 6: Windows Path Slashes & Forward/Backward Normalization
-- ==============================================================================
print("\n[PART 6] Testing path normalization (forward vs backward slashes)...")
do
  local pSrc6 = tempFile("part6_slash.aseprite")
  advanceTimestamp()
  createColoredFile(pSrc6, 16, 16, 77, 88, 99)

  -- Convert backslashes to forward slashes
  local fwdPath = pSrc6:gsub("\\", "/")

  local spr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = fwdPath, x = 0, y = 0, ui = false }
  local smartLay = spr.layers[2]
  assert_true(smartLay.isSmartObject, "Placed layer with forward slashes is smart object")
  assert_eq(smartLay.linkStatus, "Linked", "Forward slash path resolves to Linked")
  verifyCelPixel(smartLay:cel(1), 0, 0, 77, 88, 99, "Pixels verified for forward slash path")

  -- Relative path resolution test
  local resolved = smartLay.sourcePath
  assert_true(resolved ~= nil and #resolved > 0, "sourcePath returns valid string")

  spr:close()
  print("  -> Part 6 PASSED: Path normalization with forward slashes verified.")
end

-- ==============================================================================
-- PART 7: High-Density Multi-Object Stress (20 Concurrent Linked Objects)
-- ==============================================================================
print("\n[PART 7] High-density stress test: 20 concurrent linked smart objects...")
do
  local hostSpr = Sprite(128, 128, ColorMode.RGB)
  local files = {}

  advanceTimestamp()
  for i = 1, 20 do
    local p = tempFile(string.format("part7_dense_%d.aseprite", i))
    createColoredFile(p, 8, 8, i * 10, i * 12, (i * 15) % 256)
    table.insert(files, p)
  end

  app.activeSprite = hostSpr

  for i = 1, 20 do
    app.command.PlaceSmartObject{
      filename = files[i],
      x = ((i - 1) % 5) * 20,
      y = math.floor((i - 1) / 5) * 20,
      ui = false
    }
  end

  assert_eq(#hostSpr.layers, 21, "Host sprite has 20 smart layers + 1 base layer")

  -- Verify all 20 layers are Linked
  for i = 1, 20 do
    local lay = hostSpr.layers[i + 1]
    assert_true(lay.isSmartObject, "Layer " .. i .. " is smart object")
    assert_eq(lay.linkStatus, "Linked", "Layer " .. i .. " is Linked")
    verifyCelPixel(lay:cel(1), 0, 0, i * 10, i * 12, (i * 15) % 256, "Layer " .. i .. " pixel check")
  end

  -- Bulk refresh all 20 layers
  app.command.RefreshSmartObject{ all = true, force = true }

  -- Delete all 20 source files from disk
  for _, p in ipairs(files) do
    pcall(function() app.fs.removeFile(p) end)
    pcall(function() os.remove(p) end)
  end

  -- Verify all 20 layers transition to Missing without crash
  for i = 1, 20 do
    local lay = hostSpr.layers[i + 1]
    assert_eq(lay.linkStatus, "Missing", "Layer " .. i .. " transitioned to Missing")
    verifyCelPixel(lay:cel(1), 0, 0, i * 10, i * 12, (i * 15) % 256, "Layer " .. i .. " retains cels")
  end

  hostSpr:close()
  print("  -> Part 7 PASSED: 20 concurrent smart objects handled cleanly under stress.")
end

-- ==============================================================================
-- FINAL TEARDOWN & SUMMARY
-- ==============================================================================
cleanupAll()

print("\n================================================================================")
print(string.format("  TIER 5 ADVERSARIAL SUITE COMPLETE: %d / %d ASSERTIONS PASSED!       ", passCount, testCount))
print("================================================================================")
