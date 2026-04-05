-- tiktok.lua
--
-- Ludo plugin for downloading TikTok videos.
-- Handles: https://www.tiktok.com/@user/video/VIDEO_ID
--          https://www.tiktok.com/embed/VIDEO_ID
--
-- AUTHENTICATION
--   Public videos do not require cookies.
--   For restricted/following-only content, export your TikTok cookies in
--   Netscape format and save as: <OutputDirectory>/tiktok_cookies.txt
--
-- WAF CHALLENGE BYPASS
--   TikTok serves a "Please wait..." bot-check page when it detects a
--   non-browser TLS fingerprint (X-TT-System-Error: 3, ~650 bytes).
--   The plugin detects this and solves the embedded SHA-256 proof-of-work
--   challenge (technique from yt-dlp tiktok.py _solve_challenge_and_set_cookies).
--   See: plugins/tiktok.lua solve_waf_challenge()
--
-- NOTE: TikTok's play URLs carry short-lived signed tokens. The download is
-- started immediately after extracting the URL to avoid token expiry.
--
-- Based on the extraction logic from yt-dlp's tiktok.py extractor.

local plugin = {
    name    = "TikTok",
    version = "20260405",
    creator = "GitHub Copilot",
}

local TIKTOK_HOME     = "https://www.tiktok.com"
local DESKTOP_UA      = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    .. "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
local REQUEST_TIMEOUT = 30

local json = json or require("json")

-- ─── Helpers ──────────────────────────────────────────────────────────────────

-- Sanitize a string for use as a filename component.
local function safe_name(s)
    return (s or ""):gsub("[^%w%-_]", "_"):sub(1, 60)
end

-- Unescape JSON string escapes (\/ → /, \uXXXX → char).
local function unescape_url(s)
    if not s then return nil end
    s = s:gsub("\\/", "/")
    s = s:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp < 128 then return string.char(cp) end
        return ""
    end)
    return s
end

-- Extract the content of a <script id="ID_PATTERN"> element and decode it as
-- a JSON object.  Returns the decoded Lua table or nil.
local function extract_script_json(html, id_pattern)
    -- Match the opening script tag with the given id attribute
    local open_pat = '<script[^>]+id="' .. id_pattern .. '"[^>]*>'
    local _, tag_end = html:find(open_pat)
    if not tag_end then return nil end

    local rest  = html:sub(tag_end + 1)
    local start = rest:find("{")
    if not start then return nil end

    -- Walk characters to find the matching closing brace
    local depth   = 0
    local in_str  = false
    local escaped = false

    for i = start, #rest do
        local c = rest:sub(i, i)
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
                    local ok, val = pcall(json.decode, rest:sub(start, i))
                    if ok then return val end
                    return nil
                end
            end
        end
    end
    return nil
end

-- ─── Video-URL Extraction from JSON Structures ───────────────────────────────

-- Return the most-playable video URL from a TikTok web-format video object.
-- Prefers bitrateInfo (multiple quality levels) → playAddr → downloadAddr.
local function find_play_url(video)
    if type(video) ~= "table" then return nil end

    -- bitrateInfo[].PlayAddr.UrlList — prefer highest quality (first entry)
    local bi = video.bitrateInfo
    if type(bi) == "table" and #bi > 0 then
        for _, rate in ipairs(bi) do
            local pa = rate.PlayAddr
            if pa and type(pa.UrlList) == "table" then
                for _, u in ipairs(pa.UrlList) do
                    if type(u) == "string" and u:find("^https?://") then
                        return u
                    end
                end
            end
        end
    end

    -- playAddr is a direct URL string in newer web JSON
    if type(video.playAddr) == "string" and video.playAddr:find("^https?://") then
        return video.playAddr
    end

    -- downloadAddr fallback (may be watermarked)
    if type(video.downloadAddr) == "string" and video.downloadAddr:find("^https?://") then
        return video.downloadAddr
    end

    return nil
end

