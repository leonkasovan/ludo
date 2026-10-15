-- Microsoft Graph Demo — read-only explorer for the Microsoft Graph REST API
--
-- Graph exposes a single endpoint: https://graph.microsoft.com
--   v1.0 (GA) : https://graph.microsoft.com/v1.0/{resource}
--   beta      : https://graph.microsoft.com/beta/{resource}
--
-- Authentication
-- --------------
-- Every Graph call must carry a Bearer token. The simplest way to get one for
-- a demo is the Microsoft Graph Explorer:
--
--   1. Open https://developer.microsoft.com/en-us/graph/graph-explorer
--   2. Sign in with a work/school or personal Microsoft account
--   3. Click the "Access token" tab and copy the token
--   4. Paste it into the "Access token" box below, then use the tabs
--
-- Tokens are short-lived (~1 hour). When calls start returning HTTP 401,
-- copy a fresh token. The token box decodes the JWT on paste and shows the
-- user, scopes and expiry so you can see what it grants.
--
-- Scopes used here: User.Read (profile), Files.Read (OneDrive),
-- Mail.Read (mail), Calendars.Read (calendar). Graph Explorer's default
-- consent includes all of these.
--
-- Tools must NOT call ui.Init()/ui.Main()/ui.Uninit() — Ludo's own event loop
-- is already running. Use a ui.MainStep() loop instead.

local ui   = require("ui")
local json = json or require("json")

-- ====================================================================
-- Constants and state
-- ====================================================================

local GRAPH_HOST         = "https://graph.microsoft.com"
local GRAPH_EXPLORER_URL = "https://developer.microsoft.com/en-us/graph/graph-explorer"
local API_VERSIONS       = { "v1.0", "beta" }
local TIMEOUT            = 30

local win_open = true

-- Forward-declared widgets (assigned during UI construction below, but
-- captured as upvalues by the request helpers).
local token_entry, version_cb, token_status, status_lbl, raw_out

-- ====================================================================
-- Helpers
-- ====================================================================

-- Open a URL in the system default browser. libuilua exposes no uiOpenURL
-- binding, so this shells out the same way m3u8.lua does.
local function open_url(url)
    if package.config:sub(1, 1) == "\\" then     -- Windows
        os.execute('start "" "' .. url .. '"')
    else                                         -- macOS / Linux
        os.execute('open "' .. url .. '" 2>/dev/null || xdg-open "' .. url .. '" >/dev/null 2>&1')
    end
end

local function format_size(n)
    n = tonumber(n)
    if not n or n <= 0 then return "" end
    if n >= 1073741824 then return ("%.2f GB"):format(n / 1073741824) end
    if n >= 1048576    then return ("%.2f MB"):format(n / 1048576)    end
    if n >= 1024       then return ("%.2f KB"):format(n / 1024)       end
    return tostring(n) .. " B"
end

-- Graph returns ISO-8601 UTC, e.g. "2026-10-15T13:45:12Z".
local function fmt_date(s)
    if not s or s == "" then return "" end
    local t = s:gsub("T", " ")
    return t:sub(1, 19)
end

-- A table is treated as a JSON array when every key is numeric.
local function is_dense_array(t)
    local count = 0
    for k in pairs(t) do
        if type(k) ~= "number" then return false end
        count = count + 1
    end
    return count > 0
end

