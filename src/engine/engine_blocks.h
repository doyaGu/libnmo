/**
 * @file engine_blocks.h
 * @brief The built-in building block ports (blocks/), each file registering
 *        the blocks of one DLL, or of one category of Logics.dll.
 */

#ifndef NMO_ENGINE_BLOCKS_H
#define NMO_ENGINE_BLOCKS_H

#include "engine_base.h"

typedef nmo_status_t (*eng_register_fn)(nmo_engine_t *engine);

nmo_status_t eng_register_logics_streaming(nmo_engine_t *engine);
nmo_status_t eng_register_logics_test(nmo_engine_t *engine);
nmo_status_t eng_register_logics_calculator(nmo_engine_t *engine);
nmo_status_t eng_register_logics_interpolator(nmo_engine_t *engine);
nmo_status_t eng_register_logics_loops(nmo_engine_t *engine);
nmo_status_t eng_register_logics_message(nmo_engine_t *engine);
nmo_status_t eng_register_logics_array(nmo_engine_t *engine);
nmo_status_t eng_register_logics_groups(nmo_engine_t *engine);
nmo_status_t eng_register_logics_strings(nmo_engine_t *engine);
nmo_status_t eng_register_logics_attribute(nmo_engine_t *engine);
nmo_status_t eng_register_narratives(nmo_engine_t *engine);
nmo_status_t eng_register_3dtransfo(nmo_engine_t *engine);
nmo_status_t eng_register_collisions(nmo_engine_t *engine);
nmo_status_t eng_register_controllers(nmo_engine_t *engine);
nmo_status_t eng_register_interface(nmo_engine_t *engine);
nmo_status_t eng_register_sounds(nmo_engine_t *engine);
nmo_status_t eng_register_materials(nmo_engine_t *engine);
nmo_status_t eng_register_visuals(nmo_engine_t *engine);
nmo_status_t eng_register_addons(nmo_engine_t *engine);
nmo_status_t eng_register_effects(nmo_engine_t *engine);
nmo_status_t eng_register_physics(nmo_engine_t *engine);
nmo_status_t eng_register_tt_database(nmo_engine_t *engine);
nmo_status_t eng_register_tt_interface_manager(nmo_engine_t *engine);
nmo_status_t eng_register_tt_toolbox(nmo_engine_t *engine);
nmo_status_t eng_register_tt_gravity(nmo_engine_t *engine);
nmo_status_t eng_register_tt_particles(nmo_engine_t *engine);

#endif /* NMO_ENGINE_BLOCKS_H */
