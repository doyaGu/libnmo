-- interactions: how scripts start each other, across the files.
--
--   nmo script analyze interactions <file>... [-- --dot]
--
-- A script reaches another when it sends a message the other waits for, or
-- when it activates or deactivates the other. Messages join the files by
-- name; a message sent to an object that is not a group only reaches the
-- scripts of an object of that name. With --dot, prints a Graphviz graph.

local dot = false
for _, a in ipairs(arg) do
    if a == "--dot" then
        dot = true
    end
end
local several = #models > 1

local function title(root, m)
    return several and root.label .. " (" .. m.name .. ")" or root.label
end

-- Every root, by name, to join by-name activation.
local roots_by_name = {}
for _, m in ipairs(models) do
    for _, root in ipairs(m.roots) do
        roots_by_name[root.name] = roots_by_name[root.name] or { root = root, model = m }
    end
end

-- The roots waiting for each message.
local waiters = {}
for _, m in ipairs(models) do
    for _, use in ipairs(m.uses) do
        if (use.kind == "wait" or use.kind == "message") and use.message then
            local list = waiters[use.message] or {}
            waiters[use.message] = list
            list[#list + 1] = { root = use.root, model = m }
        end
    end
end

local function reaches(dest, waiter)
    if dest == nil or dest.class == nil or dest.class:find("Group") then
        return true
    end
    local owner = waiter.root.owner
    return owner == nil or owner.name == dest.name
end

-- Edges from each root: targets in first-seen order, each with its reasons.
local edges, sources = {}, {}
local function add_edge(source, target, reason)
    local key = source.root
    local entry = edges[key]
    if entry == nil then
        entry = { source = source, targets = {}, order = {} }
        edges[key] = entry
        sources[#sources + 1] = entry
    end
    local target_key = target.root or target.name
    local t = entry.targets[target_key]
    if t == nil then
        t = { target = target, reasons = {}, seen = {} }
        entry.targets[target_key] = t
        entry.order[#entry.order + 1] = t
    end
    if not t.seen[reason] then
        t.seen[reason] = true
        t.reasons[#t.reasons + 1] = reason
    end
end

for _, m in ipairs(models) do
    for _, use in ipairs(m.uses) do
        local source = { root = use.root, model = m }
        if use.kind == "send" and use.message then
            for _, waiter in ipairs(waiters[use.message] or {}) do
                if waiter.root ~= use.root and reaches(use.dest, waiter) then
                    add_edge(source, waiter, '"' .. use.message .. '"')
                end
            end
        elseif use:is_script() then
            local key = use:key()
            local target
            if type(key) == "string" then
                target = roots_by_name[key] or { name = key }
            elseif key ~= nil then
                target = { root = key.root or key, model = m }
            end
            if target ~= nil then
                add_edge(source, target, use.kind .. "s")
            end
        end
    end
end

local function target_title(target)
    if target.root then
        return title(target.root, target.model)
    end
    return '"' .. target.name .. '" (not in the loaded files)'
end

if dot then
    print("digraph interactions {")
    print("  rankdir=LR;")
    print("  node [shape=box];")
    local function id(item)
        return string.format("%q", item.root and target_title(item) or item.name)
    end
    for _, entry in ipairs(sources) do
        for _, t in ipairs(entry.order) do
            print(string.format("  %s -> %s [label=%q];", id(entry.source), id(t.target),
                                table.concat(t.reasons, ", ")))
        end
    end
    print("}")
    return
end

for _, entry in ipairs(sources) do
    print(title(entry.source.root, entry.source.model))
    for _, t in ipairs(entry.order) do
        print(string.format("  -> %-40s %s", target_title(t.target), table.concat(t.reasons, ", ")))
    end
end
