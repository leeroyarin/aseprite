-- ==============================================================================
-- Test Suite: test_m4_commands_stress.lua
-- Empirical Adversarial Stress Harness: Smart Object Commands & Lua Integration
-- Author: challenger_smart_m4_2
-- ==============================================================================

print("======================================================")
print("  Smart Object M4 Commands & Lua Adversarial Stress   ")
print("======================================================")

local testCount = 0
local passCount = 0
local failCount = 0

local function check(desc, fn)
  testCount = testCount + 1
  local ok, err = pcall(fn)
  if ok then
    passCount = passCount + 1
    print(string.format("  [PASS %02d] %s", testCount, desc))
  else
    failCount = failCount + 1
    print(string.format("  [FAIL %02d] %s", testCount, desc))
    print("         Error: " .. tostring(err))
  end
end

local function assert_eq(a, b, msg)
  if a ~= b then
    error(string.format("%s (expected '%s', got '%s')", msg or "mismatch", tostring(b), tostring(a)), 2)
  end
end

local function assert_true(cond, msg)
  if not cond then
    error(msg or "expected true, got false", 2)
  end
end

local function assert_false(cond, msg)
  if cond then
    error(msg or "expected false, got true", 2)
  end
end

local function assert_error(fn, msg)
  local ok, err = pcall(fn)
  if ok then
    error("Expected error was NOT thrown: " .. (msg or ""), 2)
  end
end

local fixture16 = "tests/smart_object/fixtures/single_frame_16x16.aseprite"
local fixture32 = "tests/smart_object/fixtures/single_frame_32x32.aseprite"
local emptyPath = "tests/smart_object/fixtures/empty.aseprite"
local corruptPath = "tests/smart_object/fixtures/corrupt.aseprite"

-- ==============================================================================
-- SECTION 1: smartObjectFilepath & sourcePath Boundary Conditions
-- ==============================================================================
print("\n--- SECTION 1: smartObjectFilepath & sourcePath Boundary Conditions ---")

check("1.1 Setting smartObjectFilepath to nil must fail with Lua error", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]
  assert_true(sm.isSmartObject, "sm is smart object")
  local originalPath = sm.smartObjectFilepath

  assert_error(function()
    sm.smartObjectFilepath = nil
  end, "Assigning nil to smartObjectFilepath must error")

  assert_eq(sm.smartObjectFilepath, originalPath, "Path unchanged after nil error")
  spr:close()
end)

check("1.2 Setting smartObjectFilepath to empty string must fail with Lua error", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_error(function()
    sm.smartObjectFilepath = ""
  end, "Assigning empty string to smartObjectFilepath must error")

  spr:close()
end)

check("1.3 Setting smartObjectFilepath to non-existent path handles gracefully without crash", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]
  local nonExistent = "tests/smart_object/fixtures/does_not_exist_xyz123.aseprite"

  -- Should assign path without crash
  sm.smartObjectFilepath = nonExistent
  assert_true(sm.smartObjectFilepath:find("does_not_exist_xyz123.aseprite") ~= nil, "Filepath updated")
  assert_eq(sm.linkStatus, "Missing", "linkStatus must be Missing for non-existent file")

  -- Previous cel content must remain intact and not deleted or nil
  local c = sm:cel(1)
  assert_true(c ~= nil and c.image ~= nil, "Cel image intact after missing file re-link")
  assert_eq(c.image.width, 16, "Preserved cel width 16")

  -- Refreshing missing file returns false with error message
  local ok, msg = sm:refreshSmartObject(false)
  assert_false(ok, "refreshSmartObject on missing file returns false")
  assert_true(msg == "Linked file missing on disk", "Expected missing file message, got: " .. tostring(msg))

  local okForce, msgForce = sm:refreshSmartObject(true)
  assert_false(okForce, "Forced refresh on missing file returns false")
  assert_true(msgForce == "Linked file missing on disk", "Expected missing file message on force")
  spr:close()
end)

