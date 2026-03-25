-- example_plugin.lua
--
-- A simple demonstration plugin for LUDO.
-- Copy this file (or create new ones following the same structure)
-- into the plugins/ directory; LUDO loads all *.lua files at startup.
--
-- Contract every plugin must fulfil:
--   plugin.validate(url)  -> boolean
--   plugin.process(url)   -> (side effect: calls ludo.newDownload)
--
-- This example targets a fictional site "example-host.com" so you can
-- see the full extraction workflow without hitting a real server.

local plugin = {
    name    = "Example Host Extractor",
    version = "1.0",
    domain  = "example-host.com"
}

-- -----------------------------------------------------------------------
-- validate(url) -> boolean
--   Return true when this plugin can handle the given URL.
-- -----------------------------------------------------------------------
function plugin.validate(url)
    return string.match(url, "^https?://.*example%-host%.com/") ~= nil
end

-- -----------------------------------------------------------------------
-- process(url)
--   Extract the real download URL and hand it off to the LUDO engine.
--   The script may call http.get / http.post as many times as needed.
-- -----------------------------------------------------------------------
function plugin.process(url)
    ludo.logInfo("example_plugin: processing " .. url)

    -- Step 1: Fetch the landing page, pretending to be a real browser.
    local options = {
        user_agent       = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                        .. "AppleWebKit/537.36 (KHTML, like Gecko) "
                        .. "Chrome/124.0 Safari/537.36",
        follow_redirects = true,
        timeout          = 30,
        headers          = {
            ["Accept"]          = "text/html,application/xhtml+xml,*/*",
            ["Accept-Language"] = "en-US,en;q=0.9",
            ["Referer"]         = "https://example-host.com/"
        }
    }

    local body, status, headers = http.get(url, options)

    if status ~= 200 then
        ludo.logError("example_plugin: HTTP " .. tostring(status)
                      .. " for " .. url)
        return nil
    end

    -- Step 2: Scrape the real download link from the HTML.
    --   Looks for:  <a id="download-link" href="https://...">
    local real_url = string.match(body, 'id="download%-link"%s+href="(.-)"')
                  or string.match(body, "id='download%-link'%s+href='(.-)'")

    if not real_url or real_url == "" then
        ludo.logError("example_plugin: could not find download link in page")
        return nil
    end

    ludo.logInfo("example_plugin: found real URL -> " .. real_url)

    -- Step 3: Use HEAD to verify the link is a file (not another HTML page).
    local _, head_status, head_headers = http.head(real_url, options)
    if head_status ~= 200 and head_status ~= 206 then
        ludo.logError("example_plugin: HEAD check failed, status "
                      .. tostring(head_status))
        return nil
    end

    local content_type = head_headers["Content-Type"] or ""
    if string.match(content_type, "text/html") then
        ludo.logError("example_plugin: real_url points to an HTML page, "
                      .. "aborting")
        return nil
    end

    -- Step 4: Enqueue the download.
    local output_dir = ludo.getOutputDirectory() .. "example-host/"
    local id = ludo.newDownload(real_url, output_dir, ludo.DOWNLOAD_NOW)
    ludo.logSuccess("example_plugin: download #" .. tostring(id) .. " queued")

    return id
end

return plugin
