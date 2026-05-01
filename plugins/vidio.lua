-- vidio.lua
-- Ludo plugin for downloading Vidio videos.
-- Ported from yt-dlp vidio.py extractor (VidioIE, VidioLiveIE).
-- Handles: vidio.com/watch/ID-SLUG, vidio.com/embed/ID-SLUG,
--          vidio.com/live/ID-SLUG

local plugin = { name = "Vidio", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Fetch API key from Vidio auth endpoint (POST with empty body)
local function fetch_api_key()
    local body, status = http.post("https://www.vidio.com/auth", "", { timeout = TIMEOUT })
    if status ~= 200 then return nil, "HTTP " .. tostring(status) end
    local ok, data = pcall(json.decode, body)
    if not ok or not data.api_key then return nil, "no api_key in response" end
    return data.api_key, nil
end

-- Call Vidio's JSON:API with the API key
local function call_vidio_api(url, api_key, note)
    local body, status = http.get(url, {
        timeout = TIMEOUT,
        headers = {
            ["Content-Type"] = "application/vnd.api+json",
            ["X-API-KEY"] = api_key,
        },
    })
    if status ~= 200 then return nil, "HTTP " .. tostring(status) end
    local ok, data = pcall(json.decode, body)
    if not ok then return nil, "JSON decode failed" end
    return data, nil
end

-- Fetch premier stream source URLs
local function fetch_premier_sources(video_id)
    local url = "https://www.vidio.com/interactions_stream.json?video_id=" .. tostring(video_id) .. "&type=videos"
    local body, status = http.get(url, { timeout = TIMEOUT })
    if status ~= 200 then return nil, "HTTP " .. tostring(status) end
    local ok, data = pcall(json.decode, body)
    if not ok then return nil, "JSON decode failed" end
    return data, nil
end

-- Fetch live stream token
local function fetch_live_token(video_id)
    local body, status = http.post(
        "https://www.vidio.com/live/" .. tostring(video_id) .. "/tokens",
        "", { timeout = TIMEOUT }
    )
    if status ~= 200 then return nil, "HTTP " .. tostring(status) end
    local ok, data = pcall(json.decode, body)
    if not ok then return nil, "JSON decode failed" end
    return data, nil
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://vidio%.com/watch/%d+%-") then return true end
    if url:match("^https?://www%.vidio%.com/watch/%d+%-") then return true end
    if url:match("^https?://vidio%.com/embed/%d+%-") then return true end
    if url:match("^https?://www%.vidio%.com/embed/%d+%-") then return true end
    if url:match("^https?://vidio%.com/live/%d+%-") then return true end
    if url:match("^https?://www%.vidio%.com/live/%d+%-") then return true end
    return false
end

function plugin.process(url)
    -- Extract video ID and display ID
    local video_id = url:match("vidio%.com/[^/]+/(%d+)")
    local display_id = url:match("vidio%.com/[^/]+/%d+%-(.+)")
    if not video_id then
        ludo.logError("Vidio: could not extract video ID")
        return
    end
    if not display_id then display_id = video_id end

    -- Determine type
    local is_live = url:match("/live/%d+%-") ~= nil

    ludo.logInfo("Vidio: fetching API key")
    local api_key, err = fetch_api_key()
    if not api_key then
        ludo.logError("Vidio: failed to get API key: " .. tostring(err))
        return
    end

    if is_live then
        ludo.logInfo("Vidio: fetching live stream " .. video_id)
        local stream_data, err2 = call_vidio_api(
            "https://www.vidio.com/api/livestreamings/" .. video_id .. "/detail",
            api_key, nil)
        if not stream_data then
            ludo.logError("Vidio: live stream API failed: " .. tostring(err2))
            return
        end
        local stream_meta = stream_data.livestreamings and stream_data.livestreamings[1]
        local user = (stream_data.users or { {} })[1]
        if not stream_meta then
            ludo.logError("Vidio: no live stream meta found")
            return
        end

        local title = stream_meta.title or ("Vidio Live " .. video_id)
        local uploader = user and user.name or nil

        if stream_meta.is_drm then
            ludo.logError("Vidio: live stream is DRM-protected and cannot be downloaded")
            return
        end

        if stream_meta.is_premium then
            ludo.logInfo("Vidio: premier live stream, fetching stream source...")
            local sources, serr = fetch_premier_sources(video_id)
            if not sources then
                ludo.logError("Vidio: premier source failed: " .. tostring(serr))
                ludo.logInfo("Vidio: this video may require login and subscription")
                return
            end
            local token_data, terr = fetch_live_token(video_id)
            local token_str = (token_data and token_data.token and ("?" .. token_data.token)) or ""

            local m3u8_url = sources.source or stream_meta.stream_token_url
            if m3u8_url then
            local hdrs = { Referer = "https://www.vidio.com/", ["X-API-KEY"] = api_key }
            local m3u8 = dofile("plugins/m3u8.lua")
            local fname = safe_name(title) .. ".mp4"
            local ok_dl, result = m3u8.download(m3u8_url .. token_str,
                ludo.getOutputDirectory(), fname, hdrs)
            if ok_dl then
                ludo.logSuccess("Vidio: saved → " .. (result or fname))
            else
                ludo.logError("Vidio: HLS download failed: " .. tostring(result))
            end
            else
                ludo.logError("Vidio: no stream URL available")
            end
            return
        end

        -- Free live stream
        local m3u8_url = stream_meta.stream_url
            or (stream_meta.stream_token_url and (function()
                local tok = fetch_live_token(video_id)
                return stream_meta.stream_token_url .. "?" .. ((tok and tok.token) or "")
            end)())

        ludo.logInfo("Vidio: \"" .. title .. "\"" .. (uploader and (" by " .. uploader) or ""))

        if not m3u8_url then
            ludo.logError("Vidio: no stream URL for live broadcast")
            return
        end

        local hdrs = { Referer = "https://www.vidio.com/", ["X-API-KEY"] = api_key }
        local m3u8 = dofile("plugins/m3u8.lua")
        local fname = safe_name(title) .. ".mp4"
        local ok_dl, result = m3u8.download(m3u8_url,
            ludo.getOutputDirectory(), fname, hdrs)
        if ok_dl then
            ludo.logSuccess("Vidio: saved → " .. (result or fname))
        else
            ludo.logError("Vidio: live stream download failed: " .. tostring(result))
        end
        return
    end

    -- ── VOD path (watch/embed) ────────────────────────────────────
    ludo.logInfo("Vidio: fetching video " .. video_id)

    local data, err = call_vidio_api(
        "https://api.vidio.com/videos/" .. video_id, api_key, nil)
    if not data then
        ludo.logError("Vidio: API request failed: " .. tostring(err))
        return
    end

    local video = data.videos and data.videos[1]
    if not video then
        ludo.logError("Vidio: no video data found")
        return
    end

    local title = (video.title or ""):gsub("^%s*(.-)%s*$", "%1")
    if title == "" then title = "Vidio " .. video_id end
    local description = video.description
    local uploader = data.users and data.users[1] and data.users[1].name or nil
    local channel = data.channels and data.channels[1] and data.channels[1].name or nil
    local duration = tonumber(video.duration)

    ludo.logInfo("Vidio: \"" .. title .. "\""
        .. (uploader and (" by " .. uploader) or "")
        .. (duration and (" (" .. tostring(duration) .. "s)") or ""))

    local is_premium = video.is_premium
    local m3u8_url = nil

    if is_premium then
        ludo.logInfo("Vidio: premier content, fetching stream source...")
        local sources, serr = fetch_premier_sources(video_id)
        if not sources then
            ludo.logError("Vidio: premier source failed: " .. tostring(serr))
            ludo.logInfo("Vidio: this video may require login and subscription")
            return
        end
        if sources.source then
            m3u8_url = sources.source
        end
    else
        -- Free video: get HLS URL from clips
        local clip = data.clips and data.clips[1]
        if clip then
            m3u8_url = clip.hls_url
        end
    end

    if not m3u8_url then
        ludo.logError("Vidio: no playable video URL found")
        return
    end

    local hdrs = {
        ["Referer"] = "https://www.vidio.com/",
        ["X-API-KEY"] = api_key,
    }
    local m3u8 = dofile("plugins/m3u8.lua")
    local fname = safe_name(title) .. ".mp4"
    local ok_dl, result = m3u8.download(m3u8_url,
        ludo.getOutputDirectory(), fname, hdrs)
    if ok_dl then
        ludo.logSuccess("Vidio: saved → " .. (result or fname))
    else
        ludo.logError("Vidio: download failed: " .. tostring(result))
    end
end

return plugin
