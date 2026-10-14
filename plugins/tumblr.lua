-- tumblr.lua
--
-- Ludo plugin for downloading videos from Tumblr posts.
-- Ported from yt-dlp's TumblrIE (yt_dlp/extractor/tumblr.py).
--
-- Supported URL shapes:
--   https://<blog>.tumblr.com/post/<id>[/slug]
--   https://<blog>.tumblr.com/video/<id>[/...]
--   https://<blog>.tumblr.com/<blog>/<id>            (old permalink style)
--   https://www.tumblr.com/<blog>/<id>[/slug]        (new "redpop" permalinks)
--   https://www.tumblr.com/video/<blog>/<id>[/...]
--
-- EXTRACTION STRATEGY (same order as the yt-dlp extractor):
--   1. Legacy blog API  : api.tumblr.com/v2/blog/<blog>.tumblr.com/posts?id=<id>
--                         using the classic public `api_key`. Tumblr now often
--                         answers HTTP 401 here, so this is best-effort only.
--   2. oEmbed API       : www.tumblr.com/oembed/1.0?url=... -> canonical
--                         www.tumblr.com permalink, author and the
--                         embed.tumblr.com post-iframe href.
--   3. Embed page       : embed.tumblr.com/embed/post/<t:key>/<id>/v2 -> the
--                         self-hosted <video><source src="..."> MP4/HLS plus
--                         external provider iframes (YouTube/Vimeo/...).
--   4. Post page scrape : canonical post HTML -> <meta property="og:video">,
--                         application/ld+json "contentUrl", and the modern
--                         JSON blob's "video_url" / media objects.
--
-- Tumblr self-hosts video as progressive MP4 transcodes (480p/720p/1080p) and
-- an HLS master playlist; we pick the highest-resolution MP4 and only fall back
-- to the HLS playlist when no progressive MP4 is exposed.

local plugin = {
    name    = "Tumblr",
    version = "20260610",
    creator = "opencode",
}

local json = json or require("json")

-- The well-known public Tumblr web API key (same value the extractor ships).
local TUMBLR_API_KEY = "aIcXSOoTtqrzR8L8YEIOmBeW94c3FmbSNSWAUbxsny9KKx5VFh"

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    .. "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

local BASE_HEADERS = {
    ["Accept-Language"] = "en-US,en;q=0.9",
    ["Accept"]          = "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8",
}

-- ─── small helpers ──────────────────────────────────────────────────────────

local function safe_name(s, maxlen)
    return (tostring(s or ""))
        :gsub("[\\/:*?\"<>|%z]", "_")
        :gsub("%s+", " ")
        :sub(1, maxlen or 80)
end

local function trim(s)
    return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Drop trailing separator/punctuation bytes (used after stripping title tails).
local function trim_tail(s)
    while #s > 0 do
        local last = s:sub(-1)
        if last:match("[%w%)%]%}!%?%.]") then break end
        s = s:sub(1, -2)
    end
    return trim(s)
end

local function html_unescape(s)
    if not s then return nil end
    s = s:gsub("&amp;", "&")
         :gsub("&#38;", "&")
         :gsub("&#x26;", "&")
         :gsub("&#x2F;", "/")
         :gsub("&lt;", "<")
         :gsub("&gt;", ">")
         :gsub("&quot;", "\"")
         :gsub("&#39;", "'")
         :gsub("&#x27;", "'")
         :gsub("&apos;", "'")
         :gsub("&nbsp;", " ")
    s = s:gsub("&#(%d+);", function(n)
        n = tonumber(n)
        if n and n < 128 then return string.char(n) end
        return ""
    end)
    s = s:gsub("&#[xX](%x+);", function(h)
        local n = tonumber(h, 16)
        if n and n < 128 then return string.char(n) end
        return ""
    end)
    return s
end

-- Decode a JSON string literal that was embedded in HTML (\uXXXX and \/).
local function unescape_json(s)
    if not s then return nil end
    s = s:gsub("\\/", "/")
    s = s:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp and cp < 128 then return string.char(cp) end
        return ""
    end)
    s = s:gsub('\\"', '"'):gsub("\\n", "\n"):gsub("\\t", "\t")
    return s
