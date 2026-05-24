-- JSON configuration functions — each returns the previous value.

local json = json or require("json")

-- Number precision (1–14, default 14)
json.encode_number_precision(3)
print(json.encode(1.23456789))    --> 1.23
json.encode_number_precision(14)

-- Max nesting depth (default 1000)
json.encode_max_depth(100)
json.decode_max_depth(100)

-- Encode invalid numbers (NaN/Inf)
-- "off" = error, "on" = allow, "null" = encode as null
json.encode_invalid_numbers("null")
print(json.encode(1/0))            --> null
json.encode_invalid_numbers("off")

-- Decode invalid numbers (NaN/Inf in input)
json.decode_invalid_numbers(true)  -- allow
json.decode_invalid_numbers(false) -- reject (default)

-- Reuse encode buffer (performance)
json.encode_keep_buffer(true)

-- Sparse array handling
-- encode_sparse_array(convert, ratio, safe)
json.encode_sparse_array(true, 2, 10)
