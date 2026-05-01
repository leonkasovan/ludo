-- iqiyi.lua
-- Ludo plugin for 爱奇艺 (iQiyi) videos.
-- Ported from yt-dlp iqiyi.py extractor (IqiyiIE).
-- Handles: www.iqiyi.com/{path}.html, www.pps.tv/{path}.html
--
-- Note: Only the domestic iqiyi.com API (MD5 signing) is supported.
-- The international iq.com site requires PhantomJS for JS-based cmd5x
-- signing and cannot be supported without a JavaScript engine.

local plugin = { name = "iQiyi", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- Pure Lua MD5 implementation (RFC 1321) using Lua 5.2 bit32
-- Used for iQiyi API signing
local md5 = (function()
    local s = {}
    for i = 0, 63 do
        local v = math.abs(math.sin(i + 1)) * 4294967296
        s[i] = math.floor(v) % 4294967296
    end

    local function F(x, y, z) return bit32.bor(bit32.band(x, y), bit32.band(bit32.bnot(x), z)) end
    local function G(x, y, z) return bit32.bor(bit32.band(x, z), bit32.band(y, bit32.bnot(z))) end
    local function H(x, y, z) return bit32.bxor(x, y, z) end
    local function I(x, y, z) return bit32.bxor(y, bit32.bor(x, bit32.bnot(z))) end

    local function rotate(x, n)
        return bit32.bor(bit32.lshift(x, n), bit32.rshift(x, 32 - n))
    end

    local function byte_to_bits(str)
        local bytes = {}
        for i = 1, #str do bytes[i] = str:byte(i) end
        local len = #bytes
        bytes[#bytes + 1] = 0x80
        while #bytes % 64 ~= 56 do bytes[#bytes + 1] = 0 end
        local bit_len = len * 8
        for i = 0, 7 do bytes[#bytes + 1] = bit32.band(bit32.rshift(bit_len, i * 8), 0xFF) end

        local h0, h1, h2, h3 = 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
        for chunk = 1, #bytes, 64 do
            local w = {}
            for i = 0, 15 do
                w[i] = 0
                for j = 0, 3 do
                    w[i] = bit32.bor(w[i], bit32.lshift(bytes[chunk + i * 4 + j + 1], j * 8))
                end
            end
            local a, b, c, d = h0, h1, h2, h3
            for i = 0, 63 do
                local f, g
                if i <= 15 then f = F(b, c, d); g = i
                elseif i <= 31 then f = G(b, c, d); g = (5 * i + 1) % 16
                elseif i <= 47 then f = H(b, c, d); g = (3 * i + 5) % 16
                else f = I(b, c, d); g = (7 * i) % 16 end
                local temp = d
                d = c
                c = b
                local shift_tbl = {7,12,17,22, 5,9,14,20, 4,11,16,23, 6,10,15,21}
                local shift_idx = math.floor(i / 16) * 4 + (i % 4) + 1
                b = b + rotate((a + f + s[i] + w[g]) % 4294967296, shift_tbl[shift_idx])
                a = temp
            end
            h0 = (h0 + a) % 4294967296
            h1 = (h1 + b) % 4294967296
            h2 = (h2 + c) % 4294967296
            h3 = (h3 + d) % 4294967296
        end

        local function word_to_hex(w)
            local h = ""
            for i = 0, 3 do
                h = h .. string.format("%02x", bit32.band(bit32.rshift(w, i * 8), 0xFF))
            end
            return h
        end
        return word_to_hex(h0) .. word_to_hex(h1) .. word_to_hex(h2) .. word_to_hex(h3)
    end
    return byte_to_bits
end)()

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://[^/]*%.iqiyi%.com/.+%.html") then return true end
    if url:match("^https?://[^/]*%.pps%.tv/.+%.html") then return true end
    return false
end

function plugin.process(url)
    -- Fetch video page to extract tvid and video_id
    ludo.logInfo("iQiyi: fetching video page")
    local body, status = http.get(url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { ["Accept-Language"] = "zh-CN,zh;q=0.9" },
    })
    if status ~= 200 then
        ludo.logError("iQiyi: failed to fetch page (HTTP " .. tostring(status) .. ")")
        return
    end

    local tvid = body:match('data%-player%-tvid%s*=%s*["\'](%d+)')
        or body:match('data%-shareplattrigger%-tvid%s*=%s*["\'](%d+)')
    local video_id = body:match('data%-player%-videoid%s*=%s*["\']([a-f%d]+)')
        or body:match('data%-shareplattrigger%-videoid%s*=%s*["\']([a-f%d]+)')

    if not tvid or not video_id then
        ludo.logError("iQiyi: could not extract tvid/video_id from page")
        return
    end

    ludo.logInfo("iQiyi: tvid=" .. tvid .. " vid=" .. video_id)

    -- Build signed API request
    local tm = tostring(os.time() * 1000)
    local key = "d5fb4bd9d50c4be6948c97edd7254b0e"
    local sc = md5(tm .. key .. tvid)

    local api_url = "http://cache.m.iqiyi.com/jp/tmts/" .. tvid .. "/" .. video_id .. "/"
        .. "?tvid=" .. tvid .. "&vid=" .. video_id
        .. "&src=76f90cbd92f94a2e925d83e8ccd22cb7"
        .. "&sc=" .. sc .. "&t=" .. tm

    local api_body, api_status = http.get(api_url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { Referer = url },
    })
    if api_status ~= 200 then
        ludo.logError("iQiyi: API request failed (HTTP " .. tostring(api_status) .. ")")
        return
    end

    -- Strip var tvInfoJs= prefix before decoding
    local clean = api_body:gsub("^var tvInfoJs=", "")
    local ok, data = pcall(json.decode, clean)
    if not ok then
        ludo.logError("iQiyi: failed to parse API response")
        return
    end

    if data.code ~= "A00000" then
        if data.code == "A00111" then
            ludo.logError("iQiyi: video is geo-restricted to China")
        else
            ludo.logError("iQiyi: API error code " .. tostring(data.code))
        end
        return
    end

    -- Extract title from page
    local title = body:match('class="mod%-play%-tit"[^>]*>([^<]+)')
        or body:match('<span[^>]+data%-videochanged%-title="word"[^>]*>([^<]+)</span>')
        or body:match('<h1[^>]*>([^<]+)')
        or ("iQiyi " .. video_id)
    title = title:gsub("^%s*(.-)%s*$", "%1")

    ludo.logInfo("iQiyi: \"" .. title .. "\"")

    -- Extract HLS URLs from streams
    local streams = data.data and data.data.vidl
    if not streams then
        ludo.logError("iQiyi: no stream data in API response")
        return
    end

    local formats = {}
    for _, stream in ipairs(streams) do
        if stream.m3utx then
            local vd = tostring(stream.vd)
            local height_map = { ["4"] = 720, ["5"] = 1080, ["18"] = 1080, ["2"] = 480, ["1"] = 360 }
            table.insert(formats, {
                url = stream.m3utx,
                format_id = vd,
                height = height_map[vd] or 0,
            })
        end
    end

    if #formats == 0 then
        ludo.logError("iQiyi: no playable streams found")
        return
    end

    table.sort(formats, function(a, b) return a.height > b.height end)
    local best = formats[1]
    ludo.logInfo("iQiyi: selected " .. (best.format_id or (tostring(best.height) .. "p")))

    local m3u8 = dofile("plugins/m3u8.lua")
    local fname = safe_name(title) .. ".mp4"
    local ok_dl, result = m3u8.download(best.url, ludo.getOutputDirectory(), fname,
        { Referer = "https://www.iqiyi.com/" })
    if ok_dl then
        ludo.logSuccess("iQiyi: saved → " .. (result or fname))
    else
        ludo.logError("iQiyi: download failed: " .. tostring(result))
    end
end

return plugin
