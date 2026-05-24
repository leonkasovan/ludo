-- ui.MultilineEntry — multi-line text area
local mle = ui.NewMultilineEntry()
mle:SetText("Hello\nWorld")
mle:Append("\nLine 3")           -- append at end
mle:SetReadOnly(1)               -- make read-only

-- Non-wrapping variant for code/log output
-- local nw = ui.NewNonWrappingMultilineEntry()

mle:OnChanged(function(e, data)
    print("Content changed")
end, nil)
