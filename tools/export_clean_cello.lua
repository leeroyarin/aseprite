local spr = app.open([[I:\Shared drives\Pitch Quest Shared Drive\Working Files\Music Store\Export\Instruments\Cello.aseprite]])
if not spr then return end

-- Layer 1 is green background (Layer 3 in Aseprite list)
-- Let's turn off Layer 3 (green background) and turn on Layer 4 (bow)
for i, l in ipairs(spr.layers) do
    if l.name == "Layer 3" then
        l.isVisible = false
        print("Set Layer 3 isVisible = false")
    elseif l.name == "Layer 4" then
        l.isVisible = true
        print("Set Layer 4 isVisible = true")
    end
end

for f = 1, 4 do
    local img = Image(spr.spec)
    img:drawSprite(spr, f)
    img:saveAs(string.format([[C:\Users\leero\Source\Repos\aseprite2\tools\cello_clean_f%d.png]], f))
    print("Saved cello frame " .. f)
end

spr:close()
