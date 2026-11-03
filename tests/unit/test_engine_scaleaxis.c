#include "engine_fixture.h"
#include "../../src/engine/engine_transform.h"
#include "../fixtures/engine_scaleaxis_samples.h"
#include "document/nmo_document_load.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <math.h>
#include <stdio.h>

#define ANIM_LINEAR_AXIS 0x2f200b08u
#define ANIM_TCB_AXIS 0x32595908u
#define ANIM_POSITION 0x637c4301u
#define ANIM_ROTATION 0x49ed4002u
#define ANIM_SCALE 0x654a3a04u
#define GUID_PLAY_GLOBAL_ANIMATION NMO_GUID(0x1c9236e1, 0x42f40996)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Rotate each principal unit vector by q, form its weighted outer product,
 * then apply the object's X or Y quarter turn. Inputs may be non-unit chords. */
static void expected_pose(float out[16], const float q[4], const float scale[3],
                            const float position[3], bool y_rotation)
{
    double norm = sqrt((double)q[0]*q[0] + (double)q[1]*q[1] + (double)q[2]*q[2] + (double)q[3]*q[3]);
    double v[3] = {q[0]/norm, q[1]/norm, q[2]/norm}, w = q[3]/norm;
    double stretch[3][3] = {{0}};
    for (int axis = 0; axis < 3; axis++) {
        double basis[3] = {0}, first[3], second[3];
        basis[axis] = 1;
        for (int c = 0; c < 3; c++) first[c] = v[(c+1)%3]*basis[(c+2)%3] - v[(c+2)%3]*basis[(c+1)%3];
        for (int c = 0; c < 3; c++) second[c] = v[(c+1)%3]*first[(c+2)%3] - v[(c+2)%3]*first[(c+1)%3];
        for (int c = 0; c < 3; c++) basis[c] += 2*(w*first[c] + second[c]);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) stretch[r][c] += scale[axis]*basis[r]*basis[c];
    }
    memset(out, 0, 16*sizeof(float));
    for (int r = 0; r < 3; r++) {
        out[r*4] = (float)(y_rotation ? -stretch[r][2] : stretch[r][0]);
        out[r*4+1] = (float)(y_rotation ? stretch[r][1] : stretch[r][2]);
        out[r*4+2] = (float)(y_rotation ? stretch[r][0] : -stretch[r][1]);
    }
    memcpy(out + 12, position, 3*sizeof(float));
    out[15] = 1;
}

TEST(engine_scaleaxis, linear_and_tcb_axes_match_native_samples_before_object_rotation)
{
    size_t checked = 0;
    for (size_t c = 0; c < COUNT(AxisReferences); c++) {
        const axis_reference_t *input = &AxisReferences[c];
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
        static float position[] = {0, 5, 6, 7}, scale[] = {0, 2, 4, 6};
        static float rotation[] = {0, 0.70710678f, 0, 0, 0.70710678f};
        size_t key_bytes = input->count * (input->tcb ? 40 : 20);
        void *keys = nmo_arena_alloc(nmo_session_get_arena(f.session), key_bytes, _Alignof(float));
        ASSERT_NOT_NULL(keys);
        memcpy(keys, input->keys, key_bytes);
        nmo_objanim_controller_t controllers[] = {
            {input->tcb ? ANIM_TCB_AXIS : ANIM_LINEAR_AXIS, (uint32_t)input->count, (uint32_t)key_bytes, keys},
            {ANIM_POSITION, 1, sizeof(position), position},
            {ANIM_SCALE, 1, sizeof(scale), scale},
            {ANIM_ROTATION, 1, sizeof(rotation), rotation},
        };
        nmo_objectanimation_state_t *state = fixture_state(&f, animation);
        ASSERT_NOT_NULL(state);
        state->entity = nmo_ref_from_id(f.owner_id);
        state->has_length = 1;
        state->length = 20;
        state->controllers = controllers;
        state->controller_count = COUNT(controllers);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        for (size_t i = 0; i < input->sample_count; i++) {
            const axis_sample_t *sample = &input->samples[i];
            ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, sample->time / 20));
            float world[16], expected[16];
            ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
            expected_pose(expected, sample->value, scale+1, position+1, false);
            for (size_t j = 0; j < COUNT(world); j++) {
                if (fabsf(expected[j]-world[j]) > 0.0001f) fprintf(stderr, "%s at %g member %zu\n", input->name, sample->time, j);
                ASSERT_FLOAT_EQ(expected[j], world[j], 0.0001f);
            }
            checked++;
        }
        ASSERT_EQ(0, memcmp(keys, input->keys, key_bytes));
        state->flags |= CK_OBJECTANIMATION_IGNORESCALEROT;
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0.5f));
        const float identity[] = {0, 0, 0, 1};
        float expected[16], world[16];
        expected_pose(expected, identity, scale+1, position+1, false);
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
        for (size_t j = 0; j < COUNT(world); j++) ASSERT_FLOAT_EQ(expected[j], world[j], 0.0001f);
        fixture_end(&f);
    }
    ASSERT_EQ(69u, checked);
}

