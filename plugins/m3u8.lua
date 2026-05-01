-- m3u8.lua — Pure Lua HLS/m3u8 downloader for Ludo
--
-- Provides m3u8 playlist parsing and sequential segment downloading.
-- Plugins dofile() this module to add HLS support.
--
-- Usage:
--   local m3u8 = dofile("plugins/m3u8.lua")
--   local ok, path_or_error = m3u8.download(m3u8_url, output_dir, "video.mp4")
--
-- Limitations:
--   - No AES-128 decryption (encrypted streams are logged as unsupported)
--   - Individual segments limited to ~8 MB (Ludo http.get() buffer)
--   - Sequential segment download (no concurrency)

local m3u8 = {}
local TIMEOUT = 30

-- ─── Helpers ──────────────────────────────────────────────────────────────

-- Parse key=value pairs from an m3u8 tag attribute string.
-- e.g. 'METHOD=AES-128,URI="key.bin",IV=0x...'
local function parse_attrs(s)
    local t = {}
    -- Split by commas, then parse each KEY=VALUE pair
    for pair in (s or ""):gmatch("[^,]+") do
        local key, val = pair:match("^([%w%-]+)=(.*)$")
        if key then
            if val:sub(1, 1) == '"' then val = val:sub(2, -2) end
            t[key] = val
        end
    end
    return t
end

-- Resolve a (possibly relative) URL against a base URL.
local function resolve_url(base, rel)
    if not rel then return nil end
    if rel:match("^https?://") then return rel end
    local dir = base:match("^(.*/)[^/]+") or (base .. "/")
    if not dir:match("/$") then dir = dir .. "/" end
    while rel:sub(1, 3) == "../" do
        rel = rel:sub(4)
        dir = dir:match("^(.*/)[^/]+/$") or dir
    end
    return dir .. rel
end

-- HTTP GET with optional headers
function m3u8._http_get(url, headers)
    local opts = { timeout = TIMEOUT }
    if headers and type(headers) == "table" and next(headers) then
        opts.headers = headers
    end
    return http.get(url, opts)
end

-- ─── Playlist Detection ───────────────────────────────────────────────────

function m3u8.is_master(text)
    return (text or ""):match("#EXT%-X%-STREAM%-INF:") ~= nil
end

-- ─── Master Playlist Parsing ──────────────────────────────────────────────

-- Extract variant stream entries from a master playlist.
-- Returns: {{url, bandwidth, resolution, codecs}, ...}
function m3u8.parse_master(text, base_url)
    local variants, last_attrs = {}, {}
    for line in (text or ""):gmatch("[^\n]+") do
        line = line:gsub("\r$", "")
        if line:match("^#EXT%-X%-STREAM%-INF:") then
            last_attrs = parse_attrs(line:match("^#EXT%-X%-STREAM%-INF:(.*)") or "")
        elseif not line:match("^#") and line:match(".+") then
            local url = resolve_url(base_url, line)
            if url then
                local bw = tonumber(last_attrs.BANDWIDTH) or 0
                if not last_attrs.CODECS or not last_attrs.CODECS:match("^avc1%.42E[0-1]") then
                    -- Skip I-frame-only streams (CODECS like avc1.42E01E)
                if not last_attrs.CODECS or not last_attrs.CODECS:match("^avc1%.42E") then
                    table.insert(variants, {
                        url = url,
                        bandwidth = bw,
                        resolution = last_attrs.RESOLUTION,
                        codecs = last_attrs.CODECS,
                    })
                end
                end
            end
            last_attrs = {}
        end
    end
    return variants
end

-- Sort variants by bandwidth descending, return the highest.
function m3u8.pick_best(variants)
    if #variants == 0 then return nil end
    table.sort(variants, function(a, b) return a.bandwidth > b.bandwidth end)
    return variants[1]
end

-- ─── Media Playlist Parsing ───────────────────────────────────────────────

