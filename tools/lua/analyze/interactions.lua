-- interactions: how scripts start each other, across the files.
--
--   nmo script analyze interactions <file>... [-- --dot]
--
-- A script reaches another when a message it sends reaches a receiver of the
-- other (see the messages analysis), or when it activates or deactivates the
-- other. A message an object computed at run time routes is marked "?".
-- With --dot, prints a Graphviz graph.

local script = require("nmo.script")
local set = script.join(models)
local dot = false
for _, a in ipairs(arg) do
    if a == "--dot" then
        dot = true
    end
end
local several = #models > 1

local function title(root)
    return several and root.label .. " (" .. root.model.name .. ")" or root.label
end

-- Every root, by name, to join by-name activation.
local roots_by_name = {}
for _, m in ipairs(models) do
    for _, root in ipairs(m.roots) do
        roots_by_name[root.name] = roots_by_name[root.name] or root
    end
end

-- Edges from each root: targets in first-seen order, each with its reasons.
local edges, sources = {}, {}
local function add_edge(source, target, reason)
    local entry = edges[source]
    if entry == nil then
        entry = { source = source, targets = {}, order = {} }
        edges[source] = entry
        sources[#sources + 1] = entry
    end
    local t = entry.targets[target]
    if t == nil then
        t = { target = target, reasons = {}, seen = {} }
        entry.targets[target] = t
        entry.order[#entry.order + 1] = t
    end
    if not t.seen[reason] then
        t.seen[reason] = true
        t.reasons[#t.reasons + 1] = reason
    end
end

for _, m in ipairs(models) do
    for _, use in ipairs(m.uses) do
        if use.kind == "send" then
            local sure, maybe = set:receivers(use)
            for _, receiver in ipairs(sure) do
                if receiver.root ~= use.root then
                    add_edge(use.root, receiver.root, '"' .. receiver.message .. '"')
                end
            end
            for _, receiver in ipairs(maybe) do
                if receiver.root ~= use.root then
                    add_edge(use.root, receiver.root,
                             '"' .. (use.message or receiver.message or "?") .. '"?')
                end
            end
        elseif use:is_script() then
            local key = use:key()
            local target
            if type(key) == "string" then
                target = roots_by_name[key] or key
            elseif key ~= nil then
                target = key.root or key
            end
            if target ~= nil then
                add_edge(use.root, target, use.kind .. "s")
            end
        end
    end
end

local function target_title(target)
    if type(target) == "string" then
        return '"' .. target .. '" (not in the loaded files)'
    end
    return title(target)
end

if dot then
    print("digraph interactions {")
    print("  rankdir=LR;")
    print("  node [shape=box];")
    for _, entry in ipairs(sources) do
        for _, t in ipairs(entry.order) do
            print(string.format("  %q -> %q [label=%q];", title(entry.source),
                                target_title(t.target), table.concat(t.reasons, ", ")))
        end
    end
    print("}")
    return
end

for _, entry in ipairs(sources) do
    print(title(entry.source))
    for _, t in ipairs(entry.order) do
        print(string.format("  -> %-40s %s", target_title(t.target), table.concat(t.reasons, ", ")))
    end
end
