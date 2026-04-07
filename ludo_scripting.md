# Ludo Scripting Manual

Ludo embeds **Lua 5.2** as its scripting engine. Every plugin and script has
access to the full Lua standard library plus three extension libraries provided
by Ludo:

| Module | Global name | Description |
|--------|-------------|-------------|
| HTTP   | `http`      | HTTP client powered by libcurl |
| Ludo   | `ludo`      | Download manager & application control |
| UI     | `ui`        | Native GUI widgets (libui) |
| Zip    | `zip`       | ZIP archive creation (zlib deflate) |

Scripts are loaded from the `plugins/` directory. Each plugin is a `.lua` file
that returns a table with two functions: `validate(url)` and `process(url)`.

---

## Table of Contents

1. [Lua Language Basics](#1-lua-language-basics)
2. [Standard Lua Library](#2-standard-lua-library)
3. [JSON Library (`json`)](#json-library-json)
3. [HTTP Library (`http`)](#3-http-library)
4. [Ludo Library (`ludo`)](#4-ludo-library)
5. [UI Library (`ui`)](#5-ui-library)
5. [Zip Library (`zip`)](#5-zip-library)
6. [Plugin System](#6-plugin-system)
7. [Converting yt-dlp Extractors to Ludo Plugins](#7-converting-yt-dlp-extractors-to-ludo-plugins)
8. [Testing and Debugging Plugins](#8-testing-and-debugging-plugins)

---

## 1. Lua Language Basics

### 1.1 Variables and Types

Lua is dynamically typed. Variables do not need type declarations.

```lua
-- Numbers
local count = 42
local pi    = 3.14159

-- Strings
local name  = "Ludo"
local path  = 'C:\\Downloads'
local multi = [[
This is a
multi-line string.
]]

-- Booleans
local active = true
local done   = false

-- Nil (absence of value)
local nothing = nil
```

### 1.2 Tables

Tables are Lua's only compound data structure — they serve as arrays,
dictionaries, objects, and modules.

```lua
-- Array-style (1-indexed)
local fruits = { "apple", "banana", "cherry" }
print(fruits[1])  --> apple
print(#fruits)    --> 3

-- Dictionary-style
local config = {
    url  = "https://example.com",
    port = 8080,
}
print(config.url)       --> https://example.com
print(config["port"])   --> 8080

-- Mixed
local entry = {
    name = "file.zip",
    tags = { "release", "stable" },
}
```

### 1.3 Control Flow

```lua
-- if / elseif / else
if status == 200 then
    print("OK")
elseif status == 404 then
    print("Not found")
else
    print("Error: " .. status)
end

-- while
local i = 1
while i <= 5 do
    print(i)
    i = i + 1
end

-- for (numeric)
for i = 1, 10 do
    print(i)
end

-- for (generic / pairs)
local t = { a = 1, b = 2, c = 3 }
for key, value in pairs(t) do
    print(key, value)
end

-- for (array / ipairs)
local list = { "x", "y", "z" }
for index, value in ipairs(list) do
    print(index, value)
end
```

### 1.4 Functions

```lua
-- Named function
function greet(name)
    return "Hello, " .. name .. "!"
end

-- Local function
local function add(a, b)
    return a + b
end

-- Anonymous / closure
local double = function(x) return x * 2 end

-- Variadic
function printf(fmt, ...)
    print(string.format(fmt, ...))
end

-- Multiple return values
function minmax(list)
    local lo, hi = list[1], list[1]
    for _, v in ipairs(list) do
        if v < lo then lo = v end
        if v > hi then hi = v end
    end
    return lo, hi
end

local lo, hi = minmax({3, 1, 4, 1, 5, 9})
print(lo, hi)  --> 1  9
```

### 1.5 String Operations

```lua
local s = "Hello, World!"

print(string.len(s))           --> 13
print(string.upper(s))         --> HELLO, WORLD!
print(string.lower(s))         --> hello, world!
print(string.sub(s, 1, 5))     --> Hello
print(string.rep("ab", 3))     --> ababab
print(string.reverse(s))       --> !dlroW ,olleH
print(string.find(s, "World")) --> 8  12
print(string.format("pi=%.2f", 3.14159))  --> pi=3.14

-- Pattern matching
for word in string.gmatch(s, "%a+") do
    print(word)
end

-- Substitution
local result = string.gsub(s, "World", "Lua")
print(result)  --> Hello, Lua!
```

### 1.5.1 Lua Pattern Reference

Lua does **not** use PCRE/POSIX regular expressions. It has its own lightweight
pattern language used by `string.match`, `string.gmatch`, `string.gsub`, and
`string.find`.

#### Character Classes

| Class | Meaning |
|-------|---------|
| `.`   | Any character |
| `%a`  | Any letter (A-Z, a-z) |
| `%d`  | Any digit (0-9) |
| `%l`  | Any lowercase letter |
| `%u`  | Any uppercase letter |
| `%w`  | Any alphanumeric character (letter or digit) |
| `%s`  | Any whitespace (space, tab, newline, etc.) |
| `%p`  | Any punctuation character |
| `%c`  | Any control character |
| `%x`  | Any hexadecimal digit (0-9, a-f, A-F) |
| `%A`  | Complement of `%a` (any non-letter) |
| `%D`  | Complement of `%d` (any non-digit) |

Uppercase versions of a class represent the complement (opposite) of the
lowercase version.

#### Quantifiers

| Quantifier | Meaning |
|------------|---------|
| `+`  | 1 or more (greedy) |
| `*`  | 0 or more (greedy) |
| `-`  | 0 or more (lazy / non-greedy) |
| `?`  | 0 or 1 |

#### Anchors & Special

| Symbol | Meaning |
|--------|---------|
| `^`    | Start of string (only at the beginning of a pattern) |
| `$`    | End of string (only at the end of a pattern) |
| `%b()` | Balanced match between two characters, e.g. `%b()` matches balanced parentheses |
| `%%`   | Literal `%` |
| `%.`   | Literal `.` (escape any magic character with `%`) |

**Magic characters** that must be escaped with `%` to match literally:
`( ) . % + - * ? [ ^ $`

#### Character Sets `[ ]`

```lua
"[aeiou]"        -- matches any vowel
"[0-9]"          -- matches any digit (same as %d)
"[%w_]"          -- matches alphanumeric or underscore
"[^%s]"          -- matches any non-whitespace character
"[%w%-_]"        -- matches alphanumeric, hyphen, or underscore
```

#### Captures `( )`

Parentheses create **captures** — the matched substrings are returned by
`string.match` or passed to replacement functions in `string.gsub`.

```lua
-- Single capture
local year = string.match("Date: 2026-04-05", "(%d%d%d%d)")
print(year)  --> 2026

-- Multiple captures
local y, m, d = string.match("2026-04-05", "(%d+)-(%d+)-(%d+)")
print(y, m, d)  --> 2026  04  05

-- Non-capturing: use the full match (no parentheses)
local full = string.match("hello world", "%a+")
print(full)  --> hello
```

#### Practical Examples for Plugin Development

```lua
-- Match a URL host
local host = url:match("^https?://([^/?#]+)")
-- "https://www.instagram.com/p/abc" -> "www.instagram.com"

-- Match a path segment
local shortcode = url:match("/p/([%w%-_]+)")
-- "https://www.instagram.com/p/aye83DjauH/" -> "aye83DjauH"

-- Optional path segments (reel or reels)
local id = url:match("/reels?/([%w%-_]+)")
-- matches both /reel/xxx and /reels/xxx

-- Extract JSON value from HTML
local video_id = html:match('"video_id"%s*:%s*"([^"]+)"')

-- Extract an attribute from an HTML tag
local src = html:match('<video[^>]+src="([^"]+)"')

-- Extract query parameter
local v = query:match("[?&]v=([^&]+)")

-- Iterate over all matches (gmatch)
for link in body:gmatch('href="(/[^"]+/download/[^"]+)"') do
    print(link)
end

-- Greedy vs lazy
local s = "<div>hello</div><div>world</div>"
print(s:match("<div>(.-)</div>"))  --> hello   (lazy -)
print(s:match("<div>(.+)</div>"))  --> hello</div><div>world  (greedy +)

-- Escape literal dots in domain patterns
url:match("instagram%.com")   -- correct: matches "instagram.com"
url:match("instagram.com")    -- WRONG: '.' matches any character

-- Match colon-separated key-value in text
local key, val = line:match('^([^:]+):%s*(.*)')

-- tab-separated fields (cookie files)
local name, value = line:match("\t([^\t]+)\t([^\t]*)$")
```

#### Key Differences from Python `re` / PCRE

| Python `re`     | Lua pattern      | Notes |
|-----------------|-------------------|-------|
| `\d`            | `%d`              | `%` instead of `\` for classes |
| `\w`            | `%w`              | Lua `%w` does not include `_` |
| `[\w_]` or `\w` | `[%w_]`           | Add `_` explicitly in Lua |
| `\s`            | `%s`              | |
| `\b`            | (not available)   | No word-boundary anchor |
| `.*?`           | `.-`              | Lazy quantifier uses `-` |
| `.+?`           | `.+` with `-`     | Use `(.-)` for lazy |
| `(?:...)`       | (not available)   | All `()` are captures |
| `re.IGNORECASE` | (not available)   | Use `[Aa]` or `string.lower()` first |
| `|` (alternation) | (not available) | Use multiple `match()` calls |
| `{3,5}`         | (not available)   | Manually expand or use loops |

### 1.6 Error Handling

```lua
-- pcall (protected call)
local ok, err = pcall(function()
    error("something went wrong")
end)
if not ok then
    print("Caught error: " .. err)
end

-- xpcall (with custom error handler)
local ok, err = xpcall(
    function() error("oops") end,
    function(e) return "HANDLED: " .. e end
)
print(err)  --> HANDLED: ...:oops
```

---

## 2. Standard Lua Library

Ludo opens all standard Lua 5.2 libraries. Below is a summary of each module
with usage examples.

### 2.1 Base Functions

```lua
print("Hello")             -- Print to stdout
type(42)                   -- "number"
type("hi")                 -- "string"
type(nil)                  -- "nil"
tostring(123)              -- "123"
tonumber("42")             -- 42
tonumber("0xff")           -- 255
assert(1 + 1 == 2)         -- passes
assert(false, "failed!")   -- raises error

-- Iterate table
local t = { a = 10, b = 20 }
for k, v in pairs(t) do print(k, v) end

-- Iterate array
local a = { "x", "y" }
for i, v in ipairs(a) do print(i, v) end

-- Load and execute a string
local f = load("return 2 + 3")
print(f())  --> 5

-- Execute a file
dofile("script.lua")
```

### 2.2 `string` Library

```lua
string.byte("A")               --> 65
string.char(65)                 --> "A"
string.len("hello")             --> 5
string.rep("-", 40)             --> ----------------------------------------
string.format("%d items", 5)    --> "5 items"
string.find("foobar", "ob")    --> 3  4
string.sub("abcdef", 2, 4)     --> "bcd"
string.gsub("aaa", "a", "b")   --> "bbb"  3
string.match("2024-01-15", "(%d+)-(%d+)-(%d+)")  --> "2024" "01" "15"
string.upper("hello")           --> "HELLO"
string.lower("HELLO")           --> "hello"
string.reverse("abc")           --> "cba"
```

### 2.3 `table` Library

```lua
local t = { 3, 1, 4, 1, 5 }

table.insert(t, 9)          -- append 9
table.insert(t, 2, 99)      -- insert 99 at position 2
table.remove(t, 1)          -- remove first element
table.sort(t)               -- sort in-place

-- Sort with comparator
table.sort(t, function(a, b) return a > b end)

-- Concatenate array elements
local csv = table.concat({ "a", "b", "c" }, ",")
print(csv)  --> a,b,c

-- Unpack
local a, b, c = table.unpack({ 10, 20, 30 })
print(a, b, c)  --> 10  20  30
```

### 2.4 `math` Library

```lua
math.abs(-7)         --> 7
math.ceil(2.3)       --> 3
math.floor(2.9)      --> 2
math.max(1, 5, 3)    --> 5
math.min(1, 5, 3)    --> 1
math.sqrt(144)       --> 12
math.sin(math.pi/2)  --> 1.0
math.cos(0)          --> 1.0
math.log(math.exp(1))  --> 1.0
math.random()        --> random float [0, 1)
math.random(1, 100)  --> random integer [1, 100]
math.randomseed(os.time())

-- Constants
print(math.pi)    --> 3.1415926535898
print(math.huge)  --> inf
```

### 2.5 `io` Library

```lua
-- Read entire file
local f = io.open("data.txt", "r")
if f then
    local content = f:read("*a")
    f:close()
    print(content)
end

-- Write to file
local f = io.open("output.txt", "w")
f:write("line 1\n")
f:write("line 2\n")
f:close()

-- Append to file
local f = io.open("log.txt", "a")
f:write("new entry\n")
f:close()

-- Read line by line
for line in io.lines("data.txt") do
    print(line)
end
```

### 2.6 `os` Library

```lua
print(os.time())              -- current Unix timestamp
print(os.date("%Y-%m-%d"))    -- formatted date: "2026-03-20"
print(os.date("%H:%M:%S"))    -- formatted time: "14:30:00"
print(os.clock())             -- CPU time in seconds

os.execute("mkdir new_folder")  -- run system command

local tmpname = os.tmpname()    -- temporary file path
os.remove("old_file.txt")      -- delete a file
os.rename("old.txt", "new.txt") -- rename a file

-- Environment variables
print(os.getenv("PATH"))

-- Measure elapsed time
local start = os.clock()
-- ... work ...
local elapsed = os.clock() - start
print(string.format("Took %.3f seconds", elapsed))
```

### 2.7 `bit32` Library (Lua 5.2)

```lua
bit32.band(0xFF, 0x0F)     --> 15
bit32.bor(0xF0, 0x0F)      --> 255
bit32.bxor(0xFF, 0x0F)     --> 240
bit32.bnot(0)               --> 4294967295
bit32.lshift(1, 8)          --> 256
bit32.rshift(256, 4)        --> 16
bit32.btest(0xFF, 0x01)     --> true
bit32.extract(0xFF, 4, 4)   --> 15  (extract 4 bits from bit 4)
```

---

## JSON Library (`json`)

Ludo bundles a fast JSON implementation (Lua CJSON) exposed as the global
`json` module. A "safe" wrapper is also available as `cjson_safe` which
returns `nil, err` instead of throwing on parse/encode errors.

**Key functions:**
- `json.encode(value)` → string: serialise a Lua value (tables, numbers,
    booleans, strings, `json.null`) to JSON. Use `json.null` to represent
    JSON `null` in Lua tables.
- `json.decode(text)` → lua value: parse a JSON string and return the Lua
    representation. `json.decode` raises on malformed input — use
    `pcall(json.decode, text)` to catch errors, or use `require("cjson_safe")`
    to get the safe wrapper that returns `nil, err`.

**Special values & behaviour:**
- `json.null` — a lightuserdata sentinel used to represent JSON `null`.
    Compare with `if v == json.null then ... end`.
- The decoder enforces strict JSON syntax and will error on invalid input.

**Configuration functions:** (each returns the previous/current value)
- `json.encode_sparse_array(convert, ratio, safe)` — control sparse array
    handling when encoding.
- `json.encode_max_depth(n)` — maximum nesting allowed when encoding.
- `json.decode_max_depth(n)` — maximum nesting allowed when decoding.
- `json.encode_number_precision(n)` — number-to-string precision for encode.
- `json.encode_keep_buffer(true|false)` — reuse internal encode buffer.
- `json.encode_invalid_numbers(option)` — how to handle NaN/Inf when
    encoding (`off`, `on`, or `null`).
- `json.decode_invalid_numbers(option)` — whether to accept non-standard
    numbers when decoding.

**Examples**
```lua
local json = json or require("json")

-- Encode a request body (used by plugins/youtube.lua)
local body = json.encode({ videoId = vid, context = { client = { hl = "en" } } })

-- Decode safely
local ok, data = pcall(json.decode, normalize_json_response(response_body))
if not ok then
        print("JSON error:", data)
end
```

---

## 3. HTTP Library

The `http` module is registered as a global table. It provides HTTP client
functionality backed by libcurl with cookie management, URL utilities, and
custom headers.

### 3.1 `http.get(url [, options])` → body, status, headers

Perform an HTTP GET request.

**Parameters:**
- `url` (string) — The URL to fetch.
- `options` (table, optional) — Request options (see [Options Table](#37-options-table)).

**Returns:**
- `body` (string) - Response body.
- `status` (number) - HTTP status code.
- `headers` (table) - Response headers as key-value pairs.

```lua
-- Simple GET
local body, status, headers = http.get("https://httpbin.org/get")
print(status)                  --> 200
print(headers["Content-Type"]) --> application/json

-- GET with options
local body, status = http.get("https://api.example.com/data", {
    user_agent = "MyBot/1.0",
    timeout    = 30,
    headers    = {
        ["Authorization"] = "Bearer my_token",
        ["Accept"]        = "application/json",
    },
})

-- Parse JSON response
if status == 200 then
    -- Process body as needed
    print("Received " .. #body .. " bytes")
end
```

### 3.2 `http.head(url [, options])` → body, status, headers

Perform an HTTP HEAD request (no response body).

**Parameters:**
- `url` (string) — The URL to check.
- `options` (table, optional) — Request options.

**Returns:**
- `body` (string) — Always empty string.
- `status` (number) — HTTP status code.
- `headers` (table) — Response headers.

```lua
-- Check if a URL exists
local _, status, headers = http.head("https://example.com/file.zip")
if status == 200 then
    print("File size: " .. (headers["Content-Length"] or "unknown"))
end

-- Check redirect target
local _, status, headers = http.head("https://short.url/abc", {
    follow_redirects = false,
})
if status == 301 or status == 302 then
    print("Redirects to: " .. (headers["Location"] or "?"))
end
```

### 3.3 `http.post(url, body [, options])` → body, status, headers

Perform an HTTP POST request.

**Parameters:**
- `url` (string) — The target URL.
- `body` (string) — The request body (form data, JSON, etc.).
- `options` (table, optional) — Request options.

**Returns:**
- `body` (string) — Response body.
- `status` (number) — HTTP status code.
- `headers` (table) — Response headers.

```lua
-- POST form data
local body, status = http.post(
    "https://httpbin.org/post",
    "username=admin&password=secret",
    {
        headers = {
            ["Content-Type"] = "application/x-www-form-urlencoded",
        },
    }
)
print(status)  --> 200

-- POST JSON
local json_body = '{"name": "test", "value": 42}'
local body, status = http.post(
    "https://api.example.com/items",
    json_body,
    {
        headers = {
            ["Content-Type"]  = "application/json",
            ["Authorization"] = "Bearer token123",
        },
        timeout = 10,
    }
)
```

### 3.4 `http.set_cookie(filepath)`

Set a file path for persistent cookie storage. All subsequent requests will
read and write cookies from this file.

**Parameters:**
- `filepath` (string) — Path to the cookie jar file.

```lua
http.set_cookie("cookies.txt")

-- Cookies are now persisted across requests
http.get("https://example.com/login")
http.get("https://example.com/dashboard")  -- uses session cookies
```

### 3.5 `http.clear_cookies()`

Clear the cookie jar and last URL state.

```lua
http.clear_cookies()
```

### 3.6 `http.get_last_url()` → string

Get the last effective URL after all redirects from the most recent request.

**Returns:**
- `url` (string) — The final URL, or empty string if no request was made.

```lua
http.get("https://github.com/user/repo/releases/latest")
local final_url = http.get_last_url()
print(final_url)  --> https://github.com/user/repo/releases/tag/v1.2.3
```

### 3.7 Options Table

All HTTP request functions accept an optional `options` table:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `user_agent` | string | `"Mozilla/5.0 LUDO/1.0"` | User-Agent header |
| `follow_redirects` | boolean | `true` | Follow HTTP redirects (max 10) |
| `timeout` | number | none | Request timeout in seconds |
| `headers` | table | none | Custom headers as `{["Name"] = "value"}` |
| `cookies` | string | none | Path to cookie jar file |

### 3.8 `http.url_encode(str)` → string

URL-encode a string (percent-encoding).

**Parameters:**
- `str` (string) — The string to encode.

**Returns:**
- `encoded` (string) — URL-encoded string.

```lua
local encoded = http.url_encode("hello world & more")
print(encoded)  --> hello%20world%20%26%20more
```

### 3.9 `http.url_decode(str)` → string

Decode a URL-encoded string.

**Parameters:**
- `str` (string) — The URL-encoded string.

**Returns:**
- `decoded` (string) — Decoded string.

```lua
local decoded = http.url_decode("hello%20world%20%26%20more")
print(decoded)  --> hello world & more
```

### 3.10 `http.parse_url(url)` → table

Parse a URL into its components.

**Parameters:**
- `url` (string) — The URL to parse.

**Returns:**
- `parts` (table) — Table with fields: `scheme`, `host`, `port`, `path`, `query`.

```lua
local parts = http.parse_url("https://example.com:8080/api/data?key=value")
print(parts.scheme)  --> https
print(parts.host)    --> example.com
print(parts.port)    --> 8080
print(parts.path)    --> /api/data
print(parts.query)   --> key=value
```

### 3.11 Complete HTTP Example

```lua
-- Download plugin: find the latest release of a GitHub repo
local plugin = {}

function plugin.validate(url)
    return string.find(url, "github%.com/.+/releases") ~= nil
end

function plugin.process(url)
    -- Follow redirects to find the actual release page
    local body, status = http.get(url, {
        user_agent = "Ludo-Plugin/1.0",
        timeout    = 30,
    })
    if status ~= 200 then
        ludo.logError("Failed to fetch: " .. url)
        return
    end

    -- Extract download links from the page
    for link in string.gmatch(body, 'href="(/[^"]+/releases/download/[^"]+)"') do
        local full_url = "https://github.com" .. link
        ludo.logInfo("Found: " .. full_url)
        ludo.newDownload(full_url)
    end
end

return plugin
```

### 3.12 `http.read_cookie(filepath, name)` → string|nil

Read a named cookie value from a Netscape-format cookie file.

This is useful for checking session state — for example reading
a `csrftoken` or `sessionid` cookie after `http.set_cookie()` has been
configured and requests have populated the cookie jar.

**Parameters:**
- `filepath` (string) — Path to the Netscape cookie-jar file.
- `name` (string) — The cookie name to search for.

**Returns:**
- `value` (string) — The cookie value, or `nil` if the file cannot be
  opened or the cookie is not found.

**Cookie file format** (one cookie per line, TAB-separated fields):
```
# Netscape HTTP Cookie File
.instagram.com	TRUE	/	TRUE	0	csrftoken	abc123def456
.instagram.com	TRUE	/	TRUE	0	sessionid	789xyz...
```

**HttpOnly cookies:** libcurl writes `HttpOnly` cookies with a `#HttpOnly_`
prefix on the domain field (e.g. `#HttpOnly_.tiktok.com`). `http.read_cookie`
recognises and strips this prefix automatically, so `tt_chain_token` and other
`HttpOnly` cookies are returned correctly.

```lua
-- Read csrftoken from the session cookie jar
local csrf = http.read_cookie("instagram_session.txt", "csrftoken")
if csrf then
    ludo.logInfo("CSRF token found")
end

-- Check if user is authenticated
local sid = http.read_cookie("instagram_cookies.txt", "sessionid")
if sid then
    ludo.logInfo("User session is available")
end
```

### 3.13 `http.base64_encode(str)` → string

Base64-encode a string (binary-safe).

**Parameters:**
- `str` (string) — The string or binary data to encode.

**Returns:**
- `encoded` (string) — Base64-encoded string.

```lua
local b64 = http.base64_encode("hello world")
print(b64)  --> aGVsbG8gd29ybGQ=
```

### 3.14 `http.base64_decode(str)` → string

Decode a base64-encoded string (binary-safe).

**Parameters:**
- `str` (string) — The base64-encoded string.

**Returns:**
- `decoded` (string) — Decoded string or binary data.

```lua
local raw = http.base64_decode("aGVsbG8gd29ybGQ=")
print(raw)  --> hello world
```

### 3.15 `http.sha256(str)` → string

Compute the SHA-256 digest of a string. Returns the raw 32-byte binary digest
(NOT hex-encoded). Use `http.base64_encode` to convert to base64, or iterate
bytes with `string.byte` to work with the raw digest.

**Parameters:**
- `str` (string) — Any string or binary data to hash.

**Returns:**
- `digest` (string) — Raw 32-byte binary SHA-256 digest.

```lua
-- Verify a known SHA-256 hash
local digest = http.sha256("hello")
local hex = digest:gsub(".", function(c) return ("%02x"):format(c:byte()) end)
-- hex == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"

-- Binary comparison (used in TikTok WAF challenge)
local expected = http.base64_decode(v_c)  -- 32 bytes
if http.sha256(prefix .. tostring(i)) == expected then ...

-- Get base64 of digest
local b64_digest = http.base64_encode(http.sha256(data))
```

**Implementation note:** Standalone FIPS 180-4 SHA-256; no external dependency.

---

#### Note on Compression Support

Ludo's HTTP engine supports transparent decompression for responses with
`Accept-Encoding: gzip, deflate, zstd, br` (brotli). Brotli is supported if built with brotli libraries.

---

## 4. Ludo Library

The `ludo` module provides download management and application logging. It is
registered as a global table.

### 4.1 `ludo.newDownload(url [, output_dir [, mode [, filename [, headers]]]])` -> id, status, output_path

Enqueue a new download.

This function returns three values: the assigned download ID, the HTTP
status from the preflight HEAD request, and the resolved output path.

**Parameters:**
- `url` (string) — The URL to download.
- `output_dir` (string, optional) — Destination directory. Defaults to the
    application's configured output directory.
- `mode` (number, optional) — Download mode constant. Defaults to `ludo.DOWNLOAD_NOW`.
- `filename` (string, optional) — Suggested output filename (basename). When provided, Ludo will use this as the filename during preflight and resolution; the final path may be adjusted to avoid collisions.
- `headers` (table, optional) — Extra HTTP headers sent with the actual CDN GET
    request (but **not** the preflight HEAD). Useful for sending `Referer`,
    `Cookie`, or other headers required by a CDN. Format: `{["Name"]="Value", ...}`.
    Multiple headers are supported.

**Returns:**
- `id` (number) - The assigned download ID for tracking.
- `status` (number) - The HTTP status code from the preflight HEAD request, or `0` if unavailable.
- `output_path` (string) - The full output path that will be used for the download.

**Constants:**
- `ludo.DOWNLOAD_NOW` - Start downloading immediately.
- `ludo.DOWNLOAD_QUEUE` - Add to queue, start when a slot is free.

**Note on 403 from preflight HEAD:** Some CDNs (e.g. TikTok) block unauthenticated
HEAD probes but serve the content on authenticated GET. When the URL was freshly
extracted from an authenticated page fetch, it is safe to queue on 403.

```lua
-- Download immediately to default directory
local id, status, output_path = ludo.newDownload("https://example.com/file.zip")
print(id, status, output_path)

-- Download to specific directory
local id = ludo.newDownload("https://example.com/file.zip", "C:\\Downloads")

-- Queue the download
local id = ludo.newDownload("https://example.com/file.zip", nil, ludo.DOWNLOAD_QUEUE)

-- Suggest an output filename (basename)
local id, status, output_path = ludo.newDownload("https://example.com/stream", nil, ludo.DOWNLOAD_NOW, "my_show_episode1.mp4")
print(output_path)  -- e.g. C:\\Downloads\\my_show_episode1.mp4

-- Pass extra headers required by CDN (e.g. TikTok requires Referer + tt_chain_token)
local tt_chain = http.read_cookie(cookie_path, "tt_chain_token")
local _, dl_status, output = ludo.newDownload(
    play_url, outdir, ludo.DOWNLOAD_NOW, "video.mp4", {
        ["Referer"]  = "https://www.tiktok.com/",
        ["Cookie"]   = "tt_chain_token=" .. (tt_chain or ""),
    })
if dl_status == 200 or dl_status == 206 or dl_status == 0 then
    ludo.logSuccess("queued → " .. output)
elseif dl_status == 403 then
    ludo.logSuccess("queued (CDN probe blocked) → " .. output)
else
    ludo.logError("preflight HTTP " .. dl_status)
end
```

### 4.2 `ludo.pauseDownload(id)`

Pause an active download.

**Parameters:**
- `id` (number) — The download ID returned by `newDownload`.

```lua
local id = ludo.newDownload("https://example.com/large-file.zip")
-- Later...
ludo.pauseDownload(id)
```

### 4.3 `ludo.removeDownload(id)`

Remove a download from the manager (cancels if active).

**Parameters:**
- `id` (number) — The download ID returned by `newDownload`.

```lua
ludo.removeDownload(id)
```

### 4.4 `ludo.logError(msg)`

Log an error message to the application's Activity Log panel.

**Parameters:**
- `msg` (string) — The error message.

```lua
ludo.logError("Failed to parse response from API")
```

### 4.5 `ludo.logSuccess(msg)`

Log a success message to the Activity Log panel.

**Parameters:**
- `msg` (string) — The success message.

```lua
ludo.logSuccess("Plugin loaded successfully")
```

### 4.6 `ludo.logInfo(msg)`

Log an informational message to the Activity Log panel.

**Parameters:**
- `msg` (string) — The info message.

```lua
ludo.logInfo("Scanning page for download links...")
```

### 4.7 `ludo.setting`

Configuration values loaded from `config.ini` are exposed through `ludo.setting`.

```lua
print(ludo.setting.maxDownloadRetry)
print(ludo.setting.maxThread)
print(ludo.setting.outputDir)
```
### 4.8 `ludo.getOutputDirectory()` - string

Get the current default output directory.

**Returns:**
- `dir` (string) — The configured output directory path.

```lua
local dir = ludo.getOutputDirectory()
ludo.logInfo("Files will be saved to: " .. dir)
```

---

## 5. UI Library

The `ui` module provides native GUI widget creation via libui. It is registered
as `require("ui")` or accessed via the global `ui` table. All widget
constructors return an object with chainable methods.

> **Note:** The Ludo main window uses its own UI. The `ui` library is intended
> for plugins that need to show their own dialog windows or custom interfaces.

### 5.1 Application Lifecycle

#### `ui.Init()` → string|nil

Initialize the UI subsystem. Must be called before creating any widgets.

**Returns:**
- `nil` on success, or an error string on failure.

```lua
local err = ui.Init()
if err then
    print("UI init failed: " .. err)
    return
end
```

#### `ui.Uninit()`

Shut down the UI subsystem and free resources.

```lua
ui.Uninit()
```

#### `ui.Main()`

Enter the main event loop. This call blocks until `ui.Quit()` is called.

```lua
ui.Main()
```

#### `ui.MainStep(wait)` → number

Process a single iteration of the event loop.

**Parameters:**
- `wait` (boolean) — If `true`, block until an event occurs. If `false`,
  return immediately.

**Returns:**
- `result` (number) — Non-zero if the loop should continue.

```lua
-- Custom event loop
while ui.MainStep(false) ~= 0 do
    -- do other work between UI events
end
```

#### `ui.Quit()`

Request the event loop to exit.

```lua
ui.Quit()
```

### 5.2 Window

#### `ui.NewWindow(title, width, height, hasMenubar)` → Window

Create a new top-level window.

**Parameters:**
- `title` (string) — Window title.
- `width` (number) — Window width in pixels.
- `height` (number) — Window height in pixels.
- `hasMenubar` (boolean) — Whether the window has a menu bar.

**Returns:**
- `window` (Window) — The window object.

```lua
local win = ui.NewWindow("My Window", 640, 480, false)
win:SetMargined(1)
win:Show()
```

#### `window:SetChild(control)`

Set the window's child (main content widget).

**Parameters:**
- `control` — Any UI control to place inside the window.

**Returns:** self (chainable)

```lua
local box = ui.NewVerticalBox()
win:SetChild(box)
```

#### `window:SetMargined(margined)`

Enable or disable margins inside the window.

**Parameters:**
- `margined` (number) — `1` for margins, `0` for no margins.

**Returns:** self (chainable)

#### `window:Show()`

Make the window visible on screen.

**Returns:** self (chainable)

#### `window:Destroy()`

Destroy the window.

### 5.3 Box (Layout Container)

#### `ui.NewVerticalBox()` → Box

Create a vertical layout box. Children are stacked top to bottom.

#### `ui.NewHorizontalBox()` → Box

Create a horizontal layout box. Children are placed left to right.

```lua
local vbox = ui.NewVerticalBox()
vbox:SetPadded(1)

local hbox = ui.NewHorizontalBox()
hbox:SetPadded(1)
```

#### `box:Append(child1 [, child2, ...] [, stretchy])`

Append one or more child controls to the box.

**Parameters:**
- `child` — One or more UI controls to add.
- `stretchy` (boolean/number, optional) — If `true`/`1`, the last child
  stretches to fill available space.

**Returns:** self (chainable)

```lua
local label  = ui.NewLabel("Name:")
local button = ui.NewButton("OK")

-- Append multiple children
vbox:Append(label, button)

-- Append with stretchy
vbox:Append(label, false)
vbox:Append(button, true)  -- button stretches
```

#### `box:Padded()` → number

Get the current padding state.

**Returns:**
- `padded` (number) — `1` if padded, `0` otherwise.

#### `box:SetPadded(padded)`

Enable or disable padding between children.

**Parameters:**
- `padded` (number) — `1` for padding, `0` for no padding.

**Returns:** self (chainable)

### 5.4 Button

#### `ui.NewButton(text)` → Button

Create a push button.

**Parameters:**
- `text` (string) — Button label.

```lua
local btn = ui.NewButton("Click Me")
```

#### `button:SetText(text)`

Change the button label.

**Parameters:**
- `text` (string) — New label text.

**Returns:** self (chainable)

```lua
btn:SetText("Done")
```

#### `button:OnClicked(callback, data)`

Register a click handler.

**Parameters:**
- `callback` (function) — Called as `callback(button, data)` on click.
- `data` — Arbitrary data passed to the callback.

**Returns:** self (chainable)

```lua
btn:OnClicked(function(b, data)
    print("Button clicked! Data: " .. tostring(data))
end, "my_data")
```

### 5.5 Label

#### `ui.NewLabel(text)` → Label

Create a static text label.

**Parameters:**
- `text` (string) — Label text.

```lua
local lbl = ui.NewLabel("Hello, World!")
```

#### `label:Text()` → string

Get the current label text.

```lua
print(lbl:Text())  --> Hello, World!
```

#### `label:SetText(text)`

Set the label text.

**Parameters:**
- `text` (string) — New text.

**Returns:** self (chainable)

```lua
lbl:SetText("Updated text")
```

### 5.6 Checkbox

#### `ui.NewCheckbox(text)` → Checkbox

Create a checkbox with a label.

**Parameters:**
- `text` (string) — Checkbox label.

```lua
local cb = ui.NewCheckbox("Enable notifications")
```

#### `checkbox:SetText(text)`

Change the checkbox label.

**Returns:** self (chainable)

#### `checkbox:OnToggled(callback, data)`

Register a toggle handler.

**Parameters:**
- `callback` (function) — Called as `callback(checkbox, data)` on toggle.
- `data` — Arbitrary data passed to the callback.

**Returns:** self (chainable)

```lua
cb:OnToggled(function(c, data)
    print("Toggled!")
end, nil)
```

### 5.7 Entry (Text Input)

> **Note:** Entry widgets are not currently exposed in the Lua binding.

### 5.8 ProgressBar

#### `ui.NewProgressBar()` → ProgressBar

Create a progress bar widget.

```lua
local pb = ui.NewProgressBar()
```

#### `progressbar:SetValue(value)`

Set the progress bar value.

**Parameters:**
- `value` (number) — Progress value (0–100). Use `-1` for indeterminate.

**Returns:** self (chainable)

```lua
pb:SetValue(50)   -- 50%
pb:SetValue(-1)   -- indeterminate (pulsing)
pb:SetValue(100)  -- complete
```

### 5.9 Slider

#### `ui.NewSlider(min, max)` → Slider

Create a slider (trackbar) widget.

**Parameters:**
- `min` (number) — Minimum value.
- `max` (number) — Maximum value.

```lua
local slider = ui.NewSlider(0, 100)
```

#### `slider:Value()` → number

Get the current slider value.

#### `slider:SetValue(value)`

Set the slider value.

**Returns:** self (chainable)

#### `slider:OnChanged(callback, data)`

Register a change handler.

**Parameters:**
- `callback` (function) — Called as `callback(slider, data)` when value changes.
- `data` — Arbitrary data.

**Returns:** self (chainable)

```lua
slider:OnChanged(function(s, data)
    print("Slider value: " .. s:Value())
end, nil)
```

### 5.10 Spinbox

#### `ui.NewSpinbox(min, max)` → Spinbox

Create a numeric spinbox widget.

**Parameters:**
- `min` (number) — Minimum value.
- `max` (number) — Maximum value.

```lua
local spin = ui.NewSpinbox(0, 100)
spin:SetValue(42)
```

#### `spinbox:Value()` → number

Get the current value.

#### `spinbox:SetValue(value)`

Set the spinbox value.

**Returns:** self (chainable)

#### `spinbox:OnChanged(callback, data)`

Register a change handler.

**Returns:** self (chainable)

```lua
spin:OnChanged(function(s, data)
    print("New value: " .. s:Value())
end, nil)
```

### 5.11 Combobox (Dropdown)

#### `ui.NewCombobox()` → Combobox

Create a dropdown combobox.

#### `ui.NewEditableCombobox()` → Combobox

Create an editable dropdown combobox.

```lua
local combo = ui.NewCombobox()
```

#### `combobox:Append(item1, item2, ...)`

Add items to the combobox.

**Parameters:**
- `item` (string) — One or more item labels.

**Returns:** self (chainable)

```lua
combo:Append("Option A", "Option B", "Option C")
```

#### `combobox:OnToggled(callback, data)`

Register a selection change handler.

**Returns:** self (chainable)

```lua
combo:OnToggled(function(c, data)
    print("Selection changed")
end, nil)
```

### 5.12 RadioButtons

#### `ui.NewRadioButtons()` → RadioButtons

Create a radio button group.

```lua
local radio = ui.NewRadioButtons()
radio:Append("Small", "Medium", "Large")
```

#### `radiobuttons:Append(item1, item2, ...)`

Add items to the radio button group.

**Returns:** self (chainable)

### 5.13 Group

#### `ui.NewGroup(title)` → Group

Create a labeled group box (frame).

**Parameters:**
- `title` (string) — Group title.

```lua
local grp = ui.NewGroup("Settings")
grp:SetMargined(1)
```

#### `group:Title()` → string

Get the group title.

#### `group:SetTitle(title)`

Set the group title.

**Returns:** self (chainable)

#### `group:SetChild(control)`

Set the group's child control.

**Parameters:**
- `control` — A UI control.

**Returns:** self (chainable)

```lua
local box = ui.NewVerticalBox()
grp:SetChild(box)
```

#### `group:Margined()` → number

Get the margin state.

#### `group:SetMargined(margined)`

Enable or disable margins.

**Returns:** self (chainable)

### 5.14 Tab

#### `ui.NewTab()` → Tab

Create a tabbed container.

```lua
local tab = ui.NewTab()
```

#### `tab:Append(name1, control1 [, name2, control2, ...])`

Add tabs with labels and content controls.

**Parameters:**
- `name` (string) — Tab label.
- `control` — UI control for the tab content.

**Returns:** self (chainable)

```lua
local page1 = ui.NewVerticalBox()
local page2 = ui.NewVerticalBox()
tab:Append("General", page1, "Advanced", page2)
```

### 5.15 Separator

#### `ui.NewHorizontalSeparator()` → Separator

Create a horizontal line separator.

```lua
local sep = ui.NewHorizontalSeparator()
vbox:Append(sep)
```

### 5.16 Date/Time Pickers

#### `ui.NewDateTimePicker()` → DateTimePicker

Create a combined date+time picker.

#### `ui.NewDatePicker()` → DateTimePicker

Create a date-only picker.

#### `ui.NewTimePicker()` → DateTimePicker

Create a time-only picker.

```lua
local dt = ui.NewDateTimePicker()
local d  = ui.NewDatePicker()
local t  = ui.NewTimePicker()
```

### 5.17 Area (Custom Drawing)

#### `ui.NewArea()` → Area

Create a custom drawing area.

#### `area:SetSize(width, height)`

Set the area size.

**Parameters:**
- `width` (number) — Width in pixels.
- `height` (number) — Height in pixels.

**Returns:** self (chainable)

```lua
local area = ui.NewArea()
area:SetSize(400, 300)
```

### 5.18 Complete UI Example

```lua
local ui = require("ui")

-- Initialize
local err = ui.Init()
if err then
    print("Error: " .. err)
    return
end

-- Create main window
local win = ui.NewWindow("Ludo Plugin", 400, 300, false)
win:SetMargined(1)

-- Create layout
local vbox = ui.NewVerticalBox()
vbox:SetPadded(1)

-- Add a label
local label = ui.NewLabel("Enter a URL:")
vbox:Append(label)

-- Add a progress bar
local progress = ui.NewProgressBar()
vbox:Append(progress)

-- Add buttons in a horizontal box
local hbox = ui.NewHorizontalBox()
hbox:SetPadded(1)

local startBtn = ui.NewButton("Start")
startBtn:OnClicked(function(b, data)
    progress:SetValue(-1)  -- indeterminate
    label:SetText("Downloading...")
end, nil)

local stopBtn = ui.NewButton("Stop")
stopBtn:OnClicked(function(b, data)
    progress:SetValue(0)
    label:SetText("Stopped.")
end, nil)

hbox:Append(startBtn, stopBtn, true)
vbox:Append(hbox)

-- Add a group with settings
local grp = ui.NewGroup("Options")
grp:SetMargined(1)
local innerBox = ui.NewVerticalBox()
innerBox:SetPadded(1)
local cb = ui.NewCheckbox("Auto-start downloads")
innerBox:Append(cb)
local slider = ui.NewSlider(1, 10)
innerBox:Append(slider)
grp:SetChild(innerBox)
vbox:Append(grp)

-- Set window content and show
win:SetChild(vbox)
win:Show()

-- Run event loop
ui.Main()

-- Cleanup
ui.Uninit()
```

### 5.19 Dialog with Tabs Example

```lua
local ui = require("ui")
ui.Init()

local win = ui.NewWindow("Settings", 500, 400, false)
win:SetMargined(1)

local tab = ui.NewTab()

-- General tab
local genBox = ui.NewVerticalBox()
genBox:SetPadded(1)
genBox:Append(ui.NewLabel("Download Directory:"))
genBox:Append(ui.NewLabel(ludo.getOutputDirectory()))
genBox:Append(ui.NewHorizontalSeparator())
genBox:Append(ui.NewCheckbox("Start minimized"))
genBox:Append(ui.NewCheckbox("Auto-update plugins"))

-- Network tab
local netBox = ui.NewVerticalBox()
netBox:SetPadded(1)
netBox:Append(ui.NewLabel("Max connections:"))
netBox:Append(ui.NewSpinbox(1, 16))
netBox:Append(ui.NewLabel("Speed limit:"))
netBox:Append(ui.NewSlider(0, 1000))

-- About tab
local aboutBox = ui.NewVerticalBox()
aboutBox:SetPadded(1)
aboutBox:Append(ui.NewLabel("Ludo Download Manager"))
aboutBox:Append(ui.NewLabel("Version 1.0"))
aboutBox:Append(ui.NewProgressBar())

tab:Append("General", genBox, "Network", netBox, "About", aboutBox)

win:SetChild(tab)
win:Show()
ui.Main()
ui.Uninit()
```

---

## 6. Plugin System

### 6.1 Plugin Structure

Each plugin is a `.lua` file placed in the `plugins/` directory. It must return
a table with two functions:

```lua
local plugin = {}

function plugin.validate(url)
    -- Return true if this plugin handles the given URL
    return string.find(url, "example%.com") ~= nil
end

function plugin.process(url)
    -- Process the URL: fetch pages, extract links, enqueue downloads
    ludo.logInfo("Processing: " .. url)
    ludo.newDownload(url) -- enqueue using resolved filename
    -- Optionally pass a filename hint (basename):
    -- ludo.newDownload(url, nil, nil, "suggested-filename.ext")
end

return plugin
```

### 6.2 Plugin Lifecycle

1. **Load** — On startup (or when the user selects a plugin directory), Ludo
   scans for `*.lua` files and verifies each has `validate` and `process`
   functions.
2. **Validate** — When a URL is submitted, each plugin's `validate(url)` is
   called in order. The first plugin that returns `true` wins.
3. **Process** — The matching plugin's `process(url)` is called. It should call
   `ludo.newDownload()` to enqueue files.
4. **Fallback** — If no plugin matches, Ludo downloads the URL directly.

### 6.3 Example: GitHub Release Plugin

```lua
-- plugins/github_release.lua
local plugin = {}

function plugin.validate(url)
    return string.find(url, "github%.com/[^/]+/[^/]+/releases") ~= nil
end

function plugin.process(url)
    ludo.logInfo("Fetching GitHub release page...")

    local body, status = http.get(url, {
        user_agent = "Ludo-GitHub-Plugin/1.0",
        timeout    = 30,
    })

    if status ~= 200 then
        ludo.logError("HTTP " .. status .. " for " .. url)
        return
    end

    local count = 0
    for link in string.gmatch(body, 'href="(/[^"]+/releases/download/[^"]+)"') do
        local download_url = "https://github.com" .. link
        ludo.logInfo("Queuing: " .. download_url)
        ludo.newDownload(download_url)
        count = count + 1
    end

    if count == 0 then
        ludo.logError("No download links found on the release page")
    else
        ludo.logSuccess("Found " .. count .. " file(s) to download")
    end
end

return plugin
```

### 6.4 Example: Direct File Plugin

```lua
-- plugins/direct_file.lua
local plugin = {}

local extensions = { ".zip", ".tar.gz", ".exe", ".msi", ".dmg", ".AppImage" }

function plugin.validate(url)
    local lower = string.lower(url)
    for _, ext in ipairs(extensions) do
        if string.sub(lower, -#ext) == ext then
            return true
        end
    end
    return false
end

function plugin.process(url)
    ludo.logInfo("Direct download: " .. url)
    ludo.newDownload(url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
end

return plugin
```

### 6.5 Example: Batch URL Plugin

```lua
-- plugins/batch_url.lua
local plugin = {}

function plugin.validate(url)
    -- Handle text files containing one URL per line
    return string.sub(url, -4) == ".txt" and
           (string.sub(url, 1, 7) == "http://" or
            string.sub(url, 1, 8) == "https://")
end

function plugin.process(url)
    ludo.logInfo("Fetching URL list from: " .. url)

    local body, status = http.get(url)
    if status ~= 200 then
        ludo.logError("Failed to fetch URL list: HTTP " .. status)
        return
    end

    local count = 0
    for line in string.gmatch(body, "[^\r\n]+") do
        line = string.match(line, "^%s*(.-)%s*$")  -- trim
        if string.sub(line, 1, 4) == "http" then
            ludo.newDownload(line, nil, ludo.DOWNLOAD_QUEUE)
            count = count + 1
        end
    end

    ludo.logSuccess("Queued " .. count .. " download(s) from list")
end

return plugin
```

### 6.6 Example: Plugin with UI Dialog

```lua
-- plugins/custom_dialog.lua
local ui = require("ui")
local plugin = {}

function plugin.validate(url)
    return string.find(url, "chooser%.example%.com") ~= nil
end

function plugin.process(url)
    ludo.logInfo("Fetching options from: " .. url)

    local body, status = http.get(url)
    if status ~= 200 then
        ludo.logError("Failed: HTTP " .. status)
        return
    end

    -- Parse available files
    local files = {}
    for name, link in string.gmatch(body, '<a href="([^"]+)">([^<]+)</a>') do
        table.insert(files, { name = name, url = link })
    end

    -- Show a selection dialog
    ui.Init()
    local win = ui.NewWindow("Select Files", 300, 200, false)
    win:SetMargined(1)

    local vbox = ui.NewVerticalBox()
    vbox:SetPadded(1)
    vbox:Append(ui.NewLabel("Found " .. #files .. " file(s):"))

    for _, file in ipairs(files) do
        local btn = ui.NewButton(file.name)
        btn:OnClicked(function(b, data)
            ludo.newDownload(data)
            ludo.logSuccess("Queued: " .. data)
        end, file.url)
        vbox:Append(btn)
    end

    local closeBtn = ui.NewButton("Close")
    closeBtn:OnClicked(function() ui.Quit() end, nil)
    vbox:Append(closeBtn)

    win:SetChild(vbox)
    win:Show()
    ui.Main()
    ui.Uninit()
end

return plugin
```

---

## Appendix: Quick Reference

### HTTP Functions

| Function | Signature | Returns |
|----------|-----------|---------|
| `http.get` | `(url [, opts])` | `body, status, headers` |
| `http.head` | `(url [, opts])` | `body, status, headers` |
| `http.post` | `(url, body [, opts])` | `body, status, headers` |
| `http.set_cookie` | `(filepath)` | — |
| `http.clear_cookies` | `()` | — |
| `http.get_last_url` | `()` | `url` |
| `http.url_encode` | `(str)` | `encoded` |
| `http.url_decode` | `(str)` | `decoded` |
| `http.parse_url` | `(url)` | `{scheme, host, port, path, query}` |
| `http.read_cookie` | `(filepath, name)` | `value` or `nil` (HttpOnly cookies supported) |
| `http.base64_encode` | `(str)` | `encoded` |
| `http.base64_decode` | `(str)` | `decoded` |
| `http.sha256` | `(str)` | `32-byte raw binary digest` |

### Ludo Functions

| Function | Signature | Returns |
|----------|-----------|---------|
| `ludo.newDownload` | `(url [, dir [, mode [, filename [, headers]]]])` | `id, status, output_path` |
| `ludo.pauseDownload` | `(id)` | — |
| `ludo.removeDownload` | `(id)` | — |
| `ludo.logError` | `(msg)` | — |
| `ludo.logSuccess` | `(msg)` | — |
| `ludo.logInfo` | `(msg)` | — |
| `ludo.getOutputDirectory` | `()` | `dir` |
| `ludo.setting` | `.maxDownloadRetry`, `.maxThread`, ... | config values |

### Ludo Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ludo.DOWNLOAD_NOW` | 0 | Start downloading immediately |
| `ludo.DOWNLOAD_QUEUE` | 1 | Add to download queue |

### UI Functions

| Function | Signature | Returns |
|----------|-----------|---------|
| `ui.Init` | `()` | `err` or `nil` |
| `ui.Uninit` | `()` | — |
| `ui.Main` | `()` | — |
| `ui.MainStep` | `(wait)` | `result` |
| `ui.Quit` | `()` | — |
| `ui.NewWindow` | `(title, w, h, menubar)` | `Window` |
| `ui.NewVerticalBox` | `()` | `Box` |
| `ui.NewHorizontalBox` | `()` | `Box` |
| `ui.NewButton` | `(text)` | `Button` |
| `ui.NewLabel` | `(text)` | `Label` |
| `ui.NewCheckbox` | `(text)` | `Checkbox` |
| `ui.NewCombobox` | `()` | `Combobox` |
| `ui.NewEditableCombobox` | `()` | `Combobox` |
| `ui.NewProgressBar` | `()` | `ProgressBar` |
| `ui.NewSlider` | `(min, max)` | `Slider` |
| `ui.NewSpinbox` | `(min, max)` | `Spinbox` |
| `ui.NewRadioButtons` | `()` | `RadioButtons` |
| `ui.NewGroup` | `(title)` | `Group` |
| `ui.NewTab` | `()` | `Tab` |
| `ui.NewHorizontalSeparator` | `()` | `Separator` |
| `ui.NewDateTimePicker` | `()` | `DateTimePicker` |
| `ui.NewDatePicker` | `()` | `DateTimePicker` |
| `ui.NewTimePicker` | `()` | `DateTimePicker` |
| `ui.NewArea` | `()` | `Area` |

---

## 7. Converting yt-dlp Extractors to Ludo Plugins

[yt-dlp](https://github.com/yt-dlp/yt-dlp) is a Python video downloader whose
*extractors* (one per site) are the de-facto reference for scraping media from
social-media and video-hosting sites.  A Ludo plugin serves the same purpose
but is written in Lua.  This section provides a systematic translation guide.

### 7.1 Architecture Mapping

| yt-dlp concept | Ludo equivalent | Notes |
|----------------|-----------------|-------|
| `InfoExtractor` class | `plugin` table returned by the `.lua` file | One class → one file |
| `_VALID_URL` (regex) | `plugin.validate(url)` (Lua patterns) | See [Python→Lua pattern table](#715-regex-to-lua-pattern-cheat-sheet) |
| `_real_extract(self, url)` | `plugin.process(url)` | Must call `ludo.newDownload()` |
| `self._download_webpage(url, ...)` | `http.get(url, opts)` | Returns `body, status, headers` |
| `self._download_json(url, ...)` | `http.get()` + `json.decode()` | Combine two calls |
| `self._search_regex(pattern, ...)` | `string.match(text, pattern)` | Translate regex to Lua pattern |
| `self._search_json(pattern, ...)` | `extract_json_object(text, pattern)` | Write a JSON brace-matcher (see §7.6) |
| `traverse_obj(data, ...)` | Nested table indexing with nil-checks | `data and data.key1 and data.key1.key2` |
| `self.report_warning(msg)` | `ludo.logError(msg)` or `ludo.logInfo(msg)` | |
| `self.raise_login_required(msg)` | `ludo.logError(msg); return nil` | |
| `self._get_cookies(url)` | `http.read_cookie(filepath, name)` | Reads Netscape cookie jar |
| `url_or_none(val)` | `if val and val ~= "" then ... end` | |
| `int_or_none(val)` | `tonumber(val)` | Returns `nil` on failure |
| `float_or_none(val)` | `tonumber(val)` | Same function in Lua |
| `str_or_none(val)` | `if type(val) == "string" then ... end` | |
| `json.dumps(obj)` | `json.encode(obj)` | Lua CJSON |
| `json.loads(text)` | `json.decode(text)` | Wrap in `pcall()` |
| `urllib.parse.quote(s)` | `http.url_encode(s)` | |
| `urllib.parse.unquote(s)` | `http.url_decode(s)` | |
| `urllib.parse.urlparse(url)` | `http.parse_url(url)` | Returns table with `.scheme`, `.host`, etc. |
| `base64.b64encode(s)` | `http.base64_encode(s)` | |
| `base64.b64decode(s)` | `http.base64_decode(s)` | |
| `hashlib.sha256(s).digest()` | `http.sha256(s)` | Returns raw 32-byte binary, not hex |
| `hashlib.sha256(a).copy().update(b).digest()` | `http.sha256(a .. b)` | Concatenate inputs |
| `re.search(r'...', text)` | `text:match("...")` | See pattern cheat sheet |
| `re.findall(r'...', text)` | Loop with `text:gmatch("...")` | |
| `re.sub(r'...', repl, text)` | `text:gsub("...", repl)` | |
| `hashlib.md5(s).hexdigest()` | (not built-in) | See §7.7 |
| `itertools.count(1)` | `for page = 1, math.huge do ... end` | |

### 7.2 Step-by-Step Conversion Process

1. **Study `_VALID_URL`** — note the URL patterns the extractor handles.
   Convert the Python regex to one or more `string.match()` / `string.find()`
   calls in `plugin.validate(url)`.

2. **Study `_real_extract()`** — identify the extraction strategies in order
   (API calls, GraphQL, HTML scraping, embed page fallbacks).

3. **Map HTTP requests** — every `self._download_webpage` / `_download_json`
   maps to `http.get()` or `http.post()`.  Set headers, user agent, cookies
   via the `options` table.

4. **Map JSON parsing** — replace `self._search_json(pattern, webpage, ...)`
   with a combination of `string.match()` to find the JSON region and
   `json.decode()` to parse it.

5. **Map regex searches** — translate Python regex in `_search_regex(...)` to
   Lua patterns.  When Lua patterns are insufficient (alternation, look-ahead),
   use multiple `match()` calls with `or`.

6. **Handle output** — instead of returning an info dict, call
   `ludo.newDownload(video_url, dir, mode, filename)` for each media URL
   found.

7. **Handle authentication** — if the extractor checks for `sessionid` or
   other cookies, use `http.set_cookie(filepath)` + `http.read_cookie()`.

### 7.3 Common Python → Lua Translations

#### Downloading and parsing a web page

```python
# yt-dlp (Python)
webpage = self._download_webpage(url, video_id)
```
```lua
-- Ludo (Lua)
local body, status = http.get(url, {
    user_agent = "Mozilla/5.0 ...",
    timeout    = 20,
})
if status ~= 200 then
    ludo.logError("HTTP " .. status)
    return nil
end
```

#### Downloading and parsing JSON

```python
# yt-dlp (Python)
data = self._download_json(api_url, video_id, headers=headers)
```
```lua
-- Ludo (Lua)
local body, status = http.get(api_url, {
    user_agent = UA,
    timeout    = 20,
    headers    = {
        ["X-Custom-Header"] = "value",
    },
})
if status ~= 200 then return nil end
local ok, data = pcall(json.decode, body)
if not ok then
    ludo.logError("JSON parse error: " .. tostring(data))
    return nil
end
```

#### POSTing JSON to an API

```python
# yt-dlp (Python)
data = self._download_json(
    api_url, video_id,
    data=json.dumps(payload).encode(),
    headers={'Content-Type': 'application/json'})
```
```lua
-- Ludo (Lua)
local payload = json.encode({
    videoId = video_id,
    context = { client = { hl = "en" } },
})
local body, status = http.post(api_url, payload, {
    headers = { ["Content-Type"] = "application/json" },
})
local ok, data = pcall(json.decode, body)
```

#### Extracting a value from HTML with regex

```python
# yt-dlp
video_url = self._search_regex(
    r'"video_url"\s*:\s*"([^"]+)"', webpage, 'video url', default=None)
```
```lua
-- Ludo
local video_url = body:match('"video_url"%s*:%s*"([^"]+)"')
```

#### `traverse_obj` → nested table access

```python
# yt-dlp
username = traverse_obj(media, ('owner', 'username'))
edges = traverse_obj(media, ('edge_sidecar_to_children', 'edges', ..., 'node'))
```
```lua
-- Ludo: safe nested access
local username = media and media.owner and media.owner.username

-- For arrays: iterate edges
local edges = media
    and media.edge_sidecar_to_children
    and media.edge_sidecar_to_children.edges
if edges then
    for _, edge in ipairs(edges) do
        local node = edge.node
        -- process node
    end
end
```

#### Cookie / session management

```python
# yt-dlp
if self._get_cookies(url).get('sessionid'):
    # authenticated path
```
```lua
-- Ludo
http.set_cookie("session.txt")
local sid = http.read_cookie("cookies.txt", "sessionid")
if sid then
    -- authenticated path
end
```

### 7.4 Plugin Template (Site Extractor)

Use this as a starting point when converting any yt-dlp extractor:

```lua
-- plugins/sitename.lua
local plugin = {
    name    = "SiteName",
    version = "20260405",
    creator = "Your Name",
}

local json = json or require("json")

local SITE_HOME   = "https://www.example.com"
local API_BASE    = "https://api.example.com/v1"
local DESKTOP_UA  = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    .. "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
local TIMEOUT     = 20

-- ── helpers ──────────────────────────────────────────────────────────

local function extract_id(url)
    -- Translate _VALID_URL regex to Lua pattern(s)
    return url:match("/video/(%d+)")
        or url:match("/v/([%w%-_]+)")
end

local function safe_filename(s)
    if not s then return "video" end
    return s:gsub("[^%w%-_]", "_"):sub(1, 80)
end

-- ── validate ─────────────────────────────────────────────────────────

function plugin.validate(url)
    if not url:match("^https?://") then return false end
    local host = (url:match("^https?://([^/?#]+)") or ""):lower()
    if host ~= "www.example.com" and host ~= "example.com" then
        return false
    end
    return extract_id(url) ~= nil
end

-- ── process ──────────────────────────────────────────────────────────

function plugin.process(url)
    local video_id = extract_id(url)
    if not video_id then
        ludo.logError("SiteName: could not extract video ID from " .. url)
        return nil
    end

    ludo.logInfo("SiteName: processing video " .. video_id)

    -- Strategy 1: API
    local api_url = API_BASE .. "/media/" .. video_id .. "/info/"
    local body, status = http.get(api_url, {
        user_agent = DESKTOP_UA,
        timeout    = TIMEOUT,
        headers    = { ["Accept"] = "application/json" },
    })

    if status == 200 then
        local ok, data = pcall(json.decode, body)
        if ok and data then
            local video_url = data.video_url
                or (data.formats and data.formats[1] and data.formats[1].url)
            if video_url then
                local title = data.title or ("video_" .. video_id)
                local filename = safe_filename(title) .. ".mp4"
                ludo.newDownload(video_url, ludo.getOutputDirectory(),
                                ludo.DOWNLOAD_NOW, filename)
                ludo.logSuccess("SiteName: queued → " .. filename)
                return
            end
        end
    end

    -- Strategy 2: HTML scraping fallback
    local page, pstatus = http.get(url, {
        user_agent = DESKTOP_UA,
        timeout    = TIMEOUT,
    })
    if pstatus == 200 then
        local video_url = page:match('"video_url"%s*:%s*"([^"]+)"')
            or page:match('<video[^>]+src="([^"]+)"')
        if video_url then
            ludo.newDownload(video_url, ludo.getOutputDirectory(),
                            ludo.DOWNLOAD_NOW, "video_" .. video_id .. ".mp4")
            ludo.logSuccess("SiteName: queued (HTML fallback)")
            return
        end
    end

    ludo.logError("SiteName: all strategies failed for " .. url)
end

return plugin
```

### 7.5 Regex to Lua Pattern Cheat Sheet

| Python regex | Lua pattern | Notes |
|--------------|-------------|-------|
| `\d+` | `%d+` | |
| `\w+` | `[%w_]+` | Lua `%w` excludes `_` |
| `\s+` | `%s+` | |
| `[^"]+` | `[^"]+` | Same syntax |
| `[^/]+` | `[^/]+` | Same syntax |
| `\.` | `%.` | Escape with `%` not `\` |
| `\-` | `%-` | Dash is a quantifier in Lua |
| `.*?` | `.-` | Lazy match |
| `.+?` | `.+` → use `.-` instead | |
| `(group)` | `(group)` | Same syntax; all groups capture |
| `(?:group)` | (not available) | All `()` capture in Lua |
| `a\|b` | (not available) | Use two `match()` calls with `or` |
| `^https?://` | `^https?://` | `?` works the same |
| `[\w-]+` | `[%w%-]+` | Escape `-` with `%` |
| `(?P<name>...)` | `(...)` | Named groups not available |
| `re.IGNORECASE` | (not available) | Pre-lowercase the string |
| `re.search(pat, text)` | `text:match(pat)` | |
| `re.findall(pat, text)` | `for m in text:gmatch(pat)` | |
| `re.sub(pat, repl, text)` | `text:gsub(pat, repl)` | |

### 7.6 Extracting JSON from HTML

Many yt-dlp extractors use `_search_json()` to find a JSON blob embedded in
JavaScript.  In Lua, write a brace-matching helper:

```lua
-- Find the first '{' after a pattern match, then walk the string tracking
-- brace depth and string escaping to locate the matching '}'.
-- Returns the parsed Lua table, or nil.
local function extract_json_object(text, pattern)
    local _, pe = text:find(pattern)
    if not pe then return nil end
    local start = text:find("{", pe + 1)
    if not start then return nil end

    local depth, in_str, escaped = 0, false, false
    for i = start, #text do
        local c = text:sub(i, i)
        if escaped then
            escaped = false
        elseif c == "\\" and in_str then
            escaped = true
        elseif c == '"' then
            in_str = not in_str
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

-- Usage (matches yt-dlp's _search_json):
-- Python: self._search_json(r'window\._sharedData\s*=', webpage, 'shared data', video_id)
-- Lua:
local shared = extract_json_object(body, "window%._sharedData%s*=")
```

### 7.7 Things yt-dlp Has That Ludo Does Not

| yt-dlp feature | Workaround in Ludo |
|----------------|-------------------|
| Regex alternation `a\|b` | Multiple `match()` calls: `url:match(pat1) or url:match(pat2)` |
| Named capture groups | Use positional captures |
| `hashlib.md5/sha1` | Implement in Lua with `bit32`, or skip if not essential |
| MPD/DASH manifest parsing | Not built-in; extract the best video URL from JSON instead |
| Signature deciphering | Not supported; target unauthenticated/API routes |
| Subtitle extraction | Not applicable — Ludo downloads files, not metadata |
| Proxy / SOCKS support | Not exposed in `http` module yet |
| `--cookies-from-browser` | Export cookies with a browser extension and place in output dir |

### 7.8 Real-World Example: Instagram

The `plugins/instagram.lua` file is a complete example of converting yt-dlp's
`instagram.py` extractor into a Ludo plugin.  Key techniques used:

1. **`_VALID_URL`** → `validate()` with four `url:match()` calls joined by `or`.
2. **base-64 shortcode→PK** conversion → big-integer arithmetic with
   `bigint_muladd()` (Lua 5.2 doubles lose precision for 18-digit IDs).
3. **Three extraction strategies** tried in order:
   - REST API (`/media/{pk}/info/`) — requires `sessionid` cookie.
   - GraphQL query (`doc_id=8845758582119845`) — works for public posts.
   - Embed page scraping (`/p/{id}/embed/`) — unauthenticated fallback.
4. **Cookie management** — `http.set_cookie()` + `http.read_cookie()` to
   check `csrftoken` and seed session from a user-exported cookie file.
5. **Carousel support** — iterates `edge_sidecar_to_children.edges` or
   `carousel_media` to queue multiple video downloads from one post.

---

## 8. Testing and Debugging Plugins

### 8.1 Prerequisites

Build Ludo in Debug configuration to get `ludo-debug.exe`:

```bash
cd build
cmake --build . --config Debug
```

The debug build writes verbose curl logs and Lua output to `ludo.log` in the
current working directory.

### 8.2 Running a Plugin Script

Use the `-s` (or `--script`) flag to execute a standalone Lua script:

```bash
cd build
./ludo-debug.exe -s test_myplugin.lua
```

The `-s` flag runs the script inside the full Ludo Lua environment — all
built-in modules (`http`, `ludo`, `json`, `ui`) are available.

### 8.3 Writing a Test Script

A test script loads a plugin with `dofile()`, tests `validate()` with known
URLs, and then calls `process()` on a live URL.

```lua
-- build/test_myplugin.lua
ludo.logInfo("=== MyPlugin test ===")

-- 1. Load the plugin
local ok, plugin = pcall(dofile, "plugins/myplugin.lua")
if not ok then
    ludo.logError("Failed to load plugin: " .. tostring(plugin))
    return
end
ludo.logInfo("Loaded: " .. (plugin.name or "?"))

-- 2. Test validate()
local cases = {
    { url = "https://www.example.com/video/12345",  expect = true  },
    { url = "https://www.example.com/user/profile",  expect = false },
    { url = "https://www.youtube.com/watch?v=abc",   expect = false },
}

local pass, fail = 0, 0
for _, c in ipairs(cases) do
    local result = plugin.validate(c.url)
    if (result and true or false) == c.expect then
        pass = pass + 1
        ludo.logInfo("  PASS validate(" .. c.url .. ")")
    else
        fail = fail + 1
        ludo.logError("  FAIL validate(" .. c.url .. "): got "
            .. tostring(result) .. ", want " .. tostring(c.expect))
    end
end
ludo.logInfo("validate: " .. pass .. " passed, " .. fail .. " failed")

-- 3. Live test process()
local test_url = "https://www.example.com/video/12345"
ludo.logInfo("--- process(" .. test_url .. ")")
local ok2, err2 = pcall(plugin.process, test_url)
if not ok2 then
    ludo.logError("process() error: " .. tostring(err2))
else
    ludo.logInfo("process() completed")
end

ludo.logInfo("=== test done ===")
```

### 8.4 Checking the Log

After running the script, inspect `ludo.log` in the build directory:

```bash
# Show only your plugin's log lines
grep "MyPlugin\|PASS\|FAIL\|error\|ERROR\|SUCCESS" build/ludo.log

# Show the most recent test run (last N lines)
tail -40 build/ludo.log

# Show curl request/response details (debug build only)
grep "\[curl\]" build/ludo.log | tail -20
```

### 8.5 Log Message Format

```
[HH:MM:SS] [LEVEL] message
```

Log levels:
- `[INFO]` — from `ludo.logInfo()` — general progress messages.
- `[SUCCESS]` — from `ludo.logSuccess()` — download queued successfully.
- `[ERROR]` — from `ludo.logError()` — failures and warnings.
- `[curl ...]` — debug-build only curl trace: request headers, response
  headers, DNS resolution, TLS handshake, connection reuse.
- `[add]` — download manager adding a new URL.
- `[perform_download]` — download starting with resolved filename.

### 8.6 Typical Workflow

```
1.  Write/edit plugins/myplugin.lua  (in the project root plugins/ dir)
2.  Copy to build:   cp plugins/myplugin.lua build/plugins/
3.  Write a test:     build/test_myplugin.lua
4.  Run:              cd build && ./ludo-debug.exe -s test_myplugin.lua
5.  Check output:     grep "MyPlugin\|PASS\|FAIL" ludo.log
6.  Iterate:          fix plugin → copy → re-run → check log
```

> **Tip:** Plugins in `build/plugins/` are copies.  Always edit the canonical
> source in the project root `plugins/` directory and copy to `build/plugins/`
> before testing.  The CMake build copies plugins during the configure step,
> but manual copies are faster during development.

### 8.7 Debugging Tips

- **Lua errors** are caught by `pcall()` in the test script and logged.
  If the script itself has a syntax error, `ludo-debug.exe` will print the
  Lua error to `ludo.log` and exit silently (exit code 0).

- **HTTP failures** — check `ludo.log` for `[curl]` lines showing the exact
  request URL, response status, and headers.  Common issues:
  - `403 Forbidden` — site requires cookies or specific headers.
  - `429 Too Many Requests` — rate-limited; wait before retrying.
  - `301/302` — check `http.get_last_url()` for the final redirect target.

- **JSON parse errors** — always wrap `json.decode()` in `pcall()`.
  Log the raw body (or a prefix of it) to see what the server actually returned:
  ```lua
  if not ok then
      ludo.logError("JSON error: " .. tostring(data))
      ludo.logError("Body prefix: " .. body:sub(1, 200))
  end
  ```

- **Cookie issues** — check that `http.set_cookie()` is called before any
  HTTP requests, and verify the cookie file exists and has the expected format
  using `http.read_cookie()`.

- **Pattern mismatches** — test patterns interactively:
  ```lua
  local test = "https://www.instagram.com/reel/ABC123/"
  local id = test:match("/reels?/([%w%-_]+)")
  ludo.logInfo("Matched: " .. tostring(id))  -- should print "ABC123"
  ```

---

## 5. Zip Library (`zip`)

The `zip` global provides a single function for creating standard ZIP archives
(PKZIP 2.0 / Info-ZIP compatible) using **zlib DEFLATE** compression.  No
external dependency beyond the vendored `zlib-1.2.8` is required.

### 5.1 `zip.create`

```lua
-- Pack an explicit list of files (entries use the basename only):
local status, errmsg = zip.create(output_path, { file1, file2, ... })

-- Pack an entire directory tree (recursive):
local status, errmsg = zip.create(output_path, directory)

-- Pack a directory tree, including only files matching a glob pattern:
local status, errmsg = zip.create(output_path, directory, glob_filter)
```

**Parameters**

| Parameter     | Type   | Description |
|---------------|--------|-------------|
| `output_path` | string | Destination `.zip` file path. Created or overwritten. |
| `{ files }`   | table  | Array of source file paths. Each file is stored using its **basename** only (no subdirectory). |
| `directory`   | string | Root directory to pack.  All files are included recursively; entry names preserve the relative path under `directory`. |
| `glob_filter` | string | Optional case-insensitive glob pattern (`*` = any sequence, `?` = any single char) applied to **basenames**.  Example: `"*.mp4"`. Omit or pass `nil`/`""` to include every file. |

**Return values**

| Value    | Type    | Meaning |
|----------|---------|---------|
| `status` | integer | `0` on success; `-1` on any failure. |
| `errmsg` | string  | Error description — present **only** when `status == -1`. |

### 5.2 Examples

```lua
-- 1. Pack two downloaded files into a single archive
local out = ludo.getOutputDirectory() .. "/bundle.zip"
local status, err = zip.create(out, {
    ludo.getOutputDirectory() .. "/video.mp4",
    ludo.getOutputDirectory() .. "/cover.jpg",
})
if status ~= 0 then
    ludo.logError("zip failed: " .. (err or "unknown error"))
else
    ludo.logSuccess("Created " .. out)
end

-- 2. Archive the entire downloads folder
local status, err = zip.create(
    ludo.getOutputDirectory() .. "/all.zip",
    ludo.getOutputDirectory()
)

-- 3. Archive only MP4 files from the downloads folder
local status, err = zip.create(
    ludo.getOutputDirectory() .. "/videos.zip",
    ludo.getOutputDirectory(),
    "*.mp4"
)
if status ~= 0 then
    ludo.logError("zip failed: " .. (err or "unknown"))
end
```

### 5.3 Notes

- Archives are written in **PKZIP 2.0** format; compatible with Windows
  Explorer, 7-Zip, unzip, and all standard ZIP tools.
- All files are compressed with **DEFLATE** (`Z_DEFAULT_COMPRESSION`).
  The original file is not modified.
- On **Windows** all paths are handled as **UTF-8** and converted to UTF-16
  internally for Win32 API calls — filenames with non-ASCII characters work
  correctly.
- Maximum entries per archive: **4096**.
- Maximum in-archive path length: **511 bytes**.
- ZIP64 (archives > 4 GB or containing files > 4 GB) is **not** supported.

