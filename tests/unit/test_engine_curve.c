#include "engine_fixture.h"

#include "object/builtin/nmo_parameter_schemas.h"

#include <math.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_BEZIER_PROGRESSION NMO_GUID(0x6bb8699a, 0x29fb6a4b)

#define POINT_USE_TANGENTS 0x01u
#define POINT_LINEAR 0x02u

typedef struct test_point {
    uint32_t flags;
    float x, y;
    float in_x, in_y, out_x, out_y;
} test_point_t;

/*
 * The progression curve of the Bezier Progression sample: the middle points
 * keep their own tangents, the end points take t,c,b ones. The tangents
 * given for the end points are the ones CK2 saved for them.
 */
static const test_point_t sample_points[4] = {
    {0u, 0.0f, 0.0f, 0.0001f, 0.000413647f, 0.116788f, 0.483092f},
    {POINT_USE_TANGENTS, 0.116788f, 0.483092f, 0.116788f, 0.347826f, 0.284672f, 0.847826f},
    {POINT_USE_TANGENTS, 0.40146f, 0.797101f, 0.284672f, 0.0942028f, 0.59854f, 0.198068f},
    {0u, 1.0f, 1.0f, 0.59854f, 0.202899f, 0.001f, 0.000338989f},
};

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

/*
 * Save `points` into the 2D curve parameter `param` as CK2 does: the curve's
 * chunk (CK2dCurve::Dump) inside the parameter's, under identifier 0xE.
 * `zero_tcb_tangents` leaves out the tangents of the points CK2 works out.
 */
static bool save_curve(engine_fixture_t *f, nmo_object_id_t param, const test_point_t *points,
                       uint32_t count, bool zero_tcb_tangents)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, param);
    nmo_chunk_t *curve = nmo_chunk_create(arena);
    nmo_chunk_t *saved = nmo_chunk_create(arena);
    if (state == NULL || curve == NULL || saved == NULL) {
        return false;
    }
    nmo_chunk_start_write(curve);
    nmo_chunk_set_data_version(curve, 7);
    nmo_chunk_write_identifier(curve, 0x100u);
    nmo_chunk_write_int(curve, (int32_t)count);
    for (uint32_t i = 0; i < count; i++) {
        nmo_chunk_write_int(curve, 0);
    }
    nmo_chunk_write_float(curve, 0.0f); /* fitting coefficient */
    for (uint32_t i = 0; i < count; i++) {
        const test_point_t *p = &points[i];
        bool tangents = !zero_tcb_tangents || (p->flags & POINT_USE_TANGENTS) != 0;
        const float values[11] = {p->x, p->y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  tangents ? p->in_x : 0.0f, tangents ? p->in_y : 0.0f,
                                  tangents ? p->out_x : 0.0f, tangents ? p->out_y : 0.0f};
        nmo_chunk_write_dword(curve, p->flags | 0x10000000u);
        for (size_t v = 0; v < 11; v++) {
            nmo_chunk_write_float(curve, values[v]);
        }
    }
    nmo_chunk_close(curve);
    nmo_chunk_start_write(saved);
    nmo_chunk_write_identifier(saved, 0x0eu);
    nmo_chunk_write_sub_chunk(saved, curve);
    nmo_chunk_close(saved);

    nmo_array_dispose(&state->buffer_data);
    state->mode = CKPARAM_MODE_SUBCHUNK;
    state->subchunk = saved;
    state->has_state = true;
    return true;
}

