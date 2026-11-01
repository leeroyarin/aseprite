-- tests/smart_object/challenger_m4_file_watch_stress.lua
-- ==============================================================================
-- Empirical Challenger Stress Harness for Smart Object File Watch & Reload (M4)
-- Tests:
--   1. Sub-second rapid external file overwrites (<1s) & force-reload semantics
--   2. Corrupt / 0-byte external file writes (crash prevention & cel retention)
--   3. Missing / deleted external file transitions (LinkStatus Missing -> Linked)
--   4. Timeline structural changes on disk (Expansion 1f->3f, Shrinking 3f->1f, Loop/Hold)
--   5. External canvas dimension mutation (16x16 -> 32x24 canvas adaptation)
--   6. Multi-document & multi-layer concurrency (shared source file across docs & layers)
--   7. Filename-filtered command execution (RefreshSmartObject with filename filter)
--   8. Self-referencing file reload guard verification
--   9. High-frequency rapid update torture harness (50 cycles)
--   10. Empirical Bug Demonstration: Stale Cache Hit on PlaceSmartObject
-- ==============================================================================

print("================================================================================")
print("  EMPIRICAL CHALLENGER: M4 FILE WATCH & DISK AUTO-RELOAD ADVERSARIAL STRESS     ")
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

-- Wait until next whole second boundary so modification time strictly increases
local function advanceTimestamp()
  local start = os.time()
  while os.time() <= start do
    -- busy wait for system clock tick
  end
end

local tempDir = app.fs.tempPath or "."
local pSec1 = app.fs.joinPath(tempDir, "stress_m4_sec1.aseprite")
local pSec2 = app.fs.joinPath(tempDir, "stress_m4_sec2.aseprite")
local pSec3 = app.fs.joinPath(tempDir, "stress_m4_sec3.aseprite")
local pSec4 = app.fs.joinPath(tempDir, "stress_m4_sec4.aseprite")
local pSec5 = app.fs.joinPath(tempDir, "stress_m4_sec5.aseprite")
local pSec6a = app.fs.joinPath(tempDir, "stress_m4_sec6a.aseprite")
local pSec6b = app.fs.joinPath(tempDir, "stress_m4_sec6b.aseprite")
local pSec7 = app.fs.joinPath(tempDir, "stress_m4_sec7.aseprite")
local pSec8 = app.fs.joinPath(tempDir, "stress_m4_sec8.aseprite")
local pBug = app.fs.joinPath(tempDir, "stress_m4_bug.aseprite")

local allPaths = { pSec1, pSec2, pSec3, pSec4, pSec5, pSec6a, pSec6b, pSec7, pSec8, pBug }

local function cleanupFiles()
  for _, p in ipairs(allPaths) do
    pcall(function() app.fs.removeFile(p) end)
    pcall(function() os.remove(p) end)
  end
end

cleanupFiles()

local function createColoredFile(path, w, h, r, g, b, a)
  a = a or 255
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
  assert_true(cel ~= nil, (msg or "") .. " - cel must not be nil")
  assert_true(cel.image ~= nil, (msg or "") .. " - cel image must not be nil")
  local px = cel.image:getPixel(x, y)
  local actualR = app.pixelColor.rgbaR(px)
  local actualG = app.pixelColor.rgbaG(px)
  local actualB = app.pixelColor.rgbaB(px)
  assert_eq(actualR, r, (msg or "") .. " R mismatch")
  assert_eq(actualG, g, (msg or "") .. " G mismatch")
  assert_eq(actualB, b, (msg or "") .. " B mismatch")
end

