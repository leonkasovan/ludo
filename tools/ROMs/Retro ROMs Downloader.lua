-- Retro ROMs Downloader (SNES, Sega Genesis, Game Boy Advance)
-- Downloads Retro ROMs from the archive.org.
-- https://archive.org/download/ni-roms/roms/
-- Requires lualib/ftcsv.lua for CSV parsing.
-- CSV db files must be in the same directory as this script.
-- System id based on https://api.screenscraper.fr/api2/systemesListe.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test

local ui    = require("ui")
local ftcsv = require("ftcsv")
local win_open = true
local window_alive = true

-- Determine the tools directory from this script's own path.
local _src = debug.getinfo(1, "S").source or ""
local TOOLS_DIR = _src:match("^@(.+)[\\/][^\\/]+$") or "tools"
local MAX_RESULTS = 99
local search_results = {}

-- Set to true to only generate config for systems with used = 1
local retro_systems = {
  { brand = "Acorn", name = "Archimedes", size = 126653785, id = 84, used = 0 },
  { brand = "Acorn", name = "Atom (Tapes) (Bitstream)", size = 117120, id = 36, used = 0 },
  { brand = "Acorn", name = "Risc PC (Flux)", size = 290621388, id = 0, used = 0 },
  { brand = "Acorn RISC OS", name = "Flash Media (Misc)", size = 590705211, id = 0, used = 0 },
  { brand = "Amstrad", name = "CPC (Flux)", size = 85067110, id = 65, used = 0 },
  { brand = "Amstrad", name = "CPC (Misc)", size = 12727, id = 65, used = 0 },
  { brand = "Apple", name = "I (Tapes)", size = 63504003, id = 86, used = 0 },
  { brand = "Apple", name = "II (A2R)", size = 3739374440, id = 86, used = 0 },
  { brand = "Apple", name = "II (WOZ)", size = 132466, id = 86, used = 0 },
  { brand = "Apple", name = "II (Waveform)", size = 9024496, id = 86, used = 0 },
  { brand = "Apple", name = "II Plus (Flux)", size = 42726259, id = 86, used = 0 },
  { brand = "Apple", name = "II Plus (WOZ)", size = 552866, id = 86, used = 0 },
  { brand = "Apple", name = "IIGS (A2R)", size = 1759736656, id = 86, used = 0 },
  { brand = "Apple", name = "IIGS (WOZ)", size = 192545275, id = 86, used = 0 },
  { brand = "Apple", name = "IIe (A2R)", size = 22808528, id = 86, used = 0 },
  { brand = "Apple", name = "IIe (Kryoflux)", size = 27834188, id = 86, used = 0 },
  { brand = "Apple", name = "IIe (WOZ)", size = 425566, id = 86, used = 0 },
  { brand = "Apple", name = "Macintosh (A2R)", size = 3269041718, id = 146, used = 0 },
  { brand = "Apple", name = "Macintosh (BETA) (Bitstreams)", size = 457274, id = 146, used = 0 },
  { brand = "Apple", name = "Macintosh (BETA) (FluxDumps)", size = 3568821658, id = 146, used = 0 },
  { brand = "Apple", name = "Macintosh (DC42)", size = 13245381, id = 146, used = 0 },
  { brand = "Apple", name = "Macintosh (KryoFlux)", size = 839422921, id = 146, used = 0 },
  { brand = "Apple", name = "Macintosh (Uncategorized)", size = 2381721, id = 146, used = 0 },
  { brand = "Apple", name = "Macintosh (WOZ)", size = 457274, id = 146, used = 0 },
  { brand = "Apple", name = "Pippin (Floppies)", size = 177077, id = 0, used = 0 },
  { brand = "Arcade", name = "PC-based", size = 1689865886, id = 0, used = 0 },
  { brand = "Arduboy Inc", name = "Arduboy", size = 12442556, id = 263, used = 0 },
  { brand = "Atari", name = "2600", size = 23801211, id = 26, used = 0 },
  { brand = "Atari", name = "5200", size = 1222984, id = 40, used = 0 },
  { brand = "Atari", name = "7800 (A78)", size = 2351941, id = 41, used = 0 },
  { brand = "Atari", name = "7800 (BIN)", size = 6127920, id = 41, used = 0 },
  { brand = "Atari", name = "8-bit Family (Kryoflux)", size = 89094075, id = 43, used = 0 },
  { brand = "Atari", name = "8-bit Family", size = 428955, id = 43, used = 0 },
  { brand = "Atari", name = "Jaguar (ABS)", size = 12670736, id = 27, used = 0 },
  { brand = "Atari", name = "Jaguar (COF)", size = 12706826, id = 27, used = 0 },
  { brand = "Atari", name = "Jaguar (J64)", size = 389947704, id = 27, used = 0 },
  { brand = "Atari", name = "Jaguar (JAG)", size = 2242327, id = 27, used = 0 },
  { brand = "Atari", name = "Jaguar (ROM)", size = 389408270, id = 27, used = 0 },
  { brand = "Atari", name = "Lynx (BLL)", size = 1241042, id = 28, used = 0 },
  { brand = "Atari", name = "Lynx (LNX)", size = 14247640, id = 28, used = 0 },
  { brand = "Atari", name = "Lynx (LYX)", size = 28707829, id = 28, used = 0 },
  { brand = "Atari", name = "ST (Flux)", size = 128727198, id = 42, used = 0 },
  { brand = "Atari", name = "ST", size = 113837726, id = 42, used = 0 },
  { brand = "Bally", name = "Astrocade (Tapes) (WAV)", size = 6444, id = 44, used = 0 },
  { brand = "Bally", name = "Astrocade (Tapes)", size = 3285, id = 44, used = 0 },
  { brand = "Bally", name = "Astrocade", size = 295947, id = 44, used = 0 },
  { brand = "Bandai", name = "Design Master Denshi Mangajuku", size = 478363, id = 0, used = 0 },
  { brand = "Bandai", name = "Gundam RX-78", size = 1919566, id = 0, used = 0 },
  { brand = "Bandai", name = "WonderSwan Color", size = 201375266, id = 45, used = 1, sname = "WS Color" },
  { brand = "Bandai", name = "WonderSwan", size = 126139611, id = 45, used = 0 },
  { brand = "Bandai", name = "Little Jammer (BIN)", size = 1884551, id = 0, used = 0 },
  { brand = "Bandai", name = "Little Jammer Pro (BIN)", size = 606955, id = 0, used = 0 },
  { brand = "Benesse", name = "Pocket Challenge V2", size = 153075487, id = 237, used = 0 },
  { brand = "Benesse", name = "Pocket Challenge W", size = 69959622, id = 0, used = 0 },
  { brand = "Bit Corporation", name = "Gamate", size = 3511646, id = 266, used = 0 },
  { brand = "Casio", name = "Loopy (BigEndian)", size = 16162137, id = 98, used = 0 },
  { brand = "Casio", name = "Loopy (LittleEndian)", size = 16472902, id = 98, used = 0 },
  { brand = "Casio", name = "PV-1000", size = 82607, id = 74, used = 0 },
  { brand = "Coleco", name = "ColecoVision", size = 2291246, id = 48, used = 0 },
  { brand = "Commodore", name = "Amiga (Bitstream)", size = 8901723, id = 64, used = 0 },
  { brand = "Commodore", name = "Amiga (Flux)", size = 1499982512, id = 64, used = 0 },
  { brand = "Commodore", name = "Amiga", size = 3544443670, id = 64, used = 0 },
  { brand = "Commodore", name = "Commodore 64 (Headerless)", size = 36979, id = 66, used = 0 },
  { brand = "Commodore", name = "Commodore 64 (PP)", size = 236578922, id = 66, used = 0 },
  { brand = "Commodore", name = "Commodore 64 (Tapes)", size = 106958404, id = 66, used = 0 },
  { brand = "Commodore", name = "Commodore 64", size = 7481088, id = 66, used = 0 },
  { brand = "Commodore", name = "Plus-4", size = 492169, id = 99, used = 0 },
  { brand = "Commodore", name = "VIC-20", size = 1722540, id = 73, used = 0 },
  { brand = "Digital Media Cartridge", name = "Firecore", size = 4752391, id = 0, used = 0 },
  { brand = "Emerson", name = "Arcadia 2001", size = 189638, id = 94, used = 0 },
  { brand = "Entex", name = "Adventure Vision", size = 14738, id = 78, used = 0 },
  { brand = "Epoch", name = "Game Pocket Computer", size = 35781, id = 95, used = 0 },
  { brand = "Epoch", name = "Super Cassette Vision", size = 699086, id = 67, used = 0 },
  { brand = "Fairchild", name = "Channel F", size = 75981, id = 80, used = 0 },
  { brand = "Fujitsu", name = "FM Towns (Flux)", size = 1585342033, id = 253, used = 0 },
  { brand = "Fujitsu", name = "FM Towns (HDM)", size = 3241779, id = 253, used = 0 },
  { brand = "Fujitsu", name = "FM-7 (Bitstream)", size = 1847696, id = 97, used = 0 },
  { brand = "Fujitsu", name = "FM-7 (Flux)", size = 143415497, id = 97, used = 0 },
  { brand = "Fujitsu", name = "FM-7 (Sector)", size = 40050449, id = 97, used = 0 },
  { brand = "Fujitsu", name = "FM-7 (Tapes) (Bitstream)", size = 8883, id = 97, used = 0 },
  { brand = "Fujitsu", name = "FM-7 (Tapes) (Waveform)", size = 109454953, id = 97, used = 0 },
  { brand = "Fujitsu", name = "FMR50 (Flux)", size = 98726450, id = 0, used = 0 },
  { brand = "Fukutake Publishing", name = "StudyBox", size = 1468615491, id = 0, used = 0 },
  { brand = "Funtech", name = "Super Acan", size = 11616639, id = 100, used = 0 },
  { brand = "GCE", name = "Vectrex", size = 232539, id = 102, used = 0 },
  { brand = "GamePark", name = "GP2X", size = 245240508, id = 0, used = 0 },
  { brand = "GamePark", name = "GP32", size = 211654407, id = 101, used = 0 },
  { brand = "Hartung", name = "Game Master", size = 165946, id = 103, used = 0 },
  { brand = "Hitachi", name = "S1 (Waveform)", size = 56287431, id = 0, used = 0 },
  { brand = "Interton", name = "VC 4000", size = 128010, id = 281, used = 0 },
  { brand = "Konami", name = "Picno", size = 7665963, id = 0, used = 0 },
  { brand = "LeapFrog", name = "Explorer", size = 300580873, id = 0, used = 0 },
  { brand = "LeapFrog", name = "LeapPad", size = 1329911, id = 0, used = 0 },
  { brand = "LeapFrog", name = "Leapster Learning Game System", size = 459592126, id = 0, used = 0 },
  { brand = "Luxor", name = "ABC 800 (Flux)", size = 71439605, id = 0, used = 0 },
  { brand = "Magnavox", name = "Odyssey 2", size = 405491, id = 104, used = 0 },
  { brand = "Mattel", name = "Intellivision", size = 1787855, id = 115, used = 0 },
  { brand = "Microsoft", name = "MSX", size = 22174052, id = 113, used = 0 },
  { brand = "Microsoft", name = "MSX2", size = 23962927, id = 113, used = 0 },
  { brand = "Milton-Bradley", name = "Omni (Waveform)", size = 9369841717, id = 0, used = 0 },
  { brand = "Mobile", name = "J2ME", size = 83988266, id = 302, used = 0 },
  { brand = "Mobile", name = "Palm OS (Digital)", size = 273775899, id = 219, used = 0 },
  { brand = "Mobile", name = "Palm OS", size = 34260182, id = 219, used = 0 },
  { brand = "Mobile", name = "Pocket PC (Digital)", size = 404039887, id = 0, used = 0 },
  { brand = "Mobile", name = "Pocket PC", size = 1501957, id = 0, used = 0 },
  { brand = "Mobile", name = "Symbian", size = 14643614, id = 0, used = 0 },
  { brand = "NEC", name = "PC Engine,TurboGrafx-16", size = 108358931, id = 31, used = 0 },
  { brand = "NEC", name = "PC Engine SuperGrafx", size = 2512197, id = 105, used = 0 },
  { brand = "NEC", name = "PC-88 (Flux)", size = 117168447, id = 221, used = 0 },
  { brand = "NEC", name = "PC-88 (KryoFlux)", size = 27661166, id = 221, used = 0 },
  { brand = "NEC", name = "PC-98 (Flux)", size = 527133707, id = 208, used = 0 },
  { brand = "NEC", name = "PC-98 (Greaseweazle)", size = 1205008, id = 208, used = 0 },
  { brand = "NEC", name = "PC-98 (HardDisk)", size = 28142688, id = 208, used = 0 },
  { brand = "NEC", name = "PC-98 (Uncategorized)", size = 4130157, id = 208, used = 0 },
  { brand = "NEC", name = "PC-98", size = 14046104, id = 208, used = 0 },
  { brand = "Nichibutsu", name = "My Vision (Mame)", size = 52820, id = 305, used = 0 },
  { brand = "Nichibutsu", name = "My Vision", size = 52073, id = 305, used = 0 },
  { brand = "Nintendo", name = "Family BASIC (Tapes)", size = 89064620, id = 3, used = 0 },
  { brand = "Nintendo", name = "Family Computer Disk System (FDS)", size = 18387237, id = 3, used = 0 },
  { brand = "Nintendo", name = "Family Computer Disk System (FDS2QD)", size = 15506819, id = 3, used = 0 },
  { brand = "Nintendo", name = "Family Computer Disk System (QD)", size = 14992180, id = 3, used = 0 },
  { brand = "Nintendo", name = "Family Computer Disk System (QD2FDS)", size = 9809512, id = 3, used = 0 },
  { brand = "Nintendo", name = "Family Computer Network System", size = 420189, id = 3, used = 0 },
  { brand = "Nintendo", name = "Game & Watch", size = 36675, id = 52, used = 0 },
  { brand = "Nintendo", name = "Game Boy Advance (Multiboot)", size = 8600183, id = 12, used = 0 },
  { brand = "Nintendo", name = "Game Boy Advance (Play-Yan)", size = 1855188, id = 12, used = 0 },
  { brand = "Nintendo", name = "Game Boy Advance (Video)", size = 3136842225, id = 12, used = 0 },
  { brand = "Nintendo", name = "Game Boy Advance (e-Reader)", size = 12618365, id = 12, used = 0 },
  { brand = "Nintendo", name = "Game Boy Advance", size = 14451190196, id = 12, used = 1, sname = "GBA" },
  { brand = "Nintendo", name = "Game Boy Color", size = 1034255828, id = 9, used = 0 },
  { brand = "Nintendo", name = "Game Boy", size = 256607123, id = 9, used = 0 },
  { brand = "Nintendo", name = "Kiosk Video Compact Flash (CardImage)", size = 2067499084, id = 0, used = 0 },
  { brand = "Nintendo", name = "Kiosk Video Compact Flash (Extracted)", size = 2009554033, id = 0, used = 0 },
  { brand = "Nintendo", name = "Nintendo 64 (BigEndian)", size = 15469211933, id = 14, used = 1, sname = "N64" },
  { brand = "Nintendo", name = "Nintendo 64 (ByteSwapped)", size = 15636762163, id = 14, used = 0 },
  { brand = "Nintendo", name = "Nintendo 64 (Mario no Photopi SmartMedia)", size = 8592242, id = 14, used = 0 },
  { brand = "Nintendo", name = "Nintendo 64DD", size = 299425222, id = 14, used = 0 },
  { brand = "Nintendo", name = "Nintendo DS (DSvision SD cards)", size = 2465897138, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo DS (Download Play)", size = 1383900925, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo DSi (Decrypted)", size = 964891305, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo DSi (Digital) (CDN) (Decrypted)", size = 9400385948, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo DSi (Digital) (CDN) (Encrypted)", size = 14044058098, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo DSi (Digital)", size = 5279903782, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo DSi (Encrypted)", size = 858115540, id = 15, used = 0 },
  { brand = "Nintendo", name = "Nintendo Entertainment System (Headered)", size = 1261909450, id = 3, used = 1, sname = "NES" },
  { brand = "Nintendo", name = "Nintendo Entertainment System (Unheadered)", size = 1262218050, id = 3, used = 0 },
  { brand = "Nintendo", name = "Pokemon Mini", size = 6089782, id = 211, used = 0 },
  { brand = "Nintendo", name = "Satellaview", size = 156272704, id = 4, used = 0 },
  { brand = "Nintendo", name = "Sufami Turbo", size = 4620821, id = 4, used = 0 },
  { brand = "Nintendo", name = "Super Nintendo Entertainment System", size = 3755288573, id = 4, used = 1, sname = "SNES" },
  { brand = "Nintendo", name = "Virtual Boy", size = 36100528, id = 11, used = 0 },
  { brand = "Nintendo", name = "Wallpapers", size = 3334259813, id = 0, used = 0 },
  { brand = "Nintendo", name = "amiibo", size = 667301, id = 0, used = 0 },
  { brand = "Nokia", name = "N-Gage (WIP)", size = 21327993, id = 30, used = 0 },
  { brand = "Philips", name = "Videopac+", size = 171355, id = 104, used = 0 },
  { brand = "Project EGG", name = "Project EGG", size = 12513724988, id = 0, used = 0 },
  { brand = "RCA", name = "Studio II", size = 20746, id = 0, used = 0 },
  { brand = "SNK", name = "NeoGeo Pocket Color", size = 63648079, id = 82, used = 0 },
  { brand = "SNK", name = "NeoGeo Pocket", size = 4620049, id = 82, used = 0 },
  { brand = "Sanyo", name = "MBC-550 (Flux)", size = 67948640, id = 0, used = 0 },
  { brand = "Sega", name = "32X", size = 322994515, id = 19, used = 0 },
  { brand = "Sega", name = "Beena", size = 290615937, id = 0, used = 0 },
  { brand = "Sega", name = "Dreamcast (Visual Memory Unit)", size = 11302, id = 23, used = 0 },
  { brand = "Sega", name = "Game Gear", size = 158435993, id = 21, used = 0 },
  { brand = "Sega", name = "Master System - Mark III", size = 113599980, id = 2, used = 0 },
  { brand = "Sega", name = "Mega Drive - Genesis", size = 2416464290, id = 1, used = 1, sname = "Genesis" },
  { brand = "Sega", name = "PICO", size = 305374483, id = 250, used = 0 },
  { brand = "Sega", name = "SG-1000", size = 4045314, id = 109, used = 0 },
  { brand = "Seta", name = "Aleck64 (BigEndian)", size = 181637657, id = 0, used = 0 },
  { brand = "Seta", name = "Aleck64 (ByteSwapped)", size = 185403197, id = 0, used = 0 },
  { brand = "Sharp", name = "MZ-2200 (Waveform)", size = 14214604, id = 0, used = 0 },
  { brand = "Sharp", name = "MZ-700 (Waveform)", size = 444862314, id = 0, used = 0 },
  { brand = "Sharp", name = "X1 (Waveform)", size = 94190731, id = 220, used = 0 },
  { brand = "Sharp", name = "X68000 (Flux)", size = 47293538, id = 79, used = 0 },
  { brand = "Sinclair", name = "ZX Spectrum +3", size = 7683558, id = 76, used = 0 },
  { brand = "TeleNova", name = "Compis (Flux)", size = 58136205, id = 0, used = 0 },
  { brand = "Texas Instruments", name = "TI-99-4A (A2R)", size = 29757329, id = 205, used = 0 },
  { brand = "Tiger", name = "Game.com", size = 9864793, id = 121, used = 0 },
  { brand = "Tiger", name = "Gizmondo", size = 724963970, id = 0, used = 0 },
  { brand = "Toshiba", name = "Pasopia (BIN)", size = 70124, id = 0, used = 0 },
  { brand = "Toshiba", name = "Pasopia (WAV)", size = 118683585, id = 0, used = 0 },
  { brand = "Toshiba", name = "Visicom", size = 11455, id = 0, used = 0 },
  { brand = "VM Labs", name = "NUON (Digital)", size = 280268605, id = 0, used = 0 },
  { brand = "VTech", name = "CreatiVision", size = 216087, id = 241, used = 0 },
  { brand = "VTech", name = "V.Smile", size = 781923469, id = 120, used = 0 },
  { brand = "Watara", name = "Supervision", size = 1698245, id = 207, used = 0 },
  { brand = "Welback", name = "Mega Duck", size = 860121, id = 90, used = 0 },
  { brand = "Yamaha", name = "Copera", size = 5178072, id = 0, used = 0 },
  { brand = "Zeebo", name = "Zeebo", size = 731999639, id = 0, used = 0 },
  { brand = "iQue", name = "iQue (CDN)", size = 453606856, id = 0, used = 0 },
  { brand = "iQue", name = "iQue (Decrypted)", size = 230974392, id = 0, used = 0 },
}

