#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_character.h"
#include "object/builtin/nmo_character_schemas.h"

#include <float.h>
#include <math.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define GUID_ANIMATION_FRAME NMO_GUID(0x0e579636,0xa3b6e7f0)
#define GUID_ANIMATION_STEP NMO_GUID(0x120127aa,0x960added)
#define GUID_BODYPART_FRAME NMO_GUID(0x55f045c7,0x6cb3417b)
#define GUID_EXCLUDE_ANIMATION NMO_GUID(0x4d3f515b,0x450c20c6)

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

static nmo_object_id_t limb(engine_fixture_t *f, nmo_object_id_t actor)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_BODYPART,"Limb");
    nmo_bodypart_state_t *part = fixture_state(f,id);
    ((nmo_3dentity_state_t *)part)->parent = nmo_ref_from_id(actor);
    part->has_character = 1;
    part->character = nmo_ref_from_id(actor);
    return id;
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
    ASSERT_TRUE(eng_animation_set_current_step(f.engine,id,.8f));
    ASSERT_TRUE(eng_animation_next_frame(f.engine,id,8,&next));
    ASSERT_FLOAT_EQ(0,next,0);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,id,5));
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
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,a,CK_TRANSITION_WARPTOSTART));
    ASSERT_EQ(b,eng_character_active_animation(f.engine,actor));
    ASSERT_EQ(0,eng_character_next_animation(f.engine,actor)); /* Retry when B reaches its interruption boundary. */
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

TEST(engine_character, copied_characters_remap_the_live_primary_and_keep_independent_clocks)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    prepare(f.engine,a);
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,a));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
    nmo_object_id_t originals[] = {actor,body,a}, copies[3] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,originals,COUNT(originals),false,copies));
    ASSERT_EQ(copies[2],eng_character_active_animation(f.engine,copies[0]));
    ASSERT_FLOAT_EQ(2,frame(f.engine,copies[2]),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,copies[0],3));
    ASSERT_FLOAT_EQ(5,x(f.engine,copies[0]),0);
    ASSERT_FLOAT_EQ(5,x(f.engine,copies[1]),0);
    ASSERT_FLOAT_EQ(2,frame(f.engine,a),0);
    ASSERT_FLOAT_EQ(2,x(f.engine,actor),0);
    ASSERT_FLOAT_EQ(2,x(f.engine,body),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,1));
    ASSERT_FLOAT_EQ(3,x(f.engine,actor),0);
    ASSERT_FLOAT_EQ(5,x(f.engine,copies[1]),0);
    fixture_end(&f);
}

TEST(engine_character, secondary_one_shots_hold_exact_endpoints_and_expire_without_a_forward_terminal_sample)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t a = clip(&f,"Secondary",actor,bone,10,0,10);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,3,CKSECONDARYANIMATION_ONESHOT,0));
    ASSERT_FLOAT_EQ(3,x(f.engine,bone),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,7));
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(10,x(f.engine,bone),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,1));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,3,0,0));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,100));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(3,frame(f.engine,a),0);
    ASSERT_FLOAT_EQ(3,x(f.engine,bone),0);
    fixture_end(&f);
}

TEST(engine_character, secondary_loops_consume_large_overruns_and_finite_counts_hold_the_terminal_pose)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t a = clip(&f,"Loop",actor,bone,10,0,10);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,0,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,30));
    ASSERT_FLOAT_EQ(10,frame(f.engine,a),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
    ASSERT_FLOAT_EQ(2,frame(f.engine,a),1e-6f);
    eng_character_flush_secondary(f.engine,actor);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,0,CKSECONDARYANIMATION_LOOPNTIMES|CKSECONDARYANIMATION_LASTFRAME,3));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,20));
    ASSERT_FLOAT_EQ(10,frame(f.engine,a),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,9));
    ASSERT_FLOAT_EQ(9,frame(f.engine,a),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
    ASSERT_FLOAT_EQ(10,frame(f.engine,a),0);
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,FLT_MAX));
    ASSERT_FLOAT_EQ(10,frame(f.engine,a),0);
    eng_character_flush_secondary(f.engine,actor);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,2,CKSECONDARYANIMATION_LOOPNTIMES,UINT32_MAX));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,FLT_MAX));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(2,x(f.engine,bone),1e-6f);
    fixture_end(&f);
}

