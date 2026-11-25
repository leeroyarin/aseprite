local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Violine.aseprite]])
if not spr then return end
for i, l in ipairs(spr.layers) do
    print(string.format("Layer %d: '%s' isVisible=%s", i, l.name, tostring(l.isVisible)))
end
for f = 1, 4 do
    local img = Image(spr.spec)
    img:drawSprite(spr, f)
    img:saveAs(string.format([[C:\Users\leero\Source\Repos\aseprite2\tools\violin_f%d.png]], f))
    print("Saved violin frame " .. f)
end
spr:close()
