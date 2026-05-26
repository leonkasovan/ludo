-- libui-ng Widget Demo
-- Run from Ludo: Tools > UI Demo (or via -s tools/ui_demo.lua)
-- Demonstrates every widget available in libui-ng via Lua bindings.
--
-- Tools must NOT call ui.Main()/ui.Uninit() — use MainStep() instead.

local ui = require("ui")
local win_open = true

local COL_NAME = 0
local COL_VALUE = 1
local demo_data = {
    { name = "Alpha", value = 42 },
    { name = "Beta",  value = 17 },
    { name = "Gamma", value = 99 },
}

local handler = {
    NumColumns = function(m) return 2 end,
    ColumnType = function(m, col) return ui.TableValueTypeString end,
    NumRows    = function(m) return #demo_data end,
    CellValue  = function(m, row, col)
        local r = demo_data[row + 1]
        if not r then return "" end
        if col == COL_NAME  then return r.name end
        if col == COL_VALUE then return tostring(r.value) end
        return ""
    end,
    SetCellValue = function(m, row, col, val) end,
}

local model = ui.NewTableModel(handler)

-- Main window
local win = ui.NewWindow("libui-ng Widget Demo", 780, 620, false)
win:SetMargined(1)

-- Root: a Tab control so widgets are organized by category
local tabs = ui.NewTab()
win:SetChild(tabs)

-- ============================================================
-- Tab 1: Basic Inputs
-- ============================================================
local basic = ui.NewVerticalBox()
basic:SetPadded(1)

-- Row: label + entry + button
local row1 = ui.NewHorizontalBox()
row1:SetPadded(1)
row1:Append(ui.NewLabel("Your name:"), false)
local name_entry = ui.NewEntry()
row1:Append(name_entry, true)
local greet_btn = ui.NewButton("Greet")
row1:Append(greet_btn, false)
basic:Append(row1, false)

-- Password & Search entries
local row1b = ui.NewHorizontalBox()
row1b:SetPadded(1)
row1b:Append(ui.NewLabel("Password:"), false)
local pw_entry = ui.NewPasswordEntry()
row1b:Append(pw_entry, true)
row1b:Append(ui.NewLabel("Search:"), false)
local sr_entry = ui.NewSearchEntry()
row1b:Append(sr_entry, true)
basic:Append(row1b, false)

-- Greeting label + Checkbox
local greet_lbl = ui.NewLabel("(no greeting yet)")
basic:Append(greet_lbl, false)

local cb = ui.NewCheckbox("Enable debug logging")
cb:OnToggled(function(c, d)
    greet_lbl:SetText("Checkbox: " .. (c:Checked() == 1 and "ON" or "OFF"))
end, nil)
basic:Append(cb, false)

-- Separator + Group example
basic:Append(ui.NewHorizontalSeparator(), false)
local group = ui.NewGroup("Settings Group")
local gbox = ui.NewVerticalBox()
gbox:SetPadded(1)
gbox:Append(ui.NewLabel("A label inside a group"), false)
local inner_btn = ui.NewButton("Group Button")
gbox:Append(inner_btn, false)
group:SetChild(gbox)
basic:Append(group, false)

-- Button callbacks
greet_btn:OnClicked(function(b, d)
    local name = name_entry:Text()
    if name == "" then name = "World" end
    greet_lbl:SetText("Hello, " .. name .. "!")
end, nil)

inner_btn:OnClicked(function(b, d)
    greet_lbl:SetText("Button inside group clicked!")
end, nil)

tabs:Append("Basic Inputs", basic)

-- ============================================================
-- Tab 2: Numeric & Selection
-- ============================================================
local numeric = ui.NewVerticalBox()
numeric:SetPadded(1)

-- Slider
local sld_row = ui.NewHorizontalBox()
sld_row:SetPadded(1)
sld_row:Append(ui.NewLabel("Slider:"), false)
local slider = ui.NewSlider(0, 100)
slider:SetValue(50)
sld_row:Append(slider, true)
local sld_val = ui.NewLabel("50")
sld_row:Append(sld_val, false)
numeric:Append(sld_row, false)
slider:OnChanged(function(s, d)
    sld_val:SetText(tostring(s:Value()))
end, nil)