TEST(engine_character, secondary_reverse_unlinked_and_zero_length_playback_keep_strict_boundaries)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t reverse = clip(&f,"Reverse",actor,bone,10,0,10);
    nmo_object_id_t unlinked = clip(&f,"Unlinked",actor,bone,2,20,22);
    nmo_object_id_t zero = clip(&f,"Zero",actor,bone,0,30,30);
    ((nmo_keyedanimation_state_t *)fixture_state(&f,reverse))->base.frame_rate = -1000;
    ((nmo_keyedanimation_state_t *)fixture_state(&f,unlinked))->base.flags = 0;
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,reverse,2,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,22));
    ASSERT_FLOAT_EQ(0,frame(f.engine,reverse),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,1));
    ASSERT_FLOAT_EQ(9,frame(f.engine,reverse),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(f.engine,actor,reverse));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,reverse,2,CKSECONDARYANIMATION_ONESHOT,0));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,3));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(0,x(f.engine,bone),0);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,unlinked,0,0,0));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,0));
    ASSERT_FLOAT_EQ(1,frame(f.engine,unlinked),0);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,0));
    ASSERT_FLOAT_EQ(2,frame(f.engine,unlinked),0);
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,0));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,zero,0,CKSECONDARYANIMATION_LOOPNTIMES,2));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,1));
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,1));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    fixture_end(&f);
}

TEST(engine_character, secondary_bones_exclude_other_clips_and_release_only_their_own_gates)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t a = clip(&f,"Older",actor,bone,10,10,20);
    nmo_object_id_t b = clip(&f,"Newer",actor,bone,10,20,30);
    nmo_object_id_t other = clip(&f,"Other",actor,bone,10,100,110);
    const nmo_keyedanimation_state_t *state = fixture_state(&f,other);
    nmo_object_id_t channel = nmo_ref_runtime_id(&state->animation_ids[0]);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,1,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_FLOAT_EQ(11,x(f.engine,bone),1e-6f);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,5));
    ASSERT_TRUE(eng_object_animation_set_step(f.engine,channel,1));
    ASSERT_FLOAT_EQ(11,x(f.engine,bone),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,b,2,CKSECONDARYANIMATION_LOOP,0));
    /* The older gate blocks B's entry pose; B owns the next processed pose. */
    ASSERT_FLOAT_EQ(11,x(f.engine,bone),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(f.engine,actor,a));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,5));
    ASSERT_FLOAT_EQ(11,x(f.engine,bone),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,1));
    ASSERT_FLOAT_EQ(23,x(f.engine,bone),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,b,9,0,0));
    ASSERT_FLOAT_EQ(3,frame(f.engine,b),1e-6f);
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    eng_character_flush_secondary(f.engine,actor);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine,channel,1));
    ASSERT_FLOAT_EQ(110,x(f.engine,bone),0);
    fixture_end(&f);
}

TEST(engine_character, secondary_removal_processes_shifted_entries_and_releases_deleted_channels)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t a = clip(&f,"Short",actor,bone,1,0,1);
    nmo_object_id_t b = clip(&f,"Long",actor,bone,10,10,20);
    nmo_object_id_t other = clip(&f,"Other",actor,bone,10,100,110);
    const nmo_keyedanimation_state_t *state = fixture_state(&f,b);
    nmo_object_id_t channel = nmo_ref_runtime_id(&state->animation_ids[0]);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,0,0,0));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,b,0,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(b,eng_character_secondary_animation(f.engine,actor,0));
    ASSERT_EQ(0,eng_character_secondary_animation(f.engine,actor,1));
    ASSERT_FLOAT_EQ(12,x(f.engine,bone),1e-6f);
    nmo_engine_destroy_object(f.engine,channel);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(f.engine,actor,b));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,5));
    ASSERT_FLOAT_EQ(105,x(f.engine,bone),0);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,0,CKSECONDARYANIMATION_LOOP,0));
    nmo_engine_destroy_object(f.engine,a);
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,2));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,actor));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,10));
    ASSERT_FLOAT_EQ(110,x(f.engine,bone),0);
    fixture_end(&f);
}

