-- facebook.lua
--
-- Ludo plugin for downloading Facebook videos.
-- Handles: /videos/ID,  /watch/?v=ID,  video.php?v=ID,  /reel/ID,
--          story.php?story_fbid=,  permalink.php,  m.facebook.com URLs.
--
-- AUTHENTICATION
--   Most Facebook content requires a logged-in session. To authenticate:
--     1. Export your Facebook browser cookies in Netscape (cookies.txt) format
--        (e.g. with the "Get cookies.txt LOCALLY" browser extension).
--     2. Save the file as:  <OutputDirectory>/facebook_cookies.txt
--   Ludo will automatically load those cookies before each request.
--
-- Based on the extraction logic from yt-dlp's facebook.py extractor.

local plugin = {
    name    = "Facebook",
    version = "20260405",
    creator = "GitHub Copilot",
}

local FB_HOME         = "https://www.facebook.com"
local DESKTOP_UA      = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    .. "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
local REQUEST_TIMEOUT = 30

local json = json or require("json")

-- ─── Helpers ──────────────────────────────────────────────────────────────────

local function safe_name(s)
    return (s or ""):gsub("[^%w%-_]", "_"):sub(1, 80)
end

-- Unescape JSON string escapes that appear in embedded JavaScript/JSON blobs.
-- Primarily handles \/ → / and \uXXXX → char (ASCII range only).
local function unescape_json_url(s)
    if not s then return nil end
    s = s:gsub("\\/", "/")
    s = s:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp < 128 then return string.char(cp) end
        return ""
    end)
    return s
end

-- Normalize encoded fragments we commonly see in relay/config blobs.
local function cleanup_extracted_url(s)
    if not s then return nil end
    s = unescape_json_url(s)
    s = s:gsub("&amp;", "&")
    s = s:gsub("<[^>]*$", "")
    if s:find("^https?://") then
        return s
    end
    return nil
end

-- ─── URL Utilities ────────────────────────────────────────────────────────────

-- Normalize Facebook URL:
--   m.facebook.com  → www.facebook.com
--   *.facebook.com  → www.facebook.com
local function normalize_url(url)
    return (url or ""):gsub("://[%w%-%.]*facebook%.com/", "://www.facebook.com/")
end

-- Extract the primary video/post identifier from a Facebook URL.
-- Returns the ID string (numeric or pfbid-prefixed) or nil.
local function extract_video_id(url)
    -- /videos/VIDEO_ID/
    local vid = url:match("/videos/(%d+)")
    if vid then return vid end

    -- /reel/ID
    vid = url:match("/reel/(%d+)")
    if vid then return vid end

    -- ?v=ID  or  ?video_id=ID  (watch pages, video.php)
    vid = url:match("[?&]v=(%d+)")
        or url:match("[?&]video_id=(%d+)")
    if vid then return vid end

    -- story.php?story_fbid=pfbid...  or  ?story_fbid=DIGITS
    vid = url:match("[?&]story_fbid=(pfbid[A-Za-z0-9]+)")
        or url:match("[?&]story_fbid=(%d+)")
    if vid then return vid end

    -- permalink.php?story_fbid=pfbid...
    vid = url:match("[?&]id=(pfbid[A-Za-z0-9]+)")
    if vid then return vid end

    -- pfbid anywhere in path (posts, permalink)
    vid = url:match("/(pfbid[A-Za-z0-9]+)")
    if vid then return vid end

    -- /watchparty/ID
    vid = url:match("/watchparty/(%d+)")
    if vid then return vid end

    -- Last numeric sequence of ≥10 digits in the path (fallback)
    for v in url:gmatch("/(%d+)") do
        if #v >= 10 then vid = v end
    end
    return vid
end

