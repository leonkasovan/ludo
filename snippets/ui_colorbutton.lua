-- ui.ColorButton — button that opens a color picker
local btn = ui.NewColorButton()
btn:OnChanged(function(c, data)
    local r, g, b, a = c:Color()
    print(string.format("RGBA: %.3f %.3f %.3f %.3f", r, g, b, a))
end, nil)

-- Set color programmatically (channels 0.0-1.0)
btn:SetColor(1.0, 0.0, 0.0, 1.0)  -- red
