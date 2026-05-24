-- HTTP Library Demo — interactive tool exploring all http.* functions
-- Run from Ludo: Tools > HTTP Demo (or via -s tools/http_demo.lua)
--
-- Tools must NOT call ui.Main()/ui.Uninit() — use MainStep() instead.

local ui = require("ui")
local win_open = true

-- ====================================================================
-- Helpers
-- ====================================================================

local function log(area, msg)
    area:Append("[" .. os.date("%H:%M:%S") .. "] " .. msg .. "\n")
end

local function clear(area)
    area:SetText("")
end

local function set_status(lbl, msg)
    lbl:SetText(msg)
end

-- ====================================================================
-- Tab 1: HTTP GET / HEAD / POST
-- ====================================================================

local request_tab = ui.NewVerticalBox()
request_tab:SetPadded(1)

-- URL row
local url_row = ui.NewHorizontalBox()
url_row:SetPadded(1)
url_row:Append(ui.NewLabel("URL:"), false)
local url_entry = ui.NewEntry()
url_entry:SetText("https://httpbin.org/get")
url_row:Append(url_entry, true)
request_tab:Append(url_row, false)

-- Options row
local opts_row = ui.NewHorizontalBox()
opts_row:SetPadded(1)
opts_row:Append(ui.NewLabel("Timeout:"), false)
local timeout_spin = ui.NewSpinbox(1, 120)
timeout_spin:SetValue(30)
opts_row:Append(timeout_spin, false)
opts_row:Append(ui.NewLabel("  Follow redirects:"), false)
local follow_cb = ui.NewCheckbox("")
follow_cb:SetChecked(1)
opts_row:Append(follow_cb, false)
request_tab:Append(opts_row, false)

-- Custom headers
local hdr_row = ui.NewHorizontalBox()
hdr_row:SetPadded(1)
hdr_row:Append(ui.NewLabel("Headers (k:v, one per line):"), false)
local hdr_entry = ui.NewEntry()
hdr_entry:SetText("")
hdr_row:Append(hdr_entry, true)
request_tab:Append(hdr_row, false)

-- Post body (shown only for POST)
local post_row = ui.NewHorizontalBox()
post_row:SetPadded(1)
post_row:Append(ui.NewLabel("POST body:"), false)
local post_entry = ui.NewEntry()
post_entry:SetText('{"hello": "world"}')
post_row:Append(post_entry, true)
request_tab:Append(post_row, false)

-- Action buttons
local btn_row = ui.NewHorizontalBox()
btn_row:SetPadded(1)
local get_btn = ui.NewButton("  GET  ")
local head_btn = ui.NewButton("  HEAD  ")
local post_btn = ui.NewButton("  POST  ")
btn_row:Append(get_btn, false)
btn_row:Append(head_btn, false)
btn_row:Append(post_btn, false)
request_tab:Append(btn_row, false)

-- Status
local req_status = ui.NewLabel("Click GET, HEAD, or POST to make a request.")
request_tab:Append(req_status, false)

-- Result area
request_tab:Append(ui.NewLabel("Response:"), false)
local req_output = ui.NewNonWrappingMultilineEntry()
req_output:SetReadOnly(1)
request_tab:Append(req_output, true)

local function build_options()
    local opts = {}
    opts.timeout = timeout_spin:Value()
    opts.follow_redirects = follow_cb:Checked() == 1
    local hdr_text = hdr_entry:Text()
    if hdr_text and hdr_text ~= "" then
        local hdrs = {}
        for line in hdr_text:gmatch("[^\n]+") do
            local k, v = line:match("^([^:]+):%s*(.+)")
            if k and v then
                hdrs[k] = v
            end
        end
        if next(hdrs) then
            opts.headers = hdrs
        end
    end
    return opts
end

local function do_request(method)
    local url = url_entry:Text()
    if url == "" then
        set_status(req_status, "Enter a URL first")
        return
    end
    local opts = build_options()
    local ok, body, status, headers = pcall(http[method], url, opts)
    if not ok then
        set_status(req_status, "Error: " .. tostring(body))
        log(req_output, "Error: " .. tostring(body))
        return
    end
    set_status(req_status, "HTTP " .. tostring(status))
    log(req_output, method:upper() .. " " .. url .. " → " .. tostring(status))
    if body and #body > 0 then
        log(req_output, body)
    end
end