-- ─── Validate ────────────────────────────────────────────────────────────────

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    -- Must be a facebook.com domain
    if not url:match("https?://[%w%-%.]*facebook%.com/") then
        return false
    end
    -- Must match a known video path pattern
    if url:match("/videos/%d") then return true end
    if url:match("/video%.php%?") then return true end
    if url:match("/watch%?") then return true end
    if url:match("/watch/live%?") then return true end
    if url:match("/reel/%d") then return true end
    if url:match("/story%.php%?") and url:match("story_fbid=") then return true end
    if url:match("/permalink%.php%?") and url:match("story_fbid=") then return true end
    if url:match("[?&]v=%d") then return true end
    if url:match("[?&]video_id=%d") then return true end
    if url:match("/watchparty/%d") then return true end
    -- groups/.../permalink/ID or groups/.../posts/ID
    if url:match("/groups/[^/]+/permalink/") then return true end
    if url:match("/groups/[^/]+/posts/") then return true end
    -- posts/ID that may contain video
    if url:match("/posts/%d") then return true end
    -- share URLs (e.g. /share/v/ID or /share/r/ID)
    if url:match("/share/[%w]/[^/]+") then return true end
    if url:match("/(pfbid[A-Za-z0-9]+)") then return true end
    return false
end

-- ─── Extraction Helpers ──────────────────────────────────────────────────────

-- Search the page HTML for the first occurrence of a named URL field inside
-- relay/JSON blobs (data-sjs blocks).  Returns the unescaped URL or nil.
local function find_relay_url(body, field_name)
    local pattern = '"' .. field_name .. '"%s*:%s*"(https?[^"]+)"'
    for raw in body:gmatch(pattern) do
        local u = cleanup_extracted_url(raw)
        if u then
            return u
        end
    end
    return nil
end

-- Fallback for newer pages that only expose progressive URLs in delivery blobs.
local function find_progressive_mp4_url(body)
    local direct = find_relay_url(body, "progressive_url")
    if direct then return direct end

    local best, best_score = nil, -99999
    for raw in body:gmatch('"(https?[^"]-fbcdn[^"]-%.mp4[^"]-)"') do
        local u = cleanup_extracted_url(raw)
        if u then
            local score = 0
            if u:find("tag=sve") or u:find("xpv_progressive") then score = score + 50 end
            if u:find("bitrate=") then score = score + 5 end
            if u:find("720") then score = score + 3 end
            if u:find("540") then score = score + 2 end
            if u:find("360") then score = score + 1 end
            if u:find("audio") then score = score - 30 end
            if u:find("dash") then score = score - 10 end
            if score > best_score then
                best = u
                best_score = score
            end
        end
    end
    return best
end

-- Try the legacy VideoConfig JS object embedded on older Facebook pages.
-- Example: "hd_src":"https:\/\/..." or "sd_src":"https:\/\/..."
local function extract_videoconfig_url(body)
    local hd = body:match('"hd_src"%s*:%s*"(https?[^"]+)"')
    if hd then return cleanup_extracted_url(hd) end
    local sd = body:match('"sd_src"%s*:%s*"(https?[^"]+)"')
    if sd then return cleanup_extracted_url(sd) end
    return nil
end

-- Try all extraction strategies against a fetched page body.
-- Returns video_url, filename_suffix (e.g. "_hd" or "_sd") or nil.
local function extract_video_from_body(body)
    local video_url = nil
    local suffix = ""

    -- Strategy 1: Relay GraphQL prefetched data — HD source
    video_url = find_relay_url(body, "playable_url_quality_hd")
    if video_url then return video_url, "_hd" end

    -- Strategy 2: Relay GraphQL prefetched data — SD source
    video_url = find_relay_url(body, "playable_url")
    if video_url then return video_url, "_sd" end

    -- Strategy 3: browser_native URLs embedded in relay data
    video_url = find_relay_url(body, "browser_native_hd_url")
        or find_relay_url(body, "browser_native_sd_url")
    if video_url then return video_url, suffix end

    -- Strategy 4: Legacy server-side VideoConfig (very old pages)
    video_url = extract_videoconfig_url(body)
    if video_url then return video_url, suffix end

    -- Strategy 5: New delivery blobs (progressive_url / fbcdn mp4)
    video_url = find_progressive_mp4_url(body)
    if video_url then return video_url, suffix end

    -- Strategy 6: Raw <video src="..."> tag
    local raw = body:match('<video[^>]+src="(https?[^"]+)"')
    if raw then
        return cleanup_extracted_url(raw), suffix
    end

    return nil, nil
