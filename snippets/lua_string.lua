-- string library — byte, char, sub, match, gmatch, gsub, format, etc.

local s = "Hello, World!"

-- Inspection
print(#s)                    --> 13
print(string.len(s))         --> 13
print(string.byte(s, 1))     --> 72  (ASCII 'H')
print(string.char(72, 101))  --> "He"

-- Extract / transform
print(string.sub(s, 1, 5))   --> Hello
print(string.sub(s, -6))     --> World!
print(string.upper(s))       --> HELLO, WORLD!
print(string.lower(s))       --> hello, world!
print(string.reverse(s))     --> !dlroW ,olleH
print(string.rep("ab", 3))   --> ababab

-- Pattern matching
print(string.find(s, "World"))      --> 8  12
print(string.match(s, "(%a+)"))     --> Hello
print(string.match(s, "(%a+)$"))    --> World

-- gmatch (iterator over all matches)
for word in string.gmatch(s, "%a+") do
    print(word)
end

-- gsub (substitution)
local result, n = string.gsub(s, "l", "*")
print(result, n)            --> He**o, Wor*d!  3

-- String formatting
print(string.format("pi=%.3f, int=%d, hex=%x", math.pi, 255, 255))
--> pi=3.142, int=255, hex=ff

-- dump (binary bytecode)
local f = load("return 2+2")
local bc = string.dump(f)
print("bytecode:", #bc, "bytes")
