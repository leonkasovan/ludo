-- ui.Entry — single-line text input
local entry = ui.NewEntry()
entry:SetText("default text")
entry:OnChanged(function(e, data)
    print("Text:", e:Text())
end, nil)

-- Variants
local pw = ui.NewPasswordEntry()   -- masked input
local sr = ui.NewSearchEntry()     -- search style (delayed OnChanged)

-- Read-only
entry:SetReadOnly(true)
print("Read only:", entry:ReadOnly())
