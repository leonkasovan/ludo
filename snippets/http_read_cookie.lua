-- http.read_cookie(filepath, name) → string or nil
-- Read a named cookie value from a Netscape-format cookie file.
-- The file is parsed directly — no HTTP request is made.

-- After http.set_cookie() + some requests, read session tokens:
local csrf = http.read_cookie("cookies.txt", "csrftoken")
if csrf then
    print("CSRF token:", csrf)
end

local sid = http.read_cookie("cookies.txt", "sessionid")
if sid then
    print("Session ID:", sid)
end

-- HttpOnly cookies (written with #HttpOnly_ prefix) are handled automatically:
local tt = http.read_cookie("tiktok_cookies.txt", "tt_chain_token")
