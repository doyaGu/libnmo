local exec = require("nmo._executor")

assert(exec.add_node(
    6,
    "A20E8D5B-DF002150",
    "Lua Script Send Message",
    { manager_entry = { policy = "create_missing", manager = "message" } }) == 1)
