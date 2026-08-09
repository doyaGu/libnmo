local exec = require("nmo._executor")

assert(exec.add_parameter(6, "local", "47884C3F-432C2C20", "Lua Op In") == 1)
assert(exec.add_operation(6, "33CC6B49-3589282B") == 2)
assert(exec.rewire_operation(17, 16, nil, nil) == 3)
