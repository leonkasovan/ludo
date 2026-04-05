# CLAUDE.md — Ludo Project Reference

This file is for AI agents working in this codebase. Read it before writing any
code, plugins, or documentation.

---

## 1. Project Overview

**Ludo** is a desktop download manager written in C (C11, MinGW64/GCC 14) on
Windows. It embeds **Lua 5.2** as a plugin engine and exposes three C-backed
modules to scripts: `http` (libcurl), `ludo` (download manager), and `ui`
(libui-ng native widgets). Plugins are `.lua` files in `plugins/` that
return a table with `validate(url)` and `process(url)`.

---

## 2. Build System

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake build (CMake ≥ 3.16) |
| `build/` | Out-of-source build directory |
| `build/ludo-debug.exe` | Debug executable |
| `build/ludo.exe` | Release executable (when built) |
| `build/plugins/` | Runtime plugin directory (auto-copied at build) |
| `build/ludo.log` | Runtime log — always check here after test runs |

### Build Commands

```bash
# Configure + build (full)
cmake -B build . && cmake --build build --config Debug

# Incremental build only
cmake --build build --config Debug

# Via tasks (VS Code)
# Run task: "Build (Configure + Build)"
```

CMake automatically copies `plugins/*.lua`, `res/`, `snippets/`, and
`config.ini` to the build directory. You **do not** need to copy plugins
manually after a successful build.

---

## 3. Plugin Development

### 3.1 Plugin Skeleton

Every plugin is a file in `plugins/` returning a Lua table:

```lua
local plugin = { name = "Site", version = "YYYYMMDD", creator = "..." }

local json = json or require("json")  -- global already available

function plugin.validate(url)
    return url:match("https?://www%.example%.com/") ~= nil
end

function plugin.process(url)
    local body, status = http.get(url, { user_agent = ..., timeout = 30 })
    if status ~= 200 then ludo.logError("..."); return end

    local video_url = ...  -- extract from body

    local outdir = ludo.getOutputDirectory()
    -- Optional: pass extra HTTP headers sent with the CDN GET request.
    -- Useful when the server requires Referer or a session cookie (e.g. TikTok).
    local _, dl_status, output = ludo.newDownload(
        video_url, outdir, ludo.DOWNLOAD_NOW, "filename.mp4",
        { ["Referer"] = "https://site.com/" })

    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Queued → " .. (output or "filename.mp4"))
    elseif dl_status == 403 then
        -- CDN may block HEAD probes; queue anyway if URL was just extracted.
        ludo.logSuccess("Queued (CDN blocked HEAD probe) → " .. (output or "filename.mp4"))
    else
        ludo.logError("Preflight HTTP " .. dl_status)
    end
end

return plugin
```

### 3.2 Key APIs

```lua
-- HTTP
local body, status, headers = http.get(url, { user_agent=..., timeout=N, headers={...} })
local body, status, headers = http.post(url, body_str, { ... })
http.set_cookie("path/to/cookies.txt")   -- Netscape format
http.clear_cookies()
local csrf = http.read_cookie("cookies.txt", "csrftoken")
local b64   = http.base64_encode(str)        -- binary-safe
local raw   = http.base64_decode(b64)        -- binary-safe
local digest = http.sha256(str)              -- 32-byte raw binary digest

-- Ludo
local id, status, path = ludo.newDownload(url [, dir [, mode [, filename [, headers]]]])
-- headers is an optional table: { ["Name"] = "Value", ... } (e.g. Referer, Cookie)
ludo.DOWNLOAD_NOW     -- constant
ludo.DOWNLOAD_QUEUE   -- constant
ludo.getOutputDirectory()  -- returns the configured output folder
ludo.logInfo("msg")
ludo.logSuccess("msg")
ludo.logError("msg")

-- JSON (global)
local ok, data = pcall(json.decode, body)
local s = json.encode(table)
```

### 3.3 Common Patterns

**Safe filename:**
```lua
local function safe_name(s)
    return (s or ""):gsub("[^%w%-_]", "_"):sub(1, 80)
end
```

**JSON unescape for embedded URLs (Facebook relay data, etc.):**
```lua
local function unescape_json_url(s)
    if not s then return nil end
    s = s:gsub("\\/", "/")
    s = s:gsub("\\u(%x%x%x%x)", function(h)
        local cp = tonumber(h, 16)
        if cp < 128 then return string.char(cp) end
        return ""
    end)
    return s
end
```

**Balanced-brace JSON object extraction from HTML:**
```lua
local function extract_json_object(text, pattern)
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
```

**Cookie jar setup:**
```lua
local cookie_path = ludo.getOutputDirectory() .. "/mysite_cookies.txt"
local ck = io.open(cookie_path, "r")
if ck then ck:close(); http.set_cookie(cookie_path) end
```

### 3.4 Plugin-Specific Notes

| Plugin | Strategy | Auth needed? |
|--------|----------|-------------|
| `youtube.lua` | InnerTube API (android_vr/ios client) | No (public) |
| `instagram.lua` | REST API → GraphQL → embed scrape | `sessionid` cookie for most content |
| `facebook.lua` | Relay `playable_url` → `browser_native` → VideoConfig → `<video>` | Cookie for most content |
| `tiktok.lua` | SIGI_STATE → __UNIVERSAL_DATA__ → regex; **WAF bypass via SHA-256 PoW** (yt-dlp technique) | Cookies for private content |
| `mediafire.lua` | HTML scrape `a.popsok` href | No |
| `dropbox.lua` | Redirect `?dl=0` → `?dl=1` | No |
| `gdrive.lua` | Google Drive direct download URL | No (public) |

---

## 4. Testing

### 4.1 Test Commands

