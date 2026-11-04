#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_character.h"
#include "object/builtin/nmo_character_schemas.h"
#include "core/nmo_utils.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_object_edit.h"
#include "type/nmo_param_guids.h"
#include "format/nmo_chunk_api.h"

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

static void saved_input(engine_fixture_t *f, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t source = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(f->tx,pin,&source));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f->tx,source,text));
}

static void saved_object(engine_fixture_t *f, nmo_object_id_t pin, nmo_object_id_t object)
{
    char value[32];
    snprintf(value,sizeof(value),"%u",object);
    saved_input(f,pin,value);
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

TEST(engine_character, global_playback_takes_over_automatic_characters_without_stopping_their_scripts)
{
    for (int own_script = 0; own_script < 2; own_script++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t primary = clip(&f,"Primary",actor,body,10,0,10);
        nmo_object_id_t manual = clip(&f,"Manual",actor,body,10,100,110);
        nmo_character_state_t *state = fixture_state(&f,actor);
        state->base.entity_flags |= CK_3DENTITY_CHARACTERDOPROCESS;
        state->active_animation = nmo_ref_from_id(primary);
        if (own_script) f.owner_id = actor;
        character_script(&f);
        nmo_object_id_t play = fixture_block(&f,NMO_GUID(0x1c9236e1,0x42f40996),"Global");
        fixture_input_int(&f,BLOCK_PIN(&f,play,0),(int32_t)manual);
        const float duration = 10;
        fixture_input_value(&f,BLOCK_PIN(&f,play,1),&duration,sizeof(duration));
        fixture_input_int(&f,BLOCK_PIN(&f,play,3),0);
        fixture_link(&f,f.script_in,BLOCK_IN(&f,play,0),0);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_NONE));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
        ASSERT_FLOAT_EQ(2,frame(f.engine,primary),0); ASSERT_FLOAT_EQ(2,x(f.engine,actor),0);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_FALSE(eng_character_automatic(f.engine,actor));
        ASSERT_TRUE(nmo_engine_object_is_active(f.engine,actor));
        ASSERT_EQ(primary,eng_character_active_animation(f.engine,actor));
        ASSERT_FLOAT_EQ(0,x(f.engine,actor),0); ASSERT_FLOAT_EQ(100,x(f.engine,body),0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
        ASSERT_EQ(2u,runs(&f,play));
        ASSERT_FLOAT_EQ(2,frame(f.engine,primary),0);
        ASSERT_FLOAT_EQ(102,x(f.engine,body),1e-5f); ASSERT_FLOAT_EQ(0,x(f.engine,actor),0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,9));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
        ASSERT_EQ(3u,runs(&f,play));
        ASSERT_FLOAT_EQ(110,x(f.engine,body),0); ASSERT_FLOAT_EQ(2,frame(f.engine,primary),0);
        ASSERT_TRUE((state->base.entity_flags & CK_3DENTITY_CHARACTERDOPROCESS) != 0);
        nmo_object_id_t originals[] = {actor,body,primary}, copies[COUNT(originals)] = {0};
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,originals,COUNT(originals),true,copies));
        ASSERT_FALSE(eng_character_automatic(f.engine,copies[0]));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
        ASSERT_FLOAT_EQ(2,frame(f.engine,copies[2]),0);
        ASSERT_FLOAT_EQ(0,x(f.engine,copies[0]),0);
        fixture_end(&f);
    }
}

