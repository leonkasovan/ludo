-- ui.Slider — draggable numeric slider
local slider = ui.NewSlider(0, 100)
slider:SetValue(50)
slider:OnChanged(function(s, data)
    print("Value:", s:Value())
end, nil)
slider:OnReleased(function(s, data)
    print("Drag finished at:", s:Value())
end, nil)

-- Range and tooltip
slider:SetRange(10, 90)
slider:SetHasToolTip(true)
print("Has tooltip:", slider:HasToolTip())
