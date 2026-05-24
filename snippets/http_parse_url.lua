-- http.parse_url(url) → { scheme, host, port, path, query }
-- Parse a URL into its components.

local parts = http.parse_url("https://example.com:8080/api/data?key=value&page=1")
print("scheme:", parts.scheme)  -- https
print("host:",   parts.host)    -- example.com
print("port:",   parts.port)    -- 8080
print("path:",   parts.path)    -- /api/data
print("query:",  parts.query)   -- key=value&page=1
