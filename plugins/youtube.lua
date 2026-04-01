-- youtube.lua
--
-- Pure Lua Ludo plugin focused on public music.youtube.com watch URLs.
--
-- This intentionally implements a narrow extraction path:
-- - only accepts music.youtube.com/watch?v=... URLs
-- - resolves one default audio stream automatically
-- - skips formats that require signature deciphering, PO tokens, or auth
--
-- It does not try to replicate the full yt-dlp YouTube extractor stack.

local plugin = {
    name = "YouTube Music (pure Lua)",
    version = "20260401",
    creator = "OpenAI Codex",
}

local DOWNLOAD_MODE = ludo.DOWNLOAD_NOW
local REQUEST_TIMEOUT = 30
local MUSIC_HOME_URL = "https://music.youtube.com/"

local WATCH_USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) " ..
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

local PLAYER_CLIENTS = {
    {
        id = "android_vr",
        api_host = "https://www.youtube.com",
        client_name = "ANDROID_VR",
        client_number = 28,
        client_version = "1.65.10",
        android_sdk_version = 32,
        user_agent = "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip",
        os_name = "Android",
        os_version = "12L",
        device_make = "Oculus",
        device_model = "Quest 3",
        hl = "en",
        gl = "US",
    },
    {
        id = "android",
        api_host = "https://www.youtube.com",
        client_name = "ANDROID",
        client_number = 3,
        client_version = "21.02.35",
        android_sdk_version = 30,
        user_agent = "com.google.android.youtube/21.02.35 (Linux; U; Android 11) gzip",
        os_name = "Android",
        os_version = "11",
        hl = "en",
        gl = "US",
    },
    {
        id = "ios",
        api_host = "https://www.youtube.com",
        client_name = "IOS",
        client_number = 5,
        client_version = "21.02.3",
        user_agent = "com.google.ios.youtube/21.02.3 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)",
        os_name = "iPhone",
        os_version = "18.3.2.22D82",
        device_make = "Apple",
        device_model = "iPhone16,2",
        hl = "en",
        gl = "US",
    },
}

local AUDIO_QUALITY_SCORE = {
    AUDIO_QUALITY_ULTRALOW = 0,
    AUDIO_QUALITY_LOW = 10,
    AUDIO_QUALITY_MEDIUM = 20,
    AUDIO_QUALITY_HIGH = 30,
}

local JSON_NULL = {}

local function trim(s)
    return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function is_digit(ch)
    return ch ~= nil and ch ~= "" and ch:match("%d") ~= nil
end

local function utf8_char(codepoint)
    if codepoint <= 0x7F then
        return string.char(codepoint)
    end
    if codepoint <= 0x7FF then
        return string.char(
            0xC0 + math.floor(codepoint / 0x40),
            0x80 + (codepoint % 0x40)
        )
    end
    if codepoint <= 0xFFFF then
        return string.char(
            0xE0 + math.floor(codepoint / 0x1000),
            0x80 + (math.floor(codepoint / 0x40) % 0x40),
            0x80 + (codepoint % 0x40)
        )
    end
    if codepoint <= 0x10FFFF then
        return string.char(
            0xF0 + math.floor(codepoint / 0x40000),
            0x80 + (math.floor(codepoint / 0x1000) % 0x40),
            0x80 + (math.floor(codepoint / 0x40) % 0x40),
            0x80 + (codepoint % 0x40)
        )
    end
    return utf8_char(0xFFFD)
end

local function json_error(index, message)
    error("JSON decode error at character " .. tostring(index) .. ": " .. message)
end

local function skip_json_whitespace(text, index)
    while index <= #text do
        local ch = text:sub(index, index)
        if ch ~= " " and ch ~= "\n" and ch ~= "\r" and ch ~= "\t" then
            break
        end
        index = index + 1
    end
    return index
end

local parse_json_value

