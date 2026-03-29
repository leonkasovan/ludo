local plugin = {
	name	= "Dropbox",
	version = "20260329",
	creator = "Dhani Novan"
}

function plugin.validate(url)
	return url:match("https://www%.dropbox%.com/scl/fi/%w+/.+")
end

function plugin.process(url)
	-- replace dl=0 with dl=1 to get the direct download link
	url = url:gsub("dl=0", "dl=1")
	local id, status, output = ludo.newDownload(url, ludo.getOutputDirectory(), ludo.DOWNLOAD_NOW)
	if status == 200 then
        ludo.logInfo("Processing " .. output)
    else
        ludo.logError("Download failed. Status: " .. status)
    end
	return id
end

return plugin