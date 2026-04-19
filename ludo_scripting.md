# Ludo Scripting Manual

Ludo embeds **Lua 5.2** as its scripting engine. Every plugin and script has
access to the full Lua standard library plus five extension libraries provided
by Ludo:

| Module | Global name | Description |
|--------|-------------|-------------|
| JSON   | `json`      | JSON encode/decode via Lua CJSON |
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
3. [JSON Library (`json`)](#3-json-library-json)
4. [HTTP Library (`http`)](#4-http-library-http)
5. [Zip Library (`zip`)](#5-zip-library-zip)
6. [Ludo Library (`ludo`)](#6-ludo-library-ludo)
7. [UI Library (`ui`)](#7-ui-library-ui)
8. [Plugin System](#8-plugin-system)
9. [Converting yt-dlp Extractors to Ludo Plugins](#9-converting-yt-dlp-extractors-to-ludo-plugins)
10. [Testing and Debugging Plugins](#10-testing-and-debugging-plugins)
11. [Tools Menu](#11-tools-menu)

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

## 3. JSON Library (`json`)

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

## 4. HTTP Library (`http`)

The `http` module is registered as a global table. It provides HTTP client
functionality backed by libcurl with cookie management, URL utilities, and
custom headers.

### 4.1 `http.get(url [, options])` → body, status, headers

Perform an HTTP GET request.

**Parameters:**
- `url` (string) — The URL to fetch.
- `options` (table, optional) — Request options (see [Options Table](#47-options-table)).

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

### 4.2 `http.head(url [, options])` → body, status, headers

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

### 4.3 `http.post(url, body [, options])` → body, status, headers

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

### 4.4 `http.set_cookie(filepath)`

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

### 4.5 `http.clear_cookies()`

Clear the cookie jar and last URL state.

```lua
http.clear_cookies()
```

### 4.6 `http.get_last_url()` → string

Get the last effective URL after all redirects from the most recent request.

**Returns:**
- `url` (string) — The final URL, or empty string if no request was made.

```lua
http.get("https://github.com/user/repo/releases/latest")
local final_url = http.get_last_url()
print(final_url)  --> https://github.com/user/repo/releases/tag/v1.2.3
```

### 4.7 Options Table

All HTTP request functions accept an optional `options` table:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `user_agent` | string | `"Mozilla/5.0 LUDO/1.0"` | User-Agent header |
| `follow_redirects` | boolean | `true` | Follow HTTP redirects (max 10) |
| `timeout` | number | none | Request timeout in seconds |
| `headers` | table | none | Custom headers as `{["Name"] = "value"}` |
| `cookies` | string | none | Path to cookie jar file |

### 4.8 `http.url_encode(str)` → string

URL-encode a string (percent-encoding).

**Parameters:**
- `str` (string) — The string to encode.

**Returns:**
- `encoded` (string) — URL-encoded string.

```lua
local encoded = http.url_encode("hello world & more")
print(encoded)  --> hello%20world%20%26%20more
```

### 4.9 `http.url_decode(str)` → string

Decode a URL-encoded string.

**Parameters:**
- `str` (string) — The URL-encoded string.

**Returns:**
- `decoded` (string) — Decoded string.

```lua
local decoded = http.url_decode("hello%20world%20%26%20more")
print(decoded)  --> hello world & more
```

### 4.10 `http.parse_url(url)` → table

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

### 4.11 Complete HTTP Example

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

### 4.12 `http.read_cookie(filepath, name)` → string|nil

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

### 4.13 `http.base64_encode(str)` → string

Base64-encode a string (binary-safe).

**Parameters:**
- `str` (string) — The string or binary data to encode.

**Returns:**
- `encoded` (string) — Base64-encoded string.

```lua
local b64 = http.base64_encode("hello world")
print(b64)  --> aGVsbG8gd29ybGQ=
```

### 4.14 `http.base64_decode(str)` → string

Decode a base64-encoded string (binary-safe).

**Parameters:**
- `str` (string) — The base64-encoded string.

**Returns:**
- `decoded` (string) — Decoded string or binary data.

```lua
local raw = http.base64_decode("aGVsbG8gd29ybGQ=")
print(raw)  --> hello world
```

### 4.15 `http.sha256(str)` → string

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
|----------|---------|----------|
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

---

## 6. Ludo Library (`ludo`)

The `ludo` module provides download management and application logging. It is
registered as a global table.

### 6.1 `ludo.newDownload(url [, output_dir [, mode [, filename [, headers]]]])` -> id, status, output_path

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

### 6.2 `ludo.pauseDownload(id)`

Pause an active download.

**Parameters:**
- `id` (number) — The download ID returned by `newDownload`.

```lua
local id = ludo.newDownload("https://example.com/large-file.zip")
-- Later...
ludo.pauseDownload(id)
```

### 6.3 `ludo.removeDownload(id)`

Remove a download from the manager (cancels if active).

**Parameters:**
- `id` (number) — The download ID returned by `newDownload`.

```lua
ludo.removeDownload(id)
```

### 6.4 `ludo.logError(msg)`

Log an error message to the application's Activity Log panel.

**Parameters:**
- `msg` (string) — The error message.

```lua
ludo.logError("Failed to parse response from API")
```

### 6.5 `ludo.logSuccess(msg)`

Log a success message to the Activity Log panel.

**Parameters:**
- `msg` (string) — The success message.

```lua
ludo.logSuccess("Plugin loaded successfully")
```

### 6.6 `ludo.logInfo(msg)`

Log an informational message to the Activity Log panel.

**Parameters:**
- `msg` (string) — The info message.

```lua
ludo.logInfo("Scanning page for download links...")
```

### 6.7 `ludo.setting`

Configuration values loaded from `config.ini` are exposed through `ludo.setting`.

```lua
print(ludo.setting.maxDownloadRetry)
print(ludo.setting.maxThread)
print(ludo.setting.outputDir)
```
### 6.8 `ludo.getOutputDirectory()` - string

Get the current default output directory.

**Returns:**
- `dir` (string) — The configured output directory path.

```lua
local dir = ludo.getOutputDirectory()
ludo.logInfo("Files will be saved to: " .. dir)
```

---

## 7. UI Library (`ui`)

The `ui` module provides native GUI widget creation via libui-ng. It is registered
as `require("ui")` or accessed via the global `ui` table. All widget
constructors return an object with chainable methods.

> **Note:** The Ludo main window uses its own UI. The `ui` library is intended
> for tool scripts and plugins that need to show their own dialog windows.
>
> **Tool scripts** must NOT call `ui.Init()`, `ui.Main()`, or `ui.Uninit()`.
> Use `ui.MainStep(true)` in a loop instead. See §11 for the correct pattern.

### 7.1 Application Lifecycle

#### `ui.Init()` → string|nil

Initialize the UI subsystem. Returns `nil` on success or an error string.
(Tool scripts can ignore the error — it is expected when Ludo is already running.)

#### `ui.Uninit()`

Shut down the UI subsystem (standalone scripts only).

#### `ui.Main()`

Run the main event loop (standalone scripts only). Blocks until `ui.Quit()`.

#### `ui.MainStep(wait)` → number

Process one iteration of the event loop.

- `wait` (boolean) — Block until an event occurs if `true`.
- Returns non-zero to continue, `0` to stop.

```lua
-- Tool-script event loop (preferred pattern)
local win_open = true
win:OnClosing(function(w) win_open = false; return 1 end, nil)
win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
```

#### `ui.Quit()`

Request the event loop to exit.

---

### 7.2 Control Base Methods

These functions operate on **any** control, regardless of type:

| Function | Description |
|----------|-------------|
| `ui.ControlShow(ctrl)` | Show the control |
| `ui.ControlHide(ctrl)` | Hide the control |
| `ui.ControlEnable(ctrl)` | Enable user interaction |
| `ui.ControlDisable(ctrl)` | Disable user interaction |
| `ui.ControlVisible(ctrl)` → bool | Whether the control is visible |
| `ui.ControlEnabled(ctrl)` → bool | Whether the control is enabled |
| `ui.ControlDestroy(ctrl)` | Destroy the control and free resources |

Additionally, `Window` objects expose `Show`, `Hide`, `Enable`, `Disable`,
`Visible`, `Enabled`, and `Destroy` as methods directly.

---

### 7.3 Window

#### `ui.NewWindow(title, width, height, hasMenubar)` → Window

Create a new top-level window.

```lua
local win = ui.NewWindow("My Tool", 640, 480, false)
win:SetMargined(1)
win:Show()
```

#### Methods

| Method | Description |
|--------|-------------|
| `win:Title()` → string | Get the window title |
| `win:SetTitle(s)` | Set the window title |
| `win:Position()` → x, y | Get window position (pixels) |
| `win:SetPosition(x, y)` | Move window |
| `win:ContentSize()` → w, h | Get client area size (pixels) |
| `win:SetContentSize(w, h)` | Resize client area |
| `win:Fullscreen()` → bool | Whether window is fullscreen |
| `win:SetFullscreen(bool)` | Set fullscreen state |
| `win:Borderless()` → bool | Whether window has no border/titlebar |
| `win:SetBorderless(bool)` | Toggle borderless mode |
| `win:Resizeable()` → bool | Whether the window can be resized |
| `win:SetResizeable(bool)` | Set resizeability |
| `win:Focused()` → bool | Whether the window has keyboard focus |
| `win:Margined()` → bool | Margin state |
| `win:SetMargined(n)` | Enable/disable margins (1 or 0) |
| `win:SetChild(ctrl)` | Set child control (content) |
| `win:OnClosing(fn, data)` | Callback fired when user closes; return 1 to allow close |
| `win:Show()` | Show the window |
| `win:Hide()` | Hide the window |
| `win:Enable()` / `win:Disable()` | Enable/disable |
| `win:Visible()` / `win:Enabled()` | Query state |
| `win:Destroy()` | Destroy the window and free resources |

> **Programmatic close:** `win:Close()` does **not** exist in libuilua.
> To close a window from code (e.g. a button callback), set `win_open = false`
> and call `win:Destroy()`. `win:OnClosing` only fires when the user clicks
> the title-bar X button — it is **not** triggered by `win:Destroy()`.

```lua
-- Close handler (important for tool scripts)
win:OnClosing(function(w, data)
    win_open = false
    return 1  -- 1 = allow close; 0 = prevent close
end, nil)
```

---

### 7.4 Box (Layout Container)

#### `ui.NewVerticalBox()` → Box
#### `ui.NewHorizontalBox()` → Box

```lua
local vbox = ui.NewVerticalBox()
vbox:SetPadded(1)
```

#### Methods

| Method | Description |
|--------|-------------|
| `box:Append(child [, ...] [, stretchy])` | Add children; optional stretchy bool |
| `box:Delete(index)` | Remove child at 0-based index |
| `box:NumChildren()` → number | Number of children |
| `box:Padded()` → number | Get padding state |
| `box:SetPadded(n)` | Enable/disable padding |

```lua
vbox:Append(ui.NewLabel("Name:"), false)
vbox:Append(entry, true)  -- entry stretches
```

---

### 7.5 Button

#### `ui.NewButton(text)` → Button

```lua
local btn = ui.NewButton("Download")
btn:OnClicked(function(b, data)
    ludo.logInfo("Clicked!")
end, nil)
```

#### Methods

| Method | Description |
|--------|-------------|
| `btn:Text()` → string | Get button label |
| `btn:SetText(s)` | Set button label |
| `btn:OnClicked(fn, data)` | Click handler: `fn(button, data)` |

---

### 7.6 Label

#### `ui.NewLabel(text)` → Label

```lua
local lbl = ui.NewLabel("Status: ready")
```

| Method | Description |
|--------|-------------|
| `lbl:Text()` → string | Get label text |
| `lbl:SetText(s)` | Set label text |

---

### 7.7 Checkbox

#### `ui.NewCheckbox(text)` → Checkbox

```lua
local cb = ui.NewCheckbox("Enable logging")
```

| Method | Description |
|--------|-------------|
| `cb:SetText(s)` | Set label |
| `cb:Checked()` → number | `1` if checked, `0` otherwise |
| `cb:SetChecked(n)` | Set checked state (1/0) |
| `cb:OnToggled(fn, data)` | Toggle handler: `fn(checkbox, data)` |

```lua
if cb:Checked() == 1 then
    ludo.logInfo("Logging enabled")
end
```

---

### 7.8 Entry (Text Input)

#### `ui.NewEntry()` → Entry
#### `ui.NewPasswordEntry()` → Entry  *(text is masked)*
#### `ui.NewSearchEntry()` → Entry    *(shows search decoration)*

```lua
local url_entry = ui.NewEntry()
url_entry:SetText("https://example.com")
```

| Method | Description |
|--------|-------------|
| `entry:Text()` → string | Get text content |
| `entry:SetText(s)` | Set text content |
| `entry:ReadOnly()` → bool | Whether entry is read-only |
| `entry:SetReadOnly(bool)` | Set read-only state |
| `entry:OnChanged(fn, data)` | Change handler: `fn(entry, data)` |

---

### 7.9 MultilineEntry (Text Area)

#### `ui.NewMultilineEntry()` → MultilineEntry         *(wrapping)*
#### `ui.NewNonWrappingMultilineEntry()` → MultilineEntry *(no wrap)*

```lua
local log = ui.NewMultilineEntry()
log:SetReadOnly(1)
```

| Method | Description |
|--------|-------------|
| `mle:Text()` → string | Get all text |
| `mle:SetText(s)` | Replace all text |
| `mle:Append(s)` | Append text at end |
| `mle:ReadOnly()` → number | `1` if read-only |
| `mle:SetReadOnly(n)` | Set read-only (1/0) |
| `mle:OnChanged(fn, data)` | Change handler: `fn(mle, data)` |

---

### 7.10 ProgressBar

#### `ui.NewProgressBar()` → ProgressBar

| Method | Description |
|--------|-------------|
| `pb:Value()` → number | Get current value |
| `pb:SetValue(n)` | Set value (0–100); `-1` = indeterminate |

```lua
pb:SetValue(-1)   -- pulsing
pb:SetValue(75)
```

---

### 7.11 Slider

#### `ui.NewSlider(min, max)` → Slider

| Method | Description |
|--------|-------------|
| `s:Value()` → number | Current value |
| `s:SetValue(n)` | Set value |
| `s:SetRange(min, max)` | Change range |
| `s:HasToolTip()` → bool | Whether tooltip is shown |
| `s:SetHasToolTip(bool)` | Show/hide tooltip |
| `s:OnChanged(fn, data)` | Change handler (while dragging) |
| `s:OnReleased(fn, data)` | Released handler (on mouse-up) |

---

### 7.12 Spinbox

#### `ui.NewSpinbox(min, max)` → Spinbox

| Method | Description |
|--------|-------------|
| `sp:Value()` → number | Current integer value |
| `sp:SetValue(n)` | Set value |
| `sp:OnChanged(fn, data)` | Change handler: `fn(spinbox, data)` |

---

### 7.13 Combobox (Dropdown)

#### `ui.NewCombobox()` → Combobox  *(read-only dropdown)*

| Method | Description |
|--------|-------------|
| `cb:Append(s [, ...])` | Add items |
| `cb:InsertAt(index, s)` | Insert item at 0-based index |
| `cb:Delete(index)` | Remove item at 0-based index |
| `cb:Clear()` | Remove all items |
| `cb:NumItems()` → number | Item count |
| `cb:Selected()` → number | 0-based selected index (−1 = none) |
| `cb:SetSelected(n)` | Select by 0-based index |
| `cb:OnToggled(fn, data)` | Selection change handler |

```lua
local combo = ui.NewCombobox()
combo:Append("Option A", "Option B", "Option C")
combo:SetSelected(0)
combo:OnToggled(function(c, d)
    print("Selected: " .. c:Selected())
end, nil)
```

---

### 7.14 EditableCombobox

#### `ui.NewEditableCombobox()` → EditableCombobox  *(dropdown + free text)*

| Method | Description |
|--------|-------------|
| `ec:Append(s [, ...])` | Add preset items |
| `ec:Text()` → string | Get current text (typed or selected) |
| `ec:SetText(s)` | Set text |
| `ec:OnChanged(fn, data)` | Change handler: `fn(ec, data)` |

---

### 7.15 RadioButtons

#### `ui.NewRadioButtons()` → RadioButtons

| Method | Description |
|--------|-------------|
| `rb:Append(s [, ...])` | Add items |
| `rb:Selected()` → number | 0-based selected index (−1 = none) |
| `rb:SetSelected(n)` | Select by 0-based index |
| `rb:OnSelected(fn, data)` | Selection change handler |

```lua
local rb = ui.NewRadioButtons()
rb:Append("Small", "Medium", "Large")
rb:SetSelected(1)  -- Medium
```

---

### 7.16 Group

#### `ui.NewGroup(title)` → Group

| Method | Description |
|--------|-------------|
| `g:Title()` → string | Get title |
| `g:SetTitle(s)` | Set title |
| `g:SetChild(ctrl)` | Set single child control |
| `g:Margined()` → number | Get margin state |
| `g:SetMargined(n)` | Enable/disable margins |

```lua
local grp = ui.NewGroup("Options")
grp:SetMargined(1)
grp:SetChild(ui.NewVerticalBox())
```

---

### 7.17 Tab

#### `ui.NewTab()` → Tab

| Method | Description |
|--------|-------------|
| `t:Append(name, ctrl [, ...])` | Add tab(s) with label and content |
| `t:InsertAt(name, index, ctrl)` | Insert tab at 0-based index |
| `t:Delete(index)` | Remove tab at 0-based index |
| `t:NumPages()` → number | Number of tabs |
| `t:Selected()` → number | 0-based active tab index |
| `t:SetSelected(n)` | Switch to tab by index |
| `t:Margined(index)` → bool | Whether tab page has margins |
| `t:SetMargined(index, bool)` | Set margin for tab page |

```lua
local tab = ui.NewTab()
tab:Append("General", genBox, "Advanced", advBox)
tab:SetSelected(0)
```

---

### 7.18 Separator

#### `ui.NewHorizontalSeparator()` → Separator
#### `ui.NewVerticalSeparator()` → Separator

```lua
vbox:Append(ui.NewHorizontalSeparator())
hbox:Append(ui.NewVerticalSeparator())
```

---

### 7.19 Date/Time Pickers

#### `ui.NewDateTimePicker()` → DateTimePicker
#### `ui.NewDatePicker()` → DateTimePicker
#### `ui.NewTimePicker()` → DateTimePicker

| Method | Description |
|--------|-------------|
| `dt:Time()` → table | Get time as `{year, month, day, hour, min, sec, wday}` |
| `dt:SetTime(t)` | Set time from table with same fields |
| `dt:OnChanged(fn, data)` | Change handler: `fn(dt, data)` |

`month` is 1–12, `wday` is 0–6 (0 = Sunday).

```lua
local d = ui.NewDatePicker()
local t = d:Time()
print(t.year .. "-" .. t.month .. "-" .. t.day)
```

---

### 7.20 ColorButton

#### `ui.NewColorButton()` → ColorButton

| Method | Description |
|--------|-------------|
| `cb:Color()` → r, g, b, a | Get color as four floats (0–1) |
| `cb:SetColor(r, g, b, a)` | Set color (floats 0–1) |
| `cb:OnChanged(fn, data)` | Change handler: `fn(colorbutton, data)` |

```lua
local cb = ui.NewColorButton()
cb:SetColor(1.0, 0.0, 0.0, 1.0)  -- red
local r, g, b, a = cb:Color()
```

---

### 7.21 Form (Labeled Layout)

#### `ui.NewForm()` → Form

Form lays out `label: control` pairs vertically.

| Method | Description |
|--------|-------------|
| `f:Append(label, ctrl, stretchy)` | Add a labeled control row |
| `f:NumChildren()` → number | Number of rows |
| `f:Delete(index)` | Remove row at 0-based index |
| `f:Padded()` → bool | Get padding state |
| `f:SetPadded(bool)` | Enable/disable padding |

```lua
local form = ui.NewForm()
form:SetPadded(true)
form:Append("URL:", ui.NewEntry(), false)
form:Append("Output:", ui.NewEntry(), false)
form:Append("Log:", ui.NewMultilineEntry(), true)
```

---

### 7.22 Grid (2D Layout)

#### `ui.NewGrid()` → Grid

Grid lays out controls on a 2-D cell grid.

| Method | Description |
|--------|-------------|
| `g:Append(ctrl, left, top [, xspan, yspan, hexpand, halign, vexpand, valign])` | Add a control |
| `g:Padded()` → bool | Get padding state |
| `g:SetPadded(bool)` | Enable/disable padding |

**Align constants:** `ui.AlignFill` (0), `ui.AlignStart` (1), `ui.AlignCenter` (2), `ui.AlignEnd` (3)

```lua
local grid = ui.NewGrid()
grid:SetPadded(true)
-- left, top, xspan, yspan, hexpand, halign, vexpand, valign
grid:Append(ui.NewLabel("Name:"), 0, 0, 1, 1, false, ui.AlignEnd, false, ui.AlignFill)
grid:Append(ui.NewEntry(),        1, 0, 1, 1, true,  ui.AlignFill, false, ui.AlignFill)
grid:Append(ui.NewLabel("URL:"),  0, 1, 1, 1, false, ui.AlignEnd, false, ui.AlignFill)
grid:Append(ui.NewEntry(),        1, 1, 1, 1, true,  ui.AlignFill, false, ui.AlignFill)
```

---

### 7.23 Area (Custom Drawing)

#### `ui.NewArea()` → Area

| Method | Description |
|--------|-------------|
| `area:SetSize(w, h)` | Set drawing area size |

---

### 7.24 Table

Tables display data from a `TableModel` using a model-view pattern.

#### Step 1 — Create a model handler table

```lua
local data = {
    { "Alice", 25 },
    { "Bob",   30 },
}

local handler = {
    NumColumns = function(self, model) return 2 end,
    ColumnType = function(self, model, col)
        return col == 0 and ui.TableValueTypeString or ui.TableValueTypeInt
    end,
    NumRows    = function(self, model) return #data end,
    CellValue  = function(self, model, row, col)
        return col == 0 and data[row+1][1] or data[row+1][2]
    end,
    SetCellValue = function(self, model, row, col, val)
        -- called when user edits a cell (if editable column)
        data[row+1][col+1] = val
    end,
}
```

#### Step 2 — Create the TableModel

```lua
local model = ui.NewTableModel(handler)
```

#### Step 3 — Create the Table view

```lua
-- ui.NewTable(model [, rowBackgroundColorModelColumn])
local tbl = ui.NewTable(model)
```

#### Step 4 — Append columns

```lua
-- AppendTextColumn(name, textModelCol, editableModelCol [, colorModelCol])
tbl:AppendTextColumn("Name", 0, ui.TableModelColumnNeverEditable)
-- AppendTextColumn with always-editable
tbl:AppendTextColumn("Age",  1, ui.TableModelColumnAlwaysEditable)
```

#### Table methods

| Method | Description |
|--------|-------------|
| `AppendTextColumn(name, textCol, editCol [, colorCol])` | Add text column |
| `AppendCheckboxColumn(name, checkCol, editCol)` | Add checkbox column |
| `AppendCheckboxTextColumn(name, checkCol, checkEditCol, textCol, textEditCol [, colorCol])` | Combined |
| `AppendProgressBarColumn(name, progressCol)` | Add progress bar column |
| `AppendButtonColumn(name, textCol, clickableCol)` | Add button column |
| `HeaderVisible()` → bool | Header visibility |
| `HeaderSetVisible(bool)` | Show/hide header |
| `GetSelectionMode()` → number | Current selection mode |
| `SetSelectionMode(mode)` | Set selection mode |
| `GetSelection()` → table | Array of 0-based selected row indices |
| `SetSelection(t)` | Set selected rows from index array |
| `ColumnWidth(col)` → number | Column width in pixels |
| `ColumnSetWidth(col, px)` | Set column width (−1 = auto) |
| `OnRowClicked(fn)` | `fn(row)` on single click |
| `OnRowDoubleClicked(fn)` | `fn(row)` on double click |
| `OnSelectionChanged(fn)` | `fn()` when selection changes |

**Selection mode constants:** `ui.TableSelectionModeNone`, `ui.TableSelectionModeZeroOrOne` (default),
`ui.TableSelectionModeOne`, `ui.TableSelectionModeZeroOrMany`

**TableValue type constants:** `ui.TableValueTypeString` (0), `ui.TableValueTypeInt` (2), `ui.TableValueTypeColor` (3)

**Editability constants:** `ui.TableModelColumnNeverEditable` (−1), `ui.TableModelColumnAlwaysEditable` (−2)

#### Notify the model of data changes

```lua
model:RowInserted(newIndex)   -- after inserting a row
model:RowChanged(index)       -- after modifying a row
model:RowDeleted(oldIndex)    -- after deleting a row
```

#### CellValue return types

| Lua type | Becomes |
|----------|---------|
| string | `uiTableValueTypeString` |
| integer | `uiTableValueTypeInt` |
| boolean | `uiTableValueTypeInt` (true→1, false→0) |
| `{r=, g=, b=, a=}` table | `uiTableValueTypeColor` |
| nil | NULL (accepted by some column types) |

#### Full Table example

```lua
local items = { "Apple", "Banana", "Cherry" }
local checks = { false, true, false }

local handler = {
    NumColumns   = function(s, m) return 2 end,
    ColumnType   = function(s, m, col)
        return col == 0 and ui.TableValueTypeString or ui.TableValueTypeInt
    end,
    NumRows      = function(s, m) return #items end,
    CellValue    = function(s, m, row, col)
        if col == 0 then return items[row+1]
        else return checks[row+1] and 1 or 0 end
    end,
    SetCellValue = function(s, m, row, col, val)
        if col == 1 then checks[row+1] = (val == 1) end
    end,
}

local model = ui.NewTableModel(handler)
local tbl   = ui.NewTable(model)
tbl:AppendTextColumn("Fruit", 0, ui.TableModelColumnNeverEditable)
tbl:AppendCheckboxColumn("Pick", 1, ui.TableModelColumnAlwaysEditable)
tbl:SetSelectionMode(ui.TableSelectionModeZeroOrMany)
tbl:OnRowClicked(function(row)
    ludo.logInfo("Clicked row " .. row)
end)
```

---

### 7.25 Dialogs

These functions display native OS dialogs. All require a parent `Window`.

| Function | Description |
|----------|-------------|
| `ui.OpenFile(win)` → string\|nil | Show "Open File" dialog; returns path or nil |
| `ui.OpenFolder(win)` → string\|nil | Show "Browse Folder" dialog |
| `ui.SaveFile(win)` → string\|nil | Show "Save File" dialog |
| `ui.MsgBox(win, title, desc)` | Show information message box |
| `ui.MsgBoxError(win, title, desc)` | Show error message box |

```lua
local path = ui.OpenFile(win)
if path then
    ludo.logInfo("Selected: " .. path)
end

local folder = ui.SaveFile(win)

ui.MsgBox(win, "Done", "Download complete!")
ui.MsgBoxError(win, "Error", "Failed to connect.")
```

---

### 7.26 Complete Tool Script Example

```lua
-- Example tool script (tools/my_tool.lua)
-- Does NOT call ui.Init()/ui.Main()/ui.Uninit()

local win = ui.NewWindow("My Tool", 500, 350, false)
win:SetMargined(1)

local vbox = ui.NewVerticalBox()
vbox:SetPadded(1)

-- Form layout
local form = ui.NewForm()
form:SetPadded(true)
local url_entry = ui.NewEntry()
form:Append("URL:", url_entry, false)
vbox:Append(form, false)

vbox:Append(ui.NewHorizontalSeparator(), false)

-- Log area
local log = ui.NewMultilineEntry()
log:SetReadOnly(1)
vbox:Append(log, true)

-- Buttons
local hbox = ui.NewHorizontalBox()
hbox:SetPadded(1)
local go_btn = ui.NewButton("Download")
go_btn:OnClicked(function(b, d)
    local url = url_entry:Text()
    if url == "" then
        ui.MsgBoxError(win, "Error", "Please enter a URL.")
        return
    end
    local id, status, path = ludo.newDownload(url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
    log:Append("Queued: " .. url .. " (" .. tostring(status) .. ")\n")
end, nil)
hbox:Append(go_btn, true)
vbox:Append(hbox, false)

win:SetChild(vbox)

-- Tool-script event loop
local win_open = true
win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)
win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
```

---

### 7.27 Table Tool Example

```lua
-- tools/download_list.lua
-- Shows a Table of completed downloads

local downloads = {}
-- populate from ludo API if available...

local handler = {
    NumColumns = function(s, m) return 3 end,
    ColumnType = function(s, m, col)
        if col == 2 then return ui.TableValueTypeInt end
        return ui.TableValueTypeString
    end,
    NumRows    = function(s, m) return #downloads end,
    CellValue  = function(s, m, row, col)
        local d = downloads[row+1]
        if col == 0 then return d.name
        elseif col == 1 then return d.url
        else return d.progress end
    end,
    SetCellValue = function(s, m, row, col, val) end,
}

local model = ui.NewTableModel(handler)
local tbl   = ui.NewTable(model)
tbl:AppendTextColumn("Filename", 0, ui.TableModelColumnNeverEditable)
tbl:AppendTextColumn("URL",      1, ui.TableModelColumnNeverEditable)
tbl:AppendProgressBarColumn("Progress", 2)
tbl:ColumnSetWidth(0, 200)

local win = ui.NewWindow("Downloads", 700, 400, false)
win:SetMargined(1)
win:SetChild(tbl)

local win_open = true
win:OnClosing(function(w, d) win_open = false; return 1 end, nil)
win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
```

---

## 8. Plugin System

### 8.1 Plugin Structure

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

### 8.2 Plugin Lifecycle

1. **Load** — On startup (or when the user selects a plugin directory), Ludo
   scans for `*.lua` files and verifies each has `validate` and `process`
   functions.
2. **Validate** — When a URL is submitted, each plugin's `validate(url)` is
   called in order. The first plugin that returns `true` wins.
3. **Process** — The matching plugin's `process(url)` is called. It should call
   `ludo.newDownload()` to enqueue files.
4. **Fallback** — If no plugin matches, Ludo downloads the URL directly.

### 8.3 Example: GitHub Release Plugin

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

### 8.4 Example: Direct File Plugin

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

### 8.5 Example: Batch URL Plugin

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

### 8.6 Example: Plugin with UI Dialog

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

## 9. Converting yt-dlp Extractors to Ludo Plugins

[yt-dlp](https://github.com/yt-dlp/yt-dlp) is a Python video downloader whose
*extractors* (one per site) are the de-facto reference for scraping media from
social-media and video-hosting sites.  A Ludo plugin serves the same purpose
but is written in Lua.  This section provides a systematic translation guide.

### 9.1 Architecture Mapping

| yt-dlp concept | Ludo equivalent | Notes |
|----------------|-----------------|-------|
| `InfoExtractor` class | `plugin` table returned by the `.lua` file | One class → one file |
| `_VALID_URL` (regex) | `plugin.validate(url)` (Lua patterns) | See [Python→Lua pattern table](#95-regex-to-lua-pattern-cheat-sheet) |
| `_real_extract(self, url)` | `plugin.process(url)` | Must call `ludo.newDownload()` |
| `self._download_webpage(url, ...)` | `http.get(url, opts)` | Returns `body, status, headers` |
| `self._download_json(url, ...)` | `http.get()` + `json.decode()` | Combine two calls |
| `self._search_regex(pattern, ...)` | `string.match(text, pattern)` | Translate regex to Lua pattern |
| `self._search_json(pattern, ...)` | `extract_json_object(text, pattern)` | Write a JSON brace-matcher (see §9.6) |
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
| `hashlib.md5(s).hexdigest()` | (not built-in) | See §9.7 |
| `itertools.count(1)` | `for page = 1, math.huge do ... end` | |

### 9.2 Step-by-Step Conversion Process

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

### 9.3 Common Python → Lua Translations

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

### 9.4 Plugin Template (Site Extractor)

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

### 9.5 Regex to Lua Pattern Cheat Sheet

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

### 9.6 Extracting JSON from HTML

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

### 9.7 Things yt-dlp Has That Ludo Does Not

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

### 9.8 Real-World Example: Instagram

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

## 10. Testing and Debugging Plugins

### 10.1 Prerequisites

Build Ludo in Debug configuration to get `ludo-debug.exe`:

```bash
cd build
cmake --build . --config Debug
```

The debug build writes verbose curl logs and Lua output to `ludo.log` in the
current working directory.

### 10.2 Running a Plugin Script

Use the `-s` (or `--script`) flag to execute a standalone Lua script:

```bash
cd build
./ludo-debug.exe -s test_myplugin.lua
```

The `-s` flag runs the script inside the full Ludo Lua environment — all
built-in modules (`http`, `ludo`, `json`, `ui`) are available.

### 10.3 Writing a Test Script

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

### 10.4 Checking the Log

After running the script, inspect `ludo.log` in the build directory:

```bash
# Show only your plugin's log lines
grep "MyPlugin\|PASS\|FAIL\|error\|ERROR\|SUCCESS" build/ludo.log

# Show the most recent test run (last N lines)
tail -40 build/ludo.log

# Show curl request/response details (debug build only)
grep "\[curl\]" build/ludo.log | tail -20
```

### 10.5 Log Message Format

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

### 10.6 Typical Workflow

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

### 10.7 Debugging Tips

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

## 11. Tools Menu

The **Tools** menu automatically lists every `.lua` file found in the `tools/`
directory (next to the executable). Clicking a tool entry runs that script in a
fresh Lua state with the full Ludo API available (`http`, `ludo`, `ui`, `zip`,
`json`).

### 11.1 Tool Script Discovery

On startup, Ludo scans the `tools/` directory and adds one menu item per
`.lua` file. The menu label is the filename without the `.lua` extension.

```
tools/
  PlayStation Games Downloader.lua   → Tools → PlayStation Games Downloader
  my_tool.lua                        → Tools → my_tool
```

### 11.2 Tool Script Environment

Tool scripts run inside the already-running Ludo process via `lua_engine_run_script`:

- A **fresh Lua state** is created for each run — globals do not persist
  between runs.
- `lualib/` directories is prepended to `package.path` so `require("ftcsv")` finds `lualib/ftcsv.lua`.
- All Ludo APIs are available: `http`, `ludo`, `ui`, `zip`, `json`.
- Unlike plugins, tool scripts do **not** need `validate`/`process` functions —
  they are executed as top-level scripts.
- **Do NOT call `ui.Init()`, `ui.Uninit()`, or `ui.Main()`** — Ludo's UI is
  already initialised. Use a `ui.MainStep()` loop instead (see §11.3).

### 11.3 Shared Lua Libraries (`lualib/`)

The `lualib/` directory contains Lua modules available to **all** scripts (tool
scripts and plugins) via `require()`. The directory is prepended to
`package.path` when any Lua state is created.

```
lualib/
  ftcsv.lua    -- CSV/TSV parser (ftcsv 1.5.0)
```

**Usage:**
```lua
local ftcsv = require("ftcsv")

-- Parse a TSV file with named-header columns
local ok, data = pcall(ftcsv.parse, "path/to/file.tsv", "\t")
if ok then
    for _, row in ipairs(data) do
        local name = row["Name"]      -- access by header name
        local size = row["File Size"]
        -- ...
    end
end
```

**`ftcsv` API summary:**

| Function | Description |
|----------|-------------|
| `ftcsv.parse(file, delim [, opts])` | Parse entire file; returns `rows, headers` |
| `ftcsv.parseLine(file, delim [, opts])` | Iterator; returns one row at a time |
| `ftcsv.encode(table, delim [, opts])` | Encode table array to CSV string |

Common options: `{ headers=true, loadFromString=false, rename={...}, fieldsToKeep={...} }`

### 11.4 Tool Script Template

```lua
-- tools/my_tool.lua
local ui = require("ui")

-- Do NOT call ui.Init() — Ludo already called it.

local win = ui.NewWindow("My Tool", 400, 300, false)
win:SetMargined(1)

local vbox = ui.NewVerticalBox()
vbox:SetPadded(1)
vbox:Append(ui.NewLabel("Hello from My Tool!"))

local win_open = true
local btn = ui.NewButton("Close")
btn:OnClicked(function(b, data)
    -- Programmatic close: win:Close() does NOT exist in libuilua.
    -- Set win_open=false to exit the MainStep loop, then destroy the window.
    win_open = false
    win:Destroy()
end, nil)
vbox:Append(btn)
win:SetChild(vbox)

-- User-initiated close (title-bar X button): allow close, exit the loop.
-- win:OnClosing is NOT triggered by win:Destroy() — only by the X button.
win:OnClosing(function(w, data)
    win_open = false
    return 1  -- 1 = allow close; 0 = prevent close
end, nil)
win:Show()

-- Drive a nested event loop until the window is closed.
-- Do NOT call ui.Main() or ui.Uninit().
while win_open do
    if ui.MainStep(true) == 0 then break end
end
```

### 11.5 Table-Based Tool Example (PlayStation Games Downloader)

The built-in **PlayStation Games Downloader** (`tools/PlayStation Games
Downloader.lua`) demonstrates the model-view Table pattern in a tool script:

```lua
local ui    = require("ui")
local ftcsv = require("ftcsv")  -- from lualib/ftcsv.lua

-- Build the table model backed by a Lua results array
local search_results = {}

local handler = {
    NumColumns  = function(m) return 6 end,
    ColumnType  = function(m, col) return ui.TableValueTypeString end,
    NumRows     = function(m) return #search_results end,
    CellValue   = function(m, row, col)
        local r = search_results[row + 1]  -- 0-based → 1-based
        if not r then return "" end
        -- return fields by column index...
        if col == 0 then return tostring(row + 1)  -- "#"
        elseif col == 1 then return r.platform
        elseif col == 2 then return r.id
        -- ...
        end
        return ""
    end,
    SetCellValue = function(m, row, col, val) end,  -- read-only
}
local model = ui.NewTableModel(handler)

-- Create and configure the Table widget
local tbl = ui.NewTable(model)
tbl:AppendTextColumn("#",        0, ui.TableModelColumnNeverEditable)
tbl:AppendTextColumn("Platform", 1, ui.TableModelColumnNeverEditable)
tbl:AppendTextColumn("Title ID", 2, ui.TableModelColumnNeverEditable)
tbl:SetSelectionMode(ui.TableSelectionModeZeroOrOne)
tbl:ColumnSetWidth(0, 40)

-- React to row selection
tbl:OnSelectionChanged(function()
    local sel = tbl:GetSelection()   -- returns {0-based index, ...}
    if sel and #sel > 0 then
        local r = search_results[sel[1] + 1]
        -- update detail labels from r ...
    end
end)

-- After replacing search_results: notify the model
-- Step 1 — delete old rows from last to first
for i = old_count, 1, -1 do
    table.remove(search_results, i)
    model:RowDeleted(i - 1)
end
-- Step 2 — add new data and notify insertions
for _, hit in ipairs(new_data) do
    table.insert(search_results, hit)
end
for i = 1, #search_results do
    model:RowInserted(i - 1)
end
```

