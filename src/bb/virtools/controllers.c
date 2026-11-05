/**
 * @file controllers.c
 * @brief Ports of the keyboard, joystick and mouse building blocks.
 *
 * Each function does what the block's function in Controllers.dll does,
 * reading the input state the host gives the engine (nmo_engine_set_key() and
 * the mouse functions) in place of the input manager. There is no window: an
 * action counts as inside the 3D window. TT_Key Waiter and TT InputString
 * (TT_Toolbox_RT.dll) are here with the blocks they vary.
 */

#include "virtools.h"
#include "core/nmo_utils.h"
#include "object/nmo_class_ids.h"

#include <stdlib.h>
#include <string.h>

static bool key_down(blk_t *b, uint32_t key)
{
    return nmo_engine_is_key_down(nmo_engine_block_engine(b), key);
}

/* ---- Controllers/Keyboard ---- */

static eng_param_t *mapper_data(blk_t *b)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine,node->first_local,node->local_count,0);
}

static bool mapper_valid(const eng_param_t *data, int32_t count)
{
    return count <= 0 || (data != NULL && data->data != NULL && (uint32_t)count <= data->size/40);
}

static int cb_mapper_bindings(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    eng_param_t *data = mapper_data(b); int32_t count = get_local_int(b,1,0);
    if (!mapper_valid(data,count)) return NMO_ENGINE_BR_PARAMETERERROR;
    /* mappedKey/mappedJoy: little-endian command and cached ID, then a 32-byte name.
     * Rebuild only the active records; copied/runtime IDs remain valid until LOAD. */
    for (int32_t i = 0; i < count; i++) {
        uint8_t *record = data->data+(size_t)i*40; char name[33]; memcpy(name,record+8,32); name[32] = 0;
        int32_t type = name[0] != 0 ? eng_message_type_add(b->engine,name) : -1;
        if (type < 0 && name[0] != 0) return NMO_ENGINE_BR_GENERICERROR;
        nmo_write_u32_le(record+4,(uint32_t)type);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_keyboard_mapper(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    IN_OFF(b,0);
    nmo_object_id_t sender = nmo_engine_block_owner(b), target = nmo_engine_block_target(b);
    if (eng_map_get(&b->engine->destroyed,sender) >= 0 || eng_map_get(&b->engine->destroyed,target) >= 0 ||
        !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,sender),NMO_CID_BEOBJECT) ||
        !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,target),NMO_CID_BEOBJECT)) return NMO_ENGINE_BR_OWNERERROR;
    eng_param_t *data = mapper_data(b); int32_t count = get_local_int(b,1,0);
    if (!mapper_valid(data,count)) return NMO_ENGINE_BR_PARAMETERERROR;
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *record = data->data+(size_t)i*40;
        if (key_down(b,nmo_read_u32_le(record))) nmo_engine_block_send_message(b,(int32_t)nmo_read_u32_le(record+4),target,sender);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int bb_joystick_mapper(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    IN_OFF(b,0);
    nmo_object_id_t sender = nmo_engine_block_owner(b), target = nmo_engine_block_target(b);
    if (eng_map_get(&b->engine->destroyed,sender) >= 0 || eng_map_get(&b->engine->destroyed,target) >= 0 ||
        !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,sender),NMO_CID_BEOBJECT) ||
        !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,target),NMO_CID_BEOBJECT)) return NMO_ENGINE_BR_OWNERERROR;
    eng_param_t *data = mapper_data(b); int32_t count = get_local_int(b,1,0);
    nmo_engine_joystick_state_t state;
    if (!nmo_engine_joystick_state(b->engine,get_in_int(b,0,0),&state)) return NMO_ENGINE_BR_GENERICERROR;
    if (!mapper_valid(data,count)) return NMO_ENGINE_BR_PARAMETERERROR;
    eng_vector_t sensitivity = {.5f,.5f,.5f}; nmo_engine_block_get_local_value(b,2,&sensitivity,sizeof(sensitivity));
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *record = data->data+(size_t)i*40; uint32_t command = nmo_read_u32_le(record); bool active;
        switch (command) {
            case 1008: active = state.position[1] < -sensitivity.y; break;
            case 1009: active = state.position[1] > sensitivity.y; break;
            case 1010: active = state.position[0] < -sensitivity.x; break;
            case 1011: active = state.position[0] > sensitivity.x; break;
            /* Controllers.dll uses SHL EAX,CL, which masks the count to five bits. */
            default: active = (state.buttons & (1u << ((command-1012u)&31u))) != 0; break;
        }
        if (active) nmo_engine_block_send_message(b,(int32_t)nmo_read_u32_le(record+4),target,sender);
    }
    nmo_engine_block_set_output_value(b,0,state.position,sizeof(state.position));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_controller_messages(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"};
    int32_t messages[10];
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        messages[i] = eng_message_type_add(b->engine,names[i]);
        if (messages[i] < 0) return NMO_ENGINE_BR_GENERICERROR;
    }
    return nmo_engine_block_set_local_value(b,0,messages,sizeof(messages)) == NMO_OK ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_PARAMETERERROR;
}

