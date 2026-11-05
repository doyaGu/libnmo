#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "core/nmo_utils.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_CONTROLLER NMO_GUID(0x12874356,0x12874356)
#define GUID_ENHANCED_CONTROLLER NMO_GUID(0x44433224,0x12874356)
#define GUID_KEYBOARD_CONTROLLER NMO_GUID(0x43f1d000,0x1a2b3c4d)
#define GUID_KEYBOARD_MAPPER NMO_GUID(0x053b2bde,0x2f8f1d74)
#define GUID_JOYSTICK_MAPPER NMO_GUID(0x03ed1bea,0x25692ecc)
#define GUID_JOYSTICK_CONTROLLER NMO_GUID(0x0085ebc1,0x21935422)
#define GUID_GOTO NMO_GUID(0x11223344,0xaabbccdd)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct controller_fixture {
    engine_fixture_t f;
    nmo_object_id_t root, block, animations[7];
    size_t animation_count;
    bool enhanced;
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

static void begin_variant(controller_fixture_t *g, bool animations, bool enhanced)
{
    memset(g,0,sizeof(*g)); g->enhanced = enhanced; g->animation_count = enhanced ? 7 : 4; fixture_begin_document(&g->f);
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
        const char *names[] = {"Stand","Walk","Backward","Run","Creep","Jump","Dance"};
        const float travel[] = {0,20,-20,40,5,60,0};
        for (size_t i = 0; i < g->animation_count; i++) {
            g->animations[i] = clip(g,names[i],travel[i]); ASSERT_TRUE(g->animations[i] != 0);
        }
    }
    g->f.script_id = fixture_add_script(&g->f,"Controller Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,enhanced ? GUID_ENHANCED_CONTROLLER : GUID_CONTROLLER,"Controller"); ASSERT_TRUE(g->block != 0);
    ASSERT_EQ(0u,fixture_target(&g->f,g->block));
    for (size_t i = 0; i < g->animation_count; i++) fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,i),(int32_t)g->animations[i]);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
}

static void begin(controller_fixture_t *g, bool animations) { begin_variant(g,animations,false); }

static void commit(controller_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,g->enhanced ? GUID_ENHANCED_CONTROLLER : GUID_CONTROLLER));
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