TEST(engine_character, loaded_character_controls_remap_parameters_and_execute_all_animation_formats)
{
    const nmo_objectanimation_format_t formats[] = {CKOBJANIM_FORMAT_CONTROLLERS,
        CKOBJANIM_FORMAT_NEWDATA,CKOBJANIM_FORMAT_LEGACY,CKOBJANIM_FORMAT_SHARED};
    for (size_t c = 0; c < COUNT(formats); c++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t animation = clip(&f,"Saved Pose",actor,body,10,0,10);
        nmo_keyedanimation_state_t *keyed = fixture_state(&f,animation);
        nmo_object_id_t channel = nmo_ref_runtime_id(&keyed->animation_ids[0]);
        nmo_objectanimation_state_t *part = fixture_state(&f,channel);
        part->format = formats[c];
        if (formats[c] == CKOBJANIM_FORMAT_SHARED) {
            nmo_object_id_t shared = clip(&f,"Shared Keys",0,0,10,0,10);
            part->has_shared_anim = 1;
            part->shared_anim = ((nmo_keyedanimation_state_t *)fixture_state(&f,shared))->animation_ids[0];
            part->controllers = NULL; part->controller_count = 0;
        } else if (formats[c] == CKOBJANIM_FORMAT_LEGACY) {
            nmo_object_t *object = nmo_object_repository_find_by_id(f.repo,channel);
            object->chunk = nmo_chunk_create(nmo_session_get_arena(f.session));
            ASSERT_NOT_NULL(object->chunk);
            object->chunk->class_id = NMO_CID_OBJECTANIMATION;
            object->chunk->data_version = 0;
        }
        character_script(&f);
        nmo_object_id_t floor = fixture_block(&f,NMO_GUID(0x0ad75ccb,0x0cd1369a),"Saved Floor");
        nmo_object_id_t lod = fixture_block(&f,NMO_GUID(0x200d6501,0x4cf61e52),"Saved LOD");
        nmo_object_id_t exclude = fixture_block(&f,GUID_EXCLUDE_ANIMATION,"Saved Exclude");
        nmo_object_id_t set_frame = fixture_block(&f,GUID_ANIMATION_FRAME,"Saved Frame");
        nmo_object_id_t step = fixture_block(&f,GUID_ANIMATION_STEP,"Saved Step");
        nmo_object_id_t force = fixture_block(&f,GUID_BODYPART_FRAME,"Saved Force");
        const nmo_object_id_t blocks[] = {floor,lod,exclude,set_frame,step,force};
        for (size_t i = 0; i < COUNT(blocks); i++) {
            saved_object(&f,fixture_target(&f,blocks[i]),i == 2 || i == 5 ? body : actor);
            fixture_link(&f,i == 0 ? f.script_in : BLOCK_OUT(&f,blocks[i-1],0),BLOCK_IN(&f,blocks[i],0),0);
        }
        saved_object(&f,BLOCK_PIN(&f,floor,0),body);
        saved_input(&f,BLOCK_PIN(&f,lod,0),"-100"); saved_input(&f,BLOCK_PIN(&f,lod,1),"-50");
        saved_input(&f,BLOCK_PIN(&f,lod,2),"0%");
        const nmo_object_id_t poses[] = {set_frame,step,force};
        const char *values[] = {"3","50%","4"};
        for (size_t i = 0; i < COUNT(poses); i++) {
            saved_input(&f,BLOCK_PIN(&f,poses[i],0),values[i]);
            saved_object(&f,BLOCK_PIN(&f,poses[i],1),animation);
        }
        fixture_commit(&f,NULL);
        nmo_beobject_state_t *driver = fixture_state(&f,f.owner_id);
        driver->has_single_activity = 1;
        driver->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&f,f.script_id);
        script->has_single_activity = true;
        script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_character_controls.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(f.session,path,NULL,NULL));
        engine_fixture_t host;
        fixture_begin_document(&host); fixture_begin_edit(&host);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
        int32_t file = -1;
        ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"controls.cmo",true,&file));
        ASSERT_TRUE(file > 0);
        nmo_object_id_t loaded_actor = nmo_engine_object_named(host.engine,"Character",NMO_CID_CHARACTER);
        nmo_object_id_t loaded_body = nmo_engine_object_named(host.engine,"Body",NMO_CID_BODYPART);
        nmo_object_id_t loaded_pose = nmo_engine_object_named(host.engine,"Saved Pose",NMO_CID_KEYEDANIMATION);
        ASSERT_TRUE(loaded_actor != 0 && loaded_actor != actor && loaded_body != 0 && loaded_pose != 0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        for (size_t i = 0; i < COUNT(blocks); i++) {
            nmo_object_id_t loaded = nmo_engine_object_named(host.engine,nmo_object_get_name(nmo_object_repository_find_by_id(f.repo,blocks[i])),NMO_CID_BEHAVIOR);
            ASSERT_TRUE(loaded != 0); ASSERT_EQ(1u,runs(&host,loaded));
        }
        ASSERT_EQ(loaded_body,eng_character_floor_reference(host.engine,loaded_actor));
        ASSERT_FLOAT_EQ(0,eng_character_animation_lod(host.engine,loaded_actor),0);
        ASSERT_FLOAT_EQ(5,frame(host.engine,loaded_pose),1e-6f);
        ASSERT_FLOAT_EQ(4,x(host.engine,loaded_body),1e-6f);
        ASSERT_TRUE(eng_animation_set_frame(host.engine,loaded_pose,6));
        ASSERT_FLOAT_EQ(4,x(host.engine,loaded_body),1e-6f);
        ASSERT_FLOAT_EQ(0,eng_animation_state(host.engine,loaded_pose)->current_step,0);
        eng_animation_source_t source;
        ASSERT_TRUE(eng_animation_source(host.engine,eng_keyed_animation_root(host.engine,loaded_pose),&source));
        ASSERT_EQ(formats[c],source.state->format);
        ASSERT_EQ(0u,((const nmo_3dentity_state_t *)fixture_state(&f,body))->moveable_flags & CK_3DENTITY_IGNOREANIMATION);
        ASSERT_EQ(0u,nmo_ref_runtime_id(&((const nmo_character_state_t *)fixture_state(&f,actor))->floor_ref));
        ASSERT_FLOAT_EQ(0,x(host.engine,host.owner_id),0);
        fixture_end(&host); fixture_end(&f); remove(path);
    }
}

TEST(engine_character, runtime_playback_settings_drive_clocks_interrupts_and_copies_without_editing_sources)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10), b = clip(&f,"B",actor,body,10,100,110);
    fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
    ASSERT_TRUE(eng_animation_set_playback(f.engine,a,CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK,
        CKANIMATION_LINKTOFRAMERATE,500));
    ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,a));
    ASSERT_EQ(NMO_OK,eng_character_process(f.engine,actor,4));
    ASSERT_FLOAT_EQ(2,frame(f.engine,a),0);
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,b,CK_TRANSITION_FROMNOW));
    ASSERT_EQ(a,eng_character_active_animation(f.engine,actor));
    ASSERT_EQ(b,eng_character_next_animation(f.engine,actor));
    ASSERT_TRUE(eng_animation_set_playback(f.engine,a,CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,500));
    ASSERT_EQ(NMO_OK,eng_character_set_next_animation(f.engine,actor,b,CK_TRANSITION_FROMNOW));
    ASSERT_EQ(b,eng_character_active_animation(f.engine,actor));
    const nmo_animation_state_t *saved = eng_animation_state(f.engine,a);
    ASSERT_FLOAT_EQ(1000,saved->frame_rate,0);
    ASSERT_EQ(0u,saved->flags & CKANIMATION_CANBEBREAK);
    nmo_object_id_t copy = 0;
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(f.engine,&a,1,false,&copy));
    ASSERT_FLOAT_EQ(500,eng_animation_playback_state(f.engine,copy)->frame_rate,0);
    ASSERT_TRUE(eng_animation_set_playback(f.engine,copy,CKANIMATION_LINKTOFRAMERATE,0,30));
    float next = 0;
    ASSERT_TRUE(eng_animation_next_frame(f.engine,copy,4000,&next));
    ASSERT_FLOAT_EQ(3,next,0);
    ASSERT_FLOAT_EQ(500,eng_animation_playback_state(f.engine,a)->frame_rate,0);
    ASSERT_FALSE(eng_animation_set_playback(f.engine,a,0,0,NAN));
    fixture_end(&f);
}

