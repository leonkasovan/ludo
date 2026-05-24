-- ui.Table + ui.TableModel — tabular data display with editing

-- 1. Define data
local data = {
    { task = "Write docs", done = 0 },
    { task = "Fix bugs",   done = 1 },
    { task = "Release",    done = 0 },
}

-- 2. Define the table model handler
local handler = {
    NumColumns = function(m) return 2 end,
    ColumnType = function(m, col)
        if col == 0 then return ui.TableValueTypeString end
        return ui.TableValueTypeInt
    end,
    NumRows    = function(m) return #data end,
    CellValue  = function(m, row, col)
        local r = data[row + 1]
        if not r then return "" end
        if col == 0 then return r.task end
        return r.done
    end,
    SetCellValue = function(m, row, col, val)
        local r = data[row + 1]
        if not r then return end
        if col == 1 then r.done = val end  -- checkbox toggle
    end,
}

-- 3. Create model + table
local model = ui.NewTableModel(handler)
local tbl   = ui.NewTable(model)
tbl:AppendTextColumn("Task",      0, ui.TableModelColumnNeverEditable)
tbl:AppendCheckboxColumn("Done",  1, ui.TableModelColumnAlwaysEditable)

-- 4. Notify model of data changes
table.insert(data, { task = "New item", done = 0 })
model:RowInserted(#data - 1)

-- Remove last row
table.remove(data)
model:RowDeleted(#data)

-- Modify a row
data[1].done = 1
model:RowChanged(0)

-- Full replacement
data = { { task = "Reset", done = 0 } }
model:Reset()

-- Selection
tbl:OnSelectionChanged(function(t, d)
    local sel = t:GetSelection()
    if sel and #sel > 0 then
        print("Selected row:", sel[1])
    end
end, nil)

-- Column types: Text, Image, Text+Image, Checkbox, Checkbox+Text, ProgressBar, Button
-- tbl:AppendTextColumn("Name", colIndex, editableModelCol [, colorModelCol])
-- tbl:AppendCheckboxColumn("Done", colIndex, editableModelCol)
-- tbl:AppendProgressBarColumn("Progress", colIndex)
-- tbl:AppendButtonColumn("Action", colIndex, clickableModelCol)
-- tbl:AppendImageColumn("Icon", colIndex)
-- tbl:AppendImageTextColumn("Item", imageCol, textCol, textEditableCol [, colorCol])
-- tbl:AppendCheckboxTextColumn("Pick", checkboxCol, cbEditableCol, textCol, textEditableCol [, colorCol])
