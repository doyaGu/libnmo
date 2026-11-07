/**
 * @file loops.c
 * @brief Ports of the Logics/Loops building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). The comments name the CK function a port follows when a block has
 * several versions.
 */

#include "../virtools.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <string.h>

/* ---- Logics/Loops ---- */

/* Delayer (TimerMini) */
static int bb_delayer(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_out_float(b, 0, 0.0f);
    }
    float wait = get_in_float(b, 0, 0.0f);
    float elapsed = get_out_float(b, 0) + nmo_engine_block_delta_time(b);
    set_out_float(b, 0, elapsed);
    if (elapsed >= wait) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Timer (TimeTimer); TimeTimerOld counts integer milliseconds. */
static int timer_step(blk_t *b, bool integer)
{
    float elapsed = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    } else {
        IN_OFF(b, 1);
        elapsed = integer ? (float)get_out_int(b, 0) : get_out_float(b, 0);
    }
    float delta = nmo_engine_block_delta_time(b);
    float duration;
    if (integer) {
        int32_t d = (int32_t)delta;
        int32_t e = (int32_t)elapsed + d;
        set_out_int(b, 0, e);
        set_out_int(b, 1, d);
        elapsed = (float)e;
        duration = (float)get_in_int(b, 0, 0);
    } else {
        elapsed += delta;
        set_out_float(b, 0, elapsed);
        set_out_float(b, 1, delta);
        duration = get_in_float(b, 0, 0.0f);
    }
    OUT_ON(b, elapsed >= duration ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_timer(blk_t *b) { return timer_step(b, false); }

static int bb_timer_old(blk_t *b) { return timer_step(b, true); }

static int cb_timer(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        bool old = nmo_engine_block_version(b) < 0x00010005u &&
                   nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_INT);
        nmo_engine_block_set_function(b, old ? bb_timer_old : bb_timer);
    }
    return NMO_ENGINE_BR_OK;
}

/* CounterV2 (version 2.0) */
static int bb_counter(blk_t *b)
{
    int32_t max_count = get_in_int(b, 0, 10);
    int32_t start = get_in_int(b, 1, 1);
    int32_t step = get_in_int(b, 2, 1);
    if (step == 0) {
        step = 1;
    }
    int32_t current = get_out_int(b, 0);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        current = start - step;
    } else {
        IN_OFF(b, 1);
    }
    current += step;
    set_out_int(b, 0, current);
    OUT_ON(b, (current - start) / step >= max_count ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* Counter (older versions) */
static int bb_counter_v1(blk_t *b)
{
    int32_t step = get_in_int(b, 2, 1);
    int32_t start = get_in_int(b, 1, 1);
    int32_t current;
    if (IN_ACTIVE(b, 0)) {
        current = start;
        IN_OFF(b, 0);
    } else {
        IN_OFF(b, 1);
        current = get_out_int(b, 0) + step;
    }
    int32_t max_count = get_in_int(b, 0, 10);
    bool done = step > 0 ? current > max_count + (start - 1)
                         : (start - current) >= max_count * -step;
    if (done) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 1);
    set_out_int(b, 0, current);
    return NMO_ENGINE_BR_OK;
}

