local exec = require("nmo._executor")

assert(exec.fold(
    4692,
    { 2364, 2208 },
    "42414C07-10000007",
    "Lua Script Mapped Fold BB",
    {
        anchor = 2364,
        preserve_boundary = true,
        inputs = {
            { old_index = 0, new_index = 0 },
            { old_index = 1, new_index = 1 },
        },
    }) == 1)