TEST(engine_scaleaxis, missing_or_ignored_channels_are_recovered_from_the_current_pose)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
    static float axis[] = {0, 0, 0, 0.258819045f, 0.965925826f}; /* Z 30 degrees */
    static float position[] = {0, 8, 9, 10}, scale[] = {0, 3, 5, 7};
    static float rotation[] = {0, 0, 0.70710678f, 0, 0.70710678f};
    nmo_objanim_controller_t all[] = {
        {ANIM_LINEAR_AXIS, 1, sizeof(axis), axis}, {ANIM_POSITION, 1, sizeof(position), position},
        {ANIM_ROTATION, 1, sizeof(rotation), rotation}, {ANIM_SCALE, 1, sizeof(scale), scale},
    }, selected[4];
    nmo_objectanimation_state_t *state = fixture_state(&f, animation);
    ASSERT_NOT_NULL(state);
    state->entity = nmo_ref_from_id(f.owner_id);
    state->has_length = 1;
    state->length = 8;
    state->controllers = selected;
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const float old_axis[] = {0, 0, 0.173648178f, 0.984807753f}; /* Z 20 degrees */
    float old_scale[] = {2, 4, 6};
    const float old_position[] = {5, 6, 7};
    float initial[16];
    for (int combination = 0; combination < 4; combination++) {
        bool ignored = (combination & 1) != 0;
        float sign = combination & 2 ? -1 : 1;
        for (int i = 0; i < 3; i++) old_scale[i] = sign * (float)(2 + 2*i);
        expected_pose(initial, old_axis, old_scale, old_position, false);
        for (uint32_t mask = 0; mask < 8; mask++) {
            state->controller_count = 1;
            selected[0] = all[0];
            state->flags = 0;
            for (uint32_t channel = 0; channel < 3; channel++) {
                if (ignored || (mask & (1u << channel))) selected[state->controller_count++] = all[channel+1];
                if (ignored && !(mask & (1u << channel))) state->flags |= 4u << channel;
            }
            ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, initial, false));
            ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0.5f));
            float world[16], expected[16];
            expected_pose(expected, axis+1, mask & 4 ? scale+1 : old_scale,
                            mask & 1 ? position+1 : old_position, (mask & 2) != 0);
            ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
            for (size_t i = 0; i < COUNT(world); i++) ASSERT_FLOAT_EQ(expected[i], world[i], 0.0001f);
            if (mask == 0) {
                for (int repeat = 0; repeat < 10; repeat++) ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0.5f));
                ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
                for (size_t i = 0; i < COUNT(world); i++) ASSERT_FLOAT_EQ(expected[i], world[i], 0.0002f);
            }
        }
    }
    fixture_end(&f);
}

