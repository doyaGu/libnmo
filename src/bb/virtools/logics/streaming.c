/**
 * @file streaming.c
 * @brief Ports of the Logics/Streaming and Logics/Synchro building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

#include "type/nmo_type_guids.h"

#include <string.h>

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
        int32_t current = get_local_int(b, 0, 0);
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
    int32_t lao = get_local_int(b, 0, 0);
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
    if (!get_local_int(b, 0, 0)) {
        for (int a = 1; a < count; a++) {
            if (IN_ACTIVE(b, a)) {
                OUT_ON(b, a);
            }
        }
    } else {
        int32_t last = get_local_int(b, 1, 0);
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
        OUT_ON(b, get_local_int(b, 0, 0));
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
    bool forbid_twice = get_local_int(b, 0, 0) != 0;
    int32_t old_out = forbid_twice ? get_local_int(b, 1, 0) : -1;
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

static const nmo_engine_block_desc_t eng_logics_streaming_blocks[] = {
    {NMO_GUID_INIT(0xeb506901, 0x984afccc), "Binary Switch", bb_binary_switch, NULL, NULL},
    {NMO_GUID_INIT(0x302561c4, 0x0d282980), "Nop", bb_nop, NULL, NULL},
    {NMO_GUID_INIT(0x63eb2b0c, 0x27e2767e), "Parameter Selector", bb_parameter_selector, NULL, NULL},
    {NMO_GUID_INIT(0x4c42aace, 0x1da45635), "Switch On Parameter", bb_switch_on_parameter, NULL, NULL},
    {NMO_GUID_INIT(0x42530844, 0x257b6053), "Sequencer", bb_sequencer, cb_sequencer, NULL},
    {NMO_GUID_INIT(0xd02d67dd, 0x10211fdd), "Binary Memory", bb_binary_memory, NULL, NULL},
    {NMO_GUID_INIT(0xc044a999, 0xfdfefaf7), "Wait For All", bb_wait_for_all, NULL, NULL},
    {NMO_GUID_INIT(0x1f0b52bf, 0x4c3342dd), "Streaming Event", bb_streaming_event, cb_streaming_event,
     NULL},
    {NMO_GUID_INIT(0x7160133a, 0x1f2532fe), "Keep Active", bb_keep_active, NULL, NULL},
    {NMO_GUID_INIT(0x271b61af, 0x41b44d43), "One At A Time", bb_one_at_a_time, cb_one_at_a_time, NULL},
    {NMO_GUID_INIT(0x7ebc16ce, 0x75ba1301), "All But One", bb_all_but_one, NULL, NULL},
    {NMO_GUID_INIT(0x79d72fde, 0x2e9d0912), "Random Switch", bb_random_switch, NULL, NULL},
};

nmo_status_t eng_register_logics_streaming(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_streaming_blocks);
}