-- Parse a media playlist, returning segment and metadata info.
-- Returns: {segments, init_map_url, has_encryption, end_list, target_duration, key_uri, key_iv}
local function parse_media(text, base_url)
    local segments = {}
    local init_map_url = nil
    local has_encryption = false
    local end_list = false
    local target_duration = nil
    local byte_range = nil
    local key_uri = nil
    local key_iv = nil
    local media_seq = 0

    for line in (text or ""):gmatch("[^\n]+") do
        line = line:gsub("\r$", "")

        if line:match("^#EXT%-X%-TARGETDURATION:") then
            target_duration = tonumber(line:match(":(%d+)")) or target_duration

        elseif line:match("^#EXT%-X%-KEY:") then
            local attrs = parse_attrs(line:match("^#EXT%-X%-KEY:(.*)") or "")
            if attrs.METHOD and attrs.METHOD ~= "NONE" then
                has_encryption = true
                key_uri = attrs.URI and resolve_url(base_url, attrs.URI) or nil
                key_iv = attrs.IV  -- hex string like "0x..." or nil
            else
                has_encryption = false
                key_uri = nil
                key_iv = nil
            end

        elseif line:match("^#EXT%-X%-MAP:") then
            local attrs = parse_attrs(line:match("^#EXT%-X%-MAP:(.*)") or "")
            if attrs.URI then
                init_map_url = resolve_url(base_url, attrs.URI)
            end

        elseif line:match("^#EXT%-X%-BYTERANGE:") then
            byte_range = line:match("^#EXT%-X%-BYTERANGE:(.+)")

        elseif line:match("^#EXT%-X%-MEDIA%-SEQUENCE:") then
            media_seq = tonumber(line:match(":(%d+)")) or 0

        elseif line:match("^#EXT%-X%-ENDLIST") then
            end_list = true

        elseif not line:match("^#") and line:match(".+") then
            local url = resolve_url(base_url, line)
            if url then
                table.insert(segments, {
                    url = url,
                    byte_range = byte_range,
                    seq = media_seq,
                })
                media_seq = media_seq + 1
                byte_range = nil
            end
        end
    end

    return {
        segments = segments,
        init_map_url = init_map_url,
        has_encryption = has_encryption,
        end_list = end_list,
        target_duration = target_duration,
        key_uri = key_uri,
        key_iv = key_iv,
    }
end

-- Fetch and parse an m3u8 URL. Resolves master playlists recursively.
-- Returns the parsed media info or (nil, err_msg).
function m3u8.fetch_segments(m3u8_url, headers)
    local body, status = m3u8._http_get(m3u8_url, headers)
    if status ~= 200 then
        return nil, "failed to fetch m3u8 (HTTP " .. tostring(status) .. ")"
    end
    if not body:match("^#EXTM3U") then
        return nil, "invalid m3u8 (missing #EXTM3U header)"
    end

    -- Resolve master playlist → pick best variant → recurse
    if m3u8.is_master(body) then
        local variants = m3u8.parse_master(body, m3u8_url)
        if #variants == 0 then return nil, "no variants found in master playlist" end
        local best = m3u8.pick_best(variants)
        ludo.logInfo(("  Best variant: %s (%d kbps)"):format(
            best.resolution or "unknown",
            math.floor((best.bandwidth or 0) / 1000)))
        return m3u8.fetch_segments(best.url, headers)
    end

    return parse_media(body, m3u8_url)
end

-- ─── Segment Download & Concatenation ─────────────────────────────────────

local function ensure_dir(dir)
    if not dir or dir == "" then return true end
    -- Check if directory exists by attempting to create a test file
    local test = io.open(dir .. "/.ludo_hls_test", "w")
    if test then
        test:close()
        os.remove(dir .. "/.ludo_hls_test")
        return true
    end
    -- Try to create directory (Windows)
    os.execute('mkdir "' .. dir .. '" 2>NUL')
    test = io.open(dir .. "/.ludo_hls_test", "w")
    if test then
        test:close()
        os.remove(dir .. "/.ludo_hls_test")
        return true
    end
    return false
end

