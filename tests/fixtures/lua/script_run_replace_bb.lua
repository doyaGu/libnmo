local exec = require("nmo._executor")

assert(exec.replace_bb(
    343,
    "42414C02-10000002",
    "Lua Script Replace BB",
    65536,
    { preserve_links = true, preserve_params = true }) == 1)
