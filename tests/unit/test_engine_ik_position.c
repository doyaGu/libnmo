#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_IK NMO_GUID(0xbefc4a32,0x893ecad4)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct position_fixture {
    engine_fixture_t f;
    nmo_object_id_t root, middle, end, target, block, out;
} position_fixture_t;

static nmo_object_id_t body(position_fixture_t *g, const char *name, nmo_object_id_t parent, float x, float y)
{
    nmo_object_id_t id = fixture_create(&g->f,NMO_CID_BODYPART,name);
    nmo_bodypart_state_t *part = fixture_state(&g->f,id);
    if (part == NULL) return 0;
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)part;
    entity->parent = nmo_ref_from_id(parent); entity->has_entityndata_chunk = 1;
    entity->world_matrix[12] = x; entity->world_matrix[13] = y;
    part->has_character = 1; part->character = nmo_ref_from_id(g->f.owner_id); part->rotation_joint.flags = 4;
    return id;
}

static void begin(position_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->f.owner_id = fixture_create(&g->f,NMO_CID_CHARACTER,"Actor");
    g->root = body(g,"Shoulder",g->f.owner_id,0,0); g->middle = body(g,"Elbow",g->root,1,0); g->end = body(g,"Hand",g->middle,1,1);
    ASSERT_TRUE(g->root != 0 && g->middle != 0 && g->end != 0);
    nmo_character_state_t *actor = fixture_state(&g->f,g->f.owner_id); ASSERT_NOT_NULL(actor); actor->root_body_part = nmo_ref_from_id(g->root);
    g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Goal");
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target); ASSERT_NOT_NULL(target);
    target->world_matrix[12] = 1.4f; target->world_matrix[13] = .4f;
    g->f.script_id = fixture_add_script(&g->f,"IK Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_IK,"IK Position"); g->out = fixture_block(&g->f,GUID_NOP,"Out");
    ASSERT_TRUE(g->block != 0 && g->out != 0); ASSERT_EQ(0u,fixture_target(&g->f,g->block));
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->root);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1),(int32_t)g->end);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,2),(int32_t)g->target);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static nmo_object_id_t local(position_fixture_t *g)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,0);
}

static void commit(position_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_IK));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t chain(position_fixture_t *g)
{
    nmo_object_id_t result = 0; read_value(&g->f,local(g),&result,sizeof(result)); return result;
}

static eng_vector_t position(position_fixture_t *g, nmo_object_id_t id)
{
    return eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1);
}

static float distance(eng_vector_t a, eng_vector_t b)
{
    eng_vector_t delta = {a.x-b.x,a.y-b.y,a.z-b.z}; return eng_vector_magnitude(&delta);
}

TEST(engine_ik_position, latency_interpolates_the_world_goal_once_per_trigger_including_zero_and_negative_values)
{
    const int32_t latencies[] = {1,0,2,5,-2};
    for (size_t i = 0; i < COUNT(latencies); i++) {
        position_fixture_t g; begin(&g);
        if (latencies[i] != 5) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),latencies[i]);
        commit(&g); nmo_object_id_t kinematic = chain(&g); ASSERT_TRUE(kinematic != 0);
        ASSERT_EQ(NMO_CID_KINEMATICCHAIN,nmo_engine_object_class(g.f.engine,kinematic));
        ASSERT_TRUE(strcmp("IKPosChain",nmo_engine_object_name(g.f.engine,kinematic)) == 0);
        eng_vector_t here = position(&g,g.end), there = position(&g,g.target);
        float divisor = latencies[i] == 0 ? 1 : (float)latencies[i];
        eng_vector_t requested = {here.x+(there.x-here.x)/divisor,here.y+(there.y-here.y)/divisor,0};
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16));
        ASSERT_TRUE(distance(position(&g,g.end),requested) <= .05f);
        ASSERT_TRUE(distance(position(&g,g.root),(eng_vector_t){0,0,0}) < 1e-6f);
        ASSERT_FLOAT_EQ(1,distance(position(&g,g.root),position(&g,g.middle)),1e-5f);
        ASSERT_FLOAT_EQ(1,distance(position(&g,g.middle),position(&g,g.end)),1e-5f);
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        nmo_object_id_t start = 0, end = 0; ASSERT_TRUE(eng_kinematic_effectors(g.f.engine,kinematic,&start,&end));
        ASSERT_EQ(g.root,start); ASSERT_EQ(g.end,end); ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id));
        eng_vector_t held = position(&g,g.end); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16));
        ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_FLOAT_EQ(0,distance(position(&g,g.end),held),1e-6f);
        const nmo_3dentity_state_t *saved = fixture_state(&g.f,g.end); ASSERT_FLOAT_EQ(1,saved->world_matrix[12],0); ASSERT_FLOAT_EQ(1,saved->world_matrix[13],0);
        fixture_end(&g.f);
    }
}

