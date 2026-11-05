#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_GET_EULER NMO_GUID(0x0c4977a8,0x6c645d14)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct euler_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, block, out, target_parameter;
} euler_fixture_t;

/* Compose independent row-vector axis rotations instead of the expanded Euler formula. */
static eng_matrix_t rotation(float x, float y, float z)
{
    eng_matrix_t result; eng_matrix_identity(&result);
    const float angles[] = {x,y,z};
    for (int axis = 0; axis < 3; axis++) {
        int p = (axis+1)%3, q = (p+1)%3;
        double s = sin(angles[axis]), c = cos(angles[axis]);
        for (int row = 0; row < 3; row++) {
            double a = result.m[row][p], b = result.m[row][q];
            result.m[row][p] = (float)(a*c-b*s); result.m[row][q] = (float)(a*s+b*c);
        }
    }
    return result;
}

static void begin(euler_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent");
    g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Euler Target");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *target = fixture_state(&g->f,g->target);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child); ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child);
    eng_matrix_t m = rotation(0,0,.6f); m.m[3][0] = 10;
    memcpy(parent->world_matrix,m.m,sizeof(m.m)); parent->has_entityndata_chunk = 1;
    m = rotation(.3f,-.4f,.7f); m.m[3][0] = 3; m.m[3][1] = 4; m.m[3][2] = 5;
    memcpy(target->world_matrix,m.m,sizeof(m.m)); target->has_entityndata_chunk = 1; target->parent = nmo_ref_from_id(g->parent);
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 6; child->world_matrix[13] = 8; child->world_matrix[14] = 10;
    g->f.script_id = fixture_add_script(&g->f,"Euler Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_GET_EULER,"Get Euler Orientation"); g->out = fixture_block(&g->f,GUID_NOP,"After Euler");
    ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(euler_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_GET_EULER));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void trigger(euler_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

static void angles(engine_fixture_t *f, nmo_object_id_t block, float x, float y, float z)
{
    const float expected[] = {x,y,z};
    for (int i = 0; i < 3; i++) {
        float actual = NAN; read_value(f,BLOCK_POUT(f,block,i),&actual,sizeof(actual));
        ASSERT_TRUE(isfinite(actual)); ASSERT_FLOAT_EQ(expected[i],actual,3e-7f);
    }
}

static void same_world(nmo_engine_t *engine, nmo_object_id_t id, const float *expected)
{
    float actual[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,actual));
    ASSERT_EQ(0,memcmp(actual,expected,sizeof(actual)));
}

TEST(engine_get_euler, reads_world_angles_in_radians_without_changing_target_children_or_scheduling)
{
    euler_fixture_t g; begin(&g); commit(&g);
    for (int i = 0; i < 3; i++) {
        const nmo_parameterout_state_t *state = fixture_state(&g.f,BLOCK_POUT(&g.f,g.block,i)); ASSERT_NOT_NULL(state);
        ASSERT_TRUE(nmo_guid_equals(CKPGUID_ANGLE,state->base.type_guid));
    }
    angles(&g.f,g.block,0,0,0);
    float target[16], child[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,target));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,child));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); angles(&g.f,g.block,.3f,-.4f,.7f);
    same_world(g.f.engine,g.target,target); same_world(g.f.engine,g.child,child);
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_get_euler, signed_axis_rotations_and_mixed_angles_follow_the_live_world_matrix)
{
    euler_fixture_t g; begin(&g); commit(&g);
    const float xz[] = {-2.7f,-.8f,0,.3f,2.8f}, y[] = {-1.4f,-.4f,0,.6f,1.4f};
    for (size_t x = 0; x < COUNT(xz); x++) for (size_t p = 0; p < COUNT(y); p++) for (size_t z = 0; z < COUNT(xz); z++) {
        eng_matrix_t m = rotation(xz[x],y[p],xz[z]); m.m[3][0] = 300; m.m[3][1] = -400;
        eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
        angles(&g.f,g.block,xz[x],y[p],xz[z]); same_world(g.f.engine,g.target,&m.m[0][0]);
    }
    ASSERT_EQ(125u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_get_euler, scaled_and_sheared_rows_are_read_directly_without_rotation_normalization)
{
    euler_fixture_t g; begin(&g); commit(&g);
    const eng_matrix_t m = {.m={{3,4,-12,0},{.25f,5,6,0},{.5f,-2,8,0},{100,200,300,1}}};
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
    angles(&g.f,g.block,.643501109f,1.176005207f,.927295218f); same_world(g.f.engine,g.target,&m.m[0][0]); fixture_end(&g.f);
}

TEST(engine_get_euler, exact_gimbal_threshold_uses_the_locked_branch_with_zero_z)
{
    euler_fixture_t g; begin(&g); commit(&g);
    float edge = ldexpf(1,-19); const float sizes[] = {0,nextafterf(edge,0),edge,nextafterf(edge,FLT_MAX)};
    for (int sign = -1; sign <= 1; sign += 2) for (size_t i = 0; i < COUNT(sizes); i++) {
        eng_matrix_t m = {.m={{0,sizes[i],(float)sign,0},{0,0,.6f,0},{0,-1,.8f,0},{0,0,0,1}}};
        eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
        bool locked = i < 3;
        angles(&g.f,g.block,locked ? PI_F*.5f : .643501109f,(float)atan2(-sign,(double)sizes[i]),locked ? 0 : PI_F*.5f);
    }
    fixture_end(&g.f);
}

TEST(engine_get_euler, finite_extreme_axes_keep_the_native_wide_norm_and_tiny_axes_take_the_locked_branch)
{
    euler_fixture_t g; begin(&g); ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->parent = nmo_ref_from_id(0); commit(&g);
    eng_matrix_t m = {.m={{FLT_MAX,FLT_MAX,-FLT_MAX,0},{0,0,1,0},{0,-1,1,0},{0,0,0,1}}};
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
    angles(&g.f,g.block,PI_F*.25f,.615479709f,PI_F*.25f);
    float tiny = nextafterf(0,1); m.m[0][0] = m.m[0][1] = tiny; m.m[0][2] = -tiny;
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&m,true); trigger(&g);
    angles(&g.f,g.block,PI_F*.5f,.615479709f,0); fixture_end(&g.f);
}

