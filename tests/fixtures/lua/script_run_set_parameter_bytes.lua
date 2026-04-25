local exec = require("nmo._executor")

assert(exec.set_parameter_bytes(64, string.char(0x2A, 0, 0, 0)) == 1)