-- Spinbox
local spn_row = ui.NewHorizontalBox()
spn_row:SetPadded(1)
spn_row:Append(ui.NewLabel("Spinbox:"), false)
local spinbox = ui.NewSpinbox(0, 100)
spinbox:SetValue(25)
spn_row:Append(spinbox, true)
numeric:Append(spn_row, false)
spinbox:OnChanged(function(s, d)
    sld_val:SetText("Spinbox: " .. s:Value())
end, nil)

-- ProgressBar
local pb = ui.NewProgressBar()
pb:SetValue(-1)
numeric:Append(ui.NewLabel("ProgressBar (indeterminate):"), false)
numeric:Append(pb, false)

-- Combobox
numeric:Append(ui.NewLabel("Combobox:"), false)
local combo = ui.NewCombobox()
combo:Append("Option A", "Option B", "Option C", "Option D")
combo:SetSelected(0)
combo:OnToggled(function(c, d)
    sld_val:SetText("Combobox: " .. c:Selected())
end, nil)
numeric:Append(combo, false)

-- EditableCombobox
numeric:Append(ui.NewLabel("Editable Combobox:"), false)
local ecombo = ui.NewEditableCombobox()
ecombo:Append("Apple", "Banana", "Cherry")
ecombo:OnChanged(function(ec, d)
    sld_val:SetText("Editable: " .. ec:Text())
end, nil)
numeric:Append(ecombo, false)

-- RadioButtons
numeric:Append(ui.NewLabel("Radio Buttons:"), false)
local radio = ui.NewRadioButtons()
radio:Append("Choice 1", "Choice 2", "Choice 3")
radio:SetSelected(0)
radio:OnSelected(function(r, d)
    sld_val:SetText("Radio: " .. r:Selected())
end, nil)
numeric:Append(radio, false)

-- Separator + ColorButton
numeric:Append(ui.NewHorizontalSeparator(), false)
local color_row = ui.NewHorizontalBox()
color_row:SetPadded(1)
color_row:Append(ui.NewLabel("ColorButton:"), false)
local cb_btn = ui.NewColorButton()
color_row:Append(cb_btn, true)
numeric:Append(color_row, false)
cb_btn:OnChanged(function(c, d)
    local r, g, b, a = c:Color()
    sld_val:SetText(string.format("Color: %.2f %.2f %.2f", r, g, b))
end, nil)

tabs:Append("Numeric/Select", numeric)

-- ============================================================
-- Tab 3: DateTime
-- ============================================================
local dt = ui.NewVerticalBox()
dt:SetPadded(1)

dt:Append(ui.NewLabel("DateTimePicker:"), false)
local dtp = ui.NewDateTimePicker()
dt:Append(dtp, false)

dt:Append(ui.NewLabel("DatePicker:"), false)
local dp = ui.NewDatePicker()
dt:Append(dp, false)

dt:Append(ui.NewLabel("TimePicker:"), false)
local tp = ui.NewTimePicker()
dt:Append(tp, false)

local dt_info = ui.NewLabel("(date/time info)")
dt:Append(dt_info, false)

local dt_btn = ui.NewButton("  Show Selected Time  ")
dt:Append(dt_btn, false)
dt_btn:OnClicked(function(b, d)
    local t = dtp:Time()
    dt_info:SetText(string.format("Date: %04d-%02d-%02d  Time: %02d:%02d:%02d",
        t.year, t.month, t.day, t.hour, t.min, t.sec))
end, nil)

dtp:OnChanged(function(p, d)
    dt_info:SetText("DateTimePicker changed")
end, nil)

tabs:Append("Date/Time", dt)

-- ============================================================
-- Tab 4: Containers (Form + Grid)
-- ============================================================
local containers = ui.NewVerticalBox()
containers:SetPadded(1)