TEST(engine_character, secondary_presets_and_invalid_requests_preserve_existing_playback)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t foreign_root, foreign = character(&f,&foreign_root);
    nmo_object_id_t a = clip(&f,"Preset",actor,bone,10,0,10);
    nmo_object_id_t b = clip(&f,"Warp",actor,bone,10,20,30);
    ((nmo_keyedanimation_state_t *)fixture_state(&f,a))->base.flags |= CKSECONDARYANIMATION_LASTFRAME<<16;
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,4,CKSECONDARYANIMATION_FROMANIMATION,0));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_play_secondary_warp(f.engine,actor,b,8,CKSECONDARYANIMATION_DOWARP,NAN,0));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_play_secondary(f.engine,foreign,a,0,0,0));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_stop_secondary(f.engine,foreign,a));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,eng_character_play_secondary(f.engine,actor,b,NAN,0,0));
    ASSERT_FLOAT_EQ(4,x(f.engine,bone),0);
    ASSERT_FLOAT_EQ(0,frame(f.engine,b),0);
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,100));
    ASSERT_FLOAT_EQ(10,x(f.engine,bone),0);
    ASSERT_EQ(1,eng_character_secondary_count(f.engine,actor));
    fixture_end(&f);
}

TEST(engine_character, secondary_gates_are_copied_and_remapped_without_copying_playback_slots)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t a = clip(&f,"Secondary",actor,bone,10,0,10);
    nmo_object_id_t other = clip(&f,"Other",actor,bone,10,100,110);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,a,2,CKSECONDARYANIMATION_LOOP,0));
    nmo_object_id_t originals[] = {actor,bone,a,other}, copies[COUNT(originals)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,originals,COUNT(originals),false,copies));
    ASSERT_EQ(0,eng_character_secondary_count(f.engine,copies[0]));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[3],5));
    ASSERT_FLOAT_EQ(2,x(f.engine,copies[1]),1e-6f);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(f.engine,actor,a));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,other,5));
    ASSERT_FLOAT_EQ(105,x(f.engine,bone),0);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[3],5));
    ASSERT_FLOAT_EQ(2,x(f.engine,copies[1]),1e-6f);
    nmo_engine_destroy_object(f.engine,copies[2]);
    ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[3],5));
    ASSERT_FLOAT_EQ(105,x(f.engine,copies[1]),0);
    fixture_end(&f);
}

TEST(engine_character, automatic_processing_advances_primary_then_secondary_and_plain_entities_have_no_gate)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t primary = clip(&f,"Primary",actor,root,10,0,0);
    nmo_object_id_t secondary = clip(&f,"Secondary",actor,bone,10,20,30);
    nmo_object_id_t plain = fixture_create(&f,NMO_CID_3DENTITY,"Plain");
    nmo_object_id_t plain_a = clip(&f,"Plain A",actor,plain,10,0,10);
    nmo_object_id_t plain_b = clip(&f,"Plain B",actor,plain,10,100,110);
    nmo_character_state_t *state = fixture_state(&f,actor);
    state->base.entity_flags |= CK_3DENTITY_CHARACTERDOPROCESS;
    state->active_animation = nmo_ref_from_id(primary);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,secondary,2,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,3));
    ASSERT_FLOAT_EQ(3,frame(f.engine,primary),1e-6f);
    ASSERT_FLOAT_EQ(5,frame(f.engine,secondary),0);
    ASSERT_FLOAT_EQ(25,x(f.engine,bone),0);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,plain_a,2,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_TRUE(eng_animation_set_frame(f.engine,plain_b,5));
    ASSERT_FLOAT_EQ(105,x(f.engine,plain),0);
    fixture_end(&f);
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name;
    return data;
}

