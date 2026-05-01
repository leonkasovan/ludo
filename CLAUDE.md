# CLAUDE.md — Ludo Project Reference

This file is for AI agents working in this codebase. Read it before writing any
code, plugins, or documentation.

1. Do not silently guess. State your assumptions clearly. If anything is ambiguous, ask instead of choosing one interpretation silently. If a simpler solution exists, recommend it before implementing.
2. Solve the requested problem with the minimum necessary code. Do not add features that were not asked for. Do not introduce abstractions for one-time use. Prefer simple, readable code over clever code.
3. Only change what the task requires. Do not refactor unrelated code. Do not rewrite comments, formatting, or naming unless necessary. Match the existing style and conventions of the codebase.
4. Keep edits local, focused, and easy to review. Touch as few files as possible. Change as little code as necessary. Avoid broad rewrites when a targeted fix is enough.
5. Do not treat 'done' as a guess. Turn requests into clear success criteria. Prefer tests, existing checks, or concrete validation over verbal confidence.
6. Understand the surrounding code before editing it. Read enough nearby code to understand how the target piece fits in. Identify the local conventions before introducing new patterns.
7. Do not accidentally erase meaning while making changes. Preserve comments unless they are clearly outdated. Preserve public interfaces unless changing them is necessary.
8. Do not continue blindly when the risk is high. Pause and ask if: the request is ambiguous, the codebase contains conflicting patterns, or the task requires an architectural decision.
9. Before considering the task complete, confirm: the request was actually addressed, the change is no larger than necessary, and the final result matches the requested scope.

---

## 1. Project Overview

**Ludo** is a desktop download manager written in C (C11, MinGW64/GCC 14) on
Windows. It embeds **Lua 5.2** as a plugin engine and exposes modules to
scripts: `http` (libcurl), `ludo` (download manager), `ui` (libui-ng native
widgets — GUI build only), and `aes128_cbc_decrypt` (console and GUI builds).
Plugins are `.lua` files in `plugins/` that return a table with
`validate(url)` and `process(url)`.

The project produces two executable targets:
- **`ludo`** — GUI downloader with libui-ng (links `libui_static` + `libuilua_static`)
- **`ludocon`** — Pure console CLI (`BUILD_CONSOLE`), no GUI dependencies

---

## 2. Build System

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake build (CMake ≥ 3.16) |
| `build/` | Out-of-source build directory |
| `build/ludo-debug.exe` / `build/ludo-debug` | Debug executable (GUI) |
| `build/ludo.exe` / `build/ludo` | Release GUI executable |
| `build/ludocon-debug.exe` / `build/ludocon-debug` | Debug console executable |
| `build/ludocon.exe` / `build/ludocon` | Release console executable |
| `build/plugins/` | Runtime plugin directory (auto-copied at build) |
| `build/ludo.log` | Runtime log — always check here after test runs |

### Build Commands

in VSCode open w64devkit terminal and then follow this command
```bash
# Configure + build (full)
cmake -B build . && cmake --build build --parallel

# Incremental build only
cmake --build build --parallel

# Build ludocon only (no GUI dependencies)
cmake -B build . -DBUILD_GUI=OFF
cmake --build build --target ludocon --parallel
```

### Linux Build Prerequisites

On Linux, the build compiles curl, zlib, brotli, and zstd from bundled
source. You need the following system packages:

