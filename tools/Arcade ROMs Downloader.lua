-- Arcade (FBNeo) ROMs Downloader
-- Downloads Arcade ROMs from the archive.org.
-- Requires lualib/ftcsv.lua for CSV parsing.
-- CSV db files must be in the same directory as this script.

local ui    = require("ui")
local ftcsv = require("ftcsv")
local win_open = true

-- Determine the tools directory from this script's own path.
local _src = debug.getinfo(1, "S").source or ""
local TOOLS_DIR = _src:match("^@(.+)[\\/][^\\/]+$") or "tools"

-- Platform definitions using the actual CSV header column names.
local PLATFORMS = {
    { name = "FBNeo", file = "FBNEO.csv",
	  source = "https://archive.org/download/cylums-final-burn-neo-rom-collection/Cylum%27s%20FinalBurn%20Neo%20ROM%20Collection%20%2802-18-21%29",
      col_id  = "game_id",
      col_name = "game_name",
      col_info = "game_info",
	  col_size   = "game_size" },
    { name = "MAME", file = "MAME.csv",
	  source = "https://archive.org/download/mame-0.221-roms-merged",
      col_id  = "game_id",
      col_name = "game_name",
      col_info = "game_info",
	  col_size   = "game_size" },
}
local MAX_RESULTS = 99
local search_results = {}

local function trim(s)
    return (s or ""):match("^%s*(.-)%s*$") or ""
end

local function format_size(s)
    local n = tonumber(s)
    if not n or n == 0 then return "?" end
    if n >= 1073741824 then return string.format("%.2f GB", n / 1073741824)
    elseif n >= 1048576  then return string.format("%.2f MB", n / 1048576)
    elseif n >= 1024     then return string.format("%.2f KB", n / 1024)
    else return tostring(n) .. " B" end
end

local function ascii_to_hex(s)
    return (s or ""):gsub(".", function(c) return string.format("%02X", string.byte(c)) end)
end

local function search_platform(platform, query, results)
    local filepath = TOOLS_DIR .. "/" .. platform.file
	-- local filepath = platform.file
    local ok, iter = pcall(ftcsv.parseLine, filepath, "|")
    if not ok then
        ludo.logInfo("Arcade ROMs Downloader: cannot open " .. filepath .. ": " .. tostring(iter))
        return
    end
    local q = query:lower()
    for _, row in iter do
        if #results >= MAX_RESULTS then break end
        local id   = trim(row[platform.col_id])
        local name = trim(row[platform.col_name])
        if (name:lower():find(q, 1, true) or id:lower():find(q, 1, true)) then
			-- Search results: { platform, game_id, game_name, game_info, rom_url, rom_size }
			table.insert(results, {
				platform  = platform.name,
				game_id   = id,
				game_name = name,
				game_info = trim(row[platform.col_info]),
				rom_url   = platform.source .. "/" .. id,
				rom_size  = trim(row[platform.col_size]),
			})
		end
    end
end

-- Table model column indices (0-based, matches AppendTextColumn order).
local COL_NUM      = 0
local COL_PLATFORM = 1
local COL_NAME     = 2
local COL_INFO     = 3

local handler = {
    NumColumns  = function(m) return 4 end,
    ColumnType  = function(m, col) return ui.TableValueTypeString end,
    NumRows     = function(m) return #search_results end,
    CellValue   = function(m, row, col)
        local r = search_results[row + 1]   -- model rows are 0-based
        if not r then return "" end
        if     col == COL_NUM      then return tostring(row + 1)
        elseif col == COL_PLATFORM then return r.platform or ""
        elseif col == COL_NAME     then return r.game_name or ""
        elseif col == COL_INFO     then return r.game_info or ""
        end
        return ""
    end,
    SetCellValue = function(m, row, col, val) end,  -- read-only
}

local model = ui.NewTableModel(handler)

-- UI construction

local win = ui.NewWindow("Arcade ROMs Downloader", 860, 600, false)
win:SetMargined(1)

local root = ui.NewVerticalBox()
root:SetPadded(1)

-- Platform checkboxes
local plat_group = ui.NewGroup("Platform")
plat_group:SetMargined(1)
local plat_box = ui.NewHorizontalBox()
plat_box:SetPadded(1)
local plat_cbs = {}
for _, p in ipairs(PLATFORMS) do
    local cb = ui.NewCheckbox(p.name)
    plat_cbs[p.name] = cb
    plat_box:Append(cb, false)
end
plat_group:SetChild(plat_box)
root:Append(plat_group, false)

-- Search row
local search_hbox = ui.NewHorizontalBox()
search_hbox:SetPadded(1)
search_hbox:Append(ui.NewLabel("Search:"), false)
local search_entry = ui.NewEntry()
local search_btn   = ui.NewButton("  Search  ")
search_hbox:Append(search_entry, true)
search_hbox:Append(search_btn, false)
root:Append(search_hbox, false)

-- Status label
local status_lbl = ui.NewLabel("Select platforms, type a game name then click Search.")
root:Append(status_lbl, false)

