-- pinterest.lua
-- Ludo plugin for downloading Pinterest video pins.
-- Ported from yt-dlp pinterest.py extractor (PinterestIE).
-- Handles: pinterest.com/pin/ID, pinterest.{tld}/pin/ID

local plugin = { name = "Pinterest", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Build the Pinterest API URL for a resource call
local function call_pinterest_api(resource, options)
    local api_url = "https://www.pinterest.com/resource/" .. resource .. "Resource/get/"
        .. "?data=" .. http.url_encode(json.encode({ options = options }))
    return http.get(api_url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = {
            ["X-Pinterest-PWS-Handler"] = "www/[username].js",
            Accept = "application/json",
        },
    })
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    local host = url:match("^https?://([^/]+)")
    if not host then return false end
    host = host:lower()
    -- Domain must be pinterest.<tld> or <sub>.pinterest.<tld>
    local pi = host:find("pinterest%.")
    if not pi then return false end
    if pi > 1 and host:sub(pi - 1, pi - 1) ~= "." then return false end
    -- Reject arbitrary subdomains (only www is allowed; ccTLDs like co.ca are fine)
    local sub = host:sub(1, pi - 1)
    if pi > 1 then
        sub = sub:gsub("%.$", "")
        if sub ~= "www" and (sub:match("[^a-z]") or #sub > 2) then return false end
    end
    -- Check path: /pin/NUM or /USER/BOARD
    if url:match("/pin/%d+") then return true end
    local path = url:match("^https?://[^/]+/(.+)$")
    if path then
        path = path:gsub("/+$", "")
        if path:match("^[^/]+/[^/]+$") then return true end
    end
    return false
end

function plugin.process(url)
    -- Extract pin ID
    local video_id = url:match("pin/(%d+)")
        or url:match("pintrest%.[a-z%.]+/([^/]+)/[^/]+$")
    if not video_id then
        ludo.logError("Pinterest: could not extract pin ID")
        return
    end

    ludo.logInfo("Pinterest: fetching pin " .. video_id)

    -- Call the Pin API
    local body, status = call_pinterest_api("Pin", {
        field_set_key = "unauth_react_main_pin",
        id = video_id,
    })
    if status ~= 200 then
        ludo.logError("Pinterest: API request failed (HTTP " .. tostring(status) .. ")")
        return
    end

    local ok, response = pcall(json.decode, body)
    if not ok then
        ludo.logError("Pinterest: failed to parse API response")
        return
    end

    local data = response.resource_response and response.resource_response.data
    if not data then
        ludo.logError("Pinterest: no data in API response")
        return
    end

    -- Check for third-party embed (Vimeo, YouTube, etc.)
    local domain = (data.domain or ""):lower()
    if domain ~= "uploaded by user" and type(data.embed) == "table" and data.embed.src then
        ludo.logInfo("Pinterest: pin embeds external content from " .. tostring(domain))
        ludo.logInfo("Pinterest: try the embed URL directly: " .. tostring(data.embed.src))
        return
    end

    -- Extract video data from videos.video_list or story_pin_data
    local video_list = data.videos and data.videos.video_list
        or (data.story_pin_data and data.story_pin_data.pages
            and (function()
                for _, page in ipairs(data.story_pin_data.pages or {}) do
                    if type(page.blocks) == "table" then
                        for _, block in ipairs(page.blocks) do
                            if type(block) == "table" and block.video
                                and block.video.video_list then
                                return block.video.video_list
                            end
                        end
                    end
                end
                return nil
            end)())

    if not video_list then
        ludo.logError("Pinterest: no video data found in pin")
        return
    end

    -- Extract metadata
    local title = data.title or data.grid_title or ("Pinterest " .. video_id)
    local uploader = nil
    if type(data.closeup_attribution) == "table" then
        uploader = data.closeup_attribution.full_name
    end

    ludo.logInfo("Pinterest: \"" .. title .. "\"" .. (uploader and (" by " .. uploader) or ""))

    -- Collect formats
    local formats = {}
    local hls_urls = {}
    for fmt_id, fmt_data in pairs(video_list) do
        if type(fmt_data) == "table" then
            local fmt_url = fmt_data.url
            if type(fmt_url) ~= "string" or fmt_url == "" then goto next_fmt end
            local is_hls = fmt_id:lower():find("hls") ~= nil
                or (fmt_url:match("%.m3u8$") ~= nil)
            if is_hls then
                table.insert(hls_urls, { url = fmt_url, bandwidth = tonumber(fmt_data.bitrate) or 0 })
            else
                table.insert(formats, {
                    url = fmt_url,
                    format_id = fmt_id,
                    height = tonumber(fmt_data.height) or 0,
                    width = tonumber(fmt_data.width) or 0,
                })
            end
        end
        ::next_fmt::
    end

    -- Prefer direct MP4, fallback to HLS
    if #formats > 0 then
        table.sort(formats, function(a, b) return a.height > b.height end)
        local best = formats[1]
        ludo.logInfo("Pinterest: selected " .. (best.format_id or (tostring(best.height) .. "p")))

        local fname = safe_name(title) .. ".mp4"
        local _, dl_status, output = ludo.newDownload(
            best.url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname,
            { Referer = "https://www.pinterest.com/" })
        if dl_status == 200 or dl_status == 206 or dl_status == 0 then
            ludo.logSuccess("Pinterest: queued → " .. (output or fname))
        elseif dl_status == 403 then
            ludo.logSuccess("Pinterest: queued (HEAD blocked) → " .. (output or fname))
        else
            ludo.logError("Pinterest: preflight HTTP " .. tostring(dl_status))
        end
    elseif #hls_urls > 0 then
        table.sort(hls_urls, function(a, b) return a.bandwidth > b.bandwidth end)
        local best = hls_urls[1]
        ludo.logInfo("Pinterest: using HLS (" .. tostring(best.bandwidth) .. " bps)")

        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = safe_name(title) .. ".mp4"
        local ok_dl, result = m3u8.download(best.url,
            ludo.getOutputDirectory(), fname,
            { Referer = "https://www.pinterest.com/" })
        if ok_dl then
            ludo.logSuccess("Pinterest: saved → " .. (result or fname))
        else
            ludo.logError("Pinterest: HLS download failed: " .. tostring(result))
        end
    else
        ludo.logError("Pinterest: no playable video formats found")
    end
end

return plugin
