#include "engine_fixture.h"

#include "document/nmo_document_load.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <math.h>
#include <stdio.h>

#define GUID_PLAY_ANIMATION NMO_GUID(0x64221225, 0x769a143f)
#define GUID_PLAY_GLOBAL_ANIMATION NMO_GUID(0x1c9236e1, 0x42f40996)
#define ANIM_TCB_POSITION 0x347e4a01u
#define ANIM_TCB_SCALE 0x1b545904u
#define ANIM_LINEAR_ROTATION 0x49ed4002u
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct animation_sample {
    float time, value[3];
} animation_sample_t;

/* Synthetic samples captured from the original Win32 controllers, supplied by
 * Ballanced's TCBReferenceSamples.h. CK2_3D.dll SHA-256:
 * 5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590.
 * These values are independent of the evaluator under test. */
static const float VectorTwoKeysKeys[] = {
    2.0f, 1.0f, -2.0f, 3.0f, 0.25f, 0.7f, -0.6f, 0.0f, 0.0f,
    10.0f, 9.0f, 4.0f, -1.0f, -0.4f, -0.3f, 0.8f, 0.0f, 0.0f,
};
static const animation_sample_t VectorTwoKeysSamples[] = {
    {2.0f, {1.0f, -2.0f, 3.0f}},
    {3.0f, {1.7648437f, -1.42636716f, 2.61757803f}},
    {4.0f, {2.5687499f, -0.823437452f, 2.21562505f}},
    {6.0f, {4.3499999f, 0.512500048f, 1.32500005f}},
    {8.0f, {6.45625019f, 2.0921874f, 0.271874964f}},
    {9.0f, {7.66640615f, 2.99980474f, -0.333203137f}},
    {10.0f, {9.0f, 4.0f, -1.0f}},
};

static const float VectorZeroTCBKeys[] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    4.0f, 6.0f, -3.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    10.0f, 10.0f, 8.0f, -4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};
static const animation_sample_t VectorZeroTCBSamples[] = {
    {1.0f, {0.0f, 0.0f, 0.0f}},
    {1.75f, {1.8125f, -1.4140625f, 0.890625f}},
    {2.5f, {3.49999976f, -2.5625f, 1.62499988f}},
    {3.25f, {4.9375f, -3.1796875f, 2.046875f}},
    {4.0f, {6.0f, -3.0f, 2.0f}},
    {5.5f, {7.203125f, -1.3671875f, 1.140625f}},
    {7.0f, {7.87499952f, 0.9375f, -0.124999881f}},
    {8.5f, {8.609375f, 4.0234375f, -1.828125f}},
    {10.0f, {10.0f, 8.0f, -4.0f}},
};

static const float VectorUnevenTCBKeys[] = {
    0.0f, 1.0f, -2.0f, 3.0f, 0.25f, 0.5f, -0.25f, 0.4f, 0.2f,
    2.0f, -4.0f, 3.0f, 2.0f, -0.35f, -0.6f, 0.4f, 0.3f, 0.1f,
    9.0f, 6.0f, -1.0f, -2.0f, 0.5f, 0.7f, -0.5f, 0.2f, 0.4f,
    20.0f, -2.0f, 5.0f, 7.0f, -0.2f, -0.3f, 0.6f, 0.3f, 0.2f,
};
static const animation_sample_t VectorUnevenTCBSamples[] = {
    {0.0f, {1.0f, -2.0f, 3.0f}},
    {0.5f, {0.129599959f, -1.19007993f, 2.88639998f}},
    {1.0f, {-1.58847415f, 0.455194235f, 2.61558533f}},
    {1.5f, {-3.32629442f, 2.24372649f, 2.21730924f}},
    {2.0f, {-4.0f, 3.0f, 2.0f}},
    {3.75f, {-1.64102554f, 2.17658281f, 0.719600916f}},
    {5.5f, {2.68004107f, 0.270741105f, -1.2896806f}},
    {7.25f, {5.847332f, -1.1027782f, -2.38750315f}},
    {9.0f, {6.0f, -1.0f, -2.0f}},
    {11.75f, {6.01929235f, -0.94159472f, -1.87074947f}},
    {14.5f, {4.590693f, 0.154712558f, -0.212083817f}},
    {17.25f, {0.505031586f, 3.13002968f, 4.20007467f}},
    {20.0f, {-2.0f, 5.0f, 7.0f}},
};

