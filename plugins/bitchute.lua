-- bitchute.lua
-- Ludo plugin for downloading BitChute videos.
-- Ported from yt-dlp bitchute.py extractor.
-- Handles: www.bitchute.com/video/ID, /embed/ID

local plugin = { name = "BitChute", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- BitChute API call (POST JSON to api.bitchute.com)
local function bitchute_api(endpoint, data, fatal)
    local url = "https://api.bitchute.com/api/beta/" .. endpoint
    local body, status = http.post(url, json.encode(data), {
        timeout = TIMEOUT, user_agent = UA,
        headers = {
            ["Accept"] = "application/json",
            ["Content-Type"] = "application/json",
        },
    })
    if status ~= 200 then
        if fatal then return nil, "HTTP " .. tostring(status) end
        return nil, nil
    end
    local ok, data = pcall(json.decode, body)
    if not ok then
        if fatal then return nil, "JSON decode failed" end
        return nil, nil
    end
    return data, nil
end

-- Try different seed hosts to find a working media URL
local function check_media_url(media_url, video_id)
    -- Replace the seed host with alternatives
    local seeds = {
        "seed122", "seed125", "seed126", "seed128",
        "seed132", "seed150", "seed151", "seed152", "seed153",
        "seed167", "seed171", "seed177", "seed305", "seed307",
        "seedp29xb", "zb10-7gsop1v78",
    }
    -- First try the original URL
    local url, status = http.head(media_url, {
        follow_redirects = true, timeout = TIMEOUT,
        headers = { Referer = "https://www.bitchute.com/" },
    })
    if status == 200 or status == 206 then
        return { url = media_url, preflight = "HEAD OK" }
    end

    -- Try alternative seed hosts
    for _, seed in ipairs(seeds) do
        local alt_url, _ = media_url:gsub("seed%d+", seed)
        alt_url, _ = alt_url:gsub("seed[a-z0-9]+", seed)
        if alt_url ~= media_url then
            local _, alt_status = http.head(alt_url, {
                follow_redirects = true, timeout = TIMEOUT,
                headers = { Referer = "https://www.bitchute.com/" },
            })
            if alt_status == 200 or alt_status == 206 then
                return { url = alt_url, preflight = "HEAD OK" }
            end
        end
    end
    return { url = media_url, preflight = "HEAD failed, trying anyway" }
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://bitchute%.com/video/") then return true end
    if url:match("^https?://www%.bitchute%.com/video/") then return true end
    if url:match("^https?://old%.bitchute%.com/video/") then return true end
    if url:match("^https?://bitchute%.com/embed/") then return true end
    if url:match("^https?://www%.bitchute%.com/embed/") then return true end
    if url:match("^https?://old%.bitchute%.com/embed/") then return true end
    return false
end

function plugin.process(url)
    local video_id = url:match("bitchute%.com/[^/]+/([^/?#&]+)")
    if not video_id then
        ludo.logError("BitChute: could not extract video ID")
        return
    end
    ludo.logInfo("BitChute: processing " .. video_id)

    -- Step 1: Get media URL
    local media, media_err = bitchute_api("video/media", { video_id = video_id }, true)
    if not media then
        ludo.logError("BitChute: media API failed: " .. tostring(media_err))
        return
    end
    local media_url = media.media_url
    if not media_url then
        ludo.logError("BitChute: no media_url in API response")
        return
    end
    ludo.logInfo("BitChute: media_url=" .. media_url)

    -- Step 2: Get video metadata
    local video_info, _ = bitchute_api("video", { video_id = video_id }, false)

    -- Step 3: Get channel metadata if available
    local channel_id = nil
    if video_info and type(video_info.channel) == "table" then
        channel_id = video_info.channel.channel_id
    end
    local channel_info = nil
    if channel_id then
        channel_info, _ = bitchute_api("channel", { channel_id = channel_id }, false)
    end

    -- Extract title and uploader
    local title = nil
    if video_info then
        title = video_info.video_name
    end
    if not title then
        title = "BitChute " .. video_id
    end
    local uploader = nil
    if channel_info then
        uploader = channel_info.profile_name or channel_info.channel_name
    end
    local duration = nil
    if video_info and video_info.duration then
        local parts = { video_info.duration:match("(%d+):(%d+):?(%d*)") }
        if #parts >= 2 then
            duration = tonumber(parts[1]) * 3600 + tonumber(parts[2]) * 60 + (tonumber(parts[3]) or 0)
        end
    end

    ludo.logInfo("BitChute: \"" .. title .. "\"" .. (uploader and (" by " .. uploader) or ""))

    -- Step 4: Handle HLS vs direct MP4
    if media_url:match("%.m3u8$") then
        ludo.logInfo("BitChute: using HLS stream")
        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = safe_name(title) .. ".mp4"
        local ok_dl, result = m3u8.download(media_url,
            ludo.getOutputDirectory(), fname,
            { Referer = "https://www.bitchute.com/" })
        if ok_dl then
            ludo.logSuccess("BitChute: saved → " .. (result or fname))
        else
            ludo.logError("BitChute: HLS download failed: " .. tostring(result))
        end
    else
        -- Direct MP4: try different seed hosts
        local fmt = check_media_url(media_url, video_id)
        ludo.logInfo("BitChute: selected MP4 (" .. fmt.preflight .. ")")

        local fname = safe_name(title) .. ".mp4"
        local _, dl_status, output = ludo.newDownload(
            fmt.url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname,
            { Referer = "https://www.bitchute.com/" })
        if dl_status == 200 or dl_status == 206 or dl_status == 0 then
            ludo.logSuccess("BitChute: queued → " .. (output or fname))
        elseif dl_status == 403 then
            ludo.logSuccess("BitChute: queued (HEAD blocked) → " .. (output or fname))
        else
            ludo.logError("BitChute: preflight HTTP " .. tostring(dl_status))
        end
    end
end

return plugin
