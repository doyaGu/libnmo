#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_MIMIC NMO_GUID(0x12db7b06,0x74c514c3)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct mimic_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, source_parent, source, child, block, out;
} mimic_fixture_t;

static nmo_object_id_t entity(mimic_fixture_t *g, const char *name, nmo_object_id_t parent,
    eng_vector_t position, eng_vector_t scale, float angle)
{
    nmo_object_id_t id = fixture_create(&g->f,NMO_CID_3DENTITY,name);
    nmo_3dentity_state_t *state = fixture_state(&g->f,id);
    if (state == NULL) return 0;
    state->parent = nmo_ref_from_id(parent); state->has_entityndata_chunk = 1;
    eng_matrix_t matrix; eng_matrix_from_euler(&matrix,0,0,angle);
    const float lengths[] = {scale.x,scale.y,scale.z};
    for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) matrix.m[row][column] *= lengths[row];
    matrix.m[3][0] = position.x; matrix.m[3][1] = position.y; matrix.m[3][2] = position.z;
    memcpy(state->world_matrix,&matrix,sizeof(matrix)); return id;
}

static nmo_object_id_t local(mimic_fixture_t *g)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,0);
}

static void begin(mimic_fixture_t *g, bool time)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = entity(g,"Parent",0,(eng_vector_t){0},(eng_vector_t){1,1,1},0);
    g->source_parent = entity(g,"Source Parent",0,(eng_vector_t){0},(eng_vector_t){1,1,1},0);
    g->f.owner_id = entity(g,"Follower",g->parent,(eng_vector_t){10,20,30},(eng_vector_t){2,3,4},0);
    g->child = entity(g,"Child",g->f.owner_id,(eng_vector_t){12,23,34},(eng_vector_t){1,1,1},0);
    g->source = entity(g,"Source",g->source_parent,(eng_vector_t){30,60,90},(eng_vector_t){6,7,8},PI_F*.5f);
    ASSERT_TRUE(g->f.owner_id != 0 && g->source != 0 && g->child != 0);
    g->f.script_id = fixture_add_script(&g->f,"Mimic Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_MIMIC,"Mimic"); g->out = fixture_block(&g->f,GUID_NOP,"Out"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->source);
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,local(g),time ? "TRUE" : "FALSE"));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void settings(mimic_fixture_t *g, uint32_t mask, float position, float orientation, float scale)
{
    const float speeds[] = {position,orientation,scale};
    for (size_t i = 0; i < COUNT(speeds); i++) {
        fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1+2*i),(mask & (1u<<i)) != 0);
        fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,2+2*i),&speeds[i],sizeof(float));
    }
}

static void commit(mimic_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_MIMIC));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void position(mimic_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1);
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-5f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-5f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-5f);
}

static void pose(mimic_fixture_t *g, float angle, eng_vector_t scale)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,g->f.owner_id,matrix));
    ASSERT_FLOAT_EQ(scale.x*cosf(angle),matrix[0],1e-5f); ASSERT_FLOAT_EQ(scale.x*sinf(angle),matrix[1],1e-5f);
    ASSERT_FLOAT_EQ(-scale.y*sinf(angle),matrix[4],1e-5f); ASSERT_FLOAT_EQ(scale.y*cosf(angle),matrix[5],1e-5f); ASSERT_FLOAT_EQ(scale.z,matrix[10],1e-5f);
}

