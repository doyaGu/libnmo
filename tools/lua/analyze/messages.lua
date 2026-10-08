-- messages: where each message goes, across the files.
--
--   nmo script analyze messages <file>... [-- <name>]
--
-- Lists the senders and the receivers of each message, and for each send the
-- receivers it reaches: a message goes to an object (found by name in the
-- other files), to the members of a group, or to every object of a class,
-- and a receiver gets the messages of its target, or of the object its
-- script belongs to. "may reach" marks a receiver an object computed at run
-- time decides. Ends with the sends no receiver gets and the receivers no
-- send reaches. With <name>, only the messages whose name contains it.

local script = require("nmo.script")
local set = script.join(models)
local filter = arg[1] and arg[1]:lower()
local several = #models > 1

local function where(use)
    local text = use.node:where()
    if several then
        text = text .. " (" .. use.model.name .. ")"
    end
    return text
end

local function route(use)
    local object = use.dest or use.listener
    local name = object and tostring(object) or use.route_name and ('"' .. use.route_name .. '"')
    if use.route == "object" then
        return (use.kind == "send" and " to " or " on ") .. tostring(name)
    elseif use.route == "group" then
        return " to every object of " .. tostring(name)
    elseif use.route == "broadcast" then
        return " to every " .. (use.broadcast_class or "object")
    elseif use.route == "unknown" then
        return use.kind == "send" and " to an object set at run time"
            or " on an object set at run time"
    end
    return ""
end

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
                entry[kind][#entry[kind] + 1] = use
            end
        end
    end
end
table.sort(order, function(a, b) return a.name < b.name end)

local lost, deaf = {}, {}
for _, entry in ipairs(order) do
    if filter == nil or entry.name:lower():find(filter, 1, true) then
        print(string.format('"%s"  %d sent, %d received', entry.name, #entry.send, #entry.wait))
        for _, send in ipairs(entry.send) do
            print(string.format("  send     %s%s", where(send), route(send)))
            local sure, maybe = set:receivers(send)
            for _, receiver in ipairs(sure) do
                print("             reaches   " .. where(receiver))
            end
            for _, receiver in ipairs(maybe) do
                print("             may reach " .. where(receiver))
            end
            if #sure + #maybe == 0 then
                print("             reaches no receiver in the loaded files")
                lost[#lost + 1] = send
            end
        end
        for _, receiver in ipairs(entry.wait) do
            print(string.format("  receive  %s%s", where(receiver), route(receiver)))
            local sure, maybe = set:senders(receiver)
            if #sure + #maybe == 0 then
                deaf[#deaf + 1] = receiver
            end
        end
        for _, use in ipairs(entry.message) do
            print(string.format("  use      %s", where(use)))
        end
    end
end

if #lost > 0 then
    print("\nSends no receiver in the loaded files gets:")
    for _, send in ipairs(lost) do
        print(string.format('  "%s"  %s%s', send.message, where(send), route(send)))
    end
end
if #deaf > 0 then
    print("\nReceivers no send in the loaded files reaches:")
    for _, receiver in ipairs(deaf) do
        print(string.format('  "%s"  %s%s', receiver.message, where(receiver), route(receiver)))
    end
end
