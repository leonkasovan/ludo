-- instagram.lua
--
-- Ludo plugin for downloading Instagram videos, reels, and carousels.
-- Handles: posts (/p/), IGTV (/tv/), Reels (/reel/, /reels/, /{user}/reel/).
--
-- AUTHENTICATION
--   Most Instagram content requires a logged-in session. To authenticate:
--     1. Export your Instagram browser cookies in Netscape (cookies.txt) format
--        (e.g. with the "Get cookies.txt LOCALLY" browser extension).
--     2. Save the file as:  <OutputDirectory>/instagram_cookies.txt
--   Ludo will automatically load those cookies before each request.
--
-- Based on the extraction logic from yt-dlp's instagram.py extractor.

local plugin = {
    name    = "Instagram",
    version = "20260404",
    creator = "GitHub Copilot",
}

local IG_HOME  = "https://www.instagram.com"
local API_BASE = "https://i.instagram.com/api/v1"
local APP_ID   = "936619743392459"

-- Desktop User-Agent for web requests
local DESKTOP_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    .. "AppleWebKit/537.36 (KHTML, like Gecko) "
    .. "Chrome/136.0.0.0 Safari/537.36"

local REQUEST_TIMEOUT = 20

local json = json or require("json")

-- ─── Arbitrary-precision decimal arithmetic ──────────────────────────────────
-- Needed to convert Instagram shortcodes (base-64) to numeric PKs without
-- losing precision (Lua 5.2 doubles only represent integers exactly up to 2^53,
-- but Instagram PKs can be ~18 decimal digits / ~60 bits).

