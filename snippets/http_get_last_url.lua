-- http.get_last_url() → string
-- Returns the final URL after following all redirects.
-- Returns empty string if no request has been made.

-- Follow a short URL to find the real destination
http.get("https://github.com/user/repo/releases/latest")
local final = http.get_last_url()
print("Final URL:", final)

-- The result persists until the next http request
print("Last URL:", http.get_last_url())