check("1.4 Re-linking to 0-byte file (empty.aseprite) preserves cels and reports Broken", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  sm.smartObjectFilepath = emptyPath
  assert_eq(sm.linkStatus, "Broken", "0-byte file must report Broken status")
  assert_true(sm:cel(1) ~= nil, "Cel preserved on 0-byte link")

  local ok, msg = sm:refreshSmartObject(true)
  assert_false(ok, "refreshSmartObject on 0-byte file returns false")
  spr:close()
end)

check("1.5 Re-linking to corrupt file (corrupt.aseprite) preserves cels safely", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  sm.smartObjectFilepath = corruptPath
  assert_true(sm:cel(1) ~= nil, "Cel preserved on corrupt link")

  local ok, msg = sm:refreshSmartObject(true)
  assert_false(ok, "refreshSmartObject on corrupt file returns false")
  spr:close()
end)

check("1.6 sourcePath alias get/set parity with smartObjectFilepath", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_eq(sm.sourcePath, sm.smartObjectFilepath, "sourcePath getter matches smartObjectFilepath")
  sm.sourcePath = fixture32
  assert_true(sm.smartObjectFilepath:find("single_frame_32x32.aseprite") ~= nil, "sourcePath setter updates path")
  assert_eq(sm:cel(1).image.width, 32, "Cel re-rendered to 32x32 via sourcePath")
  spr:close()
end)

-- ==============================================================================
-- SECTION 2: smartObjectTimelineMapping & timelineMapping Boundary Conditions
-- ==============================================================================
print("\n--- SECTION 2: smartObjectTimelineMapping & timelineMapping Boundary Conditions ---")

check("2.1 smartObjectTimelineMapping with invalid strings defaults gracefully to 'loop'", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  sm.smartObjectTimelineMapping = "unknown"
  assert_eq(sm.smartObjectTimelineMapping, "loop", "unknown defaults to loop")

  sm.smartObjectTimelineMapping = ""
  assert_eq(sm.smartObjectTimelineMapping, "loop", "empty string defaults to loop")

  sm.smartObjectTimelineMapping = "12345"
  assert_eq(sm.smartObjectTimelineMapping, "loop", "numeric string defaults to loop")

  sm.smartObjectTimelineMapping = "xyz_invalid_mapping"
  assert_eq(sm.smartObjectTimelineMapping, "loop", "random string defaults to loop")
  spr:close()
end)

check("2.2 smartObjectTimelineMapping with nil throws Lua error", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_error(function()
    sm.smartObjectTimelineMapping = nil
  end, "Assigning nil to smartObjectTimelineMapping must error")

  spr:close()
end)

check("2.3 smartObjectTimelineMapping case-insensitivity: hold, fixed, loop", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  -- Hold variations
  for _, v in ipairs({"hold", "HOLD", "Hold", "hOlD"}) do
    sm.smartObjectTimelineMapping = v
    assert_eq(sm.smartObjectTimelineMapping, "hold", "Mapping set to hold via: " .. v)
  end

  -- Fixed variations
  for _, v in ipairs({"fixed", "FIXED", "Fixed", "fIxEd"}) do
    sm.smartObjectTimelineMapping = v
    assert_eq(sm.smartObjectTimelineMapping, "fixed", "Mapping set to fixed via: " .. v)
  end

  -- Loop variations
  for _, v in ipairs({"loop", "LOOP", "Loop", "lOoP"}) do
    sm.smartObjectTimelineMapping = v
    assert_eq(sm.smartObjectTimelineMapping, "loop", "Mapping set to loop via: " .. v)
  end
  spr:close()
end)

check("2.4 timelineMapping alias behavior (getter capitalized, setter tolerant)", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  sm.timelineMapping = "Hold"
  assert_eq(sm.timelineMapping, "Hold", "timelineMapping reports Hold")
  assert_eq(sm.smartObjectTimelineMapping, "hold", "smartObjectTimelineMapping reports hold")

  sm.timelineMapping = "Fixed"
  assert_eq(sm.timelineMapping, "Fixed", "timelineMapping reports Fixed")

  sm.timelineMapping = "Loop"
  assert_eq(sm.timelineMapping, "Loop", "timelineMapping reports Loop")

  -- timelineMapping = nil should safely be ignored (no crash)
  local okNil = pcall(function() sm.timelineMapping = nil end)
  assert_true(okNil, "timelineMapping = nil does not crash")
  assert_eq(sm.timelineMapping, "Loop", "Remains Loop after nil setter")

  -- timelineMapping with invalid string defaults to Loop
  sm.timelineMapping = "invalid_string"
  assert_eq(sm.timelineMapping, "Loop", "Invalid string defaults to Loop")
  spr:close()
end)