TEST(engine_character_controller, enhanced_message_priority_uses_button_presence_and_protects_only_jump_and_dance)
{
    const struct { const char *first,*second; int selected; } cases[] = {
        {NULL,NULL,0}, {"Host Only",NULL,0}, {"Joy_Button5",NULL,0}, {"Joy_Button6",NULL,0},
        {"Joy_Up",NULL,1}, {"Joy_Down",NULL,2}, {"Joy_Button1",NULL,3}, {"Joy_Button2",NULL,4}, {"Joy_Button3",NULL,5}, {"Joy_Button4",NULL,6},
        {"Joy_Button1","Joy_Button2",3}, {"Joy_Button2","Joy_Button3",4}, {"Joy_Button3","Joy_Up",5},
        {"Joy_Up","Joy_Down",1}, {"Joy_Up","Joy_Button4",1}, {"Joy_Down","Joy_Button4",2},
    };
    for (size_t i = 0; i < COUNT(cases); i++) for (int reverse = 0; reverse < 2; reverse++) {
        controller_fixture_t g; begin_variant(&g,true,true); commit(&g);
        const char *expected[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"}; int32_t messages[11]; read_value(&g.f,local(&g),messages,sizeof(messages));
        for (size_t j = 0; j < COUNT(expected); j++) ASSERT_EQ(nmo_engine_message_type(g.f.engine,expected[j]),messages[j]); ASSERT_EQ(0,messages[10]);
        const char *names[] = {cases[i].first,cases[i].second}; for (int j = 0; j < 2; j++) if (names[reverse ? 1-j : j] != NULL) send(&g,names[reverse ? 1-j : j]);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[cases[i].selected],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(2u,runs(&g.f,g.block));
        for (size_t j = 0; j < g.animation_count; j++) { ASSERT_EQ(j < 5,!!(eng_animation_playback_state(g.f.engine,g.animations[j])->flags & CKANIMATION_CANBEBREAK)); ASSERT_TRUE(eng_animation_state(g.f.engine,g.animations[j])->flags & CKANIMATION_CANBEBREAK); }
        fixture_end(&g.f);
    }
}

TEST(engine_character_controller, enhanced_jump_and_dance_hold_until_the_native_end_window_while_run_and_creep_allow_interrupts)
{
    for (int index = 3; index < 7; index++) {
        controller_fixture_t g; begin_variant(&g,true,true); commit(&g); ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,g.animations[index])); ASSERT_TRUE(eng_animation_set_frame(g.f.engine,g.animations[index],5)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (index >= 5) {
            ASSERT_EQ(g.animations[index],eng_character_active_animation(g.f.engine,g.f.owner_id)); send(&g,"Joy_Button1"); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[index],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(0u,eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_animation_set_frame(g.f.engine,g.animations[index],19.5f)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[index],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_animation_set_frame(g.f.engine,g.animations[index],19.75f)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        }
        nmo_object_id_t warper = eng_character_warper(g.f.engine,g.f.owner_id), from = 0, to = 0; uint32_t mode = 0; ASSERT_TRUE(warper != 0); ASSERT_EQ(warper,eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_warper_parameters(g.f.engine,g.f.owner_id,&mode,&from,NULL,&to,NULL)); ASSERT_EQ(g.animations[index],from); ASSERT_EQ(g.animations[0],to); ASSERT_EQ(CK_TRANSITION_WARPBEST|CK_TRANSITION_LOOPIFEQUAL|CK_TRANSITION_USEVELOCITY,mode); ASSERT_FLOAT_EQ(5,eng_animation_length(eng_animation_state(g.f.engine,warper)),0); fixture_end(&g.f);
    }
}

TEST(engine_character_controller, enhanced_off_preserves_on_and_stops_automatic_motion_until_explicit_reactivation)
{
    controller_fixture_t g; begin_variant(&g,true,true); commit(&g); nmo_engine_block_t block = runtime_block(&g); nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1)));
    for (int i = 5; i < 7; i++) ASSERT_TRUE(eng_animation_playback_state(g.f.engine,g.animations[i])->flags & CKANIMATION_CANBEBREAK);
    uint64_t stopped = runs(&g.f,g.block); send(&g,"Joy_Button3"); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2)); ASSERT_EQ(stopped,runs(&g.f,g.block)); ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,g.animations[1])); ASSERT_TRUE(eng_animation_set_frame(g.f.engine,g.animations[1],5)); nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2)); ASSERT_FALSE(eng_character_automatic(g.f.engine,g.f.owner_id));
    float frame = 0, held = 0, matrix[16], after[16]; ASSERT_TRUE(eng_animation_frame(g.f.engine,g.animations[1],&frame)); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); stopped = runs(&g.f,g.block); send(&g,"Joy_Button2"); for (int i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2)); ASSERT_TRUE(eng_animation_frame(g.f.engine,g.animations[1],&held)); ASSERT_FLOAT_EQ(frame,held,0); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,after)); ASSERT_FLOAT_EQ(matrix[12],after[12],0); ASSERT_EQ(stopped,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_character_controller, enhanced_missing_deleted_and_wrong_class_inputs_stop_without_falling_back_to_lower_priorities)
{
    const char *messages[] = {NULL,"Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4"};
    for (int selected = 0; selected < 7; selected++) for (int kind = 0; kind < 3; kind++) {
        controller_fixture_t g; begin_variant(&g,true,true); nmo_object_id_t input = kind == 0 ? 0 : kind == 1 ? g.animations[selected] : g.root; fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,selected),(int32_t)input); commit(&g); if (messages[selected] != NULL) send(&g,messages[selected]); send(&g,"Joy_Button6"); if (selected >= 3 && selected <= 5) send(&g,"Joy_Up"); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); nmo_object_id_t active = g.animations[selected == 0 ? 1 : 0]; ASSERT_EQ(NMO_OK,eng_character_set_active_animation(g.f.engine,g.f.owner_id,active)); ASSERT_TRUE(eng_animation_set_frame(g.f.engine,active,5)); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.animations[selected]); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0u,eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(0u,eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); fixture_end(&g.f);
    }
}

