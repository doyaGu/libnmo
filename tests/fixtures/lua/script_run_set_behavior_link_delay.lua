local exec = require("nmo._executor")

assert(exec.set_behavior_link_delay(75, 5) == 1)
