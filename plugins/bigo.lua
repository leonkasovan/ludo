-- bigo.lua
-- Ludo plugin for Bigo live streams.
-- Ported from yt-dlp bigo.py extractor.
-- Note: Bigo only serves HLS (.m3u8) streams for live content.
-- Ludo does not support HLS, so this plugin extracts metadata and
-- optionally downloads the thumbnail snapshot.

local plugin = { name = "Bigo", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://bigo%.tv/") then return true end
    if url:match("^https?://www%.bigo%.tv/") then return true end
    return false
end

function plugin.process(url)
    local user_id = url:match("bigo%.tv/[a-z]+/([^/]+)")
        or url:match("bigo%.tv/([^/?#]+)")
    if not user_id then
        ludo.logError("Bigo: could not extract user ID from URL")
        return
    end

    ludo.logInfo("Bigo: checking stream for user " .. user_id)

    local body, status = http.post(
        "https://ta.bigo.tv/official_website/studio/getInternalStudioInfo",
        "siteId=" .. user_id,
        {
            timeout = TIMEOUT,
            headers = {
                ["Accept"] = "application/json",
                ["Content-Type"] = "application/x-www-form-urlencoded",
            },
        }
    )

    if status ~= 200 then
        ludo.logError("Bigo: API request failed (HTTP " .. tostring(status) .. ")")
        return
    end

    local ok, data = pcall(json.decode, body)
    if not ok then
        ludo.logError("Bigo: failed to parse API response")
        return
    end

    if data.code and data.code ~= 0 then
        ludo.logError("Bigo: " .. tostring(data.msg or "unknown error") .. " (code " .. tostring(data.code) .. ")")
        return
    end

    local info = data.data
    if not info or type(info) ~= "table" then
        ludo.logError("Bigo: no data in response")
        return
    end

    local room_id = info.roomId or user_id
    local title = info.roomTopic or "Bigo live"
    local uploader = info.nick_name or "unknown"

    if not info.alive then
        ludo.logInfo("Bigo: user \"" .. uploader .. "\" (" .. user_id .. ") is not currently live")
        ludo.logInfo("Bigo: no stream to download")
        return
    end

    ludo.logInfo("Bigo: \"" .. title .. "\" by " .. uploader .. " (room " .. tostring(room_id) .. ")")

    -- Download HLS stream via m3u8 module
    local hls_src = info.hls_src
    if hls_src and hls_src ~= "" then
        local m3u8 = dofile("plugins/m3u8.lua")
        local safe = (title or "bigo"):gsub("[\\/:*?\"<>|]", "_"):sub(1, 60)
        local fname = "bigo_" .. tostring(room_id) .. "_" .. safe .. ".mp4"
        local ok, result = m3u8.download(hls_src, ludo.getOutputDirectory(), fname)
        if ok then
            ludo.logSuccess("Bigo: stream saved → " .. (result or fname))
        else
            ludo.logError("Bigo: HLS download failed: " .. tostring(result))
        end
    else
        ludo.logError("Bigo: no stream URL available")
    end

    -- Download the thumbnail snapshot as a separate preview
    local snapshot = info.snapshot
    if snapshot and snapshot:match("^https?://") then
        local tn_fname = "bigo_" .. tostring(room_id) .. "_preview.jpg"
        ludo.newDownload(snapshot, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, tn_fname)
    end
end

return plugin