check("2.5 smartObjectLastModified boundary values (0, large int, nil/string errors)", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]
  local originalTs = sm.smartObjectLastModified

  -- Setting 0
  sm.smartObjectLastModified = 0
  assert_eq(sm.smartObjectLastModified, 0, "smartObjectLastModified set to 0")

  -- Setting large 64-bit integer
  sm.smartObjectLastModified = 1893456000 -- year 2030
  assert_eq(sm.smartObjectLastModified, 1893456000, "smartObjectLastModified set to year 2030")

  -- Setting nil must fail
  assert_error(function() sm.smartObjectLastModified = nil end, "nil timestamp must error")

  -- Setting string must fail
  assert_error(function() sm.smartObjectLastModified = "invalid_ts" end, "string timestamp must error")

  -- Restore
  sm.smartObjectLastModified = originalTs
  spr:close()
end)

-- ==============================================================================
-- SECTION 3: layer:refreshSmartObject Method Boundary Conditions
-- ==============================================================================
print("\n--- SECTION 3: layer:refreshSmartObject Method Boundary Conditions ---")

check("3.1 Calling refreshSmartObject on regular (non-smart) layer returns false and error message", function()
  local spr = Sprite(64, 64)
  local regLayer = spr.layers[1]
  assert_false(regLayer.isSmartObject, "regLayer is not smart")

  local ok, msg = regLayer:refreshSmartObject()
  assert_false(ok, "refreshSmartObject returns false on non-smart layer")
  assert_eq(msg, "Layer is not a smart object", "Error message matches")

  local okForce, msgForce = regLayer:refreshSmartObject(true)
  assert_false(okForce, "refreshSmartObject(true) returns false on non-smart layer")
  assert_eq(msgForce, "Layer is not a smart object", "Error message matches")
  spr:close()
end)

check("3.2 refreshSmartObject on up-to-date layer returns false without force", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  local ok, msg = sm:refreshSmartObject(false)
  assert_false(ok, "refreshSmartObject(false) returns false when unchanged")
  assert_eq(msg, "No changes detected on disk", "Expected no changes message")

  local okNil, msgNil = sm:refreshSmartObject(nil)
  assert_false(okNil, "refreshSmartObject(nil) returns false when unchanged")
  assert_eq(msgNil, "No changes detected on disk", "Expected no changes message")
  spr:close()
end)

check("3.3 refreshSmartObject(true) forces cel re-render", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  local ok, msg = sm:refreshSmartObject(true)
  assert_true(ok, "refreshSmartObject(true) returns true")
  assert_true(msg == nil, "No error message on success")
  assert_eq(sm:cel(1).image.width, 16, "Cel width intact")
  spr:close()
end)

check("3.4 refreshSmartObject when disk timestamp advances", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]
  local realTs = sm.smartObjectLastModified

  -- Artificially rewind layer timestamp so disk timestamp appears newer
  sm.smartObjectLastModified = realTs - 500
  assert_true(sm.linkStatus == "Linked" or sm.linkStatus == "Outdated", "linkStatus reports Linked or Outdated")

  local ok = sm:refreshSmartObject(false)
  assert_true(ok, "refreshSmartObject(false) succeeds when disk file is newer")
  assert_eq(sm.smartObjectLastModified, realTs, "Timestamp updated to real timestamp")
  spr:close()
end)

