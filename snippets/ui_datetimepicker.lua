-- ui.DateTimePicker / DatePicker / TimePicker — date/time controls
local dtp  = ui.NewDateTimePicker()  -- date + time
local dp   = ui.NewDatePicker()      -- date only
local tp   = ui.NewTimePicker()      -- time only

local now = os.date("*t")
dtp:SetTime({ year = now.year, month = now.month, day = now.day,
              hour = now.hour, min = now.min, sec = now.sec })

dtp:OnChanged(function(p, data)
    local t = p:Time()
    print(string.format("%04d-%02d-%02d %02d:%02d:%02d",
          t.year, t.month, t.day, t.hour, t.min, t.sec))
end, nil)
