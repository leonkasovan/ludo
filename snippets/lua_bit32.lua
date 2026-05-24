-- bit32 library — bitwise operations (Lua 5.2)

local hex = function(x) return ("%08x"):format(x) end

local a, b = 0x0F, 0xF0
print("a       = " .. hex(a))   --> 0000000f
print("b       = " .. hex(b))   --> 000000f0

-- Logical
print("band    = " .. hex(bit32.band(a, b)))  --> 00000000
print("bor     = " .. hex(bit32.bor(a, b)))   --> 000000ff
print("bxor    = " .. hex(bit32.bxor(a, b)))  --> 000000ff
print("bnot(a) = " .. hex(bit32.bnot(a)))     --> fffffff0
print("btest   = " .. tostring(bit32.btest(a, b)))  --> false

-- Shifts
local x = 0x12345678
print("x           = " .. hex(x))
print("lshift(x,8) = " .. hex(bit32.lshift(x, 8)))    --> 34567800
print("rshift(x,8) = " .. hex(bit32.rshift(x, 8)))    --> 00123456
print("arshift(x,8)= " .. hex(bit32.arshift(x, 8)))   --> 00123456

-- Rotates
print("lrotate(x,4)= " .. hex(bit32.lrotate(x, 4)))   --> 23456781
print("rrotate(x,4)= " .. hex(bit32.rrotate(x, 4)))   --> 81234567

-- Extract a bit field
-- bit32.extract(n, field, width) — extract `width` bits starting at `field`
print("extract(x,8,8) = " .. hex(bit32.extract(x, 8, 8)))  --> 00000056

-- Replace a bit field
-- bit32.replace(n, v, field, width) — copy `width` bits of `v` into `n` at `field`
local r = bit32.replace(0, 0xFF, 8, 8)
print("replace(0,FF,8,8) = " .. hex(r))                    --> 0000ff00
