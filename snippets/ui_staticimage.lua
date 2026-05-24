-- ui.StaticImage — display an image from file or memory
local img = ui.NewStaticImage()

-- Load from file
local ok, err = img:SetImageFromFile("C:/path/to/image.png")
if not ok then print("Error:", err) end

-- Load from memory (e.g. from http.get)
-- local body, status = http.get("https://example.com/image.png")
-- local ok, err = img:SetImageFromMemory(body)
-- if not ok then print("Error:", err) end

-- Clear the image
img:Clear()
