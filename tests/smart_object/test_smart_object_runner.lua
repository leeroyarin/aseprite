-- ==============================================================================
-- Master Test Runner for Smart Object Feature
-- Aseprite 2.0 Automation Suite (Tiers 1-4)
-- ==============================================================================

local function getScriptDir()
  local info = debug.getinfo(1, "S")
  local path = info.source:sub(2)
  local dir = path:match("(.*[/\\])") or "./"
  return dir
end

local scriptDir = getScriptDir()
print("======================================================")
print("  Aseprite 2.0 Smart Object E2E Test Suite Runner     ")
print("======================================================")
print("Working Directory: " .. scriptDir)

-- Add scriptDir to package.path so modules can be required
package.path = scriptDir .. "?.lua;" .. scriptDir .. "?/init.lua;" .. package.path

-- Ensure fixtures exist
local sampleFixture = scriptDir .. "fixtures/single_frame_16x16.aseprite"
local f = io.open(sampleFixture, "rb")
if f then
  f:close()
else
  print("\nGenerating fixtures prior to test execution...")
  dofile(scriptDir .. "fixtures/generate_fixtures.lua")
end

-- Load test harness
local harness = require("smart_object_harness")

local startTime = os.clock()

-- Execute Tier 1
dofile(scriptDir .. "tier1_feature_tests.lua")

-- Execute Tier 2
dofile(scriptDir .. "tier2_boundary_tests.lua")

-- Execute Tier 3
dofile(scriptDir .. "tier3_pairwise_tests.lua")

-- Execute Tier 4
dofile(scriptDir .. "tier4_realworld_tests.lua")

-- Execute Smart Object Import & Convert-To Tests
dofile(scriptDir .. "test_convert_smart_object.lua")

-- Execute Challenger Adversarial Import & Convert-To Stress Tests
dofile(scriptDir .. "test_adversarial_import_conv.lua")

-- Execute Challenger 2 Empirical Stress Tests
dofile(scriptDir .. "challenger2_import_conv_stress.lua")

local elapsed = os.clock() - startTime

-- ==============================================================================
-- Test Suite Execution Summary
-- ==============================================================================
local suite = harness.suite

print("\n======================================================")
print("             E2E TEST SUITE EXECUTION SUMMARY         ")
print("======================================================")
for tier = 1, 4 do
  local res = suite.tierSummary[tier]
  print(string.format(" Tier %d (%-30s): %3d Passed, %3d Failed", tier, res.name, res.pass, res.fail))
end
print("------------------------------------------------------")
print(string.format(" TOTAL:  %3d Passed, %3d Failed (Total %d test cases)", suite.passed, suite.failed, suite.passed + suite.failed))
print(string.format(" Time:   %.2f seconds", elapsed))
print("======================================================")

if suite.failed > 0 then
  print("\nFAILED TESTS LIST:")
  for _, f in ipairs(suite.failures) do
    print(string.format(" - %s: %s", f.name, f.err))
  end
  error(string.format("Smart Object E2E Test Suite FAILED: %d test(s) failed.", suite.failed))
else
  print("ALL SMART OBJECT E2E TESTS PASSED SUCCESSFULLY.")
end
