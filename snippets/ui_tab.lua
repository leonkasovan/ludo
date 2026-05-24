-- ui.Tab — multi-page container (one page visible at a time)
local tabs = ui.NewTab()
tabs:Append("Page 1", ui.NewLabel("Content of page 1"))
tabs:Append("Page 2", ui.NewLabel("Content of page 2"))

-- Insert at a specific position (0-based)
tabs:InsertAt("Page 0", 0, ui.NewLabel("Inserted at front"))

-- Get/set selection
tabs:SetSelected(0)
print("Active tab:", tabs:Selected())

-- Delete a page
tabs:Delete(2)              -- remove last page
print("Page count:", tabs:NumPages())

-- Margin per page
tabs:SetMargined(0, 1)      -- enable margin on page 0
print("Margined:", tabs:Margined(0))
