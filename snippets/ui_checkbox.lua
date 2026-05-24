-- ui.Checkbox — toggleable check box
local cb = ui.NewCheckbox("Enable feature")
cb:OnToggled(function(c, data)
    if c:Checked() == 1 then
        print("Enabled")
    else
        print("Disabled")
    end
end, nil)

-- Programmatic control
cb:SetChecked(1)     -- check it
cb:SetChecked(0)     -- uncheck it