TEST(engine_character, automatic_processing_can_resume_and_register_a_late_scriptless_character)
{
    for (int before_start = 0; before_start < 2; before_start++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t a = clip(&f,"A",actor,body,10,0,10);
        ((nmo_character_state_t *)fixture_state(&f,actor))->active_animation = nmo_ref_from_id(a);
        fixture_begin_edit(&f); fixture_commit_bare(&f,NULL);
        if (before_start) ASSERT_TRUE(eng_character_set_automatic(f.engine,actor,true));
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_TRUE(eng_character_set_automatic(f.engine,actor,true));
        ASSERT_TRUE(eng_character_set_automatic(f.engine,actor,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
        ASSERT_FLOAT_EQ(2,frame(f.engine,a),0);
        ASSERT_TRUE(eng_character_set_automatic(f.engine,actor,false));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
        ASSERT_FLOAT_EQ(2,frame(f.engine,a),0);
        ASSERT_TRUE(eng_character_set_automatic(f.engine,actor,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,3));
        ASSERT_FLOAT_EQ(5,frame(f.engine,a),0);
        ASSERT_EQ(NMO_OK,nmo_engine_activate_object(f.engine,actor,false));
        ASSERT_TRUE(eng_character_set_automatic(f.engine,actor,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
        ASSERT_FLOAT_EQ(5,frame(f.engine,a),0);
        ASSERT_FALSE(eng_character_set_automatic(f.engine,body,true));
        ASSERT_EQ(0u,((const nmo_character_state_t *)fixture_state(&f,actor))->base.entity_flags & CK_3DENTITY_CHARACTERDOPROCESS);
        fixture_end(&f);
    }
}

static nmo_object_id_t controller_local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = fixture_state(f,block);
    return state != NULL && pos < state->local_parameters.count ? nmo_behavior_ref_array_get_id(&state->local_parameters,pos) : 0;
}

static void controller_record(uint8_t *record, int32_t index, const char *message, uint16_t flags, float fps)
{
    memset(record,0,152);
    nmo_write_u32_le(record,(uint32_t)index);
    nmo_write_u32_le(record+48,0xdeadbeef); /* An obsolete saved pointer must never be followed. */
    memset(record+56,0xff,16);
    snprintf((char *)record+72,64,"%s",message);
    nmo_write_u32_le(record+136,0x12345678); /* Saved message IDs are not engine IDs. */
    uint32_t word;
    memcpy(&word,&fps,4); nmo_write_u32_le(record+140,word);
    float warp = 2; memcpy(&word,&warp,4); nmo_write_u32_le(record+144,word);
    nmo_write_u16_le(record+148,flags);
}

static void controller_bytes(engine_fixture_t *f, nmo_object_id_t block, size_t pos, const void *bytes, size_t size)
{
    nmo_parameter_state_t *state = fixture_state(f,controller_local(f,block,pos));
    ASSERT_NOT_NULL(state);
    nmo_array_dispose(&state->buffer_data);
    ASSERT_EQ(NMO_OK,nmo_array_alloc(&state->buffer_data,1,size,NULL));
    if (size != 0) memcpy(state->buffer_data.data,bytes,size);
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
}

static void controller_data(engine_fixture_t *f, nmo_object_id_t block,
    const nmo_object_id_t *animations, size_t count, const uint8_t *records, int32_t primary_count)
{
    ASSERT_TRUE(block != 0);
    controller_bytes(f,block,0,animations,count*4);
    controller_bytes(f,block,1,records,(size_t)primary_count*152);
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(f->tx,controller_local(f,block,2),(const uint8_t *)&primary_count,4));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f->tx,controller_local(f,block,3),"FALSE"));
}

static nmo_object_id_t controller_block(engine_fixture_t *f,
    const nmo_object_id_t *animations, size_t count, const uint8_t *records, int32_t primary_count)
{
    nmo_object_id_t block = fixture_block(f,NMO_GUID(0x59537e89,0x79d02f25),"Controller");
    controller_data(f,block,animations,count,records,primary_count);
    fixture_link(f,f->script_in,BLOCK_IN(f,block,0),0);
    return block;
}

static nmo_object_id_t controller(engine_fixture_t *f, nmo_object_id_t actor,
    const nmo_object_id_t *animations, size_t count, const uint8_t *records, int32_t primary_count)
{
    f->owner_id = actor;
    character_script(f);
    return controller_block(f,animations,count,records,primary_count);
}

TEST(engine_character, unlimited_primary_uses_message_order_fallback_and_runtime_playback_settings)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t animations[] = {clip(&f,"Run",actor,body,10,0,10),clip(&f,"Idle",actor,body,10,100,110)};
    uint8_t records[3][152];
    controller_record(records[0],0,"Run",2|128,500);
    controller_record(records[1],1,"Other",2|128,250);
    controller_record(records[2],1,"",2|128,250);
    nmo_object_id_t block = controller(&f,actor,animations,COUNT(animations),records[0],3);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&f,&options);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_TRUE(eng_character_automatic(f.engine,actor));
    ASSERT_EQ(animations[1],eng_character_next_animation(f.engine,actor));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,4));
    ASSERT_EQ(animations[1],eng_character_active_animation(f.engine,actor));
    ASSERT_FLOAT_EQ(1,frame(f.engine,animations[1]),0);
    float output = -1;
    read_value(&f,BLOCK_POUT(&f,block,3),&output,4); ASSERT_FLOAT_EQ(1,output,0);
    int32_t run = nmo_engine_message_type(f.engine,"Run"), other = nmo_engine_message_type(f.engine,"Other");
    ASSERT_TRUE(run >= 0 && other >= 0);
    ASSERT_EQ(NMO_OK,nmo_engine_send_message(f.engine,other,actor,0));
    ASSERT_EQ(NMO_OK,nmo_engine_send_message(f.engine,run,actor,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); /* Delivery follows execution. */
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    ASSERT_EQ(animations[0],eng_character_active_animation(f.engine,actor));
    int32_t message = -1;
    read_value(&f,BLOCK_POUT(&f,block,1),&message,4); ASSERT_EQ(run,message);
    ASSERT_FLOAT_EQ(500,eng_animation_playback_state(f.engine,animations[0])->frame_rate,0);
    ASSERT_FLOAT_EQ(1000,eng_animation_state(f.engine,animations[0])->frame_rate,0);
    ASSERT_EQ(0u,eng_animation_state(f.engine,animations[0])->flags & CKANIMATION_CANBEBREAK);
    ASSERT_EQ(0u,((const nmo_character_state_t *)fixture_state(&f,actor))->base.entity_flags & CK_3DENTITY_CHARACTERDOPROCESS);
    ASSERT_EQ(0xdeadbeefu,nmo_read_u32_le(records[0]+48));
    fixture_end(&f);
}