TEST(engine_character_controller, enhanced_truncated_message_tables_fail_after_enabling_processing_and_protecting_jump_and_dance)
{
    const size_t sizes[] = {4,28,40,43}; int32_t table[11] = {0};
    for (size_t i = 0; i < COUNT(sizes); i++) {
        controller_fixture_t g; begin_variant(&g,true,true); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,local(&g),table,sizes[i])); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id));
        for (int j = 0; j < 7; j++) ASSERT_EQ(j < 5,!!(eng_animation_playback_state(g.f.engine,g.animations[j])->flags & CKANIMATION_CANBEBREAK)); fixture_end(&g.f);
    }
}

TEST(engine_character_controller, enhanced_message_aliases_use_the_first_matching_slot_in_the_live_local_table)
{
    controller_fixture_t g; begin_variant(&g,true,true); commit(&g); int32_t table[11]; read_value(&g.f,local(&g),table,sizeof(table)); table[2] = table[4]; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,local(&g),table,sizeof(table))); send(&g,"Joy_Button1"); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[1],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_animation_playback_state(g.f.engine,g.animations[3])->flags & CKANIMATION_CANBEBREAK); fixture_end(&g.f);
}

TEST(engine_character_controller, turns_use_active_rates_without_permission_or_clock_gates_and_prefer_right)
{
    const struct { bool active; float rate; const char *first,*second; float angle; } cases[] = {
        {false,0,"Joy_Left",NULL,.18f}, {false,0,"Joy_Right","Joy_Left",-.18f},
        {true,60,"Joy_Left",NULL,.36f}, {true,-60,"Joy_Right",NULL,.36f},
        {true,60,"Joy_Left","Joy_Right",-.36f}, {true,0,"Joy_Left",NULL,0},
    };
    for (size_t i = 0; i < COUNT(cases); i++) for (int enhanced = 0; enhanced < 2; enhanced++) {
        controller_fixture_t g; begin_variant(&g,false,enhanced != 0);
        nmo_object_id_t animation = cases[i].active ? clip(&g,"Unlinked",0) : 0;
        if (animation != 0) {
            nmo_keyedanimation_state_t *state = fixture_state(&g.f,animation); state->base.flags = 0; state->base.frame_rate = cases[i].rate;
            for (size_t j = 0; j < g.animation_count; j++) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,j),(int32_t)animation);
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
    for (int enhanced = 0; enhanced < 2; enhanced++) {
        engine_fixture_t f; fixture_begin(&f);
        nmo_object_id_t block = fixture_block(&f,enhanced ? GUID_ENHANCED_CONTROLLER : GUID_CONTROLLER,"Invalid owner"); ASSERT_TRUE(block != 0);
        fixture_link(&f,f.script_in,BLOCK_IN(&f,block,0),0);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure));
        ASSERT_EQ(block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(f.engine,BLOCK_IN(&f,block,0))); fixture_end(&f);
    }
}

static nmo_object_id_t saved_animation(controller_fixture_t *g, size_t index)
{
    nmo_object_id_t source = 0; char text[32]; snprintf(text,sizeof(text),"%u",g->animations[index]);
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,index),&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,source,text) != NMO_OK) return 0;
    return source;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static void expect_message_table(nmo_engine_t *engine, nmo_object_id_t block, bool enhanced)
{
    const nmo_behavior_state_t *state = nmo_object_get_state(eng_find_object(engine,block));
    nmo_object_id_t parameter = eng_module_id(eng_module_of(engine,block,NULL),nmo_behavior_ref_array_get_id(&state->local_parameters,0));
    const void *data = NULL; size_t size = 0;
    size_t count = enhanced ? 10 : 6; ASSERT_EQ(NMO_OK,nmo_engine_param_value(engine,parameter,&data,&size,NULL)); ASSERT_EQ((count+1)*sizeof(int32_t),size);
    int32_t table[11]; memcpy(table,data,size);
    const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"};
    for (size_t i = 0; i < count; i++) ASSERT_EQ(nmo_engine_message_type(engine,names[i]),table[i]);
    ASSERT_EQ(0,table[count]);
}