local function parse_json_string(text, index)
    index = index + 1
    local parts = {}
    local chunk_start = index

    while index <= #text do
        local ch = text:sub(index, index)

        if ch == '"' then
            table.insert(parts, text:sub(chunk_start, index - 1))
            return table.concat(parts), index + 1
        end

        if ch == "\\" then
            table.insert(parts, text:sub(chunk_start, index - 1))

            local esc = text:sub(index + 1, index + 1)
            if esc == "" then
                json_error(index, "unterminated escape sequence")
            elseif esc == '"' or esc == "\\" or esc == "/" then
                table.insert(parts, esc)
                index = index + 2
            elseif esc == "b" then
                table.insert(parts, "\b")
                index = index + 2
            elseif esc == "f" then
                table.insert(parts, "\f")
                index = index + 2
            elseif esc == "n" then
                table.insert(parts, "\n")
                index = index + 2
            elseif esc == "r" then
                table.insert(parts, "\r")
                index = index + 2
            elseif esc == "t" then
                table.insert(parts, "\t")
                index = index + 2
            elseif esc == "u" then
                local hex = text:sub(index + 2, index + 5)
                if not hex:match("^%x%x%x%x$") then
                    json_error(index, "invalid unicode escape")
                end

                local codepoint = tonumber(hex, 16)
                index = index + 6

                if codepoint >= 0xD800 and codepoint <= 0xDBFF and text:sub(index, index + 1) == "\\u" then
                    local low_hex = text:sub(index + 2, index + 5)
                    if low_hex:match("^%x%x%x%x$") then
                        local low = tonumber(low_hex, 16)
                        if low >= 0xDC00 and low <= 0xDFFF then
                            codepoint = 0x10000
                                + (codepoint - 0xD800) * 0x400
                                + (low - 0xDC00)
                            index = index + 6
                        end
                    end
                end

                table.insert(parts, utf8_char(codepoint))
            else
                json_error(index, "unsupported escape \\" .. esc)
            end

            chunk_start = index
        else
            local byte = string.byte(ch)
            if byte and byte < 32 then
                json_error(index, "unescaped control character")
            end
            index = index + 1
        end
    end

    json_error(index, "unterminated string")
end

local function parse_json_number(text, index)
    local start_index = index
    local ch = text:sub(index, index)

    if ch == "-" then
        index = index + 1
        ch = text:sub(index, index)
    end

    if ch == "0" then
        index = index + 1
    elseif is_digit(ch) then
        repeat
            index = index + 1
            ch = text:sub(index, index)
        until not is_digit(ch)
    else
        json_error(index, "invalid number")
    end

    ch = text:sub(index, index)
    if ch == "." then
        index = index + 1
        ch = text:sub(index, index)
        if not is_digit(ch) then
            json_error(index, "invalid fractional number")
        end
        repeat
            index = index + 1
            ch = text:sub(index, index)
        until not is_digit(ch)
    end

    ch = text:sub(index, index)
    if ch == "e" or ch == "E" then
        index = index + 1
        ch = text:sub(index, index)
        if ch == "+" or ch == "-" then
            index = index + 1
            ch = text:sub(index, index)
        end
        if not is_digit(ch) then
            json_error(index, "invalid exponent")
        end
        repeat
            index = index + 1
            ch = text:sub(index, index)
        until not is_digit(ch)
    end

    local value = tonumber(text:sub(start_index, index - 1))
    if value == nil then
        json_error(start_index, "could not parse number")
    end

    return value, index
end

local function parse_json_array(text, index)
    index = skip_json_whitespace(text, index + 1)
    local result = {}

    if text:sub(index, index) == "]" then
        return result, index + 1
    end

    while true do
        local value
        value, index = parse_json_value(text, index)
        table.insert(result, value)

        index = skip_json_whitespace(text, index)
        local ch = text:sub(index, index)

        if ch == "," then
            index = skip_json_whitespace(text, index + 1)
        elseif ch == "]" then
            return result, index + 1
        else
            json_error(index, "expected ',' or ']'")
        end
    end
end

local function parse_json_object(text, index)
    index = skip_json_whitespace(text, index + 1)
    local result = {}

    if text:sub(index, index) == "}" then
        return result, index + 1
    end

    while true do
        if text:sub(index, index) ~= '"' then
            json_error(index, "expected string key")
        end

        local key
        key, index = parse_json_string(text, index)
        index = skip_json_whitespace(text, index)

        if text:sub(index, index) ~= ":" then
            json_error(index, "expected ':' after object key")
        end

        index = skip_json_whitespace(text, index + 1)

        local value
        value, index = parse_json_value(text, index)
        result[key] = value

        index = skip_json_whitespace(text, index)
        local ch = text:sub(index, index)

        if ch == "," then
            index = skip_json_whitespace(text, index + 1)
        elseif ch == "}" then
            return result, index + 1
        else
            json_error(index, "expected ',' or '}'")
        end
    end
end

parse_json_value = function(text, index)
    index = skip_json_whitespace(text, index)
    local ch = text:sub(index, index)

    if ch == '"' then
        return parse_json_string(text, index)
    elseif ch == "{" then
        return parse_json_object(text, index)
    elseif ch == "[" then
        return parse_json_array(text, index)
    elseif ch == "-" or is_digit(ch) then
        return parse_json_number(text, index)
    elseif text:sub(index, index + 3) == "true" then
        return true, index + 4
    elseif text:sub(index, index + 4) == "false" then
        return false, index + 5
    elseif text:sub(index, index + 3) == "null" then
        return JSON_NULL, index + 4
    end

    json_error(index, "unexpected value")
