/**
 * @file engine_input.h
 * @brief The input manager: keys, mouse, joysticks, and the host's picking.
 */

#ifndef NMO_ENGINE_INPUT_H
#define NMO_ENGINE_INPUT_H

#include "engine_base.h"

#define ENG_KEY_COUNT 256

/* A key that went down or up since the previous frame (the input manager's key buffer). */
typedef struct eng_key_event {
    uint32_t key;
    bool pressed;
} eng_key_event_t;

/*
 * What the input manager reports during a frame. The host sets the `next`
 * state; at the start of a frame it becomes the frame's state and the key
 * buffer and mouse movement follow from the difference.
 */
typedef struct eng_input {
    uint8_t keys[ENG_KEY_COUNT];
    uint8_t next_keys[ENG_KEY_COUNT];
    eng_key_event_t buffer[ENG_KEY_COUNT];
    uint32_t buffer_count;
    float mouse[2], next_mouse[2];
    float relative[3];       /* x, y and the wheel */
    float next_wheel;
    bool buttons[3], next_buttons[3]; /* CK_MOUSE_BUTTON: left, right, middle */
    bool last_buttons[3];             /* the previous frame's */
    nmo_engine_joystick_state_t joysticks[NMO_ENGINE_JOYSTICK_COUNT];
    nmo_engine_joystick_state_t next_joysticks[NMO_ENGINE_JOYSTICK_COUNT];
    nmo_object_id_t under_mouse, next_under_mouse;
    bool cursor_hidden; /* CKInputManager::ShowCursor(FALSE); the cursor starts shown */
    int32_t system_cursor; /* SetSystemCursor (VXCURSOR_POINTER); 0 until a block sets one */
    nmo_engine_pick_fn pick; /* the host's CKRenderContext::Pick; NULL for none */
    void *pick_user_data;
} eng_input_t;

/* InputManager::PreProcess: the host's state becomes the frame's. */
void eng_input_begin_frame(nmo_engine_t *engine);
/* CKInputManager::IsMouseClicked: down this frame and not the last */
bool eng_mouse_clicked(const nmo_engine_t *engine, int32_t button);

#endif /* NMO_ENGINE_INPUT_H */
