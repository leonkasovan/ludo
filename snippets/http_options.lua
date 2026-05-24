-- http options table — shared across get(), head(), post()
-- All fields are optional.

local opts = {
    -- User-Agent header
    user_agent = "MyScript/1.0",

    -- Request timeout in seconds
    timeout = 30,

    -- Follow HTTP redirects (default true)
    follow_redirects = true,

    -- HTTP version: 1 = HTTP/1.1, 2 = HTTP/2.0
    http_version = 1,

    -- Custom headers as key-value pairs
    headers = {
        ["Accept"]        = "application/json",
        ["Authorization"] = "Bearer token123",
    },
}

-- GET with options
local body, status = http.get("https://api.example.com/data", opts)

-- Per-request cookie jar (overrides http.set_cookie for this request only)
opts.cookies = "request_cookies.txt"
local body2, status2 = http.get("https://example.com", opts)