TEST(engine_character_controller, loaded_and_copied_controllers_rebind_messages_and_animation_references)
{
    for (int enhanced = 0; enhanced < 2; enhanced++) {
        controller_fixture_t g; begin_variant(&g,true,enhanced != 0);
        nmo_object_id_t sources[7];
        for (size_t i = 0; i < g.animation_count; i++) { sources[i] = saved_animation(&g,i); ASSERT_TRUE(sources[i] != 0); }
        const int32_t obsolete[] = {123456,123457,123458,123459,123460,123461,99};
        nmo_parameter_state_t *saved = fixture_state(&g.f,local(&g)); ASSERT_NOT_NULL(saved);
        nmo_array_dispose(&saved->buffer_data); ASSERT_EQ(NMO_OK,nmo_array_alloc(&saved->buffer_data,1,sizeof(obsolete),NULL));
        memcpy(saved->buffer_data.data,obsolete,sizeof(obsolete)); saved->mode = CKPARAM_MODE_BUFFER; saved->has_state = true;
        fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_character_controller.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host);
            const char *names[] = {"Joy_Button1","Joy_Down","Host Only","Joy_Left","Joy_Up","Joy_Right","Joy_Button2","Joy_Button6","Joy_Button4","Joy_Button3","Joy_Button5"};
            fixture_install_messages(&host,names,COUNT(names)); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
            fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"controller.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t ids[25], copies[25] = {0}; size_t object_count = 4+3*g.animation_count;
            ids[0] = nmo_engine_object_named(host.engine,"Actor",NMO_CID_CHARACTER);
            ids[1] = nmo_engine_object_named(host.engine,"Root",NMO_CID_BODYPART);
            ids[2] = nmo_engine_object_named(host.engine,"Controller Script",NMO_CID_BEHAVIOR);
            ids[3] = nmo_engine_object_named(host.engine,"Controller",NMO_CID_BEHAVIOR);
            ASSERT_TRUE(ids[0] != 0 && ids[0] != g.f.owner_id && ids[3] != 0); ASSERT_EQ(ids[1],eng_character_root(host.engine,ids[0]));
            const eng_module_t *module = eng_module_of(host.engine,ids[3],NULL);
            for (size_t i = 0; i < g.animation_count; i++) {
                ids[4+i] = eng_module_id(module,g.animations[i]);
                const nmo_keyedanimation_state_t *animation = fixture_state(&g.f,g.animations[i]);
                ids[4+g.animation_count+i] = eng_module_id(module,nmo_ref_runtime_id(&animation->animation_ids[0]));
                ids[4+2*g.animation_count+i] = eng_module_id(module,sources[i]);
            }
            expect_message_table(host.engine,ids[3],g.enhanced);
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(ids[4],eng_character_next_animation(host.engine,ids[0]));
            ASSERT_EQ(NMO_OK,nmo_engine_send_message(host.engine,nmo_engine_message_type(host.engine,"Joy_Button1"),ids[0],0));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            ASSERT_EQ(ids[7],eng_character_active_animation(host.engine,ids[0]));
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,object_count,false,copies));
            ASSERT_EQ(copies[1],eng_character_root(host.engine,copies[0])); expect_message_table(host.engine,copies[3],g.enhanced);
            ASSERT_EQ(copies[7],eng_character_active_animation(host.engine,copies[0]));
            for (size_t i = 0; i < g.animation_count; i++) {
                ASSERT_EQ(copies[0],eng_animation_character(host.engine,copies[4+i]));
                ASSERT_EQ(copies[1],eng_object_animation_entity(host.engine,copies[4+g.animation_count+i]));
            }
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[2],true,true));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(copies[4],eng_character_active_animation(host.engine,copies[0]));
            ASSERT_EQ(NMO_OK,nmo_engine_send_message(host.engine,nmo_engine_message_type(host.engine,"Joy_Up"),ids[0],0));
            ASSERT_EQ(NMO_OK,nmo_engine_send_message(host.engine,nmo_engine_message_type(host.engine,"Joy_Down"),copies[0],0));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            ASSERT_EQ(ids[5],eng_character_active_animation(host.engine,ids[0]));
            ASSERT_EQ(copies[6],eng_character_active_animation(host.engine,copies[0]));
            for (size_t i = 0; i < g.animation_count; i++) {
                bool protected = g.enhanced ? i >= 5 : i == 3; ASSERT_EQ(!protected,!!(eng_animation_playback_state(host.engine,copies[4+i])->flags & CKANIMATION_CANBEBREAK)); ASSERT_TRUE(eng_animation_state(host.engine,copies[4+i])->flags & CKANIMATION_CANBEBREAK);
            }
            if (g.enhanced) {
                for (size_t i = 0; i < object_count; i++) nmo_engine_destroy_object(host.engine,ids[i]);
                const char *buttons[] = {"Joy_Button2","Joy_Button3","Joy_Button4"};
                for (size_t i = 0; i < COUNT(buttons); i++) {
                    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(host.engine,copies[0],copies[4])); ASSERT_TRUE(eng_animation_set_frame(host.engine,copies[4],0)); ASSERT_EQ(NMO_OK,nmo_engine_send_message(host.engine,nmo_engine_message_type(host.engine,buttons[i]),copies[0],0)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(copies[8+i],eng_character_active_animation(host.engine,copies[0]));
                    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[2],false,false)); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[2],true,true));
                }
                int32_t object = eng_map_get(&host.engine->object_map,copies[0]); ASSERT_TRUE(object >= 0); ASSERT_EQ(1,host.engine->objects.data[object].waiting); expect_message_table(host.engine,copies[3],true);
            }
            fixture_end(&host);
        }
        fixture_end(&g.f); remove(path);
    }
}

