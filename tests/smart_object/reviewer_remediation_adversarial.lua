-- tests/smart_object/reviewer_remediation_adversarial.lua
-- Adversarial verification of Milestone 5 Remediation changes

print("================================================================================")
print("  REVIEWER ADVERSARIAL: MILESTONE 5 REMEDIATION VERIFICATION                    ")
print("================================================================================")

local testCount = 0
local passCount = 0

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

local function assert_eq(actual, expected, msg)
  testCount = testCount + 1
  if actual ~= expected then
    error(string.format("ASSERTION FAILED: %s (expected %s, got %s)",
      msg or "value mismatch", tostring(expected), tostring(actual)), 2)
  end
  passCount = passCount + 1
end

local tempDir = app.fs.tempPath or "."
local pHost = app.fs.joinPath(tempDir, "reviewer_m5_host.aseprite")
local pSrc = app.fs.joinPath(tempDir, "reviewer_m5_src.aseprite")
local pOther = app.fs.joinPath(tempDir, "reviewer_m5_other.aseprite")

-- Helper to cleanup
local function cleanup()
  pcall(function() app.fs.removeFile(pHost) end)
  pcall(function() app.fs.removeFile(pSrc) end)
  pcall(function() app.fs.removeFile(pOther) end)
end
cleanup()

-- Create source file
local srcSpr = Sprite(16, 16, ColorMode.RGB)
srcSpr:saveAs(pSrc)
srcSpr:close()

-- Create other file
local otherSpr = Sprite(16, 16, ColorMode.RGB)
otherSpr:saveAs(pOther)
otherSpr:close()

-- Create host file
local hostSpr = Sprite(32, 32, ColorMode.RGB)
hostSpr:saveAs(pHost)

-- Place smart object from pSrc
app.command.PlaceSmartObject{ filename = pSrc, x = 0, y = 0, ui = false }
local smartLay = hostSpr.layers[2]
assert_true(smartLay.isSmartObject, "Smart layer created")
assert_eq(smartLay.smartObjectFilepath, pSrc, "Filepath matches pSrc")

-- Test 1: Self-reference lockout via Lua API with exact path
print("\n[TEST 1] Testing exact host path assignment lockout...")
local okExact, errExact = pcall(function()
  smartLay.smartObjectFilepath = pHost
end)
assert_false(okExact, "Assigning host filepath must fail")
assert_true(string.find(tostring(errExact), "Cannot link smart object to its own host document") ~= nil,
  "Error message must specify cannot link to its own host document. Got: " .. tostring(errExact))
print("  -> Passed: exact host path rejected with expected error.")

-- Test 2: Self-reference lockout with forward slash variation
print("\n[TEST 2] Testing forward-slash host path assignment lockout...")
local pHostForward = string.gsub(pHost, "\\", "/")
local okForward, errForward = pcall(function()
  smartLay.smartObjectFilepath = pHostForward
end)
assert_false(okForward, "Assigning forward-slash host filepath must fail")
assert_true(string.find(tostring(errForward), "Cannot link smart object to its own host document") ~= nil,
  "Error message must specify cannot link to its own host document. Got: " .. tostring(errForward))
print("  -> Passed: forward slash variation normalized and rejected.")

-- Test 3: Relative path variation self-reference lockout
print("\n[TEST 3] Testing relative path self-reference lockout...")
local okRel, errRel = pcall(function()
  -- Use relative path syntax to host file
  smartLay.smartObjectFilepath = pHost .. "/../" .. app.fs.fileName(pHost)
end)
assert_false(okRel, "Assigning relative host filepath must fail")
print("  -> Passed: relative path normalized and rejected.")

-- Test 4: Assigning valid other file succeeds
print("\n[TEST 4] Testing valid other file assignment...")
local okOther, errOther = pcall(function()
  smartLay.smartObjectFilepath = pOther
end)
assert_true(okOther, "Assigning valid other filepath must succeed: " .. tostring(errOther))
assert_eq(smartLay.smartObjectFilepath, pOther, "Filepath matches pOther")
print("  -> Passed: valid other file assigned cleanly.")

-- Test 5: Re-testing self-reference from pOther back to pHost
print("\n[TEST 5] Testing self-reference again after valid reassignment...")
local okReHost, errReHost = pcall(function()
  smartLay.smartObjectFilepath = pHost
end)
assert_false(okReHost, "Assigning host path again must fail")
assert_eq(smartLay.smartObjectFilepath, pOther, "Filepath retained pOther on failure")
print("  -> Passed: host path rejected and state preserved.")

-- Test 6: Verify opening an existing document with smart layer (Eager SmartObjectManager lifecycle)
print("\n[TEST 6] Testing opening existing smart object document from clean state...")
hostSpr:saveAs(pHost)
hostSpr:close()

-- Open pHost directly from file
local reopenedHost = Sprite{ fromFile = pHost }
assert_true(reopenedHost ~= nil, "Reopened host sprite is valid")
assert_eq(#reopenedHost.layers, 2, "Reopened host has 2 layers")
local reopenedSmartLay = reopenedHost.layers[2]
assert_true(reopenedSmartLay.isSmartObject, "Reopened layer is smart object")
assert_eq(reopenedSmartLay.smartObjectFilepath, pOther, "Reopened filepath matches pOther")

-- Modify pOther on disk
local start = os.time()
while os.time() <= start do end -- wait 1 sec

local modSpr = Sprite{ fromFile = pOther }
local modImg = modSpr.cels[1].image
modImg:drawPixel(0, 0, Color{ r = 255, g = 0, b = 255, a = 255 })
modSpr:saveAs(pOther)
modSpr:close()

-- Switch active sprite or call refresh
assert_true(reopenedSmartLay.isDiskFileNewer, "Disk file is detected as newer")
local refOk = reopenedSmartLay:refreshSmartObject()
assert_true(refOk, "Refresh succeeded on existing opened document")
assert_false(reopenedSmartLay.isDiskFileNewer, "Disk file is no longer newer")

reopenedHost:close()
cleanup()

print("\n================================================================================")
print(string.format("  REVIEWER ADVERSARIAL VERIFICATION COMPLETE: %d / %d PASSED!  ", passCount, testCount))
print("================================================================================")
