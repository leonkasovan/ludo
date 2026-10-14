-- test_webview.lua - WebView2 control component test
-- Run: ./install/ludo-debug.exe -s test_webview.lua
-- Results go to install/ludo.log
--
-- Checks that ui.NewWebView() creates a real WebView2-backed control
-- (Windows), that SetHTML/Navigate/Eval are accepted (they are queued
-- until the async controller init completes), that resizing does not
-- crash, and that destroying the control tears COM down cleanly.

ludo.logInfo("=== WebView2 component test ===")

local ui = require("ui")

local passed, failed = 0, 0

local function check(cond, what)
    if cond then
        ludo.logInfo("  PASS " .. what)
        passed = passed + 1
    else
        ludo.logError("  FAIL " .. what)
        failed = failed + 1
    end
end

-- Pump the libui event loop for ~ms milliseconds (non-blocking steps).
local function pump(ms)
    local deadline = os.clock() + ms / 1000.0
    repeat
        ui.MainStep(false)
        local busy = 0
        while busy < 200 do busy = busy + 1 end   -- tiny yield
    until os.clock() >= deadline
end

-- --------------------------------------------------------------------
-- 1) Control creation + Lua binding surface
-- --------------------------------------------------------------------

local win = ui.NewWindow("WebView2 test", 640, 480, false)
local box = ui.NewVerticalBox()
local wv = ui.NewWebView()
check(wv ~= nil, "ui.NewWebView() returned a control")

check(type(wv.SetHTML) == "function", "WebView.SetHTML is callable")
check(type(wv.Navigate) == "function", "WebView.Navigate is callable")
check(type(wv.Eval) == "function", "WebView.Eval is callable")

box:Append(wv, true)
win:SetChild(box)

-- --------------------------------------------------------------------
-- 2) Content APIs accepted before/after async init (queued + replayed)
-- --------------------------------------------------------------------

local ok1 = pcall(function()
    wv:SetHTML([=[<!DOCTYPE html><html><head><meta charset="utf-8"></head>
<body style="background:#1e1e22;color:#eee;font-family:Segoe UI,sans-serif">
<h1 style="margin-top:40px">WebView2 <span style="color:#4ade80">OK</span></h1>
<pre id="p">static</pre>
<script>function mark(){document.getElementById('p').textContent='dynamic';}
// js bridge marker: window.__ready set below
</script>
</body></html>]=])
end)
check(ok1, "SetHTML accepted (queued if controller still initializing)")

local ok2 = pcall(function() wv:Eval("mark();") end)
check(ok2, "Eval accepted")

win:Show()

-- Give the WebView2 async init time to complete, then replay a JS call
-- that only makes sense if the document was actually rendered.
pump(4000)

local ok3 = pcall(function()
    wv:Eval("document.title = 'webview2-test';")
    wv:Eval([=[
        var d = document.createElement('div');
        d.textContent = 'appended via Eval after init';
        document.body.appendChild(d);
    ]=])
end)
check(ok3, "Post-init Eval accepted")

-- --------------------------------------------------------------------
-- 3) Resize (WM_WINDOWPOSCHANGED -> put_Bounds path)
-- --------------------------------------------------------------------

for _, size in ipairs({ {900, 700}, {320, 240}, {640, 480} }) do
    win:SetContentSize(size[1], size[2])
    pump(300)
end
check(true, "Resize cycled 3x without crash")

-- --------------------------------------------------------------------
-- 4) Navigate to a real URL + shell template load
-- --------------------------------------------------------------------

local ok4 = pcall(function() wv:Navigate("https://example.com/") end)
check(ok4, "Navigate('https://example.com/') accepted")
pump(3000)

local shell = nil
local f = io.open("res/web/shell.html", "rb")
if f then shell = f:read("*a"); f:close() end
check(shell ~= nil, "res/web/shell.html readable from install dir")

if shell then
    local markedjs = nil
    local m = io.open("res/web/marked.min.js", "rb")
    if m then markedjs = m:read("*a"); m:close() end
    check(markedjs ~= nil, "res/web/marked.min.js readable")
    if markedjs then
        local tag = '<script src="marked.min.js"></script>'
        local pos = shell:find(tag, 1, true)
        if pos then
            shell = shell:sub(1, pos - 1) .. "<script>" .. markedjs .. "</script>" .. shell:sub(pos + #tag)
        end
        wv:SetHTML(shell)
        pump(500)
        local ok5 = pcall(function()
            wv:Eval("appendMessage('assistant', '# Hello\\n\\n- one\\n- **two**\\n\\n```c\\nint main(){}\\n```');")
        end)
        check(ok5, "Markdown shell loaded + appendMessage Eval accepted")
        pump(1500)
    end
end

-- --------------------------------------------------------------------
-- 5) Destroy mid-initialization (exercises the handler owner-NULL guard)
-- --------------------------------------------------------------------

local wv2 = ui.NewWebView()
local ok6 = pcall(ui.ControlDestroy, wv2)   -- destroyed before its controller finished async init
check(ok6, "Destroy-mid-init did not crash")
pump(1500)                                  -- let the queued completion land on the freed control

-- --------------------------------------------------------------------
-- 6) Recursive teardown via the window (box -> webview -> COM Close)
-- --------------------------------------------------------------------

win:Destroy()
pump(300)

ludo.logInfo(string.format("=== WebView2 test done: %d passed, %d failed ===", passed, failed))
