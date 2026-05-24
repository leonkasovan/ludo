-- cjson_safe — safe encode/decode that returns nil, err instead of throwing

local safe = cjson_safe or require("cjson_safe")

-- No pcall needed — returns nil, error on failure
local val, err = safe.decode('{"valid": "json"}')
if val then
    print("OK:", val.valid)
else
    print("Error:", err)
end

local val, err = safe.decode("{bad}")
print("Bad:", val, err)           --> nil  "Expected value..."

-- Safe encode
local str, err = safe.encode({ a = 1 })
print(str)                        --> {"a":1}

-- All config functions work on safe too
safe.encode_number_precision(4)
print(safe.encode(math.pi))

-- Has its own null sentinel
print(safe.null == safe.new().null)  --> false
