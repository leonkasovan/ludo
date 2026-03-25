local plugin = {}

function plugin.validate(url)
    return string.find(url, "github%.com/[^/]+/[^/]+/releases") ~= nil
end

function plugin.process(url)
    ludo.logInfo("Fetching GitHub release page...")

    local body, status = http.get(url, {
        user_agent = "Ludo-GitHub-Plugin/1.0",
        timeout    = 30,
    })

    if status ~= 200 then
        ludo.logError("HTTP " .. status .. " for " .. url)
        return
    end

    local count = 0
    for link in string.gmatch(body, 'href="(/[^"]+/releases/download/[^"]+)"') do
        local download_url = "https://github.com" .. link
        ludo.logInfo("Queuing: " .. download_url)
        ludo.newDownload(download_url)
        count = count + 1
    end

    if count == 0 then
        ludo.logError("No download links found on the release page")
    else
        ludo.logSuccess("Found " .. count .. " file(s) to download")
    end
end

return plugin