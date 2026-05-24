-- debug library — introspection and debugging tools

-- Stack trace
print(debug.traceback())

-- Get info about a function
local function test(a, b) return a + b end
local info = debug.getinfo(test, "nS")
print("name:", info.name)              --> test
print("source:", info.source)          --> @...
print("linedefined:", info.linedefined)

-- Upvalue inspection
local function counter()
    local n = 0
    return function() n = n + 1; return n end
end
local c = counter()
local name, val = debug.getupvalue(c, 1)
print("upvalue:", name, val)           --> n    0
debug.setupvalue(c, 1, 100)
print("after setupvalue:", c())        --> 101

-- Local variable inspection
local function bar()
    local x, y = 10, 20
    print("local(0,1):", debug.getlocal(1, 1))   --> x    10
    print("local(0,2):", debug.getlocal(1, 2))   --> y    20
end
bar()

-- Metatable access (debug version)
local t = {}
local mt = {}
debug.setmetatable(t, mt)
print(debug.getmetatable(t) == mt)     --> true

-- Registry
local reg = debug.getregistry()
print("registry has _G:", reg._G ~= nil)
