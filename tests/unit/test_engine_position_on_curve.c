#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_POSITION NMO_GUID(0x676776d0,0x20d457bd)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct position_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, child, path, points[4], block, out;
    size_t count;
} position_fixture_t;

static nmo_object_id_t local(position_fixture_t *g, size_t index)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,index);
}

static void begin(position_fixture_t *g, const eng_vector_t *points, size_t count)
{
    memset(g,0,sizeof(*g)); g->count = count; ASSERT_TRUE(count <= COUNT(g->points)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->f.owner_id = fixture_create(&g->f,NMO_CID_3DENTITY,"Follower");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    nmo_3dentity_state_t *follower = fixture_state(&g->f,g->f.owner_id), *child = fixture_state(&g->f,g->child);
    ASSERT_NOT_NULL(follower); ASSERT_NOT_NULL(child);
    follower->parent = nmo_ref_from_id(g->parent); follower->has_entityndata_chunk = 1;
    follower->world_matrix[0] = 2; follower->world_matrix[5] = 3; follower->world_matrix[10] = 4;
    follower->world_matrix[12] = 3; follower->world_matrix[13] = 4; follower->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->f.owner_id); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    g->path = fixture_create(&g->f,NMO_CID_CURVE,"Path"); nmo_curve_state_t *path = fixture_state(&g->f,g->path); ASSERT_NOT_NULL(path);
    path->opened = 1; path->control_point_count = (uint32_t)count;
    path->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(g->f.session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); ASSERT_NOT_NULL(path->control_point_ids);
    for (size_t i = 0; i < count; i++) {
        g->points[i] = fixture_create(&g->f,NMO_CID_CURVEPOINT,"Point"); nmo_curvepoint_state_t *p = fixture_state(&g->f,g->points[i]); ASSERT_NOT_NULL(p);
        p->base.parent = nmo_ref_from_id(g->path); p->base.has_entityndata_chunk = 1; p->curve = nmo_ref_from_id(g->path); p->linear = 1;
        nmo_curvepoint_set_position(p,points[i].x,points[i].y,points[i].z); path->control_point_ids[i] = nmo_ref_from_id(g->points[i]);
    }
    g->f.script_id = fixture_add_script(&g->f,"Curve Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_POSITION,"Position On Curve"); g->out = fixture_block(&g->f,GUID_NOP,"Out"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->path);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void input_float(position_fixture_t *g, size_t index, float value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,index),&value,sizeof(value));
}

static void roll(position_fixture_t *g, float value)
{
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g->f.tx,local(g,1),(const uint8_t *)&value,sizeof(value)));
}

static void commit(position_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static eng_vector_t up(position_fixture_t *g)
{
    eng_vector_t result; read_value(&g->f,local(g,0),&result,sizeof(result)); return result;
}

static void set_up(position_fixture_t *g, eng_vector_t value)
{
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g->f.engine,local(g,0),&value,sizeof(value)));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    /* SetOrientation replaces zero axis components by 1e-6 before hierarchy propagation. */
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void position(position_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1),expected);
}

static void basis(position_fixture_t *g, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,g->f.owner_id,matrix));
    vector((eng_vector_t){matrix[0],matrix[1],matrix[2]},x); vector((eng_vector_t){matrix[4],matrix[5],matrix[6]},y); vector((eng_vector_t){matrix[8],matrix[9],matrix[10]},z);
}

TEST(engine_position_on_curve, follows_all_six_axes_and_keeps_scale_without_scheduling_another_frame)
{
    const eng_vector_t points[] = {{0,0,0},{16,0,12}};
    for (int axis = 1; axis <= 6; axis++) {
        position_fixture_t g; begin(&g,points,COUNT(points));
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,5),axis); commit(&g);
        vector(up(&g),(eng_vector_t){99,0,0}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,(eng_vector_t){8,0,6});
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); int row = (axis-1)/2;
        eng_vector_t direction = {matrix[4*row],matrix[4*row+1],matrix[4*row+2]}; eng_vector_normalize(&direction);
        float sign = axis%2 ? 1 : -1; vector(direction,(eng_vector_t){.8f*sign,0,.6f*sign});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),true),(eng_vector_t){2,3,4});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block));
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_position_on_curve, historical_vectors_choose_the_first_nonzero_component_and_ignore_its_sign)
{
    const eng_vector_t points[] = {{0,0,0},{16,0,12}}, axes[] = {{-7,0,0},{0,-3,0},{0,0,-9},{0,2,8}};
    const int rows[] = {0,1,2,1};
    for (size_t i = 0; i < COUNT(axes); i++) {
        position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1);
        ((nmo_parameterin_state_t *)fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,5)))->type_guid = CKPGUID_VECTOR;
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,5),&axes[i],sizeof(axes[i])); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
        eng_vector_t direction = {matrix[4*rows[i]],matrix[4*rows[i]+1],matrix[4*rows[i]+2]}; eng_vector_normalize(&direction);
        vector(direction,(eng_vector_t){.8f,0,.6f}); fixture_end(&g.f);
    }
}

