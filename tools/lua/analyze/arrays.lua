-- arrays: who reads and writes each data array, by column, across the files.
--
--   nmo script analyze arrays <file>... [-- <name>]
--
-- Joins the files by array name: an array one file holds and another looks
-- up by name shows the uses of both. Ends with the arrays the loaded files
-- read but never write. With <name>, only the arrays whose name contains it.

local filter = arg[1] and arg[1]:lower()
local several = #models > 1

local arrays, order = {}, {}
for _, m in ipairs(models) do
    for _, group in ipairs(m:arrays()) do
        local entry = arrays[group.name]
        if entry == nil then
            entry = { name = group.name, objects = {}, read = {}, write = {} }
            arrays[group.name] = entry
            order[#order + 1] = entry
        end
        if type(group.key) ~= "string" then
            entry.objects[#entry.objects + 1] = { object = group.key, model = m }
        end
        for _, kind in ipairs({ "read", "write" }) do
            for _, use in ipairs(group[kind] or {}) do
                entry[kind][#entry[kind] + 1] = { use = use, model = m }
            end
        end
    end
end
table.sort(order, function(a, b) return a.name < b.name end)

local function column(use)
    if use.column_name then
        return "[" .. use.column_name .. "]"
    elseif use.column then
        return "[" .. use.column .. "]"
    end
    return ""
end

local function where(item)
    local text = item.use.node:where()
    if item.use.object == nil then
        text = text .. " (by name)"
    end
    if several then
        text = text .. " (" .. item.model.name .. ")"
    end
    return text
end

local unwritten = {}
for _, entry in ipairs(order) do
    if filter == nil or entry.name:lower():find(filter, 1, true) then
        local held = {}
        for _, item in ipairs(entry.objects) do
            held[#held + 1] = tostring(item.object) .. (several and " in " .. item.model.name or "")
        end
        local title = '"' .. entry.name .. '"'
        if #held > 0 then
            title = title .. "  " .. table.concat(held, ", ")
        else
            title = title .. "  (looked up by name; no loaded file holds it)"
        end
        print(title)
        for _, kind in ipairs({ "read", "write" }) do
            table.sort(entry[kind], function(a, b) return column(a.use) < column(b.use) end)
            for _, item in ipairs(entry[kind]) do
                print(string.format("  %-6s %-24s %s", kind, column(item.use), where(item)))
            end
        end
        if #entry.write == 0 then
            unwritten[#unwritten + 1] = '"' .. entry.name .. '"'
        end
    end
end

if #unwritten > 0 then
    print("\nRead, but no loaded file writes: " .. table.concat(unwritten, ", "))
end
