-- The methods of the tables nmo.script.model() returns. The C side builds a
-- snapshot of the script model and the script index as plain tables that
-- refer to each other, and gives them these classes as metatables. This
-- chunk returns the classes, and join(); a script can add its own methods to
-- the classes.

local Model, Node, Io, Param, Operation, Link, Edge, Use, Object, Set =
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}
local classes = {
    Model = Model, Node = Node, Io = Io, Param = Param, Operation = Operation,
    Link = Link, Edge = Edge, Use = Use, Object = Object, Set = Set,
}
for _, class in pairs(classes) do
    class.__index = class
end

local function unnamed(name)
    return (name == nil or name == "") and "(unnamed)" or name
end

Node.__tostring = function(node) return node.label end
Operation.__tostring = function(op) return op.label end
Object.__tostring = function(object) return unnamed(object.name) .. "#" .. object.id end
Io.__tostring = function(io) return io.node.label .. "." .. unnamed(io.name) end
Param.__tostring = function(param)
    if param.owner == nil then
        return unnamed(param.name) .. "#" .. param.id
    end
    return param.owner.label .. "." .. unnamed(param.name)
end
Link.__tostring = function(link)
    return tostring(link.source) .. " -> " .. tostring(link.target)
end
Edge.__tostring = function(edge)
    return tostring(edge.source) .. (edge.kind == "write" and " => " or " -> ") ..
        tostring(edge.target)
end
Use.__tostring = function(use)
    return use.kind .. " " .. tostring(use:key_text()) .. " by " .. use.node.label
end

-- ---------------------------------------------------------------------------
-- Matching
-- ---------------------------------------------------------------------------

-- A criterion is a value the field must equal, a function the field must
-- satisfy, or a list of values (any of them).
local function matches(value, criterion)
    local kind = type(criterion)
    if kind == "function" then
        return criterion(value) and true or false
    elseif kind == "table" and getmetatable(criterion) == nil then
        for _, alternative in ipairs(criterion) do
            if value == alternative then
                return true
            end
        end
        return false
    end
    return value == criterion
end

local function matches_all(item, criteria)
    for field, criterion in pairs(criteria) do
        if not matches(item[field], criterion) then
            return false
        end
    end
    return true
end

