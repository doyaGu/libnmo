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
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

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
