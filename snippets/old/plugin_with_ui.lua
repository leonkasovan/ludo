local ui = require("ui")
local plugin = {}

function plugin.validate(url)
    return string.find(url, "chooser%.example%.com") ~= nil
end

function plugin.process(url)
    ludo.logInfo("Fetching options from: " .. url)

    local body, status = http.get(url)
    if status ~= 200 then
        ludo.logError("Failed: HTTP " .. status)
        return
    end

    -- Parse available files
    local files = {}
    for name, link in string.gmatch(body, '<a href="([^"]+)">([^<]+)</a>') do
        table.insert(files, { name = name, url = link })
    end

    -- Show a selection dialog
    ui.Init()
    local win = ui.NewWindow("Select Files", 300, 200, false)
    win:SetMargined(1)

    local vbox = ui.NewVerticalBox()
    vbox:SetPadded(1)
    vbox:Append(ui.NewLabel("Found " .. #files .. " file(s):"))

    for _, file in ipairs(files) do
        local btn = ui.NewButton(file.name)
        btn:OnClicked(function(b, data)
            ludo.newDownload(data)
            ludo.logSuccess("Queued: " .. data)
        end, file.url)
        vbox:Append(btn)
    end

    local closeBtn = ui.NewButton("Close")
    closeBtn:OnClicked(function() ui.Quit() end, nil)
    vbox:Append(closeBtn)

    win:SetChild(vbox)
    win:Show()
    ui.Main()
    ui.Uninit()
end

return plugin