local exec = require("nmo._executor")

local node_op = exec.add_node(237, "18655B3F-68291DC3", "Script Run Parameter Logger")
assert(node_op == 1)
assert(exec.connect_parameter_to_handle(234, node_op, "input_param:String") == 2)