TEST(engine_position_on_curve, follow_initializes_from_current_up_and_caches_it_before_roll)
{
    const eng_vector_t points[] = {{0,0,0},{0,0,20}};
    position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1);
    nmo_3dentity_state_t *saved = fixture_state(&g.f,g.f.owner_id); float angle = PI_F/3;
    saved->world_matrix[0] = 2*cosf(angle); saved->world_matrix[1] = 2*sinf(angle);
    saved->world_matrix[4] = -3*sinf(angle); saved->world_matrix[5] = 3*cosf(angle); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); vector(up(&g),(eng_vector_t){-sinf(angle),cosf(angle),0}); fixture_end(&g.f);
    begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1); roll(&g,PI_F); commit(&g); set_up(&g,(eng_vector_t){1,1,0});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float root5 = sqrtf(5);
    vector(up(&g),(eng_vector_t){1/root5,2/root5,0}); basis(&g,(eng_vector_t){-4/root5,2/root5,0},(eng_vector_t){-3/root5,-6/root5,0},(eng_vector_t){0,0,4});
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float root17 = sqrtf(17); vector(up(&g),(eng_vector_t){1/root17,4/root17,0});
    ASSERT_EQ(NMO_ENGINE_BR_OK,eng_block_callback(g.f.engine,eng_map_get(&g.f.engine->node_map,g.block),NMO_ENGINE_MSG_RESET));
    vector(up(&g),(eng_vector_t){99,0,0}); fixture_end(&g.f);
}

TEST(engine_position_on_curve, follow_and_roll_obey_hierarchy_and_preserve_local_scale)
{
    const eng_vector_t points[] = {{0,0,0},{20,0,0}};
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) for (int rolled = 0; rolled < 2; rolled++) {
        position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,6),hierarchy); if (rolled) roll(&g,PI_F*.5f); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (rolled) basis(&g,(eng_vector_t){0,-2,0},(eng_vector_t){0,0,-3},(eng_vector_t){4,0,0});
        else basis(&g,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
        position(&g,g.child,hierarchy ? (rolled ? (eng_vector_t){14,-2,-3} : (eng_vector_t){14,3,-2}) : (eng_vector_t){5,7,9});
        fixture_end(&g.f);
    }
}

TEST(engine_position_on_curve, local_scale_restoration_after_follow_uses_native_child_propagation_under_scaled_parents)
{
    const eng_vector_t points[] = {{0,0,0},{20,0,0}};
    position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,6),0);
    nmo_3dentity_state_t *parent = fixture_state(&g.f,g.parent), *follower = fixture_state(&g.f,g.f.owner_id);
    parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
    follower->world_matrix[0] = 4; follower->world_matrix[5] = 9; follower->world_matrix[10] = 16; commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,0,-8},(eng_vector_t){0,9,0},(eng_vector_t){8,0,0});
    vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),true),(eng_vector_t){2,3,4});
    position(&g,g.child,(eng_vector_t){7.5f,7,18}); fixture_end(&g.f);
}

