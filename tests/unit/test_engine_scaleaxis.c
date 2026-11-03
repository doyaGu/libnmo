#include "engine_fixture.h"
#include "../../src/engine/engine_transform.h"
#include "../fixtures/engine_scaleaxis_samples.h"
#include "object/builtin/nmo_animation_schemas.h"

#include <math.h>
#include <stdio.h>

#define ANIM_LINEAR_AXIS 0x2f200b08u
#define ANIM_TCB_AXIS 0x32595908u
#define ANIM_POSITION 0x637c4301u
#define ANIM_ROTATION 0x49ed4002u
#define ANIM_SCALE 0x654a3a04u
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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_scaleaxis, linear_and_tcb_axes_match_native_samples_before_object_rotation);
REGISTER_TEST(engine_scaleaxis, missing_or_ignored_channels_are_recovered_from_the_current_pose);
REGISTER_TEST(engine_scaleaxis, tiny_axis_phases_keep_finite_shear_with_large_principal_scales);
TEST_MAIN_END()
