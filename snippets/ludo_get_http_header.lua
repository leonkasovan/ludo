local plugin = {}

-- local url = "https://www.mediafire.com/file/8a7e3onqm08nxj7/Rumble+McSkirmish+PotS+Style+1.0+by+KamenRiderGOUKI.rar/file"
-- local url = "https://www.mediafire.com/file/qkbwqrt725dbtbt/Terry.rar/file"
-- local url = "https://www.mediafire.com/file/z5um4krkedhxgr2/Genjuro.rar/file"
function plugin.validate(url)
	return url:match("https?://www%.mediafire%.com/file/%w+/.+")
end

function plugin.process(url)
	ludo.logInfo("Processing: " .. url)
		
	local body, status, headers = http.get(url)
	if status ~= 200 then
		ludo.lua_tester.println("[Error] http.get(url)")
		return nil
	end
	
	local real_url = string.match(body, 'href="(https://download%d+.-)"')
	local _, status, headers = http.head(real_url)
	if status ~= 200 then
		ludo.lua_tester.println("[Error] get header") 
		return nil
	end

	for key, value in pairs(headers) do
		ludo.lua_tester.println(key .. ": " .. value)
	end
	
	-- local download_id
	-- download_id = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	-- return download_id
end

-- Test Validation
-- local urls = {
--     "https://www.mediafire.com/file/8a7e3onqm08nxj7/Rumble+McSkirmish+PotS+Style+1.0+by+KamenRiderGOUKI.rar/file",
--     "https://www.mediafire.com/file/qkbwqrt725dbtbt/Terry.rar/file",
--     "https://www.mediafire.com/file/z5um4krkedhxgr2/Genjuro.rar/file"
-- }

-- for _, url in ipairs(urls) do
--     if plugin.validate(url) then
--         ludo.lua_tester.printf("%s supported\n", url)
--     end
-- end

-- Test Execution
-- local url = "https://www.mediafire.com/file/8a7e3onqm08nxj7/Rumble+McSkirmish+PotS+Style+1.0+by+KamenRiderGOUKI.rar/file"
-- local url = "https://www.mediafire.com/file/qkbwqrt725dbtbt/Terry.rar/file"
local url = "https://www.mediafire.com/file/z5um4krkedhxgr2/Genjuro.rar/file"

if plugin.validate(url) then
	plugin.process(url)
end
-- return plugin