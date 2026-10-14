-- test_agent_tools.lua — standalone tests for tools/Agents/agent_tools.lua
-- and the new ludo.* C bindings.
--
-- Run (w64devkit shell, from the install dir or repo root):
--   ./install/ludo-debug.exe -s tools/Agents/test_agent_tools.lua
-- Results go to install/ludo.log — grep for PASS/FAIL.

ludo.logInfo("=== agent_tools test ===")

local ok, agent = pcall(dofile, "tools/Agents/deepseek-v4.1-flash.lua")
if not ok then ludo.logError("module load failed: " .. tostring(agent)); return end

local ok2, T = pcall(dofile, "tools/Agents/agent_tools.lua")
if not ok2 then ludo.logError("agent_tools load failed: " .. tostring(T)); return end

T.register_all(agent, { groups = { "common", "download", "plugin" } })

local passed, failed = 0, 0
local function check(name, cond, extra)
    if cond then
        ludo.logInfo("  PASS " .. name .. (extra ~= nil and (" — " .. tostring(extra)) or ""))
        passed = passed + 1
    else
        ludo.logError("  FAIL " .. name .. (extra ~= nil and (" — " .. tostring(extra)) or ""))
        failed = failed + 1
    end
end

local function call_tool(name, args_json)
    return agent.execute_tool(name, args_json)
end

-- 1. sandbox: resolve_safe_path
check("sandbox rejects bare relative path", agent.resolve_safe_path("test.lua") == nil)
check("sandbox rejects traversal", agent.resolve_safe_path("plugins/../secret.lua") == nil)
check("sandbox allows plugins/x.lua", agent.resolve_safe_path("plugins/x.lua") == "plugins/x.lua")

-- 2. execute_tool central clamp
agent.register_tool("test_big", "returns 50KB", { type = "object", properties = {} },
    function() return string.rep("A", 50000) end)
local rbig = call_tool("test_big", "{}")
check("execute_tool clamps >40000 chars",
    type(rbig) == "string" and #rbig < 40100 and rbig:find("truncated", 1, true) ~= nil,
    rbig and #rbig)

-- 3. ludo.queueDownload / downloadStatus / resumeDownload C bindings
local qok, qid = pcall(ludo.queueDownload, "http://example.com/")
check("queueDownload returns id > 0", qok and type(qid) == "number" and qid > 0, tostring(qid))
local st = qid and ludo.downloadStatus(qid) or nil
check("downloadStatus(id) returns table", type(st) == "table" and st.id == qid,
      st and tostring(st.state))
check("downloadStatus(bogus) is nil", ludo.downloadStatus(99999999) == nil)
check("resumeDownload(bogus) is false", ludo.resumeDownload(99999999) == false)

-- 4. listDownloads contains the queued id
local list = ludo.listDownloads()
local found = false
for _, s in ipairs(type(list) == "table" and list or {}) do
    if s.id == qid then found = true break end
end
check("listDownloads contains queued id", found, tostring(type(list) == "table" and #list or "?"))

-- 5. tool-level ludo_status
local rstat = call_tool("ludo_status", '{"id":' .. tostring(qid) .. '}')
check("ludo_status(id) returns text", type(rstat) == "string" and rstat:find("id=") ~= nil,
      rstat and rstat:sub(1, 80))

-- 6. URL policy
local rbad, baderr = call_tool("ludo_download", '{"url":"javascript:alert(1)"}')
check("ludo_download rejects non-http(s)", rbad == nil and tostring(baderr):find("Only http") ~= nil,
      tostring(baderr))

-- 7. ludo_process_url queues into the plugin pipeline
local rproc = call_tool("ludo_process_url", '{"url":"http://example.org/"}')
check("ludo_process_url queues", type(rproc) == "string" and rproc:find("Queued") ~= nil,
      rproc and rproc:sub(1, 80))
-- In -s script mode the GUI url_worker_thread is not running, so the queued
-- URL is only picked up in full GUI mode. Here we just assert the push was
-- accepted (return string above) and that the manager is pollable:
local st0 = qid and ludo.downloadStatus(qid) or nil
check("downloadStatus pollable right after queue",
      type(st0) == "table" and st0.state ~= nil, st0 and tostring(st0.state))

-- 7b. downloadStatus reaches completed (download workers run in -s mode)
local final = nil
for i = 1, 60 do
    local s = ludo.downloadStatus(qid)
    if s and (s.state == "completed" or s.state == "failed") then final = s break end
    os.execute("ping -n 2 127.0.0.1 >nul")
end
check("downloadStatus reaches terminal state",
      type(final) == "table" and final.state == "completed",
      final and (tostring(final.state) .. " " .. tostring(final.downloaded_bytes) .. " bytes"))

-- 8. http_head (network)
local rhead = call_tool("http_head", '{"url":"https://example.com/"}')
check("http_head returns status + headers",
    type(rhead) == "string" and rhead:find("HTTP 2") ~= nil, rhead and rhead:sub(1, 120))

-- 9. http_get text + cache + body_search + body_slice (network)
local URL = "https://example.com/"
local rget = call_tool("http_get", '{"url":"' .. URL .. '","max_chars":200}')
check("http_get returns capped text",
    type(rget) == "string" and rget:find("HTTP 2") ~= nil, rget and rget:sub(1, 60))
local rbs, bserr = call_tool("body_search", '{"url":"' .. URL .. '","pattern":"Example"}')
check("body_search finds pattern in cache",
    type(rbs) == "string" and rbs:find("line %d") ~= nil, tostring(bserr))
local rsl = call_tool("body_slice", '{"url":"' .. URL .. '","offset":0,"length":40}')
check("body_slice pages the cache",
    type(rsl) == "string" and rsl:find("chars 1%-40 of") ~= nil, rsl and rsl:sub(1, 60))
local rnc, ncerr = call_tool("body_search", '{"url":"https://never-fetched.invalid/","pattern":"x"}')
check("body_search on uncached URL errors",
    rnc == nil and tostring(ncerr):find("not cached") ~= nil, tostring(ncerr))

-- 9b. http_get refuses binary content (guard path: Content-Type + NUL sniff)
local rb = call_tool("http_get", '{"url":"https://www.google.com/images/branding/googlelogo/1x/googlelogo_color_272x92dp.png"}')
check("http_get on image URL returns BINARY notice, not bytes",
    type(rb) == "string" and rb:find("BINARY", 1, true) ~= nil, rb and rb:sub(1, 100))

-- 10. fs_search over plugins/
local rfs = call_tool("fs_search", '{"pattern":"newDownload","dir":"plugins/","max_hits":3}')
check("fs_search finds references in plugins/",
    type(rfs) == "string" and (rfs:find(":%d+:") ~= nil or rfs:find("No matches") ~= nil),
    rfs and rfs:sub(1, 100))

ludo.logInfo(string.format("=== agent_tools test done: %d passed, %d failed ===", passed, failed))