TEST(engine_character, unlimited_animation_cache_follows_reordering_and_deletion)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t animations[] = {clip(&f,"A",actor,body,10,0,10),clip(&f,"B",actor,body,10,100,110)};
    uint8_t record[152]; controller_record(record,0,"",2,30);
    nmo_object_id_t block = controller(&f,actor,animations,2,record,1);
    fixture_commit(&f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    nmo_object_id_t reversed[] = {animations[1],animations[0]};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,controller_local(&f,block,0),reversed,sizeof(reversed)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    uint8_t runtime[152]; read_value(&f,controller_local(&f,block,1),runtime,sizeof(runtime));
    ASSERT_EQ(1u,nmo_read_u32_le(runtime)); ASSERT_EQ(animations[0],nmo_read_u32_le(runtime+48));
    nmo_engine_destroy_object(f.engine,animations[0]);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(f.engine,controller_local(&f,block,0),&animations[1],4));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    read_value(&f,controller_local(&f,block,1),runtime,sizeof(runtime));
    ASSERT_EQ(UINT32_MAX,nmo_read_u32_le(runtime)); ASSERT_EQ(0u,nmo_read_u32_le(runtime+48));
    ASSERT_EQ(0u,eng_character_next_animation(f.engine,actor));
    fixture_end(&f);
}

TEST(engine_character, unlimited_off_stops_automatic_processing_and_invalid_counts_fail_before_activation)
{
    for (int invalid = 0; invalid < 3; invalid++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body), animation = clip(&f,"A",actor,body,10,0,10);
        uint8_t record[152]; controller_record(record,0,"",2|128,500);
        nmo_object_id_t block = controller(&f,actor,&animation,1,record,1);
        if (invalid != 0) {
            int32_t count = invalid == 1 ? -1 : 2;
            ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(f.tx,controller_local(&f,block,2),(const uint8_t *)&count,4));
        } else fixture_link(&f,BLOCK_OUT(&f,block,0),BLOCK_IN(&f,block,1),1);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&f,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        if (invalid) {
            ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,1));
            ASSERT_FALSE(eng_character_automatic(f.engine,actor));
            nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure));
            ASSERT_EQ(NMO_ENGINE_BR_GENERICERROR,failure.result);
        } else {
            ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
            ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
            ASSERT_FALSE(eng_character_automatic(f.engine,actor));
            float held = frame(f.engine,animation);
            ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
            ASSERT_FLOAT_EQ(held,frame(f.engine,animation),0); ASSERT_EQ(2u,runs(&f,block));
        }
        fixture_end(&f);
    }
}

static void controller_send(engine_fixture_t *f, nmo_object_id_t actor, const char *name)
{
    int32_t message = nmo_engine_message_type(f->engine,name);
    ASSERT_TRUE(message >= 0);
    ASSERT_EQ(NMO_OK,nmo_engine_send_message(f->engine,message,actor,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f->engine,0));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f->engine,0));
}

TEST(engine_character, unlimited_secondary_messages_process_every_matching_record_and_warp_stops)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), a_body = limb(&f,actor), b_body = limb(&f,actor);
    nmo_object_id_t animations[] = {clip(&f,"Idle",actor,root,10,0,0),
        clip(&f,"A",actor,a_body,10,100,110),clip(&f,"B",actor,b_body,10,200,210)};
    uint8_t primary[152]; controller_record(primary,0,"",2|128,1000);
    nmo_object_id_t block = controller(&f,actor,animations,3,primary,1);
    uint8_t secondary[4][152];
    for (int i = 0; i < 4; i++) {
        controller_record(secondary[i],1+i%2,i < 2 ? "Action" : "Stop",i < 2 ? 1|128 : 8,1000);
        float start = 1.25f; uint32_t word; memcpy(&word,&start,4); nmo_write_u32_le(secondary[i]+4,word);
    }
    controller_bytes(&f,block,6,secondary,sizeof(secondary));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,controller_local(&f,block,7),"4"));
    fixture_commit(&f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    controller_send(&f,actor,"Action");
    ASSERT_EQ(2u,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(1.25f,frame(f.engine,animations[1]),0); ASSERT_FLOAT_EQ(1.25f,frame(f.engine,animations[2]),0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_FLOAT_EQ(103.25f,x(f.engine,a_body),1e-5f); ASSERT_FLOAT_EQ(203.25f,x(f.engine,b_body),1e-5f);
    controller_send(&f,actor,"Stop");
    ASSERT_EQ(2u,eng_character_secondary_count(f.engine,actor));
    ASSERT_TRUE(eng_character_secondary_warper(f.engine,actor,0) != 0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,3));
    ASSERT_EQ(0u,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(1000,eng_animation_state(f.engine,animations[1])->frame_rate,0);
    fixture_end(&f);
}

TEST(engine_character, unlimited_dynamic_inputs_override_current_records_and_old_versions_ignore_them)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t root, actor = character(&f,&root);
        nmo_object_id_t animations[] = {clip(&f,"A",actor,root,10,0,10),clip(&f,"B",actor,root,10,100,110)};
        uint8_t record[152]; controller_record(record,0,"",0,30);
        nmo_write_u16_le(record+54,1|8|16|32|64|128);
        record[56] = 0; record[59] = 1; record[60] = 2; record[61] = 3; record[62] = 4; record[63] = 5;
        nmo_object_id_t block = controller(&f,actor,animations,2,record,1);
        if (legacy) ((nmo_behavior_state_t *)fixture_state(&f,block))->block_version = 0x10000;
        const nmo_guid_t types[] = {CKPGUID_ANIMATION,CKPGUID_BOOL,CKPGUID_BOOL,CKPGUID_FLOAT,CKPGUID_BOOL,CKPGUID_BOOL};
        for (uint32_t i = 0; i < COUNT(types); i++) {
            nmo_object_id_t pin = 0;
            ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(f.tx,block,NMO_SCRIPT_EDIT_PARAM_IN,types[i],"Override",&pin));
            if (i == 0) fixture_input_int(&f,pin,(int32_t)animations[1]);
            else if (i == 3) { float fps = 250; fixture_input_value(&f,pin,&fps,4); }
            else fixture_input_int(&f,pin,1);
        }
        fixture_commit(&f,NULL);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        nmo_object_id_t selected = animations[legacy ? 0 : 1];
        ASSERT_EQ(selected,eng_character_next_animation(f.engine,actor));
        const nmo_animation_state_t *settings = eng_animation_playback_state(f.engine,selected);
        ASSERT_FLOAT_EQ(legacy ? 30 : 250,settings->frame_rate,0);
        uint32_t mask = CKANIMATION_CANBEBREAK | CKANIMATION_LINKTOFRAMERATE | CKANIMATION_ALLOWTURN | CKANIMATION_ALIGNORIENTATION;
        ASSERT_EQ(legacy ? 0u : mask,settings->flags & mask);
        ASSERT_FLOAT_EQ(1000,eng_animation_state(f.engine,selected)->frame_rate,0);
        fixture_end(&f);
    }
}

