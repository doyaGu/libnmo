local exec = require("nmo._executor")

local first = exec.add_node(6, "302561C4-0D282980", "Script Run Nop Source")
assert(first == 1)

local second = exec.add_node(6, "302561C4-0D282980", "Script Run Nop Target")
assert(second == 2)

assert(exec.add_behavior_link(
    6,
    { operation = first, handle = "output:Out 0" },
    { operation = second, handle = "input:In 0" }) == 3)
