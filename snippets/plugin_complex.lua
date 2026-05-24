-- plugin_complex.lua — template for a complex multi-strategy plugin
-- Pattern: page scraping for API credentials, JSON POST API, multi-client
--          fallback chain, format scoring, preflight probes, cookie auth.
-- Example: YouTube, Instagram, TikTok, Facebook, Twitch

local plugin = { name = "ComplexSite", version = "20260501", creator = "Your Name" }
local json = json or require("json")
local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

-- ---- helpers ----------------------------------------------------------

local function safe_name(s)
    if type(s) ~= "string" then return "file" end
    return s:gsub("[^%w%-_]", "_"):sub(1, 80)
end

local function trim(s)
    return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Decode a JSON string, normalising common response prefixes.
local function decode_json(text)
    text = tostring(text or "")
    -- Strip UTF-8 BOM
    if text:sub(1, 3) == "\239\187\191" then text = text:sub(4) end
    text = trim(text)
    -- Strip json vulnerability prefix
    if text:sub(1, 4) == ")]}'" then text = trim(text:sub(5)) end
    local ok, data = pcall(json.decode, text)
    if not ok then return nil, data end
    return data
end

-- Parse a query string into a key-value table.
local function parse_query(query)
    local t = {}
    for pair in (query or ""):gmatch("([^&]+)") do
        local k, v = pair:match("^([^=]+)=?(.*)$")
        if k and k ~= "" then
            t[http.url_decode(k)] = http.url_decode(v or "")
        end
    end
    return t
end

-- Scan text for the first '{' after a pattern match, then find the matching
-- '}' with proper brace/string-depth tracking and return the decoded table.
local function extract_json_object(text, pattern)
    local _, pe = text:find(pattern)
    if not pe then return nil end
    local start = text:find("{", pe + 1)
    if not start then return nil end
    local depth, in_str, esc = 0, false, false
    for i = start, #text do
        local c = text:sub(i, i)
        if esc then esc = false
        elseif c == "\\" and in_str then esc = true
        elseif c == '"' then in_str = not in_str
        elseif not in_str then
            if c == "{" then depth = depth + 1
            elseif c == "}" then
                depth = depth - 1
                if depth == 0 then
                    local ok, val = pcall(json.decode, text:sub(start, i))
                    if ok then return val end
                    return nil
                end
            end
        end
    end
    return nil
end

-- Cookie helper: load cookies from <OutputDirectory>/cookie_file_name
-- into the HTTP session if the file exists.
local function setup_cookies(cookie_file_name)
    local cookie_path = ludo.getOutputDirectory() .. "/" .. cookie_file_name
    local ck = io.open(cookie_path, "r")
    if ck then
        ck:close()
        http.set_cookie(cookie_path)
        ludo.logInfo("Loaded cookies from " .. cookie_path)
        return cookie_path
    end
    return nil
end

-- ---- page scraping helpers --------------------------------------------

-- Fetch a page and extract values by regex keys.
local function fetch_page(url, opts)
    opts = opts or {}
    opts.user_agent = opts.user_agent or UA
    opts.timeout = opts.timeout or TIMEOUT
    opts.follow_redirects = true
    local body, status = http.get(url, opts)
    if status ~= 200 then
        return nil, "HTTP " .. tostring(status) .. " fetching " .. url
    end
    return body
end

-- Extract a quoted JSON string value by key from page HTML.
local function extract_page_value(page, key)
    return page:match('"' .. key .. '"%s*:%s*"([^\"]+)"')
end

-- ---- metadata extraction ----------------------------------------------

-- Given a player response from the API, find the best audio/video format
-- using a scoring system.
local function pick_best_format(streaming_data)
    if type(streaming_data) ~= "table" then
        return nil, "No streaming data"
    end

    local function score(f)
        local s = 0
        if f.audioQuality == "AUDIO_QUALITY_HIGH"  then s = s + 1000 end
        if f.audioQuality == "AUDIO_QUALITY_MEDIUM" then s = s + 500 end
        s = s + (tonumber(f.averageBitrate) or tonumber(f.bitrate) or 0)
        return s
    end

    local groups = { streaming_data.adaptiveFormats, streaming_data.formats }
    local best, best_score = nil, -1

    for _, group in ipairs(groups) do
        if type(group) == "table" then
            for _, f in ipairs(group) do
                if type(f) ~= "table" then goto next end
                if not f.url or not f.url:match("^https?://") then goto next end
                if f.signatureCipher or f.cipher then goto next end
                if f.drmFamilies then goto next end
                -- Skip formats with n-param (requires challenge solving)
                local qs = parse_query(http.parse_url(f.url).query)
                if qs.n and qs.n ~= "" then goto next end

                local s = score(f)
                if s > best_score then best, best_score = f, s end
                ::next::
            end
        end
    end

    if not best then return nil, "No downloadable formats found" end
    return best
