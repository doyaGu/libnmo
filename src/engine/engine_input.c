/**
 * @file engine_input.c
 * @brief The keyboard and mouse state the input blocks read.
 */

#include "engine_internal.h"

#include <string.h>

void nmo_engine_set_key(nmo_engine_t *engine, uint32_t key, bool down)
{
    if (engine != NULL && key < ENG_KEY_COUNT) {
        engine->input.next_keys[key] = down ? 1u : 0u;
    }
}

void nmo_engine_set_mouse_position(nmo_engine_t *engine, float x, float y)
{
    if (engine != NULL) {
        engine->input.next_mouse[0] = x;
        engine->input.next_mouse[1] = y;
    }
}

void nmo_engine_set_mouse_button(nmo_engine_t *engine, int32_t button, bool down)
{
    if (engine != NULL && button >= 0 && button < 3) {
        engine->input.next_buttons[button] = down;
    }
}

void nmo_engine_add_mouse_wheel(nmo_engine_t *engine, float delta)
{
    if (engine != NULL) {
        engine->input.next_wheel += delta;
    }
}

void eng_input_begin_frame(nmo_engine_t *engine)
{
    eng_input_t *in = &engine->input;
    in->buffer_count = 0;
    for (uint32_t key = 0; key < ENG_KEY_COUNT; key++) {
        if (in->keys[key] != in->next_keys[key]) {
            in->buffer[in->buffer_count].key = key;
            in->buffer[in->buffer_count].pressed = in->next_keys[key] != 0;
            in->buffer_count++;
        }
    }
    memcpy(in->keys, in->next_keys, sizeof(in->keys));
    in->relative[0] = in->next_mouse[0] - in->mouse[0];
    in->relative[1] = in->next_mouse[1] - in->mouse[1];
    in->relative[2] = in->next_wheel;
    in->next_wheel = 0.0f;
    memcpy(in->mouse, in->next_mouse, sizeof(in->mouse));
    memcpy(in->buttons, in->next_buttons, sizeof(in->buttons));
}

bool nmo_engine_is_key_down(const nmo_engine_t *engine, uint32_t key)
{
    return engine != NULL && key < ENG_KEY_COUNT && engine->input.keys[key] != 0;
}

int32_t nmo_engine_key_buffer_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->input.buffer_count : 0;
}

bool nmo_engine_key_buffer_get(const nmo_engine_t *engine, int32_t index, uint32_t *out_key,
                               bool *out_pressed)
{
    if (engine == NULL || index < 0 || (uint32_t)index >= engine->input.buffer_count) {
        return false;
    }
    if (out_key != NULL) {
        *out_key = engine->input.buffer[index].key;
    }
    if (out_pressed != NULL) {
        *out_pressed = engine->input.buffer[index].pressed;
    }
    return true;
}

void nmo_engine_mouse_position(const nmo_engine_t *engine, float *out_x, float *out_y)
{
    if (out_x != NULL) {
        *out_x = engine != NULL ? engine->input.mouse[0] : 0.0f;
    }
    if (out_y != NULL) {
        *out_y = engine != NULL ? engine->input.mouse[1] : 0.0f;
    }
}

void nmo_engine_mouse_relative(const nmo_engine_t *engine, float *out_x, float *out_y,
                               float *out_wheel)
{
    const float *relative = engine != NULL ? engine->input.relative : NULL;
    if (out_x != NULL) {
        *out_x = relative != NULL ? relative[0] : 0.0f;
    }
    if (out_y != NULL) {
        *out_y = relative != NULL ? relative[1] : 0.0f;
    }
    if (out_wheel != NULL) {
        *out_wheel = relative != NULL ? relative[2] : 0.0f;
    }
}

bool nmo_engine_is_mouse_button_down(const nmo_engine_t *engine, int32_t button)
{
    return engine != NULL && button >= 0 && button < 3 && engine->input.buttons[button];
}
