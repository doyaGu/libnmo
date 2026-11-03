#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_character.h"
#include "object/builtin/nmo_character_schemas.h"

#include <float.h>
#include <math.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static float frame(nmo_engine_t *engine, nmo_object_id_t animation)
{
    float value = NAN;
    eng_animation_frame(engine, animation, &value);
    return value;
}

static float x(const nmo_engine_t *engine, nmo_object_id_t entity)
{
    float matrix[16];
    return nmo_engine_entity_world_matrix(engine, entity, matrix) ? matrix[12] : NAN;
}

static nmo_object_id_t clip(engine_fixture_t *f, const char *name, nmo_object_id_t character,
                            nmo_object_id_t body, float length, float start, float end)
{
    nmo_object_id_t channel = fixture_create(f, NMO_CID_OBJECTANIMATION, "Channel");
    nmo_objectanimation_state_t *part = fixture_state(f, channel);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    float *keys = nmo_arena_alloc(arena, 8*sizeof(float), _Alignof(float));
    nmo_objanim_controller_t *controller = nmo_arena_alloc(arena, sizeof(*controller), _Alignof(nmo_objanim_controller_t));
    nmo_ref_t *reference = nmo_arena_alloc(arena, sizeof(*reference), _Alignof(nmo_ref_t));
    if (part == NULL || keys == NULL || controller == NULL || reference == NULL) return 0;
    const float values[] = {0,start,0,0, length,end,0,0};
    memcpy(keys, values, sizeof(values));
    *controller = (nmo_objanim_controller_t){0x637c4301u,2,sizeof(values),keys};
    part->format = CKOBJANIM_FORMAT_CONTROLLERS;
    part->has_length = 1;
    part->length = length;
    part->entity = nmo_ref_from_id(body);
    part->controller_count = 1;
    part->controllers = controller;
    nmo_object_id_t id = fixture_create(f, NMO_CID_KEYEDANIMATION, name);
    nmo_keyedanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    *reference = nmo_ref_from_id(channel);
    state->animation_count = 1;
    state->animation_ids = reference;
    state->base.has_character = character != 0;
    state->base.character = nmo_ref_from_id(character);
    state->base.has_root_entity = state->base.has_length = state->base.has_data = 1;
    state->base.root_entity = nmo_ref_from_id(body);
    state->base.length = length;
    state->base.flags = CKANIMATION_LINKTOFRAMERATE;
    state->base.frame_rate = 1000;
    return id;
}

static nmo_object_id_t character(engine_fixture_t *f, nmo_object_id_t *body)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_CHARACTER, "Character");
    *body = fixture_create(f, NMO_CID_BODYPART, "Body");
    nmo_character_state_t *state = fixture_state(f, id);
    nmo_bodypart_state_t *part = fixture_state(f, *body);
    if (state == NULL || part == NULL) return 0;
    state->root_body_part = nmo_ref_from_id(*body);
    ((nmo_3dentity_state_t *)part)->parent = nmo_ref_from_id(id);
    part->has_character = 1;
    part->character = nmo_ref_from_id(id);
    return id;
}

static void prepare(nmo_engine_t *engine, nmo_object_id_t animation)
{
    ASSERT_TRUE(eng_keyed_animation_center(engine, animation, 0));
    ASSERT_TRUE(eng_animation_set_frame(engine, animation, 0));
}

TEST(engine_character, clocks_restore_saved_cursors_and_separate_cursor_updates_from_pose)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t id = clip(&f, "Clip", 0, f.owner_id, 10, 0,10);
    nmo_keyedanimation_state_t *state = fixture_state(&f, id);
    state->base.length = 20;
    state->base.has_current_step = 1;
    state->base.current_step = 0.25f;
    fixture_begin_edit(&f);
    fixture_commit_bare(&f, NULL);
    ASSERT_FLOAT_EQ(5, frame(f.engine, id), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, f.owner_id), 0);
    ASSERT_TRUE(eng_animation_set_frame(f.engine, id, 5));
    ASSERT_FLOAT_EQ(2.5f, x(f.engine, f.owner_id), 0);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine, id, 0.75f));
    ASSERT_FLOAT_EQ(15, frame(f.engine, id), 0);
    ASSERT_FLOAT_EQ(2.5f, x(f.engine, f.owner_id), 0);
    nmo_object_id_t copy = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_copy_objects(f.engine, &id, 1, false, &copy));
    ASSERT_FLOAT_EQ(15, frame(f.engine, copy), 0);
    ASSERT_TRUE(eng_animation_set_frame(f.engine, copy, -4));
    ASSERT_FLOAT_EQ(0, frame(f.engine, copy), 0);
    ASSERT_FLOAT_EQ(15, frame(f.engine, id), 0);
    ASSERT_FLOAT_EQ(0.25f, state->base.current_step, 0);
    fixture_end(&f);
}