-- Results table (stretchy — takes remaining vertical space)
root:Append(ui.NewLabel("Results (up to " .. MAX_RESULTS .. "):"), false)
local results_tbl = ui.NewTable(model)
results_tbl:AppendTextColumn("#",        COL_NUM,      ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Platform", COL_PLATFORM, ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Name", COL_NAME,       ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Info",   COL_INFO,   ui.TableModelColumnNeverEditable)
results_tbl:SetSelectionMode(ui.TableSelectionModeZeroOrOne)
results_tbl:ColumnSetWidth(0, 40)
results_tbl:ColumnSetWidth(1, 80)
results_tbl:ColumnSetWidth(2, 150)
root:Append(results_tbl, true)

-- Game details group
-- { platform, game_id, game_name, game_info, rom_url, rom_size }
local det_group = ui.NewGroup("Rom Details")
det_group:SetMargined(1)
local det_box = ui.NewVerticalBox()
det_box:SetPadded(1)
local lbl_plat   = ui.NewLabel("Platform : -")
local lbl_id     = ui.NewLabel("Game ID  : -")
local lbl_name   = ui.NewLabel("Name     : -")
local lbl_info   = ui.NewLabel("Info     : -")
local lbl_url    = ui.NewLabel("URL      : -")
local lbl_size   = ui.NewLabel("Size     : -")
det_box:Append(lbl_plat)
det_box:Append(lbl_id)
det_box:Append(lbl_name)
det_box:Append(lbl_info)
det_box:Append(lbl_url)
det_box:Append(lbl_size)
det_group:SetChild(det_box)
root:Append(det_group, false)

-- Download button
local dl_btn = ui.NewButton("  Download Selected  ")
root:Append(dl_btn, false)

win:SetChild(root)

-- Callbacks

local function update_details(idx)
    if not idx or idx < 1 or idx > #search_results then
        lbl_plat:SetText("Platform : -")
        lbl_id:SetText("Game ID  : -")
        lbl_name:SetText("Name     : -")
        lbl_info:SetText("Info     : -")
        lbl_url:SetText("URL      : -")
        lbl_size:SetText("Size     : -")
        return
    end
    local r = search_results[idx]
    lbl_plat:SetText("Platform : " .. (r.platform or "-"))
    lbl_id:SetText("Game ID  : " .. (r.game_id or "-"))
    lbl_name:SetText("Name     : " .. (r.game_name or "-"))
    lbl_info:SetText("Info     : " .. (r.game_info or "-"))
    lbl_url:SetText("URL      : " .. (r.rom_url or "-"))
    lbl_size:SetText("Size     : " .. (r.rom_size or "-"))
end

-- Clicking a row in the table updates the details panel.
results_tbl:OnSelectionChanged(function()
    local sel = results_tbl:GetSelection()
    if sel and #sel > 0 then
        update_details(sel[1] + 1)  -- GetSelection returns 0-based indices
    else
        update_details(nil)
    end
end)

search_btn:OnClicked(function(b, data)
    local query = search_entry:Text()
    if not query or query:match("^%s*$") then
        status_lbl:SetText("Please enter a search term.")
        return
    end
    local any = false
    for _, p in ipairs(PLATFORMS) do
        if plat_cbs[p.name]:Checked() == 1 then any = true; break end
    end
    if not any then
        status_lbl:SetText("Please select at least one platform.")
        return
    end

    status_lbl:SetText("Searching...")

    -- Remove all current rows.  Iterate from last to first so that the
    -- 0-based index passed to RowDeleted is always valid at the time of the
    -- call (data count decreases by 1 after each removal).
    local old_count = #search_results
    for i = old_count, 1, -1 do
        table.remove(search_results, i)
        model:RowDeleted(i - 1)
    end

    -- Populate search_results from selected platforms.
    for _, p in ipairs(PLATFORMS) do
        if plat_cbs[p.name]:Checked() == 1 then
            search_platform(p, query, search_results)
        end
    end

    -- Notify the model about all newly added rows.
    for i = 1, #search_results do
        model:RowInserted(i - 1)
    end

    update_details(nil)
    status_lbl:SetText(string.format("Found %d result(s) for '%s'.", #search_results, query))
end, nil)

dl_btn:OnClicked(function(b, data)
    local sel = results_tbl:GetSelection()
    if not sel or #sel == 0 then
        status_lbl:SetText("No game selected. Run a search first.")
        return
    end
    local idx = sel[1] + 1  -- convert 0-based to 1-based Lua index
    if idx < 1 or idx > #search_results then
        status_lbl:SetText("No game selected.")
        return
    end
    local r = search_results[idx]
    local outdir = ludo.getOutputDirectory()

    -- Queue the PKG download.
    local _, pkg_status, pkg_out = ludo.newDownload(r.rom_url, outdir, ludo.DOWNLOAD_NOW)
    if pkg_status == 200 or pkg_status == 206 or pkg_status == 0 then
        ludo.logSuccess("Queued PKG: " .. (pkg_out))
    else
        ludo.logError("Preflight HTTP " .. tostring(pkg_status) .. " for " .. r.game_id)
    end

    status_lbl:SetText("Download queued: " .. r.game_name)

    -- Close the window: win:Close() does not exist in libuilua.
    -- Set win_open=false to exit the MainStep loop, then Destroy the window.
    win_open = false
    win:Destroy()
end, nil)

-- Show window and drive a nested event loop until the window is closed.
-- Tool scripts must NOT call ui.Main()/ui.Uninit() — Ludo's own event loop
-- is already running.  Use MainStep() instead.
win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)
win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
