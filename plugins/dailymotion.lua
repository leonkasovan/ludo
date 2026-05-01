-- dailymotion.lua
-- Ludo plugin for downloading Dailymotion videos.
-- Ported from yt-dlp dailymotion.py extractor.
-- Handles: dailymotion.com/video/XID, dai.ly/XID,
--          geo.dailymotion.com/player.html?video=XID

local plugin = { name = "Dailymotion", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    -- dailymotion.com/video/XID (with or without www)
    if url:match("^https?://dailymotion%.%a+/video/") then return true end
    if url:match("^https?://www%.dailymotion%.%a+/video/") then return true end
    -- geo.dailymotion.com/player
    if url:match("^https?://geo%.dailymotion%.com/player") then return true end
    -- dai.ly/XID (short URL)
    if url:match("^https?://dai%.ly/") then return true end
    -- lequipe.fr/video/XID (with or without www)
    if url:match("^https?://lequipe%.fr/video/") then return true end
    if url:match("^https?://www%.lequipe%.fr/video/") then return true end
    return false
end

function plugin.process(url)
    -- Extract video ID (xid)
    local video_id = url:match("dailymotion%.%a+/video/([%w]+)")
        or url:match("dai%.ly/([%w]+)")
        or url:match("geo%.dailymotion%.com/player%?.*[?&]video=([%w]+)")
        or url:match("geo%.dailymotion%.com/player/[%w]+%.html%?.*[?&]video=([%w]+)")
        or url:match("lequipe%.fr/video/([%w]+)")
    if not video_id then
        ludo.logError("Dailymotion: could not extract video ID from URL")
        return
    end

    ludo.logInfo("Dailymotion: fetching metadata for " .. video_id)

    -- Fetch video metadata from the player metadata endpoint
    local meta_url = "https://www.dailymotion.com/player/metadata/video/"
        .. video_id .. "?app=com.dailymotion.neon"
    local body, status = http.get(meta_url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { Referer = "https://www.dailymotion.com/", Accept = "application/json" },
    })
    if status ~= 200 then
        ludo.logError("Dailymotion: failed to fetch metadata (HTTP " .. tostring(status) .. ")")
        return
    end

    local ok, metadata = pcall(json.decode, body)
    if not ok then
        ludo.logError("Dailymotion: failed to parse metadata JSON")
        return
    end

    -- Check for API error
    if metadata.error then
        local err_title = metadata.error.title or metadata.error.raw_message or "unknown"
        ludo.logError("Dailymotion: " .. tostring(err_title))
        return
    end

    local title = metadata.title or ("Dailymotion " .. video_id)
    local owner = metadata.owner or {}
    local uploader = owner.screenname or metadata.screenname or nil
    local duration = tonumber(metadata.duration)

    ludo.logInfo("Dailymotion: \"" .. title .. "\""
        .. (uploader and (" by " .. uploader) or "")
        .. (duration and (" (" .. tostring(duration) .. "s)") or ""))

    -- Extract video URLs from qualities
    local qualities = metadata.qualities
    if not qualities or type(qualities) ~= "table" then
        ludo.logError("Dailymotion: no qualities found in metadata")
        return
    end

    -- Collect all formats, preferring direct MP4 over HLS
    local formats = {}
    local hls_url = nil
    local outdir = ludo.getOutputDirectory()

    for qual_name, media_list in pairs(qualities) do
        if type(media_list) == "table" then
            for _, m in ipairs(media_list) do
                if type(m) ~= "table" then goto skip_q end
                local media_url = m.url
                local media_type = m.type
                if not media_url or media_type == "application/vnd.lumberjack.manifest" then
                    goto skip_q
                end
                -- Strip fragment
                media_url = media_url:gsub("#.*$", "")
                if media_type == "application/x-mpegURL" then
                    -- HLS: save for later (prefer direct MP4)
                    if not hls_url then hls_url = media_url end
                else
                    -- Direct MP4 URL
                    local height = 0
                    local width = 0
                    local fps = nil
                    local _, _, w, h, f = media_url:find("/H264-(%d+)x(%d+)(%-60)?")
                    if h then
                        height = tonumber(h) or 0
                        width = tonumber(w) or 0
                        if f then fps = 60 end
                    end
                    table.insert(formats, {
                        url = media_url,
                        format_id = "http-" .. qual_name,
                        height = height,
                        width = width,
                        fps = fps or 30,
                    })
                end
                ::skip_q::
            end
        end
    end

    -- If no direct MP4 found, try HLS via m3u8 module
    if #formats == 0 and hls_url then
        ludo.logInfo("Dailymotion: using HLS stream")
        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = safe_name(title) .. ".mp4"
        local ok_dl, result = m3u8.download(hls_url, outdir, fname,
            { Referer = "https://www.dailymotion.com/" })
        if ok_dl then
            ludo.logSuccess("Dailymotion: saved → " .. (result or fname))
        else
            ludo.logError("Dailymotion: HLS download failed: " .. tostring(result))
        end
        return
    end

    if #formats == 0 then
        ludo.logError("Dailymotion: no playable formats found")
        return
    end

    -- Sort by height descending, pick best
    table.sort(formats, function(a, b) return a.height > b.height end)
    local best = formats[1]
    ludo.logInfo("Dailymotion: selected "
        .. tostring(best.height) .. "p" .. tostring(math.floor(best.fps)))

    local fname = safe_name(title) .. ".mp4"
    local _, dl_status, output = ludo.newDownload(
        best.url, outdir, ludo.DOWNLOAD_NOW, fname,
        { Referer = "https://www.dailymotion.com/" })
    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Dailymotion: queued → " .. (output or fname))
    elseif dl_status == 403 then
        ludo.logSuccess("Dailymotion: queued (HEAD blocked) → " .. (output or fname))
    else
        ludo.logError("Dailymotion: preflight HTTP " .. tostring(dl_status))
    end
end

return plugin
