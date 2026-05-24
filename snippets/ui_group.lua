-- ui.Group — labeled container with optional frame
local group = ui.NewGroup("Group Title")
group:SetMargined(1)

local box = ui.NewVerticalBox()
box:SetPadded(1)
box:Append(ui.NewLabel("Content inside the group"), false)
box:Append(ui.NewButton("Action"), false)
group:SetChild(box)

-- Get/set
print(group:Title())
group:SetTitle("Updated Title")
