-- tube8.lua
-- Ludo plugin for downloading Tube8 videos.
-- Ported from yt-dlp tube8.py extractor.
-- Handles: www.tube8.com/CATEGORY/TITLE/ID

local plugin = { name = "Tube8", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Extract JSON from a `var flashvars = {...}` or `flashvars = {...}` assignment.
-- Searches from end of HTML for the last occurrence (most likely the right one).
local function extract_flashvars(html)
    -- Find the last occurrence of "flashvars = {" assignement
    local last_idx = 0
    local start = 1
    while true do
        local s, e = html:find("flashvars%s*=", start)
        if not s then break end
        last_idx = s
        start = e + 1
    end
    if last_idx == 0 then return nil end
    -- Find the JSON object opening brace
    local _, eq = html:find("=", last_idx)
    local obj_start = html:find("{", eq + 1)
    if not obj_start then return nil end
    local depth, in_str, escaped = 0, false, false
    for i = obj_start, #html do
        local c = html:sub(i, i)
        if escaped then escaped = false
        elseif c == "\\" and in_str then escaped = true
        elseif c == '"' then in_str = not in_str
        elseif not in_str then
            if c == "{" then depth = depth + 1
            elseif c == "}" then
                depth = depth - 1
                if depth == 0 then
                    local ok, val = pcall(json.decode, html:sub(obj_start, i))
                    if ok then return val end
                    return nil
                end
            end
        end
    end
    return nil
end

-- Fallback: extract individual flashvars fields via regex
local function extract_regex_fallback(html, key)
    -- Try various formats: flashvars.key = "value", flashvars[key] = "value"
    local v = html:match('flashvars%.' .. key .. '%s*=%s*["\']([^"\']+)')
    if v then return v end
    v = html:match('flashvars%["' .. key .. '"%]%s*=%s*["\']([^"\']+)')
    return v
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://tube8%.com/") then return true end
    if url:match("^https?://www%.tube8%.com/") then return true end
    return false
end

function plugin.process(url)
    local video_id = url:match("tube8%.com/.+/(%d+)/?$")
    if not video_id then
        ludo.logError("Tube8: could not extract video ID")
        return
    end

    ludo.logInfo("Tube8: fetching page for " .. video_id)

    -- Fetch with age_verified cookie to bypass age gate
    local ok_req, body, status = pcall(http.get, url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { Cookie = "age_verified=1" },
    })
    if not ok_req then
        ludo.logInfo("Tube8: request failed: " .. tostring(body))
        ludo.logInfo("Tube8: this site may be blocked by your ISP or DNS")
        return
    end
    if status ~= 200 then
        ludo.logError("Tube8: failed to fetch page (HTTP " .. tostring(status) .. ")")
        return
    end

    -- Check if the video page was blocked by ISP
    if #body < 2000 then
        ludo.logError("Tube8: page appears to be blocked by your ISP or DNS")
        ludo.logInfo("Tube8: try using a different DNS or proxy/VPN")
        return
    end

    -- Check if video is unavailable
    if body:find('title="This video is no longer available"') then
        ludo.logError("Tube8: video " .. video_id .. " is no longer available")
        return
    end

    -- Extract flashvars JSON for structured extraction
    local flashvars = extract_flashvars(body)

    -- Extract title (try flashvars, then HTML regex)
    local title
    if flashvars and flashvars.video_title then
        title = flashvars.video_title
    else
        title = body:match("<h1[^>]*>([^<]+)")
            or extract_regex_fallback(body, "video_title")
            or ("Tube8 " .. video_id)
    end

    local thumbnail
    if flashvars and flashvars.image_url then
        thumbnail = flashvars.image_url
    end

    local duration
    if flashvars and flashvars.video_duration then
        duration = tonumber(flashvars.video_duration)
    end

    ludo.logInfo("Tube8: \"" .. title .. "\"" .. (duration and (" (" .. tostring(duration) .. "s)") or ""))

    -- Collect format URLs from all available sources
    local formats, format_urls = {}, {}

    local function add_format(url_val, height, format_id)
        local decoded = url_val
        if not decoded:match("^https?://") then
            decoded = http.url_decode(decoded)
        end
        if not decoded or not decoded:match("^https?://") then return end
        if format_urls[decoded] then return end
        format_urls[decoded] = true
        local tbr = nil
        local tbr_s = decoded:match("[/_](%d+)[kK][/_]")
        if tbr_s then tbr = tonumber(tbr_s) end
        table.insert(formats, {
            url = decoded,
            format_id = format_id or (height and tostring(height) .. "p") or nil,
            height = height or 0,
            tbr = tbr,
        })
    end

    -- Source 1: flashvars quality_NNNp keys
    if flashvars then
        for key, value in pairs(flashvars) do
            if type(key) == "string" and type(value) == "string" then
                local h = key:match("quality_(%d+)[pP]")
                if h then
                    add_format(value, tonumber(h), tostring(tonumber(h)) .. "p")
                end
            end
        end
    end

    -- Source 2: flashvars.video_url
    local video_url = flashvars and flashvars.video_url
    if video_url and type(video_url) == "string" then
        local ext = (video_url:match("%.(%w+)$") or ""):lower()
        if ext == "mp4" or ext == "flv" or ext == "webm" or ext == "" then
            add_format(video_url)
        end
    end

    -- Source 3: regex fallback for flashvars.video_url (encoded in page)
    local regex_url = body:match('flashvars%.video_url%s*=%s*["\']([^"\']+)')
    if regex_url then
        add_format(regex_url)
    end

    -- Source 4: regex fallback for quality_NNNp fields
    for q in body:gmatch("quality_(%d+)[pP]") do
        local h = tonumber(q)
        local v = extract_regex_fallback(body, "quality_" .. q .. "p")
        if v then add_format(v, h) end
    end

    if #formats == 0 then
        ludo.logError("Tube8: no playable formats found")
        ludo.logInfo("Tube8: the video may require AES decryption which Ludo does not support")
        return
    end

    -- Sort by height descending
    table.sort(formats, function(a, b) return a.height > b.height end)
    local best = formats[1]

    local qual_str = best.format_id or (best.height > 0 and tostring(best.height) .. "p") or "source"
    ludo.logInfo("Tube8: selected " .. qual_str .. (best.tbr and (" (" .. tostring(best.tbr) .. " kbps)") or ""))

    local fname = safe_name(title) .. ".mp4"
    local _, dl_status, output = ludo.newDownload(
        best.url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname,
        { Referer = "https://www.tube8.com/" })
    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Tube8: queued → " .. (output or fname))
    elseif dl_status == 403 then
        ludo.logSuccess("Tube8: queued (HEAD blocked) → " .. (output or fname))
    else
        ludo.logError("Tube8: preflight HTTP " .. tostring(dl_status) .. " for " .. fname)
    end
end

return plugin