local function trim(s)
    return (s or ""):match("^%s*(.-)%s*$") or ""
end

local function format_size(s)
    local n = tonumber(s)
    if not n or n == 0 then return "?" end
    if n >= 1073741824 then return string.format("%.2f GB", n / 1073741824)
    elseif n >= 1048576  then return string.format("%.2f MB", n / 1048576)
    elseif n >= 1024     then return string.format("%.2f KB", n / 1024)
    else return tostring(n) .. " B" end
end

local function search_system(system, query, results)
    local filepath = TOOLS_DIR .. "/" .. system.file
    ludo.logInfo("Searching using " .. filepath)
    local ok, iter = pcall(ftcsv.parseLine, filepath, "|")
    if not ok then
        ludo.logInfo("Retro ROMs Downloader: cannot open " .. filepath .. ": " .. tostring(iter))
        return
    end
    local q_lower = query:lower()
    local words = {}
    for w in q_lower:gmatch("%S+") do
        table.insert(words, w)
    end
    for _, row in iter do
        if #results >= MAX_RESULTS then break end
        local name = trim(row[system.col_name])
        local name_lower = name:lower()
        local all_match = true
        for _, w in ipairs(words) do
            if not name_lower:find(w, 1, true) then
                all_match = false
                break
            end
        end
        if all_match then
            local ext = trim(row[system.col_ext])
            local sz  = trim(row[system.col_size])
            table.insert(results, {
                system  = system.name,
                game_name = name,
                game_ext  = ext,
                rom_size  = sz,
                game_info = format_size(sz),
                rom_url   = system.source_url .. "/" .. http.url_encode(name .. ext),
            })
        end
    end