static int cb_counter(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(
            b, nmo_engine_block_version(b) == 0x00020000u ? bb_counter : bb_counter_v1);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Linear Progression. The "Time Based" setting picks the function: time in
 * float milliseconds (LinearProgressionTimeBased), integer milliseconds for
 * old blocks with an integer duration, or frames (LinearProgression).
 */
typedef enum progression_mode { PROGRESS_FRAMES, PROGRESS_TIME, PROGRESS_TIME_INT } progression_mode_t;

static int linear_progression(blk_t *b, progression_mode_t mode)
{
    float a = get_in_float(b, 1, 0.0f), c = get_in_float(b, 2, 1.0f);
    float current = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_out_float(b, 1, a);
    } else {
        IN_OFF(b, 1);
        current = mode == PROGRESS_TIME ? get_out_float(b, 0) : (float)get_out_int(b, 0);
    }
    float max_count;
    if (mode == PROGRESS_TIME) {
        current += nmo_engine_block_delta_time(b);
        set_out_float(b, 0, current);
        max_count = get_in_float(b, 0, 0.0f);
    } else {
        int32_t step = mode == PROGRESS_FRAMES ? 1 : (int32_t)nmo_engine_block_delta_time(b);
        int32_t value = (int32_t)current + step;
        set_out_int(b, 0, value);
        current = (float)value;
        max_count = (float)get_in_int(b, 0, 0);
    }
    float pos = c;
    float progression = current / max_count;
    if (current > max_count) {
        OUT_ON(b, 0);
    } else {
        pos = a + (c - a) * progression;
        OUT_ON(b, 1);
    }
    float delta = pos - get_out_float(b, 1);
    set_out_float(b, 1, pos);
    set_out_float(b, 2, delta);
    set_out_float(b, 3, progression);
    return NMO_ENGINE_BR_OK;
}

static int bb_linear_progression(blk_t *b) { return linear_progression(b, PROGRESS_TIME); }

static int bb_linear_progression_int(blk_t *b) { return linear_progression(b, PROGRESS_TIME_INT); }

static int bb_linear_progression_frames(blk_t *b) { return linear_progression(b, PROGRESS_FRAMES); }

