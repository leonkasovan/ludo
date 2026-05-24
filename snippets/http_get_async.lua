-- http.get_async(url, options, callback) → nothing
-- Async GET — runs on background thread, callback fires on main thread.
-- ** GUI builds only ** (ludocon returns nil, error).
-- The callback(body, status, headers) is one-shot.

http.get_async("https://httpbin.org/delay/3",
    { timeout = 30 },
    function(body, status, headers)
        print("Async result: HTTP", status)
        if status == 200 then
            print("Body:", body:sub(1, 200))
        end
    end)
