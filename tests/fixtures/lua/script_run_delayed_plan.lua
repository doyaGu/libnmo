local exec = require("nmo._executor")

local root = assert(exec.root_script_id())

assert(exec.add_io(root, "input", "Lua Delayed In A") == 1)
assert(exec.add_io(root, "output", "Lua Delayed Out A") == 2)