-- Form
containers:Append(ui.NewLabel("Form container (stretches labels):"), false)
local form = ui.NewForm()
form:SetPadded(1)
form:Append("Username", ui.NewEntry(), false)
form:Append("Password", ui.NewPasswordEntry(), false)
form:Append("Server", ui.NewEntry(), false)
containers:Append(form, false)

-- Grid
containers:Append(ui.NewHorizontalSeparator(), false)
containers:Append(ui.NewLabel("Grid container:"), false)
local grid = ui.NewGrid()
grid:SetPadded(1)

local g1 = ui.NewButton("NW")
local g2 = ui.NewButton("N")
local g3 = ui.NewButton("NE")
local g4 = ui.NewButton("W")
local g5 = ui.NewButton("CENTER")
local g6 = ui.NewButton("E")
local g7 = ui.NewButton("SW")
local g8 = ui.NewButton("S")
local g9 = ui.NewButton("SE")

grid:Append(g1, 0, 0, 1, 1, false, ui.AlignCenter, false, ui.AlignCenter)
grid:Append(g2, 1, 0, 1, 1, true,  ui.AlignFill,   false, ui.AlignCenter)
grid:Append(g3, 2, 0, 1, 1, false, ui.AlignCenter, false, ui.AlignCenter)
grid:Append(g4, 0, 1, 1, 1, false, ui.AlignCenter, true,  ui.AlignFill)
grid:Append(g5, 1, 1, 1, 1, true,  ui.AlignFill,   true,  ui.AlignFill)
grid:Append(g6, 2, 1, 1, 1, false, ui.AlignCenter, true,  ui.AlignFill)
grid:Append(g7, 0, 2, 1, 1, false, ui.AlignCenter, false, ui.AlignCenter)
grid:Append(g8, 1, 2, 1, 1, true,  ui.AlignFill,   false, ui.AlignCenter)
grid:Append(g9, 2, 2, 1, 1, false, ui.AlignCenter, false, ui.AlignCenter)

containers:Append(grid, true)

tabs:Append("Containers", containers)

-- ============================================================
-- Tab 5: Table
-- ============================================================
local table_tab = ui.NewVerticalBox()
table_tab:SetPadded(1)

local tbl = ui.NewTable(model)
tbl:AppendTextColumn("Name",  COL_NAME,  ui.TableModelColumnNeverEditable)
tbl:AppendTextColumn("Value", COL_VALUE, ui.TableModelColumnAlwaysEditable)
tbl:SetSelectionMode(ui.TableSelectionModeZeroOrOne)
tbl:ColumnSetWidth(0, 150)
tbl:ColumnSetWidth(1, 100)
table_tab:Append(tbl, true)

local tbl_btns = ui.NewHorizontalBox()
tbl_btns:SetPadded(1)
local tbl_add = ui.NewButton("  Add Row  ")
local tbl_del = ui.NewButton("  Delete Row  ")
local tbl_chg = ui.NewButton("  Change Value  ")
local tbl_reset = ui.NewButton("  Reset  ")
tbl_btns:Append(tbl_add, false)
tbl_btns:Append(tbl_del, false)
tbl_btns:Append(tbl_chg, false)
tbl_btns:Append(tbl_reset, false)
table_tab:Append(tbl_btns, false)

local tbl_status = ui.NewLabel("Select a row and edit the Value column, or use the buttons.")
table_tab:Append(tbl_status, false)

