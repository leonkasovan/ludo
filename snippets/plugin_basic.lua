-- plugin_basic.lua — template for a minimal plugin
-- Pattern: validate URL → extract data → queue download
-- Example: simple redirect-based sites (Dropbox, GDrive, direct media)

local plugin = {
    name    = "BasicSite",
    version = "20260501",
    creator = "Your Name",
}

local TIMEOUT = 30
local UA      = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

function plugin.validate(url)
    if type(url) ~= "string" then return false end
    return url:match("https?://www%.example%.com/file/%w+") ~= nil
end

function plugin.process(url)
    ludo.logInfo("Processing: " .. url)

    -- Step 1: fetch the page
    local body, status = http.get(url, { user_agent = UA, timeout = TIMEOUT })
    if status ~= 200 then
        ludo.logError("Failed to fetch page (HTTP " .. tostring(status) .. ")")
        return
    end

    -- Step 2: extract the download URL from HTML
    local dl_url = body:match('href="(https://dl%.example%.com/[^"]+)"')
    if not dl_url then
        ludo.logError("Could not find download URL in page")
        -- Save debug HTML for analysis
        local f = io.open(ludo.getOutputDirectory() .. "/basic_debug.html", "w")
        if f then f:write(body); f:close() end
        return
    end

    -- Step 3: queue the download
    local outdir = ludo.getOutputDirectory()
    local _, dl_status, output = ludo.newDownload(dl_url, outdir, ludo.DOWNLOAD_NOW)
    if dl_status == 200 or dl_status == 206 or dl_status == 0 then
        ludo.logSuccess("Queued → " .. (output or "file"))
    else
        ludo.logError("Preflight HTTP " .. tostring(dl_status))
    end
end

return plugin
