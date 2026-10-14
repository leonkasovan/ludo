-- Generate Plugin — plugin-authoring chat for the DeepSeek V4.1 Flash agent
-- Run from Ludo: -s "tools/Agents/Generate Plugin.lua"
--
-- Model config comes from tools/Agents/deepseek-v4.1-flash.lua,
-- which mirrors the entry in ~/.pi/agent/models.json:
--   deepseek-v4.1-flash @ https://openagentic.id/api/v1 (openai-completions)
--
-- The system prompt is always the agent's plugin-rules context
-- (ludo_plugin_rules.md + §8/§9 of ludo_scripting.md) — there is no
-- System override here; use "Downloader Agent.lua" for general chatting.
--
-- Tools must NOT call ui.Main()/ui.Uninit() — use MainStep() instead.

local ui = require("ui")
local json = json or require("json")

local agent = dofile("tools/Agents/deepseek-v4.1-flash.lua")

-- Replace the module's default tools with the shared hardened set (the
-- module's defaults remain registered as a fallback if this file is missing).
local ok_tools, agent_tools = pcall(dofile, "tools/Agents/agent_tools.lua")
if ok_tools and agent_tools then
    agent_tools.register_all(agent, { groups = { "common", "plugin" } })
end

local win_open = true
local chat_pending = false

-- ====================================================================
-- State
-- ====================================================================

local history = {}   -- { { role = ..., content = ... }, ... }
local busy = false

-- ====================================================================
-- Widgets
-- ====================================================================

local win = ui.NewWindow("Generate Plugin — DeepSeek V4.1 Flash", 860, 640, false)
win:SetMargined(1)

local tabs = ui.NewTab()
win:SetChild(tabs)

-- --------------------------------------------------------------------
-- Tab 1: Chat
-- --------------------------------------------------------------------

local chat = ui.NewVerticalBox()
chat:SetPadded(1)

-- Header: model info
local info = ui.NewLabel(string.format(
    "%s  |  ctx %d  |  max_tokens %d",
    agent.model, agent.context_window, agent.max_tokens))
chat:Append(info, false)
chat:Append(ui.NewHorizontalSeparator(), false)

-- Transcript
chat:Append(ui.NewLabel("Transcript:"), false)
local transcript = ui.NewMultilineEntry()
transcript:SetReadOnly(1)
chat:Append(transcript, true)

-- Prompt input row (multiline; Send button submits)
local input_row = ui.NewHorizontalBox()
input_row:SetPadded(1)
input_row:Append(ui.NewLabel("Prompt:"), false)
local prompt_entry = ui.NewMultilineEntry()
input_row:Append(prompt_entry, true)
local send_btn = ui.NewButton("  Send  ")
local clear_btn = ui.NewButton("  Clear  ")
input_row:Append(send_btn, false)
input_row:Append(clear_btn, false)
chat:Append(input_row, false)

-- Status
local chat_status = ui.NewLabel("Idle. Type a prompt and press Send.")
chat:Append(chat_status, false)

tabs:Append("Chat", chat)

local function chat_log(msg)
    transcript:Append(msg .. "\n")
end

local function set_busy(b, why)
    busy = b
    if b then
        chat_status:SetText(why or "Waiting for response…")
    else
        chat_status:SetText("Idle.")
    end
end

local smoke_pending = false   -- set true while a smoke test request is in flight

-- --------------------------------------------------------------------
-- Tab 2: Raw Request / Response
-- --------------------------------------------------------------------

local raw = ui.NewVerticalBox()
raw:SetPadded(1)

raw:Append(ui.NewLabel("Request body (JSON):"), false)
local req_area = ui.NewNonWrappingMultilineEntry()
req_area:SetReadOnly(1)
raw:Append(req_area, true)

raw:Append(ui.NewLabel("Raw response body:"), false)
local resp_area = ui.NewNonWrappingMultilineEntry()
resp_area:SetReadOnly(1)
raw:Append(resp_area, true)

local raw_row = ui.NewHorizontalBox()
raw_row:SetPadded(1)
local raw_copy_btn = ui.NewButton("  Clear  ")
raw_row:Append(raw_copy_btn, false)
raw_row:Append(ui.NewLabel("Last HTTP status:"), false)
local raw_status = ui.NewLabel("—")
raw_row:Append(raw_status, false)
raw:Append(raw_row, false)

tabs:Append("Raw I/O", raw)