end

local function json_decode(text)
    local value, index = parse_json_value(text, 1)
    index = skip_json_whitespace(text, index)
    if index <= #text then
        json_error(index, "trailing characters")
    end
    return value
end

local function json_escape(str)
    str = tostring(str or "")
    str = str:gsub("\\", "\\\\")
    str = str:gsub('"', '\\"')
    str = str:gsub("\b", "\\b")
    str = str:gsub("\f", "\\f")
    str = str:gsub("\n", "\\n")
    str = str:gsub("\r", "\\r")
    str = str:gsub("\t", "\\t")
    return '"' .. str .. '"'
end

local function normalize_json_response(text)
    text = tostring(text or "")
    if text:sub(1, 3) == "\239\187\191" then
        text = text:sub(4)
    end
    text = trim(text)
    if text:sub(1, 4) == ")]}'" then
        text = trim(text:sub(5))
    end
    return text
end

local function parse_query(query)
    local values = {}
    for pair in string.gmatch(query or "", "([^&]+)") do
        local key, value = pair:match("^([^=]+)=?(.*)$")
        if key and key ~= "" then
            values[http.url_decode(key)] = http.url_decode(value or "")
        end
    end
    return values
end

local function extract_video_id(url)
    if type(url) ~= "string" then
        return nil
    end

    local parts = http.parse_url(url)
    if not parts or not parts.host or not parts.path then
        return nil
    end

    local host = string.lower(parts.host)
    if host ~= "music.youtube.com" and host ~= "www.music.youtube.com" then
        return nil
    end

    if not parts.path:match("^/watch/?$") then
        return nil
    end

    local query = parse_query(parts.query)
    local video_id = query.v
    if not video_id or #video_id ~= 11 or not video_id:match("^[%w_-]+$") then
        return nil
    end

    return video_id
end

local function get_music_page(url)
    local body, status = http.get(url, {
        user_agent = WATCH_USER_AGENT,
        follow_redirects = true,
        timeout = REQUEST_TIMEOUT,
        headers = {
            ["Accept-Language"] = "en-US,en;q=0.9",
        },
    })

    if status ~= 200 then
        return nil, "HTTP " .. tostring(status) .. " while fetching " .. url
    end

    local final_url = http.get_last_url()
    if final_url == "" then
        final_url = url
    end

    return {
        body = body,
        final_url = final_url,
    }
end

local function extract_page_value(page_body, key)
    if type(page_body) ~= "string" or page_body == "" then
        return nil
    end
    return page_body:match('"' .. key .. '"%s*:%s*"([^"]+)"')
end

local function load_api_context(watch_url)
    local pages_to_try = { watch_url, MUSIC_HOME_URL }
    local last_error = nil

    for _, candidate_url in ipairs(pages_to_try) do
        local page, err = get_music_page(candidate_url)
        if page then
            local api_key = extract_page_value(page.body, "INNERTUBE_API_KEY")
            if api_key then
                return {
                    api_key = api_key,
                    visitor_data = extract_page_value(page.body, "VISITOR_DATA"),
                    referer = page.final_url,
                }
            end
            last_error = "Could not find INNERTUBE_API_KEY on " .. candidate_url
        else
            last_error = err
        end
    end

    return nil, last_error or "Could not load Music YouTube configuration"
end

local function build_player_request(video_id, client)
    local client_fields = {
        '"hl":', json_escape(client.hl), ",",
        '"gl":', json_escape(client.gl), ",",
        '"clientName":', json_escape(client.client_name), ",",
        '"clientVersion":', json_escape(client.client_version),
    }

    if client.android_sdk_version then
        table.insert(client_fields, ',')
        table.insert(client_fields, '"androidSdkVersion":')
        table.insert(client_fields, tostring(client.android_sdk_version))
    end
    if client.user_agent then
        table.insert(client_fields, ',')
        table.insert(client_fields, '"userAgent":')
        table.insert(client_fields, json_escape(client.user_agent))
    end
    if client.os_name then
        table.insert(client_fields, ',')
        table.insert(client_fields, '"osName":')
        table.insert(client_fields, json_escape(client.os_name))
    end
    if client.os_version then
        table.insert(client_fields, ',')
        table.insert(client_fields, '"osVersion":')
        table.insert(client_fields, json_escape(client.os_version))
    end
    if client.device_make then
        table.insert(client_fields, ',')
        table.insert(client_fields, '"deviceMake":')
        table.insert(client_fields, json_escape(client.device_make))
    end
    if client.device_model then
        table.insert(client_fields, ',')
        table.insert(client_fields, '"deviceModel":')
        table.insert(client_fields, json_escape(client.device_model))
    end

    return table.concat({
        "{",
        '"videoId":', json_escape(video_id), ",",
        '"context":{"client":{',
        table.concat(client_fields),
        "}}",
        ',"contentCheckOk":true',
        ',"racyCheckOk":true',
        "}",
    })