-- Navigate SIGI_STATE → ItemModule[video_id] entry.
local function item_from_sigi(sigi, video_id)
    local im = sigi and sigi.ItemModule
    if im and im[video_id] then return im[video_id] end
    return nil
end

-- Navigate __UNIVERSAL_DATA_FOR_REHYDRATION__ → itemStruct entry.
local function item_from_universal(udata, video_id)
    local scope = udata and udata.__DEFAULT_SCOPE__
    if not scope then return nil end
    local vd = scope["webapp.video-detail"]
    if not vd then return nil end
    local ii = vd.itemInfo
    if not ii then return nil end
    local item = ii.itemStruct
    if item and (video_id == nil or item.id == video_id) then return item end
    return nil
end

-- ─── WAF Challenge Bypass ─────────────────────────────────────────────────────
--
-- TikTok challenge page structure (id= attributes on <p> elements):
--   id="cs"  class="BASE64_JSON"   — challenge params after base64+JSON decode
--   id="wci" class="COOKIE_NAME"   — name of the WCI challenge cookie
--   id="rci" class="RCI_NAME"      — optional: rci cookie name
--   id="rs"  class="RCI_VALUE"     — optional: rci cookie value (raw, not base64)
--
-- challenge JSON: { "v":{"a":"<b64>","b":<ts>,"c":"<b64>"},"s":"<b64>" }
-- Algorithm: find i in [0,1000000] s.t. SHA256(b64decode(v.a)+tostring(i)) == b64decode(v.c)
-- Solution:  challenge.d = base64_encode(tostring(i))
-- Cookie:    wci_name = base64_encode(json_encode(challenge))

local function element_class(html, id)
    -- Try id= then class= attribute order
    local v = html:match('<[%a]+[^>]+id="' .. id .. '"[^>]+class="([^"]*)"')
    if v then return v end
    -- Try class= then id= attribute order
    v = html:match('<[%a]+[^>]+class="([^"]*)"[^>]+id="' .. id .. '"')
    return v
end

local function solve_waf_challenge(body)
    local cs_raw = element_class(body, "cs")
    if not cs_raw or cs_raw == "" then
        ludo.logError("TikTok WAF: <*  id=\"cs\"> class not found in page")
        return nil
    end

    -- Restore any stripped base64 padding
    local padded = cs_raw
    local rem = #padded % 4
    if rem > 0 then padded = padded .. string.rep("=", 4 - rem) end

    local cs_bytes = http.base64_decode(padded)
    local ok, challenge = pcall(json.decode, cs_bytes)
    if not ok or type(challenge) ~= "table" then
        ludo.logError("TikTok WAF: challenge JSON decode failed: " .. tostring(challenge))
        return nil
    end

    local v = challenge.v
    if type(v) ~= "table" or not v.a or not v.c then
        ludo.logError("TikTok WAF: unexpected challenge structure (v.a/v.c missing)")
        return nil
    end

    -- Proof-of-work: find i where SHA256(b64decode(v.a) .. tostring(i)) == b64decode(v.c)
    local prefix   = http.base64_decode(v.a)
    local expected = http.base64_decode(v.c)

    ludo.logInfo("TikTok WAF: solving SHA-256 PoW (max 1 000 000 iterations) ...")
    local d_value = nil
    for i = 0, 1000000 do
        if http.sha256(prefix .. tostring(i)) == expected then
            d_value = http.base64_encode(tostring(i))
            ludo.logInfo("TikTok WAF: solution found at i=" .. i)
            break
        end
    end

    if not d_value then
        ludo.logError("TikTok WAF: no solution in [0,1000000]")
        return nil
    end

    -- Build WCI cookie value: base64(json({...challenge...}))
    challenge.d = d_value
    local ok2, cookie_json = pcall(json.encode, challenge)
    if not ok2 then
        ludo.logError("TikTok WAF: json.encode failed: " .. tostring(cookie_json))
        return nil
    end
    local wci_value = http.base64_encode(cookie_json)
    local wci_name  = element_class(body, "wci") or "_wafchallengeid"
    local rci_name  = element_class(body, "rci")
    local rci_value = element_class(body, "rs")

    ludo.logInfo("TikTok WAF: wci_name=" .. wci_name)
    return wci_name, wci_value, rci_name, rci_value
