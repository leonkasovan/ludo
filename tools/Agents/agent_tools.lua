-- agent_tools.lua — shared tool implementations for the Ludo agent scripts.
--
-- Dofile'd by "Downloader Agent.lua" (groups: common+download) and
-- "Generate Plugin.lua" (groups: common+plugin). Tools re-register over the
-- module's built-in defaults (registry assignment replaces them), so the
-- module's own defaults remain as fallback if this file cannot be loaded
-- (e.g. wrong working directory).
--
-- Usage (after dofile'ing the agent module):
--   local ok, tools = pcall(dofile, "tools/Agents/agent_tools.lua")
--   if ok and tools then
--       tools.register_all(agent, { groups = { "common", "download" } })
--   end
--
-- Requires the agent module to expose register_tool / resolve_safe_path /
-- read_whole / clamp, and the globals http, json, ludo.
--
-- Tools run synchronously on the UI thread (between API rounds) — the ludo_*
-- tools only queue and never wait for transfers; http_* tools use short
-- default timeouts.

local T = {}

-- --------------------------------------------------------------------
-- Body cache: last 5 fetched bodies keyed by URL (256 KB cap each), so
-- body_search / body_slice avoid re-fetching and re-billing tokens.
-- --------------------------------------------------------------------

local CACHE_MAX   = 5
local CACHE_LIMIT = 256 * 1024
local cache = {}   -- array of { url =, body =, headers = }, newest last

local function cache_store(url, body, headers)
    if not url or type(body) ~= "string" or #body == 0 then return end
    for i, e in ipairs(cache) do
        if e.url == url then table.remove(cache, i) break end
    end
    if #body > CACHE_LIMIT then return end   -- too big to be useful for search
    cache[#cache + 1] = { url = url, body = body, headers = headers or {} }
    while #cache > CACHE_MAX do table.remove(cache, 1) end
end

local function cache_get(url)
    for i = #cache, 1, -1 do
        if cache[i].url == url then return cache[i] end
    end
    return nil
end

local function clamp_chars(s, limit)
    s = tostring(s or "")
    if #s > limit then
        return s:sub(1, limit)
            .. string.format("\n...[truncated, %d total chars — use body_slice(url, offset, length) to page]", #s)
    end
    return s
end

local function valid_pattern(p)
    return pcall(string.find, "probe", tostring(p or ""))
end

-- Content types that must never be dumped into the transcript.
local BINARY_MARKERS = {
    "video/", "audio/", "image/", "application/zip", "application/x-tar",
    "application/octet-stream", "application/pdf",
}

local function binary_content_type(headers, body)
    local ct = tostring(headers and headers["Content-Type"] or ""):lower()
    for _, m in ipairs(BINARY_MARKERS) do
        if ct:find(m, 1, true) then return ct end
    end
    if body:sub(1, 1024):find("\0", 1, true) then
        return ct ~= "" and ct or "unknown (NUL byte in body)"
    end
    return nil
end

-- --------------------------------------------------------------------
-- Registration
-- --------------------------------------------------------------------

function T.register_all(M, opts)
    opts = opts or {}
    local want = {}
    for _, g in ipairs(opts.groups or { "common", "download", "plugin" }) do
        want[g] = true
    end
    local reg = M.register_tool

    -- ----------------------------------------------------------------
    -- common: HTTP tools
    -- ----------------------------------------------------------------

    if want.common then

    reg("http_get",
        "Perform an HTTP GET and return the response body text. For text " ..
        "(HTML/JSON/XML) only — media files are refused, use ludo_download " ..
        "for those. The body is cached: use body_search/body_slice instead " ..
        "of re-fetching the same URL.",
        {
            type = "object",
            properties = {
                url         = { type = "string",  description = "Full URL to fetch" },
                timeout     = { type = "integer", description = "Timeout in seconds (default 15)" },
                max_chars   = { type = "integer", description = "Max body chars returned (default 20000, max 60000)" },
                headers     = { type = "object",  description = "Optional request headers {Name: Value}" },
                headers_out = { type = "boolean", description = "Include response headers in the result" },
                raw         = { type = "boolean", description = "Skip the binary-content guard" },
            },
            required = { "url" },
        },
        function(args)
            local http_opts = { timeout = args.timeout or 15 }
            if args.headers and type(args.headers) == "table" then
                http_opts.headers = args.headers
            end
            local ok, body, status, hdrs = pcall(http.get, args.url, http_opts)
            if not ok then return nil, "HTTP failed: " .. tostring(body) end
            hdrs = hdrs or {}
            cache_store(args.url, body, hdrs)

            local out = { "HTTP " .. tostring(status) }

            if not args.raw then
                local bct = binary_content_type(hdrs, body)
                if bct then
                    return string.format(
                        "HTTP %s\nBINARY %s ~%d bytes — not shown. Use ludo_download for media files.",
                        tostring(status), bct, #body)
                end
            end

            -- C layer silently truncates at HTTP_MAX_BODY (8 MB) — surface it.
            local cl = tonumber(hdrs["Content-Length"])
            if cl and cl >= 8 * 1024 * 1024 and #body < cl then
                out[#out + 1] = string.format("[body truncated: got %d of %s bytes]", #body, tostring(cl))
            end

            if args.headers_out then
                local h = {}
                for k, v in pairs(hdrs) do h[#h + 1] = k .. ": " .. tostring(v) end
                table.sort(h)
                out[#out + 1] = table.concat(h, "\n")
            end

            local limit = tonumber(args.max_chars) or 20000
            if limit > 60000 then limit = 60000 end
            if limit < 200 then limit = 200 end
            out[#out + 1] = clamp_chars(body, limit)
            return table.concat(out, "\n")
        end)

    reg("http_post",
        "Perform an HTTP POST with a body and return the response text. " ..
        "The response is cached for body_search/body_slice.",
        {
            type = "object",
            properties = {
                url          = { type = "string",  description = "Full URL" },
                body         = { type = "string",  description = "Raw request body (e.g. JSON)" },
                content_type = { type = "string",  description = "Content-Type header (default application/json)" },
                timeout      = { type = "integer", description = "Timeout in seconds (default 15)" },
                max_chars    = { type = "integer", description = "Max response chars returned (default 20000, max 60000)" },
            },
            required = { "url", "body" },
        },
        function(args)
            local http_opts = {
                timeout = args.timeout or 15,
                headers = { ["Content-Type"] = args.content_type or "application/json" },
            }
            local ok, resp, status, hdrs = pcall(http.post, args.url, args.body or "", http_opts)
            if not ok then return nil, "HTTP failed: " .. tostring(resp) end
            cache_store(args.url, resp, hdrs)
            local limit = tonumber(args.max_chars) or 20000
            if limit > 60000 then limit = 60000 end
            if limit < 200 then limit = 200 end
            return "HTTP " .. tostring(status) .. "\n" .. clamp_chars(resp, limit)
        end)

    reg("http_head",
        "Fetch only the response headers for a URL: status, Content-Type, " ..
        "Content-Length, Content-Disposition, Location, Accept-Ranges. Use " ..
        "it to check a file's size/type before fetching or downloading it.",
        {
            type = "object",
            properties = {
                url     = { type = "string",  description = "Full URL" },
                timeout = { type = "integer", description = "Timeout in seconds (default 15)" },
            },
            required = { "url" },
        },
        function(args)
            local ok, err, status, hdrs = pcall(http.head, args.url, { timeout = args.timeout or 15 })
            if not ok then return nil, "HEAD failed: " .. tostring(err) end
            hdrs = hdrs or {}
            local keep = {
                ["content-type"] = true, ["content-length"] = true,
                ["content-disposition"] = true, ["location"] = true,
                ["accept-ranges"] = true,
            }
            local lines = { "HTTP " .. tostring(status) }
            for k, v in pairs(hdrs) do
                if keep[k:lower()] then lines[#lines + 1] = k .. ": " .. tostring(v) end
            end
            return table.concat(lines, "\n")
        end)

    reg("http_base64",
        "Encode or decode a string as base64.",
        {
            type = "object",
            properties = {
                action = { type = "string", description = '"encode" or "decode"' },
                data   = { type = "string", description = "Input string" },
            },
            required = { "action", "data" },
        },
        function(args)
            if args.action == "decode" then
                local ok, out = pcall(http.base64_decode, args.data)
                return ok and out or ("Invalid base64: " .. tostring(out))
            end
            return http.base64_encode(args.data or "")
        end)

    reg("body_search",
        "Search a previously fetched body (cached by URL from http_get/" ..
        "http_post) for a Lua pattern. Returns matching lines with line " ..
        "numbers. Much cheaper than re-fetching.",
        {
            type = "object",
            properties = {
                url         = { type = "string",  description = "The URL whose body was fetched" },
                pattern     = { type = "string",  description = "Lua pattern to search for" },
                max_matches = { type = "integer", description = "Max matches to return (default 10)" },
            },
            required = { "url", "pattern" },
        },
        function(args)
            local e = cache_get(args.url)
            if not e then
                return nil, "Body not cached (cache holds the last 5 fetches) — http_get it first"
            end
            if not valid_pattern(args.pattern) then return nil, "Invalid Lua pattern" end
            local max = tonumber(args.max_matches) or 10
            local out, lnum = {}, 0
            for line in (e.body .. "\n"):gmatch("(.-)\n") do
                lnum = lnum + 1
                if line:find(args.pattern) then
                    out[#out + 1] = "line " .. lnum .. ": " .. line:sub(1, 300)
                    if #out >= max then break end
                end
            end
            if #out == 0 then
                return "No matches for " .. tostring(args.pattern) .. " in " .. tostring(args.url)
            end
            local s = table.concat(out, "\n")
            if #out >= max then s = s .. "\n...[stopped at " .. max .. " matches]" end
            return s
        end)

    reg("body_slice",
        "Return a character slice of a cached body (offset 0-based). Use " ..
        "it to page through content truncated by http_get's max_chars.",
        {
            type = "object",
            properties = {
                url    = { type = "string",  description = "The URL whose body was fetched" },
                offset = { type = "integer", description = "0-based char offset (default 0)" },
                length = { type = "integer", description = "Chars to return (default 4000)" },
            },
            required = { "url" },
        },
        function(args)
            local e = cache_get(args.url)
            if not e then return nil, "Body not cached — http_get it first" end
            local off = tonumber(args.offset) or 0
            if off < 0 then off = 0 end
            if off >= #e.body then
                return string.format("Offset %d beyond end (body is %d chars)", off, #e.body)
            end
            local len = tonumber(args.length) or 4000
            if len < 1 then len = 1 end
            local s = e.body:sub(off + 1, off + len)
            return string.format("chars %d-%d of %d:\n%s", off + 1, off + #s, #e.body, s)
        end)

    end -- common (HTTP)

    -- ----------------------------------------------------------------
    -- common: filesystem tools (sandboxed to plugins/ and tools/Agents/)
    -- ----------------------------------------------------------------

    if want.common then

    reg("fs_list",
        "List .lua/.md/.txt files in an allowed directory (plugins/ or tools/Agents/).",
        {
            type = "object",
            properties = {
                dir = { type = "string", description = 'Directory to list, e.g. "plugins/"' },
            },
            required = { "dir" },
        },
        function(args)
            local dir, err = M.resolve_safe_path(args.dir)
            if not dir then return nil, err end
            if dir:sub(-1) ~= "/" then dir = dir .. "/" end
            local names = {}
            local p = io.popen('dir /b "' .. dir:gsub("/", "\\") .. '" 2>nul')
            if p then
                for line in p:lines() do
                    if line:match("%.lua$") or line:match("%.md$") or line:match("%.txt$") then
                        names[#names + 1] = line
                    end
                end
                p:close()
            end
            if #names == 0 then return dir .. " (no .lua/.md/.txt files or not found)" end
            return table.concat(names, "\n")
        end)

    reg("fs_read",
        "Read a text file from plugins/ or tools/Agents/ (e.g. an existing " ..
        "plugin to study its style).",
        {
            type = "object",
            properties = {
                path = { type = "string", description = 'File path, e.g. "plugins/instagram.lua"' },
            },
            required = { "path" },
        },
        function(args)
            local path, err = M.resolve_safe_path(args.path)
            if not path then return nil, err end
            local data = M.read_whole(path)
            if not data then return nil, "File not found: " .. path end
            return M.clamp(data)
        end)

    reg("fs_write",
        "Write/create a Lua plugin file under plugins/ (or tools/Agents/). " ..
        "Creates parent dirs. Fails if the file exists unless overwrite=true.",
        {
            type = "object",
            properties = {
                path      = { type = "string",  description = 'Target file path, e.g. "plugins/tumblr.lua"' },
                content   = { type = "string",  description = "Full file content to write" },
                overwrite = { type = "boolean", description = "Allow overwriting an existing file (default false)" },
            },
            required = { "path", "content" },
        },
        function(args)
            local path, err = M.resolve_safe_path(args.path)
            if not path then return nil, err end
            local content = tostring(args.content or "")
            local existing = M.read_whole(path)
            if existing and args.overwrite ~= true then
                return nil, "File already exists (" .. #existing ..
                            " bytes); pass overwrite=true to replace it"
            end
            local dir = path:match("^(.-)[^/]+$") or ""
            if dir ~= "" then
                os.execute('mkdir "' .. dir:gsub("/", "\\") .. '" 2>nul')
            end
            local f = io.open(path, "wb")
            if not f then return nil, "Cannot open for writing: " .. path end
            f:write(content)
            f:close()
            return (existing and "Overwrote " or "Wrote ") .. #content .. " bytes to " .. path
        end)

    reg("fs_search",
        "Search .lua/.md/.txt files under plugins/ or tools/Agents/ for a " ..
        "Lua pattern. Returns path:line: snippet lines (max 30). Cheaper " ..
        "than reading whole files.",
        {
            type = "object",
            properties = {
                pattern  = { type = "string",  description = "Lua pattern to search for" },
                dir      = { type = "string",  description = 'Directory to search, e.g. "plugins/" (default plugins/)' },
                max_hits = { type = "integer", description = "Max hits (default 30)" },
            },
            required = { "pattern" },
        },
        function(args)
            if not valid_pattern(args.pattern) then return nil, "Invalid Lua pattern" end
            local root, err = M.resolve_safe_path(args.dir or "plugins/")
            if not root then return nil, err end
            if root:sub(-1) ~= "/" then root = root .. "/" end
            local max_hits = tonumber(args.max_hits) or 30
            local files = {}
            local p = io.popen('dir /b "' .. root:gsub("/", "\\") .. '" 2>nul')
            if not p then return nil, "Cannot list " .. root end
            for line in p:lines() do
                if line:match("%.lua$") or line:match("%.md$") or line:match("%.txt$") then
                    files[#files + 1] = line
                end
            end
            p:close()
            local hits = {}
            for _, fname in ipairs(files) do
                local data = M.read_whole(root .. fname)
                if data then
                    local lnum = 0
                    for line in (data .. "\n"):gmatch("(.-)\n") do
                        lnum = lnum + 1
                        if line:find(args.pattern) then
                            hits[#hits + 1] = root .. fname .. ":" .. lnum .. ": "
                                .. line:gsub("\r$", ""):sub(1, 200)
                            if #hits >= max_hits then break end
                        end
                    end
                end
                if #hits >= max_hits then break end
            end
            if #hits == 0 then
                return "No matches for " .. tostring(args.pattern) .. " in " .. root
            end
            return table.concat(hits, "\n")
        end)

    reg("ludo_log",
        "Write a message to the Ludo log (ludo.log) for post-session review.",
        {
            type = "object",
            properties = {
                level   = { type = "string", description = "info | success | error (default info)" },
                message = { type = "string", description = "Message text" },
            },
            required = { "message" },
        },
        function(args)
            local msg = tostring(args.message or "")
            if args.level == "error" then ludo.logError(msg)
            elseif args.level == "success" then ludo.logSuccess(msg)
            else ludo.logInfo(msg) end
            return "logged"
        end)

    end -- common (filesystem)

    -- ----------------------------------------------------------------
    -- download: Ludo download-manager tools (instant, non-blocking)
    -- ----------------------------------------------------------------

    if want.download then

    reg("ludo_process_url",
        "Hand a PAGE url to Ludo's plugin pipeline (same as pasting it into " ..
        "the app): the matching plugin extracts media and queues the " ..
        "download. Returns immediately. Prefer this for page URLs " ..
        "(video-site links). Check results with ludo_status.",
        {
            type = "object",
            properties = {
                url = { type = "string", description = "Page URL, e.g. https://site.com/video/123" },
            },
            required = { "url" },
        },
        function(args)
            local url = tostring(args.url or "")
            if not url:find("^https?://") then
                return nil, "Only http(s) URLs are supported"
            end
            local ok, res = pcall(ludo.addURL, url)
            if not ok then return nil, "addURL failed: " .. tostring(res) end
            return "Queued for the plugin pipeline — check ludo_status / the Downloads tab."
        end)

    reg("ludo_download",
        "Queue a DIRECT media/file URL for download. Non-blocking: returns " ..
        "an id immediately. For page URLs use ludo_process_url instead. " ..
        "Verify with ludo_status before telling the user it succeeded.",
        {
            type = "object",
            properties = {
                url      = { type = "string", description = "Direct file/media URL" },
                filename = { type = "string", description = "Optional output filename" },
                headers  = { type = "object",  description = "Optional extra request headers {Name: Value}" },
            },
            required = { "url" },
        },
        function(args)
            local url = tostring(args.url or "")
            if not url:find("^https?://") then
                return nil, "Only http(s) URLs are supported"
            end
            local headers = nil
            if args.headers and type(args.headers) == "table" then headers = args.headers end
            local ok, id = pcall(ludo.queueDownload, url, args.filename, headers)
            if not ok or type(id) ~= "number" or id < 0 then
                return nil, "Download queue failed: " .. tostring(id or "unknown error")
            end
            return string.format("Queued download id=%d -> %s (verify with ludo_status)",
                                 id, ludo.getOutputDirectory())
        end)

    reg("ludo_status",
        "Get download progress. With id: that download; without id: all " ..
        "downloads (newest first). States: queued/running/paused/completed/failed.",
        {
            type = "object",
            properties = {
                id = { type = "integer", description = "Download id (optional)" },
            },
        },
        function(args)
            local function fmt(st)
                return string.format("id=%d %s %s %.1f%% %d/%d bytes",
                    st.id or 0, tostring(st.filename), tostring(st.state),
                    tonumber(st.progress) or 0,
                    tonumber(st.downloaded_bytes) or 0, tonumber(st.total_bytes) or 0)
            end
            if args.id then
                local ok, st = pcall(ludo.downloadStatus, tonumber(args.id) or 0)
                if not ok or type(st) ~= "table" then
                    return "No download with id " .. tostring(args.id)
                end
                return fmt(st)
            end
            local ok, list = pcall(ludo.listDownloads)
            if not ok or type(list) ~= "table" or #list == 0 then return "No downloads." end
            local lines = {}
            for _, st in ipairs(list) do lines[#lines + 1] = fmt(st) end
            return table.concat(lines, "\n")
        end)

    end -- download

    -- ----------------------------------------------------------------
    -- plugin: plugin inspection tools
    -- ----------------------------------------------------------------

    if want.plugin then

    reg("plugin_probe",
        "Run validate(url) of every plugin in plugins/ and report which " ..
        "plugins match. Use it to find the right plugin for a URL or to " ..
        "test a generated plugin's validate().",
        {
            type = "object",
            properties = {
                url = { type = "string", description = "URL to test" },
            },
            required = { "url" },
        },
        function(args)
            local url = tostring(args.url or "")
            local files = {}
            local p = io.popen('dir /b "plugins" 2>nul')
            if not p then return nil, "Cannot list plugins/" end
            for line in p:lines() do
                if line:match("%.lua$") then files[#files + 1] = line end
            end
            p:close()
            table.sort(files)
            local names = {}
            for _, fname in ipairs(files) do
                local ok, pl = pcall(dofile, "plugins/" .. fname)
                if ok and type(pl) == "table" and type(pl.validate) == "function" then
                    local vok, matched = pcall(pl.validate, url)
                    if vok and matched then
                        names[#names + 1] = fname ..
                            (pl.name and (" (" .. tostring(pl.name) .. ")") or "")
                    end
                end
            end
            if #names == 0 then return "No plugin matches " .. url end
            return "Matching plugins:\n" .. table.concat(names, "\n")
        end)

    end -- plugin

    return T
end

return T
