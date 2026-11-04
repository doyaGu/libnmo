#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_CONTROLLER NMO_GUID(0x12874356,0x12874356)
#define GUID_GOTO NMO_GUID(0x11223344,0xaabbccdd)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct controller_fixture {
    engine_fixture_t f;
    nmo_object_id_t root, block, animations[4];
} controller_fixture_t;

static nmo_object_id_t clip(controller_fixture_t *g, const char *name, float travel)
{
    engine_fixture_t *f = &g->f;
    nmo_object_id_t channel = fixture_create(f,NMO_CID_OBJECTANIMATION,name);
    nmo_objectanimation_state_t *part = fixture_state(f,channel);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    float *keys = nmo_arena_alloc(arena,8*sizeof(float),_Alignof(float));
    nmo_objanim_controller_t *controller = nmo_arena_alloc(arena,sizeof(*controller),_Alignof(nmo_objanim_controller_t));
    nmo_ref_t *reference = nmo_arena_alloc(arena,sizeof(*reference),_Alignof(nmo_ref_t));
    if (part == NULL || keys == NULL || controller == NULL || reference == NULL) return 0;
    float values[] = {0,0,0,0,20,travel,0,0}; memcpy(keys,values,sizeof(values));
    *controller = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(values),keys};
    part->format = CKOBJANIM_FORMAT_CONTROLLERS; part->has_length = 1; part->length = 20;
    part->entity = nmo_ref_from_id(g->root); part->controller_count = 1; part->controllers = controller;
    nmo_object_id_t id = fixture_create(f,NMO_CID_KEYEDANIMATION,name);
    nmo_keyedanimation_state_t *animation = fixture_state(f,id);
    if (animation == NULL) return 0;
    *reference = nmo_ref_from_id(channel); animation->animation_ids = reference; animation->animation_count = 1;
    animation->base.has_data = animation->base.has_length = animation->base.has_character = animation->base.has_root_entity = 1;
    animation->base.length = 20; animation->base.character = nmo_ref_from_id(f->owner_id); animation->base.root_entity = nmo_ref_from_id(g->root);
    animation->base.flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK; animation->base.frame_rate = 1000;
    return id;
}

static void begin(controller_fixture_t *g, bool animations)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->f.owner_id = fixture_create(&g->f,NMO_CID_CHARACTER,"Actor"); g->root = fixture_create(&g->f,NMO_CID_BODYPART,"Root");
    nmo_character_state_t *actor = fixture_state(&g->f,g->f.owner_id); ASSERT_NOT_NULL(actor);
    nmo_bodypart_state_t *body = fixture_state(&g->f,g->root); ASSERT_NOT_NULL(body);
    actor->root_body_part = nmo_ref_from_id(g->root);
    nmo_3dentity_state_t *root = (nmo_3dentity_state_t *)body;
    root->parent = nmo_ref_from_id(g->f.owner_id); root->has_entityndata_chunk = 1;
    body->has_character = 1; body->character = nmo_ref_from_id(g->f.owner_id);
    const char *messages[] = {"Host Only","Joy_Right","Joy_Button2","Joy_Up"};
    fixture_install_messages(&g->f,messages,COUNT(messages));
    if (animations) {
        const char *names[] = {"Stand","Walk","Backward","Run"};
        const float travel[] = {0,20,-20,40};
        for (size_t i = 0; i < COUNT(names); i++) {
            g->animations[i] = clip(g,names[i],travel[i]); ASSERT_TRUE(g->animations[i] != 0);
        }
    }
    g->f.script_id = fixture_add_script(&g->f,"Controller Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_CONTROLLER,"Controller"); ASSERT_TRUE(g->block != 0);
    ASSERT_EQ(0u,fixture_target(&g->f,g->block));
    for (size_t i = 0; i < COUNT(g->animations); i++) fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,i),(int32_t)g->animations[i]);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
}

static void commit(controller_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_CONTROLLER));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t local(controller_fixture_t *g)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,0);
}

static void send(controller_fixture_t *g, const char *name)
{
    int32_t type = nmo_engine_message_type(g->f.engine,name); ASSERT_TRUE(type >= 0);
    ASSERT_EQ(NMO_OK,nmo_engine_send_message(g->f.engine,type,g->f.owner_id,0));
}

static nmo_engine_block_t runtime_block(controller_fixture_t *g)
{
    return (nmo_engine_block_t){.engine=g->f.engine,.node=eng_map_get(&g->f.engine->node_map,g->block)};
}