end

local function fetch_player_response(video_id, api_context, client)
    local api_host = client.api_host or "https://www.youtube.com"
    local endpoint = api_host .. "/youtubei/v1/player?prettyPrint=false&key="
        .. http.url_encode(api_context.api_key)

    local headers = {
        ["Content-Type"] = "application/json",
        ["Origin"] = api_host,
        ["Referer"] = api_context.referer or MUSIC_HOME_URL,
        ["X-YouTube-Client-Name"] = tostring(client.client_number),
        ["X-YouTube-Client-Version"] = client.client_version,
        ["Accept-Language"] = "en-US,en;q=0.9",
    }

    if api_context.visitor_data and api_context.visitor_data ~= "" then
        headers["X-Goog-Visitor-Id"] = api_context.visitor_data
    end

    local body, status = http.post(
        endpoint,
        build_player_request(video_id, client),
        {
            user_agent = client.user_agent or WATCH_USER_AGENT,
            follow_redirects = true,
            timeout = REQUEST_TIMEOUT,
            headers = headers,
        }
    )

    if status ~= 200 then
        return nil, "HTTP " .. tostring(status) .. " from the player API"
    end

    local ok, data = pcall(json_decode, normalize_json_response(body))
    if not ok then
        return nil, trim(data)
    end

    return data
end

local function get_title(player_response, video_id)
    if type(player_response) == "table"
        and type(player_response.videoDetails) == "table"
        and type(player_response.videoDetails.title) == "string"
        and player_response.videoDetails.title ~= "" then
        return player_response.videoDetails.title
    end
    return "YouTube Music track " .. video_id
end

local function get_playability_error(player_response)
    if type(player_response) ~= "table" or type(player_response.playabilityStatus) ~= "table" then
        return nil
    end

    local status = player_response.playabilityStatus.status
    if status == "OK" or status == nil then
        return nil
    end

    local parts = {}
    if type(player_response.playabilityStatus.reason) == "string"
        and player_response.playabilityStatus.reason ~= "" then
        table.insert(parts, player_response.playabilityStatus.reason)
    end

    if type(player_response.playabilityStatus.messages) == "table" then
        for _, message in ipairs(player_response.playabilityStatus.messages) do
            if type(message) == "string" and message ~= "" then
                table.insert(parts, message)
            end
        end
    end

    if #parts == 0 then
        table.insert(parts, tostring(status))
    end

    return table.concat(parts, " / ")
end

local function is_audio_only_format(fmt)
    local mime = tostring(fmt.mimeType or "")
    if mime:match("^audio/") then
        return true
    end
    return fmt.audioQuality ~= nil
        and fmt.width == nil
        and fmt.height == nil
        and fmt.qualityLabel == nil
end

local function has_audio(fmt)
    local mime = tostring(fmt.mimeType or "")
    return mime:match("^audio/")
        or fmt.audioQuality ~= nil
        or fmt.audioSampleRate ~= nil
        or fmt.audioChannels ~= nil
        or fmt.averageBitrate ~= nil
        or fmt.bitrate ~= nil
end

local function score_format(fmt)
    local score = 0
    score = score + (AUDIO_QUALITY_SCORE[fmt.audioQuality] or 0) * 1000000
    score = score + (tonumber(fmt.averageBitrate) or tonumber(fmt.bitrate) or 0)

    local mime = tostring(fmt.mimeType or "")
    if mime:match("^audio/mp4") then
        score = score + 100
    elseif mime:match("^audio/webm") then
        score = score + 50
    end

    return score
end

local function describe_format(fmt)
    local parts = {}

    if fmt.itag then
        table.insert(parts, "itag " .. tostring(fmt.itag))
    end
    if type(fmt.mimeType) == "string" and fmt.mimeType ~= "" then
        table.insert(parts, fmt.mimeType)
    end
    if type(fmt.audioQuality) == "string" and fmt.audioQuality ~= "" then
        table.insert(parts, fmt.audioQuality)
    end

    local bitrate = tonumber(fmt.averageBitrate) or tonumber(fmt.bitrate)
    if bitrate and bitrate > 0 then
        table.insert(parts, tostring(math.floor((bitrate / 1000) + 0.5)) .. " kbps")
    end

    return table.concat(parts, ", ")