get_btn:OnClicked(function() do_request("get") end, nil)
head_btn:OnClicked(function() do_request("head") end, nil)
post_btn:OnClicked(function()
    local url = url_entry:Text()
    if url == "" then set_status(req_status, "Enter a URL first"); return end
    local body = post_entry:Text()
    local opts = build_options()
    local ok, resp, status = pcall(http.post, url, body, opts)
    if not ok then
        set_status(req_status, "Error: " .. tostring(resp))
        log(req_output, "Error: " .. tostring(resp))
        return
    end
    set_status(req_status, "HTTP " .. tostring(status))
    log(req_output, "POST " .. url .. " → " .. tostring(status))
    if resp and #resp > 0 then log(req_output, resp) end
end, nil)

-- ====================================================================
-- Tab 2: Encoding / Hashing / URL utils
-- ====================================================================

local encode_tab = ui.NewVerticalBox()
encode_tab:SetPadded(1)

-- URL encode/decode
encode_tab:Append(ui.NewLabel("URL Encode / Decode:"), false)
local urlenc_row = ui.NewHorizontalBox()
urlenc_row:SetPadded(1)
local urlenc_in = ui.NewEntry()
urlenc_in:SetText("hello world & more=foo")
urlenc_row:Append(urlenc_in, true)
local urlenc_btn = ui.NewButton("  Encode  ")
urlenc_row:Append(urlenc_btn, false)
local urldec_btn = ui.NewButton("  Decode  ")
urlenc_row:Append(urldec_btn, false)
encode_tab:Append(urlenc_row, false)
local urlenc_out = ui.NewEntry()
urlenc_out:SetReadOnly(1)
encode_tab:Append(urlenc_out, false)

urlenc_btn:OnClicked(function()
    local result = http.url_encode(urlenc_in:Text())
    urlenc_out:SetText(result)
end, nil)
urldec_btn:OnClicked(function()
    local result = http.url_decode(urlenc_in:Text())
    urlenc_out:SetText(result)
end, nil)

-- Base64 encode/decode
encode_tab:Append(ui.NewHorizontalSeparator(), false)
encode_tab:Append(ui.NewLabel("Base64 Encode / Decode:"), false)
local b64_row = ui.NewHorizontalBox()
b64_row:SetPadded(1)
local b64_in = ui.NewEntry()
b64_in:SetText("hello world")
b64_row:Append(b64_in, true)
local b64enc_btn = ui.NewButton("  Encode  ")
b64_row:Append(b64enc_btn, false)
local b64dec_btn = ui.NewButton("  Decode  ")
b64_row:Append(b64dec_btn, false)
encode_tab:Append(b64_row, false)
local b64_out = ui.NewEntry()
b64_out:SetReadOnly(1)
encode_tab:Append(b64_out, false)

b64enc_btn:OnClicked(function()
    local result = http.base64_encode(b64_in:Text())
    b64_out:SetText(result)
end, nil)
b64dec_btn:OnClicked(function()
    local ok, result = pcall(http.base64_decode, b64_in:Text())
    b64_out:SetText(ok and result or "Invalid base64")
end, nil)

-- SHA-256
encode_tab:Append(ui.NewHorizontalSeparator(), false)
encode_tab:Append(ui.NewLabel("SHA-256 Hash:"), false)
local sha_row = ui.NewHorizontalBox()
sha_row:SetPadded(1)
local sha_in = ui.NewEntry()
sha_in:SetText("hello")
sha_row:Append(sha_in, true)
local sha_btn = ui.NewButton("  Hash  ")
sha_row:Append(sha_btn, false)
encode_tab:Append(sha_row, false)
local sha_out = ui.NewEntry()
sha_out:SetReadOnly(1)
encode_tab:Append(sha_out, false)

sha_btn:OnClicked(function()
    local digest = http.sha256(sha_in:Text())
    local hex = digest:gsub(".", function(c) return ("%02x"):format(c:byte()) end)
    sha_out:SetText(hex)
end, nil)

