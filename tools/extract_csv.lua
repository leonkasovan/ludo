-- extract_csv.lua: Extract file listing from archive.zip.html to CSV
local html_path = "tools/archive.zip.html"
local csv_path = "tools/archive.zip.csv"

local f = io.open(html_path, "r")
if not f then ludo.logError("Cannot open " .. html_path); return end

local html = f:read("*all")
f:close()

local out = io.open(csv_path, "w")
if not out then ludo.logError("Cannot write " .. csv_path); return end

out:write("display_filename|ext|size\n")

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