TEST(engine_character, linked_unlinked_paused_and_reverse_clocks_follow_native_frame_rules)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t id = fixture_create(&f, NMO_CID_ANIMATION, "Clock");
    nmo_animation_state_t *state = fixture_state(&f, id);
    state->has_length = 1;
    state->length = 10;
    state->flags = CKANIMATION_LINKTOFRAMERATE;
    fixture_begin_edit(&f);
    fixture_commit_bare(&f, NULL);
    ASSERT_TRUE(eng_animation_set_frame(f.engine, id, 5));
    float next;
    state->frame_rate = 30;
    ASSERT_TRUE(eng_animation_next_frame(f.engine, id, 100, &next));
    ASSERT_FLOAT_EQ(8, next, 0);
    state->frame_rate = -1000;
    ASSERT_TRUE(eng_animation_next_frame(f.engine, id, 2, &next));
    ASSERT_FLOAT_EQ(3, next, 0);
    state->frame_rate = 0;
    ASSERT_TRUE(eng_animation_next_frame(f.engine, id, 100, &next));
    ASSERT_FLOAT_EQ(5, next, 0);
    state->flags = 0;
    ASSERT_TRUE(eng_animation_next_frame(f.engine, id, 0, &next));
    ASSERT_FLOAT_EQ(6, next, 0);
    ASSERT_TRUE(eng_animation_set_frame(f.engine, id, -4));
    ASSERT_FLOAT_EQ(-4, frame(f.engine, id), 0); /* Plain animations do not clamp. */
    ASSERT_FALSE(eng_animation_next_frame(f.engine, id, NAN, &next));
    ASSERT_FALSE(eng_animation_set_frame(f.engine, id, INFINITY));
    fixture_end(&f);
}

TEST(engine_character, clocks_handle_zero_length_and_recover_a_cancellable_overflow)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t zero = clip(&f, "Zero", 0, f.owner_id, 0,7,7);
    nmo_object_id_t id = fixture_create(&f, NMO_CID_ANIMATION, "Wide Clock");
    nmo_animation_state_t *state = fixture_state(&f, id);
    state->has_length = 1;
    state->length = 2;
    state->flags = CKANIMATION_LINKTOFRAMERATE;
    state->frame_rate = -FLT_MAX;
    fixture_begin_edit(&f);
    fixture_commit_bare(&f, NULL);
    ASSERT_TRUE(eng_animation_set_frame(f.engine, zero, 3));
    ASSERT_FLOAT_EQ(0, frame(f.engine, zero), 0);
    ASSERT_FLOAT_EQ(7, x(f.engine, f.owner_id), 0);
    ASSERT_TRUE(eng_animation_set_current_step(f.engine, id, FLT_MAX));
    float next;
    ASSERT_TRUE(eng_animation_next_frame(f.engine, id, 1000, &next));
    ASSERT_FLOAT_EQ(FLT_MAX, next, 0);
    fixture_end(&f);
}

TEST(engine_character, primary_motion_accumulates_and_unbreakable_end_is_displayed_before_release)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f, &body);
    nmo_object_id_t a = clip(&f, "A", actor, body, 10,0,10);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f, NULL);
    prepare(f.engine, a);
    ASSERT_EQ(NMO_OK, eng_character_set_active_animation(f.engine, actor, a));
    ASSERT_EQ(NMO_OK, eng_character_process(f.engine, actor, 2));
    ASSERT_FLOAT_EQ(2, frame(f.engine,a), 0);
    ASSERT_FLOAT_EQ(2, x(f.engine,actor), 0);
    ASSERT_EQ(NMO_OK, eng_character_process(f.engine, actor, 3));
    ASSERT_FLOAT_EQ(5, x(f.engine,actor), 0);
    ASSERT_FLOAT_EQ(5, x(f.engine,body), 0);
    float local[16];
    ASSERT_TRUE(nmo_engine_entity_local_matrix(f.engine, body, local));
    ASSERT_FLOAT_EQ(0, local[12], 0);
    ASSERT_EQ(NMO_OK, eng_character_process(f.engine, actor, 8));
    ASSERT_EQ(a, eng_character_active_animation(f.engine,actor));
    ASSERT_FLOAT_EQ(10, frame(f.engine,a), 0);
    ASSERT_FLOAT_EQ(10, x(f.engine,actor), 0);
    ASSERT_EQ(NMO_OK, eng_character_process(f.engine, actor, 1));
    ASSERT_EQ(0u, eng_character_active_animation(f.engine,actor));
    ASSERT_FLOAT_EQ(10, x(f.engine,body), 0);
    ASSERT_EQ(0u, nmo_ref_runtime_id(&((const nmo_character_state_t *)fixture_state(&f,actor))->active_animation));
    fixture_end(&f);
}

