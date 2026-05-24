-- http.head(url [, options]) → body, status, headers
-- HEAD request — same interface as GET but no response body.
-- Useful for checking if a URL exists, content type, or file size.

local _, status, headers = http.head("https://httpbin.org/get")
print("Status:", status)
print("Content-Length:", headers["Content-Length"])
print("Content-Type:", headers["Content-Type"])

-- Check redirect target
local _, status, headers = http.head("https://github.com/user/repo/releases/latest", {
    follow_redirects = false,
})
if status == 301 or status == 302 then
    print("Redirects to:", headers["Location"])
end
