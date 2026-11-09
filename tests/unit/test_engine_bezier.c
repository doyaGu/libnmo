#include "engine_fixture.h"

#include "../../src/engine/engine_transform.h"
#include "../fixtures/engine_bezier_samples.h"
#include "document/nmo_document_load.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <math.h>
#include <stdio.h>

#define GUID_PLAY_GLOBAL_ANIMATION NMO_GUID(0x1c9236e1, 0x42f40996)
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

TEST(engine_bezier, saved_global_animation_plays_variable_size_keys_without_changing_the_document)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t part = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Part");
    nmo_object_id_t global = fixture_create(&f, NMO_CID_KEYEDANIMATION, "Global");
    struct packed_key {
        float time, value[3];
        uint32_t flags;
    } positions[] = {{0, {0, 2, -4}, 0x00100010u}, {8, {8, 6, 4}, 0x00100010u}};
    struct packed_explicit_key {
        float time, value[3];
        uint32_t flags;
        float in[3], out[3];
    } scales[] = {{0, {1, 2, 3}, 0x00200020u, {0, 0, 0}, {1, 0, 0}},
                   {8, {9, 6, 7}, 0x00200020u, {-1, 0, 0}, {0, 0, 0}}};
    _Static_assert(sizeof(positions[0]) == 20 && sizeof(scales[0]) == 44, "Packed Bezier keys");
    nmo_objanim_controller_t controllers[] = {
        {ANIM_BEZIER_POSITION, 2, sizeof(positions), positions},
        {ANIM_BEZIER_SCALE, 2, sizeof(scales), scales},
    };
    nmo_objectanimation_state_t *state = fixture_state(&f, part);
    nmo_keyedanimation_state_t *keyed = fixture_state(&f, global);
    ASSERT_TRUE(state != NULL && keyed != NULL);
    state->entity = nmo_ref_from_id(f.owner_id);
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->has_length = 1;
    state->length = 8;
    state->controller_count = COUNT(controllers);
    state->controllers = controllers;
    nmo_ref_t part_ref = nmo_ref_from_id(part);
    keyed->animation_count = 1;
    keyed->animation_ids = &part_ref;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t play = fixture_block(&f, GUID_PLAY_GLOBAL_ANIMATION, "Play");
    ASSERT_TRUE(play != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
    const nmo_object_id_t pins[] = {BLOCK_PIN(&f, play, 0), BLOCK_PIN(&f, play, 1),
                                   BLOCK_PIN(&f, play, 3)};
    char global_id[32];
    snprintf(global_id, sizeof(global_id), "%u", global);
    const char *values[] = {global_id, "8", "FALSE"};
    for (size_t i = 0; i < COUNT(pins); i++) {
        nmo_object_id_t source = 0;
        ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, pins[i], &source));
        ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(f.tx, source, values[i]));
    }
    fixture_commit(&f, NULL);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_animation_bezier.cmo");
    ASSERT_EQ(NMO_OK, nmo_session_save_file(f.session, path, NULL, NULL));
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_engine_t *engine = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(f.ctx, path, NULL, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f.ctx, document, &workspace));
    ASSERT_EQ(NMO_OK, nmo_engine_create(workspace, NULL, &engine));
    ASSERT_EQ(NMO_OK, nmo_bb_virtools_register(engine));
    nmo_object_id_t owner = nmo_engine_object_named(engine, "Owner", NMO_CID_3DENTITY);
    nmo_object_id_t loaded_part = nmo_engine_object_named(engine, "Part", NMO_CID_OBJECTANIMATION);
    ASSERT_TRUE(owner != 0 && loaded_part != 0);
    ASSERT_EQ(NMO_OK, nmo_engine_start(engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 2));
    ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 2));
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(engine, owner, world));
    /* Slow position has smoothstep weight 5/32; the scale X has explicit
     * unit derivatives and is linear, while scale Y and Z have zero derivatives. */
    const float quarter[] = {1.25f, 2.625f, -2.75f, 3, 2.625f, 3.625f};
    const size_t members[] = {12, 13, 14, 0, 5, 10};
    for (size_t i = 0; i < COUNT(members); i++) {
        ASSERT_FLOAT_EQ(quarter[i], world[members[i]], 0.0001f);
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 6));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(engine, owner, world));
    const float end[] = {8, 6, 4, 9, 6, 7};
    for (size_t i = 0; i < COUNT(members); i++) {
        ASSERT_FLOAT_EQ(end[i], world[members[i]], 0.0001f);
    }
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    const nmo_objectanimation_state_t *saved = nmo_object_get_state(
        nmo_object_repository_find_by_id(repo, loaded_part));
    ASSERT_NOT_NULL(saved);
    ASSERT_EQ(2u, saved->controller_count);
    ASSERT_EQ(sizeof(positions), saved->controllers[0].data_size);
    ASSERT_EQ(sizeof(scales), saved->controllers[1].data_size);
    ASSERT_EQ(0, memcmp(positions, saved->controllers[0].data, sizeof(positions)));
    ASSERT_EQ(0, memcmp(scales, saved->controllers[1].data, sizeof(scales)));
    const nmo_3dentity_state_t *saved_entity = nmo_object_get_state(nmo_object_repository_find_by_id(repo, owner));
    ASSERT_NOT_NULL(saved_entity);
    ASSERT_FLOAT_EQ(0, saved_entity->world_matrix[12], 0.0001f);
    nmo_engine_destroy(engine);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    fixture_end(&f);
    remove(path);
}

TEST_MAIN_BEGIN()
/* 86 engine documents: near 40s under ASan in Debug on a busy runner */
REGISTER_TEST_WITH_TIMEOUT(engine_bezier, position_and_scale_match_native_samples, 120.0);
REGISTER_TEST(engine_bezier, invalid_packed_keys_preserve_the_pose_and_unaligned_keys_are_readable);
REGISTER_TEST(engine_bezier, saved_global_animation_plays_variable_size_keys_without_changing_the_document);
TEST_MAIN_END()