TEST(engine_character_controller, message_presence_selects_animations_in_native_priority_order)
{
    const struct { const char *first,*second; int selected; } cases[] = {
        {NULL,NULL,0}, {"Joy_Button2",NULL,0}, {"Host Only",NULL,0},
        {"Joy_Up",NULL,1}, {"Joy_Down",NULL,2}, {"Joy_Button1",NULL,3},
        {"Joy_Up","Joy_Down",1}, {"Joy_Button1","Joy_Up",3}, {"Joy_Down","Joy_Button1",3},
    };
    for (size_t i = 0; i < COUNT(cases); i++) for (int reverse = 0; reverse < 2; reverse++) {
        controller_fixture_t g; begin(&g,true); commit(&g);
        const char *expected[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2"};
        int32_t messages[7]; read_value(&g.f,local(&g),messages,sizeof(messages));
        for (size_t j = 0; j < COUNT(expected); j++) ASSERT_EQ(nmo_engine_message_type(g.f.engine,expected[j]),messages[j]);
        ASSERT_EQ(0,messages[6]);
        const char *names[] = {cases[i].first,cases[i].second};
        for (int j = 0; j < 2; j++) if (names[reverse ? 1-j : j] != NULL) send(&g,names[reverse ? 1-j : j]);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(g.animations[0],eng_character_next_animation(g.f.engine,g.f.owner_id));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(g.animations[cases[i].selected],eng_character_active_animation(g.f.engine,g.f.owner_id));
        ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(2u,runs(&g.f,g.block));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        for (int j = 0; j < 4; j++) {
            ASSERT_EQ(j != 3,!!(eng_animation_playback_state(g.f.engine,g.animations[j])->flags & CKANIMATION_CANBEBREAK));
            ASSERT_TRUE(eng_animation_state(g.f.engine,g.animations[j])->flags & CKANIMATION_CANBEBREAK);
        }
        fixture_end(&g.f);
    }
}

TEST(engine_character_controller, turns_use_active_rates_without_permission_or_clock_gates_and_prefer_right)
{
    const struct { bool active; float rate; const char *first,*second; float angle; } cases[] = {
        {false,0,"Joy_Left",NULL,.18f}, {false,0,"Joy_Right","Joy_Left",-.18f},
        {true,60,"Joy_Left",NULL,.36f}, {true,-60,"Joy_Right",NULL,.36f},
        {true,60,"Joy_Left","Joy_Right",-.36f}, {true,0,"Joy_Left",NULL,0},
    };
    for (size_t i = 0; i < COUNT(cases); i++) {
        controller_fixture_t g; begin(&g,false);
        nmo_object_id_t animation = cases[i].active ? clip(&g,"Unlinked",0) : 0;
        if (animation != 0) {
            nmo_keyedanimation_state_t *state = fixture_state(&g.f,animation); state->base.flags = 0; state->base.frame_rate = cases[i].rate;
            for (int j = 0; j < 4; j++) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,j),(int32_t)animation);
        }
        nmo_character_state_t *actor = fixture_state(&g.f,g.f.owner_id);
        actor->base.world_matrix[0] = 2; actor->base.world_matrix[5] = 3; actor->base.world_matrix[10] = 4;
        actor->base.world_matrix[12] = 3; actor->base.world_matrix[13] = 5; actor->base.world_matrix[14] = 7;
        nmo_3dentity_state_t *root = fixture_state(&g.f,g.root);
        root->world_matrix[12] = 3; root->world_matrix[13] = 5; root->world_matrix[14] = 7;
        commit(&g); send(&g,cases[i].first); if (cases[i].second != NULL) send(&g,cases[i].second);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (animation != 0) ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,animation));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100));
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
        ASSERT_FLOAT_EQ(2*cosf(cases[i].angle),matrix[0],1e-5f); ASSERT_FLOAT_EQ(2*sinf(cases[i].angle),matrix[2],1e-5f);
        ASSERT_FLOAT_EQ(-4*sinf(cases[i].angle),matrix[8],1e-5f); ASSERT_FLOAT_EQ(4*cosf(cases[i].angle),matrix[10],1e-5f);
        ASSERT_FLOAT_EQ(3,matrix[5],1e-5f); ASSERT_FLOAT_EQ(3,matrix[12],1e-5f); ASSERT_FLOAT_EQ(5,matrix[13],1e-5f); ASSERT_FLOAT_EQ(7,matrix[14],1e-5f);
        fixture_end(&g.f);
    }
}