TEST(engine_character, loaded_scriptless_characters_restore_primary_queues_and_keep_file_state_immutable)
{
    const nmo_objectanimation_format_t formats[] = {CKOBJANIM_FORMAT_CONTROLLERS,
        CKOBJANIM_FORMAT_NEWDATA,CKOBJANIM_FORMAT_LEGACY,CKOBJANIM_FORMAT_SHARED};
    for (size_t c = 0; c < COUNT(formats); c++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t root, actor = character(&f,&root);
        nmo_object_id_t a = clip(&f,"Saved Primary",actor,root,10,0,10);
        nmo_object_id_t b = clip(&f,"Saved Next",actor,root,20,100,140);
        nmo_character_state_t *character_state = fixture_state(&f,actor);
        character_state->base.entity_flags |= CK_3DENTITY_CHARACTERDOPROCESS;
        character_state->active_animation = nmo_ref_from_id(a);
        character_state->anim_dest = nmo_ref_from_id(b);
        character_state->base.has_entityndata_chunk = 1;
        character_state->base.world_matrix[12] = 2;
        nmo_3dentity_state_t *body = fixture_state(&f,root);
        body->has_entityndata_chunk = 1;
        body->world_matrix[12] = 2;
        nmo_beobject_state_t *activity = (nmo_beobject_state_t *)character_state;
        activity->has_single_activity = 1;
        activity->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_keyedanimation_state_t *primary = fixture_state(&f,a);
        primary->base.has_current_step = 1;
        primary->base.current_step = 0.2f;
        ((nmo_keyedanimation_state_t *)fixture_state(&f,b))->base.frame_rate = 2000;
        nmo_object_id_t channel = nmo_ref_runtime_id(&primary->animation_ids[0]);
        nmo_objectanimation_state_t *part = fixture_state(&f,channel);
        part->has_root_pos = 1;
        part->root_pos.x = -2;
        part->format = formats[c];
        if (formats[c] == CKOBJANIM_FORMAT_SHARED) {
            nmo_object_id_t shared_group = clip(&f,"Shared",0,0,10,0,10);
            const nmo_keyedanimation_state_t *shared = fixture_state(&f,shared_group);
            part->has_shared_anim = 1;
            part->shared_anim = shared->animation_ids[0];
            part->controller_count = 0;
            part->controllers = NULL;
        } else if (formats[c] == CKOBJANIM_FORMAT_LEGACY) {
            nmo_object_t *object = nmo_object_repository_find_by_id(f.repo,channel);
            object->chunk = nmo_chunk_create(nmo_session_get_arena(f.session));
            ASSERT_NOT_NULL(object->chunk);
            object->chunk->class_id = NMO_CID_OBJECTANIMATION;
            object->chunk->data_version = 0;
        }
        fixture_begin_edit(&f);
        fixture_commit_bare(&f,NULL);
        const char *path = NMO_TEST_SCRATCH_FILE("engine_character.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(f.session,path,NULL,NULL));
        for (size_t started = 0; started < 2; started++) {
            engine_fixture_t host;
            fixture_begin_document(&host);
            fixture_begin_edit(&host);
            fixture_commit_bare(&host,NULL);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1;
            ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"character.cmo",true,&file));
            ASSERT_TRUE(file > 0);
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t loaded_actor = nmo_engine_object_named(host.engine,"Character",NMO_CID_CHARACTER);
            nmo_object_id_t loaded_body = nmo_engine_object_named(host.engine,"Body",NMO_CID_BODYPART);
            nmo_object_id_t loaded_a = nmo_engine_object_named(host.engine,"Saved Primary",NMO_CID_KEYEDANIMATION);
            nmo_object_id_t loaded_b = nmo_engine_object_named(host.engine,"Saved Next",NMO_CID_KEYEDANIMATION);
            ASSERT_TRUE(loaded_actor != 0 && loaded_actor != actor && loaded_body != 0 && loaded_a != 0 && loaded_b != 0);
            ASSERT_EQ(loaded_a,eng_character_active_animation(host.engine,loaded_actor));
            ASSERT_EQ(loaded_b,eng_character_next_animation(host.engine,loaded_actor));
            ASSERT_TRUE(eng_character_automatic(host.engine,loaded_actor));
            ASSERT_TRUE(nmo_engine_object_is_active(host.engine,loaded_actor));
            ASSERT_FLOAT_EQ(2,frame(host.engine,loaded_a),0);
            ASSERT_FLOAT_EQ(2,x(host.engine,loaded_actor),0);
            ASSERT_FLOAT_EQ(2,x(host.engine,loaded_body),0);
            ASSERT_EQ(loaded_actor,nmo_engine_entity_parent(host.engine,loaded_body));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,3));
            ASSERT_FLOAT_EQ(5,x(host.engine,loaded_actor),1e-6f);
            ASSERT_FLOAT_EQ(5,x(host.engine,loaded_body),1e-6f);
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,6));
            ASSERT_EQ(loaded_b,eng_character_active_animation(host.engine,loaded_actor));
            ASSERT_EQ(0,eng_character_next_animation(host.engine,loaded_actor));
            ASSERT_FLOAT_EQ(2,frame(host.engine,loaded_b),1e-6f);
            ASSERT_FLOAT_EQ(14,x(host.engine,loaded_actor),1e-6f);
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,2));
            ASSERT_FLOAT_EQ(22,x(host.engine,loaded_actor),1e-6f);
            ASSERT_FLOAT_EQ(22,x(host.engine,loaded_body),1e-6f);
            ASSERT_FLOAT_EQ(0,x(host.engine,host.owner_id),0);
            const nmo_animation_state_t *saved = eng_animation_state(host.engine,loaded_a);
            ASSERT_FLOAT_EQ(0.2f,saved->current_step,0);
            nmo_object_id_t loaded_channel = eng_keyed_animation_root(host.engine,loaded_a);
            eng_animation_source_t loaded;
            ASSERT_TRUE(eng_animation_source(host.engine,loaded_channel,&loaded));
            ASSERT_EQ(formats[c],loaded.state->format);
            ASSERT_FLOAT_EQ(-2,loaded.state->root_pos.x,0);
            fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(0.2f,primary->base.current_step,0);
        ASSERT_FLOAT_EQ(-2,part->root_pos.x,0);
        ASSERT_FLOAT_EQ(2,character_state->base.world_matrix[12],0);
        ASSERT_EQ(a,nmo_ref_runtime_id(&character_state->active_animation));
        ASSERT_EQ(b,nmo_ref_runtime_id(&character_state->anim_dest));
        fixture_end(&f);
        remove(path);
    }
}

