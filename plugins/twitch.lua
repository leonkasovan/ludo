-- twitch.lua
-- Ludo plugin for downloading Twitch clips, VODs, and live streams.
-- Ported from yt-dlp twitch.py extractor.
--
-- Clips: direct MP4 via GraphQL (ShareClipRenderStatus).
-- VODs / live streams: HLS via m3u8 module using usher.ttvnw.net.

local plugin = { name = "Twitch", version = "20250501", creator = "opencode" }
local json = json or require("json")

local CLIENT_ID = "ue6666qo983tsx6so1t0vnawi233wa"
local GQL_URL = "https://gql.twitch.tv/gql"
local USHER_BASE = "https://usher.ttvnw.net"
local TIMEOUT = 30

-- Persisted query SHA-256 hashes
local HASHES = {
    ShareClipRenderStatus = "1844261bb449fa51e6167040311da4a7a5f1c34fe71c71a3e0c4f551bc30c698",
    VideoMetadata         = "45111672eea2e507f8ba44d101a61862f9c56b11dee09a15634cb75cb9b9084d",
    StreamMetadata        = "b57f9b910f8cd1a4659d894fe7550ccc81ec9052c01e438b290fd66a040b9b93",
}

local function safe_name(s)
    return (s or ""):gsub("[^%w%-_]", "_"):sub(1, 80)
end

-- POST to GQL, returns parsed JSON or nil,error
local function gql_post(ops)
    local body, status = http.post(GQL_URL, json.encode(ops), {
        timeout = TIMEOUT,
        headers = {
            ["Content-Type"] = "text/plain;charset=UTF-8",
            ["Client-ID"] = CLIENT_ID,
        },
    })
    if status ~= 200 then return nil, "HTTP " .. tostring(status) end
    local ok, data = pcall(json.decode, body)
    if not ok then return nil, "JSON decode failed" end
    return data, nil
end

-- Build a persisted-query operation
local function build_op(name, variables)
    return {
        operationName = name,
        variables = variables,
        extensions = {
            persistedQuery = { version = 1, sha256Hash = HASHES[name] },
        },
    }
end

-- Append sig & token as query params
local function append_token(url, sig, token)
    local s = "sig=" .. http.url_encode(sig) .. "&token=" .. http.url_encode(token)
    return url .. (url:match("%?") and "&" or "?") .. s
end

-- Fetch a playback access token via raw GQL query
-- token_kind = "video" | "stream",  param_name = "id" | "channelName"
local function get_access_token(token_kind, param_name, value)
    local method = token_kind .. "PlaybackAccessToken"
    local query_str = ("{%s(%s:\"%s\",params:{platform:\"web\",playerBackend:\"mediaplayer\",playerType:\"site\"}){value signature}}"):format(method, param_name, value)
    local data, err = gql_post({ { query = query_str } })
    if not data then return nil, err end
    local token = data[1] and data[1].data and data[1].data[method]
    if not token then return nil, "no access token in response" end
    return token, nil
end

-- Build a usher HLS URL for VOD or stream
local function build_usher_url(kind, video_id, token)
    local path = kind == "vod" and "vod/" .. video_id
        or "api/channel/hls/" .. video_id
    local q = {
        "allow_source=true", "allow_audio_only=true", "allow_spectre=true",
        "platform=web", "player=twitchweb",
        "supported_codecs=av1,h265,h264",
        "playlist_include_framerate=true",
    }
    return USHER_BASE .. "/" .. path .. ".m3u8?" .. table.concat(q, "&")
end

-- ─── Validate ─────────────────────────────────────────────────────────

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    local b = "^https?://"
    -- clips.twitch.tv
    if url:match(b .. "clips%.twitch%.tv/") then return true end
    -- twitch.tv subdomain variants
    for _, s in ipairs({"", "www%.", "go%.", "m%."}) do
        local h = b .. s .. "twitch%.tv"
        if url:match(h .. "/[^/]+/clip/") then return true end
        if url:match(h .. "/videos/%d+") then return true end
        if url:match(h .. "/[^/]+/v/%d+") then return true end
        if url:match(h .. "/[%w_]+$") then return true end
    end
    if url:match("^https?://m%.twitch%.tv/clip/") then return true end
    if url:match("^https?://player%.twitch%.tv/%?.*video=%d+") then return true end
    return false
end

-- ─── Process ──────────────────────────────────────────────────────────