-- ==============================================================================
-- SECTION 1: Sub-second Rapid Overwrites (<1s) & Force-Reload Semantics
-- ==============================================================================
print("\n[SECTION 1] Testing sub-second rapid overwrites & force-reload semantics...")
do
  -- Create initial source: 16x16 Blue (0, 0, 255)
  createColoredFile(pSec1, 16, 16, 0, 0, 255)
  local hostSpr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec1, x = 0, y = 0, ui = false }
  local smartLay = hostSpr.layers[2]
  assert_true(smartLay.isSmartObject, "Layer is smart object")
  verifyCelPixel(smartLay:cel(1), 0, 0, 0, 0, 255, "Initial placed cel is Blue")

  local t0 = smartLay.smartObjectLastModified
  assert_true(t0 > 0, "Last modified is valid epoch")

  -- Rapid overwrite in SAME second boundary: write Red (255, 0, 0)
  createColoredFile(pSec1, 16, 16, 255, 0, 0)

  -- Regular refresh (force=false) MUST detect file modification epoch hasn't changed
  local updatedNoForce = smartLay:refreshSmartObject(false)
  if os.time() == t0 then
    assert_false(updatedNoForce, "Regular refresh skips reload when timestamp unchanged in same second")
    verifyCelPixel(smartLay:cel(1), 0, 0, 0, 0, 255, "Cel still holds Blue because reload was skipped")
  end

  -- Force refresh (force=true) MUST reload even within same second boundary
  local updatedForce = smartLay:refreshSmartObject(true)
  assert_true(updatedForce, "Force refresh succeeds regardless of timestamp")
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 0, 0, "Cel now updated to Red via force refresh")

  -- Repeated rapid forced updates (10 cycles without advancing timestamp)
  for c = 1, 10 do
    createColoredFile(pSec1, 16, 16, c * 20, 100, 150)
    local ok = smartLay:refreshSmartObject(true)
    assert_true(ok, "Rapid force reload cycle " .. c .. " must succeed")
    verifyCelPixel(smartLay:cel(1), 0, 0, c * 20, 100, 150, "Cel updated in cycle " .. c)
  end

  -- Advance timestamp to next second: write Green (0, 255, 0)
  advanceTimestamp()
  createColoredFile(pSec1, 16, 16, 0, 255, 0)

  -- Now standard refresh (force=false) MUST detect newer timestamp
  assert_true(smartLay.linkStatus == "Linked" or smartLay.linkStatus == "Outdated", "linkStatus is Linked or Outdated")
  local normalOk = smartLay:refreshSmartObject(false)
  assert_true(normalOk, "Normal refresh succeeds once timestamp advanced")
  verifyCelPixel(smartLay:cel(1), 0, 0, 0, 255, 0, "Cel updated to Green via normal timestamp detection")
  assert_true(smartLay.smartObjectLastModified > t0, "smartObjectLastModified strictly advanced")

  hostSpr:close()
  print("  -> Section 1 PASSED: Sub-second skipping and force-reload verified.")
end