static int cb_linear_progression(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) {
        return NMO_ENGINE_BR_OK;
    }
    if (get_local_int(b, 0, 0) == 0) {
        nmo_engine_block_set_function(b, bb_linear_progression_frames);
    } else if (nmo_engine_block_version(b) < 0x00010005u &&
               !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_TIME)) {
        nmo_engine_block_set_function(b, bb_linear_progression_int);
    } else {
        nmo_engine_block_set_function(b, bb_linear_progression);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Bezier Progression: as Linear Progression, with the value following the
 * Progression Curve from A to B (BezierProgressionTimeBased, its old integer
 * time version, and BezierProgression in frames). Nothing happens without a
 * curve.
 */
static int bezier_progression(blk_t *b, progression_mode_t mode)
{
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 3, &curve_size);
    if (curve == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float a = get_in_float(b, 1, 0.0f), c = get_in_float(b, 2, 1.0f);
    float elapsed = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        if (mode == PROGRESS_TIME) {
            set_out_float(b, 0, 0.0f);
        } else {
            set_out_int(b, 0, 0);
        }
        set_out_float(b, 1, a + (c - a) * nmo_engine_curve_y(curve, curve_size, 0.0f));
        set_out_float(b, 2, 0.0f);
        if (mode != PROGRESS_FRAMES) {
            set_out_float(b, 3, 0.0f);
        }
    } else {
        IN_OFF(b, 1);
        elapsed = mode == PROGRESS_TIME ? get_out_float(b, 0) : (float)get_out_int(b, 0);
    }
    float duration;
    if (mode == PROGRESS_TIME) {
        elapsed += nmo_engine_block_delta_time(b);
        set_out_float(b, 0, elapsed);
        duration = get_in_float(b, 0, 0.0f);
    } else {
        int32_t step = mode == PROGRESS_FRAMES ? 1 : (int32_t)nmo_engine_block_delta_time(b);
        int32_t value = (int32_t)elapsed + step;
        set_out_int(b, 0, value);
        elapsed = (float)value;
        duration = (float)get_in_int(b, 0, 0);
    }
    float progression = elapsed / duration;
    if (progression > 1.0f) {
        progression = 1.0f;
    }
    float value = a + (c - a) * nmo_engine_curve_y(curve, curve_size, progression);
    float delta = value - get_out_float(b, 1);
    set_out_float(b, 1, value);
    set_out_float(b, 2, delta);
    set_out_float(b, 3, progression);
    OUT_ON(b, elapsed > duration ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_bezier_progression(blk_t *b) { return bezier_progression(b, PROGRESS_TIME); }

static int bb_bezier_progression_int(blk_t *b) { return bezier_progression(b, PROGRESS_TIME_INT); }

static int bb_bezier_progression_frames(blk_t *b) { return bezier_progression(b, PROGRESS_FRAMES); }

static int cb_bezier_progression(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) {
        return NMO_ENGINE_BR_OK;
    }
    if (get_local_int(b, 0, 0) == 0) {
        nmo_engine_block_set_function(b, bb_bezier_progression_frames);
    } else if (nmo_engine_block_version(b) < 0x00010005u &&
               !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_TIME)) {
        nmo_engine_block_set_function(b, bb_bezier_progression_int);
    } else {
        nmo_engine_block_set_function(b, bb_bezier_progression);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Collection Iterator: each object of a collection (its IDs), Loop Out with
 * the object and its index, then Out. The index is the Index output's, or
 * the local's for a block without that output.
 */
static int bb_collection_iterator(blk_t *b)
{
    size_t size = 0;
    const uint8_t *ids = (const uint8_t *)nmo_engine_block_input_data(b, 0, &size);
    if (ids == NULL) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool index_output = nmo_engine_block_output_parameter_count(b) > 1;
    int32_t index = 0;
    if (index_output) {
        nmo_engine_block_get_output_value(b, 1, &index, sizeof(index));
    } else {
        index = get_local_int(b, 0, 0);
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
    } else {
        IN_OFF(b, 1);
        index++;
    }
    if (index < 0 || (size_t)index >= size / sizeof(uint32_t)) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t element = 0;
    memcpy(&element, ids + (size_t)index * sizeof(element), sizeof(element));
    nmo_engine_block_set_output_value(b, 0, &element, sizeof(element));
    if (index_output) {
        set_out_int(b, 1, index);
    } else {
        set_local_int(b, 0, index);
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Chrono: Elapsed Time from On, by the frame's time (GetLastDeltaTimeFree, the
 * time before the minimum, maximum and scale of the time settings; the
 * engine's frame time is the host's, so it is that here) until Off.
 */
static int bb_chrono(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        set_out_float(b, 0, 0.0f);
    }
    set_out_float(b, 0, get_out_float(b, 0) + nmo_engine_block_delta_time(b));
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* The entity's children in order (CK3dEntity::GetChildrenCount and GetChild) */
static int32_t entity_child_count(const nmo_engine_t *engine, int32_t entity)
{
    int32_t count = 0;
    for (int32_t c = engine->entities.data[entity].first_child; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        count++;
    }
    return count;
}

static int32_t entity_child(const nmo_engine_t *engine, int32_t entity, int32_t index)
{
    for (int32_t c = engine->entities.data[entity].first_child; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        if (index-- == 0) {
            return c;
        }
    }
    return -1;
}

/* The target, or a character target's root body part */
static nmo_object_id_t hierarchy_root(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target != 0 && nmo_engine_object_class(b->engine, target) == NMO_CID_CHARACTER) {
        target = eng_character_root(b->engine, target);
    }
    return target;
}

/*
 * Hierarchy Parser: the target, then its descendants depth first, each with
 * its depth below the target; Max Depth (0 for none) stops the descent.
 */
static int bb_hierarchy_parser(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        nmo_object_id_t father = hierarchy_root(b);
        if (father != 0) {
            set_out_int(b, 0, (int32_t)father);
            set_out_int(b, 1, 0);
            OUT_ON(b, 1);
        } else {
            OUT_ON(b, 0);
        }
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 1);
    int32_t level = get_out_int(b, 1);
    nmo_object_id_t current_id = (nmo_object_id_t)get_out_int(b, 0);
    int32_t current = current_id != 0 ? eng_entity_index(engine, current_id) : -1;
    if (current < 0) {
        OUT_ON(b, 0); /* the original reads a missing entity */
        return NMO_ENGINE_BR_OK;
    }
    int32_t next = -1;
    int32_t max_depth = get_in_int(b, 0, 0);
    if ((max_depth == 0 || level < max_depth - 1) && entity_child_count(engine, current) > 0) {
        next = entity_child(engine, current, 0);
        ++level;
    } else {
        nmo_object_id_t target = hierarchy_root(b);
        if (current_id != target) { /* never above the entity it began with */
            int32_t father;
            while ((father = engine->entities.data[current].parent) >= 0) {
                int32_t count = entity_child_count(engine, father), i = 0;
                while (i < count && entity_child(engine, father, i++) != current) {
                }
                if (i == count) { /* no brother left on this level: the father's brother */
                    if (--level == 0) {
                        break;
                    }
                    current = father;
                } else {
                    next = entity_child(engine, father, i);
                    break;
                }
            }
        }
    }
    if (next >= 0) {
        set_out_int(b, 0, (int32_t)engine->entities.data[next].id);
        set_out_int(b, 1, level);
        OUT_ON(b, 1);
    } else {
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* A 2D Curve input's curve; NULL for none (the original reads a null curve) */
static const void *input_curve(blk_t *b, int pos, size_t *size)
{
    *size = 0;
    return nmo_engine_block_input_data(b, pos, size);
}

/*
 * IBCQ: the curve's value one Delta_In further at each loop, its change, and
 * the step reached, until the step passes 1.
 */
static int bb_ibcq(blk_t *b)
{
    size_t curve_size;
    const void *curve = input_curve(b, 1, &curve_size);
    if (curve == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        float start = 0.0f;
        nmo_engine_block_set_local_value(b, 0, &start, sizeof(start));
        set_out_float(b, 1, nmo_engine_curve_y(curve, curve_size, start));
    } else {
        IN_OFF(b, 1);
    }
    float previous_in = get_local_float(b, 0, 0.0f) + get_in_float(b, 0, 0.1f);
    float previous_out = get_out_float(b, 1);
    if (previous_in > 1.0f) {
        set_out_float(b, 1, 1.0f);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    float value = nmo_engine_curve_y(curve, curve_size, previous_in);
    nmo_engine_block_set_local_value(b, 0, &previous_in, sizeof(previous_in));
    set_out_float(b, 0, value - previous_out);
    set_out_float(b, 1, value);
    set_out_float(b, 2, previous_in);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Bezier Interpolator: the curve over Number of steps steps, scaled to
 * [Min, Max], and the change since the last step.
 */
static int bb_bezier_interpolator(blk_t *b)
{
    int32_t frames = get_in_int(b, 0, 100);
    size_t curve_size;
    const void *curve = input_curve(b, 1, &curve_size);
    if (curve == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float low = get_in_float(b, 2, 0.0f), high = get_in_float(b, 3, 1.0f);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, 0);
        set_out_float(b, 0, low + (high - low) * nmo_engine_curve_y(curve, curve_size, 0.0f));
    } else {
        IN_OFF(b, 1);
    }
    int32_t previous = get_local_int(b, 0, 0);
    float value = low + (high - low) *
                            nmo_engine_curve_y(curve, curve_size, (float)previous / (float)(frames - 1));
    float delta = value - get_out_float(b, 0);
    set_out_float(b, 0, value);
    set_out_float(b, 1, delta);
    previous++;
    if (previous >= frames) {
        OUT_ON(b, 0);
    } else {
        set_local_int(b, 0, previous);
        OUT_ON(b, 1);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Time Bezier Interpolator: as Bezier Interpolator over Time (ms) of the
 * engine's time, from the time In came (local 0, -1 once the time is over).
 * A Time input of another type than Integer is read as a float.
 */
static int bb_time_bezier_interpolator(blk_t *b)
{
    int32_t time = 3000;
    if (!nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_INT) &&
        nmo_engine_block_input_parameter_count(b) > 0) {
        time = (int32_t)get_in_float(b, 0, 3000.0f);
    } else {
        time = get_in_int(b, 0, 3000);
    }
    size_t curve_size;
    const void *curve = input_curve(b, 1, &curve_size);
    if (curve == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float low = get_in_float(b, 2, 0.0f), high = get_in_float(b, 3, 1.0f);
    int32_t now = (int32_t)nmo_engine_time(b->engine);
    int32_t start;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        start = now;
        set_local_int(b, 0, start);
        set_out_float(b, 0, low + (high - low) * nmo_engine_curve_y(curve, curve_size, 0.0f));
    } else {
        IN_OFF(b, 1);
        start = get_local_int(b, 0, 0);
    }
    if (start == -1) {
        OUT_ON(b, 0);
        nmo_engine_block_activate_output(b, 1, false);
        return NMO_ENGINE_BR_OK;
    }
    int32_t elapsed = now - start;
    float y;
    if (elapsed <= time) {
        y = nmo_engine_curve_y(curve, curve_size, (float)((double)elapsed / (double)time));
    } else {
        y = nmo_engine_curve_y(curve, curve_size, 1.0f);
        set_local_int(b, 0, -1);
    }
    float value = low + (high - low) * y;
    float delta = value - get_out_float(b, 0);
    set_out_float(b, 0, value);
    set_out_float(b, 1, delta);
    nmo_engine_block_activate_output(b, 0, false);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Ken Perlin's one dimension noise (perlin.cpp), its tables made by rand() the
 * first time. Logics.dll takes rand() from the shared MSVCRT.DLL, so what the
 * tables hold depends on every rand() before in the process; here they come
 * from MSVC's rand() from its first seed, the same in every run.
 */
#define PERLIN_B 0x100
#define PERLIN_BM 0xff
#define PERLIN_N 0x1000

typedef struct perlin {
    bool ready;
    int32_t p[PERLIN_B + PERLIN_B + 2];
    float g1[PERLIN_B + PERLIN_B + 2];
} perlin_t;

static int32_t msvc_rand(uint32_t *seed)
{
    *seed = *seed * 214013u + 2531011u;
    return (int32_t)((*seed >> 16) & 0x7fffu);
}

static void perlin_init(perlin_t *t)
{
    uint32_t seed = 1u;
    int i;
    for (i = 0; i < PERLIN_B; i++) {
        t->p[i] = i;
        t->g1[i] = (float)((msvc_rand(&seed) % (PERLIN_B + PERLIN_B)) - PERLIN_B) / PERLIN_B;
        for (int j = 0; j < 5; j++) { /* the two and three dimension gradients */
            (void)msvc_rand(&seed);
        }
    }
    while (--i) {
        int32_t k = t->p[i], j = msvc_rand(&seed) % PERLIN_B;
        t->p[i] = t->p[j];
        t->p[j] = k;
    }
    for (i = 0; i < PERLIN_B + 2; i++) {
        t->p[PERLIN_B + i] = t->p[i];
        t->g1[PERLIN_B + i] = t->g1[i];
    }
    t->ready = true;
}

static float perlin_noise1(const perlin_t *tab, float arg)
{
    float t = arg + PERLIN_N;
    int32_t bx0 = ((int32_t)t) & PERLIN_BM, bx1 = (bx0 + 1) & PERLIN_BM;
    float rx0 = t - (float)(int32_t)t, rx1 = rx0 - 1.0f;
    float sx = rx0 * rx0 * (3.0f - 2.0f * rx0);
    float u = rx0 * tab->g1[tab->p[bx0]], v = rx1 * tab->g1[tab->p[bx1]];
    return u + sx * (v - u);
}

/* PerlinNoise1D: `harmonics` octaves, each `beta` times faster and `alpha` times weaker */
static float perlin_noise_1d(const perlin_t *tab, float x, float alpha, float beta, int32_t harmonics)
{
    float sum = 0.0f, scale = 1.0f, p = x;
    for (int32_t i = 0; i < harmonics; i++) {
        sum += perlin_noise1(tab, p) / scale;
        scale *= alpha;
        p *= beta;
    }
    return sum;
}

/*
 * Noise Progression: the value moves between A and B by Perlin noise of the
 * progression over Time (NoiseProgressionTimeBased) or over a number of
 * frames (NoiseProgression), until the progression passes the end.
 */
static int noise_progression(blk_t *b, bool time_based)
{
    perlin_t *tab = (perlin_t *)nmo_engine_block_state(b, sizeof(perlin_t));
    if (tab == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    if (!tab->ready) {
        perlin_init(tab);
    }
    float a = get_in_float(b, 1, 0.0f), c = get_in_float(b, 2, 1.0f);
    float current = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_out_float(b, 1, a);
    } else {
        IN_OFF(b, 1);
        current = time_based ? get_out_float(b, 0) : (float)get_out_int(b, 0);
    }
    float alpha = get_in_float(b, 3, 2.0f), beta = get_in_float(b, 4, 2.0f);
    int32_t harmonics = get_in_int(b, 5, 4);
    float total;
    if (time_based) {
        current += nmo_engine_block_delta_time(b);
        set_out_float(b, 0, current);
        total = get_in_float(b, 0, 0.0f);
    } else {
        int32_t frame = (int32_t)current + 1;
        set_out_int(b, 0, frame);
        current = (float)frame;
        total = (float)get_in_int(b, 0, 0);
    }
    float old_position = get_out_float(b, 1);
    float position = old_position;
    float progression = current / total;
    if (current > total) {
        OUT_ON(b, 0);
    } else {
        float value = 0.5f + 0.5f * perlin_noise_1d(tab, progression, alpha, beta, harmonics);
        position = a + (c - a) * value;
        OUT_ON(b, 1);
    }
    set_out_float(b, 1, position);
    set_out_float(b, 2, position - old_position);
    set_out_float(b, 3, progression);
    return NMO_ENGINE_BR_OK;
}

static int bb_noise_progression(blk_t *b) { return noise_progression(b, false); }

static int bb_noise_progression_time(blk_t *b) { return noise_progression(b, true); }

/* NoiseProgressionCallBack: the Time Based setting picks the function */
static int cb_noise_progression(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(b, get_local_int(b, 0, 0) != 0 ? bb_noise_progression_time
                                                                       : bb_noise_progression);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_loops_blocks[] = {
    {NMO_GUID_INIT(0x15d472a5, 0x3bea409f), "Delayer", bb_delayer, NULL, NULL},
    {NMO_GUID_INIT(0xa2a5a63a, 0xe4e7e8e5), "Timer", bb_timer, cb_timer, NULL},
    {NMO_GUID_INIT(0x998f000f, 0xf000f899), "Counter", bb_counter, cb_counter, NULL},
    {NMO_GUID_INIT(0xfff45680, 0xaa512a39), "Linear Progression", bb_linear_progression,
     cb_linear_progression, NULL},
    {NMO_GUID_INIT(0x6bb8699a, 0x29fb6a4b), "Bezier Progression", bb_bezier_progression,
     cb_bezier_progression, NULL},
    {NMO_GUID_INIT(0x419602ec, 0x7822779e), "Collection Iterator", bb_collection_iterator, NULL, NULL},
    {NMO_GUID_INIT(0x5e2f1788, 0x4f965a44), "Chrono", bb_chrono, NULL, NULL},
    {NMO_GUID_INIT(0x5f4a214b, 0x6817452c), "Hierarchy Parser", bb_hierarchy_parser, NULL, NULL},
    {NMO_GUID_INIT(0xf5f22514, 0x123fffee), "IBCQ", bb_ibcq, NULL, NULL},
    {NMO_GUID_INIT(0x71570e39, 0x5b37428a), "Bezier Interpolator", bb_bezier_interpolator, NULL,
     NULL},
    {NMO_GUID_INIT(0x034b2e0e, 0x0cbf75e9), "Time Bezier Interpolator",
     bb_time_bezier_interpolator, NULL, NULL},
    {NMO_GUID_INIT(0x9424ff80, 0x9751bc39), "Noise Progression", bb_noise_progression_time,
     cb_noise_progression, NULL},
};

nmo_status_t eng_register_logics_loops(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_loops_blocks);
}
