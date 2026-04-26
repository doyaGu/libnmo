local exec = require("nmo._executor")

assert(exec.fold(
    4692,
    { 2367 },
    "42414C07-10000007",
    "Lua Script Fold BB",
    { anchor = 2367, preserve_boundary = true }) == 1)
