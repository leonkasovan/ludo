-- Dialog functions — MsgBox, OpenFile, SaveFile, OpenFolder
-- All dialog functions require a parent window as first argument.

-- Message / Error boxes
ui.MsgBox(win, "Title", "Message text")
ui.MsgBoxError(win, "Error Title", "Error description")

-- File dialogs (return nil on cancel, string on success)
local path = ui.OpenFile(win)
if path then print("Selected:", path) end

local folder = ui.OpenFolder(win)
if folder then print("Folder:", folder) end

local save = ui.SaveFile(win)
if save then print("Save to:", save) end
