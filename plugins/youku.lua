-- youku.lua
-- Ludo plugin for downloading 优酷 (Youku) videos.
-- Ported from yt-dlp youku.py extractor.
-- Handles: v.youku.com/v_show/id_VID, player.youku.com/player.php/sid/VID,
--          play.tudou.com/v_show/id_VID

local plugin = { name = "Youku", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Generate __ysuid cookie value: timestamp + 3 random letters
local function generate_ysuid()
    local ts = tostring(os.time())
    local letters = ""
    for _ = 1, 3 do
        letters = letters .. string.char(math.random(65, 90))
    end
    return ts .. letters
end

-- Map stream_type to format name (mirrors yt-dlp)
local function format_name(st)
    local map = {
        ["3gp"] = "h6", ["3gphd"] = "h5", ["flv"] = "h4", ["flvhd"] = "h4",
        ["mp4"] = "h3", ["mp4hd"] = "h3", ["mp4hd2"] = "h4", ["mp4hd3"] = "h4",
        ["hd2"] = "h2", ["hd3"] = "h1",
    }
    return map[st]
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://v%.youku%.com/v_show/id_") then return true end
    if url:match("^https?://play(er)%.youku%.com/player%.php/sid/") then return true end
    if url:match("^https?://play%.tudou%.com/v_show/id_") then return true end
    if url:match("^https?://video%.tudou%.com/v/") then return true end
    if url:match("^youku:[A-Za-z0-9]+") then return true end
    return false
end

function plugin.process(url)
    -- Extract video ID
    local video_id = url:match("id_([A-Za-z0-9]+)")
        or url:match("sid/([A-Za-z0-9]+)")
        or url:match("youku:([A-Za-z0-9]+)")
    if not video_id then
        ludo.logError("Youku: could not extract video ID")
        return
    end

    ludo.logInfo("Youku: processing " .. video_id)

    -- Fetch cna from log.mmstat.com (ETag header)
    local _, _, hdrs = http.get("https://log.mmstat.com/eg.js", {
        user_agent = UA, timeout = TIMEOUT,
        headers = { Referer = "http://www.youku.com/" },
    })
    local cna = ""
    local etag_key = nil
    for k, v in pairs(hdrs or {}) do
        if k:lower() == "etag" then etag_key = k end
    end
    if etag_key then
        cna = hdrs[etag_key]:gsub('"', "")
    end
    if cna == "" then
        ludo.logInfo("Youku: mmstat did not return etag, using fallback")
        cna = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    end

    ludo.logInfo("Youku: cna=" .. cna)

    -- Build API request params
    local params = {}
    params["vid"] = video_id
    params["ccode"] = "0564"
    params["client_ip"] = "192.168.1.1"
    params["utid"] = cna
    params["client_ts"] = tostring(os.time())

    -- Build query string
    local qparts = {}
    for k, v in pairs(params) do
        qparts[#qparts + 1] = http.url_encode(k) .. "=" .. http.url_encode(tostring(v))
    end
    local query_str = table.concat(qparts, "&")

    -- Call the ups API
    local api_url = "https://ups.youku.com/ups/get.json?" .. query_str
    local body, status = http.get(api_url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = {
            Referer = url,
            ["Accept-Language"] = "zh-CN,zh;q=0.9",
        },
    })
    if status ~= 200 then
        ludo.logError("Youku: API request failed (HTTP " .. tostring(status) .. ")")
        return
    end

    local ok, data = pcall(json.decode, body)
    if not ok then
        ludo.logError("Youku: failed to parse API response")
        return
    end

    -- Check for errors
    local err = data.data and data.data.error
    if err then
        local note = err.note or ("error code " .. tostring(err.code))
        if note:find("版权") or note:find("copyright") then
            ludo.logError("Youku: video is geo-restricted to China")
        elseif note:find("私密") or note:find("private") then
            ludo.logError("Youku: video is private")
        else
            ludo.logError("Youku: " .. note)
        end
        return
    end

    local video_info = data.data and data.data.video
    local streams = data.data and data.data.stream
    if not video_info or not streams then
        ludo.logError("Youku: no video data in response")
        return
    end

    local title = video_info.title or ("Youku " .. video_id)
    local uploader = video_info.username
    local duration = tonumber(video_info.seconds)
    local thumbnail = video_info.logo

    ludo.logInfo("Youku: \"" .. title .. "\""
        .. (uploader and (" by " .. uploader) or "")
        .. (duration and (" (" .. tostring(duration) .. "s)") or ""))

    -- Collect HLS stream URLs, skip tail segments
    local hls_formats = {}
    for _, stream in ipairs(streams) do
        if stream.channel_type ~= "tail" and stream.m3u8_url then
            table.insert(hls_formats, {
                url = stream.m3u8_url,
                format_id = format_name(stream.stream_type) or stream.stream_type,
                height = tonumber(stream.height) or 0,
                width = tonumber(stream.width) or 0,
                filesize = tonumber(stream.size) or 0,
            })
        end
    end

    if #hls_formats == 0 then
        ludo.logError("Youku: no playable streams found")
        return
    end

    -- Sort by height descending (best quality first)
    table.sort(hls_formats, function(a, b) return a.height > b.height end)
    local best = hls_formats[1]

    ludo.logInfo("Youku: selected " .. (best.format_id or tostring(best.height) .. "p"))

    local m3u8 = dofile("plugins/m3u8.lua")
    local fname = safe_name(title) .. ".mp4"
    local hdrs = { Referer = "https://www.youku.com/" }
    local ok_dl, result = m3u8.download(best.url, ludo.getOutputDirectory(), fname, hdrs)
    if ok_dl then
        ludo.logSuccess("Youku: saved → " .. (result or fname))
    else
        ludo.logError("Youku: download failed: " .. tostring(result))
    end
end

return plugin