check("3.5 refreshSmartObject handles deleted disk file cleanly", function()
  -- Create a temporary duplicate file
  local tempDir = app.fs.tempPath or "."
  local tempFixture = app.fs.joinPath(tempDir, "temp_smart_delete_test.aseprite")
  pcall(function() os.remove(tempFixture) end)

  local src = Sprite{ fromFile = fixture16 }
  src:saveAs(tempFixture)
  src:close()

  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = tempFixture, ui = false }
  local sm = spr.layers[2]
  assert_eq(sm.linkStatus, "Linked", "Initial link status is Linked")

  -- Delete the temporary file
  os.remove(tempFixture)

  -- Now test refreshSmartObject
  local ok, msg = sm:refreshSmartObject(true)
  assert_false(ok, "Forced refresh returns false after file deletion")
  assert_eq(msg, "Linked file missing on disk", "Reports linked file missing")
  assert_eq(sm.linkStatus, "Missing", "linkStatus is now Missing")
  assert_true(sm:cel(1) ~= nil, "Existing cel preserved after file deletion")

  spr:close()
end)

check("3.6 Layer methods (renderCel, renderAllCels, syncTimeline, mapTimelineFrame) on reg vs smart layer", function()
  local spr = Sprite(64, 64)
  local regLayer = spr.layers[1]
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  -- Calling on regular layer: safely returns 0 or nil, does not crash
  local regRenderCel = pcall(function() regLayer:renderCel(1, 1) end)
  assert_true(regRenderCel, "regLayer:renderCel does not crash")

  local regRenderAll = pcall(function() regLayer:renderAllCels() end)
  assert_true(regRenderAll, "regLayer:renderAllCels does not crash")

  local regSync = pcall(function() regLayer:syncTimeline() end)
  assert_true(regSync, "regLayer:syncTimeline does not crash")

  local regMap = pcall(function() regLayer:mapTimelineFrame(1) end)
  assert_true(regMap, "regLayer:mapTimelineFrame does not crash")

  -- Calling on smart layer: works properly
  local smMap = sm:mapTimelineFrame(1)
  assert_eq(smMap, 1, "sm:mapTimelineFrame(1) returns 1")

  local smSync = sm:syncTimeline("Loop")
  assert_true(type(smSync) == "number" and smSync >= 1, "sm:syncTimeline returns cel count")

  local smRenderAll = pcall(function() sm:renderAllCels() end)
  assert_true(smRenderAll, "sm:renderAllCels succeeds")

  spr:close()
end)

-- ==============================================================================
-- SECTION 4: Read-Only Property Protection
-- ==============================================================================
print("\n--- SECTION 4: Read-Only Property Protection ---")

check("4.1 isSmartObject is strictly read-only on smart layer", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_error(function() sm.isSmartObject = true end, "Setting isSmartObject = true must error")
  assert_error(function() sm.isSmartObject = false end, "Setting isSmartObject = false must error")
  assert_error(function() sm.isSmartObject = nil end, "Setting isSmartObject = nil must error")
  assert_true(sm.isSmartObject, "sm.isSmartObject remains true")
  spr:close()
end)

check("4.2 linkStatus is strictly read-only on smart layer", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_error(function() sm.linkStatus = "Missing" end, "Setting linkStatus = 'Missing' must error")
  assert_error(function() sm.linkStatus = "Linked" end, "Setting linkStatus = 'Linked' must error")
  assert_error(function() sm.linkStatus = nil end, "Setting linkStatus = nil must error")
  assert_eq(sm.linkStatus, "Linked", "linkStatus remains Linked")
  spr:close()
end)

check("4.3 linkedFrameCount is strictly read-only on smart layer", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_error(function() sm.linkedFrameCount = 99 end, "Setting linkedFrameCount must error")
  assert_eq(sm.linkedFrameCount, 1, "linkedFrameCount remains 1")
  spr:close()
end)

check("4.4 transform table is strictly read-only on smart layer", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_error(function() sm.transform = {} end, "Setting transform table must error")
  local t = sm.transform
  assert_true(type(t) == "table", "transform returns a table")
  assert_eq(t.scaleX, 1.0, "Initial scaleX is 1.0")
  assert_eq(t.scaleY, 1.0, "Initial scaleY is 1.0")
  assert_eq(t.angle, 0.0, "Initial angle is 0.0")
  spr:close()
end)

