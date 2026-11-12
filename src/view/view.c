/**
 * @file view.c
 * @brief The engine's frame in an SDL3 window: the window and its OpenGL
 *        context, the keyboard and mouse, the clock and the voices. The
 *        drawing is nmo_render's (its OpenGL backend).
 */

#include "engine/nmo_view.h"
#include "render/nmo_render_gl.h"
#include "view_audio.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

struct nmo_view {
    nmo_engine_t *engine;
    SDL_Window *window;
    SDL_GLContext context;
    nmo_render_t *render;
    uint64_t start;
    bool cursor_shown;
    view_audio_t *audio; /* NULL when there is no audio device */
};

/* ============================================================================
 * Input
 * ============================================================================ */

/* SDL scan codes to DirectInput's */
static uint32_t view_scan_code(SDL_Scancode code)
{
    static const struct {
        SDL_Scancode sdl;
        uint8_t dik;
    } keys[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03},
        {SDL_SCANCODE_3, 0x04}, {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06},
        {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08}, {SDL_SCANCODE_8, 0x09},
        {SDL_SCANCODE_9, 0x0A}, {SDL_SCANCODE_0, 0x0B}, {SDL_SCANCODE_MINUS, 0x0C},
        {SDL_SCANCODE_EQUALS, 0x0D}, {SDL_SCANCODE_BACKSPACE, 0x0E}, {SDL_SCANCODE_TAB, 0x0F},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12},
        {SDL_SCANCODE_R, 0x13}, {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15},
        {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17}, {SDL_SCANCODE_O, 0x18},
        {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1A},
        {SDL_SCANCODE_RIGHTBRACKET, 0x1B}, {SDL_SCANCODE_RETURN, 0x1C},
        {SDL_SCANCODE_LCTRL, 0x1D}, {SDL_SCANCODE_A, 0x1E}, {SDL_SCANCODE_S, 0x1F},
        {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22},
        {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25},
        {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27},
        {SDL_SCANCODE_APOSTROPHE, 0x28}, {SDL_SCANCODE_GRAVE, 0x29},
        {SDL_SCANCODE_LSHIFT, 0x2A}, {SDL_SCANCODE_BACKSLASH, 0x2B}, {SDL_SCANCODE_Z, 0x2C},
        {SDL_SCANCODE_X, 0x2D}, {SDL_SCANCODE_C, 0x2E}, {SDL_SCANCODE_V, 0x2F},
        {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32},
        {SDL_SCANCODE_COMMA, 0x33}, {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35},
        {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_KP_MULTIPLY, 0x37},
        {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39}, {SDL_SCANCODE_CAPSLOCK, 0x3A},
        {SDL_SCANCODE_F1, 0x3B}, {SDL_SCANCODE_F2, 0x3C}, {SDL_SCANCODE_F3, 0x3D},
        {SDL_SCANCODE_F4, 0x3E}, {SDL_SCANCODE_F5, 0x3F}, {SDL_SCANCODE_F6, 0x40},
        {SDL_SCANCODE_F7, 0x41}, {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43},
        {SDL_SCANCODE_F10, 0x44}, {SDL_SCANCODE_KP_7, 0x47}, {SDL_SCANCODE_KP_8, 0x48},
        {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4A}, {SDL_SCANCODE_KP_4, 0x4B},
        {SDL_SCANCODE_KP_5, 0x4C}, {SDL_SCANCODE_KP_6, 0x4D}, {SDL_SCANCODE_KP_PLUS, 0x4E},
        {SDL_SCANCODE_KP_1, 0x4F}, {SDL_SCANCODE_KP_2, 0x50}, {SDL_SCANCODE_KP_3, 0x51},
        {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_KP_PERIOD, 0x53}, {SDL_SCANCODE_F11, 0x57},
        {SDL_SCANCODE_F12, 0x58}, {SDL_SCANCODE_KP_ENTER, 0x9C}, {SDL_SCANCODE_RCTRL, 0x9D},
        {SDL_SCANCODE_KP_DIVIDE, 0xB5}, {SDL_SCANCODE_RALT, 0xB8}, {SDL_SCANCODE_HOME, 0xC7},
        {SDL_SCANCODE_UP, 0xC8}, {SDL_SCANCODE_PAGEUP, 0xC9}, {SDL_SCANCODE_LEFT, 0xCB},
        {SDL_SCANCODE_RIGHT, 0xCD}, {SDL_SCANCODE_END, 0xCF}, {SDL_SCANCODE_DOWN, 0xD0},
        {SDL_SCANCODE_PAGEDOWN, 0xD1}, {SDL_SCANCODE_INSERT, 0xD2},
        {SDL_SCANCODE_DELETE, 0xD3},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].sdl == code) {
            return keys[i].dik;
        }
    }
    return 0;
}

/* A window point in render context pixels */
static void view_to_frame(const nmo_view_t *view, float x, float y, float *out_x, float *out_y)
{
    float density = SDL_GetWindowPixelDensity(view->window);
    nmo_render_target_to_frame(view->render, x * density, y * density, out_x, out_y);
}

