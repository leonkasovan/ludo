-- ui.Combobox — read-only dropdown
local combo = ui.NewCombobox()
combo:Append("Option A", "Option B", "Option C")
combo:SetSelected(0)                  -- select first item
combo:OnToggled(function(c, data)
    print("Selected:", c:Selected())
end, nil)

-- Item management
combo:InsertAt(1, "Option B2")        -- insert at index
combo:Delete(0)                       -- remove first item
combo:Clear()                         -- remove all
print("Items:", combo:NumItems())