TEST(engine_scaleaxis, tiny_axis_phases_keep_finite_shear_with_large_principal_scales)
{
    for (int tcb = 0; tcb < 2; tcb++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Animation");
        float linear[] = {-1.0e30f, 0, 0, 0.70710678f, 0.70710678f, 2, 0, 0, 0, 1};
        float spline[] = {-1.0e30f, 0, 0, 0.70710678f, 0.70710678f, 0, 0, 0, 0, 0,
                          2, 0, 0, 0, 1, 0, 0, 0, 0, 0};
        float scale[] = {0, 1.0e30f, 2.0e30f, 3}, rotation[] = {0, 0, 0, 0, 1};
        nmo_objanim_controller_t controllers[] = {
            {tcb ? ANIM_TCB_AXIS : ANIM_LINEAR_AXIS, 2, tcb ? sizeof(spline) : sizeof(linear), tcb ? spline : linear},
            {ANIM_SCALE, 1, sizeof(scale), scale}, {ANIM_ROTATION, 1, sizeof(rotation), rotation},
        };
        nmo_objectanimation_state_t *state = fixture_state(&f, animation);
        ASSERT_NOT_NULL(state);
        state->entity = nmo_ref_from_id(f.owner_id);
        state->has_length = 1;
        state->length = 2;
        state->controllers = controllers;
        state->controller_count = COUNT(controllers);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0.5f));
        float world[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
        ASSERT_FLOAT_EQ(-1.57079633f, world[1], 0.00002f);
        ASSERT_FLOAT_EQ(-1.57079633f, world[4], 0.00002f);
        ASSERT_FLOAT_EQ(scale[1], world[0], 1.0e24f);
        ASSERT_FLOAT_EQ(scale[2], world[5], 1.0e24f);
        ASSERT_FLOAT_EQ(3, world[10], 0.0001f);
        for (size_t i = 0; i < COUNT(world); i++) ASSERT_TRUE(isfinite(world[i]));
        fixture_end(&f);
    }
}