typedef struct animation_case {
    const char *name;
    const float *keys;
    uint32_t key_count;
    const animation_sample_t *samples;
    size_t sample_count;
} animation_case_t;

TEST(engine_animation, tcb_vectors_match_native_samples_through_play_animation)
{
    const animation_case_t cases[] = {
        {"Two keys", VectorTwoKeysKeys, 2, VectorTwoKeysSamples, COUNT(VectorTwoKeysSamples)},
        {"Uneven times", VectorZeroTCBKeys, 3, VectorZeroTCBSamples, COUNT(VectorZeroTCBSamples)},
        {"TCB and ease", VectorUnevenTCBKeys, 4, VectorUnevenTCBSamples, COUNT(VectorUnevenTCBSamples)},
    };
    for (size_t kind = 0; kind < 2; kind++) {
        for (size_t c = 0; c < COUNT(cases); c++) {
            const animation_case_t *input = &cases[c];
            engine_fixture_t f;
            fixture_begin_document(&f);
            nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Animated Child");
            nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
            ASSERT_TRUE(child != 0 && animation != 0);
            nmo_3dentity_state_t *parent_state = fixture_state(&f, f.owner_id);
            nmo_3dentity_state_t *child_state = fixture_state(&f, child);
            parent_state->world_matrix[12] = 30;
            child_state->parent = nmo_ref_from_id(f.owner_id);
            /* A rotated, nonuniform pose; the unanimated channels must survive. */
            memset(child_state->world_matrix, 0, sizeof(child_state->world_matrix));
            child_state->world_matrix[1] = -2;
            child_state->world_matrix[4] = 3;
            child_state->world_matrix[10] = 4;
            child_state->world_matrix[12] = 37;
            child_state->world_matrix[13] = 8;
            child_state->world_matrix[14] = 9;
            child_state->world_matrix[15] = 1;
            static float rotation[] = {0, 0, 0, 0.70710678f, 0.70710678f};
            nmo_objanim_controller_t controllers[] = {
                {kind == 0 ? ANIM_TCB_POSITION : ANIM_TCB_SCALE, input->key_count,
                 input->key_count * 36u, (void *)input->keys},
                {ANIM_LINEAR_ROTATION, 1, sizeof(rotation), rotation},
            };
            nmo_objectanimation_state_t *state = fixture_state(&f, animation);
            state->entity = nmo_ref_from_id(child);
            state->has_length = 1;
            state->length = 20;
            state->controllers = controllers;
            state->controller_count = kind == 0 ? 1 : 2;
            f.script_id = fixture_add_script(&f, "Script", &f.script_in);
            fixture_begin_edit(&f);
            nmo_object_id_t play = fixture_block(&f, GUID_PLAY_ANIMATION, "Play");
            ASSERT_TRUE(play != 0);
            fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
            fixture_input_int(&f, BLOCK_PIN(&f, play, 0), (int32_t)animation);
            const float duration = 20;
            fixture_input_value(&f, BLOCK_PIN(&f, play, 1), &duration, sizeof(duration));
            fixture_input_int(&f, BLOCK_PIN(&f, play, 3), 0);
            fixture_commit(&f, NULL);
            ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
            ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 0));
            float previous = 0;
            for (size_t sample = 0; sample < input->sample_count; sample++) {
                const animation_sample_t *expected = &input->samples[sample];
                ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, expected->time - previous));
                previous = expected->time;
                float world[16];
                ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, child, world));
                const float actual[] = {kind == 0 ? world[12] - 30 : -world[1],
                                        kind == 0 ? world[13] : world[4],
                                        kind == 0 ? world[14] : world[10]};
                for (size_t component = 0; component < 3; component++) {
                    if (fabsf(actual[component] - expected->value[component]) > 0.00003f) {
                        fprintf(stderr, "%s channel %zu time %g component %zu\n",
                                input->name, kind, expected->time, component);
                    }
                    ASSERT_FLOAT_EQ(expected->value[component], actual[component], 0.00003f);
                }
                if (kind == 0) {
                    ASSERT_FLOAT_EQ(0, world[0], 0.0001f);
                    ASSERT_FLOAT_EQ(-2, world[1], 0.0001f);
                    ASSERT_FLOAT_EQ(3, world[4], 0.0001f);
                    ASSERT_FLOAT_EQ(4, world[10], 0.0001f);
                } else {
                    ASSERT_FLOAT_EQ(37, world[12], 0.0001f);
                    ASSERT_FLOAT_EQ(8, world[13], 0.0001f);
                    ASSERT_FLOAT_EQ(9, world[14], 0.0001f);
                }
            }
            fixture_end(&f);
        }
    }
}