TEST(engine_character, unlimited_primary_warp_modes_reach_the_transition_builder)
{
    const uint16_t flags[] = {8,8|2048,8|4096};
    const uint32_t modes[] = {CK_TRANSITION_WARPBEST,CK_TRANSITION_WARPSTART,CK_TRANSITION_WARPSAMEPOS};
    for (size_t i = 0; i < COUNT(flags); i++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body);
        nmo_object_id_t animations[] = {clip(&f,"A",actor,body,10,0,10),clip(&f,"B",actor,body,10,100,110)};
        uint8_t record[152]; controller_record(record,1,"",flags[i]|2|128,500);
        controller(&f,actor,animations,2,record,1);
        fixture_commit(&f,NULL);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_TRUE(eng_animation_set_playback(f.engine,animations[0],CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,1000));
        ASSERT_EQ(NMO_OK,eng_character_set_active_animation(f.engine,actor,animations[0]));
        ASSERT_TRUE(eng_animation_set_frame(f.engine,animations[0],2));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_EQ(eng_character_warper(f.engine,actor),eng_character_active_animation(f.engine,actor));
        uint32_t mode = 0;
        ASSERT_TRUE(eng_character_warper_parameters(f.engine,actor,&mode,NULL,NULL,NULL,NULL));
        ASSERT_EQ(modes[i]|CK_TRANSITION_USEVELOCITY|CK_TRANSITION_LOOPIFEQUAL,mode);
        ASSERT_FLOAT_EQ(500,eng_animation_playback_state(f.engine,animations[1])->frame_rate,0);
        fixture_end(&f);
    }
}

TEST(engine_character, unlimited_turning_uses_active_animation_permissions_and_linked_rate)
{
    for (int turn = 0; turn < 2; turn++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body), animation = clip(&f,"Idle",actor,body,10,0,0);
        uint8_t record[152]; controller_record(record,0,"",2|(turn ? 32 : 0),30);
        nmo_object_id_t block = controller(&f,actor,&animation,1,record,1);
        fixture_commit(&f,NULL);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        int32_t left = nmo_engine_message_type(f.engine,"Joy_Left");
        ASSERT_EQ(NMO_OK,nmo_engine_send_message(f.engine,left,actor,0));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,100));
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
        ASSERT_FLOAT_EQ(turn ? cosf(.18f) : 1,matrix[0],1e-6f);
        ASSERT_FLOAT_EQ(turn ? sinf(.18f) : 0,matrix[2],1e-6f);
        ASSERT_EQ(4u,runs(&f,block));
        fixture_end(&f);
    }
}

TEST(engine_character, goto_messages_drive_unlimited_root_motion_until_arrival_stops_loop_feedback)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t body, actor = character(&f,&body);
    nmo_object_id_t target = fixture_create(&f,NMO_CID_3DENTITY,"Destination");
    ((nmo_3dentity_state_t *)fixture_state(&f,target))->world_matrix[12] = 8;
    nmo_object_id_t animations[] = {clip(&f,"Idle",actor,body,10,0,0),clip(&f,"Walk",actor,body,10,0,10)};
    uint8_t records[2][152];
    controller_record(records[0],0,"",2|128,1000);
    controller_record(records[1],1,"Joy_Up",2|128,1000);
    nmo_object_id_t control = controller(&f,actor,animations,COUNT(animations),records[0],2);
    nmo_object_id_t go = fixture_block(&f,NMO_GUID(0x11223344,0xaabbccdd),"Go To");
    nmo_object_id_t arrived = fixture_block(&f,NMO_GUID(0x302561c4,0x0d282980),"Arrived"); ASSERT_TRUE(go != 0 && arrived != 0);
    fixture_input_int(&f,fixture_target(&f,go),(int32_t)actor);
    fixture_input_int(&f,BLOCK_PIN(&f,go,0),(int32_t)target);
    float distance = 1; fixture_input_value(&f,BLOCK_PIN(&f,go,1),&distance,sizeof(distance));
    fixture_link(&f,f.script_in,BLOCK_IN(&f,go,0),0);
    fixture_link(&f,BLOCK_OUT(&f,go,2),BLOCK_IN(&f,go,1),1);
    fixture_link(&f,BLOCK_OUT(&f,go,0),BLOCK_IN(&f,arrived,0),0);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    bool walked = false;
    for (int step = 0; step < 20 && runs(&f,arrived) == 0; step++) {
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
        if (eng_character_active_animation(f.engine,actor) == animations[1]) walked = true;
    }
    ASSERT_TRUE(walked); ASSERT_EQ(1u,runs(&f,arrived));
    ASSERT_TRUE(x(f.engine,actor) >= 7 && x(f.engine,actor) <= 9);
    uint64_t queries = runs(&f,go); ASSERT_TRUE(queries > 1);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,1));
    ASSERT_EQ(queries,runs(&f,go)); ASSERT_EQ(1u,runs(&f,arrived));
    ASSERT_EQ(animations[0],eng_character_active_animation(f.engine,actor));
    int32_t message = -2; read_value(&f,BLOCK_POUT(&f,control,1),&message,sizeof(message)); ASSERT_EQ(-1,message);
    fixture_end(&f);
}

