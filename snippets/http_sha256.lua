-- http.sha256(str) → string (raw 32-byte binary digest)
-- Returns the raw binary SHA-256 hash, NOT hex-encoded.

-- Get hex by iterating bytes
local digest = http.sha256("hello")
local hex = digest:gsub(".", function(c)
    return ("%02x"):format(c:byte())
end)
print(hex)
-- 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824

-- Base64 of digest
local b64 = http.base64_encode(http.sha256("hello"))
print(b64)

-- Binary comparison (used in TikTok WAF challenge)
local expected = http.base64_decode(v_c)  -- 32 bytes
if http.sha256(prefix .. tostring(i)) == expected then
    -- solution found
end