check("4.5 Regular layer raises errors when setting smart-specific properties", function()
  local spr = Sprite(64, 64)
  local regLayer = spr.layers[1]

  assert_error(function() regLayer.isSmartObject = true end, "Setting isSmartObject on regular layer must error")
  assert_error(function() regLayer.smartObjectFilepath = fixture16 end, "Setting smartObjectFilepath on reg layer must error")
  assert_error(function() regLayer.sourcePath = fixture16 end, "Setting sourcePath on reg layer must error")
  assert_error(function() regLayer.smartObjectLastModified = 100 end, "Setting smartObjectLastModified on reg layer must error")
  assert_error(function() regLayer.smartObjectTimelineMapping = "hold" end, "Setting smartObjectTimelineMapping on reg layer must error")

  -- Check getters return safe defaults
  assert_false(regLayer.isSmartObject, "reg.isSmartObject is false")
  assert_true(regLayer.smartObjectFilepath == nil, "reg.smartObjectFilepath is nil")
  assert_eq(regLayer.smartObjectLastModified, 0, "reg.smartObjectLastModified is 0")
  assert_true(regLayer.smartObjectTimelineMapping == nil, "reg.smartObjectTimelineMapping is nil")
  assert_true(regLayer.timelineMapping == nil, "reg.timelineMapping is nil")
  assert_true(regLayer.linkStatus == nil, "reg.linkStatus is nil")
  assert_eq(regLayer.linkedFrameCount, 0, "reg.linkedFrameCount is 0")

  -- transform on regular layer returns defaults
  local rt = regLayer.transform
  assert_eq(rt.scaleX, 1.0, "reg transform scaleX is 1.0")
  assert_eq(rt.scaleY, 1.0, "reg transform scaleY is 1.0")
  assert_eq(rt.angle, 0.0, "reg transform angle is 0.0")

  spr:close()
end)

-- ==============================================================================
-- SECTION 5: app.command.RefreshSmartObject Batch Execution & Combinations
-- ==============================================================================
print("\n--- SECTION 5: app.command.RefreshSmartObject Execution & Flags ---")

check("5.1 RefreshSmartObject on doc with only regular layers behaves cleanly", function()
  local spr = Sprite(32, 32)
  -- RefreshSmartObject enabled check: should be false when no smart layers
  local isEnabled = app.command.RefreshSmartObject.enabled
  assert_false(isEnabled, "RefreshSmartObject should not be enabled on doc with no smart layers")

  -- Calling it anyway should not crash
  local ok = pcall(function() app.command.RefreshSmartObject() end)
  assert_true(ok, "RefreshSmartObject invocation on non-smart doc does not crash")
  spr:close()
end)

check("5.2 RefreshSmartObject parameter combinations: default, force, all, filename", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  assert_true(app.command.RefreshSmartObject.enabled, "RefreshSmartObject enabled when smart layer exists")

  -- Parameter combination 1: default (force=false, all=false)
  local ok1 = pcall(function() app.command.RefreshSmartObject() end)
  assert_true(ok1, "RefreshSmartObject() default succeeds")

  -- Parameter combination 2: force = false
  local ok2 = pcall(function() app.command.RefreshSmartObject{ force = false } end)
  assert_true(ok2, "RefreshSmartObject{ force = false } succeeds")

  -- Parameter combination 3: force = true
  local ok3 = pcall(function() app.command.RefreshSmartObject{ force = true } end)
  assert_true(ok3, "RefreshSmartObject{ force = true } succeeds")

  -- Parameter combination 4: all = true
  local ok4 = pcall(function() app.command.RefreshSmartObject{ all = true } end)
  assert_true(ok4, "RefreshSmartObject{ all = true } succeeds")

  -- Parameter combination 5: all = true, force = true
  local ok5 = pcall(function() app.command.RefreshSmartObject{ all = true, force = true } end)
  assert_true(ok5, "RefreshSmartObject{ all = true, force = true } succeeds")

  -- Parameter combination 6: matching filename
  local ok6 = pcall(function() app.command.RefreshSmartObject{ filename = sm.smartObjectFilepath, force = true } end)
  assert_true(ok6, "RefreshSmartObject{ filename = matching, force = true } succeeds")

  -- Parameter combination 7: non-matching filename
  local ok7 = pcall(function() app.command.RefreshSmartObject{ filename = "non_matching_file.aseprite", force = true } end)
  assert_true(ok7, "RefreshSmartObject with non-matching filename does not crash")

  -- Parameter combination 8: empty filename
  local ok8 = pcall(function() app.command.RefreshSmartObject{ filename = "", force = true } end)
  assert_true(ok8, "RefreshSmartObject with empty filename succeeds")

  spr:close()
end)