end

local function url_encode(s)
    return (tostring(s or ""):gsub("[^%w%-_%.~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

local function url_decode(s)
    if not s then return nil end
    s = s:gsub("+", " ")
    s = s:gsub("%%(%x%x)", function(h)
        return string.char(tonumber(h, 16))
    end)
    return s
end

local function http_get(url, extra_headers)
    local headers = {}
    for k, v in pairs(BASE_HEADERS) do headers[k] = v end
    for k, v in pairs(extra_headers or {}) do headers[k] = v end
    local body, status = http.get(url, {
        user_agent       = UA,
        timeout          = TIMEOUT,
        follow_redirects = true,
        headers          = headers,
    })
    return body, status
end

local function parse_json(body)
    if type(body) ~= "string" or body == "" then return nil end
    local ok, data = pcall(json.decode, body)
    if ok and type(data) == "table" then return data end
    return nil
end

-- ─── URL parsing (mirrors TumblrIE._VALID_URL) ───────────────────────────────

-- Returns { blog = <slug>, id = <post id>, new_style = bool } or nil.
local function parse_url(url)
    if type(url) ~= "string" then return nil end

    -- Legacy blog subdomain: <blog>.tumblr.com/(post|video)/<id>[/slug]
    local blog, vid, slug = url:match("^https?://([^/?#&]+)%.tumblr%.com/post/(%d+)/?([^/?#]*)")
    if not vid then
        blog, vid, slug = url:match("^https?://([^/?#&]+)%.tumblr%.com/video/(%d+)/?([^/?#]*)")
    end
    if vid and blog and blog ~= "www" and blog ~= "assets" and blog ~= "embed" then
        return { blog = blog, id = vid, slug = (slug ~= "" and slug or nil), new_style = false }
    end

    -- New permalink style: www.tumblr.com/<blog>/<id>[/slug]  (or /video/<blog>/<id>)
    local blog2, vid2, slug2 = url:match("^https?://www%.tumblr%.com/video/([%w%-_]+)/(%d+)/?([^/?#]*)")
    if not vid2 then
        blog2, vid2, slug2 = url:match("^https?://www%.tumblr%.com/([%w%-_]+)/(%d+)/?([^/?#]*)")
    end
    if vid2 and blog2 and blog2 ~= "video" then
        return { blog = blog2, id = vid2, slug = (slug2 ~= "" and slug2 or nil), new_style = true }
    end

    return nil
end

function plugin.validate(url)
    return parse_url(url) ~= nil
end

-- ─── strategy 1: legacy blog API (best effort) ──────────────────────────────

local function try_blog_api(blog, post_id)
    local api_url = string.format(
        "https://api.tumblr.com/v2/blog/%s.tumblr.com/posts?id=%s&api_key=%s",
        blog, post_id, TUMBLR_API_KEY)
    local body, status = http_get(api_url, { ["Accept"] = "application/json" })
    if status ~= 200 then return nil end
    local data = parse_json(body)
    local posts = data and data.response and data.response.posts
    if type(posts) ~= "table" or #posts == 0 then return nil end
    return posts[1]
end

-- Pull a self-hosted video URL out of a v2 API post (legacy + NPF shapes).
local function video_from_api_post(post)
    if type(post) ~= "table" then return nil end

    -- Legacy video posts expose a top-level video_url / player list.
    local best = nil
    if type(post.video_url) == "string" and post.video_url ~= "" then
        best = { url = post.video_url, height = 0, is_hls = false, kind = "api" }
    end

    -- Newer "npf" posts put media inside content[].
    if type(post.content) == "table" then
        for _, item in ipairs(post.content) do
            if type(item) == "table" and item.type == "video" then
                local media = item.media
                if type(media) == "table" then
                    local u = media.url
                    -- media.url can itself be a table keyed by quality.
                    if type(u) == "string" and u ~= "" then
                        if not best or (u:match("%.mp4") and not best.url:match("%.mp4")) then
                            best = { url = u, height = 0, is_hls = u:match("%.m3u8") ~= nil, kind = "api" }
                        end
                    elseif type(u) == "table" then
                        for _, cand in pairs(u) do
                            if type(cand) == "string" and cand:match("%.mp4") then
                                best = { url = cand, height = 0, is_hls = false, kind = "api" }
                                break
                            end
                        end
                    end
                end
            end
        end
    end

    return best
end

-- ─── strategy 2: oEmbed ─────────────────────────────────────────────────────

local function try_oembed(url)
    local oembed_url = "https://www.tumblr.com/oembed/1.0?url=" .. url_encode(url)
    local body, status = http_get(oembed_url, { ["Accept"] = "application/json" })
    if status ~= 200 then return nil end
    local data = parse_json(body)
    if not data then return nil end
    return {
        canonical  = data.url,
        author     = data.author_name,
        author_url = data.author_url,
        html       = data.html,
    }
end

-- The oEmbed payload embeds an iframe href pointing at embed.tumblr.com.
local function embed_url_from_oembed(html)
    if type(html) ~= "string" then return nil end
    local href = html:match('data%-href="(https://embed%.tumblr%.com/[^"]+)"')
    if href then return html_unescape(href) end
    return nil
end

-- ─── page scraping ──────────────────────────────────────────────────────────

-- Collect every plausible media URL from an embed/post page.
local function scrape_media(html)
    local found = {}

    local function add(u, kind)
        if type(u) ~= "string" then return end
        u = html_unescape(unescape_json(u))
        if u == "" or not u:match("^https?://") then return end
        for _, f in ipairs(found) do
            if f.url == u then return end
        end
        local is_hls = u:match("%.m3u8") ~= nil
        local height = 0
        local w = u:match("_(%d+)%.mp4")
        if w then height = tonumber(w) or 0 end
        if height == 0 then
            local h = u:match("_(%d+)k?%.m3u8") or u:match("%.(%d+)p%.")
            if h then height = tonumber(h) or 0 end
        end
        found[#found + 1] = { url = u, height = height, is_hls = is_hls, kind = kind }
    end

    -- <video><source src="..."> from the self-hosted player.
    for src in html:gmatch('<source[^>]-src="([^"]+)"') do add(src, "source") end
    for src in html:gmatch("<source[^>]-src='([^']+)'") do add(src, "source") end

    -- data-crt-options JSON blob (carries hdUrl / duration).
    local opts_raw = html:match("data%-crt%-options='(.-)'")
        or html:match('data%-crt%-options="(.-)"')
    if opts_raw then
        local opts = parse_json(html_unescape(opts_raw))
        if opts then
            if type(opts.hdUrl) == "string" then add(opts.hdUrl, "hd") end
            if type(opts.sdUrl) == "string" then add(opts.sdUrl, "sd") end
        end
    end

    -- Bare media references in inline JSON / JS.
    for u in html:gmatch('(https?://[^"\'\\)%s]+%.mp4[^"\'\\)%s]*)') do add(u, "mp4") end
    for u in html:gmatch('(https?://[^"\'\\)%s]+%.m3u8[^"\'\\)%s]*)') do add(u, "hls") end
    for u in html:gmatch('"video_url"%s*:%s*"([^"]+)"') do add(u, "json") end

    return found
end

local function scrape_meta(html)
    local m = {}
    m.og_video = html:match('<meta[^>]-property="og:video[^"]*"[^>]-content="([^"]+)"')
        or html:match('<meta[^>]-content="([^"]+)"[^>]-property="og:video[^"]*"')
    m.og_title = html:match('<meta[^>]-property="og:title"[^>]-content="([^"]+)"')
        or html:match('<meta[^>]-content="([^"]+)"[^>]-property="og:title"')
        or html:match('<meta[^>]-name="twitter:title"[^>]-content="([^"]+)"')
    m.og_image = html:match('<meta[^>]-property="og:image"[^>]-content="([^"]+)"')
        or html:match('<meta[^>]-content="([^"]+)"[^>]-property="og:image"')
    m.title = html:match("<title[^>]*>(.-)</title>")
    for k, v in pairs(m) do m[k] = html_unescape(v) end
    return m
end

local function scrape_ldjson(html)
    local urls, thumbs = {}, {}
    for block in html:gmatch('<script[^>]-type="application/ld%+json"[^>]*>(.-)</script>') do
        local decoded = unescape_json(block)
        for u in decoded:gmatch('"contentUrl"%s*:%s*"([^"]+)"') do
            urls[#urls + 1] = html_unescape(u)
        end
        for u in decoded:gmatch('"thumbnailUrl"%s*:%s*"([^"]+)"') do
            thumbs[#thumbs + 1] = html_unescape(u)
        end
    end
    return urls, thumbs
end

-- Strip "… – @blog on Tumblr" tails and "… | Tumblr" from <title> values.
local function clean_title(t)
    t = trim(t)
    if t == "" then return nil end
    t = t:gsub("%s+on Tumblr%s*$", "")
    t = t:gsub("%s*|%s*Tumblr%s*$", "")
    t = t:gsub("^Post by @[%w%-_]+%s*$", "")
    return trim_tail(t)
end

-- ─── format selection ───────────────────────────────────────────────────────

-- Prefer the highest-resolution progressive MP4, then HLS.
local function pick_best(media)
    local best_mp4, best_hls
    for _, f in ipairs(media) do
        if f.is_hls then
            if not best_hls then best_hls = f end
        else
            if not best_mp4 or f.height > best_mp4.height then best_mp4 = f end
        end
    end
    return best_mp4 or best_hls
end

-- ─── main ───────────────────────────────────────────────────────────────────

function plugin.process(url)
    local info = parse_url(url)
    if not info then
        ludo.logError("Tumblr: unsupported URL: " .. tostring(url))
        return
    end

    local blog, post_id = info.blog, info.id
    ludo.logInfo("Tumblr: resolving " .. blog .. "/" .. post_id)

    local title, thumb, author = nil, nil, nil
    local media = {}
    local page_url = url

    -- 1. Legacy blog API (may return 401 — that is fine, we fall through).
    local api_post = try_blog_api(blog, post_id)
    if api_post then
        local v = video_from_api_post(api_post)
        if v then media[#media + 1] = v end
        if type(api_post.summary) == "string" and trim(api_post.summary) ~= "" then
            title = trim(api_post.summary)
        elseif type(api_post.caption) == "string" and trim(api_post.caption) ~= "" then
            title = trim(api_post.caption):match("^[^\n]+")
        end
        thumb = api_post.thumbnail_url or thumb
        if type(api_post.trail) == "table" and type(api_post.trail[1]) == "table"
            and type(api_post.trail[1].blog) == "table" then
            author = api_post.trail[1].blog.name
        end
    else
        ludo.logInfo("Tumblr: blog API unavailable (401 / locked), using web fallbacks")
    end

    -- 2. oEmbed -> canonical permalink + embed iframe href.
    local oembed = try_oembed(url)
    local embed_url
    if oembed then
        if oembed.canonical then page_url = oembed.canonical end
        author = author or oembed.author
        embed_url = embed_url_from_oembed(oembed.html)
        local cb, cid = tostring(oembed.canonical or ""):match(
            "^https?://www%.tumblr%.com/([%w%-_]+)/(%d+)")
        if cb and cid then blog = cb end
    else
        ludo.logInfo("Tumblr: oEmbed lookup failed")
    end

    -- 3. Embed page: self-hosted <source> + external provider iframes.
    if embed_url then
        local body, status = http_get(embed_url, { ["Referer"] = "https://www.tumblr.com/" })
        if status == 200 and body then
            for _, m in ipairs(scrape_media(body)) do media[#media + 1] = m end
            local meta = scrape_meta(body)
            title = title or clean_title(meta.og_title)
            thumb = thumb or meta.og_image
        else
            ludo.logError("Tumblr: embed page returned HTTP " .. tostring(status))
        end
    end

    -- 4. Post page scrape: og:video, ld+json contentUrl, inline JSON.
    local page_body, page_status = http_get(page_url, { ["Referer"] = "https://www.tumblr.com/" })
    if page_status == 200 and page_body then
        for _, m in ipairs(scrape_media(page_body)) do media[#media + 1] = m end

        local meta = scrape_meta(page_body)
        local page_title = clean_title(meta.og_title)
        if not page_title or page_title:match("^Post by @") then
            page_title = clean_title(meta.title)
        end
        title = title or page_title
        thumb = thumb or meta.og_image

        local ld_urls, ld_thumbs = scrape_ldjson(page_body)
        for _, u in ipairs(ld_urls) do
            if u:match("^https?://") then
                media[#media + 1] = {
                    url = u, height = 0,
                    is_hls = u:match("%.m3u8") ~= nil, kind = "ldjson",
                }
            end
        end
        thumb = thumb or ld_thumbs[1]

        -- og:video is the full-quality self-hosted MP4 when present.
        if meta.og_video and meta.og_video:match("^https?://") then
            media[#media + 1] = {
                url = meta.og_video,
                height = 9000,                       -- wins over size-suffixed MP4s
                is_hls = meta.og_video:match("%.m3u8") ~= nil,
                kind = "og",
            }
        end
    elseif page_status ~= 200 then
        ludo.logError("Tumblr: post page returned HTTP " .. tostring(page_status))
    end

    -- Deduplicate, keeping the first (highest-priority) occurrence.
    local seen, uniq = {}, {}
    for _, m in ipairs(media) do
        if not seen[m.url] then
            seen[m.url] = true
            uniq[#uniq + 1] = m
        end
    end

    -- Nothing self-hosted? Detect a reblogged external embed and say so.
    if #uniq == 0 then
        local ext = page_body and page_body:match("safe%.txmblr%.com/svc/embed/inline/([^\"'#]+)")
        if ext then
            ludo.logError("Tumblr: post only embeds external media, not a Tumblr video: "
                .. tostring(url_decode(ext)))
        else
            ludo.logError("Tumblr: no video found in post " .. post_id
                .. " (photo/text post, deleted, or private)")
        end
        return
    end

    local best = pick_best(uniq)
    if not best then
        ludo.logError("Tumblr: no playable media found in post " .. post_id)
        return
    end

    -- Slug-based filename: "blog-slug-title_ID.mp4" (falls back gracefully).
    -- Prefer the post title; if the page URL had a slug, use that; otherwise
    -- author/blog name. The post id is always appended for uniqueness.
    local slug = info.slug
    if not slug or slug == "" then
        -- recover slug from the canonical page URL when the input had none
        slug = tostring(page_url):match("/post/%d+/([^/?#]+)")
            or tostring(page_url):match("^https?://www%.tumblr%.com/[%w%-_]+/%d+/([^/?#]+)")
    end
    local fname_title = trim(title)
    -- Tumblr pages often carry junk og:titles like "Tumblr" or "Post by @user"
    if fname_title == "" or fname_title:lower() == "tumblr"
        or fname_title:match("^Post by @") then
        fname_title = slug or ""
    end
    if fname_title == "" or #fname_title < 3 then
        fname_title = (author or blog or "tumblr") .. "-" .. post_id
    end
    -- strip common prefixes that make titles noisy, then fold to a filename
    fname_title = fname_title:gsub("^[Tt]umblr%s*[-–:]%s*", "")
    local fname = safe_name(blog .. "-" .. fname_title .. "-" .. post_id, 96)
        .. (best.is_hls and ".m3u8" or ".mp4")

    ludo.logInfo(string.format("Tumblr: selected %s%s%s",
        tostring(best.kind),
        best.height > 0 and (" @" .. tostring(best.height) .. "p") or "",
        best.is_hls and " (HLS)" or " (MP4)"))

    local _, dl_status, output = ludo.newDownload(
        best.url,
        ludo.getOutputDirectory(),
        ludo.DOWNLOAD_NOW,
        fname,
        { Referer = "https://www.tumblr.com/", Origin = "https://www.tumblr.com" })

    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Tumblr: queued → " .. tostring(output or fname))
    elseif dl_status == 403 then
        ludo.logSuccess("Tumblr: queued (CDN blocked HEAD probe) → " .. tostring(output or fname))
    else
        ludo.logError("Tumblr: preflight HTTP " .. tostring(dl_status))
    end
end

return plugin