TEST(engine_character_controller, animation_requests_build_five_frame_best_warps)
{
    controller_fixture_t g; begin(&g,true); commit(&g);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,g.animations[1]));
    ASSERT_TRUE(eng_animation_set_frame(g.f.engine,g.animations[1],5));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    nmo_object_id_t warper = eng_character_warper(g.f.engine,g.f.owner_id), from = 0, to = 0;
    ASSERT_TRUE(warper != 0); ASSERT_EQ(warper,eng_character_active_animation(g.f.engine,g.f.owner_id));
    uint32_t mode = 0; ASSERT_TRUE(eng_character_warper_parameters(g.f.engine,g.f.owner_id,&mode,&from,NULL,&to,NULL));
    ASSERT_EQ(g.animations[1],from); ASSERT_EQ(g.animations[0],to);
    ASSERT_EQ(CK_TRANSITION_WARPBEST|CK_TRANSITION_LOOPIFEQUAL|CK_TRANSITION_USEVELOCITY,mode);
    ASSERT_FLOAT_EQ(5,eng_animation_length(eng_animation_state(g.f.engine,warper)),0); fixture_end(&g.f);
}

TEST(engine_character_controller, off_stops_processing_and_messages_do_not_restart_the_controller)
{
    controller_fixture_t g; begin(&g,true); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2));
    nmo_engine_block_t block = runtime_block(&g); nmo_engine_block_activate_input(&block,1,true);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2)); ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id));
    float frame = 0; ASSERT_TRUE(eng_animation_frame(g.f.engine,g.animations[0],&frame));
    uint64_t executions = runs(&g.f,g.block); send(&g,"Joy_Button1");
    for (int i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2));
    float held = -1; ASSERT_TRUE(eng_animation_frame(g.f.engine,g.animations[0],&held));
    ASSERT_FLOAT_EQ(frame,held,0); ASSERT_EQ(executions,runs(&g.f,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id));
    ASSERT_EQ(executions+1,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_character_controller, off_wins_simultaneous_inputs_without_consuming_on)
{
    controller_fixture_t g; begin(&g,true); commit(&g);
    nmo_engine_block_t block = runtime_block(&g); nmo_engine_block_activate_input(&block,1,true);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id));
    ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1)));
    ASSERT_TRUE(eng_animation_playback_state(g.f.engine,g.animations[3])->flags & CKANIMATION_CANBEBREAK);
    ASSERT_EQ(0u,eng_character_next_animation(g.f.engine,g.f.owner_id)); fixture_end(&g.f);
}

TEST(engine_character_controller, missing_deleted_and_wrong_class_animations_become_null_stop_requests)
{
    for (int kind = 0; kind < 3; kind++) {
        controller_fixture_t g; begin(&g,true);
        nmo_object_id_t selected = kind == 0 ? 0 : kind == 1 ? g.animations[0] : g.root;
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)selected); commit(&g);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,g.animations[1]));
        ASSERT_TRUE(eng_animation_set_frame(g.f.engine,g.animations[1],5));
        if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.animations[0]);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,eng_character_active_animation(g.f.engine,g.f.owner_id));
        ASSERT_EQ(0u,eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id));
        fixture_end(&g.f);
    }
}

TEST(engine_character_controller, invalid_message_tables_fail_after_enabling_processing_and_protecting_run)
{
    controller_fixture_t g; begin(&g,true); commit(&g);
    int32_t truncated = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,local(&g),&truncated,sizeof(truncated)));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
    nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
    ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result);
    ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id));
    ASSERT_EQ(0u,eng_animation_playback_state(g.f.engine,g.animations[3])->flags & CKANIMATION_CANBEBREAK);
    fixture_end(&g.f);
}

TEST(engine_character_controller, non_character_owners_report_owner_error_without_consuming_inputs)
{
    engine_fixture_t f; fixture_begin(&f);
    nmo_object_id_t block = fixture_block(&f,GUID_CONTROLLER,"Invalid owner"); ASSERT_TRUE(block != 0);
    fixture_link(&f,f.script_in,BLOCK_IN(&f,block,0),0);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,0));
    nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure));
    ASSERT_EQ(block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
    ASSERT_TRUE(nmo_engine_io_is_active(f.engine,BLOCK_IN(&f,block,0))); fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_character_controller, message_presence_selects_animations_in_native_priority_order);
REGISTER_TEST(engine_character_controller, turns_use_active_rates_without_permission_or_clock_gates_and_prefer_right);
REGISTER_TEST(engine_character_controller, animation_requests_build_five_frame_best_warps);
REGISTER_TEST(engine_character_controller, off_stops_processing_and_messages_do_not_restart_the_controller);
REGISTER_TEST(engine_character_controller, off_wins_simultaneous_inputs_without_consuming_on);
REGISTER_TEST(engine_character_controller, missing_deleted_and_wrong_class_animations_become_null_stop_requests);
REGISTER_TEST(engine_character_controller, invalid_message_tables_fail_after_enabling_processing_and_protecting_run);
REGISTER_TEST(engine_character_controller, non_character_owners_report_owner_error_without_consuming_inputs);
TEST_MAIN_END()
