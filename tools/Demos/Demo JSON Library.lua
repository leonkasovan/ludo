-- JSON Library Demo — interactive explorer for json.encode, decode, and config
-- Run from Ludo: Tools > JSON Demo (or via -s tools/json_demo.lua)
-- Tools must NOT call ui.Main()/ui.Uninit() — use MainStep() instead.

local ui = require("ui")
local json = json or require("json")
local cjson_safe = cjson_safe or require("cjson_safe")
local win_open = true

local output_buf = {}

local function run_code(code)
    output_buf = {}
    local old_print = print
    print = function(...)
        local parts = {}
        for i = 1, select("#", ...) do
            parts[i] = tostring(select(i, ...))
        end
        output_buf[#output_buf + 1] = table.concat(parts, "\t")
    end
    local ok, err = pcall(load(code))
    print = old_print
    if not ok then
        output_buf[#output_buf + 1] = "Error: " .. tostring(err)
    end
    return table.concat(output_buf, "\n")
end

local function make_tab(initial_code)
    local box = ui.NewVerticalBox()
    box:SetPadded(1)
    local code_mle = ui.NewNonWrappingMultilineEntry()
    code_mle:SetText(initial_code)
    box:Append(code_mle, true)
    local run_btn = ui.NewButton("  Run  ")
    box:Append(run_btn, false)
    local output_mle = ui.NewMultilineEntry()
    output_mle:SetReadOnly(1)
    box:Append(output_mle, false)
    run_btn:OnClicked(function()
        output_mle:SetText(run_code(code_mle:Text()))
    end, nil)
    return box
end

-- ====================================================================
-- Tab examples
-- ====================================================================

local encode_examples = [[-- === json.encode() ===
-- Serialise Lua tables/values to JSON strings.

-- Basic types
print(json.encode(42))
print(json.encode(3.14))
print(json.encode(true))
print(json.encode(false))
print(json.encode(nil))
print(json.encode("hello \"world\""))

-- Arrays (consecutive integer keys starting at 1)
print(json.encode({ "a", "b", "c" }))

-- Objects (non-consecutive or string keys)
print(json.encode({ name = "Alice", age = 30 }))

-- Mixed
print(json.encode({
    id = 123,
    tags = { "lua", "json" },
    active = true,
    metadata = { count = 5 },
}))

-- json.null (JSON null in tables)
local t = { a = json.null, b = 42 }
print(json.encode(t))

-- Nested
local data = {
    users = {
        { name = "Alice", scores = { 90, 85 } },
        { name = "Bob",   scores = { 70 } },
    },
    total = 2,
}
print(json.encode(data))]]

local decode_examples = [[-- === json.decode() ===
-- Parse JSON strings into Lua values.
-- Throws on invalid input — use pcall() or cjson_safe.

-- Basic types
local ok, v = pcall(json.decode, "42")
print("number:", v)

v = json.decode('"hello"')
print("string:", v)

v = json.decode("true")
print("boolean:", v)

-- Arrays → Lua tables (1-indexed)
local arr = json.decode('[10, 20, 30]')
for i, v in ipairs(arr) do print("arr[" .. i .. "]", v) end

-- Objects
local obj = json.decode('{"name": "Alice", "age": 30}')
for k, v in pairs(obj) do print(k, v) end

-- Nested
local data = json.decode('{"users":[{"name":"Alice","scores":[90,85]}]}')
print(data.users[1].name)
print(data.users[1].scores[2])

-- null → json.null (lightuserdata)
local t = json.decode('[null, 1, "x"]')
print("t[1] == json.null:", t[1] == json.null)
print("t[2]:", t[2])
print("t[3]:", t[3])

-- Error handling with pcall
local ok, err = pcall(json.decode, "{invalid}")
if not ok then print("decode error:", err) end

-- Error handling with cjson_safe (returns nil, err instead of throwing)
local val, err = cjson_safe.decode("{invalid}")
print("safe decode:", val, err)

-- Empty / edge cases
print(json.decode("null") == json.null)
print(json.decode("[]"))    --> empty table
print(json.decode("{}"))    --> empty table]]

local config_examples = [[-- === json configuration functions ===
-- Each getter/setter returns the previous value.

-- Number precision (1-14, default 14)
print("old precision:", json.encode_number_precision(3))
print(json.encode(1.23456789))
json.encode_number_precision(14)

-- Max nesting depth
print("old encode depth:", json.encode_max_depth(50))
print("old decode depth:", json.decode_max_depth(50))

-- Invalid numbers: "off" (error), "on" (encode), "null" (replace)
print("old invalid setting:", json.encode_invalid_numbers("null"))
print(json.encode(1/0))      --> null (instead of error)
json.encode_invalid_numbers("off")

-- Decode invalid numbers: true (allow NaN/Inf), false (reject)
print("decode invalid:", json.decode_invalid_numbers(true))
local v = json.decode("NaN")
print("NaN:", v)
json.decode_invalid_numbers(false)

-- Keep buffer: reuse internal encode buffer
print("keep buffer:", json.encode_keep_buffer(true))

-- Sparse array handling
-- encode_sparse_array(convert, ratio, safe)
-- convert: true=convert to object, false=error
-- ratio: max index / actual items (default 2)
-- safe: always treat as array if max index <= safe (default 10)
print(json.encode_sparse_array(true, 2, 10))]]

local new_examples = [[-- === json.new() ===
-- Create independent module instances with separate config.

local j1 = json.new()
local j2 = json.new()

-- Each instance has its own settings
j1.encode_number_precision(3)
j2.encode_number_precision(6)

print("j1:", j1.encode(1.23456789))
print("j2:", j2.encode(1.23456789))
print("global:", json.encode(1.23456789))

-- j1 and j2 have their own null sentinel
print("j1.null == json.null:", j1.null == json.null)
print("j1.null == j2.null:", j1.null == j2.null)

-- cjson_safe.new() — safe independent instance
local safe = cjson_safe.new()
local val, err = safe.decode("{bad}")
print("safe.new decode:", val, err)]]

local safe_examples = [[-- === cjson_safe — safe JSON wrapper ===
-- encode/decode return nil, error instead of throwing.

-- Safe decode (no pcall needed)
local val, err = cjson_safe.decode('{"valid": "json"}')
if val then
    print("decoded:", val.valid)
else
    print("error:", err)
end

-- Safe encode
local val, err = cjson_safe.encode({ a = 1, b = 2 })
if val then
    print("encoded:", val)
else
    print("error:", err)
end

-- Error returns nil, err instead of throwing
local val, err = cjson_safe.decode("{bad}")
print("on bad input:", val, err)

-- All config functions work on cjson_safe too
cjson_safe.encode_number_precision(4)
print(cjson_safe.encode(math.pi))
cjson_safe.encode_number_precision(14)

-- null sentinel
print("cjson_safe.null == json.null:", cjson_safe.null == json.null)]]

-- ====================================================================
-- Build window
-- ====================================================================

local tabs = ui.NewTab()
tabs:Append("Encode",    make_tab(encode_examples))
tabs:Append("Decode",    make_tab(decode_examples))
tabs:Append("Config",    make_tab(config_examples))
tabs:Append("json.new",  make_tab(new_examples))
tabs:Append("cjson_safe", make_tab(safe_examples))

local win = ui.NewWindow("JSON Library Demo", 720, 620, false)
win:SetMargined(1)
win:SetChild(tabs)
win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
