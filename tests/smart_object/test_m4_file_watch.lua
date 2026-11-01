-- ==============================================================================
-- Test Suite: test_m4_file_watch.lua
-- Milestone 4: Smart Object Disk Modification Tracking & Cel Synchronization
-- ==============================================================================

print("======================================================")
print("  Running Milestone 4 File Watch & Refresh Test Suite ")
print("======================================================")

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "mismatch", tostring(expected), tostring(actual)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error("ASSERTION FAILED: " .. (msg or "expected true"), 2)
  end
end

local function assert_false(cond, msg)
  if cond then
    error("ASSERTION FAILED: " .. (msg or "expected false"), 2)
  end
end

-- Filesystem timestamp advance helper (ensures next file write has strictly newer timestamp)
local function advanceTimestamp()
  local start = os.time()
  while os.time() <= start do
    -- wait for system second boundary
  end
end

local tempDir = app.fs.tempPath or "."
local srcPath = app.fs.joinPath(tempDir, "test_m4_source.aseprite")
local hostPath = app.fs.joinPath(tempDir, "test_m4_host.aseprite")

-- Clean up any stale temp files from previous runs
pcall(function() app.fs.removeFile(srcPath) end)
pcall(function() app.fs.removeFile(hostPath) end)

-- ==============================================================================
-- Step 1: Create Initial Source File on Disk (16x16 RGB, Initial Color: Cyan)
-- ==============================================================================
print("\n[Step 1] Creating initial source file on disk...")
do
  local srcSpr = Sprite(16, 16, ColorMode.RGB)
  local img = srcSpr.cels[1].image
  -- Draw Cyan (0, 255, 255)
  for y = 0, 15 do
    for x = 0, 15 do
      img:drawPixel(x, y, Color{ r = 0, g = 255, b = 255, a = 255 })
    end
  end
  srcSpr:saveAs(srcPath)
  srcSpr:close()
  assert_true(app.fs.isFile(srcPath), "Source file must exist on disk")
  print("  -> Source file written to: " .. srcPath)
end

-- ==============================================================================
-- Step 2: Create Host Document & Place Smart Object
-- ==============================================================================
print("\n[Step 2] Importing source into host document via PlaceSmartObject...")
local hostSpr = Sprite(32, 32, ColorMode.RGB)
app.command.PlaceSmartObject{
  filename = srcPath,
  x = 0,
  y = 0,
  ui = false
}

