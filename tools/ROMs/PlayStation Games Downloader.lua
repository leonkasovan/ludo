-- PlayStation Games Downloader
-- Downloads PS game PKG files from the PlayStation Store CDN.
-- Supports PSX, PSP, PSV, PS3, PSM platforms.
--
-- Requires lualib/ftcsv.lua for TSV parsing.
-- TSV files (PSX_GAMES.tsv etc.) must be in the same directory as this script.

local ui    = require("ui")
local ftcsv = require("ftcsv")
local win_open = true
local window_alive = true

-- Determine the tools directory from this script's own path.
local _src = debug.getinfo(1, "S").source or ""
local TOOLS_DIR = _src:match("^@(.+)[\\/][^\\/]+$") or "tools"

-- Platform definitions using the actual TSV header column names.
local PLATFORMS = {
    { name = "PSX", file = "PSX_GAMES.tsv",
      scrape_url = "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=57&recherche=",
      database_url = "https://nopaystation.com/tsv/PSX_GAMES.tsv",
      col_title_id  = "Title ID",           col_region = "Region", col_content_id = "Content ID",
      col_name = "Name",              col_pkg    = "PKG direct link",
      col_date = "Last Modification Date",       col_size   = "File Size" },
    { name = "PSP", file = "PSP_GAMES.tsv",
      scrape_url = "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=61&recherche=",
      database_url = "https://nopaystation.com/tsv/PSP_GAMES.tsv",
      col_title_id  = "Title ID",           col_region = "Region", col_content_id = "Content ID",
      col_name = "Name",              col_pkg    = "PKG direct link",
      col_date = "Last Modification Date",       col_size   = "File Size",
      col_rap = "RAP" },
    { name = "PSV", file = "PSV_GAMES.tsv",
      scrape_url = "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=62&recherche=",
      database_url = "https://nopaystation.com/tsv/PSV_GAMES.tsv",
      col_title_id  = "Title ID",           col_region = "Region", col_content_id = "Content ID",
      col_name = "Name",              col_pkg    = "PKG direct link", col_zrif = "zRIF",
      col_date = "Last Modification Date",       col_size   = "File Size" },
    { name = "PS3", file = "PS3_GAMES.tsv",
      scrape_url = "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=59&recherche=",
      database_url = "https://nopaystation.com/tsv/PS3_GAMES.tsv",
      col_title_id  = "Title ID",           col_region = "Region", col_content_id = "Content ID",
      col_name = "Name",              col_pkg    = "PKG direct link",
      col_date = "Last Modification Date",       col_size   = "File Size",
      col_rap = "RAP" },
    { name = "PSM", file = "PSM_GAMES.tsv",
      scrape_url = "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=62&recherche=",
      database_url = "https://nopaystation.com/tsv/PSM_GAMES.tsv",
      col_title_id  = "Title ID",           col_region = "Region", col_content_id = "Content ID",
      col_name = "Name",              col_pkg    = "PKG direct link", col_zrif = "zRIF",
      col_date = "Last Modification Date",       col_size   = "File Size" },
}

local MAX_RESULTS = 99

-- Search results: { platform, id, region, name, date, size, pkg_url, rap }
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

local function search_platform(p, query, results)
    local filepath = TOOLS_DIR .. "/" .. p.file
    local ok, iter = pcall(ftcsv.parseLine, filepath, "\t")
    if not ok then
        ludo.logInfo("PS Downloader: cannot open " .. filepath .. ": " .. tostring(iter))
        return
    end
    local q = query:lower()
    for _, row in iter do
        if #results >= MAX_RESULTS then break end
        local name = trim(row[p.col_name])
        local title_id   = trim(row[p.col_title_id])
        local pkg  = trim(row[p.col_pkg])
        if pkg ~= "" and pkg ~= "MISSING" and
           (name:lower():find(q, 1, true) or title_id:lower():find(q, 1, true)) then
            local rap = nil
            if p.col_rap then
                local rv = trim(row[p.col_rap] or "")
                if rv ~= "" and rv ~= "MISSING" and #rv == 32 then
                    rap = rv
                end
            end
            local zrif = nil
            if p.col_zrif then
                local zv = trim(row[p.col_zrif] or "")
                if zv ~= "" and zv ~= "MISSING" and #zv == 48 then
                    zrif = zv
                end
            end
            table.insert(results, {
                platform = p.name,
                title_id = title_id,
                region   = trim(row[p.col_region]),
                name     = name,
                date     = trim(row[p.col_date]):sub(1, 10),
                size     = trim(row[p.col_size]),
                pkg_url  = pkg,
                rap      = rap,
            })
        end
    end
end

-- Table model column indices (0-based, matches AppendTextColumn order).
local COL_NUM      = 0
local COL_PLATFORM = 1
local COL_ID       = 2
local COL_REGION   = 3
local COL_SIZE     = 4
local COL_NAME     = 5

