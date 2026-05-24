-- ui.Spinbox — numeric entry with +/- buttons
local spin = ui.NewSpinbox(0, 100)
spin:SetValue(42)
spin:OnChanged(function(s, data)
    print("Spinbox value:", s:Value())
end, nil)
