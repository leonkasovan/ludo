-- Base control methods — available on ALL widgets
local ctrl = ui.NewButton("Test")

ctrl:Show()          -- make visible
ctrl:Hide()          -- hide
ctrl:Enable()        -- enable interaction
ctrl:Disable()       -- disable/grey out
print("Visible:", ctrl:Visible(), "Enabled:", ctrl:Enabled())
ctrl:Destroy()       -- free resources
