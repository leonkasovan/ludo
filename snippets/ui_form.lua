-- ui.Form — labeled field layout (label + control per row)
local form = ui.NewForm()
form:SetPadded(1)
form:Append("Username", ui.NewEntry(), false)
form:Append("Password", ui.NewPasswordEntry(), false)
form:Append("Server",   ui.NewEntry(), true)     -- stretchy = fills height
