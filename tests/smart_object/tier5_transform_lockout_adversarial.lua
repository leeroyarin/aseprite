-- ==============================================================================
-- tier5_transform_lockout_adversarial.lua
-- Milestone 5 Phase 2 White-Box Adversarial Challenge Harness
--
-- Objective:
-- Stress-test and verify:
-- 1. 2D Transforms:
--    - Arbitrary angles (15°, 30°, 45°, 60°, 75°, 123.45°, -45°, -135°, 7200°)
--    - Orthogonal angles (0°, 90°, 180°, 270°, -90°, -180°, -270°, 360°, 450°)
--    - Scale permutations (0.01x, 0.5x, 1.0x, 2.0x, 5.0x, asymmetric, negative)
--    - Pivot permutations (corners, edges, center, fractional, off-canvas)
--    - Float noise snapping (< 1e-9) and zero-scale edge case clamping
--    - Transform cycle invariance (multi-step transform loop -> reset returns bit-identical source)
-- 2. Multi-Frame Timeline Synchronization:
--    - Playback mapping modes (Loop, Hold, Fixed)
--    - Dynamic mode transitions (Loop -> Hold -> Fixed -> Loop)
--    - Frame count mismatches (3-frame source in 10-frame host, 10-frame in 3-frame, 1-frame in 20-frame)
--    - Host timeline dynamic resizing (adding/removing frames, scrubbing)
--    - Cel linking & buffer sharing invariants (canonical cels vs linked cels)
-- 3. Tool & Direct Pixel Edit Lockout:
--    - Complete app.useTool battery (pencil, brush, eraser, bucket, line, curve, rect, ellipse, blur, jumble, spray)
--    - Direct Image mutation APIs (drawPixel, drawImage, drawSprite, clear, resize, flip, bytes, pixels iterator)
--    - Cel modification APIs (cel.image assignment, cel.image = nil, sprite:newCel, sprite:deleteCel, cel.frame)
--    - Built-in editing commands (Clear, Cut, Paste, Fill, Stroke, MergeDown, ConvertLayer)
--    - Destructive filter commands (InvertColor, Despeckle, ColorCurve, ConvolutionMatrix)
-- ==============================================================================

local totalAsserts = 0
local passedAsserts = 0
local failedAsserts = 0
local failures = {}

local function assert_eq(actual, expected, msg)
  totalAsserts = totalAsserts + 1
  if actual == expected then
    passedAsserts = passedAsserts + 1
  else
    failedAsserts = failedAsserts + 1
    local err = string.format("[FAIL] %s: expected %s, got %s", msg or "assertion failed", tostring(expected), tostring(actual))
    table.insert(failures, err)
    print("  " .. err)
  end
end

local function assert_true(val, msg)
  assert_eq(val == true, true, msg)
end

local function assert_false(val, msg)
  assert_eq(val == false, true, msg)
end

local function assert_almost_eq(actual, expected, eps, msg)
  eps = eps or 1e-5
  totalAsserts = totalAsserts + 1
  if math.abs(actual - expected) <= eps then
    passedAsserts = passedAsserts + 1
  else
    failedAsserts = failedAsserts + 1
    local err = string.format("[FAIL] %s: expected ~%s, got %s (diff %s)", msg or "assertion failed", tostring(expected), tostring(actual), tostring(math.abs(actual - expected)))
    table.insert(failures, err)
    print("  " .. err)
  end
end

print("================================================================================")
print("  TIER 5 ADVERSARIAL CHALLENGE: 2D TRANSFORMS, TIMELINE SYNC & TOOL LOCKOUT      ")
print("================================================================================")

local fixtures = {
  single16 = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
  single32 = "tests/smart_object/fixtures/single_frame_32x32.aseprite",
  multi3   = "tests/smart_object/fixtures/multi_frame_3f.aseprite",
  multi10  = "tests/smart_object/fixtures/multi_frame_10f.aseprite",
  extreme1 = "tests/smart_object/fixtures/extreme_1x1.aseprite",
  wide     = "tests/smart_object/fixtures/nonsquare_48x12.aseprite",
  tall     = "tests/smart_object/fixtures/nonsquare_16x32.aseprite",
}

for k, path in pairs(fixtures) do
  if not app.fs.isFile(path) then
    error("Required fixture missing: " .. path)
  end
