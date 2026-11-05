#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_ORIENTATION NMO_GUID(0x5ae25172,0x5c781faa)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct quaternion_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, grandchild, reference, block, out, target_parameter, quaternion_parameter, reference_parameter;
} quaternion_fixture_t;

static void input(quaternion_fixture_t *g, nmo_quaternion_t value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&value,sizeof(value));
}

static void begin(quaternion_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Oriented Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild"); g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *target = fixture_state(&g->f,g->target), *reference = fixture_state(&g->f,g->reference);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(reference); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand);
    parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1; child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    grand->parent = nmo_ref_from_id(g->child); grand->has_entityndata_chunk = 1; grand->world_matrix[12] = 6; grand->world_matrix[13] = 9; grand->world_matrix[14] = 12;
    const eng_matrix_t r = {.m={{0,2,0,0},{-3,0,0,0},{0,0,4,0},{20,30,40,1}}}; memcpy(reference->world_matrix,r.m,sizeof(r.m));
    g->f.script_id = fixture_add_script(&g->f,"Quaternion Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_ORIENTATION,"Set Quaternion Orientation"); g->out = fixture_block(&g->f,GUID_NOP,"After Orientation"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target); input(g,(nmo_quaternion_t){0,0,1,1}); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,2),0);
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    g->quaternion_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,0)));
    g->reference_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,2)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(quaternion_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_ORIENTATION)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void position(quaternion_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1),expected);
}

static void runtime_basis(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,matrix));
    vector((eng_vector_t){matrix[0],matrix[1],matrix[2]},x); vector((eng_vector_t){matrix[4],matrix[5],matrix[6]},y); vector((eng_vector_t){matrix[8],matrix[9],matrix[10]},z);
}

static void basis(quaternion_fixture_t *g, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    runtime_basis(g->f.engine,g->target,x,y,z);
}

static void trigger(quaternion_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_quaternion_orientation, signed_principal_rotations_are_invariant_under_nonzero_quaternion_scale)
{
    const nmo_quaternion_t values[] = {{1,0,0,1},{-1,0,0,1},{0,1,0,1},{0,-1,0,1},{0,0,1,1},{0,0,-1,1}};
    const eng_vector_t x[] = {{2,0,0},{2,0,0},{0,0,2},{0,0,-2},{0,-2,0},{0,2,0}};
    const eng_vector_t y[] = {{0,0,-3},{0,0,3},{0,3,0},{0,3,0},{3,0,0},{-3,0,0}};
    const eng_vector_t z[] = {{0,4,0},{0,-4,0},{-4,0,0},{4,0,0},{0,0,4},{0,0,4}}; const float scales[] = {1e-35f,-1,1e35f};
    for (size_t i = 0; i < COUNT(values); i++) for (size_t j = 0; j < COUNT(scales); j++) {
        quaternion_fixture_t g; begin(&g); nmo_quaternion_t q = values[i]; q.x *= scales[j]; q.y *= scales[j]; q.z *= scales[j]; q.w *= scales[j]; input(&g,q); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,x[i],y[i],z[i]); position(&g,g.target,(eng_vector_t){3,4,5});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_quaternion_orientation, local_scale_restoration_propagates_after_the_hierarchy_flag_keeps_children_in_place)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        quaternion_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),hierarchy); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        basis(&g,(eng_vector_t){0,-2,0},(eng_vector_t){3,0,0},(eng_vector_t){0,0,4});
        position(&g,g.child,hierarchy ? (eng_vector_t){6,2,9} : (eng_vector_t){9,10,21}); position(&g,g.grandchild,hierarchy ? (eng_vector_t){8,1,12} : (eng_vector_t){12,14,33});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),true),(eng_vector_t){2,3,4}); fixture_end(&g.f);
    }
}

TEST(engine_quaternion_orientation, preserves_local_scale_under_nonuniform_parents_instead_of_world_axis_lengths)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        quaternion_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        target->parent = nmo_ref_from_id(g.parent); target->has_entityndata_chunk = 1; target->world_matrix[0] = 4; target->world_matrix[5] = 9; target->world_matrix[10] = 16;
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),hierarchy); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        basis(&g,(eng_vector_t){0,-6,0},(eng_vector_t){6,0,0},(eng_vector_t){0,0,16}); position(&g,g.target,(eng_vector_t){3,4,5});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),true),(eng_vector_t){2,3,4}); fixture_end(&g.f);
    }
}

TEST(engine_quaternion_orientation, affine_references_preserve_shear_and_restore_each_local_axis_scale_without_moving_the_pivot)
{
    for (int shear = 0; shear < 2; shear++) {
        quaternion_fixture_t g; begin(&g); input(&g,(nmo_quaternion_t){0,0,0,7}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.reference);
        if (shear) { eng_matrix_t r = {.m={{2,1,0,0},{.5f,3,0,0},{0,0,4,0},{20,30,40,1}}}; memcpy(((nmo_3dentity_state_t *)fixture_state(&g.f,g.reference))->world_matrix,r.m,sizeof(r.m)); }
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (shear) { float a = sqrtf(5), b = sqrtf(37); basis(&g,(eng_vector_t){4/a,2/a,0},(eng_vector_t){3/b,18/b,0},(eng_vector_t){0,0,4}); }
        else basis(&g,(eng_vector_t){0,2,0},(eng_vector_t){-3,0,0},(eng_vector_t){0,0,4});
        position(&g,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f);
    }
}

