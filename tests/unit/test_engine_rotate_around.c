#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_AROUND NMO_GUID(0xfdfd999e,0xdefeded8)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct around_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, reference, block, out;
} around_fixture_t;

static nmo_object_id_t source(around_fixture_t *g, int index)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,index)));
}

static void angle(around_fixture_t *g, float value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,1),&value,sizeof(value));
}

static void begin(around_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Orbiter");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child), *reference = fixture_state(&g->f,g->reference);
    ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(reference);
    target->parent = nmo_ref_from_id(g->parent); target->has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    reference->world_matrix[12] = 10; reference->world_matrix[13] = 20; reference->world_matrix[14] = 30;
    g->f.script_id = fixture_add_script(&g->f,"Orbit Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_AROUND,"Rotate Around"); g->out = fixture_block(&g->f,GUID_NOP,"After Orbit"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    const eng_vector_t axis = {0,1,0}; fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&axis,sizeof(axis));
    angle(g,PI_F*.5f); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,2),0);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(around_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_AROUND));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void position(around_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1),expected);
}

static void basis(around_fixture_t *g, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,g->target,matrix));
    vector((eng_vector_t){matrix[0],matrix[1],matrix[2]},x); vector((eng_vector_t){matrix[4],matrix[5],matrix[6]},y); vector((eng_vector_t){matrix[8],matrix[9],matrix[10]},z);
}

static void set_position(around_fixture_t *g, nmo_object_id_t id, eng_vector_t point)
{
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&point,-1,false);
}

static void trigger(around_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_rotate_around, rotates_about_signed_and_nonunit_axes_without_scheduling_another_frame)
{
    const eng_vector_t axes[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{7,7,7},{-2,-2,-2}};
    const eng_vector_t expected[] = {{3,5,-4},{3,-5,4},{-5,4,3},{5,4,-3},{4,-3,5},{-4,3,5},{4,5,3},{5,3,4}};
    for (size_t i = 0; i < COUNT(axes); i++) {
        around_fixture_t g; begin(&g); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&axes[i],sizeof(axes[i]));
        if (i >= 6) angle(&g,PI_F*2/3);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,expected[i]);
        basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, orientation_and_hierarchy_flags_apply_to_both_rotation_and_translation)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        eng_matrix_t parent; eng_matrix_from_euler(&parent,.2f,-.7f,.3f);
        for (int j = 0; j < 3; j++) { parent.m[0][j] *= 2; parent.m[1][j] *= 3; parent.m[2][j] *= 4; }
        parent.m[3][0] = 100; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.parent),&parent,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){-5,4,3});
        position(&g,g.child,!hierarchy ? (eng_vector_t){5,7,9} : keep_orientation ? (eng_vector_t){-3,7,7} : (eng_vector_t){-9,7,5});
        if (keep_orientation) basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        else basis(&g,(eng_vector_t){0,0,2},(eng_vector_t){0,3,0},(eng_vector_t){-4,0,0});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),false),(eng_vector_t){2,3,4});
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, orbits_in_scaled_and_sheared_reference_space_while_rotating_orientation_about_the_world_axis)
{
    for (int shear = 0; shear < 2; shear++) for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.reference); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation);
        nmo_3dentity_state_t *reference = fixture_state(&g.f,g.reference), *target = fixture_state(&g.f,g.target);
        reference->world_matrix[0] = shear ? 2 : 0; reference->world_matrix[1] = shear ? 1 : 2;
        reference->world_matrix[4] = shear ? 0 : -3; reference->world_matrix[5] = shear ? 3 : 0; reference->world_matrix[10] = 4;
        target->world_matrix[12] = shear ? 12 : 4; target->world_matrix[13] = shear ? 27 : 22; target->world_matrix[14] = 42; commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,shear ? (eng_vector_t){4,23,34} : (eng_vector_t){4,14,34});
        if (keep_orientation) basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        else if (shear) basis(&g,(eng_vector_t){0,0,2},(eng_vector_t){0,3,0},(eng_vector_t){-4,0,0});
        else basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,0,3},(eng_vector_t){0,-4,0});
        fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, descendant_references_are_sampled_after_orientation_propagation)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.child);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,!keep_orientation && hierarchy ? (eng_vector_t){1,4,11} : (eng_vector_t){9,4,7});
        position(&g,g.child,!hierarchy ? (eng_vector_t){5,7,9} : keep_orientation ? (eng_vector_t){11,7,11} : (eng_vector_t){-3,7,13}); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, self_references_keep_the_pivot_position_and_use_the_current_local_axis)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.target);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,(eng_vector_t){3,4,5});
        if (keep_orientation) basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        else basis(&g,(eng_vector_t){0,0,2},(eng_vector_t){0,3,0},(eng_vector_t){-4,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, tiny_angles_move_the_orbit_even_when_orientation_rotation_is_below_its_threshold)
{
    around_fixture_t g; begin(&g); angle(&g,1e-8f); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),0); commit(&g);
    set_position(&g,g.target,(eng_vector_t){0,0,1e8f}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){-1,0,1e8f});
    basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
}

