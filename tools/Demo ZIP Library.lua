-- ZIP Library Demo — interactive explorer for zip.create()
-- Run from Ludo: Tools > ZIP Demo (or via -s tools/zip_demo.lua)
-- Tools must NOT call ui.Main()/ui.Uninit() — use MainStep() instead.

local ui = require("ui")
local win_open = true

local output_buf = {}

local function run_code(code)
    output_buf = {}
    local old_print = print
    print = function(...)
        local parts = {}
        for i = 1, select("#", ...) do
            parts[i] = tostring(select(i, ...))
        end
        output_buf[#output_buf + 1] = table.concat(parts, "\t")
    end
    local ok, err = pcall(load(code))
    print = old_print
    if not ok then
        output_buf[#output_buf + 1] = "Error: " .. tostring(err)
    end
    return table.concat(output_buf, "\n")
end

local function make_tab(initial_code)
    local box = ui.NewVerticalBox()
    box:SetPadded(1)
    local code_mle = ui.NewNonWrappingMultilineEntry()
    code_mle:SetText(initial_code)
    box:Append(code_mle, true)
    local run_btn = ui.NewButton("  Run  ")
    box:Append(run_btn, false)
    local output_mle = ui.NewMultilineEntry()
    output_mle:SetReadOnly(1)
    box:Append(output_mle, false)
    run_btn:OnClicked(function()
        output_mle:SetText(run_code(code_mle:Text()))
    end, nil)
    return box
end

-- ====================================================================
-- Tab examples
-- ====================================================================

local create_examples = [[-- === zip.create() — file list ===
-- Pack an explicit list of files.
-- Each file is stored using its basename only (no subdirectory path).
-- Returns: status (0=ok, -1=fail), errmsg (on failure)

-- First create some test files
local f1 = io.open("_zip_demo_a.txt", "w")
f1:write("Contents of file A\n")
f1:close()

local f2 = io.open("_zip_demo_b.txt", "w")
f2:write("Contents of file B\n")
f2:close()

-- Pack them into a ZIP
local status, err = zip.create("_demo_files.zip", {
    "_zip_demo_a.txt",
    "_zip_demo_b.txt",
})
print("status:", status, "err:", err or "(none)")

-- Verify: list contents (zip doesn't have a list command, but we can check
-- that the file exists and has a valid ZIP signature)
local f = io.open("_demo_files.zip", "rb")
if f then
    local sig = f:read(4)
    print("ZIP signature OK:", sig == "PK\3\4")
    f:close()
end

-- Cleanup
os.remove("_zip_demo_a.txt")
os.remove("_zip_demo_b.txt")
os.remove("_demo_files.zip")]]

local dir_examples = [[-- === zip.create() — directory ===
-- Pack an entire directory recursively.
-- Entry names preserve the relative path under the root directory.

-- Create test directory structure
os.execute("mkdir _zip_demo_dir 2>nul")
local f1 = io.open("_zip_demo_dir/readme.txt", "w")
f1:write("This is a readme\n")
f1:close()
os.execute("mkdir _zip_demo_dir/sub 2>nul")
local f2 = io.open("_zip_demo_dir/sub/data.txt", "w")
f2:write("Nested data\n")
f2:close()

-- Pack the whole directory
local status, err = zip.create("_demo_dir.zip", "_zip_demo_dir")
print("status:", status, "err:", err or "(none)")

-- Cleanup
os.remove("_zip_demo_dir/readme.txt")
os.remove("_zip_demo_dir/sub/data.txt")
os.remove("_demo_dir.zip")
-- os.execute is used above; on Windows "rd /s /q" removes recursively
-- For cross-platform, use Lua's os.execute appropriately:
os.execute("rmdir /s /q _zip_demo_dir 2>nul")]]

local glob_examples = [[-- === zip.create() — directory with glob filter ===
-- Pack files matching a glob pattern from a directory.
-- Supports * (any sequence) and ? (any single char), case-insensitive.

-- Create test files
os.execute("mkdir _zip_glob_dir 2>nul")
local f1 = io.open("_zip_glob_dir/video1.mp4", "w"); f1:write("mp4"); f1:close()
local f2 = io.open("_zip_glob_dir/video2.mp4", "w"); f2:write("mp4"); f2:close()
local f3 = io.open("_zip_glob_dir/readme.txt", "w"); f3:write("txt"); f3:close()
local f4 = io.open("_zip_glob_dir/data.json",  "w"); f4:write("json"); f4:close()

-- Only MP4 files
local status, err = zip.create("_glob_demo.zip", "_zip_glob_dir", "*.mp4")
print("MP4 only:", status, "err:", err or "(none)")

-- Only .txt or .json files
local status2, err2 = zip.create("_glob_demo2.zip", "_zip_glob_dir", "*.?xt")
print("?xt only:", status2, "err:", err2 or "(none)")

-- Cleanup
os.remove("_zip_glob_dir/video1.mp4")
os.remove("_zip_glob_dir/video2.mp4")
os.remove("_zip_glob_dir/readme.txt")
os.remove("_zip_glob_dir/data.json")
os.execute("rmdir /s /q _zip_glob_dir 2>nul")
os.remove("_glob_demo.zip")
os.remove("_glob_demo2.zip")]]

local error_examples = [[-- === zip.create() — error handling ===

-- File not found
local status, err = zip.create("_bad.zip", { "nonexistent.txt" })
print("nonexistent file:", status, err)

-- Non-existent directory
local status, err = zip.create("_bad.zip", "_nonexistent_dir")
print("nonexistent dir:", status, err)

-- Empty file list (valid but produces empty ZIP)
local f = io.open("_empty.txt", "w"); f:write("x"); f:close()
local status, err = zip.create("_empty.zip", { "_empty.txt" })
print("one file:", status, err or "(ok)")
os.remove("_empty.txt")
os.remove("_empty.zip")]]

-- ====================================================================
-- Build window
-- ====================================================================

local tabs = ui.NewTab()
tabs:Append("File List", make_tab(create_examples))
tabs:Append("Directory", make_tab(dir_examples))
tabs:Append("Glob Filter", make_tab(glob_examples))
tabs:Append("Errors", make_tab(error_examples))

local win = ui.NewWindow("ZIP Library Demo", 720, 620, false)
win:SetMargined(1)
win:SetChild(tabs)
win:OnClosing(function(w, data)
    win_open = false
    return 1
end, nil)

win:Show()
while win_open do
    if ui.MainStep(true) == 0 then break end
end
