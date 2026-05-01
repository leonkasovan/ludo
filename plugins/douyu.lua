-- douyu.lua
-- Ludo plugin for 斗鱼 (Douyu) video clips.
-- Ported from yt-dlp douyutv.py extractor (DouyuShowIE).
-- Handles: v.douyu.com/show/ID
--
-- Note: Douyu live streams (douyu.com/ROOM) use JS-based signing
-- that cannot be replicated without a JavaScript engine. Only VOD
-- clips on v.douyu.com are supported.

local plugin = { name = "Douyu", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Extract a JSON object embedded in a JS assignment like var X = {...}
local function extract_js_json(html, var_name)
    local patterns = {
        var_name .. "%s*=",
        "window%." .. var_name .. "%s*=",
        "%$" .. var_name .. "%s*=",
    }
    local pe
    for _, p in ipairs(patterns) do
        local _, e = html:find(p)
        if e then pe = e; break end
    end
    if not pe then return nil end
    local obj_start = html:find("{", pe + 1)
    if not obj_start then return nil end
    local depth, in_str, escaped = 0, false, false
    for i = obj_start, #html do
        local c = html:sub(i, i)
        if escaped then escaped = false
        elseif c == "\\" and in_str then escaped = true
        elseif c == '"' then in_str = not in_str
        elseif not in_str then
            if c == "{" then depth = depth + 1
            elseif c == "}" then
                depth = depth - 1
                if depth == 0 then
                    local chunk = html:sub(obj_start, i)
                    -- Convert JS to JSON: unquoted keys → quoted keys
                    chunk = chunk:gsub("([{,])%s*([%w_]+)%s*:", "%1\"%2\":")
                    chunk = chunk:gsub("'", '"')
                    local ok, val = pcall(json.decode, chunk)
                    if ok then return val end
                    return nil
                end
            end
        end
    end
    return nil
end

-- Extract <video> src from HTML (fallback for some pages)
local function extract_video_src(html)
    local src = html:match('<video[^>]+src="([^"]+)"')
    if src then return src end
    src = html:match('<source[^>]+src="([^"]+)"')
    return src
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://v%.douyu%.com/show/") then return true end
    if url:match("^https?://vmobile%.douyu%.com/show/") then return true end
    if url:match("^https?://douyu%.com/") then return true end
    if url:match("^https?://www%.douyu%.com/") then return true end
    if url:match("^https?://douyutv%.com/") then return true end
    if url:match("^https?://www%.douyutv%.com/") then return true end
    return false
end

function plugin.process(url)
    -- Detect VOD vs live
    local is_show = url:match("v%.douyu%.com/show/") or url:match("vmobile%.douyu%.com/show/")
    local is_live = url:match("douyu%.com/") or url:match("douyutv%.com/")

    if is_show then
        -- Normalize URL: replace vmobile. with v.
        local normalized = url:gsub("vmobile%.", "v.")
        local video_id = normalized:match("v%.douyu%.com/show/([%w]+)")
        if not video_id then
            ludo.logError("Douyu: could not extract video ID")
            return
        end

        ludo.logInfo("Douyu: fetching VOD " .. video_id)

        local body, status = http.get(normalized, {
            user_agent = UA, timeout = TIMEOUT,
            headers = { Referer = "https://v.douyu.com/", ["Accept-Language"] = "zh-CN,zh;q=0.9" },
        })
        if status ~= 200 then
            ludo.logError("Douyu: failed to fetch VOD page (HTTP " .. tostring(status) .. ")")
            return
        end

        -- Try to extract video info from page
        local data = extract_js_json(body, "$DATA")
        if data and data.DATA and data.ROOM then
            local content = data.DATA.content or {}
            local title = content.title or ("Douyu " .. video_id)
            local uploader = content.author or nil
            local duration = tonumber(content.video_duration)
            local thumbnail = content.video_pic

            ludo.logInfo("Douyu: \"" .. title .. "\""
                .. (uploader and (" by " .. uploader) or "")
                .. (duration and (" (" .. tostring(duration) .. "s)") or ""))

            -- The actual video URL requires JS signing. Log what we know.
            ludo.logInfo("Douyu: VOD download requires JS-based API signing")
            ludo.logInfo("Douyu: try using yt-dlp for this video")
            ludo.logInfo("Douyu: metadata extracted, but stream URL not accessible")
            return
        end

        -- Fallback: try direct video element in page
        local vid_src = extract_video_src(body)
        if vid_src then
            local title = body:match("<title>([^<]+)") or ("Douyu " .. video_id)
            title = title:gsub("%s*_%s*斗鱼", ""):gsub("^%s*(.-)%s*$", "%1")

            ludo.logInfo("Douyu: " .. title)
            local fname = safe_name(title) .. ".mp4"
            local _, dl_status, output = ludo.newDownload(
                vid_src, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, fname)
            if dl_status == 200 or dl_status == 206 or dl_status == 0 then
                ludo.logSuccess("Douyu: queued → " .. (output or fname))
            elseif dl_status == 403 then
                ludo.logSuccess("Douyu: queued (HEAD blocked) → " .. (output or fname))
            else
                ludo.logError("Douyu: preflight HTTP " .. tostring(dl_status))
            end
            return
        end

        ludo.logError("Douyu: could not extract video data from page")
        ludo.logInfo("Douyu: VODs on v.douyu.com require JS-based API signing")
        ludo.logInfo("Douyu: use yt-dlp or a browser for this video")

    elseif is_live then
        local video_id = url:match("douyu%.com/([%w_]+)$")
            or url:match("douyutv%.com/(%d+)")
        if not video_id then
            -- Try topic/query format: douyu.com/topic/xxx?rid=NUM
            video_id = url:match("[?&]rid=(%d+)")
                or url:match("douyu%.com/[^/]+/([%w_]+)")
        end
        if not video_id then
            ludo.logError("Douyu: could not extract room ID")
            return
        end

        ludo.logInfo("Douyu: live stream " .. video_id)
        ludo.logInfo("Douyu: Douyu live streams require JS-based API signing")
        ludo.logInfo("Douyu: use yt-dlp or Streamlink for live streaming")
    end
end

return plugin
