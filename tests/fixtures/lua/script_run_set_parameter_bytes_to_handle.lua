local exec = require("nmo._executor")

local node_op = exec.add_node(237, "055B29FE-662D5CA0", "Script Run 2D Text Raw Logger")
assert(node_op == 1)
assert(exec.set_parameter_bytes_from_handle(
    node_op,
    "input_param:Text",
    string.char(0x72, 0x61, 0x77, 0x20, 0x74, 0x72, 0x61, 0x63, 0x65, 0),
    { resize = true }) == 2)