static nmo_object_id_t controller_floor(engine_fixture_t *f, float height, bool down)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_3DOBJECT,"Floor");
    nmo_object_id_t mesh = fixture_create(f,NMO_CID_MESH,"Floor Mesh");
    nmo_3dentity_state_t *entity = fixture_state(f,id);
    nmo_mesh_state_t *state = fixture_state(f,mesh);
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    state->vertices = nmo_arena_alloc(arena,4*sizeof(*state->vertices),_Alignof(nmo_vertex_t));
    state->faces = nmo_arena_alloc(arena,2*sizeof(*state->faces),_Alignof(nmo_face_t));
    state->face_vertex_indices = nmo_arena_alloc(arena,6*sizeof(uint16_t),_Alignof(uint16_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL) return 0;
    memset(state->vertices,0,4*sizeof(*state->vertices)); memset(state->faces,0,2*sizeof(*state->faces));
    const float xz[4][2] = {{-5,-5},{-5,5},{5,5},{5,-5}};
    for (int i = 0; i < 4; i++) state->vertices[i].position = (nmo_vector_t){xz[i][0],0,xz[i][1]};
    const uint16_t up[] = {0,1,2,0,2,3}, reversed[] = {2,1,0,3,2,0};
    memcpy(state->face_vertex_indices,down ? reversed : up,sizeof(up));
    state->vertex_count = 4; state->face_count = 2;
    entity->current_mesh = nmo_ref_from_id(mesh); entity->world_matrix[13] = height;
    return id;
}

static void controller_floor_attribute(engine_fixture_t *f, nmo_object_id_t floor, bool box)
{
    nmo_manager_entry_create_options_t options = {.enabled=true,.attribute_type_guid=CKPGUID_FLOAT,
        .category="Test",.has_compatible_class_id=true,.compatible_class_id=NMO_CID_3DENTITY,.has_flags=true,.flags=0};
    uint32_t type;
    ASSERT_EQ(NMO_OK,nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx),"Floor",&options,&type));
    nmo_object_id_t parameter = fixture_create(f,NMO_CID_PARAMETEROUT,"Floor Attribute");
    nmo_parameter_state_t *state = fixture_state(f,parameter);
    state->has_state = true; state->mode = CKPARAM_MODE_BUFFER;
    state->type_guid = box ? CKPGUID_FLOOR : CKPGUID_FLOAT;
    ASSERT_EQ(NMO_OK,nmo_array_alloc(&state->buffer_data,1,box ? 20 : 4,NULL));
    memset(state->buffer_data.data,0,state->buffer_data.count);
    if (box) {
        nmo_object_id_t field = fixture_create(f,NMO_CID_PARAMETERLOCAL,"Floor Geometry");
        nmo_parameter_state_t *geometry = fixture_state(f,field);
        geometry->has_state = true; geometry->mode = CKPARAM_MODE_BUFFER; geometry->type_guid = CKPGUID_INT;
        ASSERT_EQ(NMO_OK,nmo_array_alloc(&geometry->buffer_data,1,4,NULL));
        int32_t value = CKFLOOR_BOX; memcpy(geometry->buffer_data.data,&value,4);
        memcpy(state->buffer_data.data,&field,4);
    }
    ASSERT_EQ(NMO_OK,nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(f,floor))->attributes,parameter,type,NULL));
}

TEST(engine_character, unlimited_floor_uses_reference_or_hierarchy_bottom_and_prefers_an_upper_floor)
{
    for (int reference = 0; reference < 2; reference++) for (int enabled = 0; enabled < 2; enabled++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body), foot = limb(&f,actor);
        nmo_character_state_t *state = fixture_state(&f,actor);
        state->base.world_matrix[13] = 5;
        ((nmo_3dentity_state_t *)fixture_state(&f,body))->world_matrix[13] = 3;
        ((nmo_3dentity_state_t *)fixture_state(&f,foot))->world_matrix[13] = 1;
        if (reference) state->floor_ref = nmo_ref_from_id(body);
        nmo_object_id_t upper = controller_floor(&f,6,false), lower = controller_floor(&f,0,false);
        f.owner_id = actor; character_script(&f);
        controller_floor_attribute(&f,upper,false); controller_floor_attribute(&f,lower,false);
        nmo_object_id_t block = controller_block(&f,NULL,0,NULL,0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,controller_local(&f,block,3),enabled ? "TRUE" : "FALSE"));
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,controller_local(&f,block,8),"0.25"));
        fixture_commit(&f,NULL);
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
        ASSERT_FLOAT_EQ(enabled ? (reference ? 8 : 10) : 5,matrix[13],1e-6f);
        eng_character_stop_automatic(f.engine,actor);
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
        ASSERT_FLOAT_EQ(enabled ? (reference ? 8 : 10) : 5,matrix[13],1e-6f);
        nmo_engine_destroy_object(f.engine,upper);
        const eng_vector_t position = {0,5,0}; eng_entity_set_position(f.engine,eng_entity_index(f.engine,actor),&position,-1,false);
        eng_character_stop_automatic(f.engine,actor);
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
        ASSERT_FLOAT_EQ(enabled ? (reference ? 2 : 4) : 5,matrix[13],1e-6f);
        ASSERT_FLOAT_EQ(5,state->base.world_matrix[13],0);
        fixture_end(&f);
    }
}

