-- xiaohongshu.lua
-- Ludo plugin for downloading 小红书 (Xiaohongshu / RED) videos.
-- Ported from yt-dlp xiaohongshu.py extractor.
-- Handles: www.xiaohongshu.com/explore/ID and /discovery/item/ID

local plugin = { name = "Xiaohongshu", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Extract a JS/JSON object assignment from page HTML.
-- Handles `window.__INITIAL_STATE__ = {...}` with JS-style syntax
-- (unquoted keys, trailing commas) via a basic transform.
local function extract_initial_state(html, var_name)
    for _, prefix in ipairs({ "window." .. var_name .. "%s*=", var_name .. "%s*=" }) do
        local _, pe = html:find(prefix)
        if not pe then goto next end
        local start = html:find("{", pe + 1)
        if not start then goto next end
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
                        local chunk = html:sub(start, i)
                        -- Basic js-to-json transform: quote unquoted keys, remove trailing commas
                        chunk = chunk:gsub("([{,])%s*([%w_]+)%s*:", "%1\"%2\":")
                        chunk = chunk:gsub(",%s*}", "}")
                        chunk = chunk:gsub(",%s*]", "]")
                        local ok, val = pcall(json.decode, chunk)
                        if ok then return val end
                        -- Try without key quoting (JSON5-ish)
                        local ok2, val2 = pcall(json.decode, chunk)
                        return ok2 and val2 or nil
                    end
                end
            end
        end
        ::next::
    end
    return nil
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://www%.xiaohongshu%.com/explore/") then return true end
    if url:match("^https?://www%.xiaohongshu%.com/discovery/item/") then return true end
    return false
end

function plugin.process(url)
    local display_id = url:match("xiaohongshu%.com/[^/]+/([%da-f]+)")
    if not display_id then
        ludo.logError("Xiaohongshu: could not extract note ID")
        return
    end

    ludo.logInfo("Xiaohongshu: fetching note " .. display_id)

    local body, status = http.get(url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = {
            Referer = "https://www.xiaohongshu.com/",
            ["Accept-Language"] = "zh-CN,zh;q=0.9,en;q=0.8",
        },
    })
    if status ~= 200 then
        ludo.logError("Xiaohongshu: failed to fetch page (HTTP " .. tostring(status) .. ")")
        return
    end

    local state = extract_initial_state(body, "__INITIAL_STATE__")
    if not state then
        if #body < 3000 or body:find("sdt_source") then
            ludo.logError("Xiaohongshu: page requires JavaScript (SPA anti-bot protection)")
            ludo.logInfo("Xiaohongshu: video data is loaded dynamically by JavaScript")
            ludo.logInfo("Xiaohongshu: use yt-dlp or a browser to download this video")
        else
            ludo.logError("Xiaohongshu: could not extract __INITIAL_STATE__")
        end
        return
    end

    local note_detail = state.note and state.note.noteDetailMap and state.note.noteDetailMap[display_id]
    local note_info = note_detail and note_detail.note
    if not note_info then
        ludo.logError("Xiaohongshu: no note data found")
        return
    end

    local title = note_info.title or body:match('<meta[^>]+og:title[^>]+content="([^"]+)"')
        or ("Xiaohongshu " .. display_id)
    local description = note_info.desc
    local uploader_id = nil
    if type(note_info.user) == "table" then
        uploader_id = note_info.user.userId
    end

    ludo.logInfo("Xiaohongshu: \"" .. title .. "\"" .. (uploader_id and (" by " .. uploader_id) or ""))

    -- Extract video URLs from media stream
    local video_stream = note_info.video and note_info.video.media and note_info.video.media.stream
    local formats = {}

    if type(video_stream) == "table" then
        for _, master_entry in pairs(video_stream) do
            if type(master_entry) == "table" then
                for _, fmt_info in pairs(master_entry) do
                    if type(fmt_info) == "table" then
                        local master_url = fmt_info.masterUrl
                        if type(master_url) == "string" and master_url:match("^https?://") then
                            table.insert(formats, {
                                url = master_url,
                                format_id = fmt_info.qualityType or "master",
                                height = tonumber(fmt_info.height) or 0,
                                width = tonumber(fmt_info.width) or 0,
                                tbr = tonumber(fmt_info.avgBitrate) and math.floor(fmt_info.avgBitrate / 1000) or nil,
                                fps = tonumber(fmt_info.fps),
                            })
                        end
                        -- Also collect backup URLs (skip duplicates)
                        local backup = fmt_info.backupUrls
                        if type(backup) == "table" then
                            for _, bu in ipairs(backup) do
                                if type(bu) == "string" and bu:match("^https?://") then
                                    local has_url = false
                                    for _, f in ipairs(formats) do
                                        if f.url == bu then has_url = true; break end
                                    end
                                    if not has_url then
                                        table.insert(formats, {
                                            url = bu,
                                            format_id = (fmt_info.qualityType or "backup") .. "_bak",
                                            height = tonumber(fmt_info.height) or 0,
                                            width = tonumber(fmt_info.width) or 0,
                                        })
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end

    -- Try original video key (direct stream source)
    local origin_key = nil
    if type(note_info.video) == "table" and type(note_info.video.consumer) == "table" then
        origin_key = note_info.video.consumer.originVideoKey
    end
    if origin_key and type(origin_key) == "string" and origin_key ~= "" then
        local direct_url = "https://sns-video-bd.xhscdn.com/" .. origin_key
        ludo.logInfo("Xiaohongshu: checking original video...")
        local _, head_status = http.head(direct_url, {
            follow_redirects = true,
            timeout = TIMEOUT,
            headers = { Referer = "https://www.xiaohongshu.com/" },
        })
        if head_status == 200 then
            table.insert(formats, 1, {
                url = direct_url,
                format_id = "direct",
                quality = 1,
            })
        end
    end

    if #formats == 0 then
        ludo.logError("Xiaohongshu: no playable formats found")
        return
    end

    -- Sort: direct quality=1 first, then by height descending
    table.sort(formats, function(a, b)
        if (a.quality or 0) ~= (b.quality or 0) then
            return (a.quality or 0) > (b.quality or 0)
        end
        return (a.height or 0) > (b.height or 0)
    end)
    local best = formats[1]
    local qual_str = best.format_id or (best.height and best.height .. "p") or "source"
    ludo.logInfo("Xiaohongshu: selected " .. qual_str)

    local fname = safe_name(title) .. ".mp4"
    local _, dl_status, output = ludo.newDownload(
        best.url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname,
        { Referer = "https://www.xiaohongshu.com/" })
    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Xiaohongshu: queued → " .. (output or fname))
    elseif dl_status == 403 then
        ludo.logSuccess("Xiaohongshu: queued (HEAD blocked) → " .. (output or fname))
    else
        ludo.logError("Xiaohongshu: preflight HTTP " .. tostring(dl_status))
    end
end

return plugin
