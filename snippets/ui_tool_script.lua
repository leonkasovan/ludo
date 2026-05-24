-- Tool script template — the correct pattern for Ludo tool scripts.
-- Tools must NOT call ui.Main() or ui.Uninit().

local ui    = require("ui")
local win_open = true
local win   = ui.NewWindow("My Tool", 600, 400, false)
win:SetMargined(1)
win:SetChild(ui.NewLabel("Content"))

win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