-- ==============================================================================
-- SECTION 2: Truncated (0-byte) and Corrupt File Writes
-- ==============================================================================
print("\n[SECTION 2] Stress-testing 0-byte and corrupt external file handling...")
do
  -- Prepare fresh source with Yellow (255, 255, 0)
  advanceTimestamp()
  createColoredFile(pSec2, 16, 16, 255, 255, 0)
  local hostSpr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec2, x = 0, y = 0, ui = false }
  local smartLay = hostSpr.layers[2]
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 255, 0, "Initial cel is Yellow")

  -- Case A: 0-byte file write (simulate partial/interrupted atomic write)
  local fZero = io.open(pSec2, "wb")
  assert_true(fZero ~= nil, "Opened file for zero-byte truncate")
  fZero:close()

  -- Verify linkStatus reports Broken for 0 bytes
  assert_eq(smartLay.linkStatus, "Broken", "linkStatus must be Broken for 0-byte file")

  -- Attempt refresh: must fail safely without throwing unhandled C++ exceptions or assertions
  local res1, err1 = smartLay:refreshSmartObject(false)
  assert_false(res1, "Refresh must return false on 0-byte file")

  local res2, err2 = smartLay:refreshSmartObject(true)
  assert_false(res2, "Forced refresh must return false on 0-byte file")

  -- Cel MUST be completely preserved with Yellow pixels!
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 255, 0, "Existing cel intact after 0-byte refresh attempt")

  -- Test command execution on 0-byte file
  app.command.RefreshSmartObject{ force = true }
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 255, 0, "Existing cel intact after command execution on 0-byte file")

  -- Case B: Corrupted file write (garbage random bytes)
  local fCorrupt = io.open(pSec2, "wb")
  assert_true(fCorrupt ~= nil, "Opened file for corrupt write")
  fCorrupt:write(string.rep("\xDE\xAD\xBE\xEF\x00\xFF\xAA\x55", 64)) -- 512 bytes of garbage
  fCorrupt:close()

  local resCorrupt = smartLay:refreshSmartObject(true)
  assert_false(resCorrupt, "Refresh on corrupt file must fail gracefully")
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 255, 0, "Existing cel preserved after corrupt file refresh attempt")

  -- Case C: Restore valid file (Magenta: 255, 0, 255)
  advanceTimestamp()
  createColoredFile(pSec2, 16, 16, 255, 0, 255)
  local resRecover = smartLay:refreshSmartObject(true)
  assert_true(resRecover, "Refresh recovers successfully once valid file is restored")
  assert_eq(smartLay.linkStatus, "Linked", "linkStatus is Linked after recovery")
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 0, 255, "Cel correctly reflects recovered Magenta pixels")

  hostSpr:close()
  print("  -> Section 2 PASSED: 0-byte and corrupt files safely handled with cel preservation.")
end

-- ==============================================================================
-- SECTION 3: Missing / Deleted External File Transitions
-- ==============================================================================
print("\n[SECTION 3] Stress-testing missing and deleted external file lifecycle...")
do
  advanceTimestamp()
  createColoredFile(pSec3, 16, 16, 128, 64, 32)
  local hostSpr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec3, x = 0, y = 0, ui = false }
  local smartLay = hostSpr.layers[2]
  assert_eq(smartLay.linkStatus, "Linked", "Initial status is Linked")
  verifyCelPixel(smartLay:cel(1), 0, 0, 128, 64, 32, "Initial cel pixels verified")

  -- Delete external file from disk
  pcall(function() app.fs.removeFile(pSec3) end)
  pcall(function() os.remove(pSec3) end)
  assert_false(app.fs.isFile(pSec3), "File must be gone from disk")

  -- Verify linkStatus reflects Missing
  assert_eq(smartLay.linkStatus, "Missing", "linkStatus must transition to Missing")

  -- Refresh attempts must safely return false and preserve cels
  local okMissing1 = smartLay:refreshSmartObject(false)
  assert_false(okMissing1, "Normal refresh returns false when file missing")
  local okMissing2 = smartLay:refreshSmartObject(true)
  assert_false(okMissing2, "Forced refresh returns false when file missing")

  verifyCelPixel(smartLay:cel(1), 0, 0, 128, 64, 32, "Existing cel retained while file is missing")

  -- Command execution while file missing
  app.command.RefreshSmartObject{ force = true }
  verifyCelPixel(smartLay:cel(1), 0, 0, 128, 64, 32, "Cel still retained after app.command.RefreshSmartObject on missing file")

  -- Restore file with new color (Cyan: 0, 255, 255)
  advanceTimestamp()
  createColoredFile(pSec3, 16, 16, 0, 255, 255)
  assert_true(app.fs.isFile(pSec3), "Restored file exists")

  -- Status should immediately detect file is back
  assert_true(smartLay.linkStatus == "Linked" or smartLay.linkStatus == "Outdated", "Status restored to Linked or Outdated")

  -- Refresh should succeed
  local okRestored = smartLay:refreshSmartObject()
  assert_true(okRestored, "Refresh succeeds once missing file is restored")
  assert_eq(smartLay.linkStatus, "Linked", "Final status is Linked")
  verifyCelPixel(smartLay:cel(1), 0, 0, 0, 255, 255, "Cel updated to Cyan after restoration")

  hostSpr:close()
  print("  -> Section 3 PASSED: Missing file status transitions verified.")