static float actor_x(controller_fixture_t *g)
{
    float matrix[16]; return nmo_engine_entity_world_matrix(g->f.engine,g->f.owner_id,matrix) ? matrix[12] : NAN;
}

TEST(engine_character_controller, goto_messages_drive_root_motion_until_arrival_then_return_to_stand)
{
    for (int enhanced = 0; enhanced < 2; enhanced++) {
        controller_fixture_t g; begin_variant(&g,true,enhanced != 0);
        nmo_object_id_t target = fixture_create(&g.f,NMO_CID_3DENTITY,"Destination");
        ((nmo_3dentity_state_t *)fixture_state(&g.f,target))->world_matrix[12] = 20;
        nmo_object_id_t go = fixture_block(&g.f,GUID_GOTO,"Go To"), arrived = fixture_block(&g.f,GUID_NOP,"Arrived");
        ASSERT_TRUE(go != 0 && arrived != 0);
        fixture_input_int(&g.f,fixture_target(&g.f,go),(int32_t)g.f.owner_id);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,go,0),(int32_t)target);
        float distance = 4; fixture_input_value(&g.f,BLOCK_PIN(&g.f,go,1),&distance,sizeof(distance));
        fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,go,0),0);
        fixture_link(&g.f,BLOCK_OUT(&g.f,go,2),BLOCK_IN(&g.f,go,1),1);
        fixture_link(&g.f,BLOCK_OUT(&g.f,go,0),BLOCK_IN(&g.f,arrived,0),0); commit(&g);
        bool walked = false;
        for (int step = 0; step < 80 && runs(&g.f,arrived) == 0; step++) {
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1));
            if (eng_character_active_animation(g.f.engine,g.f.owner_id) == g.animations[1]) walked = true;
        }
        ASSERT_TRUE(walked); ASSERT_EQ(1u,runs(&g.f,arrived)); ASSERT_TRUE(actor_x(&g) >= 16 && actor_x(&g) <= 24);
        uint64_t queries = runs(&g.f,go); ASSERT_TRUE(queries > 1);
        for (int step = 0; step < 10; step++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1));
        ASSERT_EQ(g.animations[0],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(queries,runs(&g.f,go));
        ASSERT_EQ(1u,runs(&g.f,arrived)); float stopped = actor_x(&g); ASSERT_TRUE(stopped >= 16 && stopped <= 26);
        for (int step = 0; step < 5; step++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1));
        ASSERT_FLOAT_EQ(stopped,actor_x(&g),1e-5f); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id));
        fixture_end(&g.f);
    }
}

