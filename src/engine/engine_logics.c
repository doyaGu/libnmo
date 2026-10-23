/**
 * @file engine_logics.c
 * @brief Ports of the common Logics building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks), through the engine's
 * block API. The comments name the CK function a port follows when a block
 * has several versions. Random numbers come from the engine's seeded
 * generator instead of rand().
 */

#include "engine_internal.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include "type/nmo_object_guids.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef nmo_engine_block_t blk_t;

#define IN_ACTIVE(b, i) nmo_engine_block_is_input_active((b), (i))
#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static float get_in_float(blk_t *b, int pos, float fallback)
{
    float value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static int32_t get_out_int(blk_t *b, int pos)
{
    int32_t value = 0;
    nmo_engine_block_get_output_value(b, pos, &value, sizeof(value));
    return value;
}

static float get_out_float(blk_t *b, int pos)
{
    float value = 0.0f;
    nmo_engine_block_get_output_value(b, pos, &value, sizeof(value));
    return value;
}

static int32_t get_local_int(blk_t *b, int pos)
{
    int32_t value = 0;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static void set_out_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_output_value(b, pos, &value, sizeof(value));
}

static void set_out_float(blk_t *b, int pos, float value)
{
    nmo_engine_block_set_output_value(b, pos, &value, sizeof(value));
}

static void set_local_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

/* CKParameterManager::IsDerivedFrom */
static bool type_derives(blk_t *b, nmo_guid_t guid, nmo_guid_t base)
{
    if (nmo_guid_equals(guid, base)) {
        return true;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(engine->types, guid);
    const nmo_type_descriptor_t *p = nmo_type_registry_find_by_guid(engine->types, base);
    return t != NULL && p != NULL && nmo_type_get_derivation_depth(engine->types, t->id, p->id) > 0;
}

/* ---- Logics/Calculator ---- */

static int bb_op(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_guid_t guid = eng_block_operation_guid(b);
    nmo_status_t status = nmo_guid_is_null(guid) ? NMO_ERR_NOT_FOUND
                                                 : eng_block_run_operation(b, guid);
    eng_emit(engine, NMO_ENGINE_EVENT_OPERATION, b->node, 0, (int32_t)status);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_identity(blk_t *b)
{
    int count = nmo_engine_block_input_parameter_count(b);
    if (count != nmo_engine_block_output_parameter_count(b)) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the CK function throws */
    }
    for (int i = 0; i < count; i++) {
        nmo_engine_block_copy_input_to_output(b, i, i);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_random(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_guid_t guid = nmo_engine_block_output_parameter_type(b, 0);
    if (nmo_guid_equals(guid, CKPGUID_BOOL)) {
        set_out_int(b, 0, nmo_engine_block_random(b) < 0.5 ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    if (!nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), guid) ||
        !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 1), guid)) {
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_guid_equals(guid, CKPGUID_INT)) {
        int32_t min = get_in_int(b, 0, 0), max = get_in_int(b, 1, 0);
        set_out_int(b, 0, min + (int32_t)(nmo_engine_block_random(b) * (double)(max - min)));
        return NMO_ENGINE_BR_OK;
    }
    /* Float and the float tuples: every component between its min and max */
    size_t count = 0;
    if (type_derives(b, guid, CKPGUID_FLOAT)) {
        count = 1;
    } else if (nmo_guid_equals(guid, CKPGUID_2DVECTOR)) {
        count = 2;
    } else if (nmo_guid_equals(guid, CKPGUID_VECTOR)) {
        count = 3;
    } else if (nmo_guid_equals(guid, CKPGUID_COLOR) || nmo_guid_equals(guid, CKPGUID_RECT)) {
        count = 4;
    }
    if (count == 0) {
        return NMO_ENGINE_BR_OK;
    }
    float min[4] = {0}, max[4] = {0}, res[4] = {0};
    nmo_engine_block_get_input_value(b, 0, min, count * sizeof(float));
    nmo_engine_block_get_input_value(b, 1, max, count * sizeof(float));
    for (size_t i = 0; i < count; i++) {
        res[i] = min[i] + (float)nmo_engine_block_random(b) * (max[i] - min[i]);
    }
    if (nmo_guid_equals(guid, CKPGUID_RECT)) { /* VxRect::Normalize */
        if (res[0] > res[2]) { float t = res[0]; res[0] = res[2]; res[2] = t; }
        if (res[1] > res[3]) { float t = res[1]; res[1] = res[3]; res[3] = t; }
    }
    nmo_engine_block_set_output_value(b, 0, res, count * sizeof(float));
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Streaming ---- */

static int bb_binary_switch(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t condition = get_in_int(b, 0, 1);
    OUT_ON(b, condition ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_nop(blk_t *b)
{
    for (int i = 0; i < nmo_engine_block_input_count(b); i++) {
        IN_OFF(b, i);
    }
    for (int i = 0; i < nmo_engine_block_output_count(b); i++) {
        OUT_ON(b, i);
    }
    return NMO_ENGINE_BR_OK;
}

static int test_compare(int op, float a, float c)
{
    switch (op) {
    case 1: return a == c;
    case 2: return a != c;
    case 3: return a < c;
    case 4: return a <= c;
    case 5: return a > c;
    case 6: return a >= c;
    default: return 0;
    }
}

static int bb_test(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t op = get_in_int(b, 0, 1); /* CKEQUAL */
    float val[2] = {0.0f, 0.0f};
    const char *str[2] = {NULL, NULL};
    bool is_string = false;
    for (int a = 0; a < 2; a++) {
        nmo_guid_t guid = nmo_engine_block_input_parameter_type(b, a + 1);
        if (type_derives(b, guid, CKPGUID_FLOAT)) {
            val[a] = get_in_float(b, a + 1, 0.0f);
        } else if (nmo_guid_equals(guid, CKPGUID_STRING)) {
            size_t size = 0;
            const char *text = (const char *)nmo_engine_block_input_data(b, a + 1, &size);
            str[a] = (text != NULL && size > 0 && memchr(text, '\0', size) != NULL) ? text : NULL;
            is_string = true;
        } else {
            int32_t value = get_in_int(b, a + 1, 0);
            val[a] = nmo_guid_equals(guid, CKPGUID_BOOL) ? (value ? 1.0f : 0.0f) : (float)value;
        }
    }
    if (op <= 0) {
        return NMO_ENGINE_BR_OK;
    }
    int k;
    if (is_string) {
        /* ICompare: a null string sorts first */
        int cmp = str[0] == NULL ? (str[1] == NULL ? 0 : -1)
                  : str[1] == NULL ? 1 : strcmp(str[0], str[1]);
        k = test_compare(op, (float)cmp, 0.0f);
    } else {
        k = test_compare(op, val[0], val[1]);
    }
    OUT_ON(b, k ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_parameter_selector(blk_t *b)
{
    for (int i = 0; i < nmo_engine_block_input_count(b); i++) {
        if (IN_ACTIVE(b, i)) {
            IN_OFF(b, i);
            nmo_engine_block_copy_input_to_output(b, i, 0);
            break;
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_switch_on_parameter(blk_t *b)
{
    size_t tester_size = 0;
    nmo_guid_t tester_type = nmo_engine_block_input_parameter_type(b, 0);
    const void *tester = nmo_engine_block_input_data(b, 0, &tester_size);
    float epsilon = 0.0f;
    nmo_engine_block_get_local_value(b, 0, &epsilon, sizeof(epsilon));
    bool from_float = type_derives(b, tester_type, CKPGUID_FLOAT);

    for (int i = 1; tester != NULL && i < nmo_engine_block_input_parameter_count(b); i++) {
        if (!nmo_guid_equals(nmo_engine_block_input_parameter_type(b, i), tester_type)) {
            continue;
        }
        size_t size = 0;
        const void *value = nmo_engine_block_input_data(b, i, &size);
        if (value == NULL) {
            continue;
        }
        bool match;
        if (from_float && epsilon != 0.0f && size >= sizeof(float) &&
            tester_size >= sizeof(float)) {
            float t, p;
            memcpy(&t, tester, sizeof(t));
            memcpy(&p, value, sizeof(p));
            match = p >= t - epsilon && p < t + epsilon;
        } else if (nmo_guid_equals(tester_type, CKPGUID_STRING)) {
            match = memchr(value, '\0', size) != NULL && memchr(tester, '\0', tester_size) != NULL &&
                    strcmp((const char *)value, (const char *)tester) == 0;
        } else if (nmo_guid_equals(tester_type, CKPGUID_BOOL)) {
            int32_t t = 0, p = 0;
            memcpy(&t, tester, tester_size < sizeof(t) ? tester_size : sizeof(t));
            memcpy(&p, value, size < sizeof(p) ? size : sizeof(p));
            match = (t != 0) == (p != 0);
        } else {
            match = size >= tester_size && memcmp(value, tester, tester_size) == 0;
        }
        if (match) {
            IN_OFF(b, 0);
            OUT_ON(b, i);
            return NMO_ENGINE_BR_OK;
        }
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Sequencer2 (version > 1.0): Reset, In; Exit Reset, Out 1.. */
static int bb_sequencer(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        int32_t current = get_out_int(b, 0) + 1;
        if (current >= nmo_engine_block_output_count(b) - 1) {
            current = 0;
        }
        set_out_int(b, 0, current);
        OUT_ON(b, current + 1);
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_out_int(b, 0, -1);
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Sequencer (version 1.0): In, Reset; the count is a local. */
static int bb_sequencer_v1(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        int32_t current = get_local_int(b, 0);
        OUT_ON(b, current);
        if (++current >= nmo_engine_block_output_count(b)) {
            current = 0;
        }
        set_local_int(b, 0, current);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        set_local_int(b, 0, 1);
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_sequencer(blk_t *b, uint32_t message)
{
    bool v2 = nmo_engine_block_version(b) > 0x00010000u;
    if (message == NMO_ENGINE_MSG_RESET) {
        if (v2) {
            set_out_int(b, 0, -1);
        } else {
            set_local_int(b, 0, 0);
        }
    } else if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(b, v2 ? bb_sequencer : bb_sequencer_v1);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_binary_memory(blk_t *b)
{
    int32_t k;
    if (IN_ACTIVE(b, 0)) {
        k = 1;
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    } else {
        k = 0;
        IN_OFF(b, 1);
        OUT_ON(b, 1);
    }
    set_out_int(b, 0, k);
    return NMO_ENGINE_BR_OK;
}

static int bb_wait_for_all(blk_t *b)
{
    int count = nmo_engine_block_input_count(b);
    for (int a = 0; a < count; a++) {
        if (!IN_ACTIVE(b, a)) {
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
    }
    for (int a = 0; a < count; a++) {
        IN_OFF(b, a);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* StreamingEventV2: an input fires its output once until it is seen inactive. */
static int bb_streaming_event(blk_t *b)
{
    int32_t lao = get_local_int(b, 0);
    int count = nmo_engine_block_input_count(b);
    for (int i = 0; i < count && i < 32; i++) {
        if (IN_ACTIVE(b, i)) {
            IN_OFF(b, i);
            if (!(lao & (1 << i))) {
                lao |= 1 << i;
                set_out_int(b, 0, i);
                OUT_ON(b, i);
            }
        } else {
            lao &= ~(1 << i);
        }
    }
    set_local_int(b, 0, lao);
    return NMO_ENGINE_BR_OK;
}

static int bb_streaming_event_v1(blk_t *b)
{
    int32_t lao = get_out_int(b, 0);
    for (int i = 0; i < nmo_engine_block_input_count(b); i++) {
        if (IN_ACTIVE(b, i)) {
            IN_OFF(b, i);
            if (lao != i) {
                set_out_int(b, 0, i);
                OUT_ON(b, i);
                break;
            }
        }
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_streaming_event(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD && nmo_engine_block_version(b) < 0x00020000u) {
        nmo_engine_block_set_function(b, bb_streaming_event_v1);
    }
    if (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_RESET) {
        set_out_int(b, 0, -1);
        set_local_int(b, 0, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_keep_active(blk_t *b)
{
    int count = nmo_engine_block_input_count(b);
    if (IN_ACTIVE(b, 0)) {
        set_local_int(b, 1, 0);
        for (int a = 0; a < count; a++) {
            IN_OFF(b, a);
        }
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (!get_local_int(b, 0)) {
        for (int a = 1; a < count; a++) {
            if (IN_ACTIVE(b, a)) {
                OUT_ON(b, a);
            }
        }
    } else {
        int32_t last = get_local_int(b, 1);
        for (int a = 1; a < count; a++) {
            if (IN_ACTIVE(b, a) && a != last) {
                IN_OFF(b, last);
                last = a;
                set_local_int(b, 1, last);
                break;
            }
        }
        OUT_ON(b, last);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int bb_one_at_a_time(blk_t *b)
{
    int a = 1, count = nmo_engine_block_input_count(b);
    for (; a < count; a++) {
        if (IN_ACTIVE(b, a)) {
            set_local_int(b, 0, a);
            break;
        }
    }
    for (; a < count; a++) {
        IN_OFF(b, a);
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        OUT_ON(b, get_local_int(b, 0));
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_one_at_a_time(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_RESET) {
        set_local_int(b, 0, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_all_but_one(blk_t *b)
{
    int count = nmo_engine_block_input_count(b);
    for (int a = 0; a < count; a++) {
        if (!IN_ACTIVE(b, a)) {
            continue;
        }
        IN_OFF(b, a);
        for (int o = 0; o < count; o++) {
            if (o != a) {
                OUT_ON(b, o);
            }
        }
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_random_switch(blk_t *b)
{
    IN_OFF(b, 0);
    bool forbid_twice = get_local_int(b, 0) != 0;
    int32_t old_out = forbid_twice ? get_local_int(b, 1) : -1;
    int count = nmo_engine_block_input_parameter_count(b);
    float sum = 0.0f;
    for (int c = 0; c < count; c++) {
        if (c != old_out) {
            sum += get_in_float(b, c, 1.0f);
        }
    }
    float res = sum * (float)nmo_engine_block_random(b);
    sum = 0.0f;
    for (int c = 0; c < count; c++) {
        if (c == old_out) {
            continue;
        }
        sum += get_in_float(b, c, 1.0f);
        if (res <= sum) {
            OUT_ON(b, c);
            if (forbid_twice) {
                set_local_int(b, 1, c);
            }
            break;
        }
    }
    return NMO_ENGINE_BR_OK;
}

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
    if (get_local_int(b, 0) == 0) {
        nmo_engine_block_set_function(b, bb_linear_progression_frames);
    } else if (nmo_engine_block_version(b) < 0x00010005u &&
               !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_TIME)) {
        nmo_engine_block_set_function(b, bb_linear_progression_int);
    } else {
        nmo_engine_block_set_function(b, bb_linear_progression);
    }
    return NMO_ENGINE_BR_OK;
}

/* Per Second: each output takes its input times the frame time in seconds, float by float. */
static int bb_per_second(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float delta = nmo_engine_block_delta_time(b) * 0.001f;
    for (int i = 0; i < nmo_engine_block_output_parameter_count(b); i++) {
        size_t in_size = 0;
        const uint8_t *in = (const uint8_t *)nmo_engine_block_input_data(b, i, &in_size);
        size_t out_size = nmo_engine_block_output_parameter_size(b, i);
        size_t count = out_size / sizeof(float);
        if (in == NULL || count == 0 || count > 16 || in_size < count * sizeof(float)) {
            continue;
        }
        float values[16];
        for (size_t k = 0; k < count; k++) {
            memcpy(&values[k], in + k * sizeof(float), sizeof(float));
            values[k] *= delta;
        }
        nmo_engine_block_set_output_value(b, i, values, count * sizeof(float));
    }
    return NMO_ENGINE_BR_OK;
}

/* Threshold: X against MIN and MAX; the output is X clamped. */
static int bb_threshold(blk_t *b)
{
    IN_OFF(b, 0);
    float x = get_in_float(b, 0, 0.0f);
    float min = get_in_float(b, 1, 0.0f);
    float clamped = x;
    if (x < min) {
        clamped = min;
        OUT_ON(b, 0);
    } else {
        /* MAX is read only now, as the original reads it (an input may run an operation) */
        float max = get_in_float(b, 2, 0.0f);
        if (x > max) {
            clamped = max;
            OUT_ON(b, 1);
        } else {
            OUT_ON(b, 2);
        }
    }
    set_out_float(b, 0, clamped);
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Interpolator ---- */

/* hsv_rgb.cpp */
#define HSV_UNDEFINED -1.0f

static void rgb_to_hsv(float r, float g, float bl, float *h, float *s, float *v)
{
    float fmax = r > g ? r : g;
    fmax = fmax > bl ? fmax : bl;
    float fmin = r < g ? r : g;
    fmin = fmin < bl ? fmin : bl;
    *v = fmax;
    *s = fmax != 0.0f ? (fmax - fmin) / fmax : 0.0f;
    if (*s == 0.0f) {
        *h = HSV_UNDEFINED;
        return;
    }
    float delta = fmax - fmin;
    if (r == fmax) {
        *h = (g - bl) / delta;
    } else if (g == fmax) {
        *h = 2 + (bl - r) / delta;
    } else {
        *h = 4 + (r - g) / delta;
    }
    *h *= 60.0f;
    if (*h < 0.0f) {
        *h += 360.0f;
    }
}

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *bl)
{
    if (s == 0) {
        if (h == HSV_UNDEFINED) {
            *r = *g = *bl = v;
        }
        return;
    }
    if (h == 360.0f) {
        h = 0.0f;
    }
    h /= 60.0f;
    int i = (int)floorf(h);
    float f = h - (float)i;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0: *r = v; *g = t; *bl = p; break;
    case 1: *r = q; *g = v; *bl = p; break;
    case 2: *r = p; *g = v; *bl = t; break;
    case 3: *r = p; *g = q; *bl = v; break;
    case 4: *r = t; *g = p; *bl = v; break;
    case 5: *r = v; *g = p; *bl = q; break;
    default: break;
    }
}

/* A and B as `count` floats, and the value; out = A + (B - A) * value per float. */
static int interpolate_floats(blk_t *b, size_t count)
{
    float a[4] = {0}, c[4] = {0}, r[4];
    float value = get_in_float(b, 2, 0.0f);
    nmo_engine_block_get_input_value(b, 0, a, count * sizeof(float));
    nmo_engine_block_get_input_value(b, 1, c, count * sizeof(float));
    for (size_t i = 0; i < count; i++) {
        r[i] = a[i] + (c[i] - a[i]) * value;
    }
    nmo_engine_block_set_output_value(b, 0, r, count * sizeof(float));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_interpolator_float(blk_t *b) { return interpolate_floats(b, 1); }
static int bb_interpolator_2d(blk_t *b) { return interpolate_floats(b, 2); }
static int bb_interpolator_vector(blk_t *b) { return interpolate_floats(b, 3); }
/* InterpolatorColor and InterpolatorRectangle (VxRect::Interpolate) are per float too */
static int bb_interpolator_four(blk_t *b) { return interpolate_floats(b, 4); }

static int bb_interpolator_int(blk_t *b)
{
    int32_t a = get_in_int(b, 0, 0), c = get_in_int(b, 1, 0);
    float value = get_in_float(b, 2, 0.0f);
    set_out_int(b, 0, (int32_t)(a + (c - a) * value));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Slerp (VxQuaternionDetail::Interpolate) */
static int bb_interpolator_quaternion(blk_t *b)
{
    float a[4] = {0, 0, 0, 1}, c[4] = {0, 0, 0, 1}, r[4];
    double t = get_in_float(b, 2, 0.0f);
    nmo_engine_block_get_input_value(b, 0, a, sizeof(a));
    nmo_engine_block_get_input_value(b, 1, c, sizeof(c));
    double dot = (((double)a[2] * c[2] + (double)a[1] * c[1]) + (double)a[0] * c[0]) +
                 (double)a[3] * c[3];
    double sign = dot < 0.0 ? -1.0 : 1.0;
    double cosine = dot < 0.0 ? -dot : dot;
    if (t == 0.0) {
        memcpy(r, a, sizeof(r));
    } else if (t == 1.0) {
        for (int i = 0; i < 4; i++) {
            r[i] = (float)(sign * c[i]);
        }
    } else if (!(cosine < 0.99)) {
        for (int i = 0; i < 4; i++) {
            r[i] = (float)(a[i] + t * (sign * c[i] - a[i]));
        }
    } else {
        double angle = acos(cosine), phase = t * angle, inverse_sine = 1.0 / sin(angle);
        for (int i = 0; i < 4; i++) {
            double tangent = (sign * c[i] - cosine * a[i]) * inverse_sine;
            r[i] = (float)(a[i] * cos(phase) + tangent * sin(phase));
        }
    }
    nmo_engine_block_set_output_value(b, 0, r, sizeof(r));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ColorHSVInterpolator, with the original's green-for-blue slip on black colors */
static int bb_interpolator_color_hsv(blk_t *b)
{
    float c1[4] = {0}, c2[4] = {0}, res[4] = {0};
    nmo_engine_block_get_input_value(b, 0, c1, sizeof(c1));
    nmo_engine_block_get_input_value(b, 1, c2, sizeof(c2));
    float mix = get_in_float(b, 2, 0.0f);
    if (!(c1[0] || c1[1] || c1[2]) || !(c2[0] || c2[1] || c2[2])) {
        res[0] = (1.0f - mix) * c1[0] + mix * c2[0];
        res[1] = (1.0f - mix) * c1[1] + mix * c2[1];
        res[2] = (1.0f - mix) * c1[1] + mix * c2[2];
    } else {
        float h1, s1, v1, h2, s2, v2;
        const float pi = 3.14159265358979323846f;
        rgb_to_hsv(c1[0], c1[1], c1[2], &h1, &s1, &v1);
        rgb_to_hsv(c2[0], c2[1], c2[2], &h2, &s2, &v2);
        if (h1 > 180.0f) {
            h1 -= 360.0f;
        }
        if (h2 > 180.0f) {
            h2 -= 360.0f;
        }
        float x1 = cosf(h1 * pi / 180.0f) * s1, y1 = sinf(h1 * pi / 180.0f) * s1;
        float x2 = cosf(h2 * pi / 180.0f) * s2, y2 = sinf(h2 * pi / 180.0f) * s2;
        float x = x1 + (x2 - x1) * mix, y = y1 + (y2 - y1) * mix;
        s1 = sqrtf(x * x + y * y);
        h1 = asinf(y / s1) * 180.0f / pi;
        if (x < 0.0f) {
            h1 = y > 0.0f ? 180 - h1 : -180.0f - h1;
        }
        v1 = (1.0f - mix) * v1 + mix * v2;
        if (h1 < 0.0f) {
            h1 += 360.0f;
        }
        hsv_to_rgb(h1, s1, v1, &res[0], &res[1], &res[2]);
    }
    res[3] = (1.0f - mix) * c1[3] + mix * c2[3];
    nmo_engine_block_set_output_value(b, 0, res, sizeof(res));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * The function goes by the output's type, as the load callback picks it. A
 * matrix (Vx3DInterpolateMatrix) has no port: the block keeps no function.
 */
static int cb_interpolator(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_guid_t out = nmo_engine_block_output_parameter_type(b, 0);
    nmo_engine_block_fn fn = NULL;
    if (type_derives(b, out, CKPGUID_FLOAT)) {
        fn = bb_interpolator_float;
    } else if (nmo_guid_equals(out, CKPGUID_VECTOR) || nmo_guid_equals(out, CKPGUID_EULERANGLES)) {
        fn = bb_interpolator_vector;
    } else if (nmo_guid_equals(out, CKPGUID_2DVECTOR)) {
        fn = bb_interpolator_2d;
    } else if (nmo_guid_equals(out, CKPGUID_INT)) {
        fn = bb_interpolator_int;
    } else if (nmo_guid_equals(out, CKPGUID_COLOR)) {
        fn = get_local_int(b, 0) ? bb_interpolator_color_hsv : bb_interpolator_four;
    } else if (nmo_guid_equals(out, CKPGUID_QUATERNION)) {
        fn = bb_interpolator_quaternion;
    } else if (nmo_guid_equals(out, CKPGUID_RECT)) {
        fn = bb_interpolator_four;
    } else if (!nmo_guid_equals(out, CKPGUID_MATRIX)) {
        return NMO_ENGINE_BR_OK; /* another type: the function stays InterpolatorFloat */
    }
    nmo_engine_block_set_function(b, fn);
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Calculator ---- */

/*
 * Set Component: the output (color, vector, 2D vector, rectangle or box) made
 * of the inputs. The struct types it also fills (CKStructHelper) are left out.
 */
static int bb_set_component(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_guid_t out = nmo_engine_block_output_parameter_type(b, 0);
    if (nmo_guid_is_null(out)) {
        return NMO_ENGINE_BR_OK;
    }
    float v[6] = {0};
    if (nmo_guid_equals(out, CKPGUID_COLOR)) {
        for (int i = 0; i < 4; i++) {
            v[i] = get_in_float(b, i, 0.0f);
        }
        nmo_engine_block_set_output_value(b, 0, v, 4 * sizeof(float));
    } else if (nmo_guid_equals(out, CKPGUID_VECTOR) || nmo_guid_equals(out, CKPGUID_EULERANGLES) ||
               nmo_guid_equals(out, CKPGUID_2DVECTOR)) {
        int count = nmo_guid_equals(out, CKPGUID_2DVECTOR) ? 2 : 3;
        for (int i = 0; i < count; i++) {
            v[i] = get_in_float(b, i, 0.0f);
        }
        nmo_engine_block_set_output_value(b, 0, v, (size_t)count * sizeof(float));
    } else if (nmo_guid_equals(out, CKPGUID_RECT) || nmo_guid_equals(out, CKPGUID_BOX)) {
        /* Two points as corners (1), position and size (2), or center and half size (3) */
        size_t n = nmo_guid_equals(out, CKPGUID_RECT) ? 2 : 3;
        float p1[3] = {0}, p2[3] = {0};
        nmo_engine_block_get_input_value(b, 0, p1, n * sizeof(float));
        nmo_engine_block_get_input_value(b, 1, p2, n * sizeof(float));
        int32_t mode = 1;
        nmo_engine_block_get_local_value(b, 0, &mode, sizeof(mode));
        float lo[3], hi[3];
        for (size_t i = 0; i < n; i++) {
            lo[i] = mode == 3 ? p1[i] - p2[i] : p1[i];
            hi[i] = mode == 2 ? p1[i] + p2[i] : mode == 3 ? p1[i] + p2[i] : p2[i];
        }
        if (mode < 1 || mode > 3) {
            return NMO_ENGINE_BR_OK; /* the CK switch leaves the value as it was */
        }
        /* VxRect is left, top, right, bottom; VxBbox is Min, then Max */
        memcpy(v, lo, n * sizeof(float));
        memcpy(v + n, hi, n * sizeof(float));
        nmo_engine_block_set_output_value(b, 0, v, 2 * n * sizeof(float));
    }
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Message ---- */

static int bb_wait_message(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t type = get_in_int(b, 0, -1);
    if (type < 0) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        nmo_engine_block_register_wait(b, type, 0, target);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        nmo_engine_block_unregister_wait(b, type, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_send_message(blk_t *b)
{
    nmo_object_id_t sender = nmo_engine_block_target(b);
    if (sender == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t type = get_in_int(b, 0, -1);
    nmo_engine_block_send_message(b, type, nmo_engine_block_get_input_object(b, 1), sender);
    return NMO_ENGINE_BR_OK;
}

static int bb_broadcast_message(blk_t *b)
{
    nmo_object_id_t sender = nmo_engine_block_target(b);
    if (sender == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t type = get_in_int(b, 0, -1);
    int32_t class_id = NMO_CID_BEOBJECT;
    if (nmo_engine_block_version(b) > 0x00010000u) {
        class_id = get_in_int(b, 1, NMO_CID_BEOBJECT);
    }
    nmo_engine_block_broadcast_message(b, type, (nmo_class_id_t)class_id, sender);
    return NMO_ENGINE_BR_OK;
}

static int bb_switch_on_message(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int count = nmo_engine_block_last_frame_message_count(b, owner);
    for (int i = 0; count > 0 && i < nmo_engine_block_input_parameter_count(b); i++) {
        int32_t type = get_in_int(b, i, -1);
        for (int a = 0; a < count; a++) {
            if (nmo_engine_block_last_frame_message_type(b, owner, a) == type) {
                OUT_ON(b, i);
            }
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_switch_on_message(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_waiting_for_messages(b, nmo_engine_block_owner(b), true);
    }
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Array ---- */

static nmo_class_id_t type_class(blk_t *b, nmo_guid_t type)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const nmo_type_descriptor_t *desc = nmo_type_registry_find_by_guid(engine->types, type);
    return desc != NULL ? (nmo_class_id_t)desc->class_id : 0;
}

/* The string an input reads, or NULL when it reads none (or bytes without a terminator). */
static const char *input_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : NULL;
}

/* WriteOutputParameters: row `row` into the outputs from `first`, where types agree. */
static void array_write_outputs(blk_t *b, nmo_object_id_t array, int32_t row, int first)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    for (int32_t c = 0; c < columns; c++) {
        nmo_guid_t out = nmo_engine_block_output_parameter_type(b, first + c);
        nmo_engine_cell_t cell;
        if (nmo_guid_is_null(out) ||
            nmo_engine_array_get_cell(engine, array, row, c, &cell) != NMO_OK) {
            continue;
        }
        switch (cell.type) {
        case CKARRAYTYPE_INT:
            if (nmo_guid_equals(out, CKPGUID_INT)) {
                nmo_engine_block_set_output_value(b, first + c, &cell.value, sizeof(cell.value));
            }
            break;
        case CKARRAYTYPE_FLOAT:
            if (nmo_guid_equals(out, CKPGUID_FLOAT)) {
                nmo_engine_block_set_output_value(b, first + c, &cell.value, sizeof(cell.value));
            }
            break;
        case CKARRAYTYPE_STRING:
            if (nmo_guid_equals(out, CKPGUID_STRING)) {
                const char *text = cell.text != NULL ? cell.text : "";
                nmo_engine_block_set_output_value(b, first + c, text, strlen(text) + 1u);
            }
            break;
        case CKARRAYTYPE_OBJECT: {
            nmo_class_id_t cid = type_class(b, out);
            if (cid != 0) {
                uint32_t id = eng_is_child_class(engine, eng_object_class(engine, cell.value), cid)
                                  ? cell.value
                                  : 0u;
                nmo_engine_block_set_output_value(b, first + c, &id, sizeof(id));
            }
            break;
        }
        case CKARRAYTYPE_PARAMETER:
            /* CKParameter::CopyValue: when the types are compatible */
            if (cell.data != NULL && (type_derives(b, cell.parameter_type, out) ||
                                      type_derives(b, out, cell.parameter_type))) {
                nmo_engine_block_set_output_value(b, first + c, cell.data, cell.size);
            }
            break;
        default:
            break;
        }
    }
}

/* ReadInputParameters: the inputs from `first` into row `row`, where types agree. */
static void array_read_inputs(blk_t *b, nmo_object_id_t array, int32_t row, int first)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    for (int32_t c = 0; c < columns; c++) {
        nmo_guid_t in = nmo_engine_block_input_parameter_type(b, first + c);
        size_t size = 0;
        const void *data = nmo_engine_block_input_data(b, first + c, &size);
        if (nmo_guid_is_null(in) || data == NULL) {
            continue;
        }
        nmo_engine_cell_t cell = {.type = nmo_engine_array_column_type(engine, array, c)};
        switch (cell.type) {
        case CKARRAYTYPE_INT:
        case CKARRAYTYPE_FLOAT:
        case CKARRAYTYPE_OBJECT: {
            nmo_guid_t expected = cell.type == CKARRAYTYPE_INT     ? CKPGUID_INT
                                  : cell.type == CKARRAYTYPE_FLOAT ? CKPGUID_FLOAT
                                                                   : CKPGUID_BEOBJECT;
            if (!nmo_guid_equals(in, expected)) {
                continue;
            }
            memcpy(&cell.value, data, size < sizeof(cell.value) ? size : sizeof(cell.value));
            break;
        }
        case CKARRAYTYPE_STRING:
            if (!nmo_guid_equals(in, CKPGUID_STRING)) {
                continue;
            }
            cell.text = input_string(b, first + c);
            break;
        case CKARRAYTYPE_PARAMETER: {
            nmo_engine_cell_t current;
            if (nmo_engine_array_get_cell(engine, array, row, c, &current) != NMO_OK ||
                !nmo_guid_equals(in, current.parameter_type)) {
                continue;
            }
            cell.data = data;
            cell.size = size;
            break;
        }
        default:
            continue;
        }
        nmo_engine_array_set_cell(engine, array, row, c, &cell);
    }
}

/*
 * GetInputValue: input `pos` as a key for `column`; false when its type is not
 * the column's, and the key then is a zero (as the CK code leaves it).
 */
static void array_input_key(blk_t *b, nmo_object_id_t array, int32_t column, int pos,
                            nmo_engine_cell_t *key)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    memset(key, 0, sizeof(*key));
    key->type = nmo_engine_array_column_type(engine, array, column);
    nmo_guid_t in = nmo_engine_block_input_parameter_type(b, pos);
    switch (key->type) {
    case CKARRAYTYPE_INT:
        if (nmo_guid_equals(in, CKPGUID_INT)) {
            key->value = (uint32_t)get_in_int(b, pos, 0);
        }
        break;
    case CKARRAYTYPE_FLOAT:
        if (nmo_guid_equals(in, CKPGUID_FLOAT)) {
            nmo_engine_block_get_input_value(b, pos, &key->value, sizeof(key->value));
        }
        break;
    case CKARRAYTYPE_STRING:
        if (nmo_guid_equals(in, CKPGUID_STRING)) {
            key->text = input_string(b, pos);
        }
        break;
    case CKARRAYTYPE_OBJECT:
        if (nmo_guid_equals(in, CKPGUID_BEOBJECT)) {
            key->value = (uint32_t)get_in_int(b, pos, 0);
        }
        break;
    case CKARRAYTYPE_PARAMETER: {
        nmo_engine_cell_t first;
        if (nmo_engine_array_get_cell(engine, array, 0, column, &first) == NMO_OK &&
            nmo_guid_equals(in, first.parameter_type)) {
            key->data = nmo_engine_block_input_data(b, pos, &key->size);
        }
        break;
    }
    default:
        break;
    }
}

/* The data array a block works on (its target); 0 when the target is not one. */
static nmo_object_id_t array_target(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    return nmo_engine_array_row_count(nmo_engine_block_engine(b), target) >= 0 ? target : 0;
}

/* Get Cell (ArrayGetElement) */
static int bb_get_cell(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0), column = get_in_int(b, 1, 0);
    nmo_engine_cell_t cell;
    IN_OFF(b, 0);
    if (nmo_engine_array_get_cell(engine, array, row, column, &cell) != NMO_OK) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    /* The output written as a one-column row */
    nmo_guid_t out = nmo_engine_block_output_parameter_type(b, 0);
    switch (cell.type) {
    case CKARRAYTYPE_INT:
    case CKARRAYTYPE_FLOAT:
        if (nmo_guid_equals(out, cell.type == CKARRAYTYPE_INT ? CKPGUID_INT : CKPGUID_FLOAT)) {
            nmo_engine_block_set_output_value(b, 0, &cell.value, sizeof(cell.value));
        }
        break;
    case CKARRAYTYPE_STRING:
        if (nmo_guid_equals(out, CKPGUID_STRING)) {
            const char *text = cell.text != NULL ? cell.text : "";
            nmo_engine_block_set_output_value(b, 0, text, strlen(text) + 1u);
        }
        break;
    case CKARRAYTYPE_OBJECT: {
        nmo_class_id_t cid = type_class(b, out);
        if (cid != 0) {
            uint32_t id = eng_is_child_class(engine, eng_object_class(engine, cell.value), cid)
                              ? cell.value
                              : 0u;
            nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
        }
        break;
    }
    case CKARRAYTYPE_PARAMETER:
        if (cell.data != NULL && (type_derives(b, cell.parameter_type, out) ||
                                  type_derives(b, out, cell.parameter_type))) {
            nmo_engine_block_set_output_value(b, 0, cell.data, cell.size);
        }
        break;
    default:
        break;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* A parameter value as text (GetStringValue), for the types a string column takes. */
static char *value_text(blk_t *b, nmo_guid_t type, const void *data, size_t size)
{
    if (type_derives(b, type, CKPGUID_STRING)) {
        if (memchr(data, '\0', size) == NULL) {
            return NULL;
        }
        size_t length = strlen((const char *)data);
        char *copy = (char *)malloc(length + 1u);
        if (copy != NULL) {
            memcpy(copy, data, length + 1u);
        }
        return copy;
    }
    if (size < 4) {
        return NULL;
    }
    int32_t i = 0;
    float f = 0.0f;
    memcpy(&i, data, sizeof(i));
    memcpy(&f, data, sizeof(f));
    bool is_float = type_derives(b, type, CKPGUID_FLOAT);
    if (!is_float && !type_derives(b, type, CKPGUID_INT)) {
        return NULL;
    }
    int length = is_float ? snprintf(NULL, 0, "%g", f) : snprintf(NULL, 0, "%d", i);
    char *text = length >= 0 ? (char *)malloc((size_t)length + 1u) : NULL;
    if (text != NULL) {
        if (is_float) {
            snprintf(text, (size_t)length + 1u, "%g", f);
        } else {
            snprintf(text, (size_t)length + 1u, "%d", i);
        }
    }
    return text;
}

/* Set Cell (ArraySetElement, CKDataArray::SetElementValueFromParameter) */
static int bb_set_cell(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0), column = get_in_int(b, 1, 0);
    size_t size = 0;
    const void *data = nmo_engine_block_input_data(b, 2, &size);
    nmo_engine_cell_t current;
    IN_OFF(b, 0);
    if (data == NULL || nmo_engine_array_get_cell(engine, array, row, column, &current) != NMO_OK) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_cell_t cell = {.type = current.type};
    char *text = NULL;
    switch (current.type) {
    case CKARRAYTYPE_STRING:
        text = value_text(b, nmo_engine_block_input_parameter_type(b, 2), data, size);
        cell.text = text;
        nmo_engine_array_set_cell(engine, array, row, column, &cell);
        free(text);
        break;
    case CKARRAYTYPE_PARAMETER:
        if (type_derives(b, nmo_engine_block_input_parameter_type(b, 2), current.parameter_type) ||
            type_derives(b, current.parameter_type, nmo_engine_block_input_parameter_type(b, 2))) {
            cell.data = data;
            cell.size = size;
            nmo_engine_array_set_cell(engine, array, row, column, &cell);
        }
        break;
    default:
        memcpy(&cell.value, data, size < sizeof(cell.value) ? size : sizeof(cell.value));
        nmo_engine_array_set_cell(engine, array, row, column, &cell);
        break;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Get Row (ArrayGetLine) */
static int bb_get_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = get_in_int(b, 0, 0);
    IN_OFF(b, 0);
    if (row < 0 || row >= nmo_engine_array_row_count(nmo_engine_block_engine(b), array)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Row (ArraySetLine) */
static int bb_set_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0);
    int32_t unique = 1;
    nmo_engine_block_get_local_value(b, 0, &unique, sizeof(unique));
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (unique && key_column >= 0) {
        nmo_engine_cell_t key;
        array_input_key(b, array, key_column, key_column + 1, &key);
        int32_t found = nmo_engine_array_find_row(engine, array, key_column, &key, 0);
        if (found != -1 && found != row) {
            IN_OFF(b, 0);
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
    }
    if (row >= 0 && row < nmo_engine_array_row_count(engine, array)) {
        array_read_inputs(b, array, row, 1);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Add Row (AddArray) */
static int bb_add_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t unique = 1;
    nmo_engine_block_get_local_value(b, 0, &unique, sizeof(unique));
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (unique && key_column >= 0) {
        nmo_engine_cell_t key;
        array_input_key(b, array, key_column, key_column, &key);
        if (nmo_engine_array_find_row(engine, array, key_column, &key, 0) != -1) {
            IN_OFF(b, 0);
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
    }
    int32_t row = -1;
    if (nmo_engine_array_insert_row(engine, array, -1, &row) == NMO_OK) {
        array_read_inputs(b, array, row, 0);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Iterator (ArrayIterator) */
static int bb_array_iterator(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t count = nmo_engine_array_row_count(nmo_engine_block_engine(b), array);
    int32_t starting = get_in_int(b, 0, 0);
    if (starting >= count) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    int32_t ending = get_in_int(b, 1, -1);
    if (ending == -1) {
        ending = count - 1;
    }
    bool backward = get_in_int(b, 2, 0) != 0;
    int32_t index;
    bool first = false;
    if (IN_ACTIVE(b, 0)) {
        first = true;
        IN_OFF(b, 0);
        index = backward ? ending : starting;
        set_out_int(b, 0, index);
    } else {
        IN_OFF(b, 1);
        index = get_out_int(b, 0) + (backward ? -1 : 1);
        if (index <= -1) {
            index = count - 1;
        } else if (index >= count) {
            index = 0;
        }
        set_out_int(b, 0, index);
    }
    if (backward) {
        ending = starting - 1;
        if (ending == -1) {
            ending = count - 1;
        }
    } else if (++ending >= count) {
        ending = 0;
    }
    if ((!first || count == 0) && index == ending) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, index, 1);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* Clear Array (ArrayClear) */
static int bb_clear_array(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_array_clear(nmo_engine_block_engine(b), array);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Remove Key Row (RemoveArray): the row whose key equals the input. */
static int bb_remove_key_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (nmo_engine_block_input_parameter_count(b) < 1 || key_column < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_engine_cell_t key;
    array_input_key(b, array, key_column, 0, &key);
    int32_t row = nmo_engine_array_find_row(engine, array, key_column, &key, 0);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_remove_row(engine, array, row);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Iterator If (ArrayIteratorIf): the rows whose cell in a column passes a test. */
static int bb_array_iterator_if(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t index;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
    } else {
        IN_OFF(b, 1);
        index = get_out_int(b, 0) + 1;
    }
    if (index >= nmo_engine_array_row_count(engine, array)) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    int32_t column = get_in_int(b, 0, 0);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 2, &key);
    int32_t found = nmo_engine_array_find_row_op(engine, array, column, op, &key, index);
    if (found < 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, found, 1);
    set_out_int(b, 0, found);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* Get Highest (ArrayGetHighest) */
static int bb_get_highest(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = nmo_engine_array_highest_row(nmo_engine_block_engine(b), array,
                                               get_in_int(b, 0, 0));
    set_out_int(b, 0, row);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 1);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ---- Logics/Groups ---- */

static int bb_is_in_group(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    IN_OFF(b, 0);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 1);
    bool member = false;
    if (eng_is_child_class(engine, eng_object_class(engine, object), NMO_CID_BEOBJECT)) {
        int32_t count = nmo_engine_group_count(engine, group);
        for (int32_t i = 0; !member && i < count; i++) {
            member = nmo_engine_group_object(engine, group, i) == object;
        }
    }
    OUT_ON(b, member ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_group_iterator(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    int32_t count = nmo_engine_group_count(engine, group);
    if (count < 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    /* The index is the Index output, or a local in the version without it */
    bool index_output = !nmo_guid_is_null(nmo_engine_block_output_parameter_type(b, 1));
    int32_t index = index_output ? get_out_int(b, 1) : get_local_int(b, 0);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
    } else {
        IN_OFF(b, 1);
        ++index;
    }
    if (index >= count) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t element = (uint32_t)nmo_engine_group_object(engine, group, index);
    nmo_engine_block_set_output_value(b, 0, &element, sizeof(element));
    if (index_output) {
        set_out_int(b, 1, index);
    } else {
        set_local_int(b, 0, index);
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Registration
 * ============================================================================ */

static const nmo_engine_block_desc_t eng_logics_blocks[] = {
    {NMO_GUID(0x2d5d6d01, 0x6a353eb0), "Op", bb_op, NULL, NULL},
    {NMO_GUID(0x15151652, 0xaeefffd5), "Identity", bb_identity, NULL, NULL},
    {NMO_GUID(0x0c622386, 0x1c3054f7), "Random", bb_random, NULL, NULL},
    {NMO_GUID(0xeb506901, 0x984afccc), "Binary Switch", bb_binary_switch, NULL, NULL},
    {NMO_GUID(0x302561c4, 0x0d282980), "Nop", bb_nop, NULL, NULL},
    {NMO_GUID(0x17d66d26, 0x726b7dec), "Test", bb_test, NULL, NULL},
    {NMO_GUID(0x63eb2b0c, 0x27e2767e), "Parameter Selector", bb_parameter_selector, NULL, NULL},
    {NMO_GUID(0x4c42aace, 0x1da45635), "Switch On Parameter", bb_switch_on_parameter, NULL, NULL},
    {NMO_GUID(0x42530844, 0x257b6053), "Sequencer", bb_sequencer, cb_sequencer, NULL},
    {NMO_GUID(0xd02d67dd, 0x10211fdd), "Binary Memory", bb_binary_memory, NULL, NULL},
    {NMO_GUID(0xc044a999, 0xfdfefaf7), "Wait For All", bb_wait_for_all, NULL, NULL},
    {NMO_GUID(0x1f0b52bf, 0x4c3342dd), "Streaming Event", bb_streaming_event, cb_streaming_event,
     NULL},
    {NMO_GUID(0x7160133a, 0x1f2532fe), "Keep Active", bb_keep_active, NULL, NULL},
    {NMO_GUID(0x271b61af, 0x41b44d43), "One At A Time", bb_one_at_a_time, cb_one_at_a_time, NULL},
    {NMO_GUID(0x7ebc16ce, 0x75ba1301), "All But One", bb_all_but_one, NULL, NULL},
    {NMO_GUID(0x79d72fde, 0x2e9d0912), "Random Switch", bb_random_switch, NULL, NULL},
    {NMO_GUID(0x15d472a5, 0x3bea409f), "Delayer", bb_delayer, NULL, NULL},
    {NMO_GUID(0xa2a5a63a, 0xe4e7e8e5), "Timer", bb_timer, cb_timer, NULL},
    {NMO_GUID(0x998f000f, 0xf000f899), "Counter", bb_counter, cb_counter, NULL},
    {NMO_GUID(0xfff45680, 0xaa512a39), "Linear Progression", bb_linear_progression,
     cb_linear_progression, NULL},
    {NMO_GUID(0x4587ffee, 0x4587ffdd), "Wait Message", bb_wait_message, NULL, NULL},
    {NMO_GUID(0xa20e8d5b, 0xdf002150), "Send Message", bb_send_message, NULL, NULL},
    {NMO_GUID(0x3d6c4ae1, 0x72ae2cd6), "Broadcast Message", bb_broadcast_message, NULL, NULL},
    {NMO_GUID(0x1bb23f1d, 0x17ff14b9), "Switch On Message", bb_switch_on_message,
     cb_switch_on_message, NULL},
    {NMO_GUID(0x33b99f51, 0x07d95c45), "Get Cell", bb_get_cell, NULL, NULL},
    {NMO_GUID(0x30ed1c6d, 0x4a3b7067), "Set Cell", bb_set_cell, NULL, NULL},
    {NMO_GUID(0x33b77f41, 0x07b95c45), "Get Row", bb_get_row, NULL, NULL},
    {NMO_GUID(0x62e87901, 0x2df007dd), "Set Row", bb_set_row, NULL, NULL},
    {NMO_GUID(0x1c7e5dc6, 0x3f6423c2), "Add Row", bb_add_row, NULL, NULL},
    {NMO_GUID(0x198f0af9, 0x0268249f), "Iterator", bb_array_iterator, NULL, NULL},
    {NMO_GUID(0x6bec4be6, 0x12d64c7c), "Iterator If", bb_array_iterator_if, NULL, NULL},
    {NMO_GUID(0x35c9352f, 0x7b1a193b), "Clear Array", bb_clear_array, NULL, NULL},
    {NMO_GUID(0x0f8334ea, 0x279a40cd), "Remove Key Row", bb_remove_key_row, NULL, NULL},
    {NMO_GUID(0x6050252f, 0x3aa82d40), "Group Iterator", bb_group_iterator, NULL, NULL},
    {NMO_GUID(0x46f71e13, 0x13d26f61), "Get Highest", bb_get_highest, NULL, NULL},
    {NMO_GUID(0x58a1210b, 0x14e5715c), "Is In Group", bb_is_in_group, NULL, NULL},
    {NMO_GUID(0x448e54ce, 0x75a655c5), "Per Second", bb_per_second, NULL, NULL},
    {NMO_GUID(0x655e6af4, 0x08c0596f), "Threshold", bb_threshold, NULL, NULL},
    {NMO_GUID(0x35503950, 0x2dde7a65), "Interpolator", bb_interpolator_float, cb_interpolator,
     NULL},
    {NMO_GUID(0x6e800755, 0x57b64acb), "Set Component", bb_set_component, NULL, NULL},
};

nmo_status_t eng_register_logics(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_logics_blocks) / sizeof(eng_logics_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_logics_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