check("5.3 Multi-document RefreshSmartObject batch execution across all documents", function()
  -- Doc 1: Has smart layer 16x16
  local doc1 = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm1 = doc1.layers[2]

  -- Doc 2: Has only regular layer
  local doc2 = Sprite(32, 32)

  -- Doc 3: Has smart layer 32x32
  local doc3 = Sprite(128, 128)
  app.command.PlaceSmartObject{ filename = fixture32, ui = false }
  local sm3 = doc3.layers[2]

  -- Execute RefreshSmartObject with all = true and force = true
  app.command.RefreshSmartObject{ all = true, force = true }

  -- Verify both smart objects survived and re-rendered cleanly
  assert_eq(sm1:cel(1).image.width, 16, "Doc 1 cel intact at 16x16")
  assert_eq(sm3:cel(1).image.width, 32, "Doc 3 cel intact at 32x32")

  doc3:close()
  doc2:close()
  doc1:close()
end)

check("5.4 RefreshSmartObject with regular active layer refreshes all smart layers in doc", function()
  local spr = Sprite(64, 64)
  local regLayer = spr.layers[1]
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  -- Set active layer to regular layer
  app.activeLayer = regLayer
  assert_false(app.activeLayer.isSmartObject, "Active layer is regular layer")

  -- Execute RefreshSmartObject with force = true
  app.command.RefreshSmartObject{ force = true }

  -- The smart layer should still have been refreshed cleanly
  assert_eq(sm:cel(1).image.width, 16, "Smart layer cel intact")
  spr:close()
end)

-- ==============================================================================
-- SECTION 6: 2D Transform Boundary & Extreme Coordinate Tests
-- ==============================================================================
print("\n--- SECTION 6: 2D Transform Boundary & Extreme Coordinate Tests ---")

check("6.1 Negative scale transformation flips coordinates non-destructively", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  sm:setTransform(0, 0, -1.0, 1.0, 0.0, false, false)
  local t = sm.transform
  assert_eq(t.scaleX, -1.0, "ScaleX set to -1.0")
  assert_true(sm:cel(1) ~= nil, "Cel rendered with negative scale")

  sm:resetTransform()
  assert_eq(sm.transform.scaleX, 1.0, "Scale restored to 1.0")
  spr:close()
end)

check("6.2 Extreme translation offsets (-10000 to +10000)", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]
  local startX = sm.transform.x
  local startY = sm.transform.y

  sm:translate(-10000, 10000)
  assert_eq(sm.transform.x, startX - 10000, "Translated to extreme -10000")
  assert_eq(sm.transform.y, startY + 10000, "Translated to extreme +10000")

  sm:translate(10000, -10000)
  assert_eq(sm.transform.x, startX, "Returned to startX")
  assert_eq(sm.transform.y, startY, "Returned to startY")
  spr:close()
end)

check("6.3 Fractional scale and 360-degree rotation cycle", function()
  local spr = Sprite(64, 64)
  app.command.PlaceSmartObject{ filename = fixture16, ui = false }
  local sm = spr.layers[2]

  -- 8 steps of 45 degrees = 360 degrees
  for angle = 0, 360, 45 do
    sm:setTransform(5, 5, 1.5, 1.5, angle, false, false)
    assert_true(sm:cel(1) ~= nil, "Cel valid at angle " .. angle)
  end

  sm:resetTransform()
  assert_eq(sm:cel(1).image.width, 16, "Clean 16x16 after rotation cycle")
  spr:close()
end)

-- ==============================================================================
-- SUMMARY
-- ==============================================================================
print("\n======================================================")
print(string.format("  STRESS TEST SUMMARY: %d Passed, %d Failed (Total %d)", passCount, failCount, testCount))
print("======================================================")

if failCount > 0 then
  error(string.format("M4 Stress Test FAILED with %d failure(s)!", failCount), 2)
else
  print("ALL ADVERSARIAL STRESS TESTS PASSED SUCCESSFULLY!")
end