-- Pretty-print a decoded JSON value. Graph responses are nested, so raw
-- compact output is hard to read.
local function json_pretty(v, indent)
    indent = indent or ""

    if v == json.null then return "null" end

    local tv = type(v)
    if tv == "table" then
        local child = indent .. "  "
        local parts = {}
        if is_dense_array(v) then
            for i = 1, #v do
                parts[#parts + 1] = child .. json_pretty(v[i], child)
            end
            if #parts == 0 then return "[]" end
            return "[\n" .. table.concat(parts, ",\n") .. "\n" .. indent .. "]"
        end
        local keys = {}
        for k in pairs(v) do keys[#keys + 1] = k end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        for _, k in ipairs(keys) do
            parts[#parts + 1] = child .. '"' .. tostring(k) .. '": '
                .. json_pretty(v[k], child)
        end
        if #parts == 0 then return "{}" end
        return "{\n" .. table.concat(parts, ",\n") .. "\n" .. indent .. "}"
    elseif tv == "string" then
        return json.encode(v)
    end
    return tostring(v)
end

local function decode(body)
    if not body or body == "" then return nil end
    local ok, data = pcall(json.decode, body)
    if ok then return data end
    return nil
end

-- Build a query string, percent-encoding each name and value (so "$top",
-- " ", "|" etc. are all safe).
local function with_query(path, params)
    if not params or #params == 0 then return path end
    local parts = {}
    for _, kv in ipairs(params) do
        parts[#parts + 1] = http.url_encode(kv[1]) .. "=" .. http.url_encode(kv[2])
    end
    return path .. "?" .. table.concat(parts, "&")
end

local function graph_base()
    local idx = version_cb:Selected()
    return GRAPH_HOST .. "/" .. (API_VERSIONS[idx + 1] or "v1.0")
end

-- Perform a GET against Graph and render the response into the raw pane.
-- Returns: body (string|nil), status (number)
local function graph_request(path)
    local token = (token_entry:Text() or ""):gsub("%s+", "")
    if token == "" then
        status_lbl:SetText("No access token — paste one from Graph Explorer first.")
        return nil, 0
    end

    local url = graph_base() .. path
    status_lbl:SetText("GET " .. path .. " ...")

    local ok, body, status = pcall(http.get, url, {
        timeout = TIMEOUT,
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Accept"]        = "application/json",
        },
    })

    if not ok then
        status_lbl:SetText("Request error: " .. tostring(body))
        raw_out:SetText(tostring(body))
        return nil, 0
    end

    status_lbl:SetText(("HTTP %s  |  GET %s"):format(tostring(status), path))
    local data = decode(body)
    if data then
        raw_out:SetText(json_pretty(data))
    else
        raw_out:SetText((body and body ~= "") and body or "(empty response)")
    end
    return body, status
end

-- ====================================================================
-- Generic results table (model-view pattern)
-- ====================================================================

-- col_specs: array of { name = <header>, width = <px|nil>, value = fn(record) }
-- Returns the Table widget and a set_rows(new_rows) function.
local function build_results_tab(col_specs)
    local rows = {}

    local handler = {
        NumColumns   = function(m) return #col_specs end,
        ColumnType   = function(m, col) return ui.TableValueTypeString end,
        NumRows      = function(m) return #rows end,
        CellValue    = function(m, row, col)
            local rec = rows[row + 1]
            if not rec then return "" end
            local v = col_specs[col + 1].value(rec)
            if v == nil then return "" end
            return tostring(v)
        end,
        SetCellValue = function(m, row, col, val) end,
    }

    local model = ui.NewTableModel(handler)
    local tbl   = ui.NewTable(model)
    for i, cs in ipairs(col_specs) do
        tbl:AppendTextColumn(cs.name, i - 1, ui.TableModelColumnNeverEditable)
        if cs.width then tbl:ColumnSetWidth(i - 1, cs.width) end
    end
    tbl:SetSelectionMode(ui.TableSelectionModeZeroOrOne)

    local function set_rows(new_rows)
        for i = #rows, 1, -1 do
            table.remove(rows, i)
            model:RowDeleted(i - 1)
        end
        for i = 1, #(new_rows or {}) do
            rows[i] = new_rows[i]
            model:RowInserted(i - 1)
        end
    end

    return tbl, set_rows
end

-- ====================================================================
-- Tab 1 — Profile  (GET /me, /me/manager, /me/photo/$value)
-- ====================================================================

local profile_out = ui.NewMultilineEntry()
profile_out:SetReadOnly(1)

local profile_btns = ui.NewHorizontalBox()
profile_btns:SetPadded(1)

local function load_profile()
    local body, status = graph_request("/me")
    if status ~= 200 then return end
    local d = decode(body)
    if not d then return end
    local lines = {}
    local function add(k, v) lines[#lines + 1] = k .. ": " .. tostring(v or "") end
    add("Display name", d.displayName)
    add("User principal name", d.userPrincipalName)
    add("Mail", d.mail)
    add("Job title", d.jobTitle)
    add("Office", d.officeLocation)
    add("Id", d.id)
    profile_out:SetText(table.concat(lines, "\n"))
end

local function load_manager()
    local body, status = graph_request("/me/manager")
    if status ~= 200 then return end
    local d = decode(body)
    if not d then return end
    profile_out:SetText("Manager: " .. tostring(d.displayName or "")
        .. "\nTitle: " .. tostring(d.jobTitle or ""))
end

local function load_photo()
    -- $value streams the raw JPEG bytes — demonstrates a non-JSON endpoint.
    local token = (token_entry:Text() or ""):gsub("%s+", "")
    if token == "" then
        status_lbl:SetText("No access token — paste one from Graph Explorer first.")
        return
    end
    local url = graph_base() .. "/me/photo/$value"
    local ok, body, status, headers = pcall(http.get, url, {
        timeout = TIMEOUT,
        headers = { ["Authorization"] = "Bearer " .. token },
    })
    if not ok then
        status_lbl:SetText("Request error: " .. tostring(body))
        return
    end
    local ctype = headers and (headers["Content-Type"] or headers["content-type"]) or "?"
    profile_out:SetText(("HTTP %s\nContent-Type: %s\nBytes: %d")
        :format(tostring(status), ctype, body and #body or 0))
end

local btn_me      = ui.NewButton("  GET /me  ")
local btn_manager = ui.NewButton("  GET /me/manager  ")
local btn_photo   = ui.NewButton("  GET /me/photo/$value  ")
btn_me:OnClicked(function() load_profile() end, nil)
btn_manager:OnClicked(function() load_manager() end, nil)
btn_photo:OnClicked(function() load_photo() end, nil)
profile_btns:Append(btn_me, false)
profile_btns:Append(btn_manager, false)
profile_btns:Append(btn_photo, false)

local profile_tab = ui.NewVerticalBox()
profile_tab:SetPadded(1)
profile_tab:Append(profile_btns, false)
profile_tab:Append(ui.NewLabel("Summary:"), false)
profile_tab:Append(profile_out, true)

-- ====================================================================
-- Tab 2 — OneDrive  (GET /me/drive/root/children)
-- ====================================================================

local onedrive_tbl, onedrive_set_rows = build_results_tab({
    { name = "Type",     width = 70,  value = function(r)
        return r.folder and "Folder" or (r.file and "File" or "") end },
    { name = "Name",     width = 380, value = function(r) return r.name end },
    { name = "Size",     width = 100, value = function(r)
        return r.folder and "" or format_size(r.size) end },
    { name = "Modified", width = 160, value = function(r)
        return fmt_date(r.lastModifiedDateTime) end },
})

local function load_onedrive()
    local path = with_query("/me/drive/root/children", {
        { "$top",    "100" },
        { "$select", "name,size,folder,file,lastModifiedDateTime" },
    })
    local body, status = graph_request(path)
    if status ~= 200 then return end
    local d = decode(body)
    local items = (d and d.value) or {}
    onedrive_set_rows(items)
    status_lbl:SetText(("OneDrive root: %d item(s)"):format(#items))
end

local onedrive_btns = ui.NewHorizontalBox()
onedrive_btns:SetPadded(1)
local btn_onedrive = ui.NewButton("  Refresh /me/drive/root/children  ")
btn_onedrive:OnClicked(function() load_onedrive() end, nil)
onedrive_btns:Append(btn_onedrive, false)

local onedrive_tab = ui.NewVerticalBox()
onedrive_tab:SetPadded(1)
onedrive_tab:Append(onedrive_btns, false)
onedrive_tab:Append(onedrive_tbl, true)

-- ====================================================================
-- Tab 3 — Mail  (GET /me/messages)
-- ====================================================================

local mail_tbl, mail_set_rows = build_results_tab({
    { name = "Read",    width = 60,  value = function(r)
        return r.isRead and "read" or "new" end },
    { name = "From",    width = 200, value = function(r)
        return r.from and r.from.emailAddress and r.from.emailAddress.address end },
    { name = "Subject", width = 340, value = function(r) return r.subject end },
    { name = "Received", width = 160, value = function(r)
        return fmt_date(r.receivedDateTime) end },
})

local function load_mail()
    local path = with_query("/me/messages", {
        { "$top",     "25" },
        { "$orderby", "receivedDateTime desc" },
        { "$select",  "subject,from,receivedDateTime,isRead" },
    })
    local body, status = graph_request(path)
    if status ~= 200 then return end
    local d = decode(body)
    local items = (d and d.value) or {}
    mail_set_rows(items)
    status_lbl:SetText(("Mailbox: %d message(s)"):format(#items))
end

local mail_btns = ui.NewHorizontalBox()
mail_btns:SetPadded(1)
local btn_mail = ui.NewButton("  Refresh /me/messages  ")
btn_mail:OnClicked(function() load_mail() end, nil)
mail_btns:Append(btn_mail, false)

local mail_tab = ui.NewVerticalBox()
mail_tab:SetPadded(1)
mail_tab:Append(mail_btns, false)
mail_tab:Append(mail_tbl, true)

-- ====================================================================
-- Tab 4 — Calendar  (GET /me/events)
-- ====================================================================

local cal_tbl, cal_set_rows = build_results_tab({
    { name = "Subject",  width = 240, value = function(r) return r.subject end },
    { name = "Start",    width = 150, value = function(r)
        return fmt_date(r.start and r.start.dateTime) end },
    { name = "End",      width = 150, value = function(r)
        return fmt_date(r["end"] and r["end"].dateTime) end },
    { name = "Organizer", width = 180, value = function(r)
        return r.organizer and r.organizer.emailAddress and r.organizer.emailAddress.name end },
    { name = "Location", width = 140, value = function(r)
        return r.location and r.location.displayName end },
})

local function load_calendar()
    local path = with_query("/me/events", {
        { "$top",     "25" },
        { "$orderby", "start/dateTime" },
        { "$select",  "subject,start,end,organizer,location" },
    })
    local body, status = graph_request(path)
    if status ~= 200 then return end
    local d = decode(body)
    local items = (d and d.value) or {}
    cal_set_rows(items)
    status_lbl:SetText(("Calendar: %d event(s)"):format(#items))
end

local cal_btns = ui.NewHorizontalBox()
cal_btns:SetPadded(1)
local btn_cal = ui.NewButton("  Refresh /me/events  ")
btn_cal:OnClicked(function() load_calendar() end, nil)
cal_btns:Append(btn_cal, false)

local calendar_tab = ui.NewVerticalBox()
calendar_tab:SetPadded(1)
calendar_tab:Append(cal_btns, false)
calendar_tab:Append(cal_tbl, true)

-- ====================================================================
-- Tab 5 — Raw JSON / free-form GET
-- ====================================================================

local raw_tab = ui.NewVerticalBox()
raw_tab:SetPadded(1)

local raw_req_row = ui.NewHorizontalBox()
raw_req_row:SetPadded(1)
raw_req_row:Append(ui.NewLabel("Path:"), false)
local raw_path = ui.NewEntry()
raw_path:SetText("/me")
raw_req_row:Append(raw_path, true)
local btn_raw_send = ui.NewButton("  Send GET  ")
raw_req_row:Append(btn_raw_send, false)
raw_tab:Append(raw_req_row, false)

raw_tab:Append(ui.NewLabel("Raw response (the last request from any tab lands here):"), false)
raw_out = ui.NewNonWrappingMultilineEntry()
raw_out:SetReadOnly(1)
raw_tab:Append(raw_out, true)

local function send_raw()
    local p = raw_path:Text() or ""
    if p == "" then p = "/me" end
    if p:sub(1, 1) ~= "/" then p = "/" .. p end
    graph_request(p)
end

btn_raw_send:OnClicked(function() send_raw() end, nil)
raw_path:OnEnter(function() send_raw() end, nil)

-- ====================================================================
-- Token decode (JWT payload → user / scopes / expiry)
-- ====================================================================

local function decode_token()
    local token = (token_entry:Text() or ""):gsub("%s+", "")
    if token == "" then
        token_status:SetText("Paste a token from Graph Explorer (link above), then use the tabs.")
        return
    end
    -- JWT = header.payload.signature (all base64url)
    local payload = token:match("^[^.]+%.([^.]+)%.")
    if not payload then
        token_status:SetText("Not a JWT (opaque token?).")
        return
    end
    -- base64url → base64, then pad to a multiple of 4
    local b64 = payload:gsub("%-", "+"):gsub("_", "/")
    b64 = b64 .. string.rep("=", (4 - (#b64 % 4)) % 4)

    local ok, raw = pcall(http.base64_decode, b64)
    if not ok then
        token_status:SetText("base64 decode failed.")
        return
    end
    local ok2, claims = pcall(json.decode, raw)
    if not ok2 then
        token_status:SetText("Could not parse token claims.")
        return
    end

    local user = claims.upn or claims.unique_name or claims.preferred_username
        or claims.app_displayname or "?"
    local scopes = claims.scp or claims.roles or "-"
    local exp = claims.exp and os.date("%Y-%m-%d %H:%M:%S", claims.exp) or "?"
    token_status:SetText(("user=%s  |  scopes=%s  |  expires=%s")
        :format(tostring(user), tostring(scopes), tostring(exp)))
end

-- ====================================================================
-- Build the window
-- ====================================================================

local root = ui.NewVerticalBox()
root:SetPadded(1)

local auth_row = ui.NewHorizontalBox()
auth_row:SetPadded(1)
auth_row:Append(ui.NewLabel("Access token:"), false)
token_entry = ui.NewEntry()
auth_row:Append(token_entry, true)
root:Append(auth_row, false)

local explorer_row = ui.NewHorizontalBox()
explorer_row:SetPadded(1)
explorer_row:Append(ui.NewLabel("Get a token:"), false)
local explorer_entry = ui.NewEntry()
explorer_entry:SetText(GRAPH_EXPLORER_URL)
explorer_entry:SetReadOnly(1)
explorer_row:Append(explorer_entry, true)
local btn_explorer = ui.NewButton("  Open  ")
explorer_row:Append(btn_explorer, false)
root:Append(explorer_row, false)

local ver_row = ui.NewHorizontalBox()
ver_row:SetPadded(1)
ver_row:Append(ui.NewLabel("API version:"), false)
version_cb = ui.NewCombobox()
version_cb:Append("v1.0", "beta")
version_cb:SetSelected(0)
ver_row:Append(version_cb, false)
token_status = ui.NewLabel("Paste a token from Graph Explorer (link above), then use the tabs.")
ver_row:Append(token_status, true)
root:Append(ver_row, false)

local tabs = ui.NewTab()
tabs:Append("Profile",   profile_tab)
tabs:Append("OneDrive",  onedrive_tab)
tabs:Append("Mail",      mail_tab)
tabs:Append("Calendar",  calendar_tab)
tabs:Append("Raw JSON",  raw_tab)
root:Append(tabs, true)

status_lbl = ui.NewLabel("Ready.")
root:Append(status_lbl, false)

local win = ui.NewWindow("Microsoft Graph Demo", 920, 640, false)
win:SetMargined(1)
win:SetChild(root)

token_entry:OnChanged(function() decode_token() end, nil)
btn_explorer:OnClicked(function() open_url(GRAPH_EXPLORER_URL) end, nil)

win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
token_entry:SetFocus()

-- Nested event loop — do NOT call ui.Main()/ui.Uninit() from a tool script.
while win_open do
    if ui.MainStep(true) == 0 then break end
end