raw_copy_btn:OnClicked(function()
    req_area:SetText("")
    resp_area:SetText("")
    raw_status:SetText("—")
end, nil)

-- --------------------------------------------------------------------
-- Tab 3: Settings / One-shot tests
-- --------------------------------------------------------------------

local cfg = ui.NewVerticalBox()
cfg:SetPadded(1)

local temp_row = ui.NewHorizontalBox()
temp_row:SetPadded(1)
temp_row:Append(ui.NewLabel("Temperature:"), false)
local temp_spin = ui.NewSpinbox(0, 200)   -- hundredths; 0..2.00
temp_spin:SetValue(70)
temp_row:Append(temp_spin, false)
temp_row:Append(ui.NewLabel("Max tokens (incl. thinking):"), false)
local maxtok_spin = ui.NewSpinbox(256, agent.max_tokens)
maxtok_spin:SetValue(32768)
temp_row:Append(maxtok_spin, false)
cfg:Append(temp_row, false)

cfg:Append(ui.NewHorizontalSeparator(), false)

-- Pre-baked smoke tests
cfg:Append(ui.NewLabel("Smoke tests (single-turn, no chat history):"), false)
local smoke_row = ui.NewHorizontalBox()
smoke_row:SetPadded(1)
local t1_btn = ui.NewButton("Ping")
local t2_btn = ui.NewButton("JSON output")
local t3_btn = ui.NewButton("Long reply")
smoke_row:Append(t1_btn, false)
smoke_row:Append(t2_btn, false)
smoke_row:Append(t3_btn, false)
cfg:Append(smoke_row, false)

cfg:Append(ui.NewLabel("Result:"), false)
local smoke_area = ui.NewMultilineEntry()
smoke_area:SetReadOnly(1)
cfg:Append(smoke_area, true)

local smoke_status = ui.NewLabel("Idle.")
cfg:Append(smoke_status, false)

tabs:Append("Settings & Smoke Tests", cfg)

local tools_cb = ui.NewCheckbox("Enable tools (http, plugin probe, fs)")
tools_cb:SetChecked(1)
cfg:Append(tools_cb, false)

-- --------------------------------------------------------------------
-- Tab: Rich View (WebView2 markdown rendering)
-- --------------------------------------------------------------------

local rich = ui.NewVerticalBox()
rich:SetPadded(1)

local webview = ui.NewWebView()
rich:Append(webview, true)