TEST(engine_get_euler, live_targets_can_switch_between_entities)
{
    euler_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); angles(&g.f,g.block,.3f,-.4f,.7f);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.parent,sizeof(g.parent))); trigger(&g);
    angles(&g.f,g.block,0,0,.6f);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.f.owner_id,sizeof(g.f.owner_id))); trigger(&g);
    angles(&g.f,g.block,0,0,0); fixture_end(&g.f);
}

TEST(engine_get_euler, absent_deleted_and_nonentity_targets_preserve_outputs_and_leave_input_active)
{
    for (int kind = 0; kind < 3; kind++) {
        euler_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        else {
            nmo_object_id_t bad = kind == 0 ? 0 : g.block;
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&bad,sizeof(bad)));
        }
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); angles(&g.f,g.block,.3f,-.4f,.7f);
        ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_get_euler, reads_world_angles_in_radians_without_changing_target_children_or_scheduling);
REGISTER_TEST(engine_get_euler, signed_axis_rotations_and_mixed_angles_follow_the_live_world_matrix);
REGISTER_TEST(engine_get_euler, scaled_and_sheared_rows_are_read_directly_without_rotation_normalization);
REGISTER_TEST(engine_get_euler, exact_gimbal_threshold_uses_the_locked_branch_with_zero_z);
REGISTER_TEST(engine_get_euler, finite_extreme_axes_keep_the_native_wide_norm_and_tiny_axes_take_the_locked_branch);
REGISTER_TEST(engine_get_euler, live_targets_can_switch_between_entities);
REGISTER_TEST(engine_get_euler, absent_deleted_and_nonentity_targets_preserve_outputs_and_leave_input_active);
TEST_MAIN_END()