static void character_script(engine_fixture_t *f)
{
    f->script_id = fixture_add_script(f,"Character Controls",&f->script_in);
    ASSERT_TRUE(f->script_id != 0);
    fixture_begin_edit(f);
}

static void animation_inputs(engine_fixture_t *f, nmo_object_id_t block, nmo_object_id_t target,
                              nmo_object_id_t animation, float value)
{
    ASSERT_TRUE(block != 0);
    fixture_input_int(f,fixture_target(f,block),(int32_t)target);
    fixture_input_value(f,BLOCK_PIN(f,block,0),&value,sizeof(value));
    fixture_input_int(f,BLOCK_PIN(f,block,1),(int32_t)animation);
}

TEST(engine_character, pose_blocks_use_fractional_frames_and_normalized_steps_without_selecting_an_active_clip)
{
    const nmo_guid_t guids[] = {GUID_ANIMATION_FRAME,GUID_ANIMATION_STEP};
    for (size_t i = 0; i < COUNT(guids); i++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t foreign_body, foreign = character(&f,&foreign_body);
        nmo_object_id_t animation = clip(&f,"Pose",foreign,foreign_body,10,0,10);
        character_script(&f);
        nmo_object_id_t set = fixture_block(&f,guids[i],"Pose");
        animation_inputs(&f,set,actor,animation,i == 0 ? 3.5f : .35f);
        fixture_link(&f,f.script_in,BLOCK_IN(&f,set,0),0);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_FLOAT_EQ(3.5f,frame(f.engine,animation),1e-6f);
        ASSERT_FLOAT_EQ(3.5f,x(f.engine,foreign_body),1e-6f);
        ASSERT_FLOAT_EQ(0,x(f.engine,body),0);
        ASSERT_EQ(0,eng_character_active_animation(f.engine,actor));
        ASSERT_EQ(0,eng_character_active_animation(f.engine,foreign));
        ASSERT_EQ(1u,runs(&f,set));
        fixture_end(&f);
    }
}

TEST(engine_character, pose_blocks_allow_plain_animation_cursors_and_missing_animation_keeps_control_flow)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t animation = fixture_create(&f,NMO_CID_ANIMATION,"Plain");
    nmo_animation_state_t *state = fixture_state(&f,animation);
    state->has_length = 1; state->length = 10;
    character_script(&f);
    nmo_object_id_t missing = fixture_block(&f,GUID_ANIMATION_FRAME,"Missing");
    nmo_object_id_t set = fixture_block(&f,GUID_ANIMATION_STEP,"Plain Step");
    animation_inputs(&f,missing,actor,0,5);
    animation_inputs(&f,set,actor,animation,1.25f);
    fixture_link(&f,f.script_in,BLOCK_IN(&f,missing,0),0);
    fixture_link(&f,BLOCK_OUT(&f,missing,0),BLOCK_IN(&f,set,0),0);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&f,&options);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(1u,runs(&f,missing)); ASSERT_EQ(1u,runs(&f,set));
    ASSERT_FLOAT_EQ(12.5f,frame(f.engine,animation),0);
    ASSERT_FLOAT_EQ(0,x(f.engine,body),0);
    fixture_end(&f);
}

