#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_LOOK_POS NMO_GUID(0xee6e5e6e,0xe6e6e6e6)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct look_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, grandchild, block, out, target_parameter, point_parameter, speed_parameter;
} look_fixture_t;

static void point(look_fixture_t *g, eng_vector_t value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&value,sizeof(value));
}

static void begin(look_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Looking Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *target = fixture_state(&g->f,g->target);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand);
    parent->has_entityndata_chunk = target->has_entityndata_chunk = child->has_entityndata_chunk = grand->has_entityndata_chunk = 1;
    parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    grand->parent = nmo_ref_from_id(g->child); grand->world_matrix[12] = 6; grand->world_matrix[13] = 9; grand->world_matrix[14] = 12;
    g->f.script_id = fixture_add_script(&g->f,"Look Pos Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_LOOK_POS,"Look At Pos"); g->out = fixture_block(&g->f,GUID_NOP,"After Look"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target); point(g,(eng_vector_t){8,4,5});
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    g->point_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,0)));
    g->speed_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,1)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(look_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_LOOK_POS));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,2e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,2e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,2e-4f);
}

static void position(nmo_engine_t *engine, nmo_object_id_t target, eng_vector_t expected)
{
    vector(eng_entity_position(engine,eng_entity_index(engine,target),-1),expected);
}

static void basis(nmo_engine_t *engine, nmo_object_id_t target, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,target,m));
    vector((eng_vector_t){m[0],m[1],m[2]},x); vector((eng_vector_t){m[4],m[5],m[6]},y); vector((eng_vector_t){m[8],m[9],m[10]},z);
}

static void trigger(look_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_look_at_pos, default_speed_looks_at_world_points_and_moves_descendants_without_scheduling)
{
    look_fixture_t g; begin(&g); commit(&g); float speed = 0; read_value(&g.f,g.speed_parameter,&speed,sizeof(speed)); ASSERT_FLOAT_EQ(1,speed,0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    basis(g.f.engine,g.target,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
    position(g.f.engine,g.target,(eng_vector_t){3,4,5}); position(g.f.engine,g.child,(eng_vector_t){7,7,3}); position(g.f.engine,g.grandchild,(eng_vector_t){10,9,2});
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_FLOAT_EQ(2,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[0],0); fixture_end(&g.f);
}

TEST(engine_look_at_pos, following_speed_interpolates_or_extrapolates_normalized_directions_independently_of_delta)
{
    const float speeds[] = {0,.25f,.5f,1,2,-1}, deltas[] = {0,1,1000};
    for (size_t s = 0; s < COUNT(speeds); s++) for (size_t d = 0; d < COUNT(deltas); d++) {
        look_fixture_t g; begin(&g); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&speeds[s],sizeof(float)); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,deltas[d]));
        float n = hypotf(speeds[s],1-speeds[s]), x = speeds[s]/n, z = (1-speeds[s])/n;
        basis(g.f.engine,g.target,(eng_vector_t){2*z,0,-2*x},(eng_vector_t){0,3,0},(eng_vector_t){4*x,0,4*z});
        position(g.f.engine,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, hierarchy_can_keep_children_in_place_for_an_unparented_target)
{
    look_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),0); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    basis(g.f.engine,g.target,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
    position(g.f.engine,g.child,(eng_vector_t){5,7,9}); position(g.f.engine,g.grandchild,(eng_vector_t){6,9,12}); fixture_end(&g.f);
}

