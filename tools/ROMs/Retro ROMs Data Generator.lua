-- Extract file listing from each retro system to CSV that can be loaded into "tools\Retro ROMs Downloader.lua"
-- https://archive.org/download/ni-roms/roms/

-- source : https://archive.org/download/ni-roms/ni-roms_files.xml
local retro_system = {
  { brand = "ACT", name = "Apricot PC Xi", size = 1513221 },
  { brand = "APF", name = "Imagination Machine", size = 121399842 },
  { brand = "APF", name = "MP-1000", size = 58890 },
  { brand = "Acorn", name = "Archimedes", size = 126653785 },
  { brand = "Acorn", name = "Atom (Tapes) (Bitstream)", size = 117120 },
  { brand = "Acorn", name = "Risc PC (Flux)", size = 290621388 },
  { brand = "Acorn RISC OS", name = "Flash Media (Misc)", size = 590705211 },
  { brand = "Amstrad", name = "CPC (Flux)", size = 85067110 },
  { brand = "Amstrad", name = "CPC (Misc)", size = 12727 },
  { brand = "Apple", name = "I (Tapes)", size = 63504003 },
  { brand = "Apple", name = "II (A2R)", size = 3739374440 },
  { brand = "Apple", name = "II (WOZ)", size = 132466 },
  { brand = "Apple", name = "II (Waveform)", size = 9024496 },
  { brand = "Apple", name = "II Plus (Flux)", size = 42726259 },
  { brand = "Apple", name = "II Plus (WOZ)", size = 552866 },
  { brand = "Apple", name = "IIGS (A2R)", size = 1759736656 },
  { brand = "Apple", name = "IIGS (WOZ)", size = 192545275 },
  { brand = "Apple", name = "IIe (A2R)", size = 22808528 },
  { brand = "Apple", name = "IIe (Kryoflux)", size = 27834188 },
  { brand = "Apple", name = "IIe (WOZ)", size = 425566 },
  { brand = "Apple", name = "Macintosh (A2R)", size = 3269041718 },
  { brand = "Apple", name = "Macintosh (BETA) (Bitstreams)", size = 457274 },
  { brand = "Apple", name = "Macintosh (BETA) (FluxDumps)", size = 3568821658 },
  { brand = "Apple", name = "Macintosh (DC42)", size = 13245381 },
  { brand = "Apple", name = "Macintosh (KryoFlux)", size = 839422921 },
  { brand = "Apple", name = "Macintosh (Uncategorized)", size = 2381721 },
  { brand = "Apple", name = "Macintosh (WOZ)", size = 457274 },
  { brand = "Apple", name = "Pippin (Floppies)", size = 177077 },
  { brand = "Arcade", name = "PC-based", size = 1689865886 },
  { brand = "Arduboy Inc", name = "Arduboy", size = 12442556 },
  { brand = "Atari", name = "2600", size = 23801211 },
  { brand = "Atari", name = "5200", size = 1222984 },
  { brand = "Atari", name = "7800 (A78)", size = 2351941 },
  { brand = "Atari", name = "7800 (BIN)", size = 6127920 },
  { brand = "Atari", name = "8-bit Family (Kryoflux)", size = 89094075 },
  { brand = "Atari", name = "8-bit Family", size = 428955 },
  { brand = "Atari", name = "Jaguar (ABS)", size = 12670736 },
  { brand = "Atari", name = "Jaguar (COF)", size = 12706826 },
  { brand = "Atari", name = "Jaguar (J64)", size = 389947704 },
  { brand = "Atari", name = "Jaguar (JAG)", size = 2242327 },
  { brand = "Atari", name = "Jaguar (ROM)", size = 389408270 },
  { brand = "Atari", name = "Lynx (BLL)", size = 1241042 },
  { brand = "Atari", name = "Lynx (LNX)", size = 14247640 },
  { brand = "Atari", name = "Lynx (LYX)", size = 28707829 },
  { brand = "Atari", name = "ST (Flux)", size = 128727198 },
  { brand = "Atari", name = "ST", size = 113837726 },
  { brand = "Bally", name = "Astrocade (Tapes) (WAV)", size = 6444 },
  { brand = "Bally", name = "Astrocade (Tapes)", size = 3285 },
  { brand = "Bally", name = "Astrocade", size = 295947 },
  { brand = "Bandai", name = "Design Master Denshi Mangajuku", size = 478363 },
  { brand = "Bandai", name = "Gundam RX-78", size = 1919566 },
  { brand = "Bandai", name = "WonderSwan Color", size = 201375266 },
  { brand = "Bandai", name = "WonderSwan", size = 126139611 },
  { brand = "Bandai", name = "Little Jammer (BIN)", size = 1884551 },
  { brand = "Bandai", name = "Little Jammer Pro (BIN)", size = 606955 },
  { brand = "Benesse", name = "Pocket Challenge V2", size = 153075487 },
  { brand = "Benesse", name = "Pocket Challenge W", size = 69959622 },
  { brand = "Bit Corporation", name = "Gamate", size = 3511646 },
  { brand = "Casio", name = "Loopy (BigEndian)", size = 16162137 },
  { brand = "Casio", name = "Loopy (LittleEndian)", size = 16472902 },
  { brand = "Casio", name = "PV-1000", size = 82607 },
  { brand = "Coleco", name = "ColecoVision", size = 2291246 },
  { brand = "Commodore", name = "Amiga (Bitstream)", size = 8901723 },
  { brand = "Commodore", name = "Amiga (Flux)", size = 1499982512 },
  { brand = "Commodore", name = "Amiga", size = 3544443670 },
  { brand = "Commodore", name = "Commodore 64 (Headerless)", size = 36979 },
  { brand = "Commodore", name = "Commodore 64 (PP)", size = 236578922 },
  { brand = "Commodore", name = "Commodore 64 (Tapes)", size = 106958404 },
  { brand = "Commodore", name = "Commodore 64", size = 7481088 },
  { brand = "Commodore", name = "Plus-4", size = 492169 },
  { brand = "Commodore", name = "VIC-20", size = 1722540 },
  { brand = "Digital Media Cartridge", name = "Firecore", size = 4752391 },
  { brand = "Emerson", name = "Arcadia 2001", size = 189638 },
  { brand = "Entex", name = "Adventure Vision", size = 14738 },
  { brand = "Epoch", name = "Game Pocket Computer", size = 35781 },
  { brand = "Epoch", name = "Super Cassette Vision", size = 699086 },
  { brand = "Fairchild", name = "Channel F", size = 75981 },
  { brand = "Fujitsu", name = "FM Towns (Flux)", size = 1585342033 },
  { brand = "Fujitsu", name = "FM Towns (HDM)", size = 3241779 },
  { brand = "Fujitsu", name = "FM-7 (Bitstream)", size = 1847696 },
  { brand = "Fujitsu", name = "FM-7 (Flux)", size = 143415497 },
  { brand = "Fujitsu", name = "FM-7 (Sector)", size = 40050449 },
  { brand = "Fujitsu", name = "FM-7 (Tapes) (Bitstream)", size = 8883 },
  { brand = "Fujitsu", name = "FM-7 (Tapes) (Waveform)", size = 109454953 },
  { brand = "Fujitsu", name = "FMR50 (Flux)", size = 98726450 },
  { brand = "Fukutake Publishing", name = "StudyBox", size = 1468615491 },
  { brand = "Funtech", name = "Super Acan", size = 11616639 },
  { brand = "GCE", name = "Vectrex", size = 232539 },
  { brand = "GamePark", name = "GP2X", size = 245240508 },
  { brand = "GamePark", name = "GP32", size = 211654407 },
  { brand = "Hartung", name = "Game Master", size = 165946 },
  { brand = "Hitachi", name = "S1 (Waveform)", size = 56287431 },
  { brand = "Interton", name = "VC 4000", size = 128010 },
  { brand = "Konami", name = "Picno", size = 7665963 },
  { brand = "LeapFrog", name = "Explorer", size = 300580873 },
  { brand = "LeapFrog", name = "LeapPad", size = 1329911 },
  { brand = "LeapFrog", name = "Leapster Learning Game System", size = 459592126 },
  { brand = "Luxor", name = "ABC 800 (Flux)", size = 71439605 },
  { brand = "Magnavox", name = "Odyssey 2", size = 405491 },
  { brand = "Mattel", name = "Intellivision", size = 1787855 },
  { brand = "Microsoft", name = "MSX", size = 22174052 },
  { brand = "Microsoft", name = "MSX2", size = 23962927 },
  { brand = "Milton-Bradley", name = "Omni (Waveform)", size = 9369841717 },
  { brand = "Mobile", name = "J2ME", size = 83988266 },
  { brand = "Mobile", name = "Palm OS (Digital)", size = 273775899 },
  { brand = "Mobile", name = "Palm OS", size = 34260182 },
  { brand = "Mobile", name = "Pocket PC (Digital)", size = 404039887 },
  { brand = "Mobile", name = "Pocket PC", size = 1501957 },
  { brand = "Mobile", name = "Symbian", size = 14643614 },
  { brand = "NEC", name = "PC Engine,TurboGrafx-16", size = 108358931 },
  { brand = "NEC", name = "PC Engine SuperGrafx", size = 2512197 },
  { brand = "NEC", name = "PC-88 (Flux)", size = 117168447 },
  { brand = "NEC", name = "PC-88 (KryoFlux)", size = 27661166 },
  { brand = "NEC", name = "PC-98 (Flux)", size = 527133707 },
  { brand = "NEC", name = "PC-98 (Greaseweazle)", size = 1205008 },
  { brand = "NEC", name = "PC-98 (HardDisk)", size = 28142688 },
  { brand = "NEC", name = "PC-98 (Uncategorized)", size = 4130157 },
  { brand = "NEC", name = "PC-98", size = 14046104 },
  { brand = "Nichibutsu", name = "My Vision (Mame)", size = 52820 },
  { brand = "Nichibutsu", name = "My Vision", size = 52073 },
  { brand = "Nintendo", name = "Family BASIC (Tapes)", size = 89064620 },
  { brand = "Nintendo", name = "Family Computer Disk System (FDS)", size = 18387237 },
  { brand = "Nintendo", name = "Family Computer Disk System (FDS2QD)", size = 15506819 },
  { brand = "Nintendo", name = "Family Computer Disk System (QD)", size = 14992180 },
  { brand = "Nintendo", name = "Family Computer Disk System (QD2FDS)", size = 9809512 },
  { brand = "Nintendo", name = "Family Computer Network System", size = 420189 },
  { brand = "Nintendo", name = "Game & Watch", size = 36675 },
  { brand = "Nintendo", name = "Game Boy Advance (Multiboot)", size = 8600183 },
  { brand = "Nintendo", name = "Game Boy Advance (Play-Yan)", size = 1855188 },
  { brand = "Nintendo", name = "Game Boy Advance (Video)", size = 3136842225 },
  { brand = "Nintendo", name = "Game Boy Advance (e-Reader)", size = 12618365 },
  { brand = "Nintendo", name = "Game Boy Advance", size = 14451190196 },
  { brand = "Nintendo", name = "Game Boy Color", size = 1034255828 },
  { brand = "Nintendo", name = "Game Boy", size = 256607123 },
  { brand = "Nintendo", name = "Kiosk Video Compact Flash (CardImage)", size = 2067499084 },
  { brand = "Nintendo", name = "Kiosk Video Compact Flash (Extracted)", size = 2009554033 },
  { brand = "Nintendo", name = "Nintendo 64 (BigEndian)", size = 15469211933 },
  { brand = "Nintendo", name = "Nintendo 64 (ByteSwapped)", size = 15636762163 },
  { brand = "Nintendo", name = "Nintendo 64 (Mario no Photopi SmartMedia)", size = 8592242 },
  { brand = "Nintendo", name = "Nintendo 64DD", size = 299425222 },
  { brand = "Nintendo", name = "Nintendo DS (DSvision SD cards)", size = 2465897138 },
  { brand = "Nintendo", name = "Nintendo DS (Download Play)", size = 1383900925 },
  { brand = "Nintendo", name = "Nintendo DSi (Decrypted)", size = 964891305 },
  { brand = "Nintendo", name = "Nintendo DSi (Digital) (CDN) (Decrypted)", size = 9400385948 },
  { brand = "Nintendo", name = "Nintendo DSi (Digital) (CDN) (Encrypted)", size = 14044058098 },
  { brand = "Nintendo", name = "Nintendo DSi (Digital)", size = 5279903782 },
  { brand = "Nintendo", name = "Nintendo DSi (Encrypted)", size = 858115540 },
  { brand = "Nintendo", name = "Nintendo Entertainment System (Headered)", size = 1261909450 },
  { brand = "Nintendo", name = "Nintendo Entertainment System (Unheadered)", size = 1262218050 },
  { brand = "Nintendo", name = "Pokemon Mini", size = 6089782 },
  { brand = "Nintendo", name = "Satellaview", size = 156272704 },
  { brand = "Nintendo", name = "Sufami Turbo", size = 4620821 },
  { brand = "Nintendo", name = "Super Nintendo Entertainment System", size = 3755288573 },
  { brand = "Nintendo", name = "Virtual Boy", size = 36100528 },
  { brand = "Nintendo", name = "Wallpapers", size = 3334259813 },
  { brand = "Nintendo", name = "amiibo", size = 667301 },
  { brand = "Nokia", name = "N-Gage (WIP)", size = 21327993 },
  { brand = "Philips", name = "Videopac+", size = 171355 },
  { brand = "Project EGG", name = "Project EGG", size = 12513724988 },
  { brand = "RCA", name = "Studio II", size = 20746 },
  { brand = "SNK", name = "NeoGeo Pocket Color", size = 63648079 },
  { brand = "SNK", name = "NeoGeo Pocket", size = 4620049 },
  { brand = "Sanyo", name = "MBC-550 (Flux)", size = 67948640 },
  { brand = "Sega", name = "32X", size = 322994515 },
  { brand = "Sega", name = "Beena", size = 290615937 },
  { brand = "Sega", name = "Dreamcast (Visual Memory Unit)", size = 11302 },
  { brand = "Sega", name = "Game Gear", size = 158435993 },
  { brand = "Sega", name = "Master System, Mark III", size = 113599980 },
  { brand = "Sega", name = "Mega Drive, Genesis", size = 2416464290 },
  { brand = "Sega", name = "PICO", size = 305374483 },
  { brand = "Sega", name = "SG-1000", size = 4045314 },
  { brand = "Seta", name = "Aleck64 (BigEndian)", size = 181637657 },
  { brand = "Seta", name = "Aleck64 (ByteSwapped)", size = 185403197 },
  { brand = "Sharp", name = "MZ-2200 (Waveform)", size = 14214604 },
  { brand = "Sharp", name = "MZ-700 (Waveform)", size = 444862314 },
  { brand = "Sharp", name = "X1 (Waveform)", size = 94190731 },
  { brand = "Sharp", name = "X68000 (Flux)", size = 47293538 },
  { brand = "Sinclair", name = "ZX Spectrum +3", size = 7683558 },
  { brand = "TeleNova", name = "Compis (Flux)", size = 58136205 },
  { brand = "Texas Instruments", name = "TI-99-4A (A2R)", size = 29757329 },
  { brand = "Tiger", name = "Game.com", size = 9864793 },
  { brand = "Tiger", name = "Gizmondo", size = 724963970 },
  { brand = "Toshiba", name = "Pasopia (BIN)", size = 70124 },
  { brand = "Toshiba", name = "Pasopia (WAV)", size = 118683585 },
  { brand = "Toshiba", name = "Visicom", size = 11455 },
  { brand = "VM Labs", name = "NUON (Digital)", size = 280268605 },
  { brand = "VTech", name = "CreatiVision", size = 216087 },
  { brand = "VTech", name = "V.Smile", size = 781923469 },
  { brand = "Watara", name = "Supervision", size = 1698245 },
  { brand = "Welback", name = "Mega Duck", size = 860121 },
  { brand = "Yamaha", name = "Copera", size = 5178072 },
  { brand = "Zeebo", name = "Zeebo", size = 731999639 },
  { brand = "iQue", name = "iQue (CDN)", size = 453606856 },
  { brand = "iQue", name = "iQue (Decrypted)", size = 230974392 }
}

