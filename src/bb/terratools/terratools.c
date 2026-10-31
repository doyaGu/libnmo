/**
 * @file terratools.c
 * @brief Registering the Terratools pack: the TT_* building block DLLs.
 */

#include "terratools.h"

/* The DLLs' blocks, in the order they are registered */
static nmo_status_t (*const terratools_sets[])(nmo_engine_t *engine) = {
    eng_register_tt_database,
    eng_register_tt_interface_manager,
    eng_register_tt_toolbox,
    eng_register_tt_gravity,
    eng_register_tt_particles,
};

nmo_status_t nmo_bb_terratools_register(nmo_engine_t *engine)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status = NMO_OK;
    for (size_t i = 0; status == NMO_OK && i < sizeof(terratools_sets) / sizeof(terratools_sets[0]);
         i++) {
        status = terratools_sets[i](engine);
    }
    return status;
}