/* Save `points` in the old curve format: a sub-chunk per point (CK2dCurvePoint::Read). */
static bool save_old_curve(engine_fixture_t *f, nmo_object_id_t param, const test_point_t *points,
                           uint32_t count)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, param);
    nmo_chunk_t *curve = nmo_chunk_create(arena);
    nmo_chunk_t *saved = nmo_chunk_create(arena);
    if (state == NULL || curve == NULL || saved == NULL) {
        return false;
    }
    nmo_chunk_start_write(curve);
    nmo_chunk_set_data_version(curve, 0);
    nmo_chunk_write_identifier(curve, 0x3f800u);
    nmo_chunk_write_int(curve, (int32_t)count);
    for (uint32_t i = 0; i < count; i++) {
        const test_point_t *p = &points[i];
        nmo_chunk_t *point = nmo_chunk_create(arena);
        if (point == NULL) {
            return false;
        }
        nmo_chunk_start_write(point);
        nmo_chunk_write_identifier(point, 0x4000u);
        nmo_chunk_write_int(point, 0);
        nmo_chunk_write_int(point, (p->flags & POINT_USE_TANGENTS) != 0);
        nmo_chunk_write_int(point, (p->flags & POINT_LINEAR) != 0);
        nmo_chunk_write_float(point, p->x);
        nmo_chunk_write_float(point, p->y);
        nmo_chunk_write_identifier(point, 0x800u);
        for (int v = 0; v < 3; v++) {
            nmo_chunk_write_float(point, 0.0f);
        }
        nmo_chunk_close(point);
        nmo_chunk_write_int(curve, 0);
        nmo_chunk_write_sub_chunk(curve, point);
    }
    nmo_chunk_close(curve);
    nmo_chunk_start_write(saved);
    nmo_chunk_write_identifier(saved, 0x0eu);
    nmo_chunk_write_sub_chunk(saved, curve);
    nmo_chunk_close(saved);

    nmo_array_dispose(&state->buffer_data);
    state->mode = CKPARAM_MODE_SUBCHUNK;
    state->subchunk = saved;
    state->has_state = true;
    return true;
}

/* A Bezier Progression from A to B over `duration` ms, looping each frame. */
static nmo_object_id_t bezier_block(engine_fixture_t *f, float duration, float a, float b,
                                    nmo_object_id_t *out_curve, nmo_object_id_t *out_done)
{
    nmo_object_id_t block = fixture_block(f, GUID_BEZIER_PROGRESSION, "Bezier Progression");
    nmo_object_id_t done = fixture_block(f, GUID_NOP, "Done");
    if (block == 0 || done == 0 ||
        nmo_script_edit_ensure_input_parameter_source(f->tx, BLOCK_PIN(f, block, 3), out_curve) !=
            NMO_OK) {
        return 0;
    }
    fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    fixture_link(f, BLOCK_OUT(f, block, 1), BLOCK_IN(f, block, 1), 1);
    fixture_link(f, BLOCK_OUT(f, block, 0), BLOCK_IN(f, done, 0), 0);
    input_float(f, BLOCK_PIN(f, block, 0), duration);
    input_float(f, BLOCK_PIN(f, block, 1), a);
    input_float(f, BLOCK_PIN(f, block, 2), b);
    *out_done = done;
    return block;
}

static float curve_y(engine_fixture_t *f, nmo_object_id_t curve, float x)
{
    const void *data = NULL;
    size_t size = 0;
    if (nmo_engine_param_value(f->engine, curve, &data, &size, NULL) != NMO_OK) {
        return -1.0f;
    }
    return nmo_engine_curve_y(data, size, x);
}

TEST(engine_curve, bezier_progression_follows_its_curve)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t curve = 0, done = 0;
    nmo_object_id_t block = bezier_block(&f, 100.0f, 10.0f, 20.0f, &curve, &done);
    ASSERT_TRUE(block != 0);
    ASSERT_TRUE(save_curve(&f, curve, sample_points, 4, true));
    fixture_commit(&f, NULL);

    /* The curve passes through its points */
    for (int i = 0; i < 4; i++) {
        ASSERT_FLOAT_EQ(sample_points[i].y, curve_y(&f, curve, sample_points[i].x), 0.0001f);
    }
    ASSERT_FLOAT_EQ(0.0f, curve_y(&f, curve, -1.0f), 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, curve_y(&f, curve, 2.0f), 0.0001f);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    float last = 10.0f;
    for (int frame = 1; frame <= 5; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        float elapsed = 0.0f, value = 0.0f, delta = 0.0f, progression = 0.0f;
        read_value(&f, BLOCK_POUT(&f, block, 0), &elapsed, sizeof(elapsed));
        read_value(&f, BLOCK_POUT(&f, block, 1), &value, sizeof(value));
        read_value(&f, BLOCK_POUT(&f, block, 2), &delta, sizeof(delta));
        read_value(&f, BLOCK_POUT(&f, block, 3), &progression, sizeof(progression));
        float expected_progression = frame * 25.0f / 100.0f;
        if (expected_progression > 1.0f) {
            expected_progression = 1.0f;
        }
        ASSERT_FLOAT_EQ(frame * 25.0f, elapsed, 0.001f);
        ASSERT_FLOAT_EQ(expected_progression, progression, 0.0001f);
        ASSERT_FLOAT_EQ(10.0f + 10.0f * curve_y(&f, curve, progression), value, 0.0001f);
        ASSERT_FLOAT_EQ(value - last, delta, 0.0001f);
        last = value;
        /* Out once the elapsed time is past the duration: 125 ms */
        ASSERT_EQ(frame == 5 ? 1u : 0u, runs(&f, done));
    }
    ASSERT_FLOAT_EQ(20.0f, last, 0.0001f);
    fixture_end(&f);
}

