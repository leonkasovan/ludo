-- json.new() → independent module instance
-- Each instance has its own configuration state.

local json = json or require("json")

local j1 = json.new()
local j2 = json.new()

j1.encode_number_precision(3)
j2.encode_number_precision(14)

print(j1.encode(math.pi))  --> 3.14
print(j2.encode(math.pi))  --> 3.1415926535898
print(json.encode(math.pi)) --> 3.1415926535898 (unchanged)

-- Each instance has its own json.null sentinel
print(j1.null == j2.null)      --> false
print(j1.null == json.null)    --> false