local win_open = true
local _src = debug.getinfo(1, "S").source or ""
local TOOLS_DIR = _src:match("^@(.+)[\\/][^\\/]+$") or "tools"
local checked = {}
for i = 1, #retro_system do checked[i] = false end

-- local html_path = "tools/GBA.html"
-- local csv_path = "tools/GBA.csv"
function generate_csv(html_path, csv_path)
    local f = io.open(html_path, "r")
    if not f then ludo.logError("Cannot open " .. html_path); return end
    local html = f:read("*all")
    f:close()
    local out = io.open(csv_path, "w")
    if not out then ludo.logError("Cannot write " .. csv_path); return end
    out:write("game_name|game_ext|game_size\n")
    local count = 0
    local pos = 1
    while true do
    local s = html:find("<tr><td><a href=\"", pos)
    if not s then break end
    local href_start = s + 17
    local href_end = html:find("\"", href_start)
    local href = html:sub(href_start, href_end - 1)
    local name_start = html:find(">", href_end)
    local name_end = html:find("</a>", name_start)
    local name_display = html:sub(name_start + 1, name_end - 1)
    local ext = name_display:match("(%.[^%.]+)$") or ""
    name_display = ext ~= "" and name_display:sub(1, -#ext - 1) or name_display
    local size_tag = html:find("<td id=\"size\">", name_end)
    local size_end = html:find("</tr>", size_tag)
    local size_val = html:sub(size_tag + 14, size_end - 1)
    out:write(name_display .. "|" .. ext .. "|" .. size_val .. "\n")
    count = count + 1
    pos = size_end + 5
    end
    out:close()
    ludo.logInfo(string.format("Extracted %d rows to %s", count, csv_path))
end


-- retro_system Table model column indices (0-based).
local COL_CHECKBOX = 0
local COL_BRAND = 1
local COL_NAME = 2
local COL_SIZE = 3

local handler = {
    NumColumns  = function(m) return 4 end,
    NumRows = function(m) return #retro_system end,
    CellValue = function(m, row, col)
        local system = retro_system[row + 1]
        if col == COL_CHECKBOX then return checked[row + 1] or false end
        if col == COL_BRAND then return system.brand end
        if col == COL_NAME then return system.name end
        if col == COL_SIZE then return tostring(system.size) end
        return ""
    end,
    SetCellValue = function(m, row, col, val)
        if col == COL_CHECKBOX then checked[row + 1] = val end
    end,
}

http.set_cookie(TOOLS_DIR .. "/archive.org.txt")

-- Create the UI table to display the retro system data.
local win = ui.NewWindow("Retro Systems Data Generator", 800, 600, false)
win:SetMargined(1)
local root = ui.NewVerticalBox()

local retro_system_model = ui.NewTableModel(handler)
local retro_system_table = ui.NewTable(retro_system_model)
retro_system_table:AppendCheckboxColumn("#", COL_CHECKBOX, ui.TableModelColumnAlwaysEditable)
retro_system_table:AppendTextColumn("Brand", COL_BRAND, ui.TableModelColumnNeverEditable)
retro_system_table:AppendTextColumn("Name", COL_NAME, ui.TableModelColumnNeverEditable)
retro_system_table:AppendTextColumn("Size", COL_SIZE, ui.TableModelColumnNeverEditable)
retro_system_table:ColumnSetWidth(COL_CHECKBOX, 30)
retro_system_table:ColumnSetWidth(COL_BRAND, 150)
retro_system_table:ColumnSetWidth(COL_NAME, 400)
retro_system_table:ColumnSetWidth(COL_SIZE, 80)
root:Append(retro_system_table, true)

local generate_btn = ui.NewButton("Generate CSV")
root:Append(generate_btn, false)
win:SetChild(root)

generate_btn:OnClicked(function()
    local generated = 0
    for i, system in ipairs(retro_system) do
        if checked[i] then
            local csv_path = TOOLS_DIR .. "/" .. system.name .. ".csv"
            -- format url: https://archive.org/download/ni-roms/roms/ACT%20-%20Apricot%20PC%20Xi.zip/
            local url = "https://archive.org/download/ni-roms/roms/" .. http.url_encode(system.brand .. " - " .. system.name) .. ".zip/"
            local body, status = http.get(url, { timeout = 60 })
            if status ~= 200 or not body then
                ludo.logError("HTTP " .. tostring(status) .. " for " .. system.name)
            else
                local out = io.open(csv_path, "w")
                if not out then
                    ludo.logError("Cannot write " .. csv_path)
                else
                    out:write("game_name|game_ext|game_size\n")
                    local count = 0
                    local pos = 1
                    while true do
                        local s = body:find("<tr><td><a href=\"", pos)
                        if not s then break end
                        local href_start = s + 17
                        local href_end = body:find("\"", href_start)
                        local href = body:sub(href_start, href_end - 1)
                        local name_start = body:find(">", href_end)
                        local name_end = body:find("</a>", name_start)
                        local name_display = body:sub(name_start + 1, name_end - 1)
                        local ext = name_display:match("(%.[^%.]+)$") or ""
                        name_display = ext ~= "" and name_display:sub(1, -#ext - 1) or name_display
                        local size_tag = body:find("<td id=\"size\">", name_end)
                        local size_end = body:find("</tr>", size_tag)
                        local size_val = body:sub(size_tag + 14, size_end - 1)
                        out:write(name_display .. "|" .. ext .. "|" .. size_val .. "\n")
                        count = count + 1
                        pos = size_end + 5
                    end
                    out:close()
                    ludo.logInfo(string.format("Wrote %d entries to %s for %s", count, csv_path, system.name))
                    generated = generated + 1
                end
            end
        end
    end
    ludo.logInfo(string.format("Generated CSV for %d system(s)", generated))
end, nil)

win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