TEST(engine_character, pose_blocks_report_native_owner_and_parameter_errors_in_strict_mode)
{
    const nmo_guid_t guids[] = {GUID_ANIMATION_FRAME,GUID_ANIMATION_STEP,GUID_BODYPART_FRAME};
    for (size_t i = 0; i < COUNT(guids); i++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t plain = fixture_create(&f,NMO_CID_ANIMATION,"Plain");
        nmo_object_id_t animation = clip(&f,"Pose",actor,body,10,0,10);
        character_script(&f);
        nmo_object_id_t bad = fixture_block(&f,guids[i],"Invalid");
        nmo_object_id_t after = fixture_block(&f,GUID_ANIMATION_FRAME,"After");
        animation_inputs(&f,bad,i == 2 ? body : 0,plain,5);
        animation_inputs(&f,after,actor,animation,5);
        fixture_link(&f,f.script_in,BLOCK_IN(&f,bad,0),0);
        fixture_link(&f,BLOCK_OUT(&f,bad,0),BLOCK_IN(&f,after,0),0);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,1));
        nmo_engine_event_t failure;
        ASSERT_TRUE(nmo_engine_failure(f.engine,&failure));
        ASSERT_EQ(bad,failure.behavior_id);
        ASSERT_EQ(i == 2 ? NMO_ENGINE_BR_PARAMETERERROR : NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_EQ(0u,runs(&f,after));
        fixture_end(&f);
    }
}

TEST(engine_character, bodypart_frame_bypasses_exclusion_uses_child_duration_and_preserves_the_keyed_cursor)
{
    for (size_t hierarchy = 0; hierarchy < 2; hierarchy++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
        ((nmo_3dentity_state_t *)fixture_state(&f,bone))->parent = nmo_ref_from_id(root);
        nmo_object_id_t a = clip(&f,"Root",0,root,10,0,10), b = clip(&f,"Limb",0,bone,10,10,30);
        nmo_keyedanimation_state_t *group = fixture_state(&f,b);
        nmo_object_id_t channel = nmo_ref_runtime_id(&group->animation_ids[0]);
        nmo_objectanimation_state_t *part = fixture_state(&f,channel);
        part->length = 20; ((float *)part->controllers[0].data)[4] = 20;
        character_script(&f);
        nmo_object_id_t exclude = fixture_block(&f,GUID_EXCLUDE_ANIMATION,"Exclude");
        nmo_object_id_t whole = fixture_block(&f,GUID_ANIMATION_FRAME,"Whole");
        nmo_object_id_t body = fixture_block(&f,GUID_BODYPART_FRAME,"Body");
        fixture_input_int(&f,fixture_target(&f,exclude),(int32_t)root);
        fixture_input_int(&f,BLOCK_PIN(&f,exclude,1),(int32_t)hierarchy);
        animation_inputs(&f,whole,actor,a,5);
        animation_inputs(&f,body,bone,b,5);
        fixture_link(&f,f.script_in,BLOCK_IN(&f,exclude,0),0);
        fixture_link(&f,BLOCK_OUT(&f,exclude,0),BLOCK_IN(&f,whole,0),0);
        fixture_link(&f,BLOCK_OUT(&f,whole,0),BLOCK_IN(&f,body,0),0);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
        ASSERT_FLOAT_EQ(0,x(f.engine,root),0); ASSERT_FLOAT_EQ(15,x(f.engine,bone),1e-6f);
        ASSERT_FLOAT_EQ(5,frame(f.engine,a),0); ASSERT_FLOAT_EQ(0,frame(f.engine,b),0);
        ASSERT_TRUE(eng_animation_set_frame(f.engine,b,8));
        ASSERT_FLOAT_EQ(hierarchy ? 15 : 26,x(f.engine,bone),1e-6f);
        ASSERT_EQ(0u,((const nmo_3dentity_state_t *)fixture_state(&f,root))->moveable_flags & CK_3DENTITY_IGNOREANIMATION);
        if (hierarchy) {
            nmo_object_id_t originals[] = {actor,root,bone,b}, copies[COUNT(originals)] = {0};
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,originals,COUNT(originals),false,copies));
            ASSERT_TRUE(eng_animation_set_frame(f.engine,copies[3],10));
            ASSERT_FLOAT_EQ(15,x(f.engine,copies[2]),1e-6f);
            ASSERT_TRUE(eng_object_animation_set_frame(f.engine,eng_keyed_animation_part(f.engine,copies[3],copies[2]),6,true));
            ASSERT_FLOAT_EQ(16,x(f.engine,copies[2]),1e-6f);
            ASSERT_FLOAT_EQ(15,x(f.engine,bone),1e-6f);
        }
        fixture_end(&f);
    }
}