-- Compute AES-128-CBC IV from a hex string or derive from media sequence number.
-- If no explicit IV, use big-endian seq number padded to 16 bytes.
local function compute_iv(key_iv, seq)
    if key_iv then
        -- Strip "0x" prefix if present
        local h = key_iv:gsub("^0[xX]", "")
        -- Convert hex to bytes
        local b = {}
        for i = 1, #h, 2 do
            b[#b+1] = string.char(tonumber(h:sub(i, i+1), 16))
        end
        local iv = table.concat(b)
        -- Pad to 16 bytes if shorter
        while #iv < 16 do iv = iv .. "\0" end
        return iv:sub(1, 16)
    else
        -- Derive from sequence number
        local b = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        -- seq as big-endian int64 in last 8 bytes
        local s = seq
        for i = 7, 0, -1 do
            local byte = s % 256
            b = b:sub(1, 8+i) .. string.char(byte) .. b:sub(9+i+1)
            s = math.floor(s / 256)
        end
        return b
    end
end

-- Download all segments from a media playlist and concatenate to a single file.
-- Returns (true, output_path) on success or (false, error_msg) on failure.
function m3u8.download_media(seg_info, output_path, headers)
    if #seg_info.segments == 0 then
        return false, "no segments in playlist"
    end

    -- Handle encryption
    local key = nil
    if seg_info.has_encryption then
        if not seg_info.key_uri then
            return false, "encrypted stream missing key URI"
        end
        ludo.logInfo("HLS: fetching decryption key from " .. seg_info.key_uri)
        local key_data, ks = m3u8._http_get(seg_info.key_uri, headers)
        if ks ~= 200 or not key_data or #key_data ~= 16 then
            return false, ("failed to fetch key (HTTP %d, len=%d)"):format(ks, #(key_data or ""))
        end
        key = key_data
    end

    local f = io.open(output_path, "wb")
    if not f then return false, "cannot open " .. output_path end

    -- Prepend initialization segment if present
    if seg_info.init_map_url then
        local data, s = m3u8._http_get(seg_info.init_map_url, headers)
        if s == 200 then f:write(data) end
    end

    -- Download & append each segment
    local total = #seg_info.segments
    for i = 1, total do
        local seg = seg_info.segments[i]
        local label = key and "decrypting" or "downloading"
        ludo.logInfo(("  [%d/%d] %s segment..."):format(i, total, label))
        local data, s = m3u8._http_get(seg.url, headers)
        if s ~= 200 or not data or #data == 0 then
            f:close(); os.remove(output_path)
            return false, ("segment %d/%d failed (HTTP %d)"):format(i, total, s)
        end
        if key then
            local iv = compute_iv(seg_info.key_iv, seg.seq)
            local ok, dec = pcall(http.aes128_cbc_decrypt, data, key, iv)
            if not ok then
                f:close(); os.remove(output_path)
                return false, ("segment %d/%d decrypt failed: %s"):format(i, total, tostring(dec))
            end
            data = dec
        end
        f:write(data)
    end

    f:close()
    return true, output_path
end

-- ─── Main Entry Point ─────────────────────────────────────────────────────

-- Full HLS download pipeline:
--   1. Fetch m3u8 playlist (resolving master → best variant)
--   2. Download all .ts segments to a single output file
--
-- Returns (true, output_path) on success or (false, error_msg) on failure.
function m3u8.download(m3u8_url, output_dir, filename, headers)
    local dir = output_dir or ""
    if dir ~= "" then
        if not ensure_dir(dir) then
            return false, "cannot create output directory: " .. dir
        end
        if not dir:match("[/\\]$") then dir = dir .. "/" end
    end

    local output_path = dir .. (filename or "hls_output.mp4")

    ludo.logInfo("HLS: resolving playlist " .. m3u8_url)
    local seg_info, err = m3u8.fetch_segments(m3u8_url, headers)
    if not seg_info then return false, err end

    ludo.logInfo(("HLS: %d segments, %s"):format(
        #seg_info.segments,
        seg_info.has_encryption and "ENCRYPTED" or "unencrypted"))

    return m3u8.download_media(seg_info, output_path, headers)
end

return m3u8