function plugin.process(url)
    local slug = url:match("clips%.twitch%.tv/([%w%-]+)")
        or url:match("clips%.twitch%.tv/embed%?.*[?&]clip=([%w%-]+)")
        or url:match("/clip/([%w%-]+)")

    -- ── Clip path ─────────────────────────────────────────────────────
    if slug then
        ludo.logInfo("Twitch: fetching clip " .. slug)
        local op = build_op("ShareClipRenderStatus", { slug = slug })
        local data, err = gql_post({ op })
        if not data then ludo.logError("Twitch: GQL failed: " .. tostring(err)); return end
        local clip = data[1] and data[1].data and data[1].data.clip
        if not clip then ludo.logError("Twitch: clip not found"); return end

        local title = type(clip.title) == "string" and clip.title or ("twitch_clip_" .. slug)
        local uploader = nil
        if type(clip.broadcaster) == "table" then
            uploader = clip.broadcaster.displayName
        end
        local dur = tonumber(clip.durationSeconds)
        ludo.logInfo("Twitch: \"" .. title .. "\""
            .. (uploader and (" by " .. uploader) or "")
            .. (dur and (" (" .. tostring(dur) .. "s)") or ""))

        local token = clip.playbackAccessToken
        if not token or not token.signature then
            ludo.logError("Twitch: no playback access token"); return
        end

        local formats = {}
        for _, asset in ipairs(clip.assets or {}) do
            for _, q in ipairs((asset or {}).videoQualities or {}) do
                local u = q.sourceURL
                if type(u) == "string" and u:match("^https?://") then
                    table.insert(formats, {
                        url = append_token(u, token.signature, token.value),
                        height = tonumber(q.quality) or 0,
                    })
                end
            end
        end
        if #formats == 0 then ludo.logError("Twitch: no playable formats"); return end
        table.sort(formats, function(a, b) return a.height > b.height end)
        local best = formats[1]
        ludo.logInfo("Twitch: selected " .. tostring(best.height) .. "p")

        local fname = safe_name(title) .. ".mp4"
        local _, s, out = ludo.newDownload(best.url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname)
        if s == 200 or s == 206 or s == 0 then
            ludo.logSuccess("Twitch: queued → " .. (out or fname))
        elseif s == 403 then
            ludo.logSuccess("Twitch: queued (HEAD blocked) → " .. (out or fname))
        else
            ludo.logError("Twitch: preflight HTTP " .. tostring(s))
        end
        return
    end

    -- ── VOD path ──────────────────────────────────────────────────────
    local vod_id = url:match("/videos/(%d+)")
        or url:match("/v/(%d+)")
        or (url:match("player%.twitch%.tv")
            and url:match("[?&]video=(%d+)"))
    if vod_id then
        ludo.logInfo("Twitch: fetching VOD " .. vod_id)

        local token, err = get_access_token("video", "id", vod_id)
        if not token then ludo.logError("Twitch: access token failed: " .. tostring(err)); return end

        local usher_url = build_usher_url("vod", vod_id, token)
        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = "twitch_vod_" .. vod_id .. "_" .. os.date("%Y%m%d_%H%M%S") .. ".mp4"
        local ok, result = m3u8.download(usher_url, ludo.getOutputDirectory(), fname,
            { Referer = "https://player.twitch.tv/" })
        if ok then
            ludo.logSuccess("Twitch: VOD saved → " .. (result or fname))
        else
            ludo.logError("Twitch: VOD download failed: " .. tostring(result))
        end
        return
    end

    -- ── Stream path ───────────────────────────────────────────────────
    local channel = url:match("twitch%.tv/([%w_]+)$")
    if channel then
        ludo.logInfo("Twitch: fetching live stream for " .. channel)

        local token, err = get_access_token("stream", "channelName", channel)
        if not token then ludo.logError("Twitch: access token failed: " .. tostring(err)); return end

        local usher_url = build_usher_url("stream", channel, token)
        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = "twitch_live_" .. channel .. "_" .. os.date("%Y%m%d_%H%M%S") .. ".mp4"
        local ok, result = m3u8.download(usher_url, ludo.getOutputDirectory(), fname,
            { Referer = "https://player.twitch.tv/" })
        if ok then
            ludo.logSuccess("Twitch: stream saved → " .. (result or fname))
        else
            ludo.logError("Twitch: stream download failed: " .. tostring(result))
        end
        return
    end

    ludo.logError("Twitch: could not identify URL type")
end

return plugin