TEST(engine_character, forced_bodypart_frames_keep_secondary_exclusivity_and_zero_length_channels_unchanged)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t secondary = clip(&f,"Secondary",actor,bone,10,100,110);
    nmo_object_id_t pose = clip(&f,"Pose",0,bone,10,10,20), zero = clip(&f,"Zero",0,bone,0,30,30);
    character_script(&f);
    nmo_object_id_t exclude = fixture_block(&f,GUID_EXCLUDE_ANIMATION,"Exclude");
    nmo_object_id_t set = fixture_block(&f,GUID_BODYPART_FRAME,"Body");
    fixture_input_int(&f,fixture_target(&f,exclude),(int32_t)bone);
    animation_inputs(&f,set,bone,pose,5);
    fixture_link(&f,f.script_in,BLOCK_IN(&f,exclude,0),0);
    fixture_link(&f,BLOCK_OUT(&f,exclude,0),BLOCK_IN(&f,set,0),0);
    fixture_commit(&f,NULL);
    ASSERT_EQ(NMO_OK,eng_character_play_secondary(f.engine,actor,secondary,2,CKSECONDARYANIMATION_LOOP,0));
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_FLOAT_EQ(102,x(f.engine,bone),0);
    ASSERT_EQ(NMO_OK,eng_character_stop_secondary(f.engine,actor,secondary));
    ASSERT_TRUE(eng_object_animation_set_frame(f.engine,eng_keyed_animation_part(f.engine,pose,bone),5,true));
    ASSERT_FLOAT_EQ(15,x(f.engine,bone),1e-6f);
    ASSERT_FALSE(eng_object_animation_set_frame(f.engine,eng_keyed_animation_part(f.engine,zero,bone),0,true));
    ASSERT_FLOAT_EQ(15,x(f.engine,bone),1e-6f);
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
REGISTER_TEST(engine_character, copied_characters_remap_the_live_primary_and_keep_independent_clocks);
REGISTER_TEST(engine_character, secondary_one_shots_hold_exact_endpoints_and_expire_without_a_forward_terminal_sample);
REGISTER_TEST(engine_character, secondary_loops_consume_large_overruns_and_finite_counts_hold_the_terminal_pose);
REGISTER_TEST(engine_character, secondary_reverse_unlinked_and_zero_length_playback_keep_strict_boundaries);
REGISTER_TEST(engine_character, secondary_bones_exclude_other_clips_and_release_only_their_own_gates);
REGISTER_TEST(engine_character, secondary_removal_processes_shifted_entries_and_releases_deleted_channels);
REGISTER_TEST(engine_character, secondary_presets_and_invalid_requests_preserve_existing_playback);
REGISTER_TEST(engine_character, secondary_gates_are_copied_and_remapped_without_copying_playback_slots);
REGISTER_TEST(engine_character, automatic_processing_advances_primary_then_secondary_and_plain_entities_have_no_gate);
REGISTER_TEST(engine_character, loaded_scriptless_characters_restore_primary_queues_and_keep_file_state_immutable);
REGISTER_TEST(engine_character, pose_blocks_use_fractional_frames_and_normalized_steps_without_selecting_an_active_clip);
REGISTER_TEST(engine_character, pose_blocks_allow_plain_animation_cursors_and_missing_animation_keeps_control_flow);
REGISTER_TEST(engine_character, pose_blocks_report_native_owner_and_parameter_errors_in_strict_mode);
REGISTER_TEST(engine_character, bodypart_frame_bypasses_exclusion_uses_child_duration_and_preserves_the_keyed_cursor);
REGISTER_TEST(engine_character, forced_bodypart_frames_keep_secondary_exclusivity_and_zero_length_channels_unchanged);
TEST_MAIN_END()
