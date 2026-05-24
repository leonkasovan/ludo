-- Lua 5.2 Standard Library Demo — interactive explorer for all stdlib modules
-- Run from Ludo: Tools > Lua StdLib Demo (or via -s tools/lua_demo.lua)
--
-- Each tab has pre-loaded examples. Click Run to execute and see output.
-- Tools must NOT call ui.Main()/ui.Uninit() — use MainStep() instead.

local ui = require("ui")
local win_open = true

-- ====================================================================
-- Execution helper: captures print() output into a string
-- ====================================================================

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

-- ====================================================================
-- Build a tab: label, code area, output area, run button
-- ====================================================================

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
        local result = run_code(code_mle:Text())
        output_mle:SetText(result)
    end, nil)

    return box
end

-- ====================================================================
-- Example code for each library
-- ====================================================================

local base_examples = [[-- === Base Library Examples ===

-- type() - get type of a value
print("type(42)        =", type(42))
print("type('hi')      =", type("hi"))
print("type({})        =", type({}))
print("type(nil)       =", type(nil))
print("type(print)     =", type(print))

-- tostring / tonumber
print("tostring(123)   =", tostring(123))
print("tonumber('42')  =", tonumber("42"))
print("tonumber('ff',16)=", tonumber("ff", 16))

-- pairs / ipairs iteration
local t = {10, 20, 30, key = "val"}
for i, v in ipairs(t) do print("ipairs", i, v) end
for k, v in pairs(t) do print("pairs", k, v) end

-- pcall / xpcall error handling
local ok, result = pcall(function() return 1/0 end)
print("pcall 1/0      =", ok, result)

-- select and #
local function test(a, b, c) print("select('#',...)=", select("#", ...)) end
test(1, 2, 3)

-- rawget / rawset / rawequal / rawlen
local mt = {}
local o = setmetatable({x = 10}, mt)
print("rawget(o,'x')  =", rawget(o, "x"))
print("rawlen({1,2,3})=", rawlen({1,2,3}))

-- assert / error
local ok2, err = pcall(function() assert(false, "boom") end)
print("assert false   =", err)

-- getmetatable / setmetatable
print("getmetatable(o)=", getmetatable(o))

-- next
local t2 = {a=1, b=2}
print("next(t2,nil)   =", next(t2, nil))]]

