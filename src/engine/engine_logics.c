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

#include <ctype.h>
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
    if (get_local_int(b, 0) == 0) {
        nmo_engine_block_set_function(b, bb_bezier_progression_frames);
    } else if (nmo_engine_block_version(b) < 0x00010005u &&
               !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_TIME)) {
        nmo_engine_block_set_function(b, bb_bezier_progression_int);
    } else {
        nmo_engine_block_set_function(b, bb_bezier_progression);
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

/* Get Delta Time: the frame time, in milliseconds (an integer before version 2). */
static int bb_get_delta_time(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    set_out_float(b, 0, nmo_engine_block_delta_time(b));
    return NMO_ENGINE_BR_OK;
}

static int bb_get_delta_time_old(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    set_out_int(b, 0, (int32_t)nmo_engine_block_delta_time(b));
    return NMO_ENGINE_BR_OK;
}

static int cb_get_delta_time(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(b, nmo_engine_block_version(b) == 0x00020000u
                                             ? bb_get_delta_time
                                             : bb_get_delta_time_old);
    }
    return NMO_ENGINE_BR_OK;
}

/* Mini Calculator: a op b, the operator a Binary Operator or the first character of a string. */
static int bb_mini_calculator(blk_t *b)
{
    float a = get_in_float(b, 1, 0.0f), c = get_in_float(b, 2, 0.0f);
    int32_t op = 1; /* CKADD */
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_STRING)) {
        size_t size = 0;
        const char *text = (const char *)nmo_engine_block_input_data(b, 0, &size);
        char sign = text != NULL && size > 0 ? text[0] : '\0';
        op = sign == '+' ? 1 : sign == '-' ? 2 : sign == '*' ? 3 : sign == '/' ? 4 : 0;
    } else {
        op = get_in_int(b, 0, 1);
    }
    float x = 0.0f; /* CK2 leaves it unset for another operator */
    switch (op) {
    case 1: x = a + c; break;
    case 2: x = a - c; break;
    case 3: x = a * c; break;
    case 4: x = a / c; break;
    default: break;
    }
    set_out_float(b, 0, x);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Calculator: the arithmetic expression of the first input on the others, 'a'
 * the second input, 'b' the third, and so on (Calculator.cpp). The
 * expression is read as CK reads it: upper-cased, spaces removed, made into
 * a postfix node stack with the operators' precedences, then evaluated from
 * its top. A syntax error writes no output; an illegal operation writes what
 * the evaluation got to. CK reads an integer input's bytes as a float.
 */
enum {
    CALC_ADD = 1, CALC_SUB, CALC_MULT, CALC_DIV, CALC_MOD,
    CALC_POW, CALC_OPEN, CALC_CLOSE, CALC_SIN, CALC_COS,
    CALC_ABS, CALC_LN, CALC_EXP, CALC_ATAN, CALC_SQRT,
    CALC_ACOS, CALC_ASIN, CALC_XCOMP, CALC_YCOMP, CALC_ZCOMP,
    CALC_TAN
};

typedef struct calc_op {
    int token;
    const char *tag;
    size_t length;
    int precedence;
} calc_op_t;

/* op_signification, in its order (the first tag a string starts with wins) */
static const calc_op_t calc_ops[] = {
    {CALC_ADD, "+", 1, 2},       {CALC_SUB, "-", 1, 3},       {CALC_MULT, "*", 1, 4},
    {CALC_DIV, "/", 1, 5},       {CALC_OPEN, "(", 1, 0},      {CALC_CLOSE, ")", 1, 256},
    {CALC_SIN, "SIN(", 4, 0},    {CALC_COS, "COS(", 4, 0},    {CALC_TAN, "TAN(", 4, 0},
    {CALC_XCOMP, ".X", 2, 7},    {CALC_YCOMP, ".Y", 2, 7},    {CALC_ZCOMP, ".Z", 2, 7},
    {CALC_ABS, "ABS(", 4, 0},    {CALC_SQRT, "SQRT(", 5, 0},  {CALC_ASIN, "ASIN(", 5, 0},
    {CALC_ACOS, "ACOS(", 5, 0},  {CALC_ATAN, "ATAN(", 5, 0},  {CALC_POW, "^", 1, 6},
    {CALC_LN, "LN(", 3, 0},      {CALC_EXP, "EXP(", 4, 0},    {CALC_MOD, "\\", 1, 5},
};

enum { CALC_NODE_OP = 1, CALC_FLOAT, CALC_VECTOR, CALC_PIN_FLOAT, CALC_PIN_VECTOR };

typedef struct calc_node {
    int type;
    int op;   /* CALC_NODE_OP */
    int pin;  /* CALC_PIN_*: the input; -1 for one the block does not have */
    float v[3];
} calc_node_t;

