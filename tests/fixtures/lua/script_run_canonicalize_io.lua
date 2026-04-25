local exec = require("nmo._executor")

local root = assert(exec.root_script_id())
local first_input = assert(exec.io_at(root, "input", 1))

exec.remove_io(first_input, "canonicalize")
