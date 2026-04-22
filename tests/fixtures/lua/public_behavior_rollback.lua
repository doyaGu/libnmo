local session = require("nmo.session")
local behavior = require("nmo.behavior")

local ctx = session.create_context()
local s = session.load_file(ctx, "__INPUT_PATH__")
local root = assert(behavior.script_at(s, 1)).script_id

local tx = behavior.begin_edit(ctx, s, "public lua rollback")
assert(behavior.add_io(tx, root, "input", "Lua Rollback In") ~= nil)
behavior.rollback(tx)

local ok, err = pcall(function()
    behavior.report(tx)
end)
assert(ok == false)
assert(string.find(err, "stale", 1, true) ~= nil)
