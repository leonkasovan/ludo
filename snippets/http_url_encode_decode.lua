-- http.url_encode(str) → string  (percent-encoding)
-- http.url_decode(str) → string  (percent-decoding)

local encoded = http.url_encode("hello world & more=foo")
print(encoded)  -- hello%20world%20%26%20more%3Dfoo

local decoded = http.url_decode("hello%20world%20%26%20more%3Dfoo")
print(decoded)  -- hello world & more=foo
