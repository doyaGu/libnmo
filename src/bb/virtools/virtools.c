/**
 * @file virtools.c
 * @brief Registering the Virtools pack: the stock building block DLLs.
 */

#include "virtools.h"

/* The DLLs' blocks, in the order they are registered */
static nmo_status_t (*const virtools_sets[])(nmo_engine_t *engine) = {
    eng_register_logics_streaming,
    eng_register_logics_test,
    eng_register_logics_calculator,
    eng_register_logics_interpolator,
    eng_register_logics_loops,
    eng_register_logics_message,
    eng_register_logics_array,
    eng_register_logics_groups,
    eng_register_logics_strings,
    eng_register_logics_attribute,
    eng_register_narratives,
    eng_register_effects,
    eng_register_cameras,
    eng_register_characters,
    eng_register_animation_sync,
    eng_register_lights,
    eng_register_world_environments,
    eng_register_controllers,
    eng_register_interface,
    eng_register_sounds,
    eng_register_materials,
    eng_register_visuals,
    eng_register_addons,
    eng_register_3dtransfo,
    eng_register_collisions,
    eng_register_grids,
};

nmo_status_t nmo_bb_virtools_register(nmo_engine_t *engine)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status = NMO_OK;
    for (size_t i = 0; status == NMO_OK && i < sizeof(virtools_sets) / sizeof(virtools_sets[0]); i++) {
        status = virtools_sets[i](engine);
    }
    return status;
}
