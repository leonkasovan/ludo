-- generic_direct_download.lua
--
-- Fallback plugin: handles any direct http(s) link that points to a
-- binary file (zip, mp4, tar.gz, exe, apk, …).
--
-- Because validate() checks the URL extension, it won't accidentally
-- grab HTML pages from sites that have their own dedicated plugin.

local plugin = {
    name    = "Generic Direct Download",
    version = "1.0",
    domain  = "*"
}

-- File extensions we consider "direct" downloads.
local DIRECT_EXTENSIONS = {
    "zip", "tar", "gz", "bz2", "xz", "7z", "rar",
    "mp4", "mkv", "avi", "mov", "webm", "flv",
    "mp3", "flac", "ogg", "wav",
    "exe", "msi", "dmg", "pkg", "deb", "rpm", "apk",
    "pdf", "epub",
    "iso", "img",
}

local function has_direct_ext(url)
    -- Strip query string and anchor
    local path = string.match(url, "^([^?#]+)") or url
    local ext  = string.match(path, "%.([^./]+)$")
    if not ext then return false end
    ext = string.lower(ext)
    for _, e in ipairs(DIRECT_EXTENSIONS) do
        if ext == e then return true end
    end
    return false
end

function plugin.validate(url)
    if not string.match(url, "^https?://") then return false end
    -- Accept if the URL path ends with a known binary extension,
    -- OR if a HEAD request reveals a non-HTML Content-Type.
    return has_direct_ext(url)
end

function plugin.process(url)
    ludo.logInfo("generic_direct: verifying " .. url)

    local options = {
        user_agent       = "LUDO/1.0",
        follow_redirects = true,
        timeout          = 15,
    }

    -- HEAD check: verify content type and get final URL after redirects.
    local _, status, headers = http.head(url, options)

    if status ~= 200 and status ~= 206 then
        ludo.logError("generic_direct: HEAD returned " .. tostring(status))
        return nil
    end

    -- Reject HTML responses (another plugin should handle those).
    local ct = headers["Content-Type"] or headers["content-type"] or ""
    if string.match(ct, "text/html") then
        ludo.logInfo("generic_direct: Content-Type is HTML, skipping")
        return nil
    end

    -- Use the effective (post-redirect) URL when available.
    local final_url = http.get_last_url()
    if final_url == "" then final_url = url end

    local id = ludo.newDownload(final_url,
                                ludo.getOutputDirectory(),
                                ludo.DOWNLOAD_NOW)
    ludo.logSuccess("generic_direct: download #" .. tostring(id)
                    .. " started for " .. final_url)
    return id
end

return plugin