```bash
# Run a test script (all output goes to ludo.log, not stdout)
cd build
./ludo-debug.exe -s test_PLUGINNAME.lua

# Check results
grep -E "PASS|FAIL|SUCCESS|ERROR" build/ludo.log | tail -30

# Watch live
tail -f build/ludo.log
```

### 4.2 Test Script Template

```lua
-- test_myplugin.lua
ludo.logInfo("=== MyPlugin test ===")

local ok, p = pcall(dofile, "plugins/myplugin.lua")
if not ok then ludo.logError("Load failed: " .. tostring(p)); return end

-- validate() cases
local cases = {
    { "https://site.com/valid/url", true  },
    { "https://other.com/bad/url",  false },
}
local passed, failed = 0, 0
for _, c in ipairs(cases) do
    local got = p.validate(c[1])
    if got == c[2] then
        ludo.logInfo("  PASS validate(" .. c[1] .. ") = " .. tostring(got))
        passed = passed + 1
    else
        ludo.logError("  FAIL validate(" .. c[1] .. ") expected=" .. tostring(c[2]))
        failed = failed + 1
    end
end
ludo.logInfo(("validate: %d passed, %d failed"):format(passed, failed))

-- process() live test
local ok2, err = pcall(p.process, "https://site.com/valid/video/123")
if not ok2 then ludo.logError("process() error: " .. tostring(err)) end

ludo.logInfo("=== MyPlugin test done ===")
```

### 4.3 Existing Test Scripts

| Script | Plugin |
|--------|--------|
| `build/test_instagram.lua` | instagram.lua |
| `build/test_tiktok.lua` | tiktok.lua |
| `build/test_facebook.lua` | facebook.lua |

### 4.4 Test runner notes

- **Delete logs before runs:** Test scripts should remove `ludo.log` at startup so each run produces a fresh, easy-to-scan log. Several test scripts in `build/` now perform `pcall(os.remove, "ludo.log")` at the top.
- **TikTok WAF debugging:** When `plugins/tiktok.lua` encounters a server-side challenge page it will save the fetched HTML to the output directory as `downloads/tiktok_debug_<id>_waf.html` (and a `_waf_retry.html` on mobile-UA retry). The plugin attempts a mobile-UA retry; if the challenge persists the recommended remedies are: provide a fresh `downloads/tiktok_cookies.txt` (Netscape format), run from a different IP/proxy, or use a headless browser extractor to execute page JS and obtain the playable URL.


---

## 5. Important Files

| Path | Role |
|------|------|
| `src/http_module.c` | C implementation of `http` global (libcurl) |
| `src/lua_engine.c` | Loads and manages Lua state, plugin loader |
| `src/ludo_module.c` | C implementation of `ludo` global |
| `src/download_manager.c` | Threaded download queue |
| `src/main.c` | Entry point; `-s scriptfile.lua` runs standalone |
| `ludo_scripting.md` | Full scripting API reference (read this for detailed docs) |
| `build/ludo.log` | Runtime log — all `ludo.log*()` calls write here |

---

## 6. Converting yt-dlp Extractors to Ludo Plugins

See `ludo_scripting.md` §7 for the full guide. Summary:

1. Read `_VALID_URL` regex → write `plugin.validate()` using Lua patterns
   (`\.` not `\.`, `%d` not `\d`, no alternation `|`, use multiple `match()`)
2. Read `_real_extract` → write `plugin.process()`
3. `self._download_webpage(url)` → `http.get(url, { user_agent=..., headers={...} })`
4. `self._search_json(regex, html)` → `extract_json_object(html, lua_pattern)`
5. `traverse_obj(data, path)` → manual Lua table navigation with nil-guards
6. `self._download_json(url)` → `http.get(url)` + `pcall(json.decode, body)`
7. `self._get_cookies().name` → `http.read_cookie(path, "name")`
8. No DASH/HLS support in Ludo — pick the most useful direct MP4 URL

### Anti-bot considerations

| Site | Challenge | Workaround |
|------|-----------|------------|
| TikTok | JS challenge, bot fingerprinting | Plugin degrades gracefully; works when no challenge is triggered |
| Facebook | Login wall for most content | Netscape cookie file |
| Instagram | Session gate for most content | Netscape cookie file (`sessionid`) |

---

## 7. Lua 5.2 Gotchas in This Codebase

- `%w` does **not** include `_`; use `[%w_]` explicitly
- No `|` alternation in patterns; use multiple `match()` calls
- `.-` is the lazy quantifier (not `*?`)
- `json.decode` throws on error — always wrap in `pcall()`
- Lua doubles lose precision for integers > 2^53; use string arithmetic for
  large IDs (see `bigint_muladd` in `instagram.lua`)
- `string.match` returns `nil` on no-match, not `false`
- All plugins share the same Lua state (globals persist between plugin calls)

---

## 8. Known Build Quirks

- CMake configure step logs a non-fatal `Permission denied` error for
  `ninja restat` — this is benign and can be ignored.
- `ludo-debug.exe --help` hangs (it is a GUI app, not a CLI tool).
- After editing a plugin, just rebuild (`cmake --build build --config Debug`);
  CMake's custom command copies all `plugins/*.lua` to `build/plugins/`
  automatically.
  
## 9. Plugin Reference from yt-dlp

We can use yt-dlp extractor source code (C:\Projects\yt-dlp-2026.03.17\yt_dlp\extractor\facebook.py) and yt-dlp.log as reference for making ludo plugin.
Run `yt-dlp.exe --cookies downloads\facebook_cookies.txt  --print-traffic -v https://www.facebook.com/reel/1520832029440197 >yt-dlp.log 2>&1` and check yt-dlp.log
