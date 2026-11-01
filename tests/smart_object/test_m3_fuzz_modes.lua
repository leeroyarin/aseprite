-- tests/smart_object/test_m3_fuzz_modes.lua
-- ==============================================================================
-- Empirical Challenger: Random Fuzz Harness for Timeline Modes & Cel Lifecycle
-- Milestone 3 Iteration 2
-- ==============================================================================

print("================================================================================")
print("  EMPIRICAL CHALLENGER: TIMELINE FUZZ HARNESS (500 RANDOM OPERATIONS)          ")
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

-- PRNG for reproducible deterministic fuzzing
local rng_state = 987654321
local function rand(max_val)
  rng_state = (rng_state * 1103515245 + 12345) % 2147483648
  return (rng_state % max_val) + 1
end

local spr = Sprite(32, 32)
for i = 1, 9 do
  spr:newEmptyFrame()
end
assert_eq(#spr.frames, 10, "initial host has 10 frames")

app.command.PlaceSmartObject{
  filename = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
  ui = false
}
local lay = spr.layers[2]
assert_eq(#lay.cels, 10, "smart layer has 10 cels")

local modes = { "Loop", "Hold", "Fixed" }

print("\nExecuting 500 interleaved pseudo-random mutations...")
for step = 1, 500 do
  local action = rand(100)

  if action <= 35 then
    -- Action 1: Switch mode
    local m = modes[rand(3)]
    lay.timelineMapping = m
    lay:syncTimeline()
  elseif action <= 55 then
    -- Action 2: Add frame (head, middle, or tail)
    local n = #spr.frames
    if n < 40 then
      local pos = rand(n + 1)
      if pos > n then
        spr:newEmptyFrame()
      else
        spr:newEmptyFrame(pos)
      end
    end
  elseif action <= 75 then
    -- Action 3: Delete frame (if > 1)
    local n = #spr.frames
    if n > 1 then
      local pos = rand(n)
      spr:deleteFrame(pos)
    end
  elseif action <= 90 then
    -- Action 4: Layer transformation / translation
    local angle = (rand(4) - 1) * 90
    lay.angle = angle
    lay:syncTimeline()
  else
    -- Action 5: Explicit re-sync
    lay:syncTimeline()
  end

  -- Invariant Verification at every single mutation step
  local nFrames = #spr.frames
  local nCels = #lay.cels
  assert_eq(nCels, nFrames, string.format("Step %d: cel count (%d) must match host frame count (%d)", step, nCels, nFrames))

  -- Verify all cels exist and have valid images
  local uniqueIds = {}
  local uniqueCount = 0
  for f = 1, nFrames do
    local c = lay:cel(f)
    assert_true(c ~= nil, string.format("Step %d: cel %d must exist", step, f))
    assert_true(c.image ~= nil, string.format("Step %d: cel %d image must exist", step, f))
    assert_true(c.image.width > 0 and c.image.height > 0, string.format("Step %d: cel %d dimensions valid", step, f))

    if not uniqueIds[c.image.id] then
      uniqueIds[c.image.id] = true
      uniqueCount = uniqueCount + 1
    end
  end

  -- Invariant: Mode buffer count bounds
  local currentMode = lay.timelineMapping
  if currentMode == "Fixed" then
    assert_eq(uniqueCount, 1, string.format("Step %d: Fixed mode must have exactly 1 unique buffer", step))
  elseif currentMode == "Loop" or currentMode == "Hold" then
    local maxBuffers = math.min(3, nFrames)
    assert_true(uniqueCount <= maxBuffers, string.format("Step %d: %s mode buffer count (%d) <= %d", step, currentMode, uniqueCount, maxBuffers))
  end
end

spr:close()

print("\n================================================================================")
print(string.format("  500/500 FUZZ MUTATION STEPS PASSED (%d assertions)!          ", passCount))
print("================================================================================")
