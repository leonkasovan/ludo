-- zip.create(output_path, {file1, file2, ...})
-- Pack an explicit list of files (basenames used as entry names).
-- Returns: status (0=ok, -1=fail), errmsg (on failure)

-- Create test files
local f1 = io.open("file_a.txt", "w"); f1:write("A\n"); f1:close()
local f2 = io.open("file_b.txt", "w"); f2:write("B\n"); f2:close()

-- Pack them
local status, err = zip.create("output.zip", { "file_a.txt", "file_b.txt" })
if status == 0 then
    print("Archive created successfully")
else
    print("Error:", err)
end

-- Cleanup
os.remove("file_a.txt")
os.remove("file_b.txt")
os.remove("output.zip")