TEST(engine_character, unlimited_floor_respects_face_orientation_and_box_geometry)
{
    for (int box = 0; box < 2; box++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body), floor = controller_floor(&f,3,true);
        f.owner_id = actor; character_script(&f);
        controller_floor_attribute(&f,floor,box != 0);
        nmo_object_id_t block = controller_block(&f,NULL,0,NULL,0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,controller_local(&f,block,3),"TRUE"));
        fixture_commit(&f,NULL);
        if (box) {
            float height = 0; const eng_vector_t point = {0,0,0};
            ASSERT_TRUE(eng_entity_floor_height(f.engine,eng_entity_index(f.engine,floor),&point,true,false,&height));
            ASSERT_FLOAT_EQ(3,height,0);
        }
        ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine,actor,matrix));
        ASSERT_FLOAT_EQ(box ? 3 : 0,matrix[13],1e-6f);
        fixture_end(&f);
    }
}

static void controller_collection(engine_fixture_t *f, nmo_object_id_t block, const nmo_object_id_t *ids, size_t count)
{
    nmo_parameter_state_t *state = fixture_state(f,controller_local(f,block,0));
    state->mode = CKPARAM_MODE_SUBCHUNK;
    state->subchunk = nmo_chunk_create(nmo_session_get_arena(f->session));
    ASSERT_NOT_NULL(state->subchunk);
    state->subchunk->class_id = NMO_CID_OBJECTARRAY;
    ASSERT_EQ(NMO_OK,nmo_chunk_start_write(state->subchunk));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(state->subchunk,0x50));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_object_id_array(state->subchunk,ids,count));
    nmo_chunk_close(state->subchunk);
}

static void controller_format(engine_fixture_t *f, nmo_object_id_t animation, nmo_objectanimation_format_t format)
{
    const nmo_keyedanimation_state_t *keyed = fixture_state(f,animation);
    nmo_object_id_t channel = nmo_ref_runtime_id(&keyed->animation_ids[0]);
    nmo_objectanimation_state_t *part = fixture_state(f,channel);
    if (format == CKOBJANIM_FORMAT_SHARED) {
        nmo_object_id_t shared = fixture_create(f,NMO_CID_OBJECTANIMATION,"Shared Controller Keys");
        nmo_objectanimation_state_t *keys = fixture_state(f,shared);
        *keys = *part; keys->entity = nmo_ref_from_id(0);
        part->has_shared_anim = 1; part->shared_anim = nmo_ref_from_id(shared);
        part->controllers = NULL; part->controller_count = 0;
    } else if (format == CKOBJANIM_FORMAT_LEGACY) {
        nmo_object_t *object = nmo_object_repository_find_by_id(f->repo,channel);
        object->chunk = nmo_chunk_create(nmo_session_get_arena(f->session));
        ASSERT_NOT_NULL(object->chunk);
        object->chunk->class_id = NMO_CID_OBJECTANIMATION; object->chunk->data_version = 0;
    }
    part->format = format;
}

TEST(engine_character, loaded_unlimited_controllers_read_native_collections_and_copies_rebind_their_caches)
{
    const nmo_objectanimation_format_t formats[] = {CKOBJANIM_FORMAT_CONTROLLERS,
        CKOBJANIM_FORMAT_NEWDATA,CKOBJANIM_FORMAT_LEGACY,CKOBJANIM_FORMAT_SHARED};
    for (size_t i = 0; i < COUNT(formats); i++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t body, actor = character(&f,&body), bone = limb(&f,actor);
        nmo_object_id_t animations[] = {clip(&f,"Saved Idle",actor,body,10,0,0),
            clip(&f,"Saved Run",actor,body,10,0,10),clip(&f,"Saved Action",actor,bone,10,100,110)};
        for (size_t j = 0; j < COUNT(animations); j++) controller_format(&f,animations[j],formats[i]);
        uint8_t primary[2][152];
        controller_record(primary[0],1,"Run",2|128,500); controller_record(primary[1],0,"",2|128,500);
        nmo_object_id_t block = controller(&f,actor,animations,3,primary[0],2);
        controller_collection(&f,block,animations,3);
        uint8_t secondary[152]; controller_record(secondary,2,"Run",16|128|512|1024,1000);
        const float start = 1.25f; uint32_t word; memcpy(&word,&start,4); nmo_write_u32_le(secondary+4,word);
        nmo_write_u16_le(secondary+150,2);
        controller_bytes(&f,block,6,secondary,sizeof(secondary));
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,controller_local(&f,block,7),"1"));
        fixture_commit(&f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&f,actor);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&f,f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_unlimited.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(f.session,path,NULL,NULL));
        engine_fixture_t host;
        fixture_begin_document(&host); fixture_begin_edit(&host);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host,&options);
        ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
        int32_t file;
        ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"controller.cmo",true,&file));
        ASSERT_TRUE(file > 0);
        nmo_object_id_t loaded_actor = nmo_engine_object_named(host.engine,"Character",NMO_CID_CHARACTER);
        nmo_object_id_t idle = nmo_engine_object_named(host.engine,"Saved Idle",NMO_CID_KEYEDANIMATION);
        nmo_object_id_t run = nmo_engine_object_named(host.engine,"Saved Run",NMO_CID_KEYEDANIMATION);
        nmo_object_id_t action = nmo_engine_object_named(host.engine,"Saved Action",NMO_CID_KEYEDANIMATION);
        ASSERT_TRUE(loaded_actor != 0 && loaded_actor != actor && idle != 0 && run != 0 && action != 0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(idle,eng_character_active_animation(host.engine,loaded_actor));
        controller_send(&host,loaded_actor,"Run");
        ASSERT_EQ(run,eng_character_active_animation(host.engine,loaded_actor));
        ASSERT_EQ(1u,eng_character_secondary_count(host.engine,loaded_actor));
        ASSERT_FLOAT_EQ(1.25f,frame(host.engine,action),0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,2));
        ASSERT_FLOAT_EQ(3.25f,frame(host.engine,action),0);
        eng_animation_source_t source;
        ASSERT_TRUE(eng_animation_source(host.engine,eng_keyed_animation_root(host.engine,run),&source));
        ASSERT_EQ(formats[i],source.state->format);
        ASSERT_FLOAT_EQ(1000,eng_animation_state(host.engine,run)->frame_rate,0);
        nmo_object_id_t originals[] = {loaded_actor,nmo_engine_object_named(host.engine,"Body",NMO_CID_BODYPART),
            nmo_engine_object_named(host.engine,"Limb",NMO_CID_BODYPART),idle,run,action,
            nmo_engine_object_named(host.engine,"Character Controls",NMO_CID_BEHAVIOR),
            nmo_engine_object_named(host.engine,"Current Anim",NMO_CID_PARAMETEROUT)};
        nmo_object_id_t copies[COUNT(originals)] = {0};
        for (size_t j = 0; j < COUNT(originals); j++) ASSERT_TRUE(originals[j] != 0);
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,originals,COUNT(originals),true,copies));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        ASSERT_EQ(copies[3],eng_character_active_animation(host.engine,copies[0]));
        ASSERT_EQ(copies[0],nmo_engine_entity_parent(host.engine,copies[1]));
        nmo_object_id_t current = 0; read_value(&host,copies[7],&current,4);
        ASSERT_EQ(copies[3],current);
        ASSERT_EQ(idle,eng_character_active_animation(host.engine,loaded_actor));
        ASSERT_EQ(0xdeadbeefu,nmo_read_u32_le(primary[0]+48));
        fixture_end(&host); fixture_end(&f); remove(path);
    }
}

