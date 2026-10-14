-- Agent: DeepSeek V4.1 Flash (OpenAgentic)
-- Model config sourced from ~/.pi/agent/models.json
--  id:             deepseek-v4.1-flash
--  contextWindow:  1000000
--  maxTokens:      64000
--  api:            openai-completions (POST {baseUrl}/chat/completions)
--
-- Usage:
--   local agent = dofile("tools/Agents/deepseek-v4.1-flash.lua")
--   local resp, err = agent.chat({ { role = "user", content = "Hi!" } })
--   print(resp.content)
--
-- Tool use (function calling):
--   agent.register_tool("http_get", "Fetch a URL", schema, function(args) ... end)
--   local resp, err = agent.chat(msgs, { enable_tools = true })
--   The agent loops: model replies with tool_calls -> we execute the Lua
--   function -> results are sent back as role="tool" messages -> model
--   continues, until it produces a final answer (or max_tool_rounds).
--
-- Note: http.post is synchronous (the http module only offers post_async
-- for background runs), so chat() blocks until the response arrives.

local json = json or require("json")

-- Default system context (plugin rules) — loaded automatically at startup.
-- Overridable per-call via opts.system.
local here = debug and debug.getinfo and debug.getinfo(1, "S").source or ""
here = here:gsub("^@", ""):gsub("[^/\\]*$", "")   -- dir of this file (may be "")
local ok_ctx, ctx = pcall(dofile, here .. "agent_context.lua")
if not ok_ctx then ok_ctx, ctx = pcall(dofile, "tools/Agents/agent_context.lua") end
if not ok_ctx then ok_ctx, ctx = pcall(dofile, "agent_context.lua") end

local M = {}

M.name           = "DeepSeek V4.1 Flash"
M.model          = "deepseek-v4.1-flash"
M.provider       = "openagentic"
M.base_url       = "https://openagentic.id/api/v1"
M.api_key        = "" -- set via env/config; never commit a live key
M.context_window = 1000000
M.max_tokens     = 64000

-- Plugin rules context -------------------------------------------------
-- M.default_system: rules + §8/§9 of ludo_scripting.md (plugin system +
-- yt-dlp conversion guide), loaded once at startup. nil if unavailable.
if ok_ctx then
    M.context          = ctx
    M.default_system   = "You are an expert Lua developer writing plugins for Ludo.\n\n" .. ctx.full({ manual_sections = { 8, 9 } })
end

-- ====================================================================
-- Tool registry (function calling)
-- ====================================================================

-- name -> { description, parameters (JSON schema table), func(args_table) }
M.tools_registry = {}

-- func receives the decoded args table and returns a string (or table,
-- which is JSON-encoded). It runs in the caller's thread — keep it
-- synchronous and reasonably fast.
function M.register_tool(name, description, parameters, func)
    M.tools_registry[name] = {
        description = description,
        parameters  = parameters,   -- JSON Schema: { type="object", properties={...}, required={...} }
        func        = func,
    }
end

-- Cap tool result size sent back to the model (1M ctx allows generous caps)
local MAX_RESULT = 40000

