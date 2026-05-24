-- json.encode(value) → json_string
-- Serialise a Lua value to JSON.

local json = json or require("json")

-- Basic types
print(json.encode(42))              --> 42
print(json.encode(3.14))            --> 3.14
print(json.encode(true))            --> true
print(json.encode("hello"))         --> "hello"
print(json.encode({a=1, b=2}))       --> {"a":1,"b":2}
print(json.encode({"a","b","c"}))   --> ["a","b","c"]

-- Nested tables
local data = {
    users = {
        { name = "Alice", scores = { 90, 85 } },
    },
    total = 1,
}
print(json.encode(data))

-- json.null to represent JSON null in tables
local t = { a = json.null, b = 42 }
print(json.encode(t))
