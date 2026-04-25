local exec = require("nmo._executor")

assert(exec.add_operation(6, "33CC6B49-3589282B") == 1)
assert(exec.remove_operation(16) == 2)