tbl_add:OnClicked(function(b, d)
    table.insert(demo_data, { name = "New", value = 0 })
    model:RowInserted(#demo_data - 1)
    tbl_status:SetText("Added row " .. #demo_data)
end, nil)

tbl_del:OnClicked(function(b, d)
    local sel = tbl:GetSelection()
    if not sel or #sel == 0 then
        tbl_status:SetText("No row selected")
        return
    end
    local idx = sel[1] + 1
    table.remove(demo_data, idx)
    model:RowDeleted(sel[1])
    tbl_status:SetText("Deleted row " .. idx)
end, nil)

tbl_chg:OnClicked(function(b, d)
    local sel = tbl:GetSelection()
    if not sel or #sel == 0 then
        tbl_status:SetText("No row selected")
        return
    end
    local r = demo_data[sel[1] + 1]
    if r then
        r.value = r.value + 1
        model:RowChanged(sel[1])
        tbl_status:SetText("Incremented row " .. (sel[1] + 1))
    end
end, nil)

tbl_reset:OnClicked(function(b, d)
    demo_data = {
        { name = "ResetA", value = 10 },
        { name = "ResetB", value = 20 },
        { name = "ResetC", value = 30 },
    }
    model:Reset()
    tbl_status:SetText("Model reset to 3 rows")
end, nil)

tbl:OnRowClicked(function(t, row, data)
    local r = demo_data[row + 1]
    if r then
        tbl_status:SetText("Clicked row " .. (row + 1) .. ": " .. r.name)
    end
end, nil)

tbl:OnSelectionChanged(function(t, data)
    local sel = t:GetSelection()
    if sel and #sel > 0 then
        tbl_status:SetText("Selected row " .. (sel[1] + 1))
    else
        tbl_status:SetText("Selection cleared")
    end
end, nil)

tabs:Append("Table", table_tab)

-- ============================================================
-- Tab 6: Dialogs & Misc
-- ============================================================
local dialogs = ui.NewVerticalBox()
dialogs:SetPadded(1)

local dlg_info = ui.NewLabel("Click a button to open a dialog.")

local dlg_btns = ui.NewVerticalBox()
dlg_btns:SetPadded(1)

local msg_btn = ui.NewButton("  Message Box  ")
msg_btn:OnClicked(function(b, d)
    ui.MsgBox(win, "Hello", "This is a message box from the demo.")
end, nil)
dlg_btns:Append(msg_btn, false)

local err_btn = ui.NewButton("  Error Box  ")
err_btn:OnClicked(function(b, d)
    ui.MsgBoxError(win, "Error", "Something went wrong (demo error).")
end, nil)
dlg_btns:Append(err_btn, false)

local open_btn = ui.NewButton("  Open File Dialog  ")
open_btn:OnClicked(function(b, d)
    local path = ui.OpenFile(win)
    if path then
        dlg_info:SetText("Selected: " .. path)
    else
        dlg_info:SetText("Open File cancelled")
    end
end, nil)
dlg_btns:Append(open_btn, false)

local folder_btn = ui.NewButton("  Open Folder Dialog  ")
folder_btn:OnClicked(function(b, d)
    local path = ui.OpenFolder(win)
    if path then
        dlg_info:SetText("Selected folder: " .. path)
    else
        dlg_info:SetText("Open Folder cancelled")
    end
end, nil)
dlg_btns:Append(folder_btn, false)

local save_btn = ui.NewButton("  Save File Dialog  ")
save_btn:OnClicked(function(b, d)
    local path = ui.SaveFile(win)
    if path then
        dlg_info:SetText("Save path: " .. path)
    else
        dlg_info:SetText("Save File cancelled")
    end
end, nil)
dlg_btns:Append(save_btn, false)

dialogs:Append(dlg_btns, false)
dialogs:Append(ui.NewHorizontalSeparator(), false)
dialogs:Append(dlg_info, false)

-- MultilineEntry for logging
dialogs:Append(ui.NewLabel("MultilineEntry (log area):"), false)
local log_area = ui.NewMultilineEntry()
log_area:SetReadOnly(1)
log_area:Append("Demo started.\n")
dialogs:Append(log_area, true)

local log_row = ui.NewHorizontalBox()
log_row:SetPadded(1)
local log_btn = ui.NewButton("  Append Log  ")
log_row:Append(log_btn, false)
local log_clear = ui.NewButton("  Clear  ")
log_row:Append(log_clear, false)
dialogs:Append(log_row, false)

log_btn:OnClicked(function(b, d)
    log_area:Append("Log entry at " .. os.date("%H:%M:%S") .. "\n")
end, nil)
log_clear:OnClicked(function(b, d)
    log_area:SetText("")
end, nil)

tabs:Append("Dialogs & Log", dialogs)

-- ============================================================
-- Window callbacks & event loop
-- ============================================================
win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