local function filter(list, criteria)
    local found = {}
    for _, item in ipairs(list) do
        if criteria == nil or matches_all(item, criteria) then
            found[#found + 1] = item
        end
    end
    return found
end

-- The item of `list` named `key`, or at position `key`.
local function pick(list, key)
    if type(key) == "number" then
        return list[key]
    end
    for _, item in ipairs(list) do
        if item.name == key then
            return item
        end
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- Nodes
-- ---------------------------------------------------------------------------

-- Every node of the subtree, the node first, depth first.
function Node:walk()
    local stack = { self }
    return function()
        local node = table.remove(stack)
        if node ~= nil then
            for i = #node.children, 1, -1 do
                stack[#stack + 1] = node.children[i]
            end
        end
        return node
    end
end

-- The nodes of the subtree matching every criterion: find{proto = "Timer"}.
function Node:find(criteria)
    local found = {}
    for node in self:walk() do
        if criteria == nil or matches_all(node, criteria) then
            found[#found + 1] = node
        end
    end
    return found
end

-- "Root/Graph/Node" by names.
function Node:path()
    local names = {}
    local node = self
    while node ~= nil do
        table.insert(names, 1, unnamed(node.name))
        node = node.parent
    end
    return table.concat(names, "/")
end

-- "Node#id in Root#id / Graph#id": where the node is.
function Node:where()
    local root, parent = self.root, self.parent
    if parent == nil then
        return self.label
    elseif parent == root then
        return self.label .. " in " .. root.label
    end
    return self.label .. " in " .. root.label .. " / " .. parent.label
end

function Node:input(key) return pick(self.inputs, key) end
function Node:output(key) return pick(self.outputs, key) end
function Node:pin(key) return pick(self.pins, key) end
function Node:pout(key) return pick(self.pouts, key) end
function Node:var(key) return pick(self.locals, key) end
function Node:setting(key) return pick(self.settings, key) end

-- The IOs the links from output `key` (every output when nil) activate.
function Node:next(key)
    local targets = {}
    local outputs = key == nil and self.outputs or { self:output(key) }
    for _, io in ipairs(outputs) do
        for _, link in ipairs(io.links) do
            targets[#targets + 1] = link.target
        end
    end
    return targets
end

-- The IOs whose links activate input `key` (every input when nil).
function Node:prev(key)
    local sources = {}
    local inputs = key == nil and self.inputs or { self:input(key) }
    for _, io in ipairs(inputs) do
        for _, link in ipairs(io.incoming) do
            sources[#sources + 1] = link.source
        end
    end
    return sources
end

-- The value input `key` takes, as Param:value() reads it.
function Node:value(key)
    local param = self:pin(key)
    if param == nil then
        return nil, "none"
    end
    return param:value()
end

-- ---------------------------------------------------------------------------
-- Parameters
-- ---------------------------------------------------------------------------

-- The value the parameter takes, how it gets it, and the parameter its inputs
-- lead to (its origin). How is
--   "saved"     the origin holds a value no output writes: the value is that
--   "written"   outputs write the origin at run time: the value is the one
--               it starts with
--   "computed"  the origin is an output or an operation result: no value
--   "none"      the input reads nothing: no value
function Param:value()
    local origin, how = self.origin, self.origin_kind
    if (how == "saved" or how == "written") and origin ~= nil then
        return origin.data, how, origin
    end
    return nil, how, origin
end

-- The value as text: the saved value, or where it is computed.
function Param:describe()
    local _, how, origin = self:value()
    if how == "saved" then
        return origin.text or "(empty)"
    elseif how == "written" then
        local writers = {}
        for _, writer in ipairs(origin:writers()) do
            writers[#writers + 1] = tostring(writer)
        end
        return (origin.text or "(empty)") .. ", written by " .. table.concat(writers, ", ")
    elseif how == "computed" and origin ~= nil then
        return "computed by " .. tostring(origin)
    end
    return "not connected"
end

-- The inputs reading the parameter.
function Param:readers()
    local readers = {}
    for _, edge in ipairs(self.out_edges) do
        if edge.kind == "read" then
            readers[#readers + 1] = edge.target
        end
    end
    return readers
end

-- The parameters an output writes.
function Param:writes()
    local written = {}
    for _, edge in ipairs(self.out_edges) do
        if edge.kind == "write" then
            written[#written + 1] = edge.target
        end
    end
    return written
end

-- The outputs writing the parameter.
function Param:writers()
    local writers = {}
    for _, edge in ipairs(self.in_edges) do
        if edge.kind == "write" then
            writers[#writers + 1] = edge.source
        end
    end
    return writers
end

-- The node holding the parameter, through the operation holding it.
function Param:node()
    local owner = self.owner
    if owner ~= nil and getmetatable(owner) == Operation then
        return owner.graph
    end
    return owner
end

-- ---------------------------------------------------------------------------
-- Uses
-- ---------------------------------------------------------------------------

-- What the use is about: the message name, the array or script, or the
-- name a lookup finds it by; nil when it is computed at run time.
function Use:key()
    if self.message ~= nil then
        return self.message
    end
    return self.object or self.object_name
end

function Use:key_text()
    local key = self:key()
    if key == nil then
        return "(unknown)"
    elseif type(key) == "string" then
        return '"' .. key .. '"'
    end
    return tostring(key)
end

-- Whether objects `a` and `b` (either may be only a name) are one object: the
-- same table, or of one name, and of one class when both say. The files of
-- a game share their objects by name.
local function same_object(a, a_name, b, b_name)
    if a ~= nil and a == b then
        return true
    end
    a_name = a ~= nil and a.name or a_name
    b_name = b ~= nil and b.name or b_name
    if a_name == nil or a_name == "" or a_name ~= b_name then
        return false
    end
    return a == nil or b == nil or a.class == nil or b.class == nil or a.class == b.class
end

-- Whether the send delivers its message to receiver `receiver` (a "wait"
-- use): true, false, or nil when that depends on what the scripts compute at
-- run time or on objects no loaded file holds. `set` (from join()) finds the
-- members of a group the send only knows by name.
function Use:reaches(receiver, set)
    if self.kind ~= "send" or receiver.kind ~= "wait" then
        return false
    end
    if self.message ~= nil and receiver.message ~= nil and self.message ~= receiver.message then
        return false
    end
    if self.message == nil or receiver.message == nil or self.route == "unknown" or
        receiver.route == "unknown" then
        return nil
    end
    local listener, listener_name = receiver.listener, receiver.route_name
    if self.route == "object" then
        return same_object(self.dest, self.route_name, listener, listener_name)
    elseif self.route == "group" then
        -- the group gets it too: Ballance's scripts on a group wait for what is
        -- sent to the group
        if same_object(self.dest, self.route_name, listener, listener_name) then
            return true
        end
        local group = self.dest
        if (group == nil or group.members == nil) and set ~= nil then
            group = set:object(self.route_name or (group and group.name), "CKGroup")
        end
        if group == nil or group.members == nil then
            return nil
        end
        for _, member in ipairs(group.members) do
            if same_object(member, nil, listener, listener_name) then
                return true
            end
        end
        return false
    elseif self.route == "broadcast" then
        if self.broadcast_class == nil then
            return true
        elseif listener == nil or listener.classes == nil then
            return nil
        end
        return listener.classes[self.broadcast_class] == true
    end
    return nil
end

function Use:is_message() return self.kind == "send" or self.kind == "wait" or self.kind == "message" end
function Use:is_array() return self.kind == "read" or self.kind == "write" end
function Use:is_script() return self.kind == "activate" or self.kind == "deactivate" end

-- ---------------------------------------------------------------------------
-- Model
-- ---------------------------------------------------------------------------

-- The node, IO, parameter, operation, link, or object of id `id`.
function Model:get(id)
    return self.by_id[id] or self.objects[id]
end

function Model:walk()
    local i = 0
    return function()
        i = i + 1
        return self.nodes[i]
    end
end

-- The nodes matching every criterion, in document order.
function Model:find(criteria)
    return filter(self.nodes, criteria)
end

-- The uses matching every criterion: uses{kind = "send"}.
function Model:find_uses(criteria)
    return filter(self.uses, criteria)
end

-- Groups `uses` by key, sorted by key text. Each group is
-- { key = ..., name = ..., <bucket> = {uses...} } with a bucket per kind.
local function group_uses(uses, predicate)
    local groups, order = {}, {}
    for _, use in ipairs(uses) do
        local key = use:key()
        if predicate(use) and key ~= nil then
            local group = groups[key]
            if group == nil then
                local name = type(key) == "string" and key or key.name
                group = { key = key, name = name }
                groups[key] = group
                order[#order + 1] = group
            end
            local bucket = group[use.kind]
            if bucket == nil then
                bucket = {}
                group[use.kind] = bucket
            end
            bucket[#bucket + 1] = use
        end
    end
    table.sort(order, function(a, b)
        if a.name ~= b.name then
            return a.name < b.name
        end
        return tostring(a.key) < tostring(b.key)
    end)
    return order
end

-- Each message: { name = ..., send = {uses...}, wait = {...}, message = {...} }.
function Model:messages()
    return group_uses(self.uses, Use.is_message)
end

-- Each data array: { key = object or name, name = ..., read = {...}, write = {...} }.
function Model:arrays()
    return group_uses(self.uses, Use.is_array)
end

-- Each script activated: { key = script node or name, name = ..., activate = {...},
-- deactivate = {...} }.
function Model:scripts()
    return group_uses(self.uses, Use.is_script)
end

-- The uses whose key is computed at run time.
function Model:unresolved()
    local found = {}
    for _, use in ipairs(self.uses) do
        if use:key() == nil then
            found[#found + 1] = use
        end
    end
    return found
end

-- ---------------------------------------------------------------------------
-- Several files
-- ---------------------------------------------------------------------------

-- The models of several files of one game, joined: their objects by name,
-- and where each message goes from one file to another.
function classes.join(models)
    local set = setmetatable({ models = models, by_name = {} }, Set)
    for _, m in ipairs(models) do
        for _, object in pairs(m.objects) do
            local list = set.by_name[object.name]
            if list == nil then
                list = {}
                set.by_name[object.name] = list
            end
            list[#list + 1] = object
        end
    end
    return set
end

-- An object named `name` (of class `class` when given) in the files: one
-- with members first, for a group the files list in more than one place.
function Set:object(name, class)
    local found
    for _, object in ipairs(self.by_name[name] or {}) do
        if class == nil or object.class == class then
            if object.members ~= nil and #object.members > 0 then
                return object
            end
            found = found or object
        end
    end
    return found
end

-- Every use of the files matching the criteria, as Model:find_uses() does.
function Set:find_uses(criteria)
    local found = {}
    for _, m in ipairs(self.models) do
        for _, use in ipairs(m:find_uses(criteria)) do
            found[#found + 1] = use
        end
    end
    return found
end

-- The receivers send `send` reaches in the files, and those it may reach
-- (Use:reaches() is nil): two lists of "wait" uses.
function Set:receivers(send)
    local sure, maybe = {}, {}
    for _, receiver in ipairs(self:find_uses{ kind = "wait" }) do
        local reached = send:reaches(receiver, self)
        if reached then
            sure[#sure + 1] = receiver
        elseif reached == nil and (receiver.message == nil or send.message == nil or
                                   receiver.message == send.message) then
            maybe[#maybe + 1] = receiver
        end
    end
    return sure, maybe
end

-- The sends in the files that reach receiver `receiver`, and those that may.
function Set:senders(receiver)
    local sure, maybe = {}, {}
    for _, send in ipairs(self:find_uses{ kind = "send" }) do
        local reached = send:reaches(receiver, self)
        if reached then
            sure[#sure + 1] = send
        elseif reached == nil and (receiver.message == nil or send.message == nil or
                                   receiver.message == send.message) then
            maybe[#maybe + 1] = send
        end
    end
    return sure, maybe
end

return classes
