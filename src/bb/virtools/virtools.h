/**
 * @file virtools.h
 * @brief The Virtools pack's files, each registering the blocks of one DLL,
 *        or of one category of Logics.dll.
 */

#ifndef NMO_BB_VIRTOOLS_INTERNAL_H
#define NMO_BB_VIRTOOLS_INTERNAL_H

#include "../bb_sdk.h"
#include "bb/nmo_bb_virtools.h"

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
nmo_status_t eng_register_effects(nmo_engine_t *engine);
nmo_status_t eng_register_cameras(nmo_engine_t *engine);
nmo_status_t eng_register_characters(nmo_engine_t *engine);
nmo_status_t eng_register_share_animation(nmo_engine_t *engine);
nmo_status_t eng_register_ik_position(nmo_engine_t *engine);
nmo_status_t eng_register_character_controller(nmo_engine_t *engine);
nmo_status_t eng_register_character_goto(nmo_engine_t *engine);
nmo_status_t eng_register_character_curve(nmo_engine_t *engine);
nmo_status_t eng_register_enhanced_character_curve(nmo_engine_t *engine);
nmo_status_t eng_register_animation_sync(nmo_engine_t *engine);
nmo_status_t eng_register_unlimited_controller(nmo_engine_t *engine);
nmo_status_t eng_register_lights(nmo_engine_t *engine);
nmo_status_t eng_register_world_environments(nmo_engine_t *engine);
nmo_status_t eng_register_controllers(nmo_engine_t *engine);
nmo_status_t eng_register_interface(nmo_engine_t *engine);
nmo_status_t eng_register_interface_layouts(nmo_engine_t *engine);
nmo_status_t eng_register_sounds(nmo_engine_t *engine);
nmo_status_t eng_register_materials(nmo_engine_t *engine);
nmo_status_t eng_register_visuals(nmo_engine_t *engine);
nmo_status_t eng_register_addons(nmo_engine_t *engine);
nmo_status_t eng_register_lightmap(nmo_engine_t *engine);
nmo_status_t eng_register_3dtransfo(nmo_engine_t *engine);
nmo_status_t eng_register_billboard(nmo_engine_t *engine);
nmo_status_t eng_register_portals(nmo_engine_t *engine);
nmo_status_t eng_register_portal_management(nmo_engine_t *engine);
nmo_status_t eng_register_move_to(nmo_engine_t *engine);
nmo_status_t eng_register_curve_follow(nmo_engine_t *engine);
nmo_status_t eng_register_curve_properties(nmo_engine_t *engine);
nmo_status_t eng_register_mimic(nmo_engine_t *engine);
nmo_status_t eng_register_collisions(nmo_engine_t *engine);
nmo_status_t eng_register_grids(nmo_engine_t *engine);
nmo_status_t eng_register_mesh_modifiers(nmo_engine_t *engine);
/* BB_Orientate (3dtransfo.c): the entity's `dir_mode` axis turned toward `dir`; the up used */
eng_vector_t eng_bb_orientate(nmo_engine_t *engine, int32_t entity, eng_vector_t dir, int32_t dir_mode,
                              float roll, eng_vector_t up, float speed, bool keep);
nmo_status_t eng_register_grid_path(nmo_engine_t *engine);

/* Movie Player (Materials) and Sprite Movie Player (Visuals): the target's slots over time */
int virtools_movie_player(nmo_engine_block_t *b);
/* Vx3DInterpolateMatrix (Interpolator Matrix, Collision Detection): rotation slerped, position and scale
   linear */
void vx_interpolate_matrix(float step, const float a[4][4], const float c[4][4], float r[4][4]);

#endif /* NMO_BB_VIRTOOLS_INTERNAL_H */