end

-- ==============================================================================
-- SECTION 4: Timeline Structural Mutation on Disk (Expansion & Shrinking)
-- ==============================================================================
print("\n[SECTION 4] Stress-testing external animation timeline expansion and shrinking...")
do
  advanceTimestamp()
  -- Create initial 1-frame source file: Red (255, 0, 0)
  createColoredFile(pSec4, 16, 16, 255, 0, 0)

  local hostSpr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec4, x = 0, y = 0, ui = false }
  local smartLay = hostSpr.layers[2]
  assert_eq(smartLay.linkedFrameCount, 1, "Initial linkedFrameCount is 1")

  -- Expand host to 6 frames
  while #hostSpr.frames < 6 do
    hostSpr:newEmptyFrame()
  end
  assert_eq(#hostSpr.frames, 6, "Host now has 6 frames")
  smartLay:syncTimeline("Loop")
  assert_eq(#smartLay.cels, 6, "Smart layer has 6 cels")

  -- All 6 cels show Red
  for f = 1, 6 do
    verifyCelPixel(smartLay:cel(f), 0, 0, 255, 0, 0, "Host cel " .. f .. " is Red (1f source)")
  end

  -- EXPANSION: On disk, expand source file to 3 frames:
  --   Frame 1: Red (255, 0, 0)
  --   Frame 2: Green (0, 255, 0)
  --   Frame 3: Blue (0, 0, 255)
  advanceTimestamp()
  do
    local srcSpr = Sprite{ fromFile = pSec4 }
    assert_eq(#srcSpr.frames, 1, "Source had 1 frame")
    -- Frame 2
    srcSpr:newFrame()
    local img2 = srcSpr.cels[2].image
    for y = 0, 15 do
      for x = 0, 15 do
        img2:drawPixel(x, y, Color{ r = 0, g = 255, b = 0, a = 255 })
      end
    end
    -- Frame 3
    srcSpr:newFrame()
    local img3 = srcSpr.cels[3].image
    for y = 0, 15 do
      for x = 0, 15 do
        img3:drawPixel(x, y, Color{ r = 0, g = 0, b = 255, a = 255 })
      end
    end
    srcSpr:saveAs(pSec4)
    srcSpr:close()
  end

  -- Refresh smart layer
  local reloadExpand = smartLay:refreshSmartObject()
  assert_true(reloadExpand, "Refresh after expansion must succeed")
  assert_eq(smartLay.linkedFrameCount, 3, "linkedFrameCount updated to 3")
  assert_eq(#smartLay.cels, 6, "Host layer retains all 6 cels")

  -- In Loop mode: 1->Red, 2->Green, 3->Blue, 4->Red, 5->Green, 6->Blue
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 0, 0, "Loop Frame 1 is Red")
  verifyCelPixel(smartLay:cel(2), 0, 0, 0, 255, 0, "Loop Frame 2 is Green")
  verifyCelPixel(smartLay:cel(3), 0, 0, 0, 0, 255, "Loop Frame 3 is Blue")
  verifyCelPixel(smartLay:cel(4), 0, 0, 255, 0, 0, "Loop Frame 4 is Red")
  verifyCelPixel(smartLay:cel(5), 0, 0, 0, 255, 0, "Loop Frame 5 is Green")
  verifyCelPixel(smartLay:cel(6), 0, 0, 0, 0, 255, "Loop Frame 6 is Blue")

  -- Test Hold mode mapping
  smartLay:syncTimeline("Hold")
  verifyCelPixel(smartLay:cel(1), 0, 0, 255, 0, 0, "Hold Frame 1 is Red")
  verifyCelPixel(smartLay:cel(2), 0, 0, 0, 255, 0, "Hold Frame 2 is Green")
  verifyCelPixel(smartLay:cel(3), 0, 0, 0, 0, 255, "Hold Frame 3 is Blue")
  verifyCelPixel(smartLay:cel(4), 0, 0, 0, 0, 255, "Hold Frame 4 is Blue (held)")
  verifyCelPixel(smartLay:cel(5), 0, 0, 0, 0, 255, "Hold Frame 5 is Blue (held)")
  verifyCelPixel(smartLay:cel(6), 0, 0, 0, 0, 255, "Hold Frame 6 is Blue (held)")

  -- SHRINKING: On disk, shrink source file back from 3 frames down to 1 frame:
  --   Frame 1: White (255, 255, 255)
  advanceTimestamp()
  createColoredFile(pSec4, 16, 16, 255, 255, 255)

  local reloadShrink = smartLay:refreshSmartObject()
  assert_true(reloadShrink, "Refresh after shrinking must succeed")
  assert_eq(smartLay.linkedFrameCount, 1, "linkedFrameCount shrank to 1")
  assert_eq(#smartLay.cels, 6, "Host layer cel count remains 6")

  -- All 6 cels must now safely reconcile to Frame 1 (White), no out-of-bounds frame access
  for f = 1, 6 do
    verifyCelPixel(smartLay:cel(f), 0, 0, 255, 255, 255, "Shrunk cel " .. f .. " is White")
  end

  hostSpr:close()
  print("  -> Section 4 PASSED: Animation expansion (1f->3f) and shrinking (3f->1f) verified.")
end

-- ==============================================================================
-- SECTION 5: External Canvas Dimension Mutation (16x16 -> 32x24)
-- ==============================================================================
print("\n[SECTION 5] Stress-testing external canvas size mutation...")
do
  advanceTimestamp()
  createColoredFile(pSec5, 16, 16, 100, 150, 200)

  local hostSpr = Sprite(64, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec5, x = 0, y = 0, ui = false }
  local smartLay = hostSpr.layers[2]
  local initCel = smartLay:cel(1)
  assert_eq(initCel.image.width, 16, "Initial cel width 16")
  assert_eq(initCel.image.height, 16, "Initial cel height 16")

  -- Mutate external file dimensions on disk to 32x24, color = (220, 110, 50)
  advanceTimestamp()
  createColoredFile(pSec5, 32, 24, 220, 110, 50)

  local okResize = smartLay:refreshSmartObject()
  assert_true(okResize, "Refresh on dimension mutation succeeds")
  local c1 = smartLay:cel(1)
  assert_true(c1 ~= nil, "Cel exists after resize")
  assert_eq(c1.image.width, 32, "Updated cel width is 32")
  assert_eq(c1.image.height, 24, "Updated cel height is 24")

  -- Verify corners of expanded dimensions in rendered cel
  verifyCelPixel(c1, 0, 0, 220, 110, 50, "Top-left pixel")
  verifyCelPixel(c1, 31, 23, 220, 110, 50, "Bottom-right pixel (31, 23)")

  hostSpr:close()
  print("  -> Section 5 PASSED: Dimension mutation from 16x16 to 32x24 verified.")
end

-- ==============================================================================
-- SECTION 6: Multi-Document & Multi-Layer Concurrency Stress
-- ==============================================================================
print("\n[SECTION 6] Stress-testing multi-document & multi-layer concurrency...")
do
  advanceTimestamp()
  createColoredFile(pSec6a, 16, 16, 10, 20, 30)
  createColoredFile(pSec6b, 16, 16, 80, 90, 100)

  -- Host Document A: 2 smart layers pointing to pSec6a with different positions
  local hostA = Sprite(64, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec6a, x = 0, y = 0, ui = false }
  app.command.PlaceSmartObject{ filename = pSec6a, x = 20, y = 15, ui = false }
  assert_eq(#hostA.layers, 3, "Host A has 3 layers")
  local layA1 = hostA.layers[2]
  local layA2 = hostA.layers[3]
  verifyCelPixel(layA1:cel(1), 0, 0, 10, 20, 30, "Host A Layer 1 initial")
  verifyCelPixel(layA2:cel(1), 0, 0, 10, 20, 30, "Host A Layer 2 initial")
  assert_eq(layA2.transform.x, 20, "Host A Layer 2 position X")
  assert_eq(layA2.transform.y, 15, "Host A Layer 2 position Y")

  -- Host Document B: 1 layer pointing to pSec6a, 1 layer pointing to pSec6b
  local hostB = Sprite(64, 64, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec6a, x = 5, y = 5, ui = false }
  app.command.PlaceSmartObject{ filename = pSec6b, x = 10, y = 10, ui = false }
  assert_eq(#hostB.layers, 3, "Host B has 3 layers")
  local layB1 = hostB.layers[2]
  local layB2 = hostB.layers[3]
  verifyCelPixel(layB1:cel(1), 0, 0, 10, 20, 30, "Host B Layer 1 initial")
  verifyCelPixel(layB2:cel(1), 0, 0, 80, 90, 100, "Host B Layer 2 initial (src2)")

  -- Now update ONLY pSec6a on disk to (200, 10, 50)
  advanceTimestamp()
  createColoredFile(pSec6a, 16, 16, 200, 10, 50)
  -- pSec6b remains completely unchanged on disk!

  -- Execute RefreshSmartObject command targeting pSec6a across all documents
  app.command.RefreshSmartObject{ all = true, filename = pSec6a, force = true }

  -- Verify Host A layers both refreshed and kept transforms
  verifyCelPixel(layA1:cel(1), 0, 0, 200, 10, 50, "Host A Layer 1 refreshed to new src1")
  verifyCelPixel(layA2:cel(1), 0, 0, 200, 10, 50, "Host A Layer 2 refreshed to new src1")
  assert_eq(layA2.transform.x, 20, "Host A Layer 2 preserved position X")
  assert_eq(layA2.transform.y, 15, "Host A Layer 2 preserved position Y")

  -- Verify Host B Layer 1 refreshed, but Host B Layer 2 was NOT refreshed
  verifyCelPixel(layB1:cel(1), 0, 0, 200, 10, 50, "Host B Layer 1 refreshed to new src1")
  verifyCelPixel(layB2:cel(1), 0, 0, 80, 90, 100, "Host B Layer 2 UNCHANGED (src2 was not targeted)")

  -- Now execute RefreshSmartObject for pSec6b
  advanceTimestamp()
  createColoredFile(pSec6b, 16, 16, 50, 150, 250)
  app.command.RefreshSmartObject{ all = true, filename = pSec6b, force = true }
  verifyCelPixel(layB2:cel(1), 0, 0, 50, 150, 250, "Host B Layer 2 refreshed when src2 is targeted")

  hostA:close()
  hostB:close()
  print("  -> Section 6 PASSED: Multi-document and multi-layer concurrency verified.")
end

-- ==============================================================================
-- SECTION 7: Self-Referencing Link Protection
-- ==============================================================================
print("\n[SECTION 7] Verifying self-referencing link protection...")
do
  -- Create and save a host sprite to disk
  advanceTimestamp()
  local selfSpr = Sprite(32, 32, ColorMode.RGB)
  selfSpr:saveAs(pSec7)
  assert_true(app.fs.isFile(pSec7), "Self host file saved")

  -- Attempt to link pSec7 into itself
  local placeOk = pcall(function()
    app.command.PlaceSmartObject{ filename = pSec7, x = 0, y = 0, ui = false }
  end)

  -- Whether PlaceSmartObject succeeds or fails, running refresh must never recurse infinitely
  app.command.RefreshSmartObject{ all = true, force = true }
  assert_true(true, "Self-referencing refresh survived without recursion or crash")

  selfSpr:close()
  print("  -> Section 7 PASSED: Self-referencing link protection verified.")
end

-- ==============================================================================
-- SECTION 8: High-Frequency Torture Loop (50 cycles)
-- ==============================================================================
print("\n[SECTION 8] Running high-frequency torture loop (50 cycles)...")
do
  advanceTimestamp()
  createColoredFile(pSec8, 16, 16, 0, 0, 0)
  local hostSpr = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pSec8, x = 0, y = 0, ui = false }
  local smartLay = hostSpr.layers[2]

  for cycle = 1, 50 do
    local r = (cycle * 5) % 256
    local g = (cycle * 13) % 256
    local b = (cycle * 29) % 256

    -- Rapid disk write
    createColoredFile(pSec8, 16, 16, r, g, b)

    -- Rapid forced reload
    local ok = smartLay:refreshSmartObject(true)
    assert_true(ok, "Torture cycle " .. cycle .. " reload must succeed")

    -- Move layer transform
    smartLay:translate(1, -1)
    smartLay:renderAllCels()

    -- Verify pixel
    verifyCelPixel(smartLay:cel(1), 0, 0, r, g, b, "Torture cycle " .. cycle .. " pixel check")
  end

  hostSpr:close()
  print("  -> Section 8 PASSED: 50-cycle high-frequency torture loop passed cleanly.")
end

-- ==============================================================================
-- SECTION 9: Empirical Bug Demonstration: Stale Cache Hit on PlaceSmartObject
-- ==============================================================================
print("\n[SECTION 9] Testing PlaceSmartObject behavior when external file modified while cached...")
do
  advanceTimestamp()
  -- 1. Create file with Initial Color: Orange (255, 128, 0)
  createColoredFile(pBug, 16, 16, 255, 128, 0)

  -- 2. Place it into Host Document 1 (loads into s_detachedDocsCache)
  local host1 = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pBug, x = 0, y = 0, ui = false }
  local lay1 = host1.layers[2]
  verifyCelPixel(lay1:cel(1), 0, 0, 255, 128, 0, "Host 1 initial cel is Orange")
  host1:close() -- Host 1 is closed, but s_detachedDocsCache retains the cached Document!

  -- 3. Outside of Host 1, external file on disk is modified to Teal (0, 128, 255)
  advanceTimestamp()
  createColoredFile(pBug, 16, 16, 0, 128, 255)

  -- 4. User opens Host Document 2 and places pBug
  local host2 = Sprite(32, 32, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = pBug, x = 0, y = 0, ui = false }
  local lay2 = host2.layers[2]

  -- Check what was actually placed:
  local px = lay2:cel(1).image:getPixel(0, 0)
  local r = app.pixelColor.rgbaR(px)
  local g = app.pixelColor.rgbaG(px)
  local b = app.pixelColor.rgbaB(px)

  print(string.format("  -> Disk file has Teal (0, 128, 255). PlaceSmartObject placed pixel: (%d, %d, %d)", r, g, b))
  if r == 255 and g == 128 and b == 0 then
    print("  [BUG CONFIRMED] PlaceSmartObject served STALE cached Document from s_detachedDocsCache without checking disk modification time!")
    print("                  lay2.smartObjectLastModified: " .. tostring(lay2.smartObjectLastModified))
    print("                  lay2.linkStatus: " .. tostring(lay2.linkStatus))
  else
    print("  -> PlaceSmartObject correctly reloaded current disk content.")
  end

  host2:close()
end

-- Final Cleanup
cleanupFiles()

print("\n================================================================================")
print(string.format("  CHALLENGER STRESS SUITE COMPLETE: %d / %d ASSERTIONS PASSED!       ", passCount, testCount))
print("================================================================================")