end

function generate_csv(system, f_csv, callback)
    local url = "https://archive.org/download/ni-roms/roms/" .. http.url_encode(system.brand .. " - " .. system.name) .. ".zip/"
    http.get_async(url, { timeout = 60 }, function(html, status)
        if status ~= 200 or not html then
            ludo.logError("HTTP " .. tostring(status) .. " for " .. system.name)
            if callback then callback(false) end
            return
        end
        f_csv:write("game_name|game_ext|game_size\n")
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
            -- replace &amp; with & and &nbsp; with space in name_display
            name_display = name_display:gsub("&amp;", "&"):gsub("&nbsp;", " ")
            f_csv:write(name_display .. "|" .. ext .. "|" .. size_val .. "\n")
            count = count + 1
            pos = size_end + 5
        end
        ludo.logInfo(string.format("Extracted %d rows to %q", count, system.name .. ".csv"))
        if callback then callback(true) end
    end)
end

-- Table model column indices (0-based, matches AppendTextColumn order).
local COL_NUM      = 0
local COL_SYSTEM = 1
local COL_NAME     = 2
local COL_INFO     = 3

local handler = {
    NumColumns  = function(m) return 4 end,
    ColumnType  = function(m, col) return ui.TableValueTypeString end,
    NumRows     = function(m) return #search_results end,
    CellValue   = function(m, row, col)
        local r = search_results[row + 1]
        if not r then return "" end
        if     col == COL_NUM      then return tostring(row + 1)
        elseif col == COL_SYSTEM then return r.system or ""
        elseif col == COL_NAME     then return r.game_name or ""
        elseif col == COL_INFO     then return r.game_info or ""
        end
        return ""
    end,
    SetCellValue = function(m, row, col, val) end,
}