/* The parse (CreateNodeStack's globals), its stacks sized for the expression */
typedef struct calc {
    blk_t *b;
    calc_node_t *nodes;
    int node_count;
    int *ops;
    int op_count;
    int parens;
    char *token;
    bool cross_fixed; /* version 2's corrected cross product */
} calc_t;

#define CALC_SUCCESS 0
#define CALC_CREATION_ERROR (-1)
#define CALC_RUNTIME_ERROR (-2)

static const calc_op_t *calc_get_op(const char *str)
{
    for (size_t i = 0; i < sizeof(calc_ops) / sizeof(calc_ops[0]); i++) {
        if (strncmp(str, calc_ops[i].tag, calc_ops[i].length) == 0) {
            return &calc_ops[i];
        }
    }
    return NULL;
}

static int calc_precedence(int token)
{
    for (size_t i = 0; i < sizeof(calc_ops) / sizeof(calc_ops[0]); i++) {
        if (calc_ops[i].token == token) {
            return calc_ops[i].precedence;
        }
    }
    return 0;
}

static void calc_push_op(calc_t *c, int op)
{
    if (calc_precedence(op) == 0) {
        c->parens++;
    }
    c->ops[c->op_count++] = op;
}

static void calc_push_value(calc_t *c, float value)
{
    calc_node_t *node = &c->nodes[c->node_count++];
    memset(node, 0, sizeof(*node));
    node->type = CALC_FLOAT;
    node->v[0] = value;
}

/* PushOpInNodeStack: the last operation into the node stack, but '(' */
static int calc_pop_op_to_nodes(calc_t *c)
{
    if (c->op_count == 0) {
        return CALC_CREATION_ERROR;
    }
    int op = c->ops[--c->op_count];
    if (op != CALC_OPEN) {
        calc_node_t *node = &c->nodes[c->node_count++];
        memset(node, 0, sizeof(*node));
        node->type = CALC_NODE_OP;
        node->op = op;
    }
    return op;
}

/* DoParenthesis: operations into the node stack down to the opening one */
static int calc_close_parenthesis(calc_t *c)
{
    if (1 > c->parens--) {
        return CALC_CREATION_ERROR;
    }
    int op;
    do {
        op = calc_pop_op_to_nodes(c);
        if (op < CALC_SUCCESS) {
            break;
        }
    } while (calc_precedence(op) != 0);
    return op;
}

/*
 * GetExpression: the operation `str` starts with, or the operand before the
 * next operation (a '-' starts a number, or an exponent's sign). A '-' that
 * negates a non-number reads as 0 minus it.
 */
static const char *calc_get_expression(calc_t *c, const char *str)
{
    if (strncmp(str, "PI", 2) == 0) {
        return "PI";
    }
    const char *ptr = str;
    char *out = c->token;
    while (*ptr) {
        const calc_op_t *op = calc_get_op(ptr);
        if (op != NULL) {
            if (*ptr == '-') {
                if (str != ptr && (ptr[-1] != 'E' || str == ptr - 1)) {
                    break;
                }
                if (str == ptr && !isdigit((unsigned char)ptr[1]) && ptr[1] != '.') {
                    calc_push_value(c, 0.0f);
                    return op->tag;
                }
            } else if (str == ptr) {
                return op->tag;
            } else {
                break;
            }
        }
        *out++ = *ptr++;
    }
    *out = '\0';
    return c->token;
}

