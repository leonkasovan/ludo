local plugin = {
	name	= "MediaFire",
	version = "20260325",
	creator = "Dhani Novan"
}

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
	local download_id = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	return download_id
end

return plugin