TEST(engine_animation, single_tcb_scale_key_clamps_and_preserves_unanimated_channels)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_3dentity_state_t *entity = fixture_state(&f, f.owner_id);
    memset(entity->world_matrix, 0, sizeof(entity->world_matrix));
    entity->world_matrix[1] = -1;
    entity->world_matrix[4] = entity->world_matrix[10] = entity->world_matrix[15] = 1;
    entity->world_matrix[12] = 7;
    entity->world_matrix[13] = 8;
    entity->world_matrix[14] = 9;
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
    static float scale[] = {5, 2, 3, 4, 0.25f, -0.5f, 0.75f, 0.1f, 0.2f};
    nmo_objanim_controller_t controller = {ANIM_TCB_SCALE, 1, sizeof(scale), scale};
    nmo_objectanimation_state_t *state = fixture_state(&f, animation);
    ASSERT_NOT_NULL(state);
    state->entity = nmo_ref_from_id(f.owner_id);
    state->has_length = 1;
    state->length = 10;
    state->controller_count = 1;
    state->controllers = &controller;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t play = fixture_block(&f, GUID_PLAY_ANIMATION, "Play");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, play, 0), (int32_t)animation);
    const float duration = 10;
    fixture_input_value(&f, BLOCK_PIN(&f, play, 1), &duration, sizeof(duration));
    fixture_input_int(&f, BLOCK_PIN(&f, play, 3), 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* Before, exactly at and after the sole key, including repeated evaluations. */
    for (int frame = 0; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 5));
        float world[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
        const float expected[16] = {0, -2, 0, 0, 3, 0, 0, 0, 0, 0, 4, 0, 7, 8, 9, 1};
        for (size_t i = 0; i < COUNT(expected); i++) {
            ASSERT_FLOAT_EQ(expected[i], world[i], 0.0001f);
        }
    }
    fixture_end(&f);
}

TEST(engine_animation, malformed_tcb_keys_preserve_the_missing_channel)
{
    const uint32_t counts[] = {0, 1, 2, 0x40000000u};
    const uint32_t sizes[] = {0, 12, 36, 0};
    for (size_t c = 0; c < COUNT(counts); c++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_3dentity_state_t *entity = fixture_state(&f, f.owner_id);
        entity->world_matrix[12] = 42;
        nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
        static float key[] = {0, 1, 2, 3, 0, 0, 0, 0, 0};
        nmo_objanim_controller_t controller = {ANIM_TCB_POSITION, counts[c], sizes[c], key};
        nmo_objectanimation_state_t *state = fixture_state(&f, animation);
        ASSERT_NOT_NULL(state);
        state->entity = nmo_ref_from_id(f.owner_id);
        state->has_length = 1;
        state->length = 10;
        state->controller_count = 1;
        state->controllers = &controller;
        f.script_id = fixture_add_script(&f, "Script", &f.script_in);
        fixture_begin_edit(&f);
        nmo_object_id_t play = fixture_block(&f, GUID_PLAY_ANIMATION, "Play");
        fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
        fixture_input_int(&f, BLOCK_PIN(&f, play, 0), (int32_t)animation);
        fixture_commit(&f, NULL);
        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 5));
        float world[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
        ASSERT_FLOAT_EQ(42, world[12], 0.0001f);
        fixture_end(&f);
    }
}

