-- summary: what each script waits for, sends, reads, writes, and activates.
--
--   nmo script analyze summary <file>... [-- <name>]
--
-- Lists every root (a script, or a graph no script holds) of each file with
-- the messages its building blocks wait for and send, the data arrays they
-- read and write, the scripts they activate, and the keys they react to. A
-- message sent to an object other files hold names it in quotes.
-- With <name>, only the roots whose name contains it.

local filter = arg[1] and arg[1]:lower()

local function add(set, list, text)
    if not set[text] then
        set[text] = true
        list[#list + 1] = text
    end
end

local function column_text(use)
    if use.column_name then
        return "[" .. use.column_name .. "]"
    elseif use.column then
        return "[" .. use.column .. "]"
    end
    return ""
end

local function key_text(use)
    local key = use:key()
    if key == nil then
        return nil
    elseif type(key) == "string" then
        return '"' .. key .. '"'
    end
    return tostring(key)
end

-- The pins of keyboard building blocks, as "Key Event(Up)".
local function key_reaction(node)
    if node.proto == nil or not node.proto:find("Key") then
        return nil
    end
    for _, pin in ipairs(node.pins) do
        local value, how = pin:value()
        if how == "saved" and math.type(value) == "integer" and pin.name:find("Key") then
            local name = pin:describe():match('^"(.-)"')
            if name then
                return node.proto .. "(" .. name .. ")"
            end
        end
    end
    return nil
end

local function count(n, noun)
    return n .. " " .. noun .. (n == 1 and "" or "s")
end

local function summarize(root)
    local graphs, blocks = 0, 0
    local lines = {
        { label = "waits for", set = {}, list = {} },
        { label = "sends", set = {}, list = {} },
        { label = "reads", set = {}, list = {} },
        { label = "writes", set = {}, list = {} },
        { label = "activates", set = {}, list = {} },
        { label = "deactivates", set = {}, list = {} },
        { label = "keys", set = {}, list = {} },
    }
    local by_kind = {
        wait = lines[1], message = lines[1], send = lines[2], read = lines[3],
        write = lines[4], activate = lines[5], deactivate = lines[6],
    }
    local unresolved = 0
    for node in root:walk() do
        if node.kind == "BB" then
            blocks = blocks + 1
        elseif node ~= root then
            graphs = graphs + 1
        end
        for _, use in ipairs(node.uses) do
            local text = key_text(use)
            if text == nil then
                unresolved = unresolved + 1
            else
                if use:is_array() then
                    text = text .. column_text(use)
                elseif use.kind == "send" and (use.dest or use.route_name) then
                    local dest = use.dest and tostring(use.dest) or ('"' .. use.route_name .. '"')
                    text = text .. (use.route == "group" and " to group " or " to ") .. dest
                elseif use.kind == "send" and use.route == "broadcast" then
                    text = text .. " to every " .. (use.broadcast_class or "object")
                end
                local line = by_kind[use.kind]
                add(line.set, line.list, text)
            end
        end
        local reaction = key_reaction(node)
        if reaction then
            add(lines[7].set, lines[7].list, reaction)
        end
    end

    local title = root.label
    if root.owner then
        title = title .. " (on " .. tostring(root.owner) .. ")"
    end
    print(string.format("  %s: %s, %s", title, count(graphs, "graph"), count(blocks, "building block")))
    for _, line in ipairs(lines) do
        if #line.list > 0 then
            table.sort(line.list)
            print(string.format("    %-12s %s", line.label, table.concat(line.list, ", ")))
        end
    end
    if unresolved > 0 then
        print(string.format("    %-12s %d uses computed at run time", "unresolved", unresolved))
    end
end

for _, m in ipairs(models) do
    print(m.name)
    for _, root in ipairs(m.roots) do
        if filter == nil or root.name:lower():find(filter, 1, true) then
            summarize(root)
        end
    end
end
