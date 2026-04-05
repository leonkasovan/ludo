-- twitter.lua
--
-- Ludo plugin for downloading Twitter (X) videos.
-- Tries multiple strategies: embedded JSON variants → direct video.twimg.com URLs → meta tags.
--
-- AUTHENTICATION
--   Some content may require login. To use cookies, export Netscape-format
--   cookies and save as: <OutputDirectory>/twitter_cookies.txt

local plugin = {
    name    = "Twitter",
    version = "20260405",
    creator = "GitHub Copilot",
}

local DESKTOP_UA      = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    .. "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
local REQUEST_TIMEOUT = 30

local json = json or require("json")

local function safe_name(s)
    return (s or ""):gsub("[^%w%-_]", "_"):sub(1, 80)
end

local function unescape_json_url(s)
    if not s then return nil end
    s = s:gsub("\\/", "/")
    s = s:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp and cp < 128 then return string.char(cp) end
        return ""
    end)
    return s
end

-- Balanced JSON extraction (returns decoded Lua table on success)
local function extract_json_object(text, pattern)
    local _, pe = text:find(pattern)
    if not pe then return nil end
    local start = text:find("{", pe + 1)
    if not start then return nil end
    local depth, in_str, escaped = 0, false, false
    for i = start, #text do
        local c = text:sub(i, i)
        if escaped then
            escaped = false
        elseif c == "\\" and in_str then
            escaped = true
        elseif c == '"' then
            in_str = not in_str
        elseif not in_str then
            if c == "{" then depth = depth + 1
            elseif c == "}" then
                depth = depth - 1
                if depth == 0 then
                    local ok, val = pcall(json.decode, text:sub(start, i))
                    if ok then return val end
                    return nil
                end
            end
        end
    end
    return nil
end

local function extract_tweet_id(url)
    if not url then return nil end
    -- common patterns
    local id = url:match("/status[s]?/(%d+)")
        or url:match("/i/web/status/(%d+)")
        or url:match("/i/videos/(%d+)")
        or url:match("[?&]id=(%d+)")
    if id then return id end
    -- fallback: last long numeric segment
    for seg in url:gmatch("/(%d+)") do
        if #seg >= 10 then id = seg end
    end
    return id
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if not url:match("https?://[%w%-%.]*twitter%.com/") and not url:match("https?://[%w%-%.]*x%.com/") then
        return false
    end
    if url:match("/status[s]?/%d+") or url:match("/i/web/status/%d+") or url:match("/i/videos/%d+") then
        return true
    end
    if url:match("/i/videos/") or url:match("/statuses/") then return true end
    return false
end

local function find_best_variant(variants)
    if type(variants) ~= "table" then return nil end
    local best = nil
    local best_br = -1
    for _, v in ipairs(variants) do
        if type(v) == "table" then
            local url = v.url or v['url']
            local bitrate = tonumber(v.bitrate) or tonumber(v['bitrate']) or -1
            if url and url:find("^https?://") and (bitrate > best_br or best_br < 0) then
                best = url
                best_br = bitrate
            end
        end
    end
    return best
end

local function find_video_url_from_body(body)
    if not body then return nil end

    -- 1) Try to extract JSON 'video_info' or 'extended_entities'
    local info = extract_json_object(body, '"video_info"%s*:')
    if info and type(info) == "table" then
        local variants = info.variants or info['variants']
        local v = find_best_variant(variants)
        if v then return unescape_json_url(v) end
    end

    local extent = extract_json_object(body, '"extended_entities"%s*:')
    if extent and type(extent) == "table" then
        -- find the first media entry with video and inspect its video_info.variants
        if type(extent.media) == "table" then
            for _, m in ipairs(extent.media) do
                local vi = m.video_info or m['video_info']
                if vi then
                    local v = find_best_variant(vi.variants)
                    if v then return unescape_json_url(v) end
                end
            end
        end
    end

    -- 2) direct variants array (as a bracketed JSON snippet somewhere)
    local var_json = body:match('"variants"%s*:%s*(%b[])')
    if var_json then
        local ok, tbl = pcall(json.decode, var_json)
        if ok and type(tbl) == "table" then
            local v = find_best_variant(tbl)
            if v then return unescape_json_url(v) end
        end
    end

    -- 2.5) Try Next.js payload `__NEXT_DATA__` or other embedded JSON
    local next_data = extract_json_object(body, 'id="__NEXT_DATA__"')
    if not next_data then
        next_data = extract_json_object(body, 'window%.__INITIAL_STATE__')
    end
    if next_data then
        local function find_video_in_table(o)
            if type(o) == 'string' and o:match('https?://video%.twimg%.com/%S+') then
                return o
            end
            if type(o) == 'table' then
                for _, v in pairs(o) do
                    local res = find_video_in_table(v)
                    if res then return res end
                end
            end
            return nil
        end
        local ndv = find_video_in_table(next_data)
        if ndv then return unescape_json_url(ndv) end
    end

    -- 3) direct video.twimg.com URL (simple non-space match)
    local v = body:match('(https?://video%.twimg%.com/%S+)')
    if v and v:find("%.mp4") then return unescape_json_url(v) end
    if v then return unescape_json_url(v) end

    -- 4) meta tags: og:video, og:video:secure_url, twitter:player:stream
    local meta = body:match('<meta[^>]+property="og:video:secure_url"[^>]+content="([^"]+)"')
        or body:match('<meta[^>]+property="og:video"[^>]+content="([^"]+)"')
        or body:match('<meta[^>]+name="twitter:player:stream"[^>]+content="([^"]+)"')
        or body:match('<meta[^>]+property="og:video:url"[^>]+content="([^"]+)"')
    if meta then return unescape_json_url(meta) end

    return nil
end

local function enqueue(video_url, filename)
    if not video_url or video_url == "" then return nil end
    local id, status, output = ludo.newDownload(video_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, filename)
    if status == 200 or status == 206 or status == 0 then
        ludo.logSuccess("Twitter: queued → " .. (output or filename))
        return id
    else
        ludo.logError("Twitter: preflight HTTP " .. tostring(status) .. " for " .. filename)
        return nil
    end
end

function plugin.process(url)
    local tweet_id = extract_tweet_id(url)
    if not tweet_id then
        ludo.logError("Twitter: cannot extract tweet ID from " .. tostring(url))
        return
    end
    ludo.logInfo("Twitter: processing id=" .. tweet_id)

    local outdir = ludo.getOutputDirectory()
    local cookie_path = outdir .. "/twitter_cookies.txt"
    local ck = io.open(cookie_path, "r")
    if ck then
        ck:close()
        http.set_cookie(cookie_path)
        ludo.logInfo("Twitter: using cookie file: " .. cookie_path)
    end

    local body, status = http.get(url, { user_agent = DESKTOP_UA, timeout = REQUEST_TIMEOUT })
    if status ~= 200 then
        ludo.logError(("Twitter: HTTP %d fetching page"):format(status))
        return
    end

    local video_url = find_video_url_from_body(body)

    if not video_url then
        ludo.logError("Twitter: failed to extract video URL for id=" .. tweet_id)
        return
    end

    -- Suggest filename using tweet id and page title
    local title = body:match('<meta[^>]+property="og:title"[^>]+content="([^"]+)"')
        or body:match('<title>(.-)</title>')
    local base = (title and #title > 2) and safe_name(title) or ("twitter_" .. tweet_id)
    local filename = base .. "_" .. tweet_id .. ".mp4"

    enqueue(video_url, filename)
end

return plugin
