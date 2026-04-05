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
    if url:match("/(pfbid[A-Za-z0-9]+)") then return true end
    return false
end

-- ─── Extraction Helpers ──────────────────────────────────────────────────────

-- Search the page HTML for the first occurrence of a named URL field inside
-- relay/JSON blobs (data-sjs blocks).  Returns the unescaped URL or nil.
local function find_relay_url(body, field_name)
    local pattern = '"' .. field_name .. '"%s*:%s*"(https?[^"]+)"'
    for raw in body:gmatch(pattern) do
        local u = unescape_json_url(raw)
        if u and u:find("^https?://") then
            return u
        end
    end
    return nil
end

-- Try the legacy VideoConfig JS object embedded on older Facebook pages.
-- Example: "hd_src":"https:\/\/..." or "sd_src":"https:\/\/..."
local function extract_videoconfig_url(body)
    local hd = body:match('"hd_src"%s*:%s*"(https?[^"]+)"')
    if hd then return unescape_json_url(hd) end
    local sd = body:match('"sd_src"%s*:%s*"(https?[^"]+)"')
    if sd then return unescape_json_url(sd) end
    return nil
end

-- ─── Process ─────────────────────────────────────────────────────────────────

function plugin.process(url)
    url = normalize_url(url)

    -- /reel/ID pages redirect to the watch endpoint — hit it directly
    local reel_id = url:match("/reel/(%d+)")
    if reel_id then
        url = ("https://www.facebook.com/watch/?v=%s"):format(reel_id)
    end

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

    -- Fetch the video page
    local body, status = http.get(url, {
        user_agent = DESKTOP_UA,
        timeout    = REQUEST_TIMEOUT,
        headers    = {
            ["Accept"]          = "text/html,application/xhtml+xml,*/*;q=0.8",
            ["Accept-Language"] = "en-US,en;q=0.9",
        },
    })

    if status ~= 200 then
        ludo.logError(("Facebook: HTTP %d fetching page"):format(status))
        return
    end

    -- Detect login wall early
    if body:find('id="login_form"') or body:find('"loginbutton"') or
       body:find(">You must log in to continue<") then
        ludo.logError(
            "Facebook: login required — place facebook_cookies.txt in the output directory")
        return
    end

    local video_url = nil
    local filename  = nil

    -- Strategy 1: Relay GraphQL prefetched data — HD source
    video_url = find_relay_url(body, "playable_url_quality_hd")
    if video_url then
        ludo.logInfo("Facebook: relay HD strategy succeeded")
        filename = "facebook_" .. video_id .. "_hd.mp4"
    end

    -- Strategy 2: Relay GraphQL prefetched data — SD source
    if not video_url then
        video_url = find_relay_url(body, "playable_url")
        if video_url then
            ludo.logInfo("Facebook: relay SD strategy succeeded")
            filename = "facebook_" .. video_id .. "_sd.mp4"
        end
    end

    -- Strategy 3: browser_native URLs embedded in relay data
    if not video_url then
        video_url = find_relay_url(body, "browser_native_hd_url")
            or find_relay_url(body, "browser_native_sd_url")
        if video_url then
            ludo.logInfo("Facebook: browser_native URL strategy succeeded")
            filename = "facebook_" .. video_id .. ".mp4"
        end
    end

    -- Strategy 4: Legacy server-side VideoConfig (very old pages)
    if not video_url then
        video_url = extract_videoconfig_url(body)
        if video_url then
            ludo.logInfo("Facebook: VideoConfig legacy strategy succeeded")
            filename = "facebook_" .. video_id .. ".mp4"
        end
    end

    -- Strategy 5: Raw <video src="..."> tag
    if not video_url then
        local raw = body:match('<video[^>]+src="(https?[^"]+)"')
        if raw then
            video_url = unescape_json_url(raw)
            ludo.logInfo("Facebook: HTML video tag strategy succeeded")
            filename = "facebook_" .. video_id .. ".mp4"
        end
    end

    if not video_url then
        ludo.logError("Facebook: all strategies failed for id=" .. video_id)
        return
    end

    local _, dl_status, output = ludo.newDownload(
        video_url, outdir, ludo.DOWNLOAD_NOW, filename)

    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Facebook: queued → " .. (output or filename))
    else
        ludo.logError(("Facebook: preflight HTTP %d for %s"):format(dl_status, filename))
    end
end

return plugin
