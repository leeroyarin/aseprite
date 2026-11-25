"""Exercise Smart Object tag states through Layer Properties."""
import os
import sys
import subprocess
import json
sys.path.insert(0, os.path.abspath("tools/ui-test"))
from uitest import UiTest, UiTestError

with UiTest(timeout=90) as t:
    source = os.path.join(t.workdir, "tag-source.aseprite").replace("\\", "/")
    setup = os.path.join(t.workdir, "source.lua")
    with open(setup, "w") as f:
        f.write("""
local s=Sprite(2,3)
s:newEmptyFrame(2)
for frame=1,2 do
  local image=Image(2,3)
  image:clear(Color{r=255,g=0,b=0,a=255})
  if s.layers[1]:cel(frame) then s:deleteCel(s.layers[1], frame) end
  s:newCel(s.layers[1], frame, image)
end
local a=s:newTag(1,1); a.name="Idle"
local b=s:newTag(2,2); b.name="Attack"
s:saveAs(%s)
""" % json.dumps(source))
    env = dict(os.environ, ASEPRITE_USER_FOLDER=t.user_dir)
    subprocess.run([t.exe, "-b", "--script", setup], env=env, check=True)
    t.lua("""
local h=Sprite(8,8)
h:newEmptyFrame(2)
h:newEmptyFrame(3)
h:newEmptyFrame(4)
app.command.PlaceSmartObject{filename=%s,ui=false}
""" % json.dumps(source))
    try:
        t.lua("app.command.LayerProperties()")
    except UiTestError as error:
        if "no result" not in str(error):
            raise
    t.wait_for(lambda: t.find(text="Apply states"))
    entry = next(w for w in t.find(cls="ui::Entry") if w["id"] == "sm_states")
    x,y,w,h = entry["rect"]
    t.click(x+w//2,y+h//2)
    t.type("1=Idle;2=Attack")
    t.click_widget(text="Apply states")
    t.sleep(300)
    t.lua('assert(app.activeLayer.smartObjectAnimationStates == "1=Idle;2=Attack", app.activeLayer.smartObjectAnimationStates)')
    print("PASS Layer Properties applies tag cues")
    entry = next(w for w in t.find(cls="ui::Entry") if w["id"] == "sm_states")
    x,y,w,h = entry["rect"]
    t.click(x+w//2,y+h//2)
    t.key("a", mods="ctrl")
    t.type("1=Missing")
    t.click_widget(text="Apply states")
    t.sleep(300)
    t.lua('assert(app.activeLayer.smartObjectAnimationStates == "1=Idle;2=Attack")')
    assert any("existing source tag" in w.get("text","") for w in t.find(cls="Label"))
    print("PASS invalid source tag preserves prior states and explains error")
    def widget(widget_id, cls):
        return next(w for w in t.find(cls=cls) if w["id"] == widget_id)

    def enter(widget_id, cls, value):
        x, y, w, h = widget(widget_id, cls)["rect"]
        t.click(x+w//2, y+h//2)
        t.key("a", mods="ctrl")
        t.type(value)

    t.click_widget(text="New animation state...")
    t.wait_for(lambda: t.find(text="Save state"))
    enter("sm_state_last", "IntEntry", "1")
    t.click_widget(text="Loop within range")
    t.click_widget(text="Save state")
    t.lua('assert(app.activeLayer.smartObjectAnimationStates == "1-1H=Idle;2=Attack", app.activeLayer.smartObjectAnimationStates)')
    print("PASS New animation state saves an inclusive range and loop choice")

    enter("sm_frame", "ui::Entry", "2")
    enter("sm_angle", "ui::Entry", "90")
    t.sleep(650)

    t.lua("""
local layer = app.activeLayer
assert(layer:cel(1).image.width == 2 and layer:cel(1).image.height == 3)
assert(layer:cel(2).image.width == 3 and layer:cel(2).image.height == 2,
  tostring(layer:cel(2).image.width) .. "x" .. tostring(layer:cel(2).image.height)
  .. " frame4=" .. layer:cel(4).image.width .. " props=" ..
  tostring(layer.properties("smart_object_frame_transforms")["1"]))
assert(layer:cel(3).image.width == 2 and layer:cel(3).image.height == 3)
""")
    print("PASS Layer Properties rotates the chosen frame without changing neighbors")
    t.key("esc")
    t.lua("app.activeFrame = 3; app.activeTool = 'move'")
    info = t.call("active_cel_screen")
    x, y, w, h = info["bounds"]
    t.move(x+w//2, y-18*info["scale"])
    assert t.cursor() == "move"
    t.drag(x+w//2, y-18*info["scale"], x+w+24, y+h//2)
    t.lua("""
local layer = app.activeLayer
assert(layer:cel(1).image.width == 2 and layer:cel(1).image.height == 3)
assert(layer:cel(2).image.width == 3 and layer:cel(2).image.height == 2,
  tostring(layer:cel(2).image.width) .. "x" .. tostring(layer:cel(2).image.height)
  .. " frame4=" .. layer:cel(4).image.width .. " props=" ..
  tostring(layer.properties("smart_object_frame_transforms")["1"]))
local tr = layer.properties("smart_object_frame_transforms")["2"]
assert(tr and math.abs(tr[5] - 90) < 5, "expected approximately a quarter turn")
assert(layer:cel(3).image.width > 2, "rotation did not reach rendered pixels")
assert(layer:cel(4).image.width == 2 and layer:cel(4).image.height == 3)
""")
    print("PASS canvas rotation handle changes the active frame only")
    t.lua("app.activeFrame = 4; app.editor.zoom = 8")
    x, y, w, h = t.call("active_cel_screen")["bounds"]
    t.drag(x+w, y+h, x+w*2, y+h*2)
    t.lua("""
local layer = app.activeLayer
assert(layer:cel(4).image.width > 2 and layer:cel(4).image.height > 3)
assert(layer:cel(1).image.width == 2 and layer:cel(1).image.height == 3)
assert(layer:cel(2).image.width == 3 and layer:cel(2).image.height == 2)
""")
    print("PASS canvas scale handle changes the active frame only")
print("6/6 passed")