-- Multiply the decimal-string n by integer mul and add integer add.
-- Returns the result as a decimal string.
local function bigint_muladd(n, mul, add)
    local digits = {}
    for i = 1, #n do
        digits[i] = n:byte(i) - 48  -- '0' == 48
    end
    local carry = add
    for i = #digits, 1, -1 do
        local d = digits[i] * mul + carry
        digits[i] = d % 10
        carry = math.floor(d / 10)
    end
    while carry > 0 do
        table.insert(digits, 1, carry % 10)
        carry = math.floor(carry / 10)
    end
    if #digits == 0 then return "0" end
    local buf = {}
    for _, d in ipairs(digits) do buf[#buf + 1] = string.char(d + 48) end
    return table.concat(buf)
end

-- ─── Instagram base-64 codec ─────────────────────────────────────────────────
-- Instagram encodes media IDs as base-64 shortcodes using this alphabet:
local ENCODING_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"

local CHAR_TO_IDX = {}
for i = 1, #ENCODING_CHARS do
    CHAR_TO_IDX[ENCODING_CHARS:sub(i, i)] = i - 1
end

-- Convert an Instagram shortcode to its numeric PK (returned as a decimal
-- string to preserve full precision).
-- Private posts have a "long" shortcode (> 28 chars) — strip the extra suffix.
local function shortcode_to_pk(shortcode)
    if type(shortcode) ~= "string" or shortcode == "" then return nil end
    if #shortcode > 28 then
        shortcode = shortcode:sub(1, #shortcode - 28)
    end
    local r = "0"
    for i = 1, #shortcode do
        local idx = CHAR_TO_IDX[shortcode:sub(i, i)]
        if idx == nil then return nil end
        r = bigint_muladd(r, 64, idx)
    end
    return r
end

-- ─── URL helpers ─────────────────────────────────────────────────────────────

-- Extract the shortcode from any supported Instagram URL.
local function extract_shortcode(url)
    -- /p/{id}  (regular post)
    -- /tv/{id}  (IGTV)
    -- /reel/{id} and /reels/{id}
    -- /{username}/reel/{id}
    return url:match("/p/([%w%-_]+)")
        or url:match("/tv/([%w%-_]+)")
        or url:match("/reels?/([%w%-_]+)")   -- matches /reel/ and /reels/
        or url:match("/[^/]+/reel/([%w%-_]+)")
end

-- ─── JSON extraction from HTML pages ─────────────────────────────────────────

-- Scan text for the first '{' after a Lua pattern match, then walk character
-- by character tracking brace/string depth to find the matching '}', and
-- return the parsed Lua table.  Returns nil if not found or not valid JSON.
local function extract_json_object(text, pattern)
    local _, pe = text:find(pattern)
    if not pe then return nil end

    local start = text:find("{", pe + 1)
    if not start then return nil end

    local depth   = 0
    local in_str  = false
    local escaped = false
    local i       = start

    while i <= #text do
        local c = text:sub(i, i)
        if escaped then
            escaped = false
        elseif c == "\\" and in_str then
            escaped = true
        elseif c == '"' then
            in_str = not in_str
        elseif not in_str then
            if c == "{" then
                depth = depth + 1
            elseif c == "}" then
                depth = depth - 1
                if depth == 0 then
                    local ok, val = pcall(json.decode, text:sub(start, i))
                    if ok then return val end
                    return nil
                end
            end
        end
        i = i + 1
    end
    return nil
end

-- ─── Cookie file helpers ──────────────────────────────────────────────────────

-- Read a cookie value from a Netscape-format cookie file.
-- Uses http.read_cookie (C-side helper) when available, falls back to pure Lua.
local read_cookie
if http and http.read_cookie then
    read_cookie = http.read_cookie
else
    read_cookie = function(filepath, name)
        local f = io.open(filepath, "r")
        if not f then return nil end
        local result = nil
        for line in f:lines() do
            if not line:match("^#") and line ~= "" then
                local cname, cval = line:match("\t([^\t]+)\t([^\t]*)$")
                if cname == name then
                    result = cval
                    break
                end
            end
        end
        f:close()
        return result
    end
end

-- Copy src file to dst.  Returns true on success.
local function copy_file(src, dst)
    local r = io.open(src, "rb")
    if not r then return false end
    local data = r:read("*a")
    r:close()
    local w = io.open(dst, "wb")
    if not w then return false end
    w:write(data)
    w:close()
    return true
end

-- ─── API request helpers ─────────────────────────────────────────────────────

local function api_headers(extra)
    local h = {
        ["X-IG-App-ID"]    = APP_ID,
        ["X-ASBD-ID"]      = "198387",
        ["X-IG-WWW-Claim"] = "0",
        ["Origin"]         = IG_HOME,
        ["Accept"]         = "*/*",
    }
    if extra then
        for k, v in pairs(extra) do h[k] = v end
    end
    return h
end

-- Call Instagram's ruling endpoint to prime the session cookie (csrftoken).
-- Returns the csrftoken string if found in the response, or nil.
local function setup_session(pk, session_file)
    local url = API_BASE
        .. "/web/get_ruling_for_content/?content_type=MEDIA&target_id=" .. pk
    local _, status = http.get(url, {
        user_agent = DESKTOP_UA,
        timeout    = REQUEST_TIMEOUT,
        headers    = api_headers(),
    })
    if status ~= 200 and status ~= 401 then
        return nil
    end
    -- The csrftoken is now in the cookie jar / session_file.
    return read_cookie(session_file, "csrftoken")
end

-- ─── Media download helpers ───────────────────────────────────────────────────

-- Pick the URL from video_versions with the largest pixel area.
local function best_video_url(versions)
    if not versions or #versions == 0 then return nil end
    local best, best_px = versions[1], 0
    for _, v in ipairs(versions) do
        local px = (v.width or 0) * (v.height or 0)
        if px > best_px then
            best    = v
            best_px = px
        end
    end
    return best and best.url
end

-- Sanitise a string for use as a filename segment.
local function safe_name(s)
    if not s then return "" end
    return (s:gsub("[^%w%-_]", "_"):sub(1, 48))
end

-- Build a descriptive filename: instagram_{shortcode}[_{idx}].mp4
local function make_filename(shortcode, idx)
    local base = "instagram_" .. safe_name(shortcode)
    if idx then base = base .. "_" .. idx end
    return base .. ".mp4"
end

-- Enqueue one video URL for downloading.
local function enqueue(video_url, filename)
    if not video_url or video_url == "" then return nil end
    -- Unescape \uXXXX sequences that Instagram embeds in JSON strings
    video_url = video_url:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp < 128 then return string.char(cp) end
        return "%" .. h  -- leave non-ASCII percent-encoded for the downloader
    end)
    local id, status, output = ludo.newDownload(
        video_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW, filename)
    if status == 200 or status == 206 then
        ludo.logSuccess("Instagram: queued → " .. (output or filename))
        return id
    else
        ludo.logError("Instagram: HTTP " .. tostring(status)
            .. " during preflight for " .. filename)
        return nil
    end
end

-- ─── Per-item processors (API/product format) ────────────────────────────────

-- Process a single product_media dict (one item from video_versions).
local function process_product_media(item, shortcode, idx)
    local url = best_video_url(item.video_versions)
    if not url then return false end
    return enqueue(url, make_filename(shortcode, idx)) ~= nil
end

-- Process a full API product_info response (may be a carousel).
-- Returns true if at least one download was queued.
local function process_api_item(info, shortcode)
    if not info or type(info) ~= "table" then return false end
    local code = info.code or shortcode

    -- Carousel post: multiple media items
    local carousel = info.carousel_media
    if carousel and type(carousel) == "table" and #carousel > 0 then
        local any = false
        for idx, item in ipairs(carousel) do
            if process_product_media(item, code, idx) then any = true end
        end
        return any
    end

    -- Single video
    return process_product_media(info, code)
end

-- ─── Extraction strategies ───────────────────────────────────────────────────

-- Strategy 1 – Private REST API
-- Requires a valid sessionid cookie (authenticated user only).
local function try_api(shortcode, pk)
    local url  = API_BASE .. "/media/" .. pk .. "/info/"
    local body, status = http.get(url, {
        user_agent = DESKTOP_UA,
        timeout    = REQUEST_TIMEOUT,
        headers    = api_headers(),
    })
    if status ~= 200 then return false end

    local ok, data = pcall(json.decode, body)
    if not ok or type(data) ~= "table" then return false end

    local items = data.items
    if not items or #items == 0 then return false end

    ludo.logInfo("Instagram: API strategy succeeded")
    return process_api_item(items[1], shortcode)
end

-- Strategy 2 – GraphQL query (doc_id 8845758582119845)
-- Often works for public content when a valid CSRF token is available.
local function try_graphql(shortcode, csrf_token)
    if not csrf_token or csrf_token == "" then return false end

    local variables = json.encode({
        shortcode             = shortcode,
        child_comment_count   = 3,
        fetch_comment_count   = 40,
        parent_comment_count  = 24,
        has_threaded_comments = true,
    })

    local qurl = IG_HOME .. "/graphql/query/"
        .. "?doc_id=8845758582119845"
        .. "&variables=" .. http.url_encode(variables)

    local body, status = http.get(qurl, {
        user_agent = DESKTOP_UA,
        timeout    = REQUEST_TIMEOUT,
        headers    = api_headers({
            ["X-CSRFToken"]       = csrf_token,
            ["X-Requested-With"]  = "XMLHttpRequest",
            ["Referer"]           = IG_HOME .. "/p/" .. shortcode .. "/",
        }),
    })

    if status ~= 200 then return false end

    local ok, data = pcall(json.decode, body)
    if not ok or type(data) ~= "table" then return false end

    local media = data.data and data.data.xdt_shortcode_media
    if not media or type(media) ~= "table" then return false end

    -- Carousel: edge_sidecar_to_children
    local edges = media.edge_sidecar_to_children
        and media.edge_sidecar_to_children.edges
    if edges and type(edges) == "table" and #edges > 0 then
        local any = false
        for idx, edge in ipairs(edges) do
            local node = edge.node
            if type(node) == "table"
                and (node.__typename == "GraphVideo" or node.is_video == true)
            then
                local vurl = node.video_url
                if vurl then
                    if enqueue(vurl, make_filename(shortcode, idx)) then
                        any = true
                    end
                end
            end
        end
        if any then
            ludo.logInfo("Instagram: GraphQL strategy succeeded (carousel)")
            return true
        end
    end

    -- Single video
    local video_url = media.video_url
    if video_url and video_url ~= "" then
        if enqueue(video_url, make_filename(shortcode)) then
            ludo.logInfo("Instagram: GraphQL strategy succeeded")
            return true
        end
    end

    return false
end

-- Strategy 3 – Embed page scraping (unauthenticated fallback)
-- Parses window.__additionalDataLoaded and window._sharedData from the
-- /{type}/{id}/embed/ page.  May be blocked or return no video for private posts.
local function try_embed(shortcode, original_url)
    -- Strip query string / fragment from original URL and append /embed/
    local base = (original_url:match("^([^?#]+)") or original_url)
    if base:sub(-1) ~= "/" then base = base .. "/" end
    local embed_url = base .. "embed/"

    local body, status = http.get(embed_url, {
        user_agent = DESKTOP_UA,
        timeout    = REQUEST_TIMEOUT,
        headers    = {
            ["Accept"]  = "text/html,application/xhtml+xml,*/*",
            ["Referer"] = original_url,
        },
    })

    if status ~= 200 then return false end

    -- 3a. window.__additionalDataLoaded('extra', { ... })
    --     Newer format: items[0]  |  Older format: graphql.shortcode_media
    local add_data = extract_json_object(
        body,
        "window%.__additionalDataLoaded%s*%(")
    if add_data and type(add_data) == "table" then
        local item = add_data.items and add_data.items[1]
        if item and process_api_item(item, shortcode) then
            ludo.logInfo("Instagram: embed/additionalData strategy succeeded")
            return true
        end
        local sm = add_data.graphql and add_data.graphql.shortcode_media
        if sm and type(sm) == "table" and sm.video_url then
            if enqueue(sm.video_url, make_filename(shortcode)) then
                ludo.logInfo("Instagram: embed/graphql strategy succeeded")
                return true
            end
        end
    end

    -- 3b. window._sharedData = { ... }
    local shared = extract_json_object(body, "window%._sharedData%s*=")
    if shared and type(shared) == "table" then
        local pp = shared.entry_data
            and shared.entry_data.PostPage
            and shared.entry_data.PostPage[1]
        local sm = pp and pp.graphql and pp.graphql.shortcode_media
        if sm and type(sm) == "table" and sm.video_url then
            if enqueue(sm.video_url, make_filename(shortcode)) then
                ludo.logInfo("Instagram: embed/_sharedData strategy succeeded")
                return true
            end
        end
    end

    -- 3c. Raw fallback: scrape the first <video src="..."> or "video_url":"..." in the HTML
    local video_url = body:match('<video[^>]+src="([^"]+)"')
        or body:match('"video_url"%s*:%s*"([^"]+)"')
    if video_url then
        if enqueue(video_url, make_filename(shortcode)) then
            ludo.logInfo("Instagram: embed/raw-scrape strategy succeeded")
            return true
        end
    end

    return false
end

-- ─── Plugin interface ─────────────────────────────────────────────────────────

function plugin.validate(url)
    if not url:match("^https?://") then return false end
    local host = (url:match("^https?://([^/?#]+)") or ""):lower()
    if host ~= "instagram.com" and host ~= "www.instagram.com" then
        return false
    end
    -- Accept post, IGTV, reel, reels, and /{user}/reel/ paths
    return url:match("/p/[%w%-_]+")     ~= nil
        or url:match("/tv/[%w%-_]+")    ~= nil
        or url:match("/reels?/[%w%-_]+") ~= nil  -- /reel/ and /reels/
        or url:match("/[^/]+/reel/[%w%-_]+") ~= nil
end

function plugin.process(url)
    -- ── 1. Extract shortcode and numeric PK ──────────────────────────────
    local shortcode = extract_shortcode(url)
    if not shortcode then
        ludo.logError("Instagram: could not parse shortcode from URL: " .. url)
        return nil
    end

    local pk = shortcode_to_pk(shortcode)
    if not pk then
        ludo.logError("Instagram: invalid shortcode characters in: " .. shortcode)
        return nil
    end

    ludo.logInfo("Instagram: processing shortcode=" .. shortcode)

    -- ── 2. Configure cookie storage ───────────────────────────────────────
    local outdir       = ludo.getOutputDirectory()
    local session_file = outdir .. "instagram_session.txt"
    local user_cookies = outdir .. "instagram_cookies.txt"

    -- If the user supplied a cookie file, seed the session file from it so
    -- their existing session is used and new cookies get appended there too.
    local uf = io.open(user_cookies, "r")
    if uf then
        uf:close()
        copy_file(user_cookies, session_file)
        ludo.logInfo("Instagram: loaded saved cookies from instagram_cookies.txt")
    end

    -- Point libcurl at our working session file.
    http.set_cookie(session_file)

    -- ── 3. Try private REST API (works when sessionid cookie is present) ──
    if try_api(shortcode, pk) then return end

    -- ── 4. Get CSRF token and try GraphQL query ───────────────────────────
    local csrf = setup_session(pk, session_file)
    if not csrf then
        -- setup_session may have set csrftoken without returning it; re-read.
        csrf = read_cookie(session_file, "csrftoken")
    end

    if try_graphql(shortcode, csrf) then return end

    -- ── 5. Embed page fallback (unauthenticated) ──────────────────────────
    if try_embed(shortcode, url) then return end

    -- ── 6. All strategies failed ──────────────────────────────────────────
    ludo.logError(
        "Instagram: could not extract video from " .. url
        .. ". The post may be private or rate-limited. "
        .. "For authenticated access, export your browser cookies to: "
        .. user_cookies)
end

return plugin