TEST(engine_position_on_curve, banking_uses_cached_up_signed_local_axes_and_amount_without_applying_roll_alone)
{
    const eng_vector_t points[] = {{0,0,0},{10,0,0},{10,0,-10}};
    const struct { bool seed; int axis; float amount,rotation; } cases[] = {
        {false,5,.5f,-PI_F*.25f}, {true,5,.5f,PI_F*.25f}, {true,6,.5f,-PI_F*.25f}, {true,5,-.5f,-PI_F*.25f},
    };
    for (size_t i = 0; i < COUNT(cases); i++) {
        position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),1);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,5),cases[i].axis); input_float(&g,4,cases[i].amount); roll(&g,PI_F*.5f); commit(&g);
        if (cases[i].seed) set_up(&g,(eng_vector_t){0,1,0});
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float c = cosf(cases[i].rotation), s = sinf(cases[i].rotation);
        basis(&g,(eng_vector_t){2*c,-2*s,0},(eng_vector_t){3*s,3*c,0},(eng_vector_t){0,0,4});
        vector(up(&g),cases[i].seed ? (eng_vector_t){0,1,0} : (eng_vector_t){99,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_position_on_curve, follow_roll_and_bank_compose_around_the_tangent_with_pre_roll_up)
{
    const eng_vector_t points[] = {{0,0,0},{10,0,0},{10,0,-10}};
    position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),1); input_float(&g,4,.5f); roll(&g,PI_F); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float h = sqrtf(.5f), c = cosf(PI_F*1.25f), s = sinf(PI_F*1.25f);
    basis(&g,(eng_vector_t){-2*h*c,-2*s,-2*h*c},(eng_vector_t){-3*h*s,3*c,-3*h*s},(eng_vector_t){4*h,0,-4*h});
    vector(up(&g),(eng_vector_t){0,1,0}); fixture_end(&g.f);
}

TEST(engine_position_on_curve, bank_skips_open_endpoints_and_straight_segments_but_wraps_closed_seams)
{
    const eng_vector_t points[] = {{0,0,0},{10,0,0},{10,0,-10},{0,0,-10}};
    const float steps[] = {0,.1f,1};
    for (size_t i = 0; i < COUNT(steps); i++) {
        position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),1); input_float(&g,1,steps[i]); commit(&g); set_up(&g,(eng_vector_t){0,1,0});
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
    }
    position_fixture_t g; begin(&g,points,COUNT(points)); ((nmo_curve_state_t *)fixture_state(&g.f,g.path))->opened = 0;
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),1); input_float(&g,1,0); input_float(&g,4,.5f); commit(&g); set_up(&g,(eng_vector_t){0,1,0});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float h = sqrtf(.5f);
    basis(&g,(eng_vector_t){2*h,-2*h,0},(eng_vector_t){3*h,3*h,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
}

TEST(engine_position_on_curve, disabled_follow_and_bank_preserve_orientation_and_invalid_axes_are_only_read_when_needed)
{
    const eng_vector_t points[] = {{0,0,0},{16,0,12}};
    for (int active = 0; active < 2; active++) {
        position_fixture_t g; begin(&g,points,COUNT(points)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,5),0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),active); roll(&g,PI_F*.5f); commit(&g);
        ASSERT_EQ(active ? NMO_ERR_VALIDATION_FAILED : NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.f.owner_id,(eng_vector_t){8,0,6}); basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        if (active) { nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); }
        fixture_end(&g.f);
    }
}

TEST(engine_position_on_curve, missing_targets_and_curves_report_errors_after_setting_io)
{
    const eng_vector_t points[] = {{0,0,0},{16,0,12}};
    for (int kind = 0; kind < 3; kind++) {
        position_fixture_t g; begin(&g,points,COUNT(points));
        if (kind == 0) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        commit(&g); if (kind == 2) nmo_engine_destroy_object(g.f.engine,g.path);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(kind == 0 ? NMO_ENGINE_BR_OWNERERROR : NMO_ENGINE_BR_PARAMETERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        position(&g,g.f.owner_id,(eng_vector_t){3,4,5}); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_position_on_curve, follows_all_six_axes_and_keeps_scale_without_scheduling_another_frame);
REGISTER_TEST(engine_position_on_curve, historical_vectors_choose_the_first_nonzero_component_and_ignore_its_sign);
REGISTER_TEST(engine_position_on_curve, follow_initializes_from_current_up_and_caches_it_before_roll);
REGISTER_TEST(engine_position_on_curve, follow_and_roll_obey_hierarchy_and_preserve_local_scale);
REGISTER_TEST(engine_position_on_curve, local_scale_restoration_after_follow_uses_native_child_propagation_under_scaled_parents);
REGISTER_TEST(engine_position_on_curve, banking_uses_cached_up_signed_local_axes_and_amount_without_applying_roll_alone);
REGISTER_TEST(engine_position_on_curve, follow_roll_and_bank_compose_around_the_tangent_with_pre_roll_up);
REGISTER_TEST(engine_position_on_curve, bank_skips_open_endpoints_and_straight_segments_but_wraps_closed_seams);
REGISTER_TEST(engine_position_on_curve, disabled_follow_and_bank_preserve_orientation_and_invalid_axes_are_only_read_when_needed);
REGISTER_TEST(engine_position_on_curve, missing_targets_and_curves_report_errors_after_setting_io);
TEST_MAIN_END()
