-- This Lua script is designed to extract a download link from the HTML content of a webpage. It uses the `string.match` function to search for a specific pattern in the `body` variable.
local real_url = string.match(ludo.http_tester.response.content(), 'id="download%-link"%s+href="(.-)"')
ludo.lua_tester.printf("Extracted URL: %s", real_url)
