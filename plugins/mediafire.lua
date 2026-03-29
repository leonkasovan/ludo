local plugin = {
	name	= "MediaFire",
	version = "20260325",
	creator = "Dhani Novan"
}

function plugin.validate(url)
	return url:match("https?://www%.mediafire%.com/file/%w+/.+")
end

function plugin.process(url)
	-- ludo.logInfo("Processing: " .. url)
		
	local body, status, headers = http.get(url)
	if status ~= 200 then
		ludo.logError("http.get(url) failed with status: " .. status)
		return nil
	end
	
	local real_url = string.match(body, 'href="(https://download%d+.-)"')
	if not real_url then
		ludo.logError("Failed to extract real download URL")
		-- save the body to a file for debugging
		local debug_file = ludo.getOutputDirectory() .. "mediafire_debug.html"
		local file = io.open(debug_file, "w")
		if file then
			file:write(body)
			file:close()
			ludo.logInfo("Saved debug HTML to: " .. debug_file)
		else
			ludo.logError("Failed to save debug HTML")
		end
		return nil
	end
	local id, status, output = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	if status == 200 then
        ludo.logInfo("Processing " .. output)
    else
        ludo.logError("Download failed. Status: " .. status)
    end
	return id
end

return plugin