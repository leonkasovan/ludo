local plugin = {
    name    = "Google Drive",
    version = "20260325",
	creator = "Dhani Novan"
}

function plugin.validate(url)
	return url:match("https://drive%.google%.com/file/d/([%w_-]+)")
end

function plugin.process(url)
	-- ludo.logInfo("Processing: " .. url)
	
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
	local id, status, output = nil, nil, nil
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
		ludo.logInfo("File is larger than 100MB, downloading with confirm token")
		id, status, output = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	else -- below 100MB
		ludo.logInfo("File is smaller than 100MB, downloading directly")
		id, status, output = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	end
	if status == 200 then
        ludo.logInfo("Processing " .. output)
    else
        ludo.logError("Download failed. Status: " .. status)
    end
	return id
end

return plugin