TEST(engine_animation, saved_global_animation_plays_tcb_channels_after_loading)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t part = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Part");
    nmo_object_id_t global = fixture_create(&f, NMO_CID_KEYEDANIMATION, "Global");
    nmo_objectanimation_state_t *state = fixture_state(&f, part);
    nmo_keyedanimation_state_t *keyed = fixture_state(&f, global);
    ASSERT_TRUE(state != NULL && keyed != NULL);
    static float position[] = {0, 1, 2, 3, 0.5f, 0.3f, -0.2f, 0, 0,
                               10, 9, 4, 7, -0.5f, -0.1f, 0.2f, 0, 0};
    static float scale[] = {0, 1, 2, 3, 0.5f, 0.3f, -0.2f, 0, 0,
                            10, 9, 4, 7, -0.5f, -0.1f, 0.2f, 0, 0};
    nmo_objanim_controller_t controllers[] = {
        {ANIM_TCB_POSITION, 2, sizeof(position), position},
        {ANIM_TCB_SCALE, 2, sizeof(scale), scale},
    };
    state->entity = nmo_ref_from_id(f.owner_id);
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->has_length = 1;
    state->length = 10;
    state->controller_count = COUNT(controllers);
    state->controllers = controllers;
    nmo_ref_t part_ref = nmo_ref_from_id(part);
    keyed->animation_count = 1;
    keyed->animation_ids = &part_ref;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t play = fixture_block(&f, GUID_PLAY_GLOBAL_ANIMATION, "Play");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
    const nmo_object_id_t pins[] = {BLOCK_PIN(&f, play, 0), BLOCK_PIN(&f, play, 1),
                                   BLOCK_PIN(&f, play, 3)};
    char global_id[32];
    snprintf(global_id, sizeof(global_id), "%u", global);
    const char *values[] = {global_id, "10", "FALSE"};
    for (size_t i = 0; i < COUNT(pins); i++) {
        nmo_object_id_t source = 0;
        ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, pins[i], &source));
        ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(f.tx, source, values[i]));
    }
    fixture_commit(&f, NULL);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_animation_tcb.cmo");
    ASSERT_EQ(NMO_OK, nmo_session_save_file(f.session, path, NULL, NULL));
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_engine_t *engine = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(f.ctx, path, NULL, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f.ctx, document, &workspace));
    ASSERT_EQ(NMO_OK, nmo_engine_create(workspace, NULL, &engine));
    ASSERT_EQ(NMO_OK, nmo_bb_virtools_register(engine));
    nmo_object_id_t owner = nmo_engine_object_named(engine, "Owner", NMO_CID_3DENTITY);
    ASSERT_TRUE(owner != 0);
    ASSERT_EQ(NMO_OK, nmo_engine_start(engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 5));
    ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 5));
    /* Two-key Hermite at 1/2 with tensions +1/2, -1/2: weight 3/8. */
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(engine, owner, world));
    ASSERT_FLOAT_EQ(4, world[12], 0.0001f);
    ASSERT_FLOAT_EQ(2.75f, world[13], 0.0001f);
    ASSERT_FLOAT_EQ(4.5f, world[14], 0.0001f);
    ASSERT_FLOAT_EQ(4, world[0], 0.0001f);
    ASSERT_FLOAT_EQ(2.75f, world[5], 0.0001f);
    ASSERT_FLOAT_EQ(4.5f, world[10], 0.0001f);
    nmo_engine_destroy(engine);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    fixture_end(&f);
    remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation, tcb_vectors_match_native_samples_through_play_animation);
REGISTER_TEST(engine_animation, single_tcb_scale_key_clamps_and_preserves_unanimated_channels);
REGISTER_TEST(engine_animation, malformed_tcb_keys_preserve_the_missing_channel);
REGISTER_TEST(engine_animation, saved_global_animation_plays_tcb_channels_after_loading);
TEST_MAIN_END()
