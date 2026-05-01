-- bilibili.lua
-- Ludo plugin for downloading Bilibili (B站) videos.
-- Ported from yt-dlp bilibili.py extractor (BiliBiliIE).
-- Handles: www.bilibili.com/video/BV... and /video/av...

local plugin = { name = "Bilibili", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://bilibili%.com/video/") then return true end
    if url:match("^https?://www%.bilibili%.com/video/") then return true end
    if url:match("^https?://bilibili%.com/festival/.*bvid=") then return true end
    if url:match("^https?://www%.bilibili%.com/festival/.*bvid=") then return true end
    return false
end

function plugin.process(url)
    -- Fetch the video page
    local body, status = http.get(url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { ["Accept-Language"] = "zh-CN,zh;q=0.9,en;q=0.8", Referer = "https://www.bilibili.com/" },
    })
    if status ~= 200 then
        ludo.logError("Bilibili: failed to fetch page (HTTP " .. tostring(status) .. ")")
        return
    end

    -- Extract __INITIAL_STATE__ JSON for metadata
    local function extract_json(html, key)
        local pat = key:gsub("%-", "%%-") .. "%s*=%s*"
        local _, pe = html:find(pat)
        if not pe then return nil end
        local start = html:find("{", pe + 1)
        if not start then return nil end
        local depth, in_str, escaped = 0, false, false
        for i = start, #html do
            local c = html:sub(i, i)
            if escaped then escaped = false
            elseif c == "\\" and in_str then escaped = true
            elseif c == '"' then in_str = not in_str
            elseif not in_str then
                if c == "{" then depth = depth + 1
                elseif c == "}" then
                    depth = depth - 1
                    if depth == 0 then
                        local ok, val = pcall(json.decode, html:sub(start, i))
                        if ok then return val end
                        return nil
                    end
                end
            end
        end
        return nil
    end

    local init_state = extract_json(body, "window.__INITIAL_STATE__")
    if not init_state then
        ludo.logError("Bilibili: could not extract __INITIAL_STATE__ from page")
        return
    end

    local video_data = init_state.videoData or init_state.videoInfo
    if not video_data or type(video_data) ~= "table" then
        ludo.logError("Bilibili: no video data in page")
        return
    end

    local bvid = video_data.bvid
    local title = video_data.title or ("Bilibili " .. (bvid or "video"))
    local cid = video_data.cid
    local pages = video_data.pages
    local uploader = init_state.upData and init_state.upData.name or nil

    if not cid then
        -- Explicitly check for nil; allow cid = 0 (valid page cid)
        ludo.logError("Bilibili: could not extract cid")
        return
    end

    ludo.logInfo("Bilibili: \"" .. title .. "\"" .. (uploader and (" by " .. uploader) or ""))

    -- Try __playinfo__ first (embedded video formats)
    local play_info = extract_json(body, "window.__playinfo__")
    if play_info and type(play_info.data) == "table" then
        play_info = play_info.data
    elseif play_info and type(play_info) == "table" and play_info.code then
        -- play_info is the root
    else
        -- Fetch from API
        ludo.logInfo("Bilibili: fetching play info from API...")
        local api_url = "https://api.bilibili.com/x/player/playurl?bvid=" .. bvid
            .. "&cid=" .. tostring(cid) .. "&fnval=16&qn=112&platform=web"
        local api_body, api_status = http.get(api_url, {
            user_agent = UA, timeout = TIMEOUT,
            headers = { Referer = "https://www.bilibili.com/" },
        })
        if api_status == 200 then
            local ok, api_data = pcall(json.decode, api_body)
            if ok and api_data.data then
                play_info = api_data.data
            end
        end
    end

    if not play_info then
        ludo.logError("Bilibili: could not get play info")
        return
    end

    -- Extract best video URL: prefer durl (direct MP4), fallback to dash
    local video_url = nil
    local durl = play_info.durl
    if type(durl) == "table" and #durl > 0 then
        local fragment = durl[1]
        video_url = type(fragment) == "table" and fragment.url or fragment
        ludo.logInfo("Bilibili: using durl (MP4) format")
    elseif type(play_info.dash) == "table" then
        local dash_videos = play_info.dash.video
        if type(dash_videos) == "table" and #dash_videos > 0 then
            -- Pick highest quality video
            table.sort(dash_videos, function(a, b) return (a.id or 0) > (b.id or 0) end)
            local best_vid = dash_videos[1]
            video_url = best_vid.baseUrl or best_vid.base_url or best_vid.url
            ludo.logInfo("Bilibili: using DASH video (quality=" .. tostring(best_vid.id) .. ")")
        end
    end

    if not video_url then
        ludo.logError("Bilibili: no playable video URL found")
        return
    end

    local fname = safe_name(title) .. ".mp4"
    local _, dl_status, output = ludo.newDownload(
        video_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname,
        { Referer = "https://www.bilibili.com/" }
    )

    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Bilibili: queued → " .. (output or fname))
    elseif dl_status == 403 then
        ludo.logSuccess("Bilibili: queued (HEAD blocked) → " .. (output or fname))
    else
        ludo.logError("Bilibili: preflight HTTP " .. tostring(dl_status))
    end
end

return plugin
