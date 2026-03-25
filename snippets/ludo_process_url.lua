local plugin = {}

function plugin.validate(url)
	-- Added fallback to match URLs even if they don't end exactly in /view
	return url:match("https://drive%.google%.com/file/d/([%w_-]+)")
end

function plugin.process(url)
	ludo.logInfo("Processing: " .. url)
	-- Step 1: Extract the file ID from the URL
	local file_id = url:match("/d/([%w_-]+)")
	
	if file_id == nil then
		if ludo.lua_tester then
			ludo.lua_tester.println("[Error] Invalid response URL. Can't find google file id") 
		end
		return nil
	end
	
	-- Step 2:
	local real_url = "https://drive.google.com/uc?export=download&id=" .. file_id
	local _, status, headers = http.head(real_url)
	if status ~= 200 then
		ludo.lua_tester.println("[Error] get header") 
		return nil
	end
	
	local download_id
	if string.sub(headers["Content-Type"],1, 4) == "text" then -- over 100MB
		local content, status, headers = http.get(real_url)
		local uuid = string.match(content, '"uuid" value="([%w_%-]+)"></form>')
		if not uuid then
			ludo.lua_tester.println("[Error] Invalid response URL. Can't find uuid")
			return nil
		end
		local confirm_token = string.match(content, '"confirm" value="([%w]+)">')
		if not confirm_token then
			ludo.lua_tester.println("[Error] Invalid response URL. Can't find confirm_token")
			return nil
		end
		real_url = "https://drive.usercontent.google.com/download?export=download&id=" .. file_id .. "&confirm=" .. confirm_token .. "&uuid=" .. uuid
		download_id = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	else -- below 100MB
		download_id = ludo.newDownload(real_url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	end
	return download_id
end

-- Test Execution
local url = "https://drive.google.com/file/d/1iAaZoTDwiBBy_LcI-vV1U69_vK0oNLaP/view?usp=drive_link" -- over 100MB
-- local url = "https://drive.google.com/file/d/18iw_4VdfqmUDLbJl3XZ-SRKd6SqSLi6F/view?usp=sharing" -- below 100MB
if plugin.validate(url) then
	plugin.process(url)
end
-- return plugin