end

-- ─── Process ─────────────────────────────────────────────────────────────────

function plugin.process(url)
    url = normalize_url(url)
    
    -- check if the URL is a /share/v/ID or /share/r/ID format and rewrite to watch.php?v=ID
    local share_match = url:match("/share/[%w]/([^/?]+)")
    if share_match then
        local body, status, headers = http.get(url)
        -- check headers for a Location redirect to extract the video ID
        if headers and headers["Location"] then
            url = headers["Location"]
        else
            ludo.logError("Facebook: no redirect found for share URL")
            return
        end
    end

    -- Build a watch fallback for reel URLs, but keep the original URL first.
    local reel_id = url:match("/reel/(%d+)")
    local reel_watch_url = nil
    if reel_id then reel_watch_url = ("https://www.facebook.com/watch/?v=%s&_rdr"):format(reel_id) end

    local video_id = extract_video_id(url)
    if not video_id then
        ludo.logError("Facebook: cannot extract video ID from " .. url)
        return
    end

    ludo.logInfo("Facebook: processing id=" .. video_id)

    local outdir = ludo.getOutputDirectory()

    -- Cookie jar support: load facebook_cookies.txt if present in output dir
    local cookie_path = outdir .. "/facebook_cookies.txt"
    local fck = io.open(cookie_path, "r")
    if fck then
        fck:close()
        http.set_cookie(cookie_path)
        ludo.logInfo("Facebook: using cookie file: " .. cookie_path)
    end

    local video_url = nil
    local filename  = nil
    local saw_login_wall = false
    local saw_reel_block = false

    local candidates = { url }
    if reel_watch_url and reel_watch_url ~= url then
        table.insert(candidates, reel_watch_url)
    end

    for _, candidate in ipairs(candidates) do
        ludo.logInfo("Facebook: trying page " .. candidate)

        local req_headers = {
            ["Accept"]          = "text/html,application/xhtml+xml,*/*;q=0.8",
            ["Accept-Language"] = "en-US,en;q=0.9",
            ["Sec-Fetch-Mode"]  = "navigate",
        }
        if reel_id and reel_watch_url and candidate == reel_watch_url then
            req_headers["Referer"] = ("https://www.facebook.com/reel/%s"):format(reel_id)
        end

        local body, status = http.get(candidate, {
            user_agent = DESKTOP_UA,
            timeout    = REQUEST_TIMEOUT,
            headers    = req_headers,
        })

        if status == 200 then
            if body:find('id="login_form"') or body:find('"loginbutton"') or
               body:find(">You must log in to continue<") then
                saw_login_wall = true
            else
                local found_url, suffix = extract_video_from_body(body)
                if found_url then
                    video_url = found_url
                    filename = "facebook_" .. video_id .. (suffix or "") .. ".mp4"
                    ludo.logInfo("Facebook: extraction succeeded from " .. candidate)
                    break
                end
            end
        else
            ludo.logInfo(("Facebook: HTTP %d on %s"):format(status, candidate))
            if reel_id and candidate:find("/reel/") and (status == 400 or status == 403) then
                saw_reel_block = true
            end
        end
    end

    if not video_url and saw_login_wall then
        ludo.logError(
            "Facebook: login required — place facebook_cookies.txt in the output directory")
        return
    end

    if not video_url then
        if saw_reel_block then
            ludo.logError(
                "Facebook: reel request blocked (HTTP 400/403). Try facebook_cookies.txt or a different IP/proxy.")
            return
        end
        ludo.logError("Facebook: all strategies failed for id=" .. video_id)
        return
    end

    local _, dl_status, output = ludo.newDownload(
        video_url, outdir, ludo.DOWNLOAD_NOW, filename,
        {
            ["User-Agent"] = "facebookexternalhit/1.1",
            ["Referer"] = reel_watch_url or url,
        })

    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Facebook: queued → " .. (output or filename))
    else
        ludo.logError(("Facebook: preflight HTTP %d for %s"):format(dl_status, filename))
    end
end

return plugin
