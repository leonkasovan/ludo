-- ui.RadioButtons — set of mutually exclusive options
local radio = ui.NewRadioButtons()
radio:Append("Choice 1", "Choice 2", "Choice 3")
radio:SetSelected(0)
radio:OnSelected(function(r, data)
    print("Selected:", r:Selected())
end, nil)