/* CreateNodeStack on the upper-cased expression without spaces */
static int calc_parse(calc_t *c, const char *line)
{
    const char *ptr = line;
    int state = 0; /* 0 expecting an operand, 1 an operation */
    while (*ptr) {
        if (state == 0) {
            const char *str = calc_get_expression(c, ptr);
            const calc_op_t *op = calc_get_op(str);
            if (op != NULL && strlen(str) == op->length) {
                calc_push_op(c, op->token);
                ptr += op->length;
                continue;
            }
            if (strcmp(str, "-") == 0) {
                calc_push_op(c, CALC_SUB);
                ++ptr;
                continue;
            }
            calc_node_t *node = &c->nodes[c->node_count];
            memset(node, 0, sizeof(*node));
            if (str[1] == '\0' && str[0] >= 'A' && str[0] <= 'Z') {
                /* An input by its letter */
                int pin = str[0] - 'A' + 1;
                node->type = CALC_FLOAT;
                node->pin = -1;
                if (pin < nmo_engine_block_input_parameter_count(c->b)) {
                    nmo_guid_t type = nmo_engine_block_input_parameter_type(c->b, pin);
                    if (nmo_guid_equals(type, CKPGUID_INT) || nmo_guid_equals(type, CKPGUID_FLOAT) ||
                        nmo_guid_equals(type, CKPGUID_PERCENTAGE)) {
                        node->type = CALC_PIN_FLOAT;
                    } else if (nmo_guid_equals(type, CKPGUID_VECTOR)) {
                        node->type = CALC_PIN_VECTOR;
                    } else {
                        return CALC_CREATION_ERROR;
                    }
                    node->pin = pin;
                }
                c->node_count++;
                ++ptr;
                state = 1;
                continue;
            }
            node->type = CALC_FLOAT;
            if (strcmp(str, "PI") == 0) {
                node->v[0] = 3.1415926535f;
            } else {
                node->v[0] = (float)strtod(str, NULL);
                if (node->v[0] == 0.0f && strchr(str, '0') == NULL) {
                    return CALC_CREATION_ERROR;
                }
            }
            c->node_count++;
            ptr += strlen(str);
            state = 1;
        } else {
            const calc_op_t *op = calc_get_op(ptr);
            if (op == NULL) {
                return CALC_CREATION_ERROR;
            }
            if (*ptr == ')') {
                int status = calc_close_parenthesis(c);
                if (status < CALC_SUCCESS) {
                    return status;
                }
            } else {
                while (c->op_count > 0 && op->precedence <= calc_precedence(c->ops[c->op_count - 1])) {
                    int status = calc_pop_op_to_nodes(c);
                    if (status < CALC_SUCCESS) {
                        return status;
                    }
                }
                calc_push_op(c, op->token);
                if (op->token != CALC_XCOMP && op->token != CALC_YCOMP && op->token != CALC_ZCOMP) {
                    state = 0;
                }
            }
            ptr += op->length;
        }
    }
    while (c->op_count > 0) {
        int status = calc_pop_op_to_nodes(c);
        if (status < CALC_SUCCESS) {
            return status;
        }
    }
    return CALC_SUCCESS;
}

