-- ui.Box — layout container (vertical or horizontal)
local vbox = ui.NewVerticalBox()
vbox:SetPadded(1)
vbox:Append(ui.NewLabel("Item 1"), false)       -- fixed height
vbox:Append(ui.NewLabel("Item 2"), false)
vbox:Append(ui.NewButton("Stretchy"), true)      -- fills remaining space

-- Horizontal box for a toolbar row
local hbox = ui.NewHorizontalBox()
hbox:SetPadded(1)
hbox:Append(ui.NewButton("Left"), false)
hbox:Append(ui.NewButton("Right"), false)
