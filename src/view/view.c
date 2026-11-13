/**
 * @file view.c
 * @brief The engine's frame in an SDL3 window: the window and what draws in
 *        it (an OpenGL context, an SDL GPU device, or an SDL renderer showing
 *        the software backend's frames), the keyboard and mouse, the clock
 *        and the voices. The drawing is nmo_render's.
 */

#include "engine/nmo_view.h"
#include "render/nmo_render_soft.h"
#ifdef NMO_HAS_RENDER_GL
#include "render/nmo_render_gl.h"
#endif
#ifdef NMO_HAS_RENDER_GPU
#include "render/nmo_render_gpu.h"
#endif
#ifdef NMO_HAS_RENDER_GL3
#include "render/nmo_render_gl3.h"
#endif
#include "view_audio.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

struct nmo_view {
    nmo_engine_t *engine;
    SDL_Window *window;
    SDL_GLContext context;  /* an OpenGL view's */
    SDL_GPUDevice *device;  /* a GPU view's: the renderer shows the frames itself */
    SDL_Renderer *renderer; /* a software view's: shows the frames drawn in memory */
    SDL_Texture *texture;   /* a software view's frame, `texture_w` x `texture_h` */
    int texture_w, texture_h;
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
    if (view->renderer != NULL) {
        /* The software frame is drawn at its own size and scaled to the window */
        float rx = 0.0f, ry = 0.0f;
        SDL_RenderCoordinatesFromWindow(view->renderer, x, y, &rx, &ry);
        nmo_render_target_to_frame(view->render, rx, ry, out_x, out_y);
        return;
    }
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

/*
 * The size a frame is drawn at: the window's pixels, but the render
 * context's own size for the software backend, whose cost grows with the
 * pixels (the renderer scales it to the window, as a 640x480 game was).
 */
static void view_target_size(const nmo_view_t *view, int *out_w, int *out_h)
{
    if (view->renderer != NULL) {
        nmo_render_frame_size(view->render, out_w, out_h);
    } else {
        SDL_GetWindowSizeInPixels(view->window, out_w, out_h);
    }
}

/* Show the software backend's frame: upload it, scale it to the window, present */
static nmo_status_t view_show_soft(nmo_view_t *view)
{
    int width = 0, height = 0;
    const uint8_t *pixels = nmo_render_soft_pixels(view->render, &width, &height);
    if (pixels == NULL || width <= 0 || height <= 0) {
        return NMO_ERR_INVALID_STATE;
    }
    if (view->texture == NULL || view->texture_w != width || view->texture_h != height) {
        SDL_DestroyTexture(view->texture);
        view->texture = SDL_CreateTexture(view->renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, width, height);
        if (view->texture == NULL) {
            fprintf(stderr, "Error: Could not make the frame's texture (%s)\n", SDL_GetError());
            return NMO_ERR_INVALID_STATE;
        }
        view->texture_w = width;
        view->texture_h = height;
        SDL_SetRenderLogicalPresentation(view->renderer, width, height,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    SDL_UpdateTexture(view->texture, NULL, pixels, width * 4);
    SDL_SetRenderDrawColor(view->renderer, 0, 0, 0, 255);
    SDL_RenderClear(view->renderer);
    SDL_RenderTexture(view->renderer, view->texture, NULL, NULL);
    SDL_RenderPresent(view->renderer);
    return NMO_OK;
}

nmo_status_t nmo_view_draw(nmo_view_t *view)
{
    if (view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    int window_w = 0, window_h = 0;
    view_target_size(view, &window_w, &window_h);
    nmo_status_t status = nmo_render_draw(view->render, window_w, window_h);
    if (status != NMO_OK) {
        return status;
    }
    nmo_render_pick(view->render);
    if (view->renderer != NULL) {
        return view_show_soft(view);
    }
    if (view->context != NULL) {
        SDL_GL_SwapWindow(view->window);
    }
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
    view_target_size(view, &window_w, &window_h);
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

static SDL_Window *view_create_window(const char *title, int scale, SDL_WindowFlags flags)
{
    return SDL_CreateWindow(title != NULL ? title : "nmo", 640 * scale, 480 * scale,
                            flags | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
}

/* The window and an SDL renderer showing the frames the software backend draws */
static nmo_status_t view_open_soft(nmo_view_t *view, const char *title, int scale)
{
    view->window = view_create_window(title, scale, 0);
    view->renderer = view->window != NULL ? SDL_CreateRenderer(view->window, NULL) : NULL;
    if (view->renderer == NULL) {
        fprintf(stderr, "Error: Could not open a window (%s)\n", SDL_GetError());
        return NMO_ERR_INVALID_STATE;
    }
    SDL_SetRenderVSync(view->renderer, 1);
    return nmo_render_create_soft(view->engine, &view->render);
}

#ifdef NMO_HAS_RENDER_GPU
/* The window and its SDL GPU device, which the renderer claims */
static nmo_status_t view_open_gpu(nmo_view_t *view, const char *title, int scale)
{
    view->window = view_create_window(title, scale, 0);
    view->device = view->window != NULL
                       ? SDL_CreateGPUDevice((SDL_GPUShaderFormat)nmo_render_gpu_shader_formats(), false, NULL)
                       : NULL;
    if (view->device == NULL) {
        fprintf(stderr, "Error: Could not open a GPU window (%s)\n", SDL_GetError());
        return NMO_ERR_INVALID_STATE;
    }
    return nmo_render_create_gpu(view->engine, view->device, view->window, &view->render);
}
#endif

#ifdef NMO_HAS_RENDER_GL
/* SDL's loader of the OpenGL context's functions, as nmo_render_create_gl takes it */
static nmo_render_gl_function_t view_gl_function(const char *name)
{
    return (nmo_render_gl_function_t)SDL_GL_GetProcAddress(name);
}

/* The window and its OpenGL 2.1 context, for the fixed function backend */
static nmo_status_t view_open_gl(nmo_view_t *view, const char *title, int scale)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* 4 samples a pixel for TT_SetFSAA's antialiasing, or none when the driver has no such format */
    for (int samples = 4; samples >= 0 && view->context == NULL; samples -= 4) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples > 0 ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
        view->window = view_create_window(title, scale, SDL_WINDOW_OPENGL);
        view->context = view->window != NULL ? SDL_GL_CreateContext(view->window) : NULL;
        if (view->context == NULL && view->window != NULL && samples > 0) {
            SDL_DestroyWindow(view->window);
            view->window = NULL;
        }
    }
    if (view->context == NULL) {
        fprintf(stderr, "Error: Could not open an OpenGL window (%s)\n", SDL_GetError());
        return NMO_ERR_INVALID_STATE;
    }
    SDL_GL_SetSwapInterval(1);
    return nmo_render_create_gl(view->engine, view_gl_function, &view->render);
}
#endif

#ifdef NMO_HAS_RENDER_GL3
/* SDL's loader of the current context's functions, as nmo_render_create_gl3 takes it */
static nmo_render_gl3_function_t view_gl3_function(const char *name)
{
    return (nmo_render_gl3_function_t)SDL_GL_GetProcAddress(name);
}

/* One attempt at an `es` (OpenGL ES 3.0) or core (OpenGL 3.3) GL3 context; the window and
   context it made are gone again on any failure, so another attempt starts clean. */
static nmo_status_t view_open_gl3_try(nmo_view_t *view, const char *title, int scale, bool es)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        es ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, es ? 0 : 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    view->window = view_create_window(title, scale, SDL_WINDOW_OPENGL);
    view->context = view->window != NULL ? SDL_GL_CreateContext(view->window) : NULL;
    nmo_status_t status = NMO_ERR_INVALID_STATE;
    if (view->context != NULL) {
        SDL_GL_SetSwapInterval(1);
        status = nmo_render_create_gl3(view->engine, view_gl3_function, &view->render);
    }
    if (status != NMO_OK) {
        if (view->context != NULL) {
            SDL_GL_DestroyContext(view->context);
            view->context = NULL;
        }
        if (view->window != NULL) {
            SDL_DestroyWindow(view->window);
            view->window = NULL;
        }
    }
    return status;
}

/* The window and an OpenGL ES 3.0 context (WebGL 2 under Emscripten); OpenGL 3.3 core
   otherwise, since macOS's GL driver never reports an ES profile */
static nmo_status_t view_open_gl3(nmo_view_t *view, const char *title, int scale)
{
    nmo_status_t status = view_open_gl3_try(view, title, scale, true);
    if (status != NMO_OK) {
        status = view_open_gl3_try(view, title, scale, false);
    }
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Could not open a GL3 window (%s)\n", SDL_GetError());
    }
    return status;
}
#endif

nmo_status_t nmo_view_open(nmo_engine_t *engine, const char *title, int scale,
                           nmo_view_t **out_view)
{
    return nmo_view_open_with(engine, title, scale, NMO_VIEW_RENDERER_GL, out_view);
}

nmo_status_t nmo_view_open_with(nmo_engine_t *engine, const char *title, int scale,
                                nmo_view_renderer_t renderer, nmo_view_t **out_view)
{
    if (engine == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_view = NULL;
    if (!nmo_view_has_renderer(renderer)) {
        return NMO_ERR_NOT_SUPPORTED;
    }
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
    nmo_status_t status = NMO_ERR_NOT_SUPPORTED;
    switch (renderer) {
    case NMO_VIEW_RENDERER_SOFT:
        status = view_open_soft(view, title, scale);
        break;
#ifdef NMO_HAS_RENDER_GPU
    case NMO_VIEW_RENDERER_GPU:
        status = view_open_gpu(view, title, scale);
        break;
#endif
#ifdef NMO_HAS_RENDER_GL
    case NMO_VIEW_RENDERER_GL:
        status = view_open_gl(view, title, scale);
        break;
#endif
#ifdef NMO_HAS_RENDER_GL3
    case NMO_VIEW_RENDERER_GL3:
        status = view_open_gl3(view, title, scale);
        break;
#endif
    default:
        break;
    }
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

bool nmo_view_has_renderer(nmo_view_renderer_t renderer)
{
    switch (renderer) {
    case NMO_VIEW_RENDERER_SOFT:
        return true;
    case NMO_VIEW_RENDERER_GPU:
#ifdef NMO_HAS_RENDER_GPU
        return true;
#else
        return false;
#endif
    case NMO_VIEW_RENDERER_GL:
#ifdef NMO_HAS_RENDER_GL
        return true;
#else
        return false;
#endif
    case NMO_VIEW_RENDERER_GL3:
#ifdef NMO_HAS_RENDER_GL3
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}

void nmo_view_close(nmo_view_t *view)
{
    if (view == NULL) {
        return;
    }
    nmo_render_destroy(view->render); /* with its context current, or its device there */
    view_audio_close(view->audio);
    if (view->texture != NULL) {
        SDL_DestroyTexture(view->texture);
    }
    if (view->renderer != NULL) {
        SDL_DestroyRenderer(view->renderer);
    }
    if (view->context != NULL) {
        SDL_GL_DestroyContext(view->context);
    }
    if (view->device != NULL) {
        SDL_DestroyGPUDevice(view->device);
    }
    if (view->window != NULL) {
        SDL_DestroyWindow(view->window);
    }
    free(view);
    SDL_Quit();
}
