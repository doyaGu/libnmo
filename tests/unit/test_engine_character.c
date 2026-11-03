#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_character, clocks_restore_saved_cursors_and_separate_cursor_updates_from_pose);
REGISTER_TEST(engine_character, linked_unlinked_paused_and_reverse_clocks_follow_native_frame_rules);
REGISTER_TEST(engine_character, clocks_handle_zero_length_and_recover_a_cancellable_overflow);
TEST_MAIN_END()
