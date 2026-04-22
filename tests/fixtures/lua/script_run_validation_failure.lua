local exec = require("nmo._executor")

local owner = 253
local doomed = assert(exec.interface_sub_at(owner, 1))

exec.remove_node(owner, doomed, "preserve")
