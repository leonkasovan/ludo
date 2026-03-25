local plugin = {}
function plugin.validate(url)
    return url:match("https://drive%.google%.com/file/d/([%w_-]+)/view")
end

local urls = {
	"https://drive.google.com/file/d/18iw_4VdfqmUDLbJl3XZ-SRKd6SqSLi6F/view?usp=sharing",
	"https://drive.google.com/file/d/1D3yCZRolhL3LVBcJ1BNq1MzvezUqZ3Uw/view?usp=sharing"
}

for _, url in ipairs(urls) do
    if plugin.validate(url) then
        ludo.lua_tester.printf("%s supported\n", url)
    end
end