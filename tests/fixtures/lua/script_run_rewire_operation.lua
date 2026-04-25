local exec = require("nmo._executor")

assert(exec.add_parameter(6, "local", "5A5716FD-44E276D7", "Lua Op In") == 1)
assert(exec.add_operation(6, "33CC6B49-3589282B") == 2)
assert(exec.rewire_operation(17, 16, nil, nil) == 3)