end

-- ─── Validate ────────────────────────────────────────────────────────────────

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    -- Standard video page: @user/video/ID or embed/ID
    if url:match("https?://www%.tiktok%.com/@[^/]+/video/%d+") then return true end
    if url:match("https?://www%.tiktok%.com/embed/%d+") then return true end
    return false
end

-- ─── Process ─────────────────────────────────────────────────────────────────

function plugin.process(url)
    -- Extract numeric video ID
    local video_id = url:match("/video/(%d+)") or url:match("/embed/(%d+)")
    if not video_id then
        ludo.logError("TikTok: cannot extract video ID from " .. url)
        return
    end

    -- Build canonical URL (use original @user path when available for nicer logs)
    local canon_url = url:match("^(https?://www%.tiktok%.com/@[^/?#]+/video/%d+)")
        or ("https://www.tiktok.com/@_/video/%s"):format(video_id)

    ludo.logInfo("TikTok: processing video_id=" .. video_id)

    local outdir = ludo.getOutputDirectory()

    -- Cookie jar: always set the path so server-set cookies (e.g. tt_chain_token)
    -- are persisted back to the file after each http.get call.
    local cookie_path = outdir .. "/tiktok_cookies.txt"
    http.set_cookie(cookie_path)
    local ck = io.open(cookie_path, "r")
    if ck then
        ck:close()
        ludo.logInfo("TikTok: using cookie file: " .. cookie_path)
    end

    -- Fetch the video page with browser-like headers
    local body, status = http.get(canon_url, {
        user_agent = DESKTOP_UA,
        timeout    = REQUEST_TIMEOUT,
        headers    = {
            ["Accept"]          = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            ["Accept-Language"] = "en-US,en;q=0.9",
            ["Referer"]         = TIKTOK_HOME .. "/",
            ["Sec-Fetch-Dest"]  = "document",
            ["Sec-Fetch-Mode"]  = "navigate",
        },
    })

    if status ~= 200 then
        ludo.logError(("TikTok: HTTP %d fetching page"):format(status))
        return
    end

    -- ── WAF challenge detection & bypass ─────────────────────────────────────
    -- WAF challenge page is ~650 bytes and contains <p id="cs" class="BASE64">
    if #body < 3000 and body:find('id="cs"') then
        ludo.logInfo(("TikTok: WAF challenge detected (%d bytes), solving ..."):format(#body))

        -- Save debug copy for analysis
        local dbg = io.open(outdir .. "/tiktok_debug_" .. video_id .. "_waf.html", "w")
        if dbg then dbg:write(body); dbg:close() end

        local wci_name, wci_value, rci_name, rci_value = solve_waf_challenge(body)
        if not wci_name then
            ludo.logError("TikTok: WAF bypass failed — try refreshing " .. cookie_path)
            return
        end

        -- Build Cookie header string with WAF solution
        local cookie_parts = { wci_name .. "=" .. wci_value }
        if rci_name and rci_value and rci_name ~= "" and rci_value ~= "" then
            cookie_parts[#cookie_parts + 1] = rci_name .. "=" .. rci_value
        end
        local cookie_header = table.concat(cookie_parts, "; ")

        body, status = http.get(canon_url, {
            user_agent = DESKTOP_UA,
            timeout    = REQUEST_TIMEOUT,
            headers    = {
                ["Accept"]          = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
                ["Accept-Language"] = "en-US,en;q=0.9",
                ["Referer"]         = TIKTOK_HOME .. "/",
                ["Sec-Fetch-Dest"]  = "document",
                ["Sec-Fetch-Mode"]  = "navigate",
                ["Cookie"]          = cookie_header,
            },
        })

        if status ~= 200 then
            ludo.logError(("TikTok: HTTP %d after WAF retry"):format(status))
            return
        end
        ludo.logInfo(("TikTok: WAF bypass succeeded (body=%d bytes)"):format(#body))
    end

    local play_url = nil
    local title    = nil

    -- Strategy 1: SIGI_STATE (older TikTok page format)
    local sigi = extract_script_json(body, "SIGI_STATE")
        or extract_script_json(body, "sigi%-persisted%-data")
    if sigi then
        local item = item_from_sigi(sigi, video_id)
        if item then
            play_url = find_play_url(item.video)
            title    = item.desc
            if play_url then
                ludo.logInfo("TikTok: SIGI_STATE strategy succeeded")
            end
        end
    end

    -- Strategy 2: __UNIVERSAL_DATA_FOR_REHYDRATION__ (newer page format)
    if not play_url then
        local udata = extract_script_json(body, "__UNIVERSAL_DATA_FOR_REHYDRATION__")
        if udata then
            local item = item_from_universal(udata, video_id)
            if item then
                play_url = find_play_url(item.video)
                title    = item.desc
                if play_url then
                    ludo.logInfo("TikTok: __UNIVERSAL_DATA strategy succeeded")
                end
            end
        end
    end

    -- Strategy 3: Regex fallback — search raw HTML for playAddr
    if not play_url then
        local raw = body:match('"playAddr"%s*:%s*"(https?[^"]+)"')
            or body:match('"play_addr"%s*:%s*{[^}]*"url_list"%s*:%s*%["(https?[^"]+)"')
        if raw then
            play_url = unescape_url(raw)
            ludo.logInfo("TikTok: regex fallback strategy succeeded")
        end
    end

    if not play_url then
        ludo.logError("TikTok: failed to extract play URL for video_id=" .. video_id)
        -- Save debug page for analysis
        local dbg2 = io.open(outdir .. "/tiktok_debug_" .. video_id .. ".html", "w")
        if dbg2 then
            dbg2:write(body)
            dbg2:close()
            ludo.logInfo("TikTok: page saved for debug: tiktok_debug_" .. video_id .. ".html")
        end
        return
    end

    -- Build a clean filename and queue the download.
    -- The tt_chain_token cookie (set by the server during the page fetch and
    -- persisted back to cookie_path via CURLOPT_COOKIEJAR) must be sent with
    -- the CDN GET request; the URL's tk=tt_chain_token parameter binds to it.
    local filename = safe_name(title or ("tiktok_" .. video_id)) .. "_" .. video_id .. ".mp4"
    local dl_headers = {
        ["Referer"]         = TIKTOK_HOME .. "/",
        ["Accept"]          = "*/*",
        ["Accept-Language"] = "en-US,en;q=0.9",
    }
    local tt_chain  = http.read_cookie(cookie_path, "tt_chain_token")
    local sessionid = http.read_cookie(cookie_path, "sessionid")
    local cookie_parts = {}
    if tt_chain  and tt_chain  ~= "" then cookie_parts[#cookie_parts + 1] = "tt_chain_token=" .. tt_chain  end
    if sessionid and sessionid ~= "" then cookie_parts[#cookie_parts + 1] = "sessionid="      .. sessionid end
    if #cookie_parts > 0 then
        dl_headers["Cookie"] = table.concat(cookie_parts, "; ")
        ludo.logInfo("TikTok: attaching cookies to download request: " .. dl_headers["Cookie"]:sub(1, 60) .. "...")
    else
        ludo.logInfo("TikTok: no session cookies found — download may fail")
    end
    local _, dl_status, output = ludo.newDownload(
        play_url, outdir, ludo.DOWNLOAD_NOW, filename, dl_headers)

    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("TikTok: queued → " .. (output or filename))
    elseif dl_status == 403 then
        -- TikTok CDN blocks HEAD probes without session cookies; the signed
        -- play URL should still be accessible via GET, so queue it anyway.
        ludo.logSuccess("TikTok: queued (CDN blocked HEAD probe) → " .. (output or filename))
    else
        ludo.logError(("TikTok: preflight HTTP %d for %s"):format(dl_status, filename))
    end
end

return plugin
