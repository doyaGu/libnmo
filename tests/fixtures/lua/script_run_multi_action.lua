local exec = require("nmo._executor")

local root = assert(exec.root_script_id())

assert(exec.add_io(root, "input", "Lua Multi In A"))
assert(exec.add_io(root, "input", "Lua Multi In B"))
assert(exec.add_io(root, "output", "Lua Multi Out A"))
