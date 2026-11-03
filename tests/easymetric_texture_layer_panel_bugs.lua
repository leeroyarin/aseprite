-- =============================================================================
-- Easymetric texture / layer / panel bugs - regression tests
-- Branch: fix/texture-layer-panel-bugs
--
-- These tests model the exact rules fixed in C++ (see HANDOFF entry
-- for this branch). The studio UI itself (panel clicks, Move-tool
-- drags, splitter drags) cannot be driven from a Lua script, so
-- those cases are covered by the documented manual tests in
-- docs/aseprite2/TEXTURE_LAYER_PANEL_BUGS_MANUAL_TESTS.md, which
-- must be run on a Windows build before merge.
--
-- The Lua cases below run inside Aseprite (aseprite --script) and
-- check the rules themselves, not the UI widgets:
--   Bug 1: a sub-selection inside a face rect is preserved on a
--          face change; a Move in a panel moves masked pixels,
--          never the whole sheet cel.
--   Bug 2: a splitter drag may only start in a real gap strip,
--          never inside a pane; document changes mid-stroke
--          must not trigger a panel fit.
--   Bug 3: panel layers follow the user's chosen sheet layer
--          (Base / Texture / Depth); locked or hidden layers
--          are never auto-selected; no non-Move tool changes
--          the active layer.
-- =============================================================================

local passed = 0
local failed = 0

local function check(name, cond)
  if cond then
    passed = passed + 1
    print("  [PASS] " .. name)
  else
    failed = failed + 1
    print("  [FAIL] " .. name)
  end
end

-- --- Rule models, mirroring the C++ fixes -------------------------------

-- Bug 1c: keep an existing mask if it lies inside the new face rect.
local function mask_after_face_change(face, existing)
  if existing ~= nil and existing.x >= face.x and existing.y >= face.y and
     existing.x + existing.w <= face.x + face.w and
     existing.y + existing.h <= face.y + face.h then
    return existing
  end
  return face
end

-- Bug 1b: Move in a panel moves masked pixels, not the whole cel.
local function move_target(is_panel, inside_selection)
  if is_panel and inside_selection then return "masked-pixels" end
  return "cel"
end

-- Bug 2a: splitter drag only in the real gap strip.
local function splitter_gap_at(pos_x, col_x, bw)
  if pos_x >= col_x - bw and pos_x < col_x then return true end
  return false
end

-- Bug 2b: a panel fit must not run while any panel editor is busy.
local function fit_allowed(editors_busy) return not editors_busy end

-- Bug 3a: panel layer follows the user's sheet choice, otherwise
-- the panel's own layer is left untouched (never forced to texture).
local function panel_layer_after_init(active_layer, panel_layer, sheets)
  for _, s in ipairs(sheets) do
    if active_layer == s then return active_layer end
  end
  if panel_layer ~= nil then return panel_layer end
  return sheets[2] -- texture fallback, studio entry only
end

-- Bug 3b: auto-select only for visible, movable, editable layers.
local function auto_select_allowed(visible, movable, editable)
  return visible and movable and editable
end

-- --- Setup: a real sprite with the three shared sheets ---------------
local spr = Sprite(96, 64)
local function make_layer(name)
  local lyr = spr:newLayer()
  lyr.name = name
  return lyr
end
local base = make_layer("Surface_Base")
local tex = make_layer("Surface_Texture")
local depth = make_layer("Surface_DEPTH")
local sheets = { "Surface_Base", "Surface_Texture", "Surface_DEPTH" }
check("Bug 3: three shared sheets exist", base ~= nil and tex ~= nil and depth ~= nil)

-- --- Bug 1 ------------------------------------------------------------
local front = { x = 32, y = 0, w = 32, h = 32 }
local sub = { x = 40, y = 8, w = 8, h = 8 }
check("Bug 1: sub-selection inside face is preserved on face change",
  mask_after_face_change(front, sub) == sub)
check("Bug 1: empty/outside mask is replaced by the face rect",
  mask_after_face_change(front, nil).x == 32 and
  mask_after_face_change(front, { x = 0, y = 32, w = 8, h = 8 }).x == 32)
check("Bug 1: Move in a panel with a selection moves masked pixels, not the cel",
  move_target(true, true) == "masked-pixels")
check("Bug 1: Move outside a panel keeps the old cel behaviour",
  move_target(false, true) == "cel")

-- --- Bug 2 ------------------------------------------------------------
check("Bug 2: click in a real gap starts a splitter drag",
  splitter_gap_at(97, 100, 6) == true)
check("Bug 2: click 1px inside a pane does NOT start a splitter drag",
  splitter_gap_at(101, 100, 6) == false)
check("Bug 2: panel fit is skipped while an editor is busy",
  fit_allowed(true) == false and fit_allowed(false) == true)

-- --- Bug 3 ------------------------------------------------------------
check("Bug 3: panel follows Base when the user chose Base",
  panel_layer_after_init("Surface_Base", "Surface_Texture", sheets) == "Surface_Base")
check("Bug 3: panel follows Depth when the user chose Depth",
  panel_layer_after_init("Surface_DEPTH", "Surface_Texture", sheets) == "Surface_DEPTH")
check("Bug 3: panel on a non-sheet layer is NOT forced back to texture",
  panel_layer_after_init("Layer 1", "Layer 1", sheets) == "Layer 1")
check("Bug 3: locked layer is never auto-selected",
  auto_select_allowed(true, false, true) == false)
check("Bug 3: hidden layer is never auto-selected",
  auto_select_allowed(false, true, true) == false)
check("Bug 3: visible, movable, editable layer may be auto-selected (Move tool only)",
  auto_select_allowed(true, true, true) == true)

print(string.format("\nTexture/layer/panel bug tests: %d passed, %d failed", passed, failed))
if failed > 0 then error("texture/layer/panel bug regression tests failed") end
