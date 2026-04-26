local exec = require("nmo._executor")

assert(exec.replace_bb(
    2233,
    "42414C07-10000007",
    "Lua Script Message Probe",
    65536,
    { preserve_links = true, preserve_params = true }) == 1)