local string_examples = [[-- === String Library Examples ===

local s = "Hello, World!"

-- len / upper / lower / reverse
print("len:", #s, string.len(s))
print("upper:", string.upper(s))
print("lower:", string.lower(s))
print("reverse:", string.reverse(s))

-- byte / char
print("byte 'A':", string.byte("A"))
print("char 65,66:", string.char(65, 66, 67))

-- sub
print("sub(1,5):", string.sub(s, 1, 5))
print("sub(-6):", string.sub(s, -6))

-- find / match
local start, stop = string.find(s, "World")
print("find 'World':", start, stop)
print("match '%a+':", string.match(s, "%a+"))

-- gmatch (iterator)
for word in string.gmatch(s, "%a+") do
    print("gmatch word:", word)
end

-- gsub
local replaced, n = string.gsub(s, "l", "*")
print("gsub 'l'->'*':", replaced, n)

-- rep
print("rep '-*' 5:", string.rep("-*", 5))

-- format
print("format:", string.format("pi = %.3f, int = %d", math.pi, 42))

-- dump (bytecode)
local f = load("return 2+2")
local bc = string.dump(f)
print("dump len:", #bc, "bytes")]]

local table_examples = [[-- === Table Library Examples ===

local t = { "banana", "apple", "cherry", "date" }

-- insert / remove
table.insert(t, "elderberry")
table.insert(t, 2, "blueberry")
print("after inserts:", table.concat(t, ", "))

table.remove(t, 1)
print("after remove :", table.concat(t, ", "))

-- sort
table.sort(t)
print("sorted       :", table.concat(t, ", "))
table.sort(t, function(a, b) return #a > #b end)
print("by length    :", table.concat(t, ", "))

-- concat
local csv = table.concat(t, ", ")
print("concat csv   :", csv)

-- pack / unpack
local packed = table.pack("a", "b", "c")
print("packed.n     :", packed.n)
local a, b, c = table.unpack(packed)
print("unpack       :", a, b, c)

-- Advanced: sort table of records
local people = {
    { name = "Zoe",   age = 25 },
    { name = "Alice", age = 30 },
    { name = "Bob",   age = 20 },
}
table.sort(people, function(a, b) return a.age < b.age end)
for _, p in ipairs(people) do
    print(string.format("  %s is %d", p.name, p.age))
end]]

local math_examples = [[-- === Math Library Examples ===

print("pi     =", math.pi)
print("huge   =", math.huge)

-- Rounding
print("ceil 2.3  =", math.ceil(2.3))
print("floor 2.9 =", math.floor(2.9))
print("abs -7    =", math.abs(-7))

-- Min / Max
print("max(3,7,2)=", math.max(3, 7, 2))
print("min(3,7,2)=", math.min(3, 7, 2))

-- Trig
print("sin(pi/2) =", math.sin(math.pi/2))
print("cos(0)    =", math.cos(0))
print("tan(pi/4) =", math.tan(math.pi/4))
print("deg(pi)   =", math.deg(math.pi))
print("rad(180)  =", math.rad(180))
print("asin(0.5) =", math.asin(0.5))
print("acos(0.5) =", math.acos(0.5))
print("atan2(1,0)=", math.atan2(1, 0))

-- Hyperbolic
print("cosh(0)   =", math.cosh(0))
print("sinh(0)   =", math.sinh(0))
print("tanh(1)   =", math.tanh(1))

-- Exponents / logs
print("sqrt(144) =", math.sqrt(144))
print("exp(1)    =", math.exp(1))
print("log(e)    =", math.log(math.exp(1)))
print("log(100,10)=", math.log(100, 10))
print("pow(2,10) =", math.pow(2, 10))

-- Fractional
print("fmod(10,3)=", math.fmod(10, 3))
local m, e = math.frexp(12.5)
print("frexp 12.5=", m, "* 2^", e)
print("ldexp(0.78125,4)=", math.ldexp(0.78125, 4))
local int, frac = math.modf(3.14159)
print("modf 3.14159 =", int, frac)

-- Random
math.randomseed(os.time())
print("random      =", math.random())
print("random(1,6) =", math.random(1, 6))]]

local io_examples = [[-- === I/O Library Examples ===

-- Write to a temp file
local f = io.open("_lua_demo_tmp.txt", "w")
f:write("line one\n")
f:write("line two\n")
f:write("line three\n")
f:close()
print("wrote _lua_demo_tmp.txt")

-- Read entire file
local f = io.open("_lua_demo_tmp.txt", "r")
local content = f:read("*a")
f:close()
print("read all:", content)

-- Read line by line
for line in io.lines("_lua_demo_tmp.txt") do
    print("line:", line)
end

-- File seek
local f = io.open("_lua_demo_tmp.txt", "r")
f:seek("set", 5)
print("seek to 5:", f:read("*l"))
f:close()

-- Buffering
local f = io.open("_lua_demo_tmp.txt", "a")
f:setvbuf("line")
f:write("buffered line\n")
f:close()

-- io.type
local f = io.open("_lua_demo_tmp.txt", "r")
print("io.type:", io.type(f))
f:close()
print("io.type closed:", io.type(f))

-- Temp file
local tmp = io.tmpfile()
tmp:write("temp data\n")
tmp:seek("set", 0)
print("tmpfile:", tmp:read("*a"))
tmp:close()

-- Cleanup
os.remove("_lua_demo_tmp.txt")
print("cleaned up")]]

local os_examples = [[-- === OS Library Examples ===

-- Time
print("time()      =", os.time())
local t = os.date("*t")
print(string.format("date: %04d-%02d-%02d", t.year, t.month, t.day))

-- date formatting
print("date       :", os.date("%Y-%m-%d %H:%M:%S"))
print("date (iso) :", os.date("%Y-%m-%dT%H:%M:%S"))

-- difftime
local t1 = os.time()
-- small delay
local t2 = os.time()
print("difftime    =", os.difftime(t2, t1))

-- clock (CPU time)
print("clock       =", os.clock())

-- getenv
local path = os.getenv("PATH")
print("PATH exists:", path ~= nil and #path > 0)

-- execute
local ok, exit, code = os.execute("echo hello")
print("execute ok :", ok, "exit:", exit, "code:", code)

-- rename / remove
local f = io.open("_os_demo.txt", "w"); f:write("test"); f:close()
os.rename("_os_demo.txt", "_os_demo_renamed.txt")
local f2 = io.open("_os_demo_renamed.txt", "r")
print("renamed file:", f2:read("*a"))
f2:close()
os.remove("_os_demo_renamed.txt")

-- tmpname
print("tmpname    :", os.tmpname())

-- exit (commented: would exit the program)
-- os.exit(0)

-- setlocale
print("locale     :", os.setlocale("C"))]]

local bit32_examples = [[-- === Bit32 Library Examples ===

local a, b = 0x0F, 0xF0
print(string.format("     a=%08x,      b=%08x", a, b))

-- Logical
print(string.format(" band(a,b) = %08x", bit32.band(a, b)))
print(string.format(" bor(a,b)  = %08x", bit32.bor(a, b)))
print(string.format(" bxor(a,b) = %08x", bit32.bxor(a, b)))
print(string.format(" bnot(a)   = %08x", bit32.bnot(a)))
print(string.format(" btest(a,b)= %s",  tostring(bit32.btest(a, b))))

-- Shifts
local x = 0x12345678
print(string.format("      x    = %08x", x))
print(string.format(" lshift(x,8)= %08x", bit32.lshift(x, 8)))
print(string.format(" rshift(x,8)= %08x", bit32.rshift(x, 8)))
print(string.format(" arshift(x,8)=%08x", bit32.arshift(x, 8)))

-- Rotates
print(string.format(" lrotate(x,4)=%08x", bit32.lrotate(x, 4)))
print(string.format(" rrotate(x,4)=%08x", bit32.rrotate(x, 4)))

-- Extract / Replace
print(string.format(" extract(x,8,8)=%08x", bit32.extract(x, 8, 8)))
local x2 = bit32.replace(0, 0xFF, 8, 8)
print(string.format(" replace(0,FF,8,8)=%08x", x2))]]

local coroutine_examples = [[-- === Coroutine Library Examples ===

-- Basic co-routine
local co = coroutine.create(function()
    for i = 1, 3 do
        print("  coroutine yielded:", i)
        coroutine.yield(i * 10)
    end
    return "done"
end)

print("status:", coroutine.status(co))
local ok, result = coroutine.resume(co)
print("resume 1:", ok, result)
local ok, result = coroutine.resume(co)
print("resume 2:", ok, result)
local ok, result = coroutine.resume(co)
print("resume 3:", ok, result)
local ok, result = coroutine.resume(co)
print("resume 4:", ok, result)
print("status:", coroutine.status(co))

-- coroutine.wrap (returns a function that resumes)
local cw = coroutine.wrap(function()
    for i = 1, 3 do
        coroutine.yield(i * 100)
    end
end)
print("wrap 1:", cw())
print("wrap 2:", cw())
print("wrap 3:", cw())

-- running
print("running:", coroutine.running())]]

local debug_examples = [[-- === Debug Library Examples ===

-- traceback
print("traceback:")
print(debug.traceback())

-- getinfo on a function
local f = function(a, b) return a + b end
local info = debug.getinfo(f, "nS")
print("func name:", info.name or "(anonymous)")
print("source:", info.source)
print("linedefined:", info.linedefined)

-- getregistry
local reg = debug.getregistry()
print("registry has _G:", reg._G ~= nil)

-- get upvalue
local function make_counter()
    local count = 0
    return function()
        count = count + 1
        return count
    end
end
local c = make_counter()
local name, val = debug.getupvalue(c, 1)
print("upvalue:", name, "=", val)
debug.setupvalue(c, 1, 100)
print("after setupvalue:", c())

-- getmetatable (debug version)
local t = {}
local mt = debug.getmetatable(t)
print("debug.getmetatable({}) =", mt)

-- setmetatable (debug version)
local mt2 = {}
debug.setmetatable(t, mt2)
print("debug.getmetatable after set =", debug.getmetatable(t))

-- getlocal
local function test()
    local x, y = 10, 20
    print("getlocal(0,1) =", debug.getlocal(1, 1))
    print("getlocal(0,2) =", debug.getlocal(1, 2))
end
test()]]

-- ====================================================================
-- Build the window
-- ====================================================================

local tabs = ui.NewTab()
tabs:Append("Base",       make_tab(base_examples))
tabs:Append("String",     make_tab(string_examples))
tabs:Append("Table",      make_tab(table_examples))
tabs:Append("Math",       make_tab(math_examples))
tabs:Append("I/O",        make_tab(io_examples))
tabs:Append("OS",         make_tab(os_examples))
tabs:Append("Bit32",      make_tab(bit32_examples))
tabs:Append("Coroutine",  make_tab(coroutine_examples))
tabs:Append("Debug",      make_tab(debug_examples))

local win = ui.NewWindow("Lua 5.2 Standard Library Demo", 720, 620, false)
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
