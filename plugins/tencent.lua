-- tencent.lua
-- Ludo plugin for 腾讯视频 (Tencent Video / v.qq.com / WeTV).
-- Ported from yt-dlp tencent.py extractor.
-- Handles: v.qq.com/x/page/VID, v.qq.com/x/cover/SID/VID,
--          wetv.vip/{lang}/play/SID/VID, wetv.vip/play/SID

local plugin = { name = "Tencent", version = "20250501", creator = "opencode" }
local json = json or require("json")

local TIMEOUT = 30
local UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

local function safe_name(s)
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, 80)
end

-- AES-CBC-encrypt with whitespace padding (0x20 spaces) for Tencent ckey
local function aes_cbc_encrypt_ws(data, key, iv)
    local pad = 16 - (#data % 16)
    local padded = data .. string.rep(string.char(0x20), pad)
    local out = {}
    local prev = iv
    for i = 1, #padded, 16 do
        local blk = padded:sub(i, i + 15)
        local xored = {}
        for j = 1, 16 do
            xored[j] = string.char(bit32.bxor(blk:byte(j), prev:byte(j)))
        end
        local enc = http.aes128_encrypt_block(table.concat(xored), key)
        out[#out + 1] = enc
        prev = enc
    end
    return table.concat(out)
end

-- Generate ckey for Tencent API
local function generate_ckey(video_id, url, guid, app_ver, platform, host)
    local ua_short = UA:sub(1, 48):lower()
    local url_short = url:sub(1, 48)
    local ts = math.floor(os.time())
    local payload = ("%s|%d|mg3c3b04ba|%s|%s|%s|%s|%s||Mozilla|Netscape|Windows x86_64|00|")
        :format(video_id, ts, app_ver, guid, platform, url_short, ua_short)
    local checksum = 0
    for i = 1, #payload do checksum = checksum + payload:byte(i) end
    local plaintext = "|" .. checksum .. "|" .. payload

    local aes_key = "Ok\xda\xa3\x9e/\x8c\xb0\x7f^r-\x9e\xde\xf3\x14"
    local aes_iv  = "\x01PJ\xf3V\xe6\x19\xcf.B\xbb\xa6\x8c?p\xf9"

    local encrypted = aes_cbc_encrypt_ws(plaintext, aes_key, aes_iv)
    local hex = ""
    for i = 1, #encrypted do hex = hex .. string.format("%02X", encrypted:byte(i)) end
    return hex
end

-- Call Tencent video API
local function call_tencent_api(video_id, series_id, url, app_ver, platform, host, referer)
    local guid = ""
    for _ = 1, 16 do
        local r = math.random(0, 35)
        guid = guid .. (r < 10 and string.char(48 + r) or string.char(87 + r))
    end
    local ckey = generate_ckey(video_id, url, guid, app_ver, platform, host)

    local q = "?vid=" .. video_id .. "&cid=" .. (series_id or "")
        .. "&cKey=" .. ckey .. "&encryptVer=8.1"
        .. "&spcaptiontype=1&sphls=2&dtype=3"
        .. "&defn=shd&spsrt=2&sphttps=1&otype=json&spwm=1"
        .. "&hevclv=28&drm=40&spvideo=4&spsfrhdr=100"
        .. "&host=" .. host .. "&referer=" .. referer
        .. "&ehost=" .. url
        .. "&appVer=" .. app_ver .. "&platform=" .. platform
        .. "&guid=" .. guid
        .. "&flowid=" .. guid .. string.rep("0", 16)

    local api_url
    if platform == "4830201" then
        api_url = "https://play.wetv.vip/getvinfo" .. q
    else
        api_url = "https://h5vv6.video.qq.com/getvinfo" .. q
    end

    local body, status = http.get(api_url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { Referer = referer },
    })
    if status ~= 200 then return nil, "HTTP " .. tostring(status) end

    -- Try to extract JSON from JSONP wrapper or use raw body
    local resp_clean = body:match("QZOutputJson=(.+)")
    if resp_clean then
        resp_clean = resp_clean:gsub(";%s*$", "")
    else
        resp_clean = body
    end
    -- Save debug body
    local df = io.open(ludo.getOutputDirectory() .. "/tencent_debug.json", "w")
    if df then df:write(body); df:close() end
    local ok, data = pcall(json.decode, resp_clean)
    if not ok then
        ludo.logInfo("Tencent: saved API response to tencent_debug.json")
        return nil, "JSON decode failed"
    end
    if not ok then return nil, "JSON decode failed" end
    if data.code ~= "0.0" then
        local msg = data.msg or ("error code " .. tostring(data.code))
        if msg:find("所在区域") or msg:find("copyright") then
            return nil, "geo-restricted"
        end
        return nil, msg
    end
    return data, nil
end

-- Parse API response to get HLS URLs
local function extract_streams(api_response)
    local vi = api_response.vl and api_response.vl.vi and api_response.vl.vi[1]
    if not vi then return nil, "no video info in response" end
    local formats = {}
    for _, ui in ipairs(vi.ul and vi.ul.ui or {}) do
        local url = ui.url
        if url then
            local pt = ""
            if ui.hls and ui.hls.pt then pt = ui.hls.pt end
            table.insert(formats, {
                url = url .. pt,
                height = tonumber(vi.vh) or 0,
                width = tonumber(vi.vw) or 0,
            })
        end
    end
    if #formats == 0 then return nil, "no stream URLs found" end
    return formats, nil
end

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    if url:match("^https?://v%.qq%.com/x/page/") then return true end
    if url:match("^https?://v%.qq%.com/x/cover/") then return true end
    if url:match("^https?://wetv%.vip/.*/play/") then return true end
    if url:match("^https?://[^/]+%.wetv%.vip/.*/play/") then return true end
    return false
end

function plugin.process(url)
    -- Determine platform from URL
    local is_wetv = url:match("wetv%.vip") ~= nil

    -- Extract video_id and optional series_id
    local video_id, series_id
    if is_wetv then
        -- wetv.vip/.../play/SID/VID or wetv.vip/.../play/SID
        video_id = url:match("play/[^/]+/([%w]+)")
        series_id = url:match("play/([%w]+)")
        if not video_id then video_id = series_id end
    else
        -- v.qq.com/x/page/VID or v.qq.com/x/cover/SID/VID.html
        video_id = url:match("x/page/([%w]+)")
            or url:match("x/cover/[%w]+/([%w]+)")
        series_id = url:match("x/cover/([%w]+)")
    end
    if not video_id then
        ludo.logError("Tencent: could not extract video ID")
        return
    end

    local app_ver = is_wetv and "3.5.57" or "3.5.57"
    local platform = is_wetv and "4830201" or "10901"
    local host = is_wetv and "wetv.vip" or "v.qq.com"
    local referer = is_wetv and "wetv.vip" or "v.qq.com"

    ludo.logInfo("Tencent: processing " .. video_id .. (series_id and (" (series " .. series_id .. ")") or ""))

    local data, err = call_tencent_api(video_id, series_id or "", url, app_ver, platform, host, referer)
    if not data then
        if err == "geo-restricted" then
            ludo.logError("Tencent: video is geo-restricted")
        else
            ludo.logError("Tencent: API failed: " .. tostring(err))
        end
        return
    end

    -- Try multiple qualities
    local format_entries = {}
    local fmts, ferr = extract_streams(data)
    if fmts then
        for _, f in ipairs(fmts) do table.insert(format_entries, f) end
    end

    -- Try other qualities
    local qualities_list = { "fhd", "hd", "sd", "ld" }
    for _, q in ipairs(qualities_list) do
        local d2, e2 = call_tencent_api(video_id, series_id or "", url, app_ver, platform,
            string.gsub(host, "v%.qq%.com", "v.qq.com"), referer)
        if d2 then
            -- Modify quality param by using the quality name
            local q_url = string.gsub(url, "defn=%w+", "defn=" .. q)
            local f2, _ = extract_streams(d2)
            if f2 then
                for _, f in ipairs(f2) do table.insert(format_entries, f) end
            end
            break
        end
    end

    if #format_entries == 0 then
        ludo.logError("Tencent: no playable formats found")
        return
    end

    -- Get title from page
    local page_body, page_status = http.get(url, {
        user_agent = UA, timeout = TIMEOUT,
        headers = { Referer = referer },
    })
    local title = ("Tencent " .. video_id)
    if page_status == 200 then
        local og_title = page_body:match('<meta[^>]+og:title[^>]+content="([^"]+)"')
        if og_title then title = og_title end
    end

    ludo.logInfo("Tencent: \"" .. title .. "\"")

    table.sort(format_entries, function(a, b) return a.height > b.height end)
    local best = format_entries[1]

    ludo.logInfo("Tencent: selected " .. tostring(best.height) .. "p")

    local m3u8 = dofile("plugins/m3u8.lua")
    local fname = safe_name(title) .. ".mp4"
    local hdrs = { Referer = "https://" .. host .. "/" }
    local ok_dl, result = m3u8.download(best.url, ludo.getOutputDirectory(), fname, hdrs)
    if ok_dl then
        ludo.logSuccess("Tencent: saved → " .. (result or fname))
    else
        ludo.logError("Tencent: download failed: " .. tostring(result))
    end
end

return plugin