TEST(engine_character_controller, keyboard_scan_codes_drive_both_character_controllers_with_native_frame_delivery_and_off_behavior)
{
    const struct { uint32_t first, second; int ordinary, enhanced; } cases[] = {
        {0,0,0,0},{0x48,0,1,1},{0x50,0,2,2},{0xd2,0,3,3},{0xc7,0,0,4},{0xc9,0,0,5},{0xd3,0,0,6},
        {0xcf,0,0,0},{0xd1,0,0,0},{0xd2,0xc7,3,3},{0xc7,0x48,1,4},{0xc9,0x48,1,5},{0xd3,0x48,1,1},{0xc8,0,0,0},
    };
    for (int enhanced = 0; enhanced < 2; enhanced++) for (size_t i = 0; i < COUNT(cases); i++) {
        controller_fixture_t g; begin_variant(&g,true,enhanced != 0); nmo_object_id_t keyboard = fixture_block(&g.f,GUID_KEYBOARD_CONTROLLER,"Keyboard Controller"); ASSERT_TRUE(keyboard != 0); fixture_input_int(&g.f,fixture_target(&g.f,keyboard),(int32_t)g.f.owner_id); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,keyboard,0),0); commit(&g); if (cases[i].first != 0) nmo_engine_set_key(g.f.engine,cases[i].first,true); if (cases[i].second != 0) nmo_engine_set_key(g.f.engine,cases[i].second,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[enhanced ? cases[i].enhanced : cases[i].ordinary],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(2u,runs(&g.f,keyboard)); ASSERT_EQ(2u,runs(&g.f,g.block));
        nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,keyboard)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(3u,runs(&g.f,keyboard)); ASSERT_EQ(4u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_character_controller, custom_keyboard_mapper_bindings_drive_both_controllers_and_keep_multiple_messages_per_key)
{
    const struct { uint32_t key; const char *message; } bindings[] = {{0x11,"Joy_Up"},{0x1f,"Joy_Down"},{0x13,"Joy_Button1"},{0x2c,"Joy_Button2"},{0x39,"Joy_Up"},{0x39,"Joy_Button3"},{0x2e,"Joy_Button4"}};
    const struct { uint32_t first, second; int ordinary, enhanced; } cases[] = {{0,0,0,0},{0x11,0,1,1},{0x1f,0,2,2},{0x13,0,3,3},{0x2c,0,0,4},{0x39,0,1,5},{0x2e,0,0,6},{0x11,0x2c,1,4},{0x39,0x13,3,3}};
    for (int enhanced = 0; enhanced < 2; enhanced++) for (size_t i = 0; i < COUNT(cases); i++) {
        controller_fixture_t g; begin_variant(&g,true,enhanced != 0); nmo_object_id_t mapper = fixture_block(&g.f,GUID_KEYBOARD_MAPPER,"Custom Keyboard Mapper"); ASSERT_TRUE(mapper != 0); fixture_input_int(&g.f,fixture_target(&g.f,mapper),(int32_t)g.f.owner_id); const nmo_behavior_state_t *state = fixture_state(&g.f,mapper); nmo_parameter_state_t *data = fixture_state(&g.f,nmo_behavior_ref_array_get_id(&state->local_parameters,0)); nmo_array_dispose(&data->buffer_data); ASSERT_EQ(NMO_OK,nmo_array_alloc(&data->buffer_data,1,40*COUNT(bindings),NULL)); memset(data->buffer_data.data,0,40*COUNT(bindings)); data->mode = CKPARAM_MODE_BUFFER; data->has_state = true;
        for (size_t j = 0; j < COUNT(bindings); j++) { uint8_t *record = (uint8_t *)data->buffer_data.data+40*j; nmo_write_u32_le(record,bindings[j].key); nmo_write_u32_le(record+4,UINT32_MAX); memcpy(record+8,bindings[j].message,strlen(bindings[j].message)); } ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,nmo_behavior_ref_array_get_id(&state->local_parameters,1),"7")); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,mapper,0),0); commit(&g); if (cases[i].first != 0) nmo_engine_set_key(g.f.engine,cases[i].first,true); if (cases[i].second != 0) nmo_engine_set_key(g.f.engine,cases[i].second,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[enhanced ? cases[i].enhanced : cases[i].ordinary],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,mapper,0))); ASSERT_EQ(2u,runs(&g.f,mapper));
        nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,mapper)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(3u,runs(&g.f,mapper)); fixture_end(&g.f);
    }
}

