-- ui.Button — clickable button
local btn = ui.NewButton("  Click Me  ")
btn:OnClicked(function(b, data)
    print("Button clicked!")
end, nil)

-- Get/set text
print(btn:Text())   -- "  Click Me  "
btn:SetText("  New Label  ")