/* EvalNode: the node on top of the stack, popping it and what it operates on */
static int calc_eval(calc_t *c, float *val, int *type)
{
    if (c->node_count == 0) {
        return CALC_RUNTIME_ERROR;
    }
    const calc_node_t *node = &c->nodes[--c->node_count];
    switch (node->type) {
    case CALC_FLOAT:
        val[0] = node->v[0];
        *type = CALC_FLOAT;
        return CALC_SUCCESS;
    case CALC_PIN_FLOAT:
        nmo_engine_block_get_input_value(c->b, node->pin, val, sizeof(float));
        *type = CALC_FLOAT;
        return CALC_SUCCESS;
    case CALC_PIN_VECTOR:
        nmo_engine_block_get_input_value(c->b, node->pin, val, 3u * sizeof(float));
        *type = CALC_VECTOR;
        return CALC_SUCCESS;
    case CALC_NODE_OP:
        break;
    default:
        return CALC_RUNTIME_ERROR;
    }

    int op = node->op;
    *type = CALC_FLOAT;
    float a[3] = {0.0f, 0.0f, 0.0f}, l[3] = {0.0f, 0.0f, 0.0f}; /* right, then left operand */
    int ta = 0, tl = 0;
    if (op == CALC_OPEN || op == CALC_CLOSE || calc_eval(c, a, &ta) != CALC_SUCCESS) {
        return CALC_RUNTIME_ERROR;
    }
    bool binary = op == CALC_ADD || op == CALC_SUB || op == CALC_MULT || op == CALC_DIV ||
                  op == CALC_POW || op == CALC_MOD;
    if (binary && calc_eval(c, l, &tl) != CALC_SUCCESS) {
        return CALC_RUNTIME_ERROR;
    }
    switch (op) {
    case CALC_XCOMP:
    case CALC_YCOMP:
    case CALC_ZCOMP:
        if (ta != CALC_VECTOR) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = a[op - CALC_XCOMP];
        return CALC_SUCCESS;
    case CALC_ADD:
    case CALC_SUB:
        val[0] = op == CALC_ADD ? l[0] + a[0] : l[0] - a[0];
        if (ta != tl) {
            return CALC_RUNTIME_ERROR;
        }
        if (ta == CALC_VECTOR) {
            for (int k = 1; k < 3; k++) {
                val[k] = op == CALC_ADD ? l[k] + a[k] : l[k] - a[k];
            }
            *type = CALC_VECTOR;
        }
        return CALC_SUCCESS;
    case CALC_MULT:
        if (ta == tl) {
            /* a product, or vectors' dot product */
            val[0] = ta == CALC_FLOAT ? l[0] * a[0] : l[0] * a[0] + l[1] * a[1] + l[2] * a[2];
            return CALC_SUCCESS;
        }
        *type = CALC_VECTOR;
        for (int k = 0; k < 3; k++) {
            val[k] = ta == CALC_FLOAT ? a[0] * l[k] : l[0] * a[k];
        }
        return CALC_SUCCESS;
    case CALC_DIV:
        if (a[0] == 0.0f || (ta == CALC_VECTOR && (a[1] == 0.0f || a[2] == 0.0f))) {
            return CALC_RUNTIME_ERROR;
        }
        if (ta == tl) {
            /* a quotient, or the sum of the vectors' component quotients */
            val[0] = ta == CALC_FLOAT ? l[0] / a[0] : l[0] / a[0] + l[1] / a[1] + l[2] / a[2];
            return CALC_SUCCESS;
        }
        *type = CALC_VECTOR;
        for (int k = 0; k < 3; k++) {
            val[k] = ta == CALC_FLOAT ? l[k] / a[0] : l[0] / a[k];
        }
        return CALC_SUCCESS;
    case CALC_POW:
        if (ta == CALC_VECTOR && tl == CALC_VECTOR) {
            /* the cross product: left ^ right since version 2, the other way before */
            const float *p = c->cross_fixed ? l : a, *q = c->cross_fixed ? a : l;
            val[0] = p[1] * q[2] - q[1] * p[2];
            val[1] = q[0] * p[2] - p[0] * q[2];
            val[2] = p[0] * q[1] - q[0] * p[1];
            *type = CALC_VECTOR;
            return CALC_SUCCESS;
        }
        val[0] = powf(l[0], a[0]);
        return CALC_SUCCESS;
    case CALC_MOD:
        if (a[0] == 0.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = fmodf(l[0], a[0]);
        return CALC_SUCCESS;
    case CALC_ABS:
        val[0] = fabsf(a[0]);
        if (ta == CALC_VECTOR) {
            val[1] = fabsf(a[1]);
            val[2] = fabsf(a[2]);
            *type = CALC_VECTOR;
        }
        return CALC_SUCCESS;
    case CALC_SIN: val[0] = sinf(a[0]); return CALC_SUCCESS;
    case CALC_COS: val[0] = cosf(a[0]); return CALC_SUCCESS;
    case CALC_TAN: val[0] = tanf(a[0]); return CALC_SUCCESS;
    case CALC_ATAN: val[0] = atanf(a[0]); return CALC_SUCCESS;
    case CALC_EXP: val[0] = expf(a[0]); return CALC_SUCCESS;
    case CALC_SQRT:
        if (a[0] < 0.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = sqrtf(a[0]);
        return CALC_SUCCESS;
    case CALC_ASIN:
    case CALC_ACOS:
        if (a[0] > 1.0f || a[0] < -1.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = op == CALC_ASIN ? asinf(a[0]) : acosf(a[0]);
        return CALC_SUCCESS;
    case CALC_LN:
        if (a[0] <= 0.0f) {
            return CALC_RUNTIME_ERROR;
        }
        val[0] = logf(a[0]);
        return CALC_SUCCESS;
    default:
        return CALC_RUNTIME_ERROR;
    }
}

static int bb_calculator(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    size_t size = 0;
    const char *expression = (const char *)nmo_engine_block_input_data(b, 0, &size);
    size_t length = expression != NULL ? strnlen(expression, size) : 0u;
    /* Each character makes at most an operation and two nodes (a '-' its 0) */
    calc_t c;
    memset(&c, 0, sizeof(c));
    c.b = b;
    c.cross_fixed = nmo_engine_block_version(b) >= 0x00020000u;
    char *line = (char *)malloc(length + 1u);
    c.token = (char *)malloc(length + 1u);
    c.nodes = (calc_node_t *)malloc((2u * length + 1u) * sizeof(calc_node_t));
    c.ops = (int *)malloc((length + 1u) * sizeof(int));
    if (line == NULL || c.token == NULL || c.nodes == NULL || c.ops == NULL) {
        free(line);
        free(c.token);
        free(c.nodes);
        free(c.ops);
        return NMO_ENGINE_BR_GENERICERROR;
    }
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        if (!isspace((unsigned char)expression[i])) {
            line[n++] = (char)toupper((unsigned char)expression[i]);
        }
    }
    line[n] = '\0';
    if (calc_parse(&c, line) == CALC_SUCCESS && c.node_count > 0) {
        float x[3] = {0.0f, 0.0f, 0.0f};
        int type = 0;
        calc_eval(&c, x, &type); /* "Illegal Operation In CALCULATOR": x as it got */
        bool vector = nmo_guid_equals(nmo_engine_block_output_parameter_type(b, 0), CKPGUID_VECTOR);
        nmo_engine_block_set_output_value(b, 0, x, vector ? sizeof(x) : sizeof(x[0]));
    }
    free(line);
    free(c.token);
    free(c.nodes);
    free(c.ops);
    return NMO_ENGINE_BR_OK;
}

/*
 * Get Component: the components of a Color, Vector, Euler Angles or 2D
 * Vector, and a Rectangle or Box as corners, minimum and size, or center and
 * half size by its "Rectangle/Box Mode" setting.
 */
static int bb_get_component(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_guid_t type = nmo_engine_block_input_parameter_type(b, 0);
    float v[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, v, sizeof(v));
    int components = 0;
    if (nmo_guid_equals(type, CKPGUID_COLOR)) {
        components = 4;
    } else if (nmo_guid_equals(type, CKPGUID_VECTOR) || nmo_guid_equals(type, CKPGUID_EULERANGLES)) {
        components = 3;
    } else if (nmo_guid_equals(type, CKPGUID_2DVECTOR)) {
        components = 2;
    }
    for (int i = 0; i < components; i++) {
        set_out_float(b, i, v[i]);
    }
    int32_t mode = 1;
    nmo_engine_block_get_local_value(b, 0, &mode, sizeof(mode));
    if (nmo_guid_equals(type, CKPGUID_RECT)) {
        /* left, top, right, bottom */
        float corner[2] = {v[0], v[1]}, other[2] = {v[2], v[3]};
        if (mode == 2) {
            other[0] = v[2] - v[0];
            other[1] = v[3] - v[1];
        } else if (mode == 3) {
            other[0] = (v[2] - v[0]) * 0.5f;
            other[1] = (v[3] - v[1]) * 0.5f;
            corner[0] = (v[0] + v[2]) * 0.5f;
            corner[1] = (v[1] + v[3]) * 0.5f;
        } else if (mode != 1) {
            corner[0] = corner[1] = other[0] = other[1] = 0.0f;
        }
        nmo_engine_block_set_output_value(b, 0, corner, sizeof(corner));
        nmo_engine_block_set_output_value(b, 1, other, sizeof(other));
    } else if (nmo_guid_equals(type, CKPGUID_BOX)) {
        /* VxBbox: Max, then Min */
        float first[3] = {v[3], v[4], v[5]}, second[3] = {v[0], v[1], v[2]};
        for (int k = 0; k < 3; k++) {
            if (mode == 2) {
                second[k] = v[k] - v[3 + k];
            } else if (mode == 3) {
                first[k] = (v[k] + v[3 + k]) * 0.5f;
                second[k] = (v[k] - v[3 + k]) * 0.5f;
            }
        }
        nmo_engine_block_set_output_value(b, 0, first, sizeof(first));
        nmo_engine_block_set_output_value(b, 1, second, sizeof(second));
    }
    return NMO_ENGINE_BR_OK;
}

/* A block's local parameter `pos` */
static eng_param_t *block_local(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine, node->first_local, node->local_count, pos);
}

/*
 * Variation: X less its value at the last In (kept in local 0), and Change
 * or No Change. Reset stores Reset Value as that value, without an output.
 */
static int bb_variation(blk_t *b)
{
    eng_param_t *old = block_local(b, 0);
    if (!IN_ACTIVE(b, 0)) {
        IN_OFF(b, 1);
        if (old != NULL && old->size > 0) {
            uint8_t *value = (uint8_t *)calloc(1, old->size);
            if (value == NULL) {
                return NMO_ENGINE_BR_GENERICERROR;
            }
            nmo_engine_block_get_input_value(b, 1, value, old->size);
            eng_param_write(b->engine, old, value, old->size);
            free(value);
        }
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    nmo_guid_t type = nmo_engine_block_output_parameter_type(b, 0);
    int floats = 0;
    bool old_on_change = true; /* the float case gives OldX on a change, the vectors on none */
    if (nmo_guid_equals(type, CKPGUID_FLOAT) || nmo_guid_equals(type, CKPGUID_PERCENTAGE) ||
        nmo_guid_equals(type, CKPGUID_ANGLE)) {
        floats = 1;
    } else if (nmo_guid_equals(type, CKPGUID_VECTOR)) {
        floats = 3;
        old_on_change = false;
    } else if (nmo_guid_equals(type, CKPGUID_2DVECTOR)) {
        floats = 2;
        old_on_change = false;
    } else if (nmo_guid_equals(type, CKPGUID_COLOR)) {
        floats = 4;
        old_on_change = false;
    }
    if (floats > 0) {
        float x[4] = {0}, previous[4] = {0};
        nmo_engine_block_get_input_value(b, 0, x, (size_t)floats * sizeof(float));
        nmo_engine_block_get_local_value(b, 0, previous, (size_t)floats * sizeof(float));
        nmo_engine_block_set_local_value(b, 0, x, (size_t)floats * sizeof(float));
        bool changed = false;
        for (int i = 0; i < floats; i++) {
            x[i] -= previous[i];
            changed = changed || x[i] != 0.0f;
        }
        nmo_engine_block_set_output_value(b, 0, x, (size_t)floats * sizeof(float));
        if (changed == old_on_change) {
            nmo_engine_block_set_output_value(b, 1, previous, (size_t)floats * sizeof(float));
        }
        OUT_ON(b, changed ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_guid_equals(type, CKPGUID_INT)) {
        int32_t x = get_in_int(b, 0, 0), previous = 0;
        nmo_engine_block_get_local_value(b, 0, &previous, sizeof(previous));
        nmo_engine_block_set_local_value(b, 0, &x, sizeof(x));
        x -= previous;
        set_out_int(b, 0, x);
        if (x != 0) {
            set_out_int(b, 1, previous);
        }
        OUT_ON(b, x != 0 ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    /* Any other type: a change of its bytes, the old value into OldX */
    size_t size = 0;
    const uint8_t *now = (const uint8_t *)nmo_engine_block_input_data(b, 0, &size);
    if (now == NULL || old == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    size_t compared = size < old->size ? size : old->size;
    if (compared > 0 && memcmp(now, old->data, compared) != 0) {
        size_t out_size = nmo_engine_block_output_parameter_size(b, 1);
        size_t copied = out_size < old->size ? out_size : old->size;
        if (copied > 0) {
            nmo_engine_block_set_output_value(b, 1, old->data, copied);
        }
        OUT_ON(b, 0);
    } else {
        OUT_ON(b, 1);
    }
    eng_param_write(b->engine, old, now, size);
    return NMO_ENGINE_BR_OK;
}

/* Bezier Transform: X through the curve, between the boundaries (a Rectangle, or 4 floats). */
static int bb_bezier_transform(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float x = get_in_float(b, 0, 0.0f);
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 1, &curve_size);
    if (curve == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float rect[4] = {1.0f, 0.0f, 0.0f, 1.0f}; /* left, top, right, bottom */
    int32_t four_floats = 0;
    nmo_engine_block_get_local_value(b, 0, &four_floats, sizeof(four_floats));
    if (four_floats) {
        rect[0] = get_in_float(b, 2, rect[0]);
        rect[2] = get_in_float(b, 3, rect[2]);
        rect[3] = get_in_float(b, 4, rect[3]);
        rect[1] = get_in_float(b, 5, rect[1]);
    } else {
        nmo_engine_block_get_input_value(b, 2, rect, sizeof(rect));
    }
    float y = rect[3] + (rect[1] - rect[3]) *
                           nmo_engine_curve_y(curve, curve_size, (x - rect[0]) / (rect[2] - rect[0]));
    set_out_float(b, 0, y);
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
/* Get Key Row (GetArray): the row whose key column holds the key, into the outputs. */
static int bb_get_key_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t key_column = nmo_engine_array_key_column(engine, array);
    if (key_column < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "No column criterion defined" */
    }
    nmo_engine_cell_t key;
    array_input_key(b, array, key_column, 0, &key);
    int32_t row = nmo_engine_array_find_row(engine, array, key_column, &key, 0);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    array_write_outputs(b, array, row, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Row Search (ArraySearch): the first row from First Search Row whose cell compares true. */
static int bb_row_search(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t column = get_in_int(b, 0, 0);
    int32_t first = get_in_int(b, 1, 0);
    int32_t op = get_in_int(b, 2, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 3, &key);
    int32_t row = nmo_engine_array_find_row_op(engine, array, column, op, &key, first);
    IN_OFF(b, 0);
    if (row < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    set_out_int(b, 0, row);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Shuffle Order (CKDataArray::RandomShuffle): each row swapped with a random one. */
static int bb_shuffle_order(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t rows = nmo_engine_array_row_count(engine, array);
    for (int32_t i = 0; i < rows; i++) {
        int32_t r = (int32_t)(nmo_engine_block_random(b) * 32768.0); /* rand() */
        nmo_engine_array_swap_rows(engine, array, i, r % rows);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Remove Row If (ArrayRemoveIf): every row whose cell compares true, and how many. */
static int bb_remove_row_if(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t column = get_in_int(b, 0, 0);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 2, &key);
    int32_t count = 0;
    for (int32_t i = 0; i != -1;) {
        i = nmo_engine_array_find_row_op(engine, array, column, op, &key, i);
        if (i != -1) {
            nmo_engine_array_remove_row(engine, array, i);
            count++;
            if (i >= nmo_engine_array_row_count(engine, array)) {
                break;
            }
        }
    }
    set_out_int(b, 0, count);
    IN_OFF(b, 0);
    OUT_ON(b, count == 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

/* Value Count (ArrayValueCount): how many cells of a column compare true. */
static int bb_value_count(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = get_in_int(b, 0, 1);
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 2, &key);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    set_out_int(b, 0, nmo_engine_array_count(nmo_engine_block_engine(b), array, column, op, &key));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Test Cell (ArrayTestRow): True when the cell compares true. In stays active, as in CK. */
static int bb_test_cell(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t row = get_in_int(b, 0, 0);
    int32_t column = get_in_int(b, 1, 0);
    int32_t op = get_in_int(b, 2, 1); /* CKEQUAL */
    nmo_engine_cell_t key;
    array_input_key(b, array, column, 3, &key);
    bool holds = nmo_engine_array_test_row(nmo_engine_block_engine(b), array, row, column, op, &key);
    OUT_ON(b, holds ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* Sort Rows (ArraySort): the rows by a column. */
static int bb_sort_rows(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_array_sort(nmo_engine_block_engine(b), array, get_in_int(b, 0, 0),
                          get_in_int(b, 1, 1) != 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Remove Row (ArrayRemoveLine): Removed, or Not Present for a row it does not have. */
static int bb_remove_row(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t row = get_in_int(b, 0, 0);
    IN_OFF(b, 0);
    if (row < 0 || row >= nmo_engine_array_row_count(engine, array)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_array_remove_row(engine, array, row);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Change Value If (ArrayChangeValueIf): New Value into every cell of a column that compares true. */
static int bb_change_value_if(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t column = get_in_int(b, 0, 0);
    int32_t op = get_in_int(b, 1, 1); /* CKEQUAL */
    nmo_engine_cell_t key, value;
    array_input_key(b, array, column, 2, &key);
    bool has_value = nmo_engine_block_input_parameter_count(b) > 3;
    if (has_value) {
        array_input_key(b, array, column, 3, &value);
    }
    int32_t count = 0;
    for (int32_t i = 0; i != -1;) {
        i = nmo_engine_array_find_row_op(engine, array, column, op, &key, i);
        if (i != -1) {
            if (has_value) {
                nmo_engine_array_set_cell(engine, array, i, column, &value);
            }
            i++;
            count++;
        }
    }
    set_out_int(b, 0, count);
    IN_OFF(b, 0);
    OUT_ON(b, count == 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_remove_column(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_array_remove_column(nmo_engine_block_engine(b), array, get_in_int(b, 0, -1));
    return NMO_ENGINE_BR_OK;
}

/*
 * Insert Column: a column of Column Type before Index (-1: after the last).
 * A parameter column takes the type of the Parameter Type input; a Parameter
 * Type value is CK's index of a type, which the engine cannot map, so such a
 * column holds no values.
 */
static int bb_insert_column(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 1, &size);
    if (name == NULL || size == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t type = get_in_int(b, 2, CKARRAYTYPE_INT);
    nmo_guid_t param_type = NMO_GUID(0u, 0u);
    if (type == CKARRAYTYPE_PARAMETER) {
        param_type = nmo_engine_block_input_parameter_type(b, 3);
        if (nmo_guid_equals(param_type, CKPGUID_PARAMETERTYPE)) {
            param_type = NMO_GUID(0u, 0u);
        }
    }
    nmo_engine_array_insert_column(nmo_engine_block_engine(b), array, get_in_int(b, 0, -1), type,
                                   param_type);
    return NMO_ENGINE_BR_OK;
}

/* Column Sum (ArraySum): the sum of an Integer or Float column. */
static int bb_column_sum(blk_t *b)
{
    nmo_object_id_t array = array_target(b);
    if (array == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t sum = nmo_engine_array_sum(nmo_engine_block_engine(b), array, get_in_int(b, 0, 0));
    nmo_engine_block_set_output_value(b, 0, &sum, sizeof(sum));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

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

/* Add To Group: the target into the group (CKGroup::AddObject). */
static int bb_add_to_group(blk_t *b)
{
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    if (nmo_engine_group_count(nmo_engine_block_engine(b), group) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_group_add(nmo_engine_block_engine(b), group, nmo_engine_block_target(b));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Fill Group By Class: the target group gets each object of the class (and
 * of the class after it, when that one derives from it), or of every class
 * deriving from it with Derived Classes; Only In Current Scene, those in
 * the level scene. In the context's lists' order; private objects are left
 * out (the engine has none).
 */
static int bb_fill_group_by_class(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t group = nmo_engine_block_target(b);
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t class_id = get_in_int(b, 0, 0);
    bool derived = get_in_int(b, 1, 0) != 0, in_scene = get_in_int(b, 2, 1) != 0;
    int32_t first = derived ? 0 : class_id, last = derived ? 55 : class_id + 1;
    for (int32_t c = first; c <= last; c++) {
        if (c < 0 || class_id < 0 ||
            !nmo_type_registry_is_class_derived_from(engine->types, (uint32_t)c, (uint32_t)class_id)) {
            continue;
        }
        nmo_object_id_t *ids = NULL;
        size_t count = eng_objects_of_class(engine, (uint32_t)c, &ids);
        for (size_t i = 0; i < count; i++) {
            if (!in_scene || eng_in_scene(engine, ids[i])) {
                eng_group_add(engine, group, ids[i]);
            }
        }
        free(ids);
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
        index = get_local_int(b, 0);
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
 * Load String: the text of a file, as read in text mode on Windows (CR LF
 * becomes LF), or File Error.
 */
static int bb_load_string(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    const char *name = input_string(b, 0);
    const char *path = name != NULL && engine->resolve_file != NULL
                           ? engine->resolve_file(engine->resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 0);
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : 0;
    char *text = size >= 0 && fseek(file, 0, SEEK_SET) == 0 ? (char *)malloc((size_t)size + 1u)
                                                              : NULL;
    size_t length = 0;
    if (text != NULL) {
        size_t read = fread(text, 1, (size_t)size, file);
        for (size_t i = 0; i < read; i++) {
            if (!(text[i] == '\r' && i + 1 < read && text[i + 1] == '\n')) {
                text[length++] = text[i];
            }
        }
        text[length] = '\0';
        nmo_engine_block_set_output_text(b, 0, text);
    }
    free(text);
    fclose(file);
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
    {NMO_GUID(0x6bb8699a, 0x29fb6a4b), "Bezier Progression", bb_bezier_progression,
     cb_bezier_progression, NULL},
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
    {NMO_GUID(0x49064205, 0x10e72f7a), "Get Key Row", bb_get_key_row, NULL, NULL},
    {NMO_GUID(0x78863443, 0x45af59b6), "Row Search", bb_row_search, NULL, NULL},
    {NMO_GUID(0x270f177a, 0x4ca20023), "Shuffle Order", bb_shuffle_order, NULL, NULL},
    {NMO_GUID(0x57865622, 0x662d2fee), "Remove Row If", bb_remove_row_if, NULL, NULL},
    {NMO_GUID(0x534377de, 0x75fd478a), "Value Count", bb_value_count, NULL, NULL},
    {NMO_GUID(0x4e6c6da8, 0x636904fc), "Test Cell", bb_test_cell, NULL, NULL},
    {NMO_GUID(0x6f623e68, 0x62bb5a98), "Sort Rows", bb_sort_rows, NULL, NULL},
    {NMO_GUID(0x1fa57136, 0x14310857), "Remove Row", bb_remove_row, NULL, NULL},
    {NMO_GUID(0x07253edb, 0x4d1237ed), "Change Value If", bb_change_value_if, NULL, NULL},
    {NMO_GUID(0x419602ec, 0x7822779e), "Collection Iterator", bb_collection_iterator, NULL, NULL},
    {NMO_GUID(0x391555d6, 0x42f2500e), "Load String", bb_load_string, NULL, NULL},
    {NMO_GUID(0x3f377888, 0x128a7767), "Remove Column", bb_remove_column, NULL, NULL},
    {NMO_GUID(0x05cb7802, 0x04a64a58), "Insert Column", bb_insert_column, NULL, NULL},
    {NMO_GUID(0x49124252, 0x1be909a4), "Column Sum", bb_column_sum, NULL, NULL},
    {NMO_GUID(0x6050252f, 0x3aa82d40), "Group Iterator", bb_group_iterator, NULL, NULL},
    {NMO_GUID(0x00024125, 0x785420ab), "Add To Group", bb_add_to_group, NULL, NULL},
    {NMO_GUID(0x46f71e13, 0x13d26f61), "Get Highest", bb_get_highest, NULL, NULL},
    {NMO_GUID(0x58a1210b, 0x14e5715c), "Is In Group", bb_is_in_group, NULL, NULL},
    {NMO_GUID(0x448e54ce, 0x75a655c5), "Per Second", bb_per_second, NULL, NULL},
    {NMO_GUID(0x47dc3232, 0x64bf203a), "Get Delta Time", bb_get_delta_time, cb_get_delta_time,
     NULL},
    {NMO_GUID(0x55bc3115, 0x1dfe1e40), "Mini Calculator", bb_mini_calculator, NULL, NULL},
    {NMO_GUID(0x4445257b, 0x70016c57), "Fill Group By Class", bb_fill_group_by_class, NULL, NULL},
    {NMO_GUID(0x4bc209b8, 0x5b3679b0), "Calculator", bb_calculator, NULL, NULL},
    {NMO_GUID(0x2689057b, 0x1a1a446a), "Get Component", bb_get_component, NULL, NULL},
    {NMO_GUID(0x3e7335d9, 0x29687f36), "Variation", bb_variation, NULL, NULL},
    {NMO_GUID(0x6d433e1b, 0x191954b4), "Bezier Transform", bb_bezier_transform, NULL, NULL},
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
