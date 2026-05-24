-- http.post(url, body [, options]) → body, status, headers
-- POST request — body can be form data, JSON, or raw text.

-- Form data
local body, status = http.post("https://httpbin.org/post",
    "username=admin&password=secret",
    { headers = { ["Content-Type"] = "application/x-www-form-urlencoded" } })

-- JSON
local body, status = http.post("https://httpbin.org/post",
    '{"name": "test", "value": 42}',
    {
        headers = {
            ["Content-Type"]  = "application/json",
            ["Authorization"] = "Bearer token123",
        },
        timeout = 10,
    })
