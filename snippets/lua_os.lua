-- os library — time, date, system commands, file operations

-- Date and Time
local now = os.time()                  -- current timestamp
print("timestamp:", now)
print("date:", os.date("%Y-%m-%d"))    --> 2026-05-24
print("time:", os.date("%H:%M:%S"))    --> 14:30:00
print("full:", os.date("%c"))          --> locale-dependent

-- Parse a date table
local t = os.date("*t")                -- table with year, month, day, etc.
print(string.format("%04d-%02d-%02d", t.year, t.month, t.day))

-- Time difference
local t1 = os.time()
local t2 = os.time()
print("elapsed:", os.difftime(t2, t1))

-- CPU time
print("cpu clock:", os.clock())

-- Environment variable
local path = os.getenv("PATH")
if path then print("PATH:", path:sub(1, 50) .. "...") end

-- Execute a system command
local ok, exit, code = os.execute("echo hello")
print(ok, exit, code)

-- File operations
local f = io.open("_test.txt", "w")
f:write("test data\n")
f:close()
os.rename("_test.txt", "_renamed.txt")
os.remove("_renamed.txt")

-- Temporary filename
print("tmpname:", os.tmpname())

-- Locale
print("locale:", os.setlocale("C"))    -- "C" = default
