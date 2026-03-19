To give your Lua scripts enough power to bypass bot detection, follow redirects, and scrape complex sites, you need more than just a simple "fetch" command. 

The best approach is to expose a high-level **Request Object** or a table-driven `http` module. Since you are likely using **libcurl** as your backend, these functions map directly to its capabilities.

### 1. The Core HTTP Methods
These are the primary tools the script will use to navigate to the "real" download link.

* **`http.get(url, [options])`**: The most used function. It should return the response body, status code, and a table of headers.
* **`http.head(url, [options])`**: Crucial for download managers. It allows the script to check `Content-Length` (file size) and `Content-Type` (is it a `.zip` or just another HTML page?) without downloading the whole file.
* **`http.post(url, body, [options])`**: Some file hosts require a "click" (form submission) to generate a temporary download link.

### 2. Request Options (The "Options" Table)
Instead of adding 20 parameters to your functions, pass a Lua table. Your C code should check for these keys:

* **`headers`**: A table of custom headers (e.g., `{["Referer"] = "..."}`).
* **`user_agent`**: To mimic a real browser (very common for link extraction).
* **`follow_redirects`**: Boolean. Scripts often need to see the final URL after 3-4 hops.
* **`timeout`**: Prevent a script from hanging your worker thread if a host is down.
* **`cookies`**: Pass existing session cookies or a path to a "cookie jar" file.

### 3. State & Session Management
If a site requires a login or a multi-step "waiting" process, the Lua script needs to maintain state.

* **`http.set_cookie(path)`**: Tells the backend to save/load cookies from a specific file.
* **`http.clear_cookies()`**: Resets the session.
* **`http.get_last_url()`**: After a `get` call with redirects enabled, this returns the *actual* final URL.

### 4. URL & String Utilities
Link extraction often involves messy strings. While Lua has string patterns, these helpers make script writing much faster:

* **`http.url_encode/decode(string)`**: Essential for handling URLs with spaces or special characters.
* **`http.parse_url(url)`**: Returns a table with `host`, `path`, `query`, and `scheme`. This is incredibly helpful for the `is_supported` validation logic.

---

### Example: What a "Pro" Extraction Script looks like
If you expose the functions above, a script for a complex site might look like this:

```lua
function plugin.extract_real_url(original_url)
    -- 1. Pretend to be a browser to avoid 403 errors
    local options = {
        user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ...",
        follow_redirects = true,
        headers = { ["Referer"] = "https://source-site.com/" }
    }

    -- 2. Fetch the landing page
    local response, status, headers = http.get(original_url, options)
    
    if status == 200 then
        -- 3. Scrape a hidden token or ID from the HTML
        local download_id = string.match(response, 'data%-id="(.-)"')
        
        -- 4. Post to an API to get the final link
        local api_url = "https://api.site.com/generate"
        local final_json = http.post(api_url, "id=" .. download_id, options)
        
        -- (Assume you've also exposed a JSON parser to Lua)
        local data = json.decode(final_json)
        return data.download_url
    end
    return nil
end
```

### 5. Implementation Tip: The "Download" Handoff
Once the Lua script returns the `real_url`, your C code should take that URL and start a **new** libcurl transfer specifically for the file download. 

**Why?** You don't want to load a 2GB file into a Lua string. The Lua `http.get` should be restricted to small buffers (like HTML or JSON), while the actual file download should stream directly to the disk in C.

Would you like to see the C code for wrapping a basic `http.get` call using `libcurl` and `luaL_register`?