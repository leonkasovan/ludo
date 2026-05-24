-- http.set_cookie(filepath) — enable persistent cookie jar
-- http.clear_cookies() — clear jar and last URL state
-- http.read_cookie(filepath, name) → value or nil

-- 1. Enable cookie persistence
http.set_cookie("session_cookies.txt")

-- 2. Login — cookies saved to the file
http.post("https://example.com/login",
    "username=user&password=pass",
    { headers = { ["Content-Type"] = "application/x-www-form-urlencoded" } })

-- 3. Authenticated request — cookies sent automatically
local body, status = http.get("https://example.com/dashboard")
print("Status:", status)

-- 4. Read a specific cookie from the jar
local csrf = http.read_cookie("session_cookies.txt", "csrftoken")
if csrf then
    print("CSRF token:", csrf)
end

-- 5. Clear cookies
http.clear_cookies()