bool nmo_view_poll(nmo_view_t *view)
{
    SDL_Event event;
    bool open = true;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            open = false;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            uint32_t key = view_scan_code(event.key.scancode);
            if (key != 0 && !event.key.repeat) {
                nmo_engine_set_key(view->engine, key, event.type == SDL_EVENT_KEY_DOWN);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            float x = 0.0f, y = 0.0f;
            view_to_frame(view, event.motion.x, event.motion.y, &x, &y);
            nmo_engine_set_mouse_position(view->engine, x, y);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int32_t button = event.button.button == SDL_BUTTON_LEFT    ? 0
                             : event.button.button == SDL_BUTTON_RIGHT ? 1
                             : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                         : -1;
            if (button >= 0) {
                nmo_engine_set_mouse_button(view->engine, button,
                                            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            nmo_engine_add_mouse_wheel(view->engine, event.wheel.y * 120.0f);
            break;
        default:
            break;
        }
    }
    /* Steering temporarily overrides the arrows in the engine. Refresh
       their physical state even without a new key-down event, so a key
       held across the handoff resumes control immediately. */
    const SDL_Scancode arrows[] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
                                  SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
    const bool *held = SDL_GetKeyboardState(NULL);
    for (size_t i = 0; i < sizeof(arrows) / sizeof(arrows[0]); i++) {
        nmo_engine_set_key(view->engine, view_scan_code(arrows[i]), held[arrows[i]]);
    }
    bool cursor = nmo_engine_cursor_is_visible(view->engine);
    if (cursor != view->cursor_shown) {
        view->cursor_shown = cursor;
        if (cursor) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
    return open;
}

/* ============================================================================
 * Drawing, sound and time
 * ============================================================================ */

nmo_status_t nmo_view_draw(nmo_view_t *view)
{
    if (view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    int window_w = 0, window_h = 0;
    SDL_GetWindowSizeInPixels(view->window, &window_w, &window_h);
    nmo_status_t status = nmo_render_draw(view->render, window_w, window_h);
    if (status != NMO_OK) {
        return status;
    }
    nmo_render_pick(view->render);
    SDL_GL_SwapWindow(view->window);
    return NMO_OK;
}

void nmo_view_set_mipmaps(nmo_view_t *view, bool all)
{
    if (view != NULL) {
        nmo_render_set_mipmaps(view->render, all);
    }
}

nmo_status_t nmo_view_screenshot(nmo_view_t *view, const char *path)
{
    if (view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    int window_w = 0, window_h = 0;
    SDL_GetWindowSizeInPixels(view->window, &window_w, &window_h);
    return nmo_render_screenshot(view->render, window_w, window_h, path);
}

void nmo_view_play_sounds(nmo_view_t *view)
{
    if (view != NULL) {
        view_audio_play(view->audio, view->engine);
    }
}

void nmo_view_wait(const nmo_view_t *view, double ms)
{
    if (view != NULL && ms > 0.0) {
        SDL_DelayPrecise((Uint64)(ms * 1.0e6));
    }
}

double nmo_view_time(const nmo_view_t *view)
{
    return view != NULL ? (double)(SDL_GetTicksNS() - view->start) / 1.0e6 : 0.0;
}

/* ============================================================================
 * The window
 * ============================================================================ */

nmo_status_t nmo_view_open(nmo_engine_t *engine, const char *title, int scale,
                           nmo_view_t **out_view)
{
    if (engine == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_view = NULL;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: SDL_Init failed (%s)\n", SDL_GetError());
        return NMO_ERR_INVALID_STATE;
    }
    nmo_view_t *view = (nmo_view_t *)calloc(1, sizeof(*view));
    if (view == NULL) {
        SDL_Quit();
        return NMO_ERR_NOMEM;
    }
    view->engine = engine;
    if (scale < 1) {
        scale = 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* 4 samples a pixel for TT_SetFSAA's antialiasing, or none when the driver has no such format */
    for (int samples = 4; samples >= 0 && view->context == NULL; samples -= 4) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples > 0 ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
        view->window = SDL_CreateWindow(title != NULL ? title : "nmo", 640 * scale, 480 * scale,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                            SDL_WINDOW_HIGH_PIXEL_DENSITY);
        view->context = view->window != NULL ? SDL_GL_CreateContext(view->window) : NULL;
        if (view->context == NULL && view->window != NULL && samples > 0) {
            SDL_DestroyWindow(view->window);
            view->window = NULL;
        }
    }
    if (view->context == NULL) {
        fprintf(stderr, "Error: Could not open an OpenGL window (%s)\n", SDL_GetError());
        nmo_view_close(view);
        return NMO_ERR_INVALID_STATE;
    }
    SDL_GL_SetSwapInterval(1);
    nmo_status_t status = nmo_render_create_gl(engine, &view->render);
    if (status != NMO_OK) {
        nmo_view_close(view);
        return status;
    }
    view->audio = view_audio_open();
    view->start = SDL_GetTicksNS();
    view->cursor_shown = true;
    *out_view = view;
    return NMO_OK;
}

void nmo_view_close(nmo_view_t *view)
{
    if (view == NULL) {
        return;
    }
    nmo_render_destroy(view->render); /* with its context current */
    view_audio_close(view->audio);
    if (view->context != NULL) {
        SDL_GL_DestroyContext(view->context);
    }
    if (view->window != NULL) {
        SDL_DestroyWindow(view->window);
    }
    free(view);
    SDL_Quit();
}