static int bb_keyboard_controller(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_map_get(&b->engine->destroyed,target) >= 0 || !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,target),NMO_CID_BEOBJECT)) return NMO_ENGINE_BR_OWNERERROR;
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    const eng_param_t *buffer = eng_node_param(b->engine,node->first_local,node->local_count,0);
    int32_t messages[10];
    if (buffer == NULL || buffer->size < sizeof(messages)) return NMO_ENGINE_BR_PARAMETERERROR;
    memcpy(messages,buffer->data,sizeof(messages));
    static const struct { uint32_t key; int message; } mappings[] = {
        {0xd2,4},{0xc7,5},{0xc9,6},{0xd3,7},{0xcf,8},{0xd1,9},
        {0x48,2},{0x50,3},{0x4b,0},{0x4d,1},
    };
    /* Native order is buttons, then Up/Down/Left/Right. Held keys repeat on
     * every frame, and this block deliberately leaves the On input active. */
    for (size_t i = 0; i < sizeof(mappings)/sizeof(mappings[0]); i++)
        if (key_down(b,mappings[i].key)) nmo_engine_block_send_message(b,messages[mappings[i].message],target,target);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int bb_joystick_controller(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    nmo_object_id_t target = nmo_engine_block_target(b), sender = nmo_engine_block_owner(b);
    if (eng_map_get(&b->engine->destroyed,target) >= 0 || !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,target),NMO_CID_BEOBJECT)) return NMO_ENGINE_BR_OWNERERROR;
    nmo_engine_joystick_state_t state;
    if (!nmo_engine_joystick_state(b->engine,get_in_int(b,0,0),&state)) return NMO_ENGINE_BR_GENERICERROR;
    eng_vector_t sensitivity = {.5f,.5f,.5f}; nmo_engine_block_get_local_value(b,1,&sensitivity,sizeof(sensitivity));
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    const eng_param_t *buffer = eng_node_param(b->engine,node->first_local,node->local_count,0);
    int32_t messages[10];
    if (buffer == NULL || buffer->size < sizeof(messages)) return NMO_ENGINE_BR_PARAMETERERROR;
    memcpy(messages,buffer->data,sizeof(messages));
    for (int i = 0; i < 6; i++) if (state.buttons & (1u << i)) nmo_engine_block_send_message(b,messages[4+i],target,sender);
    if (state.position[1] < -sensitivity.y) nmo_engine_block_send_message(b,messages[2],target,sender);
    if (state.position[1] > sensitivity.y) nmo_engine_block_send_message(b,messages[3],target,sender);
    if (state.position[0] < -sensitivity.x) nmo_engine_block_send_message(b,messages[0],target,sender);
    if (state.position[0] > sensitivity.x) nmo_engine_block_send_message(b,messages[1],target,sender);
    nmo_engine_block_set_output_value(b,0,state.position,sizeof(state.position));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Joystick Waiter V2 numbers outputs by enabled axes, POV moved/centered,
 * then enabled buttons. Its output parameters are also the relative history. */