TEST(engine_mimic, position_orientation_and_scale_are_independently_enabled_and_weighted)
{
    for (uint32_t mask = 0; mask < 8; mask++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,mask,.25f,.5f,.5f); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100));
        position(&g,g.f.owner_id,(mask & 1) ? (eng_vector_t){15,30,45} : (eng_vector_t){10,20,30});
        pose(&g,(mask & 2) ? PI_F*.25f : 0,(mask & 4) ? (eng_vector_t){4,5,6} : (eng_vector_t){2,3,4});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        float source[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.source,source));
        ASSERT_EQ(0,memcmp(source,((nmo_3dentity_state_t *)fixture_state(&g.f,g.source))->world_matrix,sizeof(source)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
        ASSERT_FLOAT_EQ(10,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_mimic, time_scaling_caps_each_weight_only_above_one_and_preserves_negative_values)
{
    mimic_fixture_t g; begin(&g,true); settings(&g,7,2,.5f,-.5f); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.f.owner_id,(eng_vector_t){30,60,90});
    pose(&g,PI_F*.5f*.35f,(eng_vector_t){.6f,1.6f,2.6f}); fixture_end(&g.f);
    const float speeds[] = {2,-.5f};
    for (size_t i = 0; i < COUNT(speeds); i++) {
        begin(&g,false); settings(&g,1,speeds[i],0,0); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.f.owner_id,i == 0 ? (eng_vector_t){50,100,150} : (eng_vector_t){0,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_mimic, orientation_uses_the_short_arc_extrapolation_and_normalized_close_angle_blends)
{
    const float factors[] = {.5f,1.5f,-.5f};
    for (size_t i = 0; i < COUNT(factors); i++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,2,0,factors[i],0);
        nmo_3dentity_state_t *follower = fixture_state(&g.f,g.f.owner_id), *source = fixture_state(&g.f,g.source);
        eng_matrix_t a,b; eng_matrix_from_euler(&a,0,0,170*PI_F/180); eng_matrix_from_euler(&b,0,0,-170*PI_F/180);
        for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) {
            follower->world_matrix[4*row+column] = a.m[row][column]*(row+2);
            source->world_matrix[4*row+column] = b.m[row][column]*(row+6);
        }
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        pose(&g,(170+20*factors[i])*PI_F/180,(eng_vector_t){2,3,4}); fixture_end(&g.f);
    }
    mimic_fixture_t g; begin(&g,false); settings(&g,2,0,.25f,0);
    nmo_3dentity_state_t *source = fixture_state(&g.f,g.source); eng_matrix_t near; eng_matrix_from_euler(&near,0,0,.1f);
    memcpy(source->world_matrix,&near,sizeof(near)); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float angle = 2*atan2f(.25f*sinf(.05f),.75f+.25f*cosf(.05f));
    pose(&g,angle,(eng_vector_t){2,3,4}); fixture_end(&g.f);
}

TEST(engine_mimic, scale_copy_and_orientation_preservation_use_local_scale_under_different_parents)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,orientation ? 2 : 4,0,1,1);
        nmo_3dentity_state_t *parent = fixture_state(&g.f,g.parent), *source_parent = fixture_state(&g.f,g.source_parent);
        parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
        source_parent->world_matrix[0] = 5; source_parent->world_matrix[5] = 6; source_parent->world_matrix[10] = 7;
        nmo_3dentity_state_t *follower = fixture_state(&g.f,g.f.owner_id); follower->world_matrix[0] = 4; follower->world_matrix[5] = 9; follower->world_matrix[10] = 16;
        if (!orientation) {
            nmo_3dentity_state_t *source = fixture_state(&g.f,g.source);
            memset(source->world_matrix,0,sizeof(source->world_matrix)); source->world_matrix[0] = 30; source->world_matrix[5] = 42; source->world_matrix[10] = 56; source->world_matrix[15] = 1;
        }
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        eng_vector_t local_scale = eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),true);
        ASSERT_FLOAT_EQ(orientation ? 2 : 6,local_scale.x,1e-5f); ASSERT_FLOAT_EQ(orientation ? 3 : 7,local_scale.y,1e-5f); ASSERT_FLOAT_EQ(orientation ? 4 : 8,local_scale.z,1e-5f);
        pose(&g,orientation ? PI_F*.5f : 0,orientation ? (eng_vector_t){6,6,16} : (eng_vector_t){12,21,32});
        position(&g,g.f.owner_id,(eng_vector_t){10,20,30}); fixture_end(&g.f);
    }
}

TEST(engine_mimic, orientation_scale_restoration_moves_children_even_when_hierarchy_is_disabled)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,2,0,0,0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,7),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); pose(&g,0,(eng_vector_t){2,3,4});
        position(&g,g.child,hierarchy ? (eng_vector_t){12,23,34} : (eng_vector_t){14,29,46}); fixture_end(&g.f);
    }
}

TEST(engine_mimic, hierarchy_changes_propagate_position_and_scale_while_sampling_live_descendant_sources)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,1,1,0,0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,7),hierarchy);
        ((nmo_3dentity_state_t *)fixture_state(&g.f,g.source))->parent = nmo_ref_from_id(g.f.owner_id);
        fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,g.block,0),1); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,(eng_vector_t){30,60,90});
        position(&g,g.source,hierarchy ? (eng_vector_t){50,100,150} : (eng_vector_t){30,60,90});
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.f.owner_id,hierarchy ? (eng_vector_t){50,100,150} : (eng_vector_t){30,60,90}); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_mimic, absent_sources_are_noops_and_absent_targets_report_after_setting_io)
{
    for (int kind = 0; kind < 4; kind++) {
        mimic_fixture_t g; begin(&g,false);
        if (kind == 0) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)g.f.script_id);
        if (kind == 3) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        commit(&g); if (kind == 2) nmo_engine_destroy_object(g.f.engine,g.source);
        ASSERT_EQ(kind == 3 ? NMO_ERR_VALIDATION_FAILED : NMO_OK,nmo_engine_step(g.f.engine,10));
        if (kind == 3) {
            nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
            ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
            ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        } else ASSERT_EQ(1u,runs(&g.f,g.out));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        position(&g,g.f.owner_id,(eng_vector_t){10,20,30}); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_mimic, position_orientation_and_scale_are_independently_enabled_and_weighted);
REGISTER_TEST(engine_mimic, time_scaling_caps_each_weight_only_above_one_and_preserves_negative_values);
REGISTER_TEST(engine_mimic, orientation_uses_the_short_arc_extrapolation_and_normalized_close_angle_blends);
REGISTER_TEST(engine_mimic, scale_copy_and_orientation_preservation_use_local_scale_under_different_parents);
REGISTER_TEST(engine_mimic, orientation_scale_restoration_moves_children_even_when_hierarchy_is_disabled);
REGISTER_TEST(engine_mimic, hierarchy_changes_propagate_position_and_scale_while_sampling_live_descendant_sources);
REGISTER_TEST(engine_mimic, absent_sources_are_noops_and_absent_targets_report_after_setting_io);
TEST_MAIN_END()
