-- agent_context.lua — shared system context for Ludo agents.
--
-- Loads plugin rules for the agent's system prompt:
--   1. ludo_plugin_rules.md  (compact curated rules, ships next to this file)
--   2. Optionally appends specific sections extracted from the full
--      ludo_scripting.md manual (located at the app root, 2 levels up).
--
-- Usage:
--   local ctx = dofile("tools/Agents/agent_context.lua")
--   local rules   = ctx.rules()            -- compact rules (cached)
--   local section = ctx.manual_section(9)  -- §9 of the full manual, cached
--   local all     = ctx.full()             -- rules + conversion section

local M = {}

-- dir of this file (works via dofile and require; falls back to cwd-relative)
local this_dir = ""
do
    local src = debug and debug.getinfo and debug.getinfo(1, "S").source or ""
    this_dir = src:gsub("^@", ""):match("^(.-)[^/\\]+$") or ""
end

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local data = f:read("*a")
    f:close()
    return data
end

-- Candidate locations for the rules file / manual, relative to this file:
--   tools/Agents/  (source tree and installed layout are the same)
local RULES_PATHS = {
    this_dir .. "ludo_plugin_rules.md",
    "tools/Agents/ludo_plugin_rules.md",
    "ludo_plugin_rules.md",
}

local MANUAL_PATHS = {
    this_dir .. "../../ludo_scripting.md",
    this_dir .. "../ludo_scripting.md",
    "ludo_scripting.md",
}

local function first_existing(paths)
    for _, p in ipairs(paths) do
        local data = read_file(p)
        if data then return data, p end
    end
    return nil, nil
end

local rules_cache = nil
local manual_cache_path = nil
local manual_cache = nil
local section_cache = {}

--- Compact plugin rules (small, always include in system prompt).
function M.rules()
    if rules_cache then return rules_cache end
    local data, used = first_existing(RULES_PATHS)
    if not data then
        return "# Ludo plugin rules\n(file ludo_plugin_rules.md not found — ask the user for conventions)"
    end
    rules_cache = data
    return rules_cache
end

--- Locate and cache the full scripting manual.
local function manual()
    if manual_cache then return manual_cache end
    local data, used = first_existing(MANUAL_PATHS)
    manual_cache = data or ""
    manual_cache_path = used
    return manual_cache
end

--- Extract one numbered top-level section ("## N. Title") from the manual.
--  e.g. manual_section(9) → the yt-dlp conversion guide.
--  Returns nil if the manual or section is missing.
function M.manual_section(n)
    if section_cache[n] ~= nil then return section_cache[n] end
    local doc = manual()
    if doc == "" then section_cache[n] = nil; return nil end
    local start_pat = "\n## " .. tostring(n) .. "%. "
    local s = doc:find(start_pat)
    if not s then section_cache[n] = nil; return nil end
    -- section ends at the next "\n## " (or EOF)
    local e = doc:find("\n## ", s + 1)
    local text = e and doc:sub(s + 1, e - 1) or doc:sub(s + 1)
    section_cache[n] = text
    return text
end

--- Full default system context: rules + key manual sections.
--  Keeps the prompt bounded: rules (~4KB) + chosen sections.
function M.full(opts)
    opts = opts or {}
    local parts = { M.rules() }
    for _, n in ipairs(opts.manual_sections or { 8, 9 }) do
        local sec = M.manual_section(n)
        if sec then
            parts[#parts + 1] = "\n\n---\n\n# From ludo_scripting.md\n" .. sec
        end
    end
    return table.concat(parts)
end

--- Which manual file was used (for diagnostics).
function M.manual_path() return manual_cache_path end

return M