static int bb_joystick_waiter(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (eng_map_get(&b->engine->destroyed,owner) >= 0 ||
        !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,owner),NMO_CID_BEOBJECT)) return NMO_ENGINE_BR_OWNERERROR;
    nmo_engine_joystick_state_t state;
    if (!nmo_engine_joystick_state(b->engine,get_in_int(b,0,0),&state)) return NMO_ENGINE_BR_GENERICERROR;
    eng_vector_t sensitivity = {.5f,.5f,.5f}; nmo_engine_block_get_local_value(b,0,&sensitivity,sizeof(sensitivity));
    float previous[8] = {0}, old_pov = -1;
    nmo_engine_block_get_output_value(b,0,previous,3*sizeof(float));
    nmo_engine_block_get_output_value(b,1,previous+3,3*sizeof(float));
    nmo_engine_block_get_output_value(b,2,previous+6,2*sizeof(float));
    nmo_engine_block_get_output_value(b,3,&old_pov,sizeof(old_pov));
    nmo_engine_block_set_output_value(b,0,state.position,sizeof(state.position));
    nmo_engine_block_set_output_value(b,1,state.rotation,sizeof(state.rotation));
    nmo_engine_block_set_output_value(b,2,state.sliders,sizeof(state.sliders));
    nmo_engine_block_set_output_value(b,3,&state.point_of_view,sizeof(state.point_of_view));
    if (nmo_engine_block_version(b) < 0x00020000u) {
        /* Older files have ten fixed outputs and no Idle event. */
        for (int i = 0; i < 6; i++) if (state.buttons & (1u << i)) OUT_ON(b,4+i);
        if (state.position[1] < -sensitivity.y) OUT_ON(b,0);
        if (state.position[1] > sensitivity.y) OUT_ON(b,1);
        if (state.position[0] < -sensitivity.x) OUT_ON(b,2);
        if (state.position[0] > sensitivity.x) OUT_ON(b,3);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    uint32_t axes = (uint32_t)get_local_int(b,1,3), relative = (uint32_t)get_local_int(b,3,448);
    int output = 1; bool one_active = false;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t bit = 1u << i;
        if ((axes & bit) == 0) continue;
        float value = i < 3 ? state.position[i] : i < 6 ? state.rotation[i-3] : state.sliders[i-6];
        float threshold = sensitivity.x;
        if (relative & bit) { value = i < 3 ? previous[i]-value : value-previous[i]; threshold = sensitivity.y; }
        if (value < -threshold || value > threshold) { OUT_ON(b,output); one_active = true; }
        output++;
    }
    if (axes & 256u) {
        if ((relative & 256u) == 0 || state.point_of_view != old_pov) {
            OUT_ON(b,output+(state.point_of_view >= 0 ? 0 : 1)); one_active = true;
        }
        output += 2;
    }
    uint32_t buttons = (uint32_t)get_local_int(b,2,1);
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t bit = 1u << i;
        if ((buttons & bit) == 0) continue;
        if (state.buttons & bit) { OUT_ON(b,output); one_active = true; }
        output++;
    }
    if (!one_active) OUT_ON(b,0);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

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

/* Key Waiter: out once the key is down. */
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

/* TT_Key Waiter: as Key Waiter, and Off stops the waiting (nothing out). */
static int bb_tt_key_waiter(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return bb_key_waiter(b);
}

static int bb_tt_read_key(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return bb_read_key(b);
}