TEST(engine_character, primary_handoff_carries_time_across_linked_and_mixed_clocks)
{
    for (int mixed = 0; mixed < 2; mixed++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10), b = clip(&f,"B",actor,body,20,0,mixed ? 20 : 40);
        nmo_keyedanimation_state_t *sa = fixture_state(&f,a), *sb = fixture_state(&f,b);
        if (mixed) sa->base.flags = 0;
        else sb->base.frame_rate = 500;
        fixture_begin_edit(&f);
        fixture_commit_bare(&f,NULL);
        prepare(f.engine,a); prepare(f.engine,b);
        ASSERT_TRUE(eng_animation_set_frame(f.engine,a,mixed ? 9.5f : 8));
        ASSERT_TRUE(eng_keyed_animation_center(f.engine,a,mixed ? 9.5f : 8));
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,a));
        ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,b,CK_TRANSITION_TOSTART));
        ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,mixed ? 4 : 5));
        ASSERT_EQ(b,eng_character_active_animation(f.engine,actor));
        ASSERT_EQ(0u,eng_character_next_animation(f.engine,actor));
        ASSERT_FLOAT_EQ(mixed ? 2 : 1.5f,frame(f.engine,b),0.000001f);
        ASSERT_FLOAT_EQ(mixed ? 12 : 13,x(f.engine,actor),0.000001f);
        ASSERT_FLOAT_EQ(mixed ? 9.5f : 8,frame(f.engine,a),0);
        fixture_end(&f);
    }
}

TEST(engine_character, queued_self_is_consumed_once_and_reverse_playback_finishes_the_source_start)
{
    for (int reverse = 0; reverse < 2; reverse++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10), b = reverse ? clip(&f,"B",actor,body,20,0,20) : a;
        nmo_keyedanimation_state_t *state = fixture_state(&f,a);
        fixture_begin_edit(&f);
        fixture_commit_bare(&f,NULL);
        prepare(f.engine,a);
        if (reverse) prepare(f.engine,b);
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,a));
        if (reverse) {
            ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
            state->base.frame_rate = -1000;
        } else {
            ASSERT_TRUE(eng_animation_set_frame(f.engine,a,8));
            ASSERT_TRUE(eng_keyed_animation_center(f.engine,a,8));
        }
        ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,b,CK_TRANSITION_TOSTART));
        ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,reverse ? 5 : 25));
        ASSERT_EQ(b,eng_character_active_animation(f.engine,actor));
        ASSERT_EQ(0u,eng_character_next_animation(f.engine,actor));
        ASSERT_FLOAT_EQ(reverse ? 3 : 10,frame(f.engine,b),0.000001f);
        ASSERT_FLOAT_EQ(reverse ? 3 : 20,x(f.engine,actor),0.000001f);
        fixture_end(&f);
    }
}

TEST(engine_character, queued_entry_promotes_without_advancing_and_immediate_switch_preserves_world_position)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10), b = clip(&f,"B",actor,body,20,100,140);
    nmo_keyedanimation_state_t *state = fixture_state(&f,a);
    state->base.flags |= CKANIMATION_CANBEBREAK;
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,a,CK_TRANSITION_TOSTART));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,99));
    ASSERT_EQ(a,eng_character_active_animation(f.engine,actor));
    ASSERT_FLOAT_EQ(0,frame(f.engine,a),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,5));
    ASSERT_FLOAT_EQ(5,x(f.engine,actor),0);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,b,CK_TRANSITION_FROMNOW));
    ASSERT_EQ(b,eng_character_active_animation(f.engine,actor));
    ASSERT_FLOAT_EQ(5,x(f.engine,body),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
    ASSERT_FLOAT_EQ(9,x(f.engine,actor),0);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_set_next_animation(f.engine,actor,0,0));
    ASSERT_EQ(b,eng_character_active_animation(f.engine,actor)); /* B is unbreakable. */
    ASSERT_EQ(NMO_ERR_NOT_SUPPORTED,eng_character_set_next_animation(f.engine,actor,a,CK_TRANSITION_WARPTOSTART));
    ASSERT_EQ(b,eng_character_active_animation(f.engine,actor));
    fixture_end(&f);
}

