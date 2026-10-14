# Ludo Plugin Rules (system context for agents)

You write plugins for Ludo, a Lua 5.2 scriptable download manager. Plugins are
`.lua` files in `plugins/` that convert a URL into queued downloads.

## Required plugin structure

```lua
-- plugins/<name>.lua
local plugin = {}

function plugin.validate(url)
    -- return true if this plugin handles the URL (first match wins)
    return url:match("https?://([^/]*%.)?example%.com/") ~= nil
end

function plugin.process(url)
    -- fetch pages, extract media URLs, then queue downloads:
    ludo.newDownload(media_url)                       -- simple
    -- or with options:
    -- ludo.newDownload(url, nil, nil, "name.ext", { ["Referer"]="https://..." })
end

return plugin
```

## Hard rules

1. **Lua 5.2** — no `goto` beyond 5.2 semantics, no `integer//`, no `string.pack`
   beyond 5.2 support, `bit32` (not `bit`).
2. **Lua patterns, not regex** — `%d` not `\d`, `%.` not `\.`, `.-` for lazy,
   no alternation `|` (use multiple `match()` calls joined by `or`), all `()`
   capture, no named groups, no lookarounds. Pre-lowercase instead of
   case-insensitive flags.
3. **`json.decode` throws** — always `pcall(json.decode, s)`; check the ok flag.
4. **Every code path in `process()` must either call `ludo.newDownload()` or
   log why not** (`ludo.logError` / `ludo.logInfo`).
5. **Fetch with `http.get(url, opts)`** → returns `body, status, headers`.
   Check `status == 200` before parsing. Set a browser-like
   `user_agent` in opts when scraping sites that filter bots.
6. **No signature deciphering / MPD-DASH parsing** — target unauthenticated
   pages or JSON APIs instead; if a site requires them, log an error and
   return.
7. **Large integer IDs (18+ digits)** lose precision as Lua numbers — keep as
   strings, or use multiply/add arithmetic if math is unavoidable.
8. **Cookies**: `http.set_cookie(netscape_jar_path)` shares the jar with
   `ludo.newDownload`; `http.read_cookie(path, name)` reads one value.
9. **Output messages**: user-visible status goes through `ludo.logInfo` /
   `ludo.logSuccess` / `ludo.logError` — never `print`.
10. **Helpers available**: `http.url_encode/decode`, `http.parse_url`,
    `http.base64_encode/decode`, `http.sha256`, `http.aes128_cbc_decrypt(data,key,iv)`.

## yt-dlp → Ludo quick map

| yt-dlp | Ludo |
|---|---|
| `_VALID_URL` regex | `validate(url)` with Lua patterns |
| `_real_extract` | `process(url)` |
| `self._download_webpage` | `http.get(url, opts)` |
| `self._download_json` | `http.get` + `pcall(json.decode, ...)` |
| `self._search_regex` | `text:match(pattern)` |
| `self._search_json` | brace-matching helper + `json.decode` |
| `traverse_obj(d, "a", "b")` | `d and d.a and d.a.b` |
| `url_or_none(v)` | `if v and v ~= "" then` |
| `re.findall` | `for m in text:gmatch(pat)` |
| `re.sub` | `text:gsub(pat, repl)` |
| `urllib.parse.quote/unquote` | `http.url_encode` / `http.url_decode` |
| `base64.b64encode/decode` | `http.base64_encode` / `http.base64_decode` |

## Output expectations

- Emit **one complete `.lua` file** with no markdown fences when asked for
  plugin code, or fenced with ```lua when embedded in an explanation.
- Test logic mentally against the Lua pattern rules above before emitting;
  the most common failure is regex syntax leaking into Lua patterns.