local handler = {
    NumColumns  = function(m) return 6 end,
    ColumnType  = function(m, col) return ui.TableValueTypeString end,
    NumRows     = function(m) return #search_results end,
    CellValue   = function(m, row, col)
        local r = search_results[row + 1]   -- model rows are 0-based
        if not r then return "" end
        if     col == COL_NUM      then return tostring(row + 1)
        elseif col == COL_PLATFORM then return r.platform or ""
        elseif col == COL_ID       then return r.title_id or ""
        elseif col == COL_REGION   then return r.region   or ""
        elseif col == COL_SIZE     then return format_size(r.size)
        elseif col == COL_NAME     then return r.name     or ""
        end
        return ""
    end,
    SetCellValue = function(m, row, col, val) end,  -- read-only
}

local model = ui.NewTableModel(handler)

-- UI construction

local win = ui.NewWindow("PlayStation Games Downloader", 860, 600, false)
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
    cb:SetChecked(1)
    plat_cbs[p.name] = cb
    plat_box:Append(cb, false)

    local is_database_exist = io.open(TOOLS_DIR .. "/" .. p.file, "r")
    if not is_database_exist then
        -- If the local TSV file doesn't exist, attempt to download it from the database_url.
        http.get_async(p.database_url, { timeout = 30 }, function(body, status, headers)
            if status == 200 or status == 206 then
                local path = TOOLS_DIR .. "/" .. p.file
                local f = io.open(path, "wb")
                if f then
                    f:write(body)
                    f:close()
                    ludo.logSuccess("Downloaded " .. p.file)
                else
                    ludo.logError("Failed to save " .. p.file .. " to disk.")
                end
            else
                ludo.logError("Failed to download " .. p.file .. ": HTTP " .. tostring(status))
            end
        end)
    else
        is_database_exist:close()
    end
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
local status_lbl = ui.NewLabel("Select platforms, type a game name or Title ID, then click Search.")
root:Append(status_lbl, false)

-- Results table (stretchy — takes remaining vertical space)
root:Append(ui.NewLabel("Results (up to " .. MAX_RESULTS .. "):"), false)
local results_tbl = ui.NewTable(model)
results_tbl:AppendTextColumn("#",        COL_NUM,      ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Platform", COL_PLATFORM, ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Title ID", COL_ID,       ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Region",   COL_REGION,   ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Size",     COL_SIZE,     ui.TableModelColumnNeverEditable)
results_tbl:AppendTextColumn("Name",     COL_NAME,     ui.TableModelColumnNeverEditable)
results_tbl:SetSelectionMode(ui.TableSelectionModeZeroOrOne)
results_tbl:ColumnSetWidth(0, 40)
results_tbl:ColumnSetWidth(1, 80)
results_tbl:ColumnSetWidth(2, 110)
results_tbl:ColumnSetWidth(3, 60)
results_tbl:ColumnSetWidth(4, 180)
root:Append(results_tbl, true)

-- Game details group
local det_group = ui.NewGroup("Rom Details")
det_group:SetMargined(1)
local det_box = ui.NewHorizontalBox()
det_box:SetPadded(1)
local entry_info = ui.NewMultilineEntry()
entry_info:SetReadOnly(1)
det_box:Append(entry_info, true)
local ss_img = ui.NewStaticImage()
det_box:Append(ss_img, true)
det_group:SetChild(det_box)
root:Append(det_group, true)

-- Download button
local dl_btn = ui.NewButton("  Download Selected  ")
root:Append(dl_btn, false)

win:SetChild(root)

