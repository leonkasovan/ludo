-- ui.Window — top-level window
local win = ui.NewWindow("My Window", 640, 480, false)
win:SetMargined(1)
win:SetChild(ui.NewLabel("Content goes here"))
win:Show()
