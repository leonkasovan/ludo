-- json.decode(json_string) → lua_value
-- Parse JSON into Lua tables/values. Throws on invalid input.

local json = json or require("json")

-- Basic types
print(json.decode("42"))            --> 42
print(json.decode('"hello"'))       --> hello
print(json.decode("true"))          --> true
print(json.decode("null") == json.null)  --> true (lightuserdata)

-- Arrays → 1-indexed tables
local arr = json.decode("[10, 20, 30]")
for i, v in ipairs(arr) do print(i, v) end

-- Objects
local obj = json.decode('{"name": "Alice", "age": 30}')
print(obj.name, obj.age)

-- Nested
local data = json.decode('{"items":[{"id":1}]}')
print(data.items[1].id)

-- Safety: wrap in pcall
local ok, result = pcall(json.decode, "{bad}")
if not ok then
    print("Error:", result)
end
