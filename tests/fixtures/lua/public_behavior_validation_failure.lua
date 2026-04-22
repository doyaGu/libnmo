local session = require("nmo.session")
local behavior = require("nmo.behavior")

local ctx = session.create_context()
local s = session.load_file(ctx, "__INPUT_PATH__")
local owner = __OWNER_ID__
local doomed = __DOOMED_ID__

local tx = behavior.begin_edit(ctx, s, "public lua validation failure")
behavior.remove_io(tx, doomed, false)

local ok, err = pcall(function()
    behavior.validate_interface_refs(tx, owner)
end)
assert(ok == false)
assert(err ~= nil)

behavior.rollback(tx)

ok, err = pcall(function()
    behavior.report(tx)
end)
assert(ok == false)
assert(string.find(err, "stale", 1, true) ~= nil)