TEST(engine_rotate_around, resamples_live_angles_axes_reference_transforms_and_current_positions)
{
    around_fixture_t g; begin(&g); const eng_vector_t axis = {0,0,1}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&axis,sizeof(axis));
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.reference); commit(&g); set_position(&g,g.reference,(eng_vector_t){10,0,0});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){14,7,5});
    float negative = -PI_F*.5f; eng_vector_t y = {0,1,0};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,0),&y,sizeof(y))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,1),&negative,sizeof(negative)));
    set_position(&g,g.reference,(eng_vector_t){20,0,0}); trigger(&g,1000); position(&g,g.target,(eng_vector_t){25,7,6});
    basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
}

TEST(engine_rotate_around, missing_deleted_and_nonentity_references_use_the_world_origin)
{
    for (int kind = 0; kind < 3; kind++) {
        around_fixture_t g; begin(&g);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),kind == 1 ? (int32_t)g.reference : kind == 2 ? (int32_t)g.block : 0);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.reference);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){-5,4,3}); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, zero_axes_keep_native_nan_results_instead_of_silently_becoming_noops)
{
    for (int zero_angle = 0; zero_angle < 2; zero_angle++) {
        around_fixture_t g; begin(&g); const eng_vector_t zero = {0,0,0}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&zero,sizeof(zero));
        if (zero_angle) angle(&g,0);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        ASSERT_TRUE(isnan(matrix[12]) && isnan(matrix[13]) && isnan(matrix[14]));
        basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, missing_and_deleted_targets_report_owner_errors_before_consuming_input)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        around_fixture_t g; begin(&g); if (!deleted) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        commit(&g); if (deleted) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, disconnected_inputs_preserve_native_axis_angle_orientation_and_hierarchy_defaults)
{
    for (int missing_angle = 0; missing_angle < 2; missing_angle++) {
        around_fixture_t g; begin(&g);
        nmo_object_id_t target = nmo_parameterin_source_id(fixture_state(&g.f,fixture_target(&g.f,g.block))), rotation = source(&g,1);
        size_t kept = 0;
        for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param == target || (!missing_angle && g.f.values[i].param == rotation)) g.f.values[kept++] = g.f.values[i];
        g.f.value_count = kept;
        for (int i = 0; i < 5; i++) if (missing_angle || i != 1) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,i)),0);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,missing_angle ? (eng_vector_t){3,4,5} : (eng_vector_t){-5,4,3});
        position(&g,g.child,missing_angle ? (eng_vector_t){5,7,9} : (eng_vector_t){-3,7,7});
        basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_rotate_around, rotates_about_signed_and_nonunit_axes_without_scheduling_another_frame);
REGISTER_TEST(engine_rotate_around, orientation_and_hierarchy_flags_apply_to_both_rotation_and_translation);
REGISTER_TEST(engine_rotate_around, orbits_in_scaled_and_sheared_reference_space_while_rotating_orientation_about_the_world_axis);
REGISTER_TEST(engine_rotate_around, descendant_references_are_sampled_after_orientation_propagation);
REGISTER_TEST(engine_rotate_around, self_references_keep_the_pivot_position_and_use_the_current_local_axis);
REGISTER_TEST(engine_rotate_around, tiny_angles_move_the_orbit_even_when_orientation_rotation_is_below_its_threshold);
REGISTER_TEST(engine_rotate_around, resamples_live_angles_axes_reference_transforms_and_current_positions);
REGISTER_TEST(engine_rotate_around, missing_deleted_and_nonentity_references_use_the_world_origin);
REGISTER_TEST(engine_rotate_around, zero_axes_keep_native_nan_results_instead_of_silently_becoming_noops);
REGISTER_TEST(engine_rotate_around, missing_and_deleted_targets_report_owner_errors_before_consuming_input);
REGISTER_TEST(engine_rotate_around, disconnected_inputs_preserve_native_axis_angle_orientation_and_hierarchy_defaults);
TEST_MAIN_END()
