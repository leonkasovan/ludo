-- ui.ProgressBar — determinate (0-100) or indeterminate (-1)
local pb = ui.NewProgressBar()
pb:SetValue(50)    -- 50% filled
pb:SetValue(-1)    -- indeterminate / pulsing mode
print("Current value:", pb:Value())
