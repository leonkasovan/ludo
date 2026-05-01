-- baidu.lua
-- Ludo plugin for 百度视频 (Baidu Video).
-- Ported from yt-dlp baidu.py extractor.
-- Fetches playlist metadata and episode URLs from Baidu's video API.

local plugin = { name = "Baidu", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30

-- Simple HTML entity unescape (&amp; → &, &lt; → <, ASCII numeric entities)
local function unescape_html(s)
    if type(s) ~= "string" then return s end
    s = s:gsub("&amp;", "&")
    s = s:gsub("&lt;", "<")
    s = s:gsub("&gt;", ">")
    s = s:gsub("&quot;", '"')
    s = s:gsub("&#39;", "'")
    s = s:gsub("&#(%d+);", function(n)
        local cp = tonumber(n)
        if cp and cp >= 32 and cp <= 126 then return string.char(cp) end
        return n -- keep high/control entities as-is
    end)
    return s
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    -- http://v.baidu.com/{type}/{id}.htm  (type=[a-z]+, id=\d+)
    if url:match("^https?://v%.baidu%.com/[a-z]+/%d+%.htm") then return true end
    return false
end

function plugin.process(url)
    -- Extract category and playlist ID
    local category, playlist_id = url:match("v%.baidu%.com/([a-z]+)/(%d+)%.htm")
    if not category or not playlist_id then
        ludo.logError("Baidu: could not parse URL")
        return
    end

    -- Map category to API worktype (same as yt-dlp baidu.py)
    local api_category = category
    if category == "show" then api_category = "tvshow"
    elseif category == "tv" then api_category = "tvplay" end

    ludo.logInfo("Baidu: fetching playlist " .. playlist_id .. " (" .. api_category .. ")")

    -- Helper: call Baidu API
    local function call_api(path)
        local api_url = "http://app.video.baidu.com/" .. path
            .. "/?worktype=adnative" .. api_category .. "&id=" .. playlist_id
        local body, s = http.get(api_url, { timeout = TIMEOUT })
        if s ~= 200 then return nil, "HTTP " .. tostring(s) end
        local ok, data = pcall(json.decode, body)
        if not ok then return nil, "JSON decode failed" end
        return data, nil
    end

    -- Fetch playlist metadata
    local detail, err = call_api("xqinfo")
    if not detail then
        ludo.logError("Baidu: failed to fetch playlist metadata: " .. tostring(err))
        return
    end

    local playlist_title = type(detail.title) == "string" and detail.title or ("Baidu playlist " .. playlist_id)
    local playlist_description = unescape_html(type(detail.intro) == "string" and detail.intro or "")

    ludo.logInfo("Baidu: \"" .. playlist_title .. "\"")
    if playlist_description ~= "" then
        ludo.logInfo("Baidu: " .. playlist_description:sub(1, 120))
    end

    -- Fetch episodes list
    local episodes_data, err2 = call_api("xqsingle")
    if not episodes_data then
        ludo.logError("Baidu: failed to fetch episodes: " .. tostring(err2))
        return
    end

    local episodes = episodes_data.videos
    if type(episodes) ~= "table" or #episodes == 0 then
        ludo.logError("Baidu: no episodes found in playlist")
        return
    end

    ludo.logInfo("Baidu: " .. tostring(#episodes) .. " episodes found, queuing...")

    -- Queue each episode as a download
    local queued = 0
    for i, ep in ipairs(episodes) do
        if type(ep) ~= "table" or type(ep.url) ~= "string" or ep.url == "" then
            goto skip_ep
        end
        local ep_title = type(ep.title) == "string" and ep.title or ("episode_" .. tostring(i))

        ludo.logInfo("Baidu: [" .. tostring(i) .. "/" .. tostring(#episodes) .. "] " .. ep_title)
        local _, dl_status, output = ludo.newDownload(
            ep.url, ludo.getOutputDirectory(), ludo.DOWNLOAD_QUEUE)

        if dl_status == 200 or dl_status == 206 or dl_status == 0 then
            ludo.logInfo("  Queued → " .. (output or ep_title))
        elseif dl_status == 403 then
            ludo.logInfo("  Queued (HEAD blocked) → " .. (output or ep_title))
        else
            ludo.logInfo("  Queued (preflight HTTP " .. tostring(dl_status) .. ") → " .. (output or ep_title))
        end
        queued = queued + 1
        ::skip_ep::
    end

    if queued > 0 then
        ludo.logSuccess("Baidu: queued " .. tostring(queued) .. "/" .. tostring(#episodes)
            .. " episodes from \"" .. playlist_title .. "\"")
    else
        ludo.logError("Baidu: no episodes could be queued")
    end
end

return plugin