local function read_asset(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

-- Inline marked.min.js into the shell so SetHTML is self-contained
-- (NavigateToString has no base URL, so relative <script src> won't resolve).
local shell = read_asset("res/web/shell.html")
if shell then
    local marked = read_asset("res/web/marked.min.js") or ""
    local tag = '<script src="marked.min.js"></script>'
    local pos = shell:find(tag, 1, true)
    if pos then
        shell = shell:sub(1, pos - 1)
            .. "<script>" .. marked .. "</script>"
            .. shell:sub(pos + #tag)
    end
    webview:SetHTML(shell)
end

tabs:Append("Rich View", rich)

local function rich_msg(role, text)
    if not shell then return end   -- assets missing (wrong cwd) - skip rich updates
    webview:Eval(string.format("appendMessage(%s, %s);",
        json.encode(role), json.encode(text or "")))
end

-- The WebView initializes asynchronously; the first SetHTML above is queued
-- by the C layer and replayed once the controller is ready, so give the
-- message pump a moment after the window is shown.

local function get_opts(event_fn)
    local o = {
        temperature = temp_spin:Value() / 100,
        max_tokens  = maxtok_spin:Value(),
        enable_tools = tools_cb:Checked() == 1,
    }
    if event_fn then
        o.on_event = function(kind, ev)
            if kind == "tool" then
                event_fn(ev.name, ev.arguments, ev.err)
            end
        end
    end
    return o
end

local function run_smoke(name, prompt)
    if busy or smoke_pending then return end
    smoke_area:SetText("")
    smoke_status:SetText(name .. ": running…")
    smoke_pending = true

    local opts = get_opts(function(name, args, terr)
        smoke_area:Append(">> tool " .. name .. "(" .. tostring(args) .. ")\n")
    end)
    opts.system = "Follow the user's instructions exactly."
    if name == "JSON output" then
        opts.system = "Respond ONLY with valid JSON, no markdown fences, no commentary."
    end

    agent.ask_async(prompt, opts, function(resp, err)
        if not resp then
            smoke_status:SetText(name .. ": FAILED — " .. tostring(err))
            smoke_area:Append(tostring(err) .. "\n")
            return
        end
        smoke_status:SetText(string.format(
            "%s: OK (finish=%s, tokens=%s)",
            name, tostring(resp.finish_reason),
            resp.usage and tostring(resp.usage.total_tokens) or "?"))
        smoke_area:SetText(resp.content)
        if resp.reasoning then
            smoke_area:Append("\n--- reasoning ---\n" .. resp.reasoning)
        end
    end)
end

t1_btn:OnClicked(function() run_smoke("Ping", "Reply with exactly the word: pong") end, nil)
t2_btn:OnClicked(function() run_smoke("JSON output",
    'Return this JSON with values filled in: {"status":"ok","sum":<1+1>}') end, nil)
t3_btn:OnClicked(function() run_smoke("Long reply",
    "Write a 300-word explanation of what an emulator is.") end, nil)

-- --------------------------------------------------------------------
-- Chat send logic
-- --------------------------------------------------------------------

local function send_prompt()
    if busy then return end
    local text = prompt_entry:Text()
    if text == "" then return end
    prompt_entry:SetText("")
    set_busy(true, "Waiting for " .. agent.model .. "…")

    chat_log("──────────────────────────────")
    chat_log("[you] " .. text)
    rich_msg("user", text)
    history[#history + 1] = { role = "user", content = text }

    local opts = get_opts(function(name, args, terr)
        chat_log(">> tool " .. name .. "(" .. tostring(args) .. ")" .. (terr and " ERROR: " .. terr or ""))
        rich_msg("tool", "tool " .. name .. "(" .. tostring(args) .. ")"
            .. (terr and " ERROR: " .. terr or ""))
    end)
    -- system prompt: always the agent's plugin-rules default (no override)

    req_area:SetText(agent.build_body(history, opts))
    raw_status:SetText("request in flight…")

    -- Asynchronous: keeps the window (and the WebView) responsive while
    -- the request is in flight. The callback runs on the UI thread.
    local ok = pcall(agent.chat_async, history, opts, function(resp, err)
        if not resp then
            resp_area:SetText(tostring(err))
            raw_status:SetText("failed")
            chat_log("[error] " .. tostring(err))
            rich_msg("system", "Error: " .. tostring(err))
            chat_status:SetText("Error — see transcript.")
            -- roll back so the failed turn can be retried
            table.remove(history)
            set_busy(false)
            return
        end

        resp_area:SetText("<see parsed below>\n\n" .. json.encode(resp))
        raw_status:SetText("completed")

        history[#history + 1] = { role = "assistant", content = resp.content }
        chat_log("[assistant] " .. resp.content)
        rich_msg("assistant", resp.content)
        if resp.usage then
            local think = resp.usage.completion_thinking_tokens
                or (resp.usage.completion_tokens_details and resp.usage.completion_tokens_details.reasoning_tokens)
            chat_log(string.format("[usage] prompt=%s completion=%s total=%s%s",
                tostring(resp.usage.prompt_tokens),
                tostring(resp.usage.completion_tokens),
                tostring(resp.usage.total_tokens),
                think and (" (thinking=" .. tostring(think) .. ")") or ""))
        end
        chat_status:SetText(string.format("Done (finish=%s).", tostring(resp.finish_reason)))
        set_busy(false)
    end)
    if not ok then
        -- chat_async itself threw (e.g. build_body error)
        chat_status:SetText("Error sending request.")
        set_busy(false)
    end
end

send_btn:OnClicked(send_prompt, nil)

clear_btn:OnClicked(function()
    history = {}
    transcript:SetText("")
    if shell then webview:Eval("clearContent();") end
    chat_status:SetText("History cleared.")
end, nil)

-- --------------------------------------------------------------------
-- Window + event loop
-- --------------------------------------------------------------------

win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

chat_log("Agent loaded: " .. agent.name)
if agent.default_system then
    chat_log("Plugin rules loaded: " .. #agent.default_system .. " chars (default context)")
else
    chat_log("Plugin rules NOT found (agent_context.lua / ludo_plugin_rules.md missing)")
end
chat_log("Endpoint: " .. agent.base_url .. "/chat/completions")
chat_log("Context window: " .. agent.context_window .. " tokens")
chat_log("Press Send to start a conversation.")

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