TEST(engine_quaternion_orientation, self_references_use_the_previous_world_frame_before_restoring_local_scale)
{
    quaternion_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.target); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,-2,0},(eng_vector_t){3,0,0},(eng_vector_t){0,0,4});
    trigger(&g); basis(&g,(eng_vector_t){-2,0,0},(eng_vector_t){0,-3,0},(eng_vector_t){0,0,4}); position(&g,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f);
}

TEST(engine_quaternion_orientation, reads_live_quaternions_references_and_target_parameters)
{
    quaternion_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); nmo_quaternion_t identity = {0,0,0,1};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.quaternion_parameter,&identity,sizeof(identity))); trigger(&g); basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.reference_parameter,&g.reference,sizeof(g.reference))); trigger(&g); basis(&g,(eng_vector_t){0,2,0},(eng_vector_t){-3,0,0},(eng_vector_t){0,0,4});
    nmo_object_id_t zero = 0; nmo_quaternion_t quarter = {0,0,1,1};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.reference_parameter,&zero,sizeof(zero))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.quaternion_parameter,&quarter,sizeof(quarter)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.parent,sizeof(g.parent))); trigger(&g);
    runtime_basis(g.f.engine,g.parent,(eng_vector_t){0,-2,0},(eng_vector_t){3,0,0},(eng_vector_t){0,0,4}); basis(&g,(eng_vector_t){0,2,0},(eng_vector_t){-3,0,0},(eng_vector_t){0,0,4});
    ASSERT_EQ(4u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_quaternion_orientation, degenerate_quaternions_keep_native_nan_patterns_and_complete_without_parameter_errors)
{
    const nmo_quaternion_t inputs[] = {{0,0,0,0},{0,0,0,NAN},{0,0,0,INFINITY}};
    for (size_t i = 0; i < COUNT(inputs); i++) {
        quaternion_fixture_t g; begin(&g); input(&g,inputs[i]); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++) {
            if (i == 2 && row == col) ASSERT_FLOAT_EQ(1,matrix[4*row+col],0); else ASSERT_TRUE(isnan(matrix[4*row+col]));
        }
        position(&g,g.target,(eng_vector_t){3,4,5}); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_quaternion_orientation, missing_deleted_and_nonentity_references_use_world_orientation)
{
    for (int kind = 0; kind < 3; kind++) {
        quaternion_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),kind == 1 ? (int32_t)g.reference : kind == 2 ? (int32_t)g.block : 0);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.reference);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,-2,0},(eng_vector_t){3,0,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
    }
}

TEST(engine_quaternion_orientation, disconnected_inputs_keep_identity_quaternion_and_hierarchy_defaults)
{
    quaternion_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
    target->world_matrix[0] = 0; target->world_matrix[1] = -2; target->world_matrix[4] = 3; target->world_matrix[5] = 0;
    g.f.value_count = 1; for (int i = 0; i < 3; i++) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,i)),0);
    commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
    position(&g,g.child,(eng_vector_t){0,6,9}); fixture_end(&g.f);
}

TEST(engine_quaternion_orientation, missing_deleted_and_nonentity_targets_report_owner_errors_after_setting_io)
{
    for (int kind = 0; kind < 3; kind++) {
        quaternion_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_quaternion_orientation, signed_principal_rotations_are_invariant_under_nonzero_quaternion_scale);
REGISTER_TEST(engine_quaternion_orientation, local_scale_restoration_propagates_after_the_hierarchy_flag_keeps_children_in_place);
REGISTER_TEST(engine_quaternion_orientation, preserves_local_scale_under_nonuniform_parents_instead_of_world_axis_lengths);
REGISTER_TEST(engine_quaternion_orientation, affine_references_preserve_shear_and_restore_each_local_axis_scale_without_moving_the_pivot);
REGISTER_TEST(engine_quaternion_orientation, self_references_use_the_previous_world_frame_before_restoring_local_scale);
REGISTER_TEST(engine_quaternion_orientation, reads_live_quaternions_references_and_target_parameters);
REGISTER_TEST(engine_quaternion_orientation, degenerate_quaternions_keep_native_nan_patterns_and_complete_without_parameter_errors);
REGISTER_TEST(engine_quaternion_orientation, missing_deleted_and_nonentity_references_use_world_orientation);
REGISTER_TEST(engine_quaternion_orientation, disconnected_inputs_keep_identity_quaternion_and_hierarchy_defaults);
REGISTER_TEST(engine_quaternion_orientation, missing_deleted_and_nonentity_targets_report_owner_errors_after_setting_io);
TEST_MAIN_END()
