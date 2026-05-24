-- plugin_moderate.lua — template for a moderate-complexity plugin
-- Pattern: JSON API consumption, multi-format selection, HLS fallback
-- Example: video sites with quality tiers (Dailymotion, Bluesky, Vidio)

local plugin = { name = "ModerateSite", version = "20260501", creator = "Your Name" }
local json = json or require("json")
local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

-- ---- helpers ----------------------------------------------------------

local function safe_name(s)
    if type(s) ~= "string" then return "file" end
    return s:gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Fetch a JSON endpoint and return the decoded table, or nil on failure.
local function fetch_json(url, opts)
    opts = opts or {}
    opts.user_agent = opts.user_agent or UA
    opts.timeout = opts.timeout or TIMEOUT
    local body, status = http.get(url, opts)
    if status ~= 200 then
        ludo.logError("HTTP " .. tostring(status) .. " fetching " .. url)
        return nil
    end
    local ok, data = pcall(json.decode, body)
    if not ok then
        ludo.logError("JSON decode failed for " .. url)
        return nil
    end
    return data
end

-- Extract <video> src from an HTML embed page.
local function extract_video_src(html)
    return html:match('<video[^>]+src="([^"]+)"')
end

-- ---- URL validation ---------------------------------------------------

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    return url:match("^https?://www%.examplesite%.com/") ~= nil
end

-- ---- Main extraction --------------------------------------------------

function plugin.process(url)
    -- Extract video ID from URL
    local video_id = url:match("examplesite%.com/video/(%w+)")
    if not video_id then
        ludo.logError("Could not extract video ID")
        return
    end
    ludo.logInfo("Processing video " .. video_id)

    -- Step 1: fetch metadata JSON from the API
    local meta_url = "https://api.examplesite.com/v1/videos/" .. video_id
    local meta = fetch_json(meta_url, { headers = { Referer = url } })
    if not meta then return end
    if meta.error then
        ludo.logError("API error: " .. tostring(meta.error.message or meta.error.code))
        return
    end

    local title = meta.title or ("video_" .. video_id)
    ludo.logInfo("Title: " .. title)

    -- Step 2: collect available formats
    local outdir   = ludo.getOutputDirectory()
    local hls_url  = nil
    local formats  = {}

    for _, fmt in ipairs(meta.formats or {}) do
        if type(fmt) ~= "table" then goto next end
        local media_url = fmt.url
        if not media_url then goto next end
        media_url = media_url:gsub("#.*$", "")  -- strip fragment

        if (fmt.mime_type or ""):match("application/x%-mpegURL")
            or (fmt.protocol or ""):match("m3u8")
            or media_url:match("%.m3u8") then
            hls_url = media_url  -- save as fallback
        elseif media_url:match("^https?://") then
            table.insert(formats, {
                url    = media_url,
                height = tonumber(fmt.height) or 0,
                label  = fmt.label or (tostring(fmt.height or 0) .. "p"),
            })
        end
        ::next::
    end

    -- Step 3: pick the best direct format (highest resolution)
    table.sort(formats, function(a, b) return a.height > b.height end)
    local best = formats[1]

    if best then
        local fname = safe_name(title) .. ".mp4"
        ludo.logInfo("Selected: " .. (best.label or "best"))
        local _, dl_status, output = ludo.newDownload(
            best.url, outdir, ludo.DOWNLOAD_NOW, fname,
            { Referer = url })
        if dl_status == 200 or dl_status == 206 or dl_status == 0 then
            ludo.logSuccess("Queued → " .. (output or fname))
        elseif dl_status == 403 then
            ludo.logSuccess("Queued (HEAD blocked) → " .. (output or fname))
        else
            ludo.logError("Preflight HTTP " .. tostring(dl_status))
        end
        return
    end

    -- Step 4: fallback to HLS via m3u8 module
    if hls_url then
        ludo.logInfo("No direct MP4; using HLS fallback")
        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = safe_name(title) .. ".mp4"
        local ok_dl, result = m3u8.download(hls_url, outdir, fname,
            { Referer = url })
        if ok_dl then
            ludo.logSuccess("Saved → " .. (result or fname))
        else
            ludo.logError("HLS download failed: " .. tostring(result))
        end
        return
    end

    ludo.logError("No playable formats found")
end

return plugin