-- Callbacks
local current_details_id = nil
local function update_details(idx)
    if not idx or idx < 1 or idx > #search_results then
        entry_info:SetText("")
        ss_img:Clear()
        current_details_id = nil
        return
    end
    local r = search_results[idx]
    current_details_id = r.title_id
    ss_img:Clear()
    local scrape_url = nil
    for _, p in ipairs(PLATFORMS) do
        if p.name == r.platform then
            scrape_url = p.scrape_url .. r.name:gsub(" ", "+")
            break
        end
    end
    if scrape_url then
        entry_info:SetText("Loading details...")
        ludo.logInfo("Fetching details for " .. r.title_id .. " from " .. scrape_url)
        http.get_async(scrape_url, { timeout = 30 }, function(body, status)
            if not window_alive or current_details_id ~= r.title_id then return end
            if status ~= 200 or not body or #body == 0 then
                entry_info:SetText("Failed to fetch details (HTTP " .. tostring(status) .. ")")
                ludo.logInfo("Failed to fetch details for " .. r.title_id .. " (HTTP " .. tostring(status) .. ")")
                return
            end
            local ok, data = pcall(json.decode, body)
            if not ok or not data or not data.response or not data.response.jeux or #data.response.jeux == 0 then
                entry_info:SetText("Failed to parse response.")
                ludo.logInfo("Invalid JSON response for " .. r.title_id .. ": " .. body)
                return
            end
            local jeu = data.response.jeux[1]
            local lines = {}
            local names = jeu.noms
            if names and #names > 0 then
                local name_text = ""
                for _, n in ipairs(names) do
                    if n.region == "en" or n.region == "ss" or n.region == "wor" then
                        name_text = n.text; break
                    end
                end
                if name_text == "" then name_text = names[1].text end
                table.insert(lines, "Name: " .. name_text)
            end
            if jeu.developpeur and jeu.developpeur.text ~= "" then
                table.insert(lines, "Developer: " .. jeu.developpeur.text)
            end
            if jeu.editeur and jeu.editeur.text ~= "" then
                table.insert(lines, "Publisher: " .. jeu.editeur.text)
            end
            if jeu.dates and #jeu.dates > 0 and jeu.dates[1].text ~= "" then
                table.insert(lines, "Year: " .. jeu.dates[1].text)
            end
            if jeu.genres and #jeu.genres > 0 then
                for _, g in ipairs(jeu.genres) do
                    if g.noms and g.principale == "1" then
                        for _, n in ipairs(g.noms) do
                            if n.langue == "en" then
                                table.insert(lines, "Genre: " .. n.text); break
                            end
                        end
                        break
                    end
                end
            end
            if jeu.joueurs and jeu.joueurs.text ~= "" then
                table.insert(lines, "Players: " .. jeu.joueurs.text)
            end
            if jeu.note and jeu.note.text ~= "" then
                table.insert(lines, "Rating: " .. jeu.note.text .. "/20")
            end
            if jeu.resolution and jeu.resolution ~= "" then
                table.insert(lines, "Resolution: " .. jeu.resolution)
            end
            if jeu.synopsis and #jeu.synopsis > 0 then
                local synopsis = ""
                for _, s in ipairs(jeu.synopsis) do
                    if s.langue == "en" then synopsis = s.text; break end
                end
                if synopsis == "" then synopsis = jeu.synopsis[1].text end
                table.insert(lines, "")
                table.insert(lines, "Synopsis:")
                -- replace &quot; with actual quotes, and trim whitespace
                synopsis = synopsis:gsub("&quot;", '"')
                table.insert(lines, synopsis:match("^%s*(.-)%s*$"))
            end
            local ss_url = nil
            if jeu.medias and #jeu.medias > 0 then
                for _, m in ipairs(jeu.medias) do
                    if m.type == "ss" and m.url and m.url ~= "" then
                        ss_url = m.url; break
                    end
                end
            end
            entry_info:SetText(table.concat(lines, "\n"))
            if ss_url then
                http.get_async(ss_url, { timeout = 30 }, function(img_body, img_status)
                    if not window_alive or current_details_id ~= r.title_id then return end
                    if img_status ~= 200 or not img_body or #img_body == 0 then return end
                    ss_img:SetImageFromMemory(img_body)
                end)
            end
        end)
    else
        entry_info:SetText("Details not available.")
    end
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
    local _, pkg_status, pkg_out = ludo.newDownload(
        r.pkg_url, outdir, ludo.DOWNLOAD_NOW, r.title_id .. ".pkg")
    if pkg_status == 200 or pkg_status == 206 or pkg_status == 0 then
        ludo.logSuccess("Queued PKG: " .. (pkg_out or (r.title_id .. ".pkg")))
    else
        ludo.logError("PKG preflight HTTP " .. tostring(pkg_status) .. " for " .. r.title_id)
        ui.MsgBoxError(win, "Download Failed",
            "Preflight check returned HTTP " .. tostring(pkg_status) .. " for\n" .. r.name)
        return  -- Don't close window on error
    end

    -- Generate binary RAP (content_id.rap) file if present (PS3/PSP).
    if r.rap then
        local rap_path = outdir .. "/" .. r.content_id .. ".rap"
        -- open as binary to avoid newline translation on Windows, which would corrupt the RAP file
        local rap_file = io.open(rap_path, "wb")
        if rap_file then
            rap_file:write(ascii_to_hex(r.rap))
            rap_file:close()
            ludo.logSuccess("Generated RAP: " .. rap_path)
        else
            ludo.logError("Failed to write RAP file: " .. rap_path)
        end
    end

    status_lbl:SetText("Download queued: " .. r.name)

    -- Close the window: win:Close() does not exist in libuilua.
    -- Set win_open=false to exit the MainStep loop, then Destroy the window.
    window_alive = false
    win_open = false
    win:Destroy()
end, nil)

-- Show window and drive a nested event loop until the window is closed.
-- Tool scripts must NOT call ui.Main()/ui.Uninit() — Ludo's own event loop
-- is already running.  Use MainStep() instead.
win:OnClosing(function(w, data)
    window_alive = false
    win_open = false
    return 1
end, nil)
win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
