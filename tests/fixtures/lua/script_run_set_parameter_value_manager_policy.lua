local exec = require("nmo._executor")

local op = exec.add_node(
    6,
    "A20E8D5B-DF002150",
    "Lua Script Send Message Value",
    { manager_entry = { policy = "create_missing", schema = "message" } })
assert(op == 1)

assert(exec.set_parameter_value_from_handle(
    op,
    "input_param:Message",
    "LuaScriptCreatedMessage",
    { manager_entry = { policy = "create_missing", schema = "message" } }) == 2)
