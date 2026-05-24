-- http.base64_encode(str) → string  (binary-safe)
-- http.base64_decode(str) → string  (binary-safe)

-- Encode
local b64 = http.base64_encode("hello world")
print(b64)  -- aGVsbG8gd29ybGQ=

-- Decode
local raw = http.base64_decode("aGVsbG8gd29ybGQ=")
print(raw)  -- hello world

-- Works with binary data too
local b64 = http.base64_encode("\0\1\2\255")
local raw = http.base64_decode(b64)