TEST(engine_ik_position, feedback_resamples_the_live_goal_and_reuses_the_same_chain)
{
    position_fixture_t g; begin(&g);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),2);
    fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,g.block,0),1); commit(&g);
    nmo_object_id_t original = chain(&g); float initial = distance(position(&g,g.end),position(&g,g.target));
    for (int frame = 0; frame < 10; frame++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_TRUE(distance(position(&g,g.end),position(&g,g.target)) < initial*.15f); ASSERT_EQ(original,chain(&g));
    eng_vector_t new_goal = {.6f,1.3f,0};
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.target),&new_goal,-1,false);
    initial = distance(position(&g,g.end),new_goal);
    for (int frame = 0; frame < 10; frame++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100));
    ASSERT_TRUE(distance(position(&g,g.end),new_goal) < initial*.15f); ASSERT_EQ(original,chain(&g));
    ASSERT_EQ(20u,runs(&g.f,g.block)); ASSERT_EQ(20u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_ik_position, invalid_chains_and_inputs_report_parameter_errors_after_setting_io)
{
    for (int kind = 0; kind < 8; kind++) {
        position_fixture_t g; begin(&g);
        if (kind == 2) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        if (kind == 3) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),0);
        if (kind == 4) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),0);
        if (kind == 5) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.f.owner_id);
        commit(&g);
        if (kind == 0) { nmo_object_id_t missing = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,local(&g),&missing,sizeof(missing))); }
        if (kind == 1) nmo_engine_destroy_object(g.f.engine,chain(&g));
        if (kind == 6) nmo_engine_destroy_object(g.f.engine,g.target);
        if (kind == 7) nmo_engine_destroy_object(g.f.engine,g.root);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id)); fixture_end(&g.f);
    }
}

TEST(engine_ik_position, solver_rejections_do_not_suppress_out_or_enable_automatic_character_processing)
{
    for (int kind = 0; kind < 3; kind++) {
        position_fixture_t g; begin(&g);
        if (kind == 0) {
            ((nmo_bodypart_state_t *)fixture_state(&g.f,g.root))->rotation_joint.flags = 0;
            ((nmo_bodypart_state_t *)fixture_state(&g.f,g.middle))->rotation_joint.flags = 0;
        }
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.root);
        if (kind == 2) {
            nmo_object_id_t disconnected = body(&g,"Other Hand",g.f.owner_id,1,1);
            fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)disconnected);
        }
        commit(&g); eng_vector_t held = position(&g,g.end);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.out));
        ASSERT_FLOAT_EQ(0,distance(position(&g,g.end),held),1e-6f);
        ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_ik_position, latency_interpolates_the_world_goal_once_per_trigger_including_zero_and_negative_values);
REGISTER_TEST(engine_ik_position, feedback_resamples_the_live_goal_and_reuses_the_same_chain);
REGISTER_TEST(engine_ik_position, invalid_chains_and_inputs_report_parameter_errors_after_setting_io);
REGISTER_TEST(engine_ik_position, solver_rejections_do_not_suppress_out_or_enable_automatic_character_processing);
TEST_MAIN_END()