TEST(engine_curve, tcb_tangents_are_the_ones_ck2_saves)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t worked_out = 0, saved = 0, done = 0;
    ASSERT_TRUE(bezier_block(&f, 100.0f, 0.0f, 1.0f, &worked_out, &done) != 0);
    ASSERT_TRUE(bezier_block(&f, 100.0f, 0.0f, 1.0f, &saved, &done) != 0);
    /* The same curve, with the end points' tangents given as CK2 saved them */
    test_point_t given[4];
    memcpy(given, sample_points, sizeof(given));
    given[0].flags |= POINT_USE_TANGENTS;
    given[3].flags |= POINT_USE_TANGENTS;
    ASSERT_TRUE(save_curve(&f, worked_out, sample_points, 4, true));
    ASSERT_TRUE(save_curve(&f, saved, given, 4, false));
    fixture_commit(&f, NULL);

    for (int i = 0; i <= 20; i++) {
        float x = i / 20.0f;
        ASSERT_FLOAT_EQ(curve_y(&f, saved, x), curve_y(&f, worked_out, x), 0.0001f);
    }
    /* Between the points the curve bends: past the straight line to (0.117, 0.483) */
    ASSERT_TRUE(curve_y(&f, worked_out, 0.05f) > 0.05f / 0.116788f * 0.483092f);
    fixture_end(&f);
}

TEST(engine_curve, curve_defaults_linear_segments_and_old_curves)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t unsaved = 0, linear = 0, old = 0, done = 0;
    ASSERT_TRUE(bezier_block(&f, 100.0f, 0.0f, 1.0f, &unsaved, &done) != 0);
    ASSERT_TRUE(bezier_block(&f, 100.0f, 0.0f, 1.0f, &linear, &done) != 0);
    ASSERT_TRUE(bezier_block(&f, 100.0f, 0.0f, 1.0f, &old, &done) != 0);
    /* Out of order: the curve sorts its points by x */
    const test_point_t points[3] = {
        {POINT_LINEAR, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {POINT_LINEAR, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {POINT_LINEAR, 0.25f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    };
    ASSERT_TRUE(save_curve(&f, linear, points, 3, true));
    ASSERT_TRUE(save_old_curve(&f, old, points, 3));
    fixture_commit(&f, NULL);

    /* No saved curve: CK2dCurve's (0,0)-(1,1), a straight line with t,c,b tangents */
    ASSERT_FLOAT_EQ(0.3f, curve_y(&f, unsaved, 0.3f), 0.0001f);
    ASSERT_FLOAT_EQ(0.75f, curve_y(&f, unsaved, 0.75f), 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, curve_y(&f, linear, 0.125f), 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, curve_y(&f, linear, 0.25f), 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, curve_y(&f, linear, 0.625f), 0.0001f);
    /* The old format reads the same curve */
    for (int i = 0; i <= 8; i++) {
        ASSERT_FLOAT_EQ(curve_y(&f, linear, i / 8.0f), curve_y(&f, old, i / 8.0f), 0.0001f);
    }
    /* Bytes that hold no curve */
    const float not_a_curve[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ASSERT_FLOAT_EQ(0.0f, nmo_engine_curve_y(not_a_curve, sizeof(not_a_curve), 0.5f), 0.0f);
    ASSERT_FLOAT_EQ(0.0f, nmo_engine_curve_y(NULL, 0, 0.5f), 0.0f);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_curve, bezier_progression_follows_its_curve);
REGISTER_TEST(engine_curve, tcb_tangents_are_the_ones_ck2_saves);
REGISTER_TEST(engine_curve, curve_defaults_linear_segments_and_old_curves);
TEST_MAIN_END()
