-- Base library — global functions like type(), print(), pcall(), etc.

-- Type checking
print(type(42))          --> number
print(type("hi"))        --> string
print(type({}))          --> table
print(type(nil))         --> nil
print(type(print))       --> function

-- Conversion
print(tostring(123))     --> "123"
print(tonumber("42"))    --> 42
print(tonumber("ff",16)) --> 255

-- Iteration
local t = { a = 1, b = 2, c = 3 }
for k, v in pairs(t) do print(k, v) end   -- all keys
local a = { "x", "y", "z" }
for i, v in ipairs(a) do print(i, v) end  -- numeric keys

-- Error handling
local ok, err = pcall(function() error("fail") end)
print(ok, err)           --> false  ...: fail

local ok, err = xpcall(
    function() error("fail") end,
    function(e) return "caught: " .. e end)
print(ok, err)           --> false  caught: ...

-- Selective arguments
local function many(a, b, c, d, e)
    return select(3, ...)
end
print(many(1,2,3,4,5))   --> 3  4  5

-- Metatables
local t = {}
local mt = { __tostring = function() return "mytable" end }
setmetatable(t, mt)
print(getmetatable(t) == mt) --> true

-- Raw access (bypasses metamethods)
rawset(t, "key", 42)
print(rawget(t, "key"))  --> 42
print(rawequal(t, t))    --> true
print(rawlen({1,2,3}))   --> 3

-- Assertion
assert(1 + 1 == 2)       -- passes
-- assert(false, "failed")  -- raises error

-- Dynamic loading
local f = load("return 2 + 3")
print(f())               --> 5

-- Garbage collection
print(collectgarbage("count"))  -- KB used