TEST(engine_character_controller, joystick_slots_drive_both_character_controllers_with_button_priority_and_frame_delivery)
{
    const struct { float x,y; uint32_t buttons; int ordinary, enhanced; } cases[] = {
        {0,0,0,0,0},{0,-1,0,1,1},{0,1,0,2,2},{0,0,1,3,3},{0,0,2,0,4},{0,0,4,0,5},{0,0,8,0,6},
        {0,0,48,0,0},{0,0,3,3,3},{0,-1,2,1,4},{0,-1,8,1,1},{-1,0,0,0,0},
    };
    for (int enhanced = 0; enhanced < 2; enhanced++) for (size_t i = 0; i < COUNT(cases); i++) {
        controller_fixture_t g; begin_variant(&g,true,enhanced != 0); nmo_object_id_t joystick = fixture_block(&g.f,GUID_JOYSTICK_CONTROLLER,"Joystick Controller"); ASSERT_TRUE(joystick != 0); fixture_input_int(&g.f,fixture_target(&g.f,joystick),(int32_t)g.f.owner_id); fixture_input_int(&g.f,BLOCK_PIN(&g.f,joystick,0),3); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,joystick,0),0); commit(&g); nmo_engine_joystick_state_t other = {.attached = true,.position = {0,1,0},.point_of_view = -1,.buttons = 1}, selected = {.attached = true,.position = {cases[i].x,cases[i].y,.25f},.point_of_view = -1,.buttons = cases[i].buttons}; nmo_engine_set_joystick(g.f.engine,0,&other); nmo_engine_set_joystick(g.f.engine,3,&selected);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[enhanced ? cases[i].enhanced : cases[i].ordinary],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(2u,runs(&g.f,joystick)); float output[3]; read_value(&g.f,BLOCK_POUT(&g.f,joystick,0),output,sizeof(output)); for (size_t j = 0; j < 3; j++) ASSERT_FLOAT_EQ(selected.position[j],output[j],0);
        nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,joystick)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(3u,runs(&g.f,joystick)); ASSERT_EQ(4u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_character_controller, custom_joystick_mapper_bindings_drive_both_controllers_with_duplicate_commands_and_high_buttons)
{
    const struct { uint32_t command; const char *message; } bindings[] = {{1010,"Joy_Up"},{1011,"Joy_Down"},{1012,"Joy_Button1"},{1043,"Joy_Button2"},{1013,"Joy_Up"},{1013,"Joy_Button3"},{1008,"Joy_Button4"}};
    const struct { float x,y; uint32_t buttons; int ordinary, enhanced; } cases[] = {
        {0,0,0,0,0},{-1,0,0,1,1},{1,0,0,2,2},{0,0,1,3,3},{0,0,0x80000000u,0,4},{0,0,2,1,5},{0,-1,0,0,6},{-1,0,0x80000000u,1,4},{0,0,3,3,3},{-.5f,0,0,0,0},
    };
    for (int enhanced = 0; enhanced < 2; enhanced++) for (size_t i = 0; i < COUNT(cases); i++) {
        controller_fixture_t g; begin_variant(&g,true,enhanced != 0); nmo_object_id_t mapper = fixture_block(&g.f,GUID_JOYSTICK_MAPPER,"Custom Joystick Mapper"); ASSERT_TRUE(mapper != 0); fixture_input_int(&g.f,fixture_target(&g.f,mapper),(int32_t)g.f.owner_id); fixture_input_int(&g.f,BLOCK_PIN(&g.f,mapper,0),3);
        const nmo_behavior_state_t *state = fixture_state(&g.f,mapper); nmo_parameter_state_t *data = fixture_state(&g.f,nmo_behavior_ref_array_get_id(&state->local_parameters,0)); nmo_array_dispose(&data->buffer_data); ASSERT_EQ(NMO_OK,nmo_array_alloc(&data->buffer_data,1,40*COUNT(bindings),NULL)); memset(data->buffer_data.data,0,40*COUNT(bindings)); data->mode = CKPARAM_MODE_BUFFER; data->has_state = true;
        for (size_t j = 0; j < COUNT(bindings); j++) { uint8_t *record = (uint8_t *)data->buffer_data.data+40*j; nmo_write_u32_le(record,bindings[j].command); nmo_write_u32_le(record+4,UINT32_MAX); memcpy(record+8,bindings[j].message,strlen(bindings[j].message)); } ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,nmo_behavior_ref_array_get_id(&state->local_parameters,1),"7")); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,mapper,0),0); commit(&g);
        nmo_engine_joystick_state_t other = {.attached = true,.position = {1,-1,0},.point_of_view = -1,.buttons = 1}, selected = {.attached = true,.position = {cases[i].x,cases[i].y,.25f},.point_of_view = -1,.buttons = cases[i].buttons}; nmo_engine_set_joystick(g.f.engine,0,&other); nmo_engine_set_joystick(g.f.engine,3,&selected);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_next_animation(g.f.engine,g.f.owner_id)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[enhanced ? cases[i].enhanced : cases[i].ordinary],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,mapper,0))); ASSERT_EQ(2u,runs(&g.f,mapper)); float output[3]; read_value(&g.f,BLOCK_POUT(&g.f,mapper,0),output,sizeof(output)); for (size_t j = 0; j < 3; j++) ASSERT_FLOAT_EQ(selected.position[j],output[j],0);
        nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,mapper)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(g.animations[0],eng_character_active_animation(g.f.engine,g.f.owner_id)); ASSERT_TRUE(eng_character_automatic(g.f.engine,g.f.owner_id)); ASSERT_EQ(3u,runs(&g.f,mapper)); ASSERT_EQ(4u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_character_controller, custom_joystick_mapper_bindings_drive_both_controllers_with_duplicate_commands_and_high_buttons);

