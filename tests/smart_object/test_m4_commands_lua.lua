-- ==============================================================================
-- Test Suite: test_m4_commands_lua.lua
-- Milestone 4: Smart Object Lua Scripting API & Command Integration
-- ==============================================================================

print("======================================================")
print("  Running Milestone 4 Lua Scripting & Commands Tests   ")
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

local fixturePath = "tests/smart_object/fixtures/single_frame_16x16.aseprite"
local altFixturePath = "tests/smart_object/fixtures/single_frame_32x32.aseprite"

local spr = Sprite(64, 64)
local regLayer = spr.layers[1]

-- ==============================================================================
-- 1. Regular Layer Baseline Checks
-- ==============================================================================
print("\n[Section 1] Verifying non-smart layer properties...")
assert_false(regLayer.isSmartObject, "Regular layer is not smart object")
assert_true(regLayer.smartObjectFilepath == nil, "Regular layer has nil smartObjectFilepath")
assert_eq(regLayer.smartObjectLastModified, 0, "Regular layer has 0 lastModified")
assert_true(regLayer.smartObjectTimelineMapping == nil, "Regular layer has nil timelineMapping")

-- Read-only protection check: attempting to assign isSmartObject must fail
local okWrite = pcall(function()
  regLayer.isSmartObject = true
end)
assert_false(okWrite, "Writing to isSmartObject must raise error")
print("  -> Regular layer baseline & read-only protection verified.")

-- ==============================================================================
-- 2. Smart Object Layer Creation via PlaceSmartObject
-- ==============================================================================
print("\n[Section 2] Placing Smart Object Layer...")
app.command.PlaceSmartObject{
  filename = fixturePath,
  name = "MySmartLayer",
  x = 10,
  y = 12,
  ui = false
}

assert_eq(#spr.layers, 2, "Sprite now has 2 layers")
local sm = spr.layers[2]
assert_eq(sm.name, "MySmartLayer", "Layer name matches")
assert_true(sm.isSmartObject, "Layer identified as smart object")

-- Verify read-only enforcement on smart layer
local okWriteSmart = pcall(function()
  sm.isSmartObject = false
end)
assert_false(okWriteSmart, "Writing to sm.isSmartObject must raise error")
print("  -> Layer creation and properties verified.")

-- ==============================================================================
-- 3. Property smartObjectFilepath & sourcePath Alias
-- ==============================================================================
print("\n[Section 3] Verifying filepath properties and re-linking...")
assert_true(sm.smartObjectFilepath ~= nil, "Filepath is not nil")
assert_eq(sm.sourcePath, sm.smartObjectFilepath, "sourcePath alias matches smartObjectFilepath")

-- Test writable re-linking to another fixture file
sm.smartObjectFilepath = altFixturePath
assert_true(sm.smartObjectFilepath:find("single_frame_32x32.aseprite") ~= nil, "Path updated to 32x32")
local cel32 = sm:cel(1)
assert_eq(cel32.image.width, 32, "Cel re-rendered to 32x32")

-- Re-link back
sm.smartObjectFilepath = fixturePath
assert_eq(sm:cel(1).image.width, 16, "Cel re-rendered back to 16x16")
print("  -> Filepath get/set and re-linking verified.")

-- ==============================================================================
-- 4. Property smartObjectLastModified
-- ==============================================================================
print("\n[Section 4] Verifying smartObjectLastModified...")
local ts = sm.smartObjectLastModified
assert_true(type(ts) == "number" and ts > 0, "Last modified is positive number")

-- Writable test
sm.smartObjectLastModified = 123456789
assert_eq(sm.smartObjectLastModified, 123456789, "Timestamp writable check passed")
sm.smartObjectLastModified = ts -- restore
print("  -> smartObjectLastModified verified.")

-- ==============================================================================
-- 5. Property smartObjectTimelineMapping & timelineMapping Alias
-- ==============================================================================
print("\n[Section 5] Verifying timeline mapping properties...")
assert_true(sm.smartObjectTimelineMapping == "loop" or sm.smartObjectTimelineMapping == "Loop")

-- Case-insensitive setters
sm.smartObjectTimelineMapping = "hold"
assert_true(sm.smartObjectTimelineMapping:lower() == "hold", "Mapping set to hold")

sm.smartObjectTimelineMapping = "FIXED"
assert_true(sm.smartObjectTimelineMapping:lower() == "fixed", "Mapping set to FIXED")

sm.smartObjectTimelineMapping = "Loop"
assert_true(sm.smartObjectTimelineMapping:lower() == "loop", "Mapping set to Loop")
print("  -> smartObjectTimelineMapping verified.")

-- ==============================================================================
-- 6. Methods: refreshSmartObject, transforms, syncTimeline
-- ==============================================================================
print("\n[Section 6] Verifying Smart Object methods...")
-- Force refresh
local refOk = sm:refreshSmartObject(true)
assert_true(refOk, "sm:refreshSmartObject(true) succeeds")

-- Non-smart layer method invocation
local regRefOk, regErrMsg = pcall(function() return regLayer:refreshSmartObject() end)
if regRefOk then
  assert_false(regErrMsg, "Non-smart layer returns false")
end

-- 2D Transform methods
sm:setTransform(0, 0, 2.0, 2.0, 0.0, false, false)
assert_eq(sm.scale.x, 2.0, "Scale X is 2.0")
assert_eq(sm:cel(1).image.width, 32, "Transformed cel width is 32")

sm:resetTransform()
assert_eq(sm.scale.x, 1.0, "Reset scale is 1.0")
assert_eq(sm:cel(1).image.width, 16, "Reset cel width is 16")

sm:translate(10, 10)
local t = sm.transform
assert_eq(t.x, 10, "Translated X is 10")
assert_eq(t.y, 10, "Translated Y is 10")
print("  -> Methods verified.")

-- ==============================================================================
-- 7. Command RefreshSmartObject Execution & Flags
-- ==============================================================================
print("\n[Section 7] Testing app.command.RefreshSmartObject...")
assert_true(app.command.RefreshSmartObject.enabled, "RefreshSmartObject command is enabled")

app.command.RefreshSmartObject{ force = true }
app.command.RefreshSmartObject{ all = true, force = true }
print("  -> Command execution verified.")

-- ==============================================================================
-- 8. UserData Persistence Roundtrip
-- ==============================================================================
print("\n[Section 8] Verifying UserData persistence across file save and reload...")
local tempSave = app.fs.joinPath(app.fs.tempPath or ".", "test_m4_roundtrip.aseprite")
pcall(function() app.fs.removeFile(tempSave) end)

spr:saveAs(tempSave)
spr:close()

local loadedSpr = Sprite{ fromFile = tempSave }
assert_eq(#loadedSpr.layers, 2, "Loaded sprite has 2 layers")
local reloadedSm = loadedSpr.layers[2]
assert_true(reloadedSm.isSmartObject, "Persisted layer is still smart object")
assert_true(reloadedSm.smartObjectFilepath:find("single_frame_16x16.aseprite") ~= nil, "Filepath restored")
assert_true(reloadedSm.smartObjectLastModified > 0, "LastModified restored")

loadedSpr:close()
pcall(function() app.fs.removeFile(tempSave) end)
print("  -> UserData persistence roundtrip verified.")

print("\n======================================================")
print("  ALL M4 LUA SCRIPTING & COMMANDS TESTS PASSED (8/8)!  ")
print("======================================================")