static int cb_tt_key_waiter(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_function(b, get_local_int(b, 0, 0) ? bb_tt_read_key : bb_tt_key_waiter);
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

/* ---- Input String ---- */

/* Input String's locals: its settings, then the caret, the text (with '\b' at the caret), On */
#define IS_MAX_SIZE 0

#define IS_MULTILINE 2

#define IS_DISABLED 3

#define IS_USE_CARET 4

#define IS_CARET 5

#define IS_TEXT 6

#define IS_ON 7

/* Disable Keyboard Section's bits */
#define IS_NO_NUMPAD 1

#define IS_NO_ARROWS 2

/*
 * VxScanCodeToAscii for a US keyboard: the character a scan code types,
 * shifted when a Shift key is down; 0 for none.
 */
static char scan_code_to_ascii(const nmo_engine_t *engine, uint32_t key)
{
    static const char plain[0x3A] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
        [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x0F] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
        [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '[',
        [0x1B] = ']', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
        [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
        [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
        [0x39] = ' ',
    };
    static const char shifted[0x3A] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^',
        [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x0F] = '\t', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
        [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = '{',
        [0x1B] = '}', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
        [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
        [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
        [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
        [0x39] = ' ',
    };
    if (key >= sizeof(plain)) {
        return 0;
    }
    bool shift = nmo_engine_is_key_down(engine, 0x2A) || nmo_engine_is_key_down(engine, 0x36);
    return shift ? shifted[key] : plain[key];
}

/* The caret taken out of the text (RemoveCaret); the caret is 0 after */
static void is_remove_caret(char *str, int *caret, int len)
{
    if (len <= 0 || *caret >= len) {
        *caret = 0;
        return;
    }
    memmove(str + *caret, str + *caret + 1, (size_t)(len - *caret));
    str[len - 1] = '\0';
    *caret = 0;
}

/* PageUp: the caret to the start */
static void is_page_up(char *str, int *caret)
{
    memmove(str + 1, str, (size_t)*caret);
    str[0] = '\b';
    *caret = 0;
}

/* Home: the caret to the start of its line (of the text, on one line) */
static bool is_home(bool multi, char *str, int *caret)
{
    if (*caret <= 0 || str[*caret - 1] == '\n') {
        *caret = 0;
        return false;
    }
    if (!multi) {
        is_page_up(str, caret);
        return true;
    }
    int pos = *caret - 1;
    while (pos > 0 && str[pos] != '\n') {
        pos--;
    }
    char *line = str;
    int start = 0;
    if (str[pos] == '\n') {
        line = str + pos + 1;
        *caret -= pos + 1;
        start = pos + 1;
    }
    memmove(line + 1, line, (size_t)*caret);
    *line = '\b';
    *caret = start;
    return true;
}

/* PageDown: the caret to the end */
static bool is_page_down(int length, char *str, int *caret, int max)
{
    if (*caret >= length - 1) {
        return false;
    }
    is_remove_caret(str, caret, length);
    str[length] = '\0';
    if (length - 1 == max) {
        char c = str[length - 2];
        str[length - 2] = '\b';
        str[length - 1] = c;
        *caret = length - 2;
    } else {
        str[length - 1] = '\b';
        *caret = length - 1;
    }
    return true;
}

/* End: the caret to the end of its line (of the text, on one line) */
static bool is_end(int length, bool multi, char *str, int *caret, int max)
{
    char *newline = multi ? strchr(str + *caret, '\n') : NULL;
    if (newline == NULL) {
        return is_page_down(length, str, caret, max);
    }
    memmove(str + *caret, str + *caret + 1, (size_t)(newline - (str + *caret) - 1));
    *caret = (int)(newline - str) - 1;
    str[*caret] = '\b';
    return true;
}

/* A character typed at the caret, the text kept to `size` characters */
static void is_add(char c, char *str, int *caret, int *length, int size)
{
    if (*length - 1 == size && *caret == *length - 2) {
        if (c != '\n') {
            str[*caret + 1] = c;
        }
        return;
    }
    if (*length == size && *caret == *length - 1) {
        if (c == '\n') {
            return;
        }
        str[*caret + 1] = c;
        str[*caret + 2] = '\0';
    } else {
        int end = *length - 1 == size ? size : *length;
        memmove(str + *caret + 1, str + *caret, (size_t)(end - *caret));
        str[(*caret)++] = c;
        str[end + 1] = '\0';
    }
    if (*length <= size) {
        ++*length;
    }
}

/* `length` bytes of `text` and a terminator (none when out of memory) */
static char *is_copy(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        if (length > 0) {
            memcpy(copy, text, length);
        }
        copy[length] = '\0';
    }
    return copy;
}

/* The String output: the text, without its caret unless Use Caret */
static void is_set_string(blk_t *b, const char *str, int caret, bool with_caret)
{
    size_t length = strlen(str);
    char *text = (char *)malloc(length + 1u);
    if (text == NULL) {
        return;
    }
    memcpy(text, str, length + 1u);
    if (!with_caret) {
        is_remove_caret(text, &caret, (int)length);
    }
    nmo_engine_block_set_output_value(b, 0, text, strlen(text) + 1u);
    free(text);
}

/* What the frame's keys did to an Input String's text */
#define IS_EDIT_NONE 0

#define IS_EDIT_CHANGED 1

#define IS_EDIT_ENDED 2 /* the end key (the keys after it unread) */

/*
 * The text edited at the caret by the keys pressed this frame, each one put
 * in output `key_pos`: arrows and Home/End/Page keys (unless Disable
 * Keyboard Section leaves them out), Backspace, Delete, the keypad,
 * Return on several lines, and the characters the keys type. The End Key
 * on several lines ends the editing, and with `enter_ends` Enter on one.
 */
static int is_edit(blk_t *b, char *str, int *caret, int *length, int size, int key_pos,
                   bool enter_ends)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    bool multi = get_local_int(b, IS_MULTILINE, 0) != 0;
    uint32_t end_key = multi ? (uint32_t)get_in_int(b, 1, 0) : 0u;
    int disabled = get_local_int(b, IS_DISABLED, 0);
    bool changed = false;
    int count = nmo_engine_key_buffer_count(engine);
    for (int i = 0; i < count; i++) {
        uint32_t key = 0;
        bool pressed = false;
        if (!nmo_engine_key_buffer_get(engine, i, &key, &pressed) || !pressed) {
            continue;
        }
        nmo_engine_block_set_output_value(b, key_pos, &key, sizeof(key));
        if (multi ? (end_key != 0 && key == end_key)
                  : (enter_ends && (key == 0x1C || key == 0x9C))) {
            return IS_EDIT_ENDED;
        }
        /* The keypad's characters, unless Disable Keyboard Section leaves it out */
        static const struct {
            uint32_t key;
            char c;
        } keypad[] = {{0x37, '*'}, {0x47, '7'}, {0x48, '8'}, {0x49, '9'}, {0x4A, '-'},
                      {0x4B, '4'}, {0x4C, '5'}, {0x4D, '6'}, {0x4E, '+'}, {0x4F, '1'},
                      {0x50, '2'}, {0x51, '3'}, {0x52, '0'}, {0x53, '.'}, {0x8D, '='},
                      {0x9C, '\n'}, {0xB3, ','}, {0xB5, '/'}};
        size_t k = 0;
        while (k < sizeof(keypad) / sizeof(keypad[0]) && keypad[k].key != key) {
            k++;
        }
        if (k < sizeof(keypad) / sizeof(keypad[0])) {
            if ((disabled & IS_NO_NUMPAD) == 0) {
                is_add(keypad[k].c, str, caret, length, size);
                changed = true;
            }
            continue;
        }
        bool arrows = (disabled & IS_NO_ARROWS) == 0;
        switch (key) {
        case 0x0E: /* Backspace */
            if (*caret > 0) {
                if (*length - 1 == size && *caret == *length - 2) {
                    str[*caret + 1] = '\0';
                } else {
                    memmove(str + *caret - 1, str + *caret, (size_t)(*length - *caret));
                    --*caret;
                    str[*length - 1] = '\0';
                }
                --*length;
                changed = true;
            }
            break;
        case 0xD3: /* Delete */
            if (*length > 1) {
                if (*length - 1 == size && *caret == *length - 2) {
                    str[*caret + 1] = '\0';
                } else if (*caret < *length - 1) {
                    memmove(str + *caret + 1, str + *caret + 2, (size_t)(*length - *caret - 2));
                    str[*length - 1] = '\0';
                } else {
                    break;
                }
                --*length;
                changed = true;
            }
            break;
        case 0x1C: /* Return types a line break on several lines */
            if (multi) {
                is_add('\n', str, caret, length, size);
                changed = true;
            }
            break;
        case 0xC9: /* Page Up */
            if (arrows && *caret > 0) {
                is_page_up(str, caret);
                changed = true;
            }
            break;
        case 0xC7:
            if (arrows) {
                changed = is_home(multi, str, caret);
            }
            break;
        case 0xD1: /* Page Down */
            if (arrows) {
                changed = is_page_down(*length, str, caret, size);
            }
            break;
        case 0xCF:
            if (arrows) {
                changed = is_end(*length, multi, str, caret, size);
            }
            break;
        case 0xCB: /* Left */
            if (arrows && *caret > 0) {
                str[*caret] = str[*caret - 1];
                str[--*caret] = '\b';
                changed = true;
            }
            break;
        case 0xCD: /* Right */
            if (arrows && *caret < *length - 1 && *caret + 1 < size) {
                str[*caret] = str[*caret + 1];
                str[++*caret] = '\b';
                changed = true;
            }
            break;
        default: {
            char c = scan_code_to_ascii(engine, key);
            if (c != 0 && c != 27) {
                is_add(c, str, caret, length, size);
                changed = true;
            }
            break;
        }
        }
    }
    return changed ? IS_EDIT_CHANGED : IS_EDIT_NONE;
}

/*
 * Input String (EditString): On starts the text from Reset String (Out On);
 * then each key of the buffer edits it at the caret, Updated String when it
 * changed, until Enter (the End Key when Multiline) gives the String and
 * Off, as does the Off input. The keyboard repetition setting is the input
 * manager's, which the engine's key buffer does not have.
 */
static int bb_input_string(blk_t *b)
{
    int size = get_local_int(b, IS_MAX_SIZE, 32);
    size = size > 0 ? size : 1;
    int caret = get_local_int(b, IS_CARET, 0);
    char *saved = (char *)calloc((size_t)size + 3u, 1u);
    if (saved == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    nmo_engine_block_get_local_value(b, IS_TEXT, saved, (size_t)size + 2u);
    char *str = saved;
    int length = (int)strlen(str);
    caret = caret < 0 ? 0 : caret > length ? length : caret;
    int result = NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    bool store = false;

    if (IN_ACTIVE(b, 1)) {
        if (get_local_int(b, IS_ON, 0) != 0) {
            is_set_string(b, str, caret, false);
            int32_t off = 0;
            nmo_engine_block_set_local_value(b, IS_ON, &off, sizeof(off));
        }
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        free(saved);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        int32_t on = 1;
        nmo_engine_block_set_local_value(b, IS_ON, &on, sizeof(on));
        IN_OFF(b, 0);
        size_t reset_size = 0;
        const char *reset = (const char *)nmo_engine_block_input_data(b, 0, &reset_size);
        int len = 0;
        for (size_t i = 0; reset != NULL && i < reset_size && reset[i] != '\0'; i++) {
            if (reset[i] != '\b' && len < size) { /* RemoveAllCaret, kept to Max Size */
                str[len++] = reset[i];
            }
        }
        if (len < size) {
            str[len] = '\b';
            caret = len;
            str[len + 1] = '\0';
        } else {
            str[size] = str[size - 1];
            str[size - 1] = '\b';
            caret = size - 1;
            str[size + 1] = '\0';
        }
        nmo_engine_block_set_local_value(b, IS_CARET, &caret, sizeof(caret));
        is_set_string(b, str, caret, get_local_int(b, IS_USE_CARET, 0) != 0);
        OUT_ON(b, 0);
        store = true;
    } else {
        int edit = is_edit(b, str, &caret, &length, size, 1, true);
        if (edit == IS_EDIT_ENDED) {
            is_set_string(b, str, caret, false);
            nmo_engine_block_set_local_value(b, IS_CARET, &caret, sizeof(caret));
            OUT_ON(b, 1);
            result = NMO_ENGINE_BR_OK;
            store = true;
        } else if (edit == IS_EDIT_CHANGED) {
            nmo_engine_block_set_local_value(b, IS_CARET, &caret, sizeof(caret));
            is_set_string(b, str, caret, get_local_int(b, IS_USE_CARET, 0) != 0);
            OUT_ON(b, 2);
            store = true;
        }
    }
    if (store) {
        nmo_engine_block_set_local_value(b, IS_TEXT, str, (size_t)size + 2u);
    }
    free(saved);
    return result;
}

/* Created, loaded or reset: off, the text the caret alone */
static int cb_input_string(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_RESET) {
        int32_t zero = 0;
        nmo_engine_block_set_local_value(b, IS_ON, &zero, sizeof(zero));
        int size = get_local_int(b, IS_MAX_SIZE, 32);
        size = size > 0 ? size : 1;
        char *text = (char *)calloc((size_t)size + 2u, 1u);
        if (text != NULL) {
            text[0] = '\b';
            nmo_engine_block_set_local_value(b, IS_TEXT, text, (size_t)size + 2u);
            free(text);
        }
        nmo_engine_block_set_local_value(b, IS_CARET, &zero, sizeof(zero));
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * TT InputString: as Input String, but Enter only types (a line break on
 * several lines, and on the keypad) and only the End Key on several lines
 * ends it; On also gives the Reset String as the second string, and every
 * change both strings (with the caret when Use Caret, and without).
 */
static int bb_tt_input_string(blk_t *b)
{
    int size = get_local_int(b, IS_MAX_SIZE, 32);
    size = size > 0 ? size : 1;
    int caret = get_local_int(b, IS_CARET, 0);
    char *str = (char *)calloc((size_t)size + 3u, 1u);
    if (str == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    nmo_engine_block_get_local_value(b, IS_TEXT, str, (size_t)size + 2u);
    int length = (int)strlen(str);
    caret = caret < 0 ? 0 : caret > length ? length : caret;
    int result = NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    bool store = false;
    bool ended = false;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        ended = true;
    } else if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        size_t reset_size = 0;
        const char *reset = (const char *)nmo_engine_block_input_data(b, 0, &reset_size);
        const char *nul = reset != NULL ? (const char *)memchr(reset, '\0', reset_size) : NULL;
        size_t len = reset == NULL ? 0u : nul != NULL ? (size_t)(nul - reset) : reset_size;
        char *copy = is_copy(reset, len);
        if (copy == NULL) {
            free(str);
            return NMO_ENGINE_BR_GENERICERROR;
        }
        nmo_engine_block_set_output_value(b, 1, copy, len + 1u);
        if ((int)len >= size) {
            memcpy(str, copy, (size_t)size);
            str[size] = str[size - 1];
            str[size - 1] = '\b';
            caret = size - 1;
            str[size + 1] = '\0';
        } else {
            memcpy(str, copy, len);
            str[len] = '\b';
            caret = (int)len;
            str[len + 1] = '\0';
        }
        free(copy);
        nmo_engine_block_set_local_value(b, IS_CARET, &caret, sizeof(caret));
        is_set_string(b, str, caret, get_local_int(b, IS_USE_CARET, 0) != 0);
        OUT_ON(b, 0);
        store = true;
    } else {
        int edit = is_edit(b, str, &caret, &length, size, 2, false);
        ended = edit == IS_EDIT_ENDED;
        if (edit == IS_EDIT_CHANGED) {
            nmo_engine_block_set_local_value(b, IS_CARET, &caret, sizeof(caret));
            bool use_caret = get_local_int(b, IS_USE_CARET, 0) != 0;
            is_set_string(b, str, caret, use_caret);
            char *plain = is_copy(str, strlen(str));
            if (plain != NULL) {
                int c = caret;
                is_remove_caret(plain, &c, (int)strlen(plain));
                nmo_engine_block_set_output_value(b, 1, plain, strlen(plain) + 1u);
                free(plain);
            }
            OUT_ON(b, 2);
            store = true;
        }
    }
    if (ended) {
        /* Off, or the End Key: the caret taken out of the text itself */
        is_remove_caret(str, &caret, length);
        nmo_engine_block_set_local_value(b, IS_CARET, &caret, sizeof(caret));
        nmo_engine_block_set_output_value(b, 0, str, strlen(str) + 1u);
        OUT_ON(b, 1);
        result = NMO_ENGINE_BR_OK;
        store = true;
    }
    if (store) {
        nmo_engine_block_set_local_value(b, IS_TEXT, str, (size_t)size + 2u);
    }
    free(str);
    return result;
}

/* Created, loaded or settings edited: an empty String, the caret at 0 */
static int cb_tt_input_string(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_RESET) {
        int size = get_local_int(b, IS_MAX_SIZE, 32);
        size = size > 0 ? size : 1;
        char *text = (char *)calloc((size_t)size + 2u, 1u);
        if (text != NULL) {
            nmo_engine_block_set_output_value(b, 0, text, (size_t)size + 2u);
            free(text);
        }
        int32_t zero = 0;
        nmo_engine_block_set_local_value(b, IS_CARET, &zero, sizeof(zero));
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_controller_blocks[] = {
    {NMO_GUID_INIT(0x053b2bde,0x2f8f1d74),"Keyboard Mapper",bb_keyboard_mapper,cb_mapper_bindings,NULL},
    {NMO_GUID_INIT(0x43f1d000,0x1a2b3c4d),"Keyboard Controller",bb_keyboard_controller,cb_controller_messages,NULL},
    {NMO_GUID_INIT(0x03ed1bea,0x25692ecc),"Joystick Mapper",bb_joystick_mapper,cb_mapper_bindings,NULL},
    {NMO_GUID_INIT(0x012d010b,0x013d010b),"Joystick Waiter",bb_joystick_waiter,NULL,NULL},
    {NMO_GUID_INIT(0x0085ebc1,0x21935422),"Joystick Controller",bb_joystick_controller,cb_controller_messages,NULL},
    {NMO_GUID_INIT(0x1af2274b, 0x6b8c1524), "Key Event", bb_key_event, NULL, NULL},
    {NMO_GUID_INIT(0x016d010b, 0x017d010b), "Key Waiter", bb_key_waiter, cb_key_waiter, NULL},
    {NMO_GUID_INIT(0x2ffb3ef0, 0x21807ae3), "TT_Key Waiter", bb_tt_key_waiter, cb_tt_key_waiter, NULL},
    {NMO_GUID_INIT(0x1f49612e, 0x6cd63de0), "Switch On Key", bb_switch_on_key, NULL, NULL},
    {NMO_GUID_INIT(0x014d010b, 0x015d010b), "Mouse Waiter", bb_mouse_waiter, NULL, NULL},
    {NMO_GUID_INIT(0x5b1f1381, 0x5c640372), "Get Mouse Position", bb_get_mouse_position, NULL, NULL},
    {NMO_GUID_INIT(0x32d26c84, 0x3c741f89), "Get Mouse Displacement", bb_get_mouse_displacement, NULL,
     NULL},
    {NMO_GUID_INIT(0x693e7e4b, 0x100104c4), "Input String", bb_input_string, cb_input_string, NULL},
    {NMO_GUID_INIT(0x52fd6294, 0x612f51a5), "TT InputString", bb_tt_input_string, cb_tt_input_string,
     NULL},
};

nmo_status_t eng_register_controllers(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_controller_blocks);
}
