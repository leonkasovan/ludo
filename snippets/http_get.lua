-- http.get(url [, options]) → body, status, headers
-- Perform a GET request with optional options table.
--
-- Returns: response_body (string), http_status (number), headers (table)

local body, status, headers = http.get("https://httpbin.org/get")
print("Status:", status)
print("Content-Type:", headers["Content-Type"])

-- With options
local body, status = http.get("https://api.example.com/data", {
    user_agent = "MyBot/1.0",
    timeout    = 15,
    headers    = {
        ["Authorization"] = "Bearer token123",
        ["Accept"]        = "application/json",
    },
})