```bash
# Ubuntu / Debian
sudo apt install cmake gcc g++ libgtk-3-dev libgnutls28-dev pkg-config
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

-- Zip
local status [, errmsg] = zip.create(output_path, { file1, file2, ... })
local status [, errmsg] = zip.create(output_path, directory [, glob_filter])
-- status: 0 = success, -1 = failure; glob_filter e.g. "*.mp4" (case-insensitive, * and ? wildcards)

-- JSON (global)
local ok, data = pcall(json.decode, body)
local s = json.encode(table)

-- M3U8 (shared module: dofile("plugins/m3u8.lua"))
local m3u8 = dofile("plugins/m3u8.lua")
local ok, path_or_err = m3u8.download(m3u8_url, output_dir, "file.mp4" [, headers])
-- headers is an optional table: { ["Name"] = "Value", ... } (e.g. Referer)
local is_master = m3u8.is_master(playlist_text)
local variants = m3u8.parse_master(text, base_url)  -- array of {url, bandwidth, resolution}
local best = m3u8.pick_best(variants)  -- highest bandwidth variant
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
| `bluesky.lua` | AT Protocol: resolveHandle → getPostThread → getBlob | No (public) |
| `twitch.lua` | GraphQL persisted queries for clips (ShareClipRenderStatus); VODs/streams via usher HLS + m3u8 module | No (public) |
| `baidu.lua` | REST API (xqinfo/xqsingle) → episode list → queue each URL | No (public) |
| `bigo.lua` | POST API (getInternalStudioInfo) → HLS stream download via m3u8 module | No (public) |
| `m3u8.lua` | Pure Lua HLS/m3u8 playlist parser + segment downloader (shared module, not a plugin) | No (public m3u8) |
| `bilibili.lua` | Webpage scrape → __INITIAL_STATE__ → playinfo API → DASH/durl download | No (public) |
| `dailymotion.lua` | REST API (player metadata endpoint) → direct MP4 or HLS via m3u8 module | No (public) |
| `telegram.lua` | HTML scrape embed page → `<video src>` → direct MP4 download | No (public) |
| `tube8.lua` | Flashvars JSON extraction → quality_NNNp direct MP4 URLs | No (public) |
| `vidio.lua` | REST API (videos/{id}) → HLS playlist → m3u8 module download | No (public) |
| `pinterest.lua` | Pinterest API (PinResource) → video_list or story_pin_data → MP4/HLS | No (public) |
| `douyu.lua` | Webpage scrape → $DATA JSON → VOD metadata (stream URLs require JS signing) | No (public VOD metadata) |
| `xiaohongshu.lua` | Webpage scrape → __INITIAL_STATE__ → direct MP4 URLs (SPA anti-bot blocks non-JS) | No (public, but CAPTCHA blocks) |
| `youku.lua` | REST API (ups.get.json) with cna/utid → HLS streams → m3u8 module download | No (public) |
| `iqiyi.lua` | MD5-signed API (tmts) → HLS stream download via m3u8 module | No (public) |
| `tencent.lua` | AES-CBC-whitespace signed API (getvinfo) → HLS via m3u8 module | No (public) |

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
| `build/test_bluesky.lua` | bluesky.lua |
| `build/test_twitch.lua` | twitch.lua |
| `build/test_baidu.lua` | baidu.lua |
| `build/test_bigo.lua` | bigo.lua |
| `build/test_m3u8.lua` | m3u8.lua |
| `build/test_bilibili.lua` | bilibili.lua |
| `build/test_dailymotion.lua` | dailymotion.lua |
| `build/test_telegram.lua` | telegram.lua |
| `build/test_tube8.lua` | tube8.lua |
| `build/test_vidio.lua` | vidio.lua |
| `build/test_pinterest.lua` | pinterest.lua |
| `build/test_douyu.lua` | douyu.lua |
| `build/test_xiaohongshu.lua` | xiaohongshu.lua |
| `build/test_youku.lua` | youku.lua |
| `build/test_iqiyi.lua` | iqiyi.lua |
| `build/test_tencent.lua` | tencent.lua |

### 4.4 Test runner notes

- **Delete logs before runs:** Test scripts should remove `ludo.log` at startup so each run produces a fresh, easy-to-scan log. Several test scripts in `build/` now perform `pcall(os.remove, "ludo.log")` at the top.
- **TikTok WAF debugging:** When `plugins/tiktok.lua` encounters a server-side challenge page it will save the fetched HTML to the output directory as `downloads/tiktok_debug_<id>_waf.html` (and a `_waf_retry.html` on mobile-UA retry). The plugin attempts a mobile-UA retry; if the challenge persists the recommended remedies are: provide a fresh `downloads/tiktok_cookies.txt` (Netscape format), run from a different IP/proxy, or use a headless browser extractor to execute page JS and obtain the playable URL.


---

## 5. Important Files

| Path | Role |
|------|------|
| `src/http_module.c` | C implementation of `http` global (libcurl) |
| `src/lua_engine.c` | Loads and manages Lua state, plugin loader; sorts `generic.lua` last |
| `src/ludo_module.c` | C implementation of `ludo` global |
| `src/download_manager.c` | Threaded download queue |
| `src/main.c` | Entry point; `-s scriptfile.lua` runs standalone |
| `ludo_scripting.md` | Full scripting API reference (read this for detailed docs) |
| `build/ludo.log` | Runtime log — all `ludo.log*()` calls write here |
| `third_party/curl-8.19.0/lib/curl_config_linux.h` | curl config for Linux (brotli, zstd, GnuTLS enabled) |

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
- `[%a-]` in character classes may not parse correctly — `-` after `%a`/`%w` can be misparsed. Prefer splitting on `,` and parsing each `KEY=VALUE` pair individually (see `parse_attrs` in `m3u8.lua`).
- All plugins share the same Lua state (globals persist between plugin calls)

---

## 8. Known Build Quirks

- CMake configure step logs a non-fatal `Permission denied` error for
  `ninja restat` — this is benign and can be ignored.
- `ludo-debug.exe --help` hangs (it is a GUI app, not a CLI tool).
- `ludocon-debug.exe --help` works (it is a console app, use `ludocon` for CLI tasks).
- After editing a plugin, just rebuild (`cmake --build build --parallel`);
  CMake's custom command copies all `plugins/*.lua` to `build/plugins/`
  automatically.
- **Linux:** On older distros (e.g. Ubuntu 20.04) the system curl (7.68)
  lacks brotli/zstd support. The CMake build now builds brotli and zstd
  from bundled source as static libraries for potential future use by
  a custom curl build. At runtime, http_module.c detects which content
  encodings curl supports and only advertises those (dynamic Accept-
  Encoding). Brotli and zstd static libs (`libbrotli*.a`, `libzstd.a`)
  are produced in the build output.
- **Linux build requirements:** cmake, gcc, libgtk-3-dev (for GUI),
  pkg-config. No extra TLS libraries needed — the system curl is used.
  
## 9. Plugin Reference from yt-dlp

We can use yt-dlp extractor source code (C:\Projects\yt-dlp-2026.03.17\yt_dlp\extractor\facebook.py) and yt-dlp.log as reference for making ludo plugin.
Run `yt-dlp.exe --cookies downloads\facebook_cookies.txt  --print-traffic -v https://www.facebook.com/reel/1520832029440197 >yt-dlp.log 2>&1` and check yt-dlp.log