TEST(engine_character, terminal_yaw_is_transferred_after_motion_in_the_old_world_basis)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10);
    nmo_keyedanimation_state_t *state = fixture_state(&f,a);
    state->base.flags |= CKANIMATION_CANBEBREAK | CKANIMATION_ALIGNORIENTATION;
    nmo_object_id_t channel = nmo_ref_runtime_id(&state->animation_ids[0]);
    nmo_objectanimation_state_t *part = fixture_state(&f,channel);
    float rotation[] = {0,0,0,0,1, 10,0,0.70710678f,0,0.70710678f};
    nmo_objanim_controller_t controllers[] = {*part->controllers,{0x49ed4002u,2,sizeof(rotation),rotation}};
    part->controllers = controllers; part->controller_count = COUNT(controllers);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    prepare(f.engine,a);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,a));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,12));
    ASSERT_EQ(0u,eng_character_active_animation(f.engine,actor));
    ASSERT_FLOAT_EQ(10,x(f.engine,actor),0.00001f);
    float matrix[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
    ASSERT_FLOAT_EQ(0,matrix[14],0.00001f);
    ASSERT_FLOAT_EQ(0,matrix[0],0.00001f);
    ASSERT_FLOAT_EQ(1,matrix[2],0.00001f);
    ASSERT_FLOAT_EQ(-1,matrix[8],0.00001f);
    ASSERT_FLOAT_EQ(0,frame(f.engine,a),0); /* The completed source keeps its logical cursor. */
    fixture_end(&f);
}

TEST(engine_character, automatic_scriptless_characters_run_each_frame_and_respect_object_activity)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10);
    nmo_character_state_t *state = fixture_state(&f,actor);
    state->base.entity_flags |= CK_3DENTITY_CHARACTERDOPROCESS;
    state->active_animation = nmo_ref_from_id(a);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,3));
    ASSERT_FLOAT_EQ(5,frame(f.engine,a),0);
    ASSERT_FLOAT_EQ(5,x(f.engine,actor),0);
    ASSERT_EQ(NMO_OK,nmo_engine_activate_object(f.engine,actor,false));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_FLOAT_EQ(5,frame(f.engine,a),0);
    ASSERT_EQ(NMO_OK,nmo_engine_activate_object(f.engine,actor,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_FLOAT_EQ(6,frame(f.engine,a),0);
    fixture_end(&f);
}

TEST(engine_character, automatic_processing_precedes_the_characters_own_script)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10);
    nmo_character_state_t *state = fixture_state(&f,actor);
    state->base.entity_flags |= CK_3DENTITY_CHARACTERDOPROCESS;
    state->active_animation = nmo_ref_from_id(a);
    f.owner_id = actor;
    f.script_id = fixture_add_script(&f,"Script",&f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t set = fixture_block(&f,NMO_GUID(0xe456e78a,0x456789aa),"Set Position");
    fixture_input_int(&f,fixture_target(&f,set),(int32_t)actor);
    fixture_link(&f,f.script_in,BLOCK_IN(&f,set,0),0);
    float target[] = {100,0,0};
    fixture_input_value(&f,BLOCK_PIN(&f,set,0),target,sizeof(target));
    fixture_commit(&f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_FLOAT_EQ(100,x(f.engine,actor),0);
    ASSERT_FLOAT_EQ(100,x(f.engine,body),0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_FLOAT_EQ(102,x(f.engine,actor),0);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_character, clocks_restore_saved_cursors_and_separate_cursor_updates_from_pose);
REGISTER_TEST(engine_character, linked_unlinked_paused_and_reverse_clocks_follow_native_frame_rules);
REGISTER_TEST(engine_character, clocks_handle_zero_length_and_recover_a_cancellable_overflow);
REGISTER_TEST(engine_character, primary_motion_accumulates_and_unbreakable_end_is_displayed_before_release);
REGISTER_TEST(engine_character, primary_handoff_carries_time_across_linked_and_mixed_clocks);
REGISTER_TEST(engine_character, queued_self_is_consumed_once_and_reverse_playback_finishes_the_source_start);
REGISTER_TEST(engine_character, queued_entry_promotes_without_advancing_and_immediate_switch_preserves_world_position);
REGISTER_TEST(engine_character, terminal_yaw_is_transferred_after_motion_in_the_old_world_basis);
REGISTER_TEST(engine_character, automatic_scriptless_characters_run_each_frame_and_respect_object_activity);
REGISTER_TEST(engine_character, automatic_processing_precedes_the_characters_own_script);
TEST_MAIN_END()