assert_eq(#hostSpr.layers, 2, "Host sprite must have 2 layers")
local smartLayer = hostSpr.layers[2]
assert_true(smartLayer.isSmartObject, "Layer must be smart object")
assert_eq(smartLayer.smartObjectFilepath, srcPath, "Stored filepath must match")

local initialCel = smartLayer:cel(1)
assert_true(initialCel ~= nil, "Cel 1 must exist")
local px0 = initialCel.image:getPixel(0, 0)
assert_eq(app.pixelColor.rgbaR(px0), 0, "Initial R must be 0")
assert_eq(app.pixelColor.rgbaG(px0), 255, "Initial G must be 255")
assert_eq(app.pixelColor.rgbaB(px0), 255, "Initial B must be 255")

local initialTime = smartLayer.smartObjectLastModified
assert_true(type(initialTime) == "number" and initialTime > 0, "Last modified must be positive number")
print("  -> Initial placement verified with Cyan pixels. Timestamp: " .. initialTime)

-- ==============================================================================
-- Step 3: Verify Unchanged File Timestamp Skips Refresh
-- ==============================================================================
print("\n[Step 3] Verifying refresh is skipped when disk file is unchanged...")
do
  local updated, status = smartLayer:refreshSmartObject(false)
  assert_false(updated, "Refresh must return false when file unchanged")
  -- Cel pixels must remain intact
  local pxSame = smartLayer:cel(1).image:getPixel(0, 0)
  assert_eq(app.pixelColor.rgbaB(pxSame), 255, "Pixels remain Cyan")
  print("  -> Unchanged skip verified.")
end

-- ==============================================================================
-- Step 4: Modify Source File on Disk (Pixel Color Change: Magenta)
-- ==============================================================================
print("\n[Step 4] Modifying source file pixels on disk to Magenta...")
advanceTimestamp()
do
  local srcSpr = Sprite{ fromFile = srcPath }
  local img = srcSpr.cels[1].image
  -- Overwrite with Magenta (255, 0, 255)
  for y = 0, 15 do
    for x = 0, 15 do
      img:drawPixel(x, y, Color{ r = 255, g = 0, b = 255, a = 255 })
    end
  end
  srcSpr:saveAs(srcPath)
  srcSpr:close()
  print("  -> Source file overwritten with Magenta pixels.")
end

-- Refresh smart object in host document
local updateResult = smartLayer:refreshSmartObject()
assert_true(updateResult, "smartLayer:refreshSmartObject() must succeed on modified file")

local updatedCel = smartLayer:cel(1)
local pxNew = updatedCel.image:getPixel(0, 0)
assert_eq(app.pixelColor.rgbaR(pxNew), 255, "Updated pixel R must be 255 (Magenta)")
assert_eq(app.pixelColor.rgbaG(pxNew), 0, "Updated pixel G must be 0")
assert_eq(app.pixelColor.rgbaB(pxNew), 255, "Updated pixel B must be 255 (Magenta)")
assert_true(smartLayer.smartObjectLastModified > initialTime, "Timestamp must advance")
print("  -> Host cel successfully refreshed to Magenta.")

-- ==============================================================================
-- Step 5: Multi-Frame Expansion on Disk (1 Frame -> 3 Frames)
-- ==============================================================================
print("\n[Step 5] Expanding source file animation on disk to 3 frames...")
advanceTimestamp()
do
  local srcSpr = Sprite{ fromFile = srcPath }
  -- Frame 1: Magenta (255, 0, 255)
  -- Frame 2: Yellow (255, 255, 0)
  srcSpr:newFrame()
  local img2 = srcSpr.cels[2].image
  for y = 0, 15 do
    for x = 0, 15 do
      img2:drawPixel(x, y, Color{ r = 255, g = 255, b = 0, a = 255 })
    end
  end
  -- Frame 3: Green (0, 255, 0)
  srcSpr:newFrame()
  local img3 = srcSpr.cels[3].image
  for y = 0, 15 do
    for x = 0, 15 do
      img3:drawPixel(x, y, Color{ r = 0, g = 255, b = 0, a = 255 })
    end
  end
  srcSpr:saveAs(srcPath)
  srcSpr:close()
  print("  -> Source file now contains 3 frames.")
end

-- Expand host document to 3 frames
while #hostSpr.frames < 3 do
  hostSpr:newEmptyFrame()
end
assert_eq(#hostSpr.frames, 3, "Host sprite now has 3 frames")

-- Refresh layer
smartLayer:refreshSmartObject()
assert_eq(smartLayer.linkedFrameCount, 3, "Linked frame count updated to 3")

-- Verify multi-frame synchronization
local pF1 = smartLayer:cel(1).image:getPixel(0, 0)
assert_eq(app.pixelColor.rgbaR(pF1), 255, "Frame 1 R is 255")
assert_eq(app.pixelColor.rgbaG(pF1), 0, "Frame 1 G is 0")

local pF2 = smartLayer:cel(2).image:getPixel(0, 0)
assert_eq(app.pixelColor.rgbaR(pF2), 255, "Frame 2 R is 255 (Yellow)")
assert_eq(app.pixelColor.rgbaG(pF2), 255, "Frame 2 G is 255 (Yellow)")

local pF3 = smartLayer:cel(3).image:getPixel(0, 0)
assert_eq(app.pixelColor.rgbaR(pF3), 0, "Frame 3 R is 0")
assert_eq(app.pixelColor.rgbaG(pF3), 255, "Frame 3 G is 255 (Green)")
print("  -> Multi-frame timeline synchronization verified.")

-- ==============================================================================
-- Step 6: Command Refresh Integration (app.command.RefreshSmartObject)
-- ==============================================================================
print("\n[Step 6] Testing app.command.RefreshSmartObject execution...")
advanceTimestamp()
do
  local srcSpr = Sprite{ fromFile = srcPath }
  local img1 = srcSpr.cels[1].image
  -- Modify Frame 1 to Red (255, 0, 0)
  for y = 0, 15 do
    for x = 0, 15 do
      img1:drawPixel(x, y, Color{ r = 255, g = 0, b = 0, a = 255 })
    end
  end
  srcSpr:saveAs(srcPath)
  srcSpr:close()
end

app.command.RefreshSmartObject{ force = true }
local pCmdF1 = smartLayer:cel(1).image:getPixel(0, 0)
assert_eq(app.pixelColor.rgbaR(pCmdF1), 255, "Command refresh updated Frame 1 R to 255")
assert_eq(app.pixelColor.rgbaB(pCmdF1), 0, "Command refresh updated Frame 1 B to 0 (Pure Red)")
print("  -> app.command.RefreshSmartObject successfully executed.")

-- ==============================================================================
-- Step 7: Resilience & Error Handling (Missing File)
-- ==============================================================================
print("\n[Step 7] Testing missing file error handling...")
local backupPath = srcPath .. ".bak"
pcall(function() app.fs.removeFile(backupPath) end)
os.rename(srcPath, backupPath)

local failResult, failMsg = smartLayer:refreshSmartObject()
assert_false(failResult, "Refresh should return false when file is missing")
-- Existing cels must NOT be deleted or destroyed
assert_true(smartLayer:cel(1) ~= nil, "Existing cached cel preserved")

-- Restore file and recover
os.rename(backupPath, srcPath)
local recoverResult = smartLayer:refreshSmartObject(true)
assert_true(recoverResult, "Refresh recovers once file is restored")
print("  -> Resilience verified.")

-- Cleanup
hostSpr:close()
pcall(function() app.fs.removeFile(srcPath) end)
pcall(function() app.fs.removeFile(hostPath) end)

print("\n======================================================")
print("  ALL M4 FILE WATCH & REFRESH TESTS PASSED (7/7)!      ")
print("======================================================")