local function clamp(s)
    s = tostring(s or "")
    if #s > MAX_RESULT then
        return s:sub(1, MAX_RESULT) .. string.format("\n...[truncated, %d total chars]", #s)
    end
    return s
end

-- Execute a tool by name with a JSON arguments string (as sent by the model).
-- Returns (result_string, nil) or (nil, error).
function M.execute_tool(name, arguments_json)
    local tool = M.tools_registry[name]
    if not tool then
        return nil, "Unknown tool: " .. tostring(name)
    end
    local args = {}
    if arguments_json and arguments_json ~= "" then
        local ok, decoded = pcall(json.decode, arguments_json)
        if not ok then
            return nil, "Bad tool arguments JSON: " .. tostring(decoded)
        end
        args = decoded or {}
    end
    local ok, result, terr = pcall(tool.func, args)
    if not ok then
        return nil, "Tool error: " .. tostring(result)
    end
    if result == nil and terr ~= nil then
        -- tool signaled an error via `return nil, errmsg`
        return nil, tostring(terr)
    end
    if type(result) == "table" then
        local ok2, encoded = pcall(json.encode, result)
        result = ok2 and encoded or "{}"
    end
    return clamp(result), nil
end

-- Build the "tools" array for the request body (OpenAI format).
function M.build_tools_json()
    local tools = {}
    for name, t in pairs(M.tools_registry) do
        tools[#tools + 1] = {
            type     = "function",
            function_ = {
                name        = name,
                description = t.description,
                parameters  = t.parameters or { type = "object", properties = {} },
            },
        }
    end
    -- "function" is a Lua keyword — rename after encoding
    local body = json.encode({ tools = tools })
    body = body:gsub('"function_"', '"function"')
    return body
end

-- ====================================================================
-- Default tools: expose Ludo's http module + file I/O to the agent
-- ====================================================================

-- File tools operate only inside these roots (relative to CWD = app root).
local ALLOWED_ROOTS = { "plugins/", "tools/Agents/" }

local function resolve_safe_path(path)
    path = tostring(path or "")
    -- normalize and reject traversal
    path = path:gsub("\\", "/")
    if path:find("..", 1, true) then
        return nil, "Path traversal not allowed"
    end
    local ok = false
    for _, root in ipairs(ALLOWED_ROOTS) do
        if path:sub(1, #root) == root then ok = true break end
    end
    if not ok then
        return nil, "Path must be under one of: " .. table.concat(ALLOWED_ROOTS, ", ")
    end
    return path
end

local function read_whole(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local d = f:read("*a")
    f:close()
    return d
end

-- Exported for tools/Agents/agent_tools.lua (shared tool implementations)
M.resolve_safe_path = resolve_safe_path
M.read_whole        = read_whole
M.clamp             = clamp

M.register_tool("http_get",
    "Perform an HTTP GET request and return the response body text.",
    {
        type = "object",
        properties = {
            url     = { type = "string", description = "Full URL to fetch" },
            timeout = { type = "integer", description = "Timeout in seconds (default 30)" },
        },
        required = { "url" },
    },
    function(args)
        local opts = { timeout = args.timeout or 30 }
        if args.headers and type(args.headers) == "table" then opts.headers = args.headers end
        local ok, body, status = pcall(http.get, args.url, opts)
        if not ok then return nil, "HTTP failed: " .. tostring(body) end
        return clamp("HTTP " .. tostring(status) .. "\n" .. body)
    end)

M.register_tool("http_post",
    "Perform an HTTP POST request with a body and return the response text.",
    {
        type = "object",
        properties = {
            url  = { type = "string", description = "Full URL" },
            body = { type = "string", description = "Raw request body (e.g. JSON)" },
            content_type = { type = "string", description = "Content-Type header (default application/json)" },
        },
        required = { "url", "body" },
    },
    function(args)
        local opts = {
            timeout = args.timeout or 30,
            headers = { ["Content-Type"] = args.content_type or "application/json" },
        }
        local ok, body, status = pcall(http.post, args.url, args.body or "", opts)
        if not ok then return nil, "HTTP failed: " .. tostring(body) end
        return clamp("HTTP " .. tostring(status) .. "\n" .. body)
    end)

M.register_tool("http_base64",
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

M.register_tool("fs_list",
    "List .lua files in an allowed directory (plugins/ or tools/Agents/).",
    {
        type = "object",
        properties = {
            dir = { type = "string", description = 'Directory to list, e.g. "plugins/"' },
        },
        required = { "dir" },
    },
    function(args)
        local dir, err = resolve_safe_path(args.dir)
        if not dir then return nil, err end
        if dir:sub(-1) ~= "/" then dir = dir .. "/" end
        local names = {}
        local p = io.popen('dir /b "' .. dir:gsub("/", "\\\\") .. '" 2>nul')
        if p then
            for line in p:lines() do
                if line:match("%.lua$") or line:match("%.md$") then
                    names[#names + 1] = line
                end
            end
            p:close()
        end
        if #names == 0 then return dir .. " (no .lua/.md files or not found)" end
        return table.concat(names, "\n")
    end)

M.register_tool("fs_read",
    "Read a text file from plugins/ or tools/Agents/ (e.g. an existing plugin to study its style).",
    {
        type = "object",
        properties = {
            path = { type = "string", description = 'File path, e.g. "plugins/instagram.lua"' },
        },
        required = { "path" },
    },
    function(args)
        local path, err = resolve_safe_path(args.path)
        if not path then return nil, err end
        local data = read_whole(path)
        if not data then return nil, "File not found: " .. path end
        return clamp(data)
    end)

M.register_tool("fs_write",
    "Write/create a Lua plugin file under plugins/ (or tools/Agents/). Creates parent dirs. Fails if the file exists unless overwrite=true. Returns bytes written.",
    {
        type = "object",
        properties = {
            path      = { type = "string", description = 'Target file path, e.g. "plugins/tumblr.lua"' },
            content   = { type = "string", description = "Full file content to write" },
            overwrite = { type = "boolean", description = "Allow overwriting an existing file (default false)" },
        },
        required = { "path", "content" },
    },
    function(args)
        local path, err = resolve_safe_path(args.path)
        if not path then return nil, err end
        local content = tostring(args.content or "")
        local existing = read_whole(path)
        if existing and args.overwrite ~= true then
            return nil, "File already exists (" .. #existing .. " bytes); pass overwrite=true to replace it"
        end
        -- create parent directories
        local dir = path:match("^(.-)[^/]+$") or ""
        if dir ~= "" then
            os.execute('mkdir "' .. dir:gsub("/", "\\\\") .. '" 2>nul')
        end
        local f = io.open(path, "wb")
        if not f then return nil, "Cannot open for writing: " .. path end
        f:write(content)
        f:close()
        return (existing and "Overwrote " or "Wrote ") .. #content .. " bytes to " .. path
    end)

-- ====================================================================
-- Request building
-- ====================================================================

-- messages: array of { role = "system"|"user"|"assistant"|"tool", content = "..." }
--   assistant messages may carry .tool_calls (as returned by parse_response)
--   tool messages carry .tool_call_id
-- opts: optional { temperature, max_tokens, system, enable_tools,
--                 max_tool_rounds, use_plugin_rules }
--   opts.system            — explicit system prompt (wins if non-empty)
--   opts.use_plugin_rules  — default true; use plugin rules as default context
function M.build_body(messages, opts)
    opts = opts or {}
    local system = opts.system
    if (not system or system == "") and opts.use_plugin_rules ~= false and ok_ctx then
        system = M.default_system
    end
    local msgs = {}
    if system and system ~= "" then
        msgs[#msgs + 1] = { role = "system", content = system }
    end
    for _, m in ipairs(messages) do
        local copy = { role = m.role, content = m.content }
        if m.tool_calls then copy.tool_calls = m.tool_calls end
        if m.tool_call_id then copy.tool_call_id = m.tool_call_id end
        if m.name then copy.name = m.name end
        msgs[#msgs + 1] = copy
    end
    local body = {
        model       = M.model,
        messages    = msgs,
        temperature = opts.temperature or 0.7,
        max_tokens  = opts.max_tokens or M.max_tokens,
    }
    local prefix = json.encode(body)   -- "{...}"
    if not opts.enable_tools then
        return prefix
    end
    -- Splice in the tools array as raw JSON (keeps the "function" keys intact,
    -- which json.encode cannot produce since "function" is a Lua keyword).
    -- M.build_tools_json() returns {"tools":[...]} — strip the outer braces so
    -- "tools" becomes a top-level key of the request body.
    local tools_json = M.build_tools_json()
    return prefix:sub(1, -2) .. ',' .. tools_json:sub(2, -2) .. '}'
end

-- Extract the assistant reply (content, tool_calls, usage) from a response.
-- The OpenAgentic proxy sometimes appends an SSE-style trailer
-- ("data: [DONE]\n\n") after the JSON body — strip it before decoding.
function M.parse_response(body)
    body = tostring(body or "")
    body = body:gsub("%s*data:%s*%[DONE%]%s*$", "")
    local ok, data = pcall(json.decode, body)
    if not ok then return nil, "JSON decode failed: " .. tostring(data) end
    if data.error then
        return nil, "API error: " .. tostring(data.error.message or data.error)
    end
    local choice = data.choices and data.choices[1]
    if not choice then return nil, "No choices in response" end
    local msg = choice.message or {}
    local resp = {
        content      = msg.content or "",
        reasoning    = msg.reasoning_content or nil,
        finish_reason = choice.finish_reason,
        usage        = data.usage,
        model        = data.model,
    }
    if msg.tool_calls and #msg.tool_calls > 0 then
        resp.tool_calls = msg.tool_calls
    end
    return resp
end

-- ====================================================================
-- Tool-call round handling
-- ====================================================================

-- Execute resp.tool_calls; append the assistant msg + tool results to
-- history. Returns the tool result messages (also already in history).
local function run_tool_round(history, resp, on_event)
    -- assistant message (with tool_calls) must go back to the API verbatim
    local assistant_msg = { role = "assistant", content = resp.content }
    if resp.tool_calls then assistant_msg.tool_calls = resp.tool_calls end
    history[#history + 1] = assistant_msg
    if on_event then on_event("assistant", assistant_msg) end

    for _, tc in ipairs(resp.tool_calls) do
        local fn = tc.function_ or tc["function"]
        local name = fn and fn.name or "?"
        local result, terr = M.execute_tool(name, fn and fn.arguments or "")
        if on_event then on_event("tool", { name = name, arguments = fn and fn.arguments or "", result = result, err = terr }) end
        history[#history + 1] = {
            role         = "tool",
            tool_call_id = tc.id,
            content      = terr and ("ERROR: " .. terr) or (result or ""),
        }
    end
    return true
end

-- ====================================================================
-- Synchronous chat (with tool loop)
-- ====================================================================

-- Returns final resp table, or nil + error string.
function M.chat(messages, opts)
    opts = opts or {}
    local history = {}
    for _, m in ipairs(messages) do history[#history + 1] = m end
    local rounds = 0
    local max_rounds = opts.max_tool_rounds or 8

    while true do
        local resp, err = M.raw_chat(history, opts)
        if not resp then return nil, err end
        -- Thinking models can burn the whole max_tokens budget on reasoning
        -- and finish=length with empty content. Retry once at full budget.
        if resp.finish_reason == "length" and resp.content == ""
           and (not resp.tool_calls or #resp.tool_calls == 0)
           and (opts.max_tokens or M.max_tokens) < M.max_tokens then
            if opts.on_event then opts.on_event("retry", { why = "empty reply, finish=length — retrying at max_tokens=" .. M.max_tokens }) end
            local retry_opts = {}
            for k, v in pairs(opts) do retry_opts[k] = v end
            retry_opts.max_tokens = M.max_tokens
            local resp2, err2 = M.raw_chat(history, retry_opts)
            if resp2 then return resp2 end
            return resp, err   -- fall back to the original (truncated) result
        end
        if not resp.tool_calls or not opts.enable_tools then
            return resp
        end
        rounds = rounds + 1
        if rounds > max_rounds then
            return resp, nil
        end
        run_tool_round(history, resp, opts.on_event)
    end
end

-- One request/response round trip, no tool loop.
function M.raw_chat(messages, opts)
    local body = M.build_body(messages, opts)
    local ok, resp_body, status = pcall(http.post, M.base_url .. "/chat/completions", body, {
        headers = {
            ["Authorization"] = "Bearer " .. M.api_key,
            ["Content-Type"]  = "application/json",
        },
        timeout = 300,
    })
    if not ok then
        return nil, "HTTP failed: " .. tostring(resp_body)
    end
    if status ~= 200 then
        return nil, "HTTP " .. tostring(status) .. ": " .. tostring(resp_body):sub(1, 500)
    end
    return M.parse_response(resp_body)
end

-- ====================================================================
-- Asynchronous chat (GUI build only — callback runs on the UI thread)
-- ====================================================================

-- Asynchronous chat with tool loop. Each API round is posted with
-- http.post_async; tool functions still run synchronously on the UI
-- thread between rounds.
function M.chat_async(messages, opts, callback)
    opts = opts or {}
    local history = {}
    for _, m in ipairs(messages) do history[#history + 1] = m end
    local rounds = 0
    local max_rounds = opts.max_tool_rounds or 8
    local retried_empty = false

    local function round()
        local body = M.build_body(history, opts)
        http.post_async(M.base_url .. "/chat/completions", body, {
            headers = {
                ["Authorization"] = "Bearer " .. M.api_key,
                ["Content-Type"]  = "application/json",
            },
            timeout = 300,
        }, function(resp_body, status)
            if status ~= 200 then
                callback(nil, "HTTP " .. tostring(status) .. ": " .. tostring(resp_body):sub(1, 500))
                return
            end
            local resp, perr = M.parse_response(resp_body)
            if not resp then
                callback(nil, perr)
                return
            end
            -- Thinking models can burn the whole max_tokens budget on
            -- reasoning: finish=length with empty content. One retry at
            -- full budget.
            if resp.finish_reason == "length" and resp.content == ""
               and (not resp.tool_calls or #resp.tool_calls == 0)
               and not retried_empty
               and (opts.max_tokens or M.max_tokens) < M.max_tokens then
                retried_empty = true
                if opts.on_event then opts.on_event("retry", { why = "empty reply, finish=length — retrying at max_tokens=" .. M.max_tokens }) end
                local retry_opts = {}
                for k, v in pairs(opts) do retry_opts[k] = v end
                retry_opts.max_tokens = M.max_tokens
                opts = retry_opts
                round()
                return
            end
            if not resp.tool_calls or not opts.enable_tools then
                callback(resp, nil)
                return
            end
            rounds = rounds + 1
            if rounds > max_rounds then
                callback(resp, nil)
                return
            end
            run_tool_round(history, resp, opts.on_event)
            round()
        end)
    end

    round()
    return true
end

function M.ask_async(prompt, opts, callback)
    return M.chat_async({ { role = "user", content = prompt } }, opts, callback)
end

-- ====================================================================
-- Convenience: single-turn prompt
-- ====================================================================

function M.ask(prompt, opts)
    return M.chat({ { role = "user", content = prompt } }, opts)
end

return M
