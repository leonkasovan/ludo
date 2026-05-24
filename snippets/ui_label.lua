-- ui.Label — static text display
local lbl = ui.NewLabel("Status: ready")
lbl:SetText("Status: changed")
print(lbl:Text())   -- "Status: changed"