-- URL parse
encode_tab:Append(ui.NewHorizontalSeparator(), false)
encode_tab:Append(ui.NewLabel("Parse URL:"), false)
local parse_row = ui.NewHorizontalBox()
parse_row:SetPadded(1)
local parse_in = ui.NewEntry()
parse_in:SetText("https://user:pass@example.com:8080/path/to/page?key=val&foo=bar#hash")
parse_row:Append(parse_in, true)
local parse_btn = ui.NewButton("  Parse  ")
parse_row:Append(parse_btn, false)
encode_tab:Append(parse_row, false)
local parse_out = ui.NewNonWrappingMultilineEntry()
parse_out:SetReadOnly(1)
parse_out:SetText("Click Parse to see URL components")
encode_tab:Append(parse_out, true)

parse_btn:OnClicked(function()
    local parts = http.parse_url(parse_in:Text())
    local out = ""
    for _, k in ipairs({ "scheme", "host", "port", "path", "query" }) do
        out = out .. k .. ": " .. (parts[k] or "") .. "\n"
    end
    parse_out:SetText(out)
end, nil)

-- ====================================================================
-- Tab 3: Cookie management & last URL
-- ====================================================================

local cookie_tab = ui.NewVerticalBox()
cookie_tab:SetPadded(1)

cookie_tab:Append(ui.NewLabel("Cookie Jar Setup:"), false)
local cj_row = ui.NewHorizontalBox()
cj_row:SetPadded(1)
cj_row:Append(ui.NewLabel("Cookie file:"), false)
local cj_entry = ui.NewEntry()
cj_entry:SetText("http_demo_cookies.txt")
cj_row:Append(cj_entry, true)
local cj_browse_btn = ui.NewButton("  Browse...  ")
cj_row:Append(cj_browse_btn, false)
local cj_set_btn = ui.NewButton("  Set Cookie File  ")
cj_row:Append(cj_set_btn, false)
cookie_tab:Append(cj_row, false)

local cj_status = ui.NewLabel("Cookie jar not set. Click Browse to pick a file.")
cookie_tab:Append(cj_status, false)

cj_browse_btn:OnClicked(function()
    local path = ui.OpenFile(win)
    if path then
        cj_entry:SetText(path)
        http.set_cookie(path)
        set_status(cj_status, "Cookie jar set to: " .. path)
        log(cj_log, "Cookie file: " .. path)
    end
end, nil)

cj_set_btn:OnClicked(function()
    local path = cj_entry:Text()
    if path == "" then
        set_status(cj_status, "Enter a file path first")
        return
    end
    http.set_cookie(path)
    set_status(cj_status, "Cookie jar set to: " .. path)
    log(cj_log, "Cookie file: " .. path)
end, nil)

cookie_tab:Append(ui.NewHorizontalSeparator(), false)

-- Cookie fetch
cookie_tab:Append(ui.NewLabel("Fetch a URL with cookies (first sets cookies, second reads them):"), false)
local cf_row = ui.NewHorizontalBox()
cf_row:SetPadded(1)
cf_row:Append(ui.NewLabel("URL:"), false)
local cf_entry = ui.NewEntry()
cf_entry:SetText("https://httpbin.org/cookies/set?foo=bar&baz=qux")
cf_row:Append(cf_entry, true)
local cf_btn = ui.NewButton("  Fetch & Set Cookies  ")
cf_row:Append(cf_btn, false)
cookie_tab:Append(cf_row, false)

local cf2_row = ui.NewHorizontalBox()
cf2_row:SetPadded(1)
cf2_row:Append(ui.NewLabel("URL:"), false)
local cf2_entry = ui.NewEntry()
cf2_entry:SetText("https://httpbin.org/cookies")
cf2_row:Append(cf2_entry, true)
local cf2_btn = ui.NewButton("  Fetch (reads cookies)  ")
cf2_row:Append(cf2_btn, false)
cookie_tab:Append(cf2_row, false)

local cf_status = ui.NewLabel("");
cookie_tab:Append(cf_status, false)

cookie_tab:Append(ui.NewHorizontalSeparator(), false)

-- Read cookie from file
cookie_tab:Append(ui.NewLabel("Read a named cookie from the cookie file:"), false)
local rc_row = ui.NewHorizontalBox()
rc_row:SetPadded(1)
rc_row:Append(ui.NewLabel("Cookie name:"), false)
local rc_name = ui.NewEntry()
rc_name:SetText("foo")
rc_row:Append(rc_name, false)
local rc_btn = ui.NewButton("  Read  ")
rc_row:Append(rc_btn, false)
cookie_tab:Append(rc_row, false)
local rc_val = ui.NewEntry()
rc_val:SetReadOnly(1)
cookie_tab:Append(rc_val, false)