end

local function is_downloader_safe_url(url)
    local parts = http.parse_url(url)
    if not parts or type(parts.query) ~= "string" then
        return true
    end

    local query = parse_query(parts.query)
    if query.n and query.n ~= "" then
        return false, "needs n challenge solving"
    end

    return true
end

local function probe_stream_url(url)
    local _, status = http.head(url, {
        follow_redirects = true,
        timeout = REQUEST_TIMEOUT,
    })

    return status
end

local function pick_default_format(player_response)
    if type(player_response) ~= "table" or type(player_response.streamingData) ~= "table" then
        return nil, "No streamingData was returned for this track"
    end

    local candidates = {
        player_response.streamingData.adaptiveFormats,
        player_response.streamingData.formats,
    }

    local best_audio = nil
    local best_audio_score = nil
    local best_fallback = nil
    local best_fallback_score = nil

    for _, group in ipairs(candidates) do
        if type(group) == "table" then
            for _, fmt in ipairs(group) do
                if type(fmt) == "table"
                    and type(fmt.url) == "string"
                    and fmt.url:match("^https?://")
                    and fmt.signatureCipher == nil
                    and fmt.cipher == nil
                    and fmt.drmFamilies == nil then

                    local is_safe = is_downloader_safe_url(fmt.url)
                    if not is_safe then
                        goto continue
                    end

                    local score = score_format(fmt)
                    if is_audio_only_format(fmt) then
                        if best_audio == nil or score > best_audio_score then
                            best_audio = fmt
                            best_audio_score = score
                        end
                    elseif has_audio(fmt) then
                        if best_fallback == nil or score > best_fallback_score then
                            best_fallback = fmt
                            best_fallback_score = score
                        end
                    end
                end
                ::continue::
            end
        end
    end

    if best_audio then
        return best_audio
    end
    if best_fallback then
        return best_fallback
    end

    return nil, (
        "No direct audio URL was exposed. " ..
        "This pure Lua plugin only supports formats that already include a direct stream URL."
    )
end

function plugin.validate(url)
    return extract_video_id(url) ~= nil
end

function plugin.process(url)
    local video_id = extract_video_id(url)
    if not video_id then
        ludo.logError("This plugin only supports music.youtube.com/watch?v=... URLs")
        return nil
    end

    ludo.logInfo("Resolving Music YouTube audio for " .. video_id)

    local api_context, context_error = load_api_context(url)
    if not api_context then
        ludo.logError(context_error or "Could not read Music YouTube page configuration")
        return nil
    end

    local title = nil
    local last_error = nil

    for _, client in ipairs(PLAYER_CLIENTS) do
        local player_response, player_error = fetch_player_response(video_id, api_context, client)
        if not player_response then
            last_error = (client.id or client.client_name) .. ": " .. (player_error or "Could not fetch the player response")
            goto continue_client
        end

        local playability_error = get_playability_error(player_response)
        if playability_error then
            last_error = (client.id or client.client_name) .. ": playback unavailable: " .. playability_error
            goto continue_client
        end

        title = title or get_title(player_response, video_id)

        local selected_format, format_error = pick_default_format(player_response)
        if not selected_format then
            last_error = (client.id or client.client_name) .. ": " .. (format_error or "Could not resolve a direct audio format")
            goto continue_client
        end

        local probe_status = probe_stream_url(selected_format.url)
        if probe_status ~= 200 and probe_status ~= 206 then
            last_error = (client.id or client.client_name) .. ": preflight returned HTTP " .. tostring(probe_status)
            goto continue_client
        end

        local id, status, output = ludo.newDownload(
            selected_format.url,
            ludo.getOutputDirectory(),
            DOWNLOAD_MODE
        )

        if status == 200 or status == 206 then
            ludo.logSuccess("Queued Music YouTube download: " .. (title or get_title(player_response, video_id)))
            ludo.logInfo("Selected default audio stream (" .. (client.id or client.client_name) .. "): " .. describe_format(selected_format))
            ludo.logInfo("Output: " .. tostring(output))
            return id
        end

        last_error = (client.id or client.client_name) .. ": Ludo queue returned HTTP " .. tostring(status)

        ::continue_client::
    end

    ludo.logError(last_error or "Could not resolve a downloader-safe Music YouTube stream")
    return nil
end

return plugin
