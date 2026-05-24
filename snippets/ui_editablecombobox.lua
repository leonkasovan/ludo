-- ui.EditableCombobox — dropdown with free-text entry
local ec = ui.NewEditableCombobox()
ec:Append("Apple", "Banana", "Cherry")
ec:OnChanged(function(c, data)
    print("Text:", c:Text())
end, nil)

-- Set text programmatically
ec:SetText("Custom value")