TEST(engine_character, unlimited_secondary_input_overrides_control_warp_entry_loop_count_and_last_frame)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t root, actor = character(&f,&root), bone = limb(&f,actor);
    nmo_object_id_t animations[] = {clip(&f,"Idle",actor,root,10,0,0),
        clip(&f,"Unused",actor,bone,10,100,110),clip(&f,"Selected",actor,bone,10,200,210)};
    uint8_t primary[152]; controller_record(primary,0,"",2|128,1000);
    nmo_object_id_t block = controller(&f,actor,animations,3,primary,1);
    uint8_t secondary[152]; controller_record(secondary,1,"Action",0,30);
    const uint32_t fields[] = {0,1,2,4,5,8,9,10,11};
    const nmo_guid_t types[] = {CKPGUID_ANIMATION,CKPGUID_BOOL,CKPGUID_FLOAT,CKPGUID_BOOL,
        CKPGUID_FLOAT,CKPGUID_INT,CKPGUID_BOOL,CKPGUID_FLOAT,CKPGUID_INT};
    uint16_t mask = 0;
    for (uint32_t i = 0; i < COUNT(fields); i++) {
        mask |= (uint16_t)(1u<<fields[i]); secondary[56+fields[i]] = (uint8_t)i;
        nmo_object_id_t pin = 0;
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(f.tx,block,NMO_SCRIPT_EDIT_PARAM_IN,types[i],"Secondary Override",&pin));
        if (i == 0) fixture_input_int(&f,pin,(int32_t)animations[2]);
        else if (i == 2 || i == 4 || i == 7) {
            float value = i == 2 ? 2 : i == 4 ? 500 : 1.5f; fixture_input_value(&f,pin,&value,4);
        } else fixture_input_int(&f,pin,i == 5 ? CKSECONDARYANIMATION_LOOPNTIMES : 1);
    }
    nmo_write_u16_le(secondary+54,mask);
    controller_bytes(&f,block,6,secondary,sizeof(secondary));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f.tx,controller_local(&f,block,7),"1"));
    fixture_commit(&f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    controller_send(&f,actor,"Action");
    ASSERT_EQ(1u,eng_character_secondary_count(f.engine,actor));
    ASSERT_EQ(animations[2],eng_character_secondary_animation(f.engine,actor,0));
    nmo_object_id_t warper = eng_character_secondary_warper(f.engine,actor,0);
    ASSERT_TRUE(warper != 0);
    ASSERT_FLOAT_EQ(2,eng_animation_length(eng_animation_playback_state(f.engine,warper)),0);
    ASSERT_FLOAT_EQ(500,eng_animation_playback_state(f.engine,animations[2])->frame_rate,0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,100));
    ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,2));
    ASSERT_EQ(1u,eng_character_secondary_count(f.engine,actor));
    ASSERT_FLOAT_EQ(10,frame(f.engine,animations[2]),0);
    ASSERT_FLOAT_EQ(0,frame(f.engine,animations[1]),0);
    ASSERT_FLOAT_EQ(1000,eng_animation_state(f.engine,animations[2])->frame_rate,0);
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
REGISTER_TEST(engine_character, global_playback_takes_over_automatic_characters_without_stopping_their_scripts);
REGISTER_TEST(engine_character, loaded_character_controls_remap_parameters_and_execute_all_animation_formats);
REGISTER_TEST(engine_character, runtime_playback_settings_drive_clocks_interrupts_and_copies_without_editing_sources);
REGISTER_TEST(engine_character, automatic_processing_can_resume_and_register_a_late_scriptless_character);
REGISTER_TEST(engine_character, unlimited_primary_uses_message_order_fallback_and_runtime_playback_settings);
REGISTER_TEST(engine_character, unlimited_animation_cache_follows_reordering_and_deletion);
REGISTER_TEST(engine_character, unlimited_off_stops_automatic_processing_and_invalid_counts_fail_before_activation);
REGISTER_TEST(engine_character, unlimited_secondary_messages_process_every_matching_record_and_warp_stops);
REGISTER_TEST(engine_character, unlimited_dynamic_inputs_override_current_records_and_old_versions_ignore_them);
REGISTER_TEST(engine_character, unlimited_primary_warp_modes_reach_the_transition_builder);
REGISTER_TEST(engine_character, unlimited_turning_uses_active_animation_permissions_and_linked_rate);
REGISTER_TEST(engine_character, goto_messages_drive_unlimited_root_motion_until_arrival_stops_loop_feedback);
REGISTER_TEST(engine_character, unlimited_floor_uses_reference_or_hierarchy_bottom_and_prefers_an_upper_floor);
REGISTER_TEST(engine_character, unlimited_floor_respects_face_orientation_and_box_geometry);
REGISTER_TEST(engine_character, loaded_unlimited_controllers_read_native_collections_and_copies_rebind_their_caches);
REGISTER_TEST(engine_character, unlimited_secondary_input_overrides_control_warp_entry_loop_count_and_last_frame);
TEST_MAIN_END()