local model = ui.NewTableModel(handler)
http.set_cookie(TOOLS_DIR .. "/archive.org.txt")

local win = ui.NewWindow("Retro ROMs Downloader", 860, 600, false)
win:SetMargined(1)

local function setup_ui(SYSTEMS)
    local root = ui.NewVerticalBox()
    root:SetPadded(1)

    local sys_group = ui.NewGroup("System")
    sys_group:SetMargined(1)
    local sys_box = ui.NewHorizontalBox()
    sys_box:SetPadded(1)
    local sys_cbs = {}
    for _, p in ipairs(SYSTEMS) do
        local system_name = p.sname or p.name
        local cb = ui.NewCheckbox(system_name)
        cb:SetChecked(1)
        sys_cbs[system_name] = cb
        sys_box:Append(cb, false)
    end
    sys_group:SetChild(sys_box)
    root:Append(sys_group, false)

    local search_hbox = ui.NewHorizontalBox()
    search_hbox:SetPadded(1)
    search_hbox:Append(ui.NewLabel("Search:"), false)
    local search_entry = ui.NewEntry()
    local search_btn   = ui.NewButton("  Search  ")
    search_hbox:Append(search_entry, true)
    search_hbox:Append(search_btn, false)
    root:Append(search_hbox, false)

    local status_lbl = ui.NewLabel("Select systems, type a game name then click Search.")
    root:Append(status_lbl, false)

    root:Append(ui.NewLabel("Results (up to " .. MAX_RESULTS .. "):"), false)
    local results_tbl = ui.NewTable(model)
    results_tbl:AppendTextColumn("#",        COL_NUM,      ui.TableModelColumnNeverEditable)
    results_tbl:AppendTextColumn("System", COL_SYSTEM, ui.TableModelColumnNeverEditable)
    results_tbl:AppendTextColumn("Name",     COL_NAME,     ui.TableModelColumnNeverEditable)
    results_tbl:AppendTextColumn("Info",     COL_INFO,     ui.TableModelColumnNeverEditable)
    results_tbl:SetSelectionMode(ui.TableSelectionModeZeroOrOne)
    results_tbl:ColumnSetWidth(0, 30)
    results_tbl:ColumnSetWidth(1, 160)
    results_tbl:ColumnSetWidth(2, 550)
    results_tbl:ColumnSetWidth(3, 100)
    root:Append(results_tbl, true)

    local det_group = ui.NewGroup("Rom Details")
    det_group:SetMargined(1)
    local det_box = ui.NewHorizontalBox()
    det_box:SetPadded(1)
    local entry_info = ui.NewMultilineEntry()
    entry_info:SetReadOnly(1)
    det_box:Append(entry_info, true)
    local ss_img = ui.NewStaticImage()
    det_box:Append(ss_img, true)
    det_group:SetChild(det_box)
    root:Append(det_group, true)

    local dl_btn = ui.NewButton("  Download Selected  ")
    root:Append(dl_btn, false)

    win:SetChild(root)

    local current_details_id = nil
    local function update_details(idx)
        if not idx or idx < 1 or idx > #search_results then
            entry_info:SetText("")
            ss_img:Clear()
            current_details_id = nil
            return
        end
        local r = search_results[idx]
        current_details_id = r.game_name
        ss_img:Clear()
        local scrape_url = nil
        for _, p in ipairs(SYSTEMS) do
            if p.name == r.system then
                scrape_url = p.scrape_url .. http.url_encode(r.game_name:gsub("%s*%(.*%)%s*", " "):gsub("^%s*(.-)%s*$", "%1"):gsub("%s+", " "))
                break
            end
        end
        if scrape_url then
            entry_info:SetText("Loading details...\n" .. scrape_url)
            ludo.logInfo("Fetching details for " .. r.game_name .. " from " .. scrape_url)
            http.get_async(scrape_url, { timeout = 10 }, function(body, status)
                if not window_alive or current_details_id ~= r.game_name then return end
                if status ~= 200 or not body or #body == 0 then
                    entry_info:SetText("Failed to fetch details (HTTP " .. tostring(status) .. ")\n" .. scrape_url)
                    return
                end
                local ok, data = pcall(json.decode, body)
                if not ok or not data or not data.response or not data.response.jeux then
                    entry_info:SetText("Failed to parse response.")
                    return
                end
                local jeu = data.response.jeux[1]
                if not jeu or not jeu.noms or #jeu.noms == 0 then
                    entry_info:SetText("No details found." .. "\n" .. scrape_url)
                    return
                end
                local lines = {}
                local names = jeu.noms
                if names and #names > 0 then
                    local name_text = ""
                    for _, n in ipairs(names) do
                        if n.region == "en" or n.region == "ss" or n.region == "wor" then
                            name_text = n.text; break
                        end
                    end
                    if name_text == "" then name_text = names[1].text end
                    table.insert(lines, "Name: " .. name_text)
                end
                if jeu.developpeur and jeu.developpeur.text ~= "" then
                    table.insert(lines, "Developer: " .. jeu.developpeur.text)
                end
                if jeu.editeur and jeu.editeur.text ~= "" then
                    table.insert(lines, "Publisher: " .. jeu.editeur.text)
                end
                if jeu.dates and #jeu.dates > 0 and jeu.dates[1].text ~= "" then
                    table.insert(lines, "Year: " .. jeu.dates[1].text)
                end
                if jeu.genres and #jeu.genres > 0 then
                    for _, g in ipairs(jeu.genres) do
                        if g.noms and g.principale == "1" then
                            for _, n in ipairs(g.noms) do
                                if n.langue == "en" then
                                    table.insert(lines, "Genre: " .. n.text); break
                                end
                            end
                            break
                        end
                    end
                end
                if jeu.joueurs and jeu.joueurs.text ~= "" then
                    table.insert(lines, "Players: " .. jeu.joueurs.text)
                end
                if jeu.note and jeu.note.text ~= "" then
                    table.insert(lines, "Rating: " .. jeu.note.text .. "/20")
                end
                if jeu.resolution and jeu.resolution ~= "" then
                    table.insert(lines, "Resolution: " .. jeu.resolution)
                end
                if jeu.synopsis and #jeu.synopsis > 0 then
                    local synopsis = ""
                    for _, s in ipairs(jeu.synopsis) do
                        if s.langue == "en" then synopsis = s.text; break end
                    end
                    if synopsis == "" then synopsis = jeu.synopsis[1].text end
                    table.insert(lines, "")
                    table.insert(lines, "Synopsis:")
                    synopsis = synopsis:gsub("&quot;", '"')
                    table.insert(lines, synopsis:match("^%s*(.-)%s*$"))
                end
                local ss_url = nil
                if jeu.medias and #jeu.medias > 0 then
                    for _, m in ipairs(jeu.medias) do
                        if m.type == "ss" and m.url and m.url ~= "" then
                            ss_url = m.url; break
                        end
                    end
                end
                entry_info:SetText(table.concat(lines, "\n"))
                if ss_url then
                    http.get_async(ss_url, { timeout = 10 }, function(img_body, img_status)
                        if not window_alive or current_details_id ~= r.game_name then return end
                        if img_status ~= 200 or not img_body or #img_body == 0 then return end
                        ss_img:SetImageFromMemory(img_body)
                    end)
                end
            end)
        else
            entry_info:SetText("Details not available.")
        end
    end

    results_tbl:OnSelectionChanged(function()
        local sel = results_tbl:GetSelection()
        if sel and #sel > 0 then
            update_details(sel[1] + 1)
        else
            update_details(nil)
        end
    end)

    local function do_search()
        local query = search_entry:Text()
        if not query or query:match("^%s*$") then
            status_lbl:SetText("Please enter a search term.")
            return
        end
        local any = false
        local system_name
        for _, p in ipairs(SYSTEMS) do
            system_name = p.sname or p.name
            if sys_cbs[system_name]:Checked() == 1 then any = true; break end
        end
        if not any then
            status_lbl:SetText("Please select at least one system.")
            return
        end

        status_lbl:SetText("Searching...")

        local old_count = #search_results
        if old_count > MAX_RESULTS then old_count = MAX_RESULTS end
        for i = old_count, 1, -1 do
            table.remove(search_results, i)
            model:RowDeleted(i - 1)
        end

        for _, p in ipairs(SYSTEMS) do
            system_name = p.sname or p.name
            ludo.logInfo("Checking system " .. system_name .. " checkbox: " .. tostring(sys_cbs[system_name]:Checked()))
            if sys_cbs[system_name]:Checked() == 1 then
                search_system(p, query, search_results)
            end
        end

        for i = 1, #search_results do
            model:RowInserted(i - 1)
        end

        update_details(nil)
        status_lbl:SetText(string.format("Found %d result(s) for '%s'.", #search_results, query))
    end

    search_btn:OnClicked(function(b, data) do_search() end, nil)
    search_entry:OnEnter(function(e, data) do_search() end, nil)

    dl_btn:OnClicked(function(b, data)
        local sel = results_tbl:GetSelection()
        if not sel or #sel == 0 then
            status_lbl:SetText("No game selected. Run a search first.")
            return
        end
        local idx = sel[1] + 1
        if idx < 1 or idx > #search_results then
            status_lbl:SetText("No game selected.")
            return
        end
        local r = search_results[idx]
        local outdir = ludo.getOutputDirectory()

        local _, pkg_status, pkg_out = ludo.newDownload(r.rom_url, outdir, ludo.DOWNLOAD_NOW)
        if pkg_status == 200 or pkg_status == 206 or pkg_status == 0 then
            ludo.logSuccess("Queued PKG: " .. (pkg_out))
        else
            ludo.logError("Preflight HTTP " .. tostring(pkg_status) .. " for " .. r.game_name)
            ui.MsgBoxError(win, "Download Failed",
                "Preflight check returned HTTP " .. tostring(pkg_status) .. " for\n" .. r.game_name)
            return
        end

        status_lbl:SetText("Download queued: " .. r.game_name)

        window_alive = false
        win_open = false
        win:Destroy()
    end, nil)

    win:OnClosing(function(w, data)
        window_alive = false
        win_open = false
        return 1
    end, nil)
    win:Show()
    search_entry:SetFocus()
    while win_open do
        if ui.MainStep(true) == 0 then break end
    end
end

local config_path = "lualib/roms/retro.lua"
local f_config = io.open(config_path, "r")

if not f_config then
    f_config = io.open(config_path, "w")
    if not f_config then
        ui.MsgBoxError(win, "Configuration Error", "Cannot create " .. config_path)
        return
    end
    f_config:write("-- Retro Systems Configuration\nlocal SYSTEMS = {\n")
    local sys_processed = 0
    local total_used = 0
    for _, system in ipairs(retro_systems) do
        if system.used == 1 then total_used = total_used + 1 end
    end
    local function on_system_done()
        sys_processed = sys_processed + 1
    end
    for i, system in ipairs(retro_systems) do
        if system.used == 1 then
            local csv_name = system.brand .. " - " .. system.name .. ".csv"
            local csv_path = TOOLS_DIR .. "/" .. csv_name
            local f_csv = io.open(csv_path, "w")
            if f_csv then
                ludo.logInfo("Processing " .. system.name)
                generate_csv(system, f_csv, function(ok)
                    f_csv:close()
                    if ok then
                        if system.sname then
                            f_config:write(string.format("\t{\n\tsname = %q,\n\tname = %q,\n\tfile = %q,\n\tsource_url = %q,\n\tscrape_url = %q,\n\tcol_name = \"game_name\",\n\tcol_ext = \"game_ext\",\n\tcol_size = \"game_size\"\n\t},\n",
                            system.sname,
                            system.name,
                            csv_name,
                            "https://archive.org/download/ni-roms/roms/" .. system.brand .. " - " .. system.name .. ".zip",
                            "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=" .. system.id .. "&recherche="
                            ))
                        else
                            f_config:write(string.format("\t{\n\tname = %q,\n\tfile = %q,\n\tsource_url = %q,\n\tscrape_url = %q,\n\tcol_name = \"game_name\",\n\tcol_ext = \"game_ext\",\n\tcol_size = \"game_size\"\n\t},\n",
                            system.name,
                            csv_name,
                            "https://archive.org/download/ni-roms/roms/" .. system.brand .. " - " .. system.name .. ".zip",
                            "https://api.screenscraper.fr/api2/jeuRecherche.php?output=json&devid=recalbox&devpassword=C3KbyjX8PKsUgm2tu53y&softname=Emulationstation-Recalbox-9.1&ssid=test&sspassword=test&systemeid=" .. system.id .. "&recherche="
                            ))
                        end
                    end
                    on_system_done()
                end)
            else
                on_system_done()
            end
        end
    end
    if total_used == 0 then
        f_config:write("}\nreturn SYSTEMS\n")
        f_config:close()
        local ok, SYSTEMS = pcall(require, "roms.retro")
        if ok and SYSTEMS then setup_ui(SYSTEMS) end
    else
        while sys_processed < total_used do
            if ui.MainStep(true) == 0 then break end
        end
        f_config:write("}\nreturn SYSTEMS\n")
        f_config:close()
        local ok, SYSTEMS = pcall(require, "roms.retro")
        if ok and SYSTEMS then
            setup_ui(SYSTEMS)
        else
            ui.MsgBoxError(win, "Configuration Error", "Failed to load generated config.")
        end
    end
else
    f_config:close()
    local ok, SYSTEMS = pcall(require, "roms.retro")
    if ok and SYSTEMS then setup_ui(SYSTEMS) end
end