-- io library — file I/O operations

-- Write to a file
local f = io.open("example.txt", "w")
f:write("line one\n")
f:write("line two\n")
f:close()

-- Read entire file
local f = io.open("example.txt", "r")
local content = f:read("*a")   -- *a = all, *l = line, *n = number
f:close()
print(content)

-- Read line by line
for line in io.lines("example.txt") do
    print("line:", line)
end

-- File positioning
local f = io.open("example.txt", "r")
f:seek("set", 5)               -- move to byte 5
print(f:read("*l"))             -- read from position 5
print("position:", f:seek())    -- current position
f:close()

-- Append mode
local f = io.open("example.txt", "a")
f:write("appended line\n")
f:close()

-- Buffering control
local f = io.open("example.txt", "r")
f:setvbuf("line")               -- line buffering
-- options: "no" (unbuffered), "full" (block), "line"
f:close()

-- Check file handle type
local f = io.open("example.txt", "r")
print(io.type(f))               --> "file"
f:close()
print(io.type(f))               --> "closed file"

-- Temporary file
local tmp = io.tmpfile()
tmp:write("temporary data\n")
tmp:seek("set", 0)
print(tmp:read("*a"))
tmp:close()

-- Cleanup
os.remove("example.txt")