TEST(engine_scaleaxis, saved_playback_supports_controller_and_legacy_axis_layouts)
{
    const nmo_objectanimation_format_t formats[] = {CKOBJANIM_FORMAT_CONTROLLERS, CKOBJANIM_FORMAT_NEWDATA,
                                                    CKOBJANIM_FORMAT_LEGACY, CKOBJANIM_FORMAT_CONTROLLERS};
    for (size_t c = 0; c < COUNT(formats); c++) {
        bool tcb = c == 3, legacy = c == 1 || c == 2;
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t part = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Part");
        nmo_object_id_t global = fixture_create(&f, NMO_CID_KEYEDANIMATION, "Global");
        float linear[] = {0, 0, 0, 0, 1, 8, 0, 0, 0.70710678f, 0.70710678f};
        float old[] = {0, 123, 0, 0, 0, 1, 8, 456, 0, 0, 0.70710678f, 0.70710678f};
        float spline[] = {0, 0, 0, 0, 1, 0.5f, 0, 0, 0, 0,
                          8, 0, 0, 0.70710678f, 0.70710678f, -0.5f, 0, 0, 0, 0};
        float position[] = {0, 5, 6, 7}, scale[] = {0, 2, 4, 6};
        float rotation[] = {0, 0.70710678f, 0, 0, 0.70710678f};
        nmo_objanim_controller_t controllers[] = {
            {tcb ? ANIM_TCB_AXIS : ANIM_LINEAR_AXIS, 2,
             tcb ? sizeof(spline) : legacy ? sizeof(old) : sizeof(linear), tcb ? spline : legacy ? old : linear},
            {ANIM_POSITION, 1, sizeof(position), position}, {ANIM_SCALE, 1, sizeof(scale), scale},
            {ANIM_ROTATION, 1, sizeof(rotation), rotation},
        };
        nmo_objectanimation_state_t *state = fixture_state(&f, part);
        nmo_keyedanimation_state_t *keyed = fixture_state(&f, global);
        ASSERT_TRUE(state != NULL && keyed != NULL);
        state->entity = nmo_ref_from_id(f.owner_id);
        state->format = formats[c];
        state->has_length = 1;
        state->length = 8;
        state->controllers = controllers;
        state->controller_count = COUNT(controllers);
        if (formats[c] == CKOBJANIM_FORMAT_LEGACY) {
            nmo_object_t *object = nmo_object_repository_find_by_id(f.repo, part);
            ASSERT_NOT_NULL(object);
            object->chunk = nmo_chunk_create(nmo_session_get_arena(f.session));
            ASSERT_NOT_NULL(object->chunk);
            object->chunk->class_id = NMO_CID_OBJECTANIMATION;
            object->chunk->data_version = 0;
        }
        nmo_ref_t part_ref = nmo_ref_from_id(part);
        keyed->animation_count = 1;
        keyed->animation_ids = &part_ref;
        f.script_id = fixture_add_script(&f, "Script", &f.script_in);
        fixture_begin_edit(&f);
        nmo_object_id_t play = fixture_block(&f, GUID_PLAY_GLOBAL_ANIMATION, "Play");
        ASSERT_TRUE(play != 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, play, 0), 0);
        const nmo_object_id_t pins[] = {BLOCK_PIN(&f, play, 0), BLOCK_PIN(&f, play, 1)};
        char global_id[32];
        snprintf(global_id, sizeof(global_id), "%u", global);
        const char *values[] = {global_id, "8"};
        for (size_t i = 0; i < COUNT(pins); i++) {
            nmo_object_id_t source = 0;
            ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, pins[i], &source));
            ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(f.tx, source, values[i]));
        }
        fixture_commit(&f, NULL);
        const char *path = NMO_TEST_SCRATCH_FILE("engine_scaleaxis.cmo");
        nmo_status_t status = nmo_session_save_file(f.session, path, NULL, NULL);
        if (status != NMO_OK) fprintf(stderr, "Scale-axis format %zu save: %s\n", c, nmo_last_error_message());
        ASSERT_EQ(NMO_OK, status);
        nmo_document_t *document = NULL;
        nmo_workspace_t *workspace = NULL;
        nmo_engine_t *engine = NULL;
        ASSERT_EQ(NMO_OK, nmo_document_load_file(f.ctx, path, NULL, &document));
        ASSERT_EQ(NMO_OK, nmo_workspace_create(f.ctx, document, &workspace));
        ASSERT_EQ(NMO_OK, nmo_engine_create(workspace, NULL, &engine));
        ASSERT_EQ(NMO_OK, nmo_bb_virtools_register(engine));
        ASSERT_EQ(NMO_OK, nmo_engine_start(engine, NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 4));
        ASSERT_EQ(NMO_OK, nmo_engine_step(engine, 4));
        nmo_object_id_t owner = nmo_engine_object_named(engine, "Owner", NMO_CID_3DENTITY);
        ASSERT_TRUE(owner != 0);
        float world[16], expected[16];
        double half_angle = (tcb ? 33.75 : 45) * 3.14159265358979323846 / 360;
        const float axis[] = {0, 0, (float)sin(half_angle), (float)cos(half_angle)};
        expected_pose(expected, axis, scale+1, position+1, false);
        ASSERT_TRUE(nmo_engine_entity_world_matrix(engine, owner, world));
        for (size_t i = 0; i < COUNT(world); i++) ASSERT_FLOAT_EQ(expected[i], world[i], 0.0001f);
        nmo_object_id_t loaded_part = nmo_engine_object_named(engine, "Part", NMO_CID_OBJECTANIMATION);
        const nmo_objectanimation_state_t *saved = nmo_object_get_state(nmo_object_repository_find_by_id(
            nmo_document_get_repository(document), loaded_part));
        ASSERT_NOT_NULL(saved);
        ASSERT_EQ(formats[c], saved->format);
        bool found = false;
        for (uint32_t i = 0; i < saved->controller_count; i++) {
            if (saved->controllers[i].type == controllers[0].type) {
                ASSERT_EQ(controllers[0].data_size, saved->controllers[i].data_size);
                ASSERT_EQ(0, memcmp(controllers[0].data, saved->controllers[i].data, controllers[0].data_size));
                found = true;
            }
        }
        ASSERT_TRUE(found);
        nmo_engine_destroy(engine);
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        fixture_end(&f);
        remove(path);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_scaleaxis, linear_and_tcb_axes_match_native_samples_before_object_rotation);
REGISTER_TEST(engine_scaleaxis, missing_or_ignored_channels_are_recovered_from_the_current_pose);
REGISTER_TEST(engine_scaleaxis, tiny_axis_phases_keep_finite_shear_with_large_principal_scales);
REGISTER_TEST(engine_scaleaxis, saved_playback_supports_controller_and_legacy_axis_layouts);
TEST_MAIN_END()
