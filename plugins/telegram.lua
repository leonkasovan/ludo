-- telegram.lua
-- Ludo plugin for downloading Telegram video posts.
-- Ported from yt-dlp telegram.py extractor.
-- Handles: t.me/CHANNEL/MSG_ID

local plugin = { name = "Telegram", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Clean HTML: remove tags and collapse whitespace
local function clean_html(s)
    if type(s) ~= "string" then return nil end
    s = s:gsub("<[^>]+>", "")
    s = s:gsub("&amp;", "&")
    s = s:gsub("&lt;", "<")
    s = s:gsub("&gt;", ">")
    s = s:gsub("&quot;", '"')
    s = s:gsub("&#39;", "'")
    s = s:gsub("&nbsp;", " ")
    s = s:gsub("%s+", " ")
    s = s:gsub("^%s*(.-)%s*$", "%1")
    return s ~= "" and s or nil
end

-- Extract content of an element by class name from HTML
local function get_by_class(html, class_name)
    local open_pat = '<[^>]+class="[^"]*' .. class_name .. '[^"]*"'
    local _, tag_end = html:find(open_pat)
    if not tag_end then return nil end
    -- Skip past opening tag
    local _, close_gt = html:find(">", tag_end)
    if not close_gt then return nil end
    -- Find the matching closing tag (assumes no nesting beyond 3 levels)
    local depth = 0
    local start = close_gt + 1
    for i = start, #html do
        if html:sub(i, i) == "<" then
            if html:sub(i, i + 1) == "</" then
                local _ce = html:find(">", i)
                if _ce then
                    if depth == 0 then
                        return html:sub(start, i - 1):gsub("^%s*(.-)%s*$", "%1")
                    end
                    depth = depth - 1
                    i = _ce
                end
            else
                depth = depth + 1
                local _ce2 = html:find(">", i)
                if _ce2 then i = _ce2 end
            end
        end
    end
    return nil
end

-- Parse HH:MM:SS or MM:SS duration to seconds
local function parse_duration(str)
    if type(str) ~= "string" then return nil end
    local h, m, s = str:match("^(%d+):(%d+):(%d+)$")
    if h then return tonumber(h) * 3600 + tonumber(m) * 60 + tonumber(s) end
    local m2, s2 = str:match("^(%d+):(%d+)$")
    if m2 then return tonumber(m2) * 60 + tonumber(s2) end
    local s3 = tonumber(str:match("^(%d+)$"))
    return s3
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    -- https://t.me/CHANNEL/MSG_ID
    if url:match("^https?://t%.me/[^/]+/%d+") then return true end
    return false
end

function plugin.process(url)
    local channel_id, msg_id = url:match("t%.me/([^/]+)/(%d+)")
    if not channel_id or not msg_id then
        ludo.logError("Telegram: could not parse channel/message ID")
        return
    end

    ludo.logInfo("Telegram: fetching embed for " .. channel_id .. "/" .. msg_id)

    -- Fetch the embed page with ?embed=1&single
    local embed_url = url:gsub("%?.*$", "")  -- strip existing query
    local body, status = http.get(embed_url .. "?embed=1&single=", {
        user_agent = UA, timeout = TIMEOUT,
    })
    if status ~= 200 then
        ludo.logError("Telegram: failed to fetch embed (HTTP " .. tostring(status) .. ")")
        return
    end

    -- Extract metadata
    local description = clean_html(get_by_class(body, "tgme_widget_message_text"))
    local channel = clean_html(get_by_class(body, "tgme_widget_message_author"))
    local timestamp_str = body:match('<time[^>]+datetime="([^"]+)"')
    local timestamp = 0
    if timestamp_str then
        -- Parse ISO 8601
        local y, mo, d, h, mi, s = timestamp_str:match("(%d+)%-(%d+)%-(%d+)T(%d+):(%d+):(%d+)")
        if y then
            timestamp = os.time({ year = y, month = mo, day = d, hour = h, min = mi, sec = s })
        end
    end

    local title = description or ("Telegram " .. channel_id .. " " .. msg_id)

    ludo.logInfo("Telegram: " .. (channel or channel_id) .. " — \"" .. title:sub(1, 72) .. "\"")

    -- Extract all video players
    local video_count = 0
    local vid_start = 1
    local outdir = ludo.getOutputDirectory()

    while true do
        local _, pe = body:find('tgme_widget_message_video_player', vid_start)
        if not pe then break end

        -- Find the enclosing <a> tag (starts before the class)
        local a_start = body:sub(1, pe):find('<a[^>]*$')
        if not a_start then
            -- Search backwards from pe
            a_start = body:sub(1, pe):match(".*<a[^>]*")
            if not a_start then a_start = 1 end
        end
        local a_href = body:match('href="([^"]+)"', a_start)
        local _, a_close = body:find("</time>", pe)
        if not a_close then break end

        vid_start = a_close + 1
        local video_block = body:sub(a_start, a_close + 6) -- include </time>

        -- Extract video URL
        local video_url = video_block:match('<video[^>]+src="([^"]+)"')
        if not video_url then goto next_video end

        -- Extract duration
        local dur_str = video_block:match('<time[^>]+duration[^>]*>([%d:]+)</time>')
        local duration = parse_duration(dur_str)

        -- Extract thumbnail
        local thumb = video_block:match("tgme_widget_message_video_thumb[^}]+background%-image:url%('([^']+)'%)")

        -- If we have a href, use it as the ID; otherwise use msg_id
        local vid_id = a_href and a_href:match("/(%d+)") or msg_id

        video_count = video_count + 1

        -- Build filename: channel_msg_id_videoNumber.mp4
        local fname = safe_name(channel or channel_id) .. "_" .. tostring(msg_id)
        if video_count > 1 then fname = fname .. "_v" .. tostring(video_count) end
        fname = fname .. ".mp4"

        ludo.logInfo(("Telegram: [%d] video %ss"):format(video_count, duration or "?"))

        local _, dl_status, output = ludo.newDownload(
            video_url, outdir, ludo.DOWNLOAD_NOW, fname,
            { Referer = "https://t.me/" })
        if dl_status == 200 or dl_status == 206 or dl_status == 0 then
            ludo.logInfo("  Queued → " .. (output or fname))
        elseif dl_status == 403 then
            ludo.logInfo("  Queued (HEAD blocked) → " .. (output or fname))
        else
            ludo.logInfo("  Queued (preflight HTTP " .. tostring(dl_status) .. ") → " .. (output or fname))
        end

        ::next_video::
    end

    if video_count == 0 then
        ludo.logError("Telegram: no videos found in post")
        return
    end

    ludo.logSuccess(("Telegram: queued %d video(s) from %s/%s"):format(
        video_count, channel or channel_id, msg_id))
end

return plugin
