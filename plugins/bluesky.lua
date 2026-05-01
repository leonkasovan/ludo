-- Bluesky plugin for Ludo
-- Ported from yt-dlp bluesky.py extractor
-- Version: 20250501

local plugin = { name = "Bluesky", version = "20250501", creator = "opencode" }
local json = json or require("json")

local function safe_name(s)
    return (s or ""):gsub("[^%w%-_]", "_"):sub(1, 80)
end

local function get_json(url, video_id, query)
    local q = ""
    if query then
        local t = {}
        for k, v in pairs(query) do
            table.insert(t, k .. "=" .. tostring(v))
        end
        q = "?" .. table.concat(t, "&")
    end
    local body, status = http.get(url .. q, { timeout = 20 })
    if status ~= 200 then
        ludo.logError("Bluesky: failed to fetch " .. url .. " (" .. status .. "): " .. (body or "no body"))
        return nil
    end
    local ok, data = pcall(json.decode, body)
    if not ok then
        ludo.logError("Bluesky: JSON decode failed for " .. url)
        return nil
    end
    return data
end


function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://bsky%.app/profile/[^/]+/post/[^/?#]+$") then return true end
    if url:match("^https?://www%.bsky%.app/profile/[^/]+/post/[^/?#]+$") then return true end
    if url:match("^https?://main%.bsky%.dev/profile/[^/]+/post/[^/?#]+$") then return true end
    if url:match("^at://[^/]+/app%.bsky%.feed%.post/[^/?#]+$") then return true end
    return false
end

local function get_post(handle, post_id)
    local url = "https://public.api.bsky.app/xrpc/app.bsky.feed.getPostThread"
    local data = get_json(url, post_id, {
        uri = "at://" .. handle .. "/app.bsky.feed.post/" .. post_id,
        depth = 0,
        parentHeight = 0,
    })
    if not data or not data.thread or not data.thread.post then
        ludo.logError("Bluesky: failed to extract post JSON")
        return nil
    end
    return data.thread.post
end

local function get_service_endpoint(did, video_id)
    local url
    if did:sub(1,8) == "did:web:" then
        url = "https://" .. did:sub(9) .. "/.well-known/did.json"
    else
        url = "https://plc.directory/" .. did
    end
    local data = get_json(url, video_id)
    if not data or not data.service then return "https://bsky.social" end
    for _, svc in ipairs(data.service) do
        if svc.type == "AtprotoPersonalDataServer" and svc.serviceEndpoint then
            return svc.serviceEndpoint
        end
    end
    return "https://bsky.social"
end

local function extract_videos(post, video_id)
    local entries = {}
    local embed = post.embed or {}
    local author = post.author or {}
    local did = author.did
    local function add_entry(embed_path)
        local e = embed_path
        if not e or type(e) ~= "table" then return end
        local playlist = e.playlist
        local formats = {}
        if playlist then
            table.insert(formats, { url = playlist, ext = "mp4", format_id = "hls" })
        end
        local cid = e.cid or (e.video and e.video.ref and e.video.ref["$link"])
        if did and cid then
            local endpoint = get_service_endpoint(did, video_id)
            table.insert(formats, {
                format_id = "blob",
                url = endpoint .. "/xrpc/com.atproto.sync.getBlob?did=" .. did .. "&cid=" .. cid,
                ext = "mp4",
                quality = 1,
            })
        end
        if #formats > 0 then
            table.insert(entries, {
                id = video_id,
                formats = formats,
                uploader = author.displayName or author.handle,
                uploader_id = author.handle,
                uploader_url = author.handle and ("https://bsky.app/profile/" .. author.handle) or nil,
                channel_id = did,
                channel_url = did and ("https://bsky.app/profile/" .. did) or nil,
                thumbnail = e.thumbnail,
                title = post.text and post.text:gsub("\n", " "):sub(1,72) or ("Bluesky video #" .. video_id),
                description = post.text,
            })
        end
    end
    -- Try main embed
    add_entry(embed)
    -- Try embed.media
    if embed.media then add_entry(embed.media) end
    -- Try embed.record.record
    if embed.record and embed.record.record then add_entry(embed.record.record) end
    return entries
end

function plugin.process(url)
    local handle, video_id = url:match("profile/([%w%.:%%%-]+)/post/(%w+)")
    if not handle then
        handle, video_id = url:match("at://([%w%.:%%%-]+)/app%.bsky%.feed%.post/(%w+)")
    end
    if not handle or not video_id then
        ludo.logError("Bluesky: failed to parse handle/video_id from URL")
        return
    end
    ludo.logInfo("Bluesky: handle=" .. handle .. " video_id=" .. video_id)
    local post = get_post(handle, video_id)
    if not post then return end
    local entries = extract_videos(post, video_id)
    if not entries or #entries == 0 then
        ludo.logError("Bluesky: no video found in post")
        return
    end
    local entry = entries[1]
    local outdir = ludo.getOutputDirectory()
    local fname = safe_name((entry.title or ("bluesky_" .. video_id)) .. ".mp4")
    -- Prefer direct MP4 blob over HLS playlist
    local url
    for _, f in ipairs(entry.formats) do
        if f.format_id == "blob" then url = f.url; break end
    end
    if not url then url = entry.formats[1].url end
    local _, dl_status, output = ludo.newDownload(url, outdir, ludo.DOWNLOAD_NOW, fname)
    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Queued → " .. (output or fname))
    elseif dl_status == 403 then
        ludo.logSuccess("Queued (CDN blocked HEAD probe) → " .. (output or fname))
    else
        ludo.logError("Preflight HTTP " .. dl_status)
    end
end

return plugin
