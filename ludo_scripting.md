# Ludo Scripting Manual

Ludo embeds **Lua 5.2** as its scripting engine. Every plugin and script has
access to the full Lua standard library plus three extension libraries provided
by Ludo:

| Module | Global name | Description |
|--------|-------------|-------------|
| HTTP   | `http`      | HTTP client powered by libcurl |
| Ludo   | `ludo`      | Download manager & application control |
| UI     | `ui`        | Native GUI widgets (libui) |

Scripts are loaded from the `plugins/` directory. Each plugin is a `.lua` file
that returns a table with two functions: `validate(url)` and `process(url)`.

---

## Table of Contents

1. [Lua Language Basics](#1-lua-language-basics)
2. [Standard Lua Library](#2-standard-lua-library)
3. [HTTP Library (`http`)](#3-http-library)
4. [Ludo Library (`ludo`)](#4-ludo-library)
5. [UI Library (`ui`)](#5-ui-library)
6. [Plugin System](#6-plugin-system)

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
- `body` (string) — Response body.
- `status` (number) — HTTP status code.
- `headers` (table) — Response headers as key-value pairs.

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

---

## 4. Ludo Library

The `ludo` module provides download management and application logging. It is
registered as a global table.

### 4.1 `ludo.newDownload(url [, output_dir [, mode]])` → id

Enqueue a new download.

**Parameters:**
- `url` (string) — The URL to download.
- `output_dir` (string, optional) — Destination directory. Defaults to the
  application's configured output directory.
- `mode` (number, optional) — Download mode constant. Defaults to `ludo.DOWNLOAD_NOW`.

**Returns:**
- `id` (number) — The download ID for tracking.

**Constants:**
- `ludo.DOWNLOAD_NOW` — Start downloading immediately.
- `ludo.DOWNLOAD_QUEUE` — Add to queue, start when a slot is free.

```lua
-- Download immediately to default directory
local id = ludo.newDownload("https://example.com/file.zip")

-- Download to specific directory
local id = ludo.newDownload("https://example.com/file.zip", "C:\\Downloads")

-- Queue the download
local id = ludo.newDownload("https://example.com/file.zip", nil, ludo.DOWNLOAD_QUEUE)
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

### 4.7 `ludo.getOutputDirectory()` → string

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
    ludo.newDownload(url)
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

### Ludo Functions

| Function | Signature | Returns |
|----------|-----------|---------|
| `ludo.newDownload` | `(url [, dir [, mode]])` | `id` |
| `ludo.pauseDownload` | `(id)` | — |
| `ludo.removeDownload` | `(id)` | — |
| `ludo.logError` | `(msg)` | — |
| `ludo.logSuccess` | `(msg)` | — |
| `ludo.logInfo` | `(msg)` | — |
| `ludo.getOutputDirectory` | `()` | `dir` |

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