end

-- Build an output filename from MIME type and title.
local function make_filename(title, fmt)
    local mime = tostring(fmt and fmt.mimeType or "")
    local ext = "mp4"
    if mime:match("^audio/webm") then ext = "webm"
    elseif mime:match("^audio/mp4") then ext = "m4a"
    elseif mime:match("^audio/ogg") then ext = "ogg" end
    local safe = tostring(title or "file")
    safe = safe:gsub('[\\/:*?"<>|]', "_")
    safe = trim(safe)
    if safe == "" then safe = "file" end
    return safe .. "." .. ext
end

-- ---- client configuration ---------------------------------------------

-- Multiple client profiles for API fallback chain.
local CLIENTS = {
    {
        id = "client_a",
        api_host  = "https://www.example.com",
        user_agent = "Mozilla/5.0 ExampleClient/1.0",
        headers = {
            ["Origin"]  = "https://www.example.com",
        },
    },
    {
        id = "client_b",
        api_host  = "https://www.example.com",
        user_agent = "ExampleApp/2.0 (Linux; Android 14)",
        headers = {},
    },
}

-- ---- main process -----------------------------------------------------

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    return url:match("^https?://www%.example%.com/") ~= nil
end

function plugin.process(url)
    -- Optional: load authentication cookies
    setup_cookies("complexsite_cookies.txt")

    -- Extract resource ID from URL
    local video_id = url:match("example%.com/([%w%-_]+)")
    if not video_id then
        ludo.logError("Could not extract resource ID")
        return
    end
    ludo.logInfo("Processing: " .. video_id)

    -- Step 1: scrape the page for API credentials
    local page, err = fetch_page(url)
    if not page then
        ludo.logError("Page fetch failed: " .. tostring(err))
        return
    end
    local api_key   = extract_page_value(page, "API_KEY")
    local api_token = extract_page_value(page, "API_TOKEN")
    if not api_key then
        -- Fallback: try extracting a JSON object from embedded script data
        local embed = extract_json_object(page, '<script id="data"[^>]*>')
        if embed and embed.api_key then api_key = embed.api_key end
    end
    if not api_key then
        ludo.logError("Could not extract API credentials from page")
        return
    end
    ludo.logInfo("API credentials found")

    -- Step 2: try each client in order until one succeeds
    local last_error
    for _, client in ipairs(CLIENTS) do
        ludo.logInfo("Trying client: " .. client.id)

        -- Build API request
        local endpoint = client.api_host .. "/api/v1/resolve"
            .. "?key=" .. http.url_encode(api_key)
        local request_body = json.encode({
            video_id = video_id,
            client   = client.id,
        })

        local headers = {}
        for k, v in pairs(client.headers or {}) do headers[k] = v end
        headers["Content-Type"] = "application/json"
        if api_token then headers["Authorization"] = "Bearer " .. api_token end

        local body, status = http.post(endpoint, request_body, {
            user_agent = client.user_agent,
            timeout    = TIMEOUT,
            headers    = headers,
        })

        if status ~= 200 then
            last_error = client.id .. ": HTTP " .. tostring(status)
            goto next_client
        end

        local data = decode_json(body)
        if not data then
            last_error = client.id .. ": JSON decode failed"
            goto next_client
        end
        if data.error then
            last_error = client.id .. ": " .. tostring(data.error)
            goto next_client
        end

        local title = data.title or ("video_" .. video_id)
        ludo.logInfo("Title: " .. title)

        -- Step 3: select the best format
        local fmt, fmt_err = pick_best_format(data.streaming_data)
        if not fmt then
            last_error = client.id .. ": " .. tostring(fmt_err)
            goto next_client
        end

        -- Step 4: preflight HEAD probe
        local _, head_status = http.head(fmt.url, {
            follow_redirects = true, timeout = TIMEOUT,
            headers = { Referer = url },
        })
        if head_status ~= 200 and head_status ~= 206 then
            last_error = client.id .. ": preflight HTTP " .. tostring(head_status)
            goto next_client
        end

        -- Step 5: queue the download
        local outdir = ludo.getOutputDirectory()
        local fname = make_filename(title, fmt)
        local _, dl_status, output = ludo.newDownload(
            fmt.url, outdir, ludo.DOWNLOAD_NOW, fname,
            { Referer = url })

        if dl_status == 200 or dl_status == 206 or dl_status == 0 then
            ludo.logSuccess("Queued → " .. (output or fname))
            return
        end

        last_error = client.id .. ": ludo queue HTTP " .. tostring(dl_status)

        ::next_client::
    end

    ludo.logError(last_error or "All clients failed")
end

return plugin