local cj_log = ui.NewNonWrappingMultilineEntry()
cj_log:SetReadOnly(1)
cookie_tab:Append(cj_log, true)

-- Clear cookies
local cj_clear_row = ui.NewHorizontalBox()
cj_clear_row:SetPadded(1)
local cj_clear_btn = ui.NewButton("  Clear Cookies  ")
cj_clear_row:Append(cj_clear_btn, false)
local last_url_btn = ui.NewButton("  Get Last URL  ")
cj_clear_row:Append(last_url_btn, false)
cookie_tab:Append(cj_clear_row, false)

-- Callbacks
cf_btn:OnClicked(function()
    local url = cf_entry:Text()
    if url == "" then return end
    local ok, body, status = pcall(http.get, url)
    if ok then
        set_status(cf_status, "HTTP " .. status .. " — cookies set by server")
        log(cj_log, "GET " .. url .. " → " .. status)
    else
        set_status(cf_status, "Error: " .. tostring(body))
    end
end, nil)

cf2_btn:OnClicked(function()
    local url = cf2_entry:Text()
    if url == "" then return end
    local ok, body, status = pcall(http.get, url)
    if ok then
        set_status(cf_status, "HTTP " .. status)
        log(cj_log, "GET " .. url .. " → " .. status)
        if body and #body > 0 then log(cj_log, body) end
    else
        set_status(cf_status, "Error: " .. tostring(body))
    end
end, nil)

rc_btn:OnClicked(function()
    local cfile = cj_entry:Text()
    local cname = rc_name:Text()
    if cfile == "" or cname == "" then return end
    local val = http.read_cookie(cfile, cname)
    if val then
        rc_val:SetText(val)
        log(cj_log, "Cookie '" .. cname .. "' = " .. val)
    else
        rc_val:SetText("(not found)")
        log(cj_log, "Cookie '" .. cname .. "' not found in " .. cfile)
    end
end, nil)

cj_clear_btn:OnClicked(function()
    http.clear_cookies()
    set_status(cj_status, "Cookies cleared")
    log(cj_log, "Cookie jar cleared")
end, nil)

last_url_btn:OnClicked(function()
    local url = http.get_last_url()
    cj_status:SetText("Last URL: " .. (url ~= "" and url or "(none)"))
end, nil)

-- ====================================================================
-- Tab 4: Async HTTP demo (GUI only)
-- ====================================================================

local async_tab = ui.NewVerticalBox()
async_tab:SetPadded(1)

async_tab:Append(ui.NewLabel("Async HTTP — requests run on a background thread."), false)

local aurl_row = ui.NewHorizontalBox()
aurl_row:SetPadded(1)
aurl_row:Append(ui.NewLabel("URL:"), false)
local aurl_entry = ui.NewEntry()
aurl_entry:SetText("https://httpbin.org/delay/2")
aurl_row:Append(aurl_entry, true)
local asend_btn = ui.NewButton("  Send Async  ")
aurl_row:Append(asend_btn, false)
async_tab:Append(aurl_row, false)

local aresult = ui.NewLabel("No async requests sent yet.")
async_tab:Append(aresult, false)

local alog = ui.NewNonWrappingMultilineEntry()
alog:SetReadOnly(1)
async_tab:Append(alog, true)

asend_btn:OnClicked(function()
    local url = aurl_entry:Text()
    if url == "" then return end
    log(alog, "Queuing async GET: " .. url)
    aresult:SetText("Request in progress… GUI stays responsive!")
    http.get_async(url, { timeout = 30 },
        function(body, status, headers)
            aresult:SetText("Async result: HTTP " .. tostring(status))
            log(alog, "Async GET " .. url .. " → " .. tostring(status))
            if body and #body > 0 then
                local preview = body:sub(1, 500)
                log(alog, preview)
            end
        end)
end, nil)

-- ====================================================================
-- Build tabs
-- ====================================================================

local tabs = ui.NewTab()
tabs:Append("Request", request_tab)
tabs:Append("Encode/Hash", encode_tab)
tabs:Append("Cookies", cookie_tab)
tabs:Append("Async", async_tab)

-- ====================================================================
-- Window + event loop
-- ====================================================================

local win = ui.NewWindow("HTTP Library Demo", 780, 620, false)
win:SetMargined(1)
win:SetChild(tabs)
win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
