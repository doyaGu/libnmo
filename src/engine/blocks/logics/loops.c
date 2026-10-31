/**
 * @file loops.c
 * @brief Ports of the Logics/Loops building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). The comments name the CK function a port follows when a block has
 * several versions.
 */

#include "../../engine_block_util.h"

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

static const nmo_engine_block_desc_t eng_logics_loops_blocks[] = {
    {NMO_GUID(0x15d472a5, 0x3bea409f), "Delayer", bb_delayer, NULL, NULL},
    {NMO_GUID(0xa2a5a63a, 0xe4e7e8e5), "Timer", bb_timer, cb_timer, NULL},
    {NMO_GUID(0x998f000f, 0xf000f899), "Counter", bb_counter, cb_counter, NULL},
    {NMO_GUID(0xfff45680, 0xaa512a39), "Linear Progression", bb_linear_progression,
     cb_linear_progression, NULL},
    {NMO_GUID(0x6bb8699a, 0x29fb6a4b), "Bezier Progression", bb_bezier_progression,
     cb_bezier_progression, NULL},
    {NMO_GUID(0x419602ec, 0x7822779e), "Collection Iterator", bb_collection_iterator, NULL, NULL},
};

nmo_status_t eng_register_logics_loops(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_loops_blocks);
}