REGISTER_TEST(engine_character_controller, message_presence_selects_animations_in_native_priority_order);
REGISTER_TEST(engine_character_controller, enhanced_message_priority_uses_button_presence_and_protects_only_jump_and_dance);
REGISTER_TEST(engine_character_controller, enhanced_jump_and_dance_hold_until_the_native_end_window_while_run_and_creep_allow_interrupts);
REGISTER_TEST(engine_character_controller, enhanced_off_preserves_on_and_stops_automatic_motion_until_explicit_reactivation);
REGISTER_TEST(engine_character_controller, enhanced_missing_deleted_and_wrong_class_inputs_stop_without_falling_back_to_lower_priorities);
REGISTER_TEST(engine_character_controller, enhanced_truncated_message_tables_fail_after_enabling_processing_and_protecting_jump_and_dance);
REGISTER_TEST(engine_character_controller, enhanced_message_aliases_use_the_first_matching_slot_in_the_live_local_table);
REGISTER_TEST(engine_character_controller, turns_use_active_rates_without_permission_or_clock_gates_and_prefer_right);
REGISTER_TEST(engine_character_controller, animation_requests_build_five_frame_best_warps);
REGISTER_TEST(engine_character_controller, off_stops_processing_and_messages_do_not_restart_the_controller);
REGISTER_TEST(engine_character_controller, off_wins_simultaneous_inputs_without_consuming_on);
REGISTER_TEST(engine_character_controller, missing_deleted_and_wrong_class_animations_become_null_stop_requests);
REGISTER_TEST(engine_character_controller, invalid_message_tables_fail_after_enabling_processing_and_protecting_run);
REGISTER_TEST(engine_character_controller, non_character_owners_report_owner_error_without_consuming_inputs);
REGISTER_TEST(engine_character_controller, loaded_and_copied_controllers_rebind_messages_and_animation_references);
REGISTER_TEST(engine_character_controller, goto_messages_drive_root_motion_until_arrival_then_return_to_stand);
REGISTER_TEST(engine_character_controller, keyboard_scan_codes_drive_both_character_controllers_with_native_frame_delivery_and_off_behavior);
REGISTER_TEST(engine_character_controller, custom_keyboard_mapper_bindings_drive_both_controllers_and_keep_multiple_messages_per_key);
REGISTER_TEST(engine_character_controller, joystick_slots_drive_both_character_controllers_with_button_priority_and_frame_delivery);
TEST_MAIN_END()
