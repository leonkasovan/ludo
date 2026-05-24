-- ui.Image / ui.LoadImageFromMemory — manual image loading
-- Used with ui.TableValueTypeImage columns.

-- Load image from raw bytes (returns uiImage userdata or nil, err)
local img, err = ui.LoadImageFromMemory(file_data)
if img then
    -- img is used as a cell value in tables
    -- ui.NewTableValueImage(img) is automatic from CellValue handler
end

-- Create an empty image and append pixel data
local empty = ui.NewImage(32, 32)
-- pixel_data: 32*32*4 bytes of premultiplied RGBA
-- ui.ImageAppend(empty, pixel_data, 32, 32)
