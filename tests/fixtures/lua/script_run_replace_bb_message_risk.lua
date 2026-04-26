local exec = require("nmo._executor")

assert(exec.replace_bb(
    2233,
    "055B29FE-662D5CA0",
    "Lua Script Message Probe",
    65536,
    { preserve_links = true, preserve_params = true }) == 1)
