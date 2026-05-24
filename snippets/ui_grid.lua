-- ui.Grid — flexible grid layout
-- GridAppend(grid, child, left, top, xspan, yspan, hexpand, halign, vexpand, valign)
local grid = ui.NewGrid()
grid:SetPadded(1)

-- A 2x2 grid
grid:Append(ui.NewButton("Top-Left"),     0, 0, 1, 1, false, ui.AlignFill,   false, ui.AlignFill)
grid:Append(ui.NewButton("Top-Right"),    1, 0, 1, 1, true,  ui.AlignFill,   false, ui.AlignFill)
grid:Append(ui.NewButton("Bottom-Left"),  0, 1, 1, 1, false, ui.AlignCenter, true,  ui.AlignCenter)
grid:Append(ui.NewButton("Bottom-Right"), 1, 1, 1, 1, true,  ui.AlignFill,   true,  ui.AlignFill)
