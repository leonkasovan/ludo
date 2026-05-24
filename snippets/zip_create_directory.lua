-- zip.create(output_path, directory [, glob_filter])
-- Pack an entire directory recursively.
-- Entry names preserve relative paths under the directory.

-- Pack all files in a directory
local status, err = zip.create("backup.zip", "/path/to/folder")
if status == 0 then
    print("Directory packed successfully")
else
    print("Error:", err)
end

-- Pack only .mp4 files (case-insensitive glob)
local status, err = zip.create("videos.zip", "/path/to/folder", "*.mp4")
if status == 0 then
    print("Videos packed")
else
    print("Error:", err)
end

-- Pack with ? wildcard (single character)
local status, err = zip.create("selected.zip", "/path/to/folder", "file_??.txt")
