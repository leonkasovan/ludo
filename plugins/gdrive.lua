local plugin = {
    name    = "Google Drive",
    version = "20260325",
	creator = "Dhani Novan"
}

function plugin.validate(url)
	return url:match("https://drive%.google%.com/file/d/([%w_-]+)")
end

function plugin.process(url)
	ludo.logInfo("Processing: " .. url)
	
	-- Step 1: Extract the file ID from the URL
	local file_id = url:match("/d/([%w_-]+)")
	
	if file_id == nil then
		ludo.logError("Invalid response URL. Can't find google file id") 
		return nil
	end
	
	-- Step 2:
	local real_url = "https://drive.google.com/uc?export=download&id=" .. file_id
	local _, status, headers = http.head(real_url)
	if status ~= 200 then
		ludo.logError("Fail to get info header") 
		return nil
	end
	
	-- Step 3:
	local download_id
	if string.sub(headers["Content-Type"],1, 4) == "text" then -- over 100MB
		local content, status, headers = http.get(real_url)
		local uuid = string.match(content, '"uuid" value="([%w_%-]+)"></form>')
		if not uuid then
			ludo.logError("Invalid response URL. Can't find uuid")
			return nil
		end
		local confirm_token = string.match(content, '"confirm" value="([%w]+)">')
		if not confirm_token then
			ludo.logError("[Error] Invalid response URL. Can't find confirm_token")
			return nil
		end
		real_url = "https://drive.usercontent.google.com/download?export=download&id=" .. file_id .. "&confirm=" .. confirm_token .. "&uuid=" .. uuid
		download_id = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	else -- below 100MB
		download_id = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	end
	return download_id
end

return plugin