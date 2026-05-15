-- Shared utilities for Ludo plugins.
-- Usage: local utils = require("utils") or dofile("plugins/utils.lua")
-- NOTE: dofile() returns the module table; require() needs lualib/ path configured.

local utils = {
    name = "utils",
    version = "20260513",
    creator = "ludo",
}

local json = json or require("json")

-- Desktop Chrome user-agent (full)
utils.DESKTOP_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

-- Shorter user-agent (compatible with older servers)
utils.UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

-- Default request timeout (seconds)
utils.TIMEOUT = 30

--- Sanitise a string into a safe filename.
-- Replaces filesystem-unsafe characters with `_` and truncates to `maxlen`.
-- @param s     Input string
-- @param maxlen Maximum length (default 80)
-- @return Safe filename string
function utils.safe_name(s, maxlen)
    maxlen = maxlen or 80
    return (s or ""):gsub("[\\/:*?\"<>|]", "_"):sub(1, maxlen)
end

--- Unescape JSON-embedded URLs (handles \\/ and \\uXXXX sequences).
-- @param s JSON-escaped string
-- @return Decoded string or nil if input is nil
function utils.unescape_json_url(s)
    if not s then return nil end
    s = s:gsub("\\/", "/")
    s = s:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp < 128 then return string.char(cp) end
        return ""
    end)
    return s
end

--- Extract a balanced-brace JSON object from text.
-- Searches for `pattern`, then scans for `{...}` with brace-depth tracking.
-- @param text    HTML/JS text to search
-- @param pattern Lua pattern to locate the JSON block (e.g. "videoData%s*=")
-- @return Decoded Lua table or nil
function utils.extract_json_object(text, pattern)
    local _, pe = text:find(pattern)
    if not pe then return nil end
    local start = text:find("{", pe + 1)
    if not start then return nil end
    local depth, in_str, escaped = 0, false, false
    for i = start, #text do
        local c = text:sub(i, i)
        if escaped then escaped = false
        elseif c == "\\" and in_str then escaped = true
        elseif c == '"' then in_str = not in_str
        elseif not in_str then
            if c == "{" then depth = depth + 1
            elseif c == "}" then
                depth = depth - 1
                if depth == 0 then
                    local ok, val = pcall(json.decode, text:sub(start, i))
                    if ok then return val end
                    return nil
                end
            end
        end
    end
    return nil
end

--- Load a Netscape-format cookie file for the current Lua state.
-- Checks if the file exists; if so, sets it as the active cookiejar.
-- @param path Path to cookies.txt
function utils.load_cookies(path)
    local f = io.open(path, "r")
    if f then
        f:close()
        http.set_cookie(path)
    end
end

return utils
