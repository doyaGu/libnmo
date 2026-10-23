/**
 * @file engine_controllers.c
 * @brief Ports of the keyboard and mouse building blocks.
 *
 * Each function does what the block's function in Controllers.dll (or
 * TT_Toolbox_RT.dll) does, reading the input state the host gives the engine
 * (nmo_engine_set_key() and the mouse functions) in place of the input
 * manager. There is no window: an action counts as inside the 3D window.
 */

#include "engine_internal.h"

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

static int32_t get_out_int(blk_t *b, int pos)
{
    int32_t value = 0;
    nmo_engine_block_get_output_value(b, pos, &value, sizeof(value));
    return value;
}

static int32_t get_local_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static void set_out_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_output_value(b, pos, &value, sizeof(value));
}

static bool key_down(blk_t *b, uint32_t key)
{
    return nmo_engine_is_key_down(nmo_engine_block_engine(b), key);
}

/* ---- Controllers/Keyboard ---- */

/* Key Event: Pressed when every key waited is down, Released when one goes up. */
static int bb_key_event(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_out_int(b, 0, 0);
    }
    bool state = get_out_int(b, 0) != 0;
    int count = nmo_engine_block_input_parameter_count(b);
    int i = 0;
    for (; i < count; i++) {
        if (!key_down(b, (uint32_t)get_in_int(b, i, 0))) {
            break;
        }
    }
    if (i != count && state) {
        state = false;
        OUT_ON(b, 1);
    } else if (i == count && !state) {
        state = true;
        OUT_ON(b, 0);
    }
    set_out_int(b, 0, state ? 1 : 0);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Key Waiter (and TT_Key Waiter): out once the key is down. */
static int bb_key_waiter(blk_t *b)
{
    if (key_down(b, (uint32_t)get_in_int(b, 0, 0))) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ReadKey: "Wait For Any Key", out when the first key of the buffer went down. */
static int bb_read_key(blk_t *b)
{
    uint32_t key = 0;
    bool pressed = false;
    if (nmo_engine_key_buffer_get(nmo_engine_block_engine(b), 0, &key, &pressed) && pressed) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        nmo_engine_block_set_output_value(b, 0, &key, sizeof(key));
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_key_waiter(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(b, get_local_int(b, 0, 0) ? bb_read_key : bb_key_waiter);
    }
    return NMO_ENGINE_BR_OK;
}

/* Switch On Key: each output whose key is down, every frame until Off. */
static int bb_switch_on_key(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int count = nmo_engine_block_input_parameter_count(b);
    if (count != nmo_engine_block_output_count(b)) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    for (int i = 0; i < count; i++) {
        if (key_down(b, (uint32_t)get_in_int(b, i, 0))) {
            OUT_ON(b, i);
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ---- Controllers/Mouse ---- */

/* The outputs Mouse Waiter has, by its "Outputs" setting, in this order. */
enum {
    MW_MOVE = 1,
    MW_LEFT_DOWN = 2,
    MW_LEFT_UP = 4,
    MW_RIGHT_DOWN = 8,
    MW_RIGHT_UP = 16,
    MW_MIDDLE_DOWN = 32,
    MW_MIDDLE_UP = 64,
    MW_ROLL = 128,
};

static int bb_mouse_waiter(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    bool stay_active = get_local_int(b, 0, 1) != 0;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    if (stay_active && IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }

    float old_position[2] = {0}, position[2];
    nmo_engine_block_get_output_value(b, 0, old_position, sizeof(old_position));
    nmo_engine_mouse_position(engine, &position[0], &position[1]);
    nmo_engine_block_set_output_value(b, 0, position, sizeof(position));
    int32_t outputs = get_local_int(b, 1, 0xffff);

    float wheel = 0.0f;
    nmo_engine_mouse_relative(engine, NULL, NULL, &wheel);
    nmo_engine_block_set_output_value(b, 1, &wheel, sizeof(wheel));
    /* Left, middle, right: the output parameters 2, 3 and 4 hold the last state */
    static const int32_t buttons[3] = {0, 2, 1};
    bool was[3], now[3];
    for (int i = 0; i < 3; i++) {
        was[i] = get_out_int(b, 2 + i) != 0;
        now[i] = nmo_engine_is_mouse_button_down(engine, buttons[i]);
        set_out_int(b, 2 + i, now[i] ? 1 : 0);
    }

    const bool fired[8] = {
        position[0] != old_position[0] || position[1] != old_position[1],
        !was[0] && now[0], was[0] && !now[0], /* left */
        !was[2] && now[2], was[2] && !now[2], /* right */
        !was[1] && now[1], was[1] && !now[1], /* middle */
        wheel != 0.0f,
    };
    int pos = 0;
    bool one_active = false;
    for (int bit = 0; bit < 8; bit++) {
        if ((outputs & (1 << bit)) == 0) {
            continue;
        }
        if (fired[bit] && pos < nmo_engine_block_output_count(b)) {
            OUT_ON(b, pos);
            one_active = true;
        }
        pos++;
    }
    if (stay_active || !one_active) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_get_mouse_position(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float position[2];
    nmo_engine_mouse_position(nmo_engine_block_engine(b), &position[0], &position[1]);
    set_out_int(b, 0, (int32_t)position[0]);
    set_out_int(b, 1, (int32_t)position[1]);
    nmo_engine_block_set_output_value(b, 2, position, sizeof(position));
    return NMO_ENGINE_BR_OK;
}

/* Get Mouse Displacement (GetMouseRelativePosition) */
static int bb_get_mouse_displacement(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float delta[2];
    nmo_engine_mouse_relative(nmo_engine_block_engine(b), &delta[0], &delta[1], NULL);
    set_out_int(b, 0, (int32_t)delta[0]);
    set_out_int(b, 1, (int32_t)delta[1]);
    nmo_engine_block_set_output_value(b, 2, delta, sizeof(delta));
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_controller_blocks[] = {
    {NMO_GUID(0x1af2274b, 0x6b8c1524), "Key Event", bb_key_event, NULL, NULL},
    {NMO_GUID(0x016d010b, 0x017d010b), "Key Waiter", bb_key_waiter, cb_key_waiter, NULL},
    {NMO_GUID(0x2ffb3ef0, 0x21807ae3), "TT_Key Waiter", bb_key_waiter, cb_key_waiter, NULL},
    {NMO_GUID(0x1f49612e, 0x6cd63de0), "Switch On Key", bb_switch_on_key, NULL, NULL},
    {NMO_GUID(0x014d010b, 0x015d010b), "Mouse Waiter", bb_mouse_waiter, NULL, NULL},
    {NMO_GUID(0x5b1f1381, 0x5c640372), "Get Mouse Position", bb_get_mouse_position, NULL, NULL},
    {NMO_GUID(0x32d26c84, 0x3c741f89), "Get Mouse Displacement", bb_get_mouse_displacement, NULL,
     NULL},
};

nmo_status_t eng_register_controllers(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_controller_blocks) / sizeof(eng_controller_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_controller_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