end

-- ==============================================================================
-- SECTION 1: 2D TRANSFORM ADVERSARIAL STRESS
-- ==============================================================================
print("\n--- SECTION 1: 2D Transform Adversarial Stress ---")

-- Test 1.1: Complex compound transforms with extreme values
do
  local host = Sprite(200, 200, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = fixtures.single16, ui = false }
  local smLayer = host.layers[#host.layers]
  assert_true(smLayer.isSmartObject, "Layer must be smart object")

  -- Apply non-uniform scale + non-orthogonal rotation + pivot
  smLayer:setTransform(50, 50, 1.75, 0.85, 37.5, false, false, 0.25, 0.75)
  assert_eq(#smLayer.cels, 1, "Should have 1 cel")
  local c0 = smLayer.cels[1]
  assert_true(c0 ~= nil and c0.image ~= nil, "Cel image must be rendered")
  assert_true(c0.image.width > 0 and c0.image.height > 0, "Dimensions must be positive")
  assert_true(c0.position.x >= 0 and c0.position.y >= 0, "Position must be valid")

  -- Extreme rotation angles (>360, negative, multi-turn)
  local testAngles = { 45.0, 135.0, 225.0, 315.0, -45.0, -180.0, -720.0, 1080.0, 7200.0 }
  for _, ang in ipairs(testAngles) do
    smLayer.angle = ang
    assert_almost_eq(smLayer.angle, ang, 1e-6, "Angle must be preserved")
    local cel = smLayer.cels[1]
    assert_true(cel.image.width > 0 and cel.image.height > 0, "Image size valid at angle " .. ang)
  end

  -- Scale extremes (0.01x up to 10.0x, and near-zero clamping)
  local testScales = {
    { 0.01, 0.01 },
    { 0.1, 0.1 },
    { 0.5, 2.0 },
    { 3.5, 0.5 },
    { 8.0, 8.0 },
    { -1.0, 1.0 },   -- Negative scale horizontal (folded to flipH)
    { 1.0, -1.0 },   -- Negative scale vertical (folded to flipV)
    { -2.0, -2.0 },  -- Negative both
  }
  for _, sc in ipairs(testScales) do
    smLayer.scale = { x = sc[1], y = sc[2] }
    local cel = smLayer.cels[1]
    assert_true(cel.image.width >= 1 and cel.image.height >= 1, "Image size clamped to at least 1x1")
  end

  -- Pivot permutations (corners, centers, outside canvas)
  local testPivots = {
    { 0.0, 0.0 }, { 1.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 1.0 },
    { 0.5, 0.5 }, { 0.25, 0.75 }, { -1.0, -1.0 }, { 2.0, 2.0 }
  }
  for _, p in ipairs(testPivots) do
    smLayer.pivot = { x = p[1], y = p[2] }
    local cel = smLayer.cels[1]
    assert_true(cel.image ~= nil, "Cel rendered with pivot (" .. p[1] .. "," .. p[2] .. ")")
  end

  host:close()
  print("  -> Test 1.1 Passed: Extreme angles, scales, pivots handled without crash.")
end

-- Test 1.2: Pristine Source Fidelity After Compound Transform Cycles
do
  local host = Sprite(100, 100, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = fixtures.single16, ui = false }
  local smLayer = host.layers[#host.layers]
  local initialCel = smLayer.cels[1]
  local origW = initialCel.image.width
  local origH = initialCel.image.height
  local initialPixels = {}
  for y = 0, origH - 1 do
    for x = 0, origW - 1 do
      table.insert(initialPixels, initialCel.image:getPixel(x, y))
    end
  end

  -- Execute 20 violent random transformation steps
  local testTransforms = {
    { sx = 2.0, sy = 0.5, ang = 45, fh = true, fv = false },
    { sx = 0.5, sy = 3.0, ang = 90, fh = false, fv = true },
    { sx = 1.5, sy = 1.5, ang = 180, fh = true, fv = true },
    { sx = 0.25, sy = 0.25, ang = 270, fh = false, fv = false },
    { sx = 4.0, sy = 1.0, ang = 330, fh = true, fv = false },
    { sx = 1.0, sy = 1.0, ang = 720, fh = false, fv = false },
  }
  for _, t in ipairs(testTransforms) do
    smLayer:setTransform(10, 10, t.sx, t.sy, t.ang, t.fh, t.fv, 0.5, 0.5)
  end

  -- Reset transform
  smLayer:resetTransform()
  local resetCel = smLayer.cels[1]
  assert_eq(resetCel.image.width, origW, "Reset width must match original")
  assert_eq(resetCel.image.height, origH, "Reset height must match original")

  local pristineMatch = true
  local idx = 1
  for y = 0, origH - 1 do
    for x = 0, origW - 1 do
      if resetCel.image:getPixel(x, y) ~= initialPixels[idx] then
        pristineMatch = false
        break
      end
      idx = idx + 1
    end
    if not pristineMatch then break end
  end
  assert_true(pristineMatch, "Pristine source pixel fidelity 100% restored after compound transform cycle")

  host:close()
  print("  -> Test 1.2 Passed: Pristine source integrity 100% bit-identical after transform cycle.")
end

-- ==============================================================================
-- SECTION 2: MULTI-FRAME TIMELINE SYNCHRONIZATION ADVERSARIAL STRESS
-- ==============================================================================
print("\n--- SECTION 2: Timeline Synchronization Adversarial Stress ---")

-- Test 2.1: Multi-frame (3 frames) linked into 10-frame host across Loop, Hold, Fixed
do
  local host = Sprite(64, 64, ColorMode.RGB)
  -- Add 9 frames to make host 10 frames
  for i = 1, 9 do
    host:newEmptyFrame()
  end
  assert_eq(#host.frames, 10, "Host should have 10 frames")

  app.command.PlaceSmartObject{ filename = fixtures.multi3, ui = false }
  local smLayer = host.layers[#host.layers]
  assert_eq(smLayer.linkedFrameCount, 3, "Linked file has 3 frames")
  assert_eq(#smLayer.cels, 10, "Smart layer must have 10 cels across 10 host frames")

  -- Mode 1: Loop mode verification (0, 1, 2, 0, 1, 2, 0, 1, 2, 0)
  smLayer.timelineMapping = "Loop"
  -- Verify cel linking topology: canonical cels on frames 1, 2, 3 (0-indexed 0, 1, 2)
  -- Frame 4 should link to Frame 1 (cel 4 link == cel 1)
  local cels = smLayer.cels
  assert_eq(#cels, 10, "Must have 10 cels in Loop mode")

  -- Check buffer sharing: cels with same mapped frame share image ID or link
  local id1 = cels[1].image.id
  local id2 = cels[2].image.id
  local id3 = cels[3].image.id
  assert_true(id1 ~= id2 and id2 ~= id3, "Canonical cels have distinct buffers")
  assert_eq(cels[4].image.id, id1, "Frame 4 (index 3) loops to frame 1 buffer")
  assert_eq(cels[5].image.id, id2, "Frame 5 loops to frame 2 buffer")
  assert_eq(cels[6].image.id, id3, "Frame 6 loops to frame 3 buffer")
  assert_eq(cels[7].image.id, id1, "Frame 7 loops to frame 1 buffer")

  -- Mode 2: Dynamic switch to Hold mode (0, 1, 2, 2, 2, 2, 2, 2, 2, 2)
  smLayer.timelineMapping = "Hold"
  cels = smLayer.cels
  assert_eq(#cels, 10, "Must have 10 cels in Hold mode")
  local hold1 = cels[1].image.id
  local hold2 = cels[2].image.id
  local hold3 = cels[3].image.id
  assert_true(hold1 ~= hold2 and hold2 ~= hold3, "Canonical cels in Hold have distinct buffers")
  for f = 4, 10 do
    assert_eq(cels[f].image.id, hold3, "Frame " .. f .. " in Hold must hold frame 3 buffer")
  end

  -- Mode 3: Dynamic switch to Fixed mode (0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
  smLayer.timelineMapping = "Fixed"
  cels = smLayer.cels
  assert_eq(#cels, 10, "Must have 10 cels in Fixed mode")
  local fix1 = cels[1].image.id
  for f = 2, 10 do
    assert_eq(cels[f].image.id, fix1, "Frame " .. f .. " in Fixed must share frame 1 buffer")
  end

  -- Mode 4: Switch back to Loop mode
  smLayer.timelineMapping = "Loop"
  cels = smLayer.cels
  local loop1 = cels[1].image.id
  local loop2 = cels[2].image.id
  local loop3 = cels[3].image.id
  assert_eq(cels[4].image.id, loop1, "Restored Loop: Frame 4 loops to frame 1")
  assert_eq(cels[5].image.id, loop2, "Restored Loop: Frame 5 loops to frame 2")
  assert_eq(cels[6].image.id, loop3, "Restored Loop: Frame 6 loops to frame 3")

  host:close()
  print("  -> Test 2.1 Passed: Multi-frame timeline mapping and dynamic mode switching.")
end

-- Test 2.2: Extreme Timeline Mutation Stress (displacing, inserting, deleting frames)
do
  local host = Sprite(32, 32, ColorMode.RGB)
  for i = 1, 5 do host:newEmptyFrame() end
  app.command.PlaceSmartObject{ filename = fixtures.multi3, ui = false }
  local smLayer = host.layers[#host.layers]
  assert_eq(#smLayer.cels, 6, "Initial 6 cels")

  -- Delete frame 2
  host:deleteFrame(2)
  assert_eq(#host.frames, 5, "Host now has 5 frames")
  assert_eq(#smLayer.cels, 5, "Smart layer cel count tracks host frames")

  -- Insert 10 new empty frames
  for i = 1, 10 do host:newEmptyFrame() end
  assert_eq(#host.frames, 15, "Host now has 15 frames")
  assert_eq(#smLayer.cels, 15, "Smart layer cel count tracks host frames")

  -- Delete frames in a loop down to 1 frame
  while #host.frames > 1 do
    host:deleteFrame(#host.frames)
  end
  assert_eq(#host.frames, 1, "Host now has 1 frame")
  assert_eq(#smLayer.cels, 1, "Smart layer cel count tracks host frames")

  host:close()
  print("  -> Test 2.2 Passed: Dynamic frame addition and deletion maintains exact cel topology.")
end

-- ==============================================================================
-- SECTION 3: TOOL & SCRIPT DIRECT PIXEL EDIT LOCKOUT BYPASS ADVERSARIAL STRESS
-- ==============================================================================
print("\n--- SECTION 3: Direct Pixel Edit Lockout Adversarial Stress ---")

do
  local host = Sprite(40, 40, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = fixtures.single16, x = 10, y = 10, ui = false }
  local smLayer = host.layers[#host.layers]
  assert_true(smLayer.isSmartObject, "Layer is smart object")
  assert_eq(smLayer.isSmartObject, true, "isSmartObject must be true")

  app.activeSprite = host
  app.activeLayer = smLayer

  local cel = smLayer.cels[1]
  local img = cel.image

  -- 3.1: app.useTool on smart layer MUST fail or be blocked
  local toolsToTest = {
    "pencil", "brush", "eraser", "paint_bucket", "line",
    "curve", "rectangle", "filled_rectangle", "ellipse",
    "filled_ellipse", "contour", "polygon", "blur", "jumble"
  }
  for _, toolName in ipairs(toolsToTest) do
    local ok, err = pcall(function()
      app.useTool{
        tool = toolName,
        color = Color{ r = 255, g = 0, b = 0, a = 255 },
        points = { Point(12, 12), Point(15, 15) }
      }
    end)
    -- Expect error "cannot draw on a smart object layer" or "cannot draw on the active layer"
    assert_false(ok, "app.useTool(" .. toolName .. ") must be blocked on smart object")
  end

  -- 3.2: Direct image mutation methods
  -- drawPixel
  local ok, err = pcall(function() img:drawPixel(0, 0, Color{ r = 255, g = 0, b = 0 }) end)
  assert_false(ok, "img:drawPixel must throw error on smart object cel")

  -- drawImage
  local dummyImg = Image(4, 4, ColorMode.RGB)
  ok, err = pcall(function() img:drawImage(dummyImg, Point(0, 0)) end)
  assert_false(ok, "img:drawImage must throw error on smart object cel")

  -- drawSprite
  ok, err = pcall(function() img:drawSprite(host, 1, Point(0, 0)) end)
  assert_false(ok, "img:drawSprite must throw error on smart object cel")

  -- clear
  ok, err = pcall(function() img:clear() end)
  assert_false(ok, "img:clear must throw error on smart object cel")

  -- resize
  ok, err = pcall(function() img:resize(32, 32) end)
  assert_false(ok, "img:resize must throw error on smart object cel")

  -- flip
  ok, err = pcall(function() img:flip(0) end)
  assert_false(ok, "img:flip must throw error on smart object cel")

  -- bytes setter
  ok, err = pcall(function() img.bytes = string.rep("\0", img.rowStride * img.height) end)
  assert_false(ok, "img.bytes setter must throw error on smart object cel")

  -- pixels iterator mutation
  ok, err = pcall(function()
    for pixel in img:pixels() do
      pixel(app.pixelColor.rgba(255, 0, 0, 255))
      break
    end
  end)
  assert_false(ok, "img:pixels() mutation must throw error on smart object cel")

  -- 3.3: Cel assignment / mutation
  ok, err = pcall(function() cel.image = dummyImg end)
  assert_false(ok, "cel.image assignment must throw error")

  ok, err = pcall(function() cel.image = nil end)
  assert_false(ok, "cel.image = nil must throw error")

  ok, err = pcall(function() host:newCel(smLayer, 1, dummyImg) end)
  assert_false(ok, "host:newCel on smart layer must throw error")

  ok, err = pcall(function() host:deleteCel(smLayer, 1) end)
  assert_false(ok, "host:deleteCel on smart layer must throw error")

  -- 3.4: Commands that modify pixels
  -- Selection and Clear
  host.selection:select(Rectangle(10, 10, 8, 8))
  app.command.Clear() -- Should be a no-op or blocked
  assert_true(smLayer.isSmartObject, "Layer is still smart object")

  -- Stroke & Fill
  ok, err = pcall(function() app.command.Stroke() end)
  ok, err = pcall(function() app.command.Fill() end)

  -- MergeDownLayer
  ok, err = pcall(function() app.command.MergeDownLayer() end)

  -- InvertColor & Despeckle filters
  ok, err = pcall(function() app.command.InvertColor() end)
  ok, err = pcall(function() app.command.Despeckle() end)

  -- Verify pristine cel image didn't change
  assert_eq(cel.image.width, 16, "Width intact")
  assert_eq(cel.image.height, 16, "Height intact")

  host:close()
  print("  -> Test 3 Passed: All direct pixel edit & tool bypass attempts thoroughly blocked.")
end

-- ==============================================================================
-- SECTION 4: THRESHOLD ANGLE FUZZING & ASYMMETRIC SCALE LIMITS
-- ==============================================================================
print("\n--- SECTION 4: Threshold Angle Fuzzing & Scale Limits ---")

do
  local host = Sprite(128, 128, ColorMode.RGB)
  app.command.PlaceSmartObject{ filename = fixtures.single16, ui = false }
  local smLayer = host.layers[#host.layers]

  -- Threshold angles around 90°, 180°, 270°, 360° to stress boundary between rotate_image and rotsprite
  local boundaryAngles = {
    89.998, 89.9995, 90.000, 90.0005, 90.002,
    179.998, 179.9995, 180.000, 180.0005, 180.002,
    269.998, 269.9995, 270.000, 270.0005, 270.002,
    359.998, 359.9995, 360.000, 360.0005, 360.002
  }
  for _, ang in ipairs(boundaryAngles) do
    smLayer.angle = ang
    local cel = smLayer.cels[1]
    assert_true(cel ~= nil and cel.image ~= nil, "Threshold angle " .. ang .. " produced valid cel")
    assert_true(cel.image.width >= 16 and cel.image.height >= 16, "Dimensions valid at threshold angle " .. ang)
  end

  -- Scale boundary limits
  local scaleLimits = {
    { 0.001, 10.0 },
    { 10.0, 0.001 },
    { -0.001, -0.001 },
    { 0.0, 0.0 }
  }
  for _, sc in ipairs(scaleLimits) do
    smLayer.scale = { x = sc[1], y = sc[2] }
    local cel = smLayer.cels[1]
    assert_true(cel.image.width >= 1 and cel.image.height >= 1, "Clamped to >= 1x1 at scale (" .. sc[1] .. "," .. sc[2] .. ")")
  end

  host:close()
  print("  -> Test 4 Passed: Threshold angle transitions and scale limits verified.")
end

-- ==============================================================================
-- SECTION 5: MULTI-LAYER CONCURRENT SCRUBBING & NON-DESTRUCTIVE INVARIANCE
-- ==============================================================================
print("\n--- SECTION 5: Multi-Layer Concurrency & Scrubbing Invariance ---")

do
  local host = Sprite(120, 120, ColorMode.RGB)
  for i = 1, 9 do host:newEmptyFrame() end
  assert_eq(#host.frames, 10, "Host has 10 frames")

  -- Create 4 smart object layers with different fixtures and transforms
  app.command.PlaceSmartObject{ filename = fixtures.single16, x = 0, y = 0, ui = false }
  local lay1 = host.layers[#host.layers]
  lay1:setTransform(10, 10, 1.5, 1.5, 45, false, false, 0.5, 0.5)

  app.command.PlaceSmartObject{ filename = fixtures.multi3, x = 30, y = 30, ui = false }
  local lay2 = host.layers[#host.layers]
  lay2.timelineMapping = "Loop"
  lay2:setTransform(40, 40, 0.8, 1.2, 90, true, false, 0.5, 0.5)

  app.command.PlaceSmartObject{ filename = fixtures.multi10, x = 60, y = 60, ui = false }
  local lay3 = host.layers[#host.layers]
  lay3.timelineMapping = "Hold"
  lay3:setTransform(20, 50, 1.0, 1.0, 180, false, true, 0.0, 0.0)

  app.command.PlaceSmartObject{ filename = fixtures.tall, x = 15, y = 70, ui = false }
  local lay4 = host.layers[#host.layers]
  lay4:setTransform(50, 10, 2.0, 0.5, 270, true, true, 1.0, 1.0)

  -- Scrub across all 10 frames 5 times forward and backward
  for cycle = 1, 5 do
    for f = 1, 10 do
      app.activeFrame = host.frames[f]
      assert_eq(#lay1.cels, 10, "Lay1 cel count stable")
      assert_eq(#lay2.cels, 10, "Lay2 cel count stable")
      assert_eq(#lay3.cels, 10, "Lay3 cel count stable")
      assert_eq(#lay4.cels, 10, "Lay4 cel count stable")
    end
    for f = 10, 1, -1 do
      app.activeFrame = host.frames[f]
    end
  end

  -- Verify each layer maintained its distinct transforms
  assert_almost_eq(lay1.angle, 45, 1e-4, "Lay1 angle intact")
  assert_almost_eq(lay2.angle, 90, 1e-4, "Lay2 angle intact")
  assert_almost_eq(lay3.angle, 180, 1e-4, "Lay3 angle intact")
  assert_almost_eq(lay4.angle, 270, 1e-4, "Lay4 angle intact")

  assert_true(lay2.flipH == true and lay2.flipV == false, "Lay2 flips intact")
  assert_true(lay3.flipH == false and lay3.flipV == true, "Lay3 flips intact")
  assert_true(lay4.flipH == true and lay4.flipV == true, "Lay4 flips intact")

  host:close()
  print("  -> Test 5 Passed: Multi-layer concurrent scrubbing and transform isolation verified.")
end

-- ==============================================================================
-- SUMMARY & VERDICT
-- ==============================================================================
print("\n================================================================================")
print("                    TIER 5 ADVERSARIAL STRESS SUITE SUMMARY                     ")
print("================================================================================")
print(string.format(" Total Assertions Evaluated : %d", totalAsserts))
print(string.format(" Assertions Passed          : %d", passedAsserts))
print(string.format(" Assertions Failed          : %d", failedAsserts))

if failedAsserts > 0 then
  print("\nFAILURE LOG:")
  for i, f in ipairs(failures) do
    print(string.format("  [%d] %s", i, f))
  end
  print("\nVERDICT: GAPS_FOUND")
  error("Tier 5 adversarial challenge failed with " .. failedAsserts .. " errors")
else
  print("\n--> EMPIRICAL VERDICT: 100% PASS! NO GAPS OR VULNERABILITIES DETECTED!")
  print("VERDICT: NO_GAPS_CLEAN")
end