TEST(engine_look_at_pos, local_scale_restoration_after_orientation_also_propagates_when_hierarchy_is_disabled)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        look_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        target->parent = nmo_ref_from_id(g.parent); target->world_matrix[0] = 4; target->world_matrix[5] = 9; target->world_matrix[10] = 16;
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),hierarchy); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        basis(g.f.engine,g.target,(eng_vector_t){0,0,-8},(eng_vector_t){0,9,0},(eng_vector_t){8,0,0});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),true),(eng_vector_t){2,3,4});
        position(g.f.engine,g.child,hierarchy ? (eng_vector_t){5,7,1} : (eng_vector_t){4,7,13});
        position(g.f.engine,g.grandchild,hierarchy ? (eng_vector_t){6.5f,9,-1} : (eng_vector_t){4.5f,9,19}); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, coincident_positions_leave_rolled_or_sheared_matrices_and_children_unchanged)
{
    look_fixture_t g; begin(&g); point(&g,(eng_vector_t){3,4,5}); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
    target->world_matrix[1] = .5f; target->world_matrix[4] = -.25f; commit(&g);
    const nmo_object_id_t ids[] = {g.target,g.child,g.grandchild}; float before[COUNT(ids)][16];
    for (size_t i = 0; i < COUNT(ids); i++) ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,ids[i],before[i]));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    for (size_t i = 0; i < COUNT(ids); i++) { float after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,ids[i],after)); ASSERT_EQ(0,memcmp(before[i],after,sizeof(after))); }
    ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_look_at_pos, tiny_and_large_nonzero_displacements_are_normalized_without_a_cutoff)
{
    const float sizes[] = {1e-35f,1e35f};
    for (size_t i = 0; i < COUNT(sizes); i++) {
        look_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        target->world_matrix[12] = target->world_matrix[13] = target->world_matrix[14] = 0;
        point(&g,(eng_vector_t){sizes[i],0,0}); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        basis(g.f.engine,g.target,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, vertical_opposing_and_zero_old_directions_retain_native_nan_propagation)
{
    for (int kind = 0; kind < 3; kind++) {
        look_fixture_t g; begin(&g);
        if (kind == 0) point(&g,(eng_vector_t){3,9,5});
        if (kind == 1) { const float half = .5f; point(&g,(eng_vector_t){3,4,0}); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&half,sizeof(half)); }
        if (kind == 2) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[10] = 0;
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        for (int axis = 0; axis < (kind == 0 ? 2 : 3); axis++) for (int c = 0; c < 3; c++) ASSERT_TRUE(isnan(matrix[4*axis+c]));
        if (kind == 0) { ASSERT_TRUE(isfinite(matrix[8]) && isfinite(matrix[9]) && isfinite(matrix[10])); ASSERT_FLOAT_EQ(4,matrix[9],1e-5f); }
        ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, live_positions_speeds_and_targets_are_read_on_each_activation)
{
    look_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    eng_vector_t p = {3,4,0}; float half = .5f;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.point_parameter,&p,sizeof(p)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.speed_parameter,&half,sizeof(half))); trigger(&g,1000);
    float h = sqrtf(.5f); basis(g.f.engine,g.target,(eng_vector_t){-2*h,0,-2*h},(eng_vector_t){0,3,0},(eng_vector_t){4*h,0,-4*h});
    p = (eng_vector_t){5,0,0}; float one = 1;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.point_parameter,&p,sizeof(p)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.speed_parameter,&one,sizeof(one)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.f.owner_id,sizeof(g.f.owner_id))); trigger(&g,0);
    basis(g.f.engine,g.f.owner_id,(eng_vector_t){0,0,-1},(eng_vector_t){0,1,0},(eng_vector_t){1,0,0}); ASSERT_EQ(3u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_look_at_pos, missing_deleted_and_nonentity_targets_consume_input_and_activate_output_before_owner_error)
{
    for (int kind = 0; kind < 3; kind++) {
        look_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_look_at_pos, default_speed_looks_at_world_points_and_moves_descendants_without_scheduling);
REGISTER_TEST(engine_look_at_pos, following_speed_interpolates_or_extrapolates_normalized_directions_independently_of_delta);
REGISTER_TEST(engine_look_at_pos, hierarchy_can_keep_children_in_place_for_an_unparented_target);
REGISTER_TEST(engine_look_at_pos, local_scale_restoration_after_orientation_also_propagates_when_hierarchy_is_disabled);
REGISTER_TEST(engine_look_at_pos, coincident_positions_leave_rolled_or_sheared_matrices_and_children_unchanged);
REGISTER_TEST(engine_look_at_pos, tiny_and_large_nonzero_displacements_are_normalized_without_a_cutoff);
REGISTER_TEST(engine_look_at_pos, vertical_opposing_and_zero_old_directions_retain_native_nan_propagation);
REGISTER_TEST(engine_look_at_pos, live_positions_speeds_and_targets_are_read_on_each_activation);
REGISTER_TEST(engine_look_at_pos, missing_deleted_and_nonentity_targets_consume_input_and_activate_output_before_owner_error);
TEST_MAIN_END()
