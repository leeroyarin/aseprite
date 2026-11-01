-- test_m2_lua_api.lua
-- Tests native C++ Lua bindings on Layer for Milestone 2

local spr = Sprite(48, 48)
local initialLayer = spr.layers[1]

-- Check defaults on regular layer
assert(initialLayer.isSmartObject == false, "regular layer is not smart")
assert(initialLayer.scale.x == 1.0 and initialLayer.scale.y == 1.0, "regular layer scale default 1.0")
assert(initialLayer.angle == 0.0, "regular layer angle default 0.0")
assert(initialLayer.flipH == false, "regular layer flipH default false")
assert(initialLayer.flipV == false, "regular layer flipV default false")
assert(initialLayer.pivot.x == 0.5 and initialLayer.pivot.y == 0.5, "regular layer pivot default 0.5")

-- Place smart object layer using PlaceSmartObject command
app.command.PlaceSmartObject{
  filename = "tests/smart_object/fixtures/single_frame_16x16.aseprite",
  ui = false
}

assert(#spr.layers == 2, "sprite now has 2 layers")
local smartLayer = spr.layers[2]
assert(smartLayer.isSmartObject == true, "layer is a smart object")

-- Verify initial cel
local cel = smartLayer:cel(1)
assert(cel ~= nil, "cel exists for frame 1")
assert(cel.image.width == 16, "cel width is 16")
assert(cel.image.height == 16, "cel height is 16")
local pxTL = cel.image:getPixel(0, 0)
assert(app.pixelColor.rgbaR(pxTL) == 255, "initial TL is Red")

-- Test scale setter / getter
smartLayer.scale = { x = 2.0, y = 3.0 }
assert(smartLayer.scale.x == 2.0, "scale.x is 2.0")
assert(smartLayer.scale.y == 3.0, "scale.y is 3.0")
assert(smartLayer.scale[1] == 2.0, "scale[1] is 2.0")
assert(smartLayer.scale[2] == 3.0, "scale[2] is 3.0")

-- Verify cel image resized
local celScaled = smartLayer:cel(1)
assert(celScaled.image.width == 32, "cel image width scaled to 32")
assert(celScaled.image.height == 48, "cel image height scaled to 48")

-- Test scale array setter
smartLayer.scale = { 2.0, 0.5 }
assert(smartLayer.scale.x == 2.0, "scale.x array set to 2.0")
assert(smartLayer.scale.y == 0.5, "scale.y array set to 0.5")
local celAspect = smartLayer:cel(1)
assert(celAspect.image.width == 32, "aspect cel width 32")
assert(celAspect.image.height == 8, "aspect cel height 8")

-- Test angle setter / getter
smartLayer.angle = 90.0
assert(math.abs(smartLayer.angle - 90.0) < 1e-5, "angle is 90.0")

-- Test flipH setter / getter
smartLayer.flipH = true
assert(smartLayer.flipH == true, "flipH is true")

-- Test flipV setter / getter
smartLayer.flipV = true
assert(smartLayer.flipV == true, "flipV is true")

-- Test pivot setter / getter
smartLayer.pivot = { x = 0.25, y = 0.75 }
assert(smartLayer.pivot.x == 0.25, "pivot.x is 0.25")
assert(smartLayer.pivot.y == 0.75, "pivot.y is 0.75")

-- Test transform table getter
local t = smartLayer.transform
assert(t ~= nil, "transform table is not nil")
assert(t.scaleX == 2.0, "transform.scaleX is 2.0")
assert(t.scaleY == 0.5, "transform.scaleY is 0.5")
assert(math.abs(t.angle - 90.0) < 1e-5, "transform.angle is 90.0")
assert(t.flipH == true, "transform.flipH is true")
assert(t.flipV == true, "transform.flipV is true")
assert(t.pivotX == 0.25, "transform.pivotX is 0.25")
assert(t.pivotY == 0.75, "transform.pivotY is 0.75")

-- Test compound setTransform
smartLayer:setTransform(2.0, 2.0, 0.0, false, false, 0.5, 0.5)
assert(smartLayer.scale.x == 2.0, "compound scale.x 2.0")
assert(smartLayer.scale.y == 2.0, "compound scale.y 2.0")
assert(smartLayer.angle == 0.0, "compound angle 0.0")
assert(smartLayer.flipH == false, "compound flipH false")
assert(smartLayer.flipV == false, "compound flipV false")
local celComp = smartLayer:cel(1)
assert(celComp.image.width == 32, "compound cel width 32")
assert(celComp.image.height == 32, "compound cel height 32")
-- Pixel art 2x2 nearest neighbor block check
assert(app.pixelColor.rgbaR(celComp.image:getPixel(0, 0)) == 255, "block (0,0) Red")
assert(app.pixelColor.rgbaR(celComp.image:getPixel(1, 1)) == 255, "block (1,1) Red")

-- Test resetTransform
smartLayer:resetTransform()
assert(smartLayer.scale.x == 1.0, "reset scale.x 1.0")
assert(smartLayer.scale.y == 1.0, "reset scale.y 1.0")
assert(smartLayer.angle == 0.0, "reset angle 0.0")
assert(smartLayer.flipH == false, "reset flipH false")
assert(smartLayer.flipV == false, "reset flipV false")
assert(smartLayer.pivot.x == 0.5, "reset pivot.x 0.5")
local celReset = smartLayer:cel(1)
assert(celReset.image.width == 16, "reset cel width 16")
assert(celReset.image.height == 16, "reset cel height 16")
assert(app.pixelColor.rgbaR(celReset.image:getPixel(0, 0)) == 255, "reset TL is Red")

spr:close()
print("ALL M2 NATIVE C++ LUA BINDINGS & RASTER TESTS PASSED SUCCESSFULLY!")
