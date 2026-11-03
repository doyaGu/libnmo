#include "engine_fixture.h"

#include "../../src/engine/engine_transform.h"
#include "../fixtures/engine_bezier_samples.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <math.h>
#include <stdio.h>

#define ANIM_BEZIER_POSITION 0x921ab801u
#define ANIM_BEZIER_SCALE 0x18ab4404u
#define ANIM_LINEAR_ROTATION 0x49ed4002u
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

TEST(engine_bezier, position_and_scale_match_native_samples)
{
    ASSERT_EQ(43u, COUNT(BezierReferences));
    size_t checked = 0;
    for (size_t kind = 0; kind < 2; kind++) {
        for (size_t c = 0; c < COUNT(BezierReferences); c++) {
            const bezier_reference_t *input = &BezierReferences[c];
            engine_fixture_t f;
            fixture_begin_document(&f);
            nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Animated Child");
            nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
            ASSERT_TRUE(child != 0 && animation != 0);
            nmo_3dentity_state_t *parent = fixture_state(&f, f.owner_id);
            nmo_3dentity_state_t *entity = fixture_state(&f, child);
            parent->world_matrix[12] = 30;
            entity->parent = nmo_ref_from_id(f.owner_id);
            memset(entity->world_matrix, 0, sizeof(entity->world_matrix));
            entity->world_matrix[1] = -2;
            entity->world_matrix[4] = 3;
            entity->world_matrix[10] = 4;
            entity->world_matrix[12] = 37;
            entity->world_matrix[13] = 8;
            entity->world_matrix[14] = 9;
            entity->world_matrix[15] = 1;
            size_t key_bytes = (input->word_count - 1) * 4;
            void *keys = nmo_arena_alloc(nmo_session_get_arena(f.session), key_bytes, _Alignof(float));
            ASSERT_NOT_NULL(keys);
            memcpy(keys, input->words + 1, key_bytes);
            static float rotation[] = {0, 0, 0, 0.70710678f, 0.70710678f};
            nmo_objanim_controller_t controllers[] = {
                {kind == 0 ? ANIM_BEZIER_POSITION : ANIM_BEZIER_SCALE, input->words[0],
                 (uint32_t)key_bytes, keys},
                {ANIM_LINEAR_ROTATION, 1, sizeof(rotation), rotation},
            };
            nmo_objectanimation_state_t *state = fixture_state(&f, animation);
            ASSERT_NOT_NULL(state);
            state->entity = nmo_ref_from_id(child);
            state->format = CKOBJANIM_FORMAT_CONTROLLERS;
            state->has_length = 1;
            state->length = 20;
            state->controllers = controllers;
            state->controller_count = kind == 0 ? 1 : 2;
            fixture_begin_edit(&f);
            fixture_commit(&f, NULL);
            ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
            for (size_t s = 0; s < input->sample_count; s++) {
                const bezier_sample_t *expected = &input->samples[s];
                /* Controller time is independent of the BB's progression curve. */
                ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, expected->time / 20));
                float world[16];
                ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, child, world));
                const float actual[] = {kind == 0 ? world[12] - 30 : -world[1],
                                        kind == 0 ? world[13] : world[4],
                                        kind == 0 ? world[14] : world[10]};
                for (size_t component = 0; component < 3; component++) {
                    if (fabsf(actual[component] - expected->value[component]) > 0.00005f) {
                        fprintf(stderr, "%s channel %zu time %g component %zu\n",
                                input->name, kind, expected->time, component);
                    }
                    ASSERT_FLOAT_EQ(expected->value[component], actual[component], 0.00005f);
                }
                if (kind == 0) {
                    ASSERT_FLOAT_EQ(-2, world[1], 0.0001f);
                    ASSERT_FLOAT_EQ(3, world[4], 0.0001f);
                    ASSERT_FLOAT_EQ(4, world[10], 0.0001f);
                } else {
                    ASSERT_FLOAT_EQ(37, world[12], 0.0001f);
                    ASSERT_FLOAT_EQ(8, world[13], 0.0001f);
                    ASSERT_FLOAT_EQ(9, world[14], 0.0001f);
                }
                checked++;
            }
            ASSERT_EQ(0, memcmp(input->words + 1, controllers[0].data, controllers[0].data_size));
            fixture_end(&f);
        }
    }
    ASSERT_EQ(1222u, checked);
}

TEST(engine_bezier, invalid_packed_keys_preserve_the_pose_and_unaligned_keys_are_readable)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
    size_t size = sizeof(TwoMixedWords) - sizeof(uint32_t);
    uint8_t *storage = nmo_arena_alloc(nmo_session_get_arena(f.session), size + 2, 4);
    ASSERT_NOT_NULL(storage);
    uint8_t *keys = storage + 1; /* The packed API permits unaligned buffers. */
    memcpy(keys, TwoMixedWords + 1, size);
    nmo_objanim_controller_t controller = {ANIM_BEZIER_POSITION, 2, (uint32_t)size, keys};
    nmo_objectanimation_state_t *state = fixture_state(&f, animation);
    ASSERT_NOT_NULL(state);
    state->entity = nmo_ref_from_id(f.owner_id);
    state->has_length = 1;
    state->length = 20;
    state->controller_count = 1;
    state->controllers = &controller;
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    float initial[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, initial));
    initial[0] = 2;
    initial[5] = 3;
    initial[10] = 4;
    initial[12] = 42;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, initial, false));
    /* Every truncated prefix, a trailing byte and overflow-sized key counts. */
    for (size_t cut = 0; cut < size + 5; cut++) {
        controller.data_size = (uint32_t)(cut < size ? cut : size + 1);
        controller.key_count = cut == size + 1 ? 0 : cut == size + 2 ? UINT32_MAX : 2;
        memcpy(keys, TwoMixedWords + 1, size);
        if (cut == size + 3 || cut == size + 4) {
            controller.data_size = (uint32_t)size;
            float time = cut == size + 3 ? NAN : -1;
            memcpy(keys + (cut == size + 3 ? 0 : 32), &time, sizeof(time));
        }
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0.5f));
        float world[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
        ASSERT_EQ(0, memcmp(initial, world, sizeof(world)));
    }
    memcpy(keys, TwoMixedWords + 1, size);
    controller.data_size = (uint32_t)size;
    controller.key_count = 2;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0.5f));
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
    ASSERT_FLOAT_EQ(-0.5f, world[12], 0.00005f);
    ASSERT_FLOAT_EQ(1.5f, world[13], 0.00005f);
    ASSERT_FLOAT_EQ(5, world[14], 0.00005f);
    ASSERT_FLOAT_EQ(2, world[0], 0.0001f);
    ASSERT_FLOAT_EQ(3, world[5], 0.0001f);
    ASSERT_FLOAT_EQ(4, world[10], 0.0001f);
    ASSERT_EQ(0, memcmp(keys, TwoMixedWords + 1, size));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_bezier, position_and_scale_match_native_samples);
REGISTER_TEST(engine_bezier, invalid_packed_keys_preserve_the_pose_and_unaligned_keys_are_readable);
TEST_MAIN_END()
