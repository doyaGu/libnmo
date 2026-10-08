-- messages: who sends and who waits for each message, across the files.
--
--   nmo script analyze messages <file>... [-- <name>]
--
-- Joins the files by message name: a message one file sends and another
-- waits for shows both. Ends with the messages no loaded file waits for or
-- sends. With <name>, only the messages whose name contains it.

local filter = arg[1] and arg[1]:lower()
local several = #models > 1

local bus, order = {}, {}
for _, m in ipairs(models) do
    for _, group in ipairs(m:messages()) do
        local entry = bus[group.name]
        if entry == nil then
            entry = { name = group.name, send = {}, wait = {}, message = {} }
            bus[group.name] = entry
            order[#order + 1] = entry
        end
        for _, kind in ipairs({ "send", "wait", "message" }) do
            for _, use in ipairs(group[kind] or {}) do
                entry[kind][#entry[kind] + 1] = { use = use, model = m }
            end
        end
    end
end
table.sort(order, function(a, b) return a.name < b.name end)

local function where(item)
    local text = item.use.node:where()
    if several then
        text = text .. " (" .. item.model.name .. ")"
    end
    if item.use.kind == "send" and item.use.dest then
        text = text .. " to " .. tostring(item.use.dest)
    end
    return text
end

local unheard, unsent = {}, {}
for _, entry in ipairs(order) do
    if filter == nil or entry.name:lower():find(filter, 1, true) then
        print(string.format('"%s"  %d sent, %d waited for', entry.name, #entry.send,
                            #entry.wait + #entry.message))
        for _, kind in ipairs({ "send", "wait", "message" }) do
            for _, item in ipairs(entry[kind]) do
                print(string.format("  %-8s %s", kind, where(item)))
            end
        end
        if #entry.send > 0 and #entry.wait + #entry.message == 0 then
            unheard[#unheard + 1] = '"' .. entry.name .. '"'
        elseif #entry.send == 0 then
            unsent[#unsent + 1] = '"' .. entry.name .. '"'
        end
    end
end

if #unheard > 0 then
    print("\nSent, but no loaded file waits for: " .. table.concat(unheard, ", "))
end
if #unsent > 0 then
    print("\nWaited for, but no loaded file sends: " .. table.concat(unsent, ", "))
end
