/**
 * @file render_soft.c
 * @brief The software backend: frames drawn in memory by soft_raster.c,
 *        with no graphics device and no window.
 *
 * The screen is a surface the size the host draws at; it keeps what was
 * drawn from frame to frame, so a frame that does not clear its background
 * draws over the one before. Textures keep their levels as RGBA bytes, and
 * a texture drawn into is drawn upright: its first row is the frame's top.
 */

#include "render/nmo_render_soft.h"
#include "render_soft.h"
#include "soft_internal.h"
#include "../render_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Surfaces
 * ============================================================================ */

static void soft_surface_free(soft_surface_t *surface)
{
    free(surface->color);
    free(surface->depth);
    free(surface->stencil);
    memset(surface, 0, sizeof(*surface));
}

/* The screen `width` x `height`, made again (black, far, 0) when its size changes */
static bool soft_screen_size(soft_t *soft, int32_t width, int32_t height)
{
    soft_surface_t *screen = &soft->screen;
    if (screen->color != NULL && screen->width == width && screen->height == height) {
        return true;
    }
    soft_surface_free(screen);
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t pixels = (size_t)width * (size_t)height;
    screen->color = (uint8_t *)malloc(pixels * 4u);
    screen->depth = (float *)malloc(pixels * sizeof(float));
    screen->stencil = (uint8_t *)calloc(pixels, 1u);
    if (screen->color == NULL || screen->depth == NULL || screen->stencil == NULL) {
        soft_surface_free(screen);
        return false;
    }
    screen->width = width;
    screen->height = height;
    for (size_t i = 0; i < pixels; i++) {
        screen->color[i * 4 + 0] = screen->color[i * 4 + 1] = screen->color[i * 4 + 2] = 0;
        screen->color[i * 4 + 3] = 255;
        screen->depth[i] = 1.0f;
    }
    return true;
}

/* ============================================================================
 * Textures
 * ============================================================================ */

static void soft_level_free(soft_level_t *level)
{
    free(level->rgba);
    memset(level, 0, sizeof(*level));
}

static uintptr_t soft_texture_create(render_backend_t *backend)
{
    (void)backend;
    return (uintptr_t)calloc(1, sizeof(soft_texture_t));
}

static void soft_texture_destroy(render_backend_t *backend, uintptr_t handle)
{
    (void)backend;
    soft_texture_t *texture = (soft_texture_t *)handle;
    if (texture == NULL) {
        return;
    }
    for (int face = 0; face < 6; face++) {
        for (int level = 0; level < SOFT_MAX_LEVELS; level++) {
            soft_level_free(&texture->faces[face][level]);
        }
    }
    free(texture);
}

/* Level `level` of face `face` made `width` x `height`: a copy of `rgba`, or clear */
static bool soft_level_set(soft_level_t *level, int32_t width, int32_t height, const uint8_t *rgba)
{
    size_t size = (size_t)width * (size_t)height * 4u;
    if (level->rgba == NULL || level->width != width || level->height != height) {
        soft_level_free(level);
        level->rgba = (uint8_t *)malloc(size > 0 ? size : 4u);
        if (level->rgba == NULL) {
            return false;
        }
        level->width = width;
        level->height = height;
    }
    if (rgba != NULL) {
        memcpy(level->rgba, rgba, size);
    } else {
        memset(level->rgba, 0, size);
    }
    return true;
}

/* The faces a texture of `kind` has; the others go */
static void soft_texture_kind(soft_texture_t *texture, render_texture_kind_t kind)
{
    if (texture->kind == kind) {
        return;
    }
    for (int face = kind == RENDER_TEXTURE_CUBE ? 6 : 1; face < 6; face++) {
        for (int level = 0; level < SOFT_MAX_LEVELS; level++) {
            soft_level_free(&texture->faces[face][level]);
        }
    }
    texture->kind = kind;
}

static void soft_texture_image(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                               int32_t face, int32_t level, int32_t width, int32_t height,
                               const uint8_t *rgba)
{
    (void)backend;
    soft_texture_t *texture = (soft_texture_t *)handle;
    if (texture == NULL || face < 0 || face >= 6 || level < 0 || level >= SOFT_MAX_LEVELS ||
        width <= 0 || height <= 0) {
        return;
    }
    soft_texture_kind(texture, kind);
    soft_level_set(&texture->faces[face][level], width, height, rgba);
}

static void soft_texture_levels(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                                int32_t last)
{
    (void)backend;
    (void)kind;
    soft_texture_t *texture = (soft_texture_t *)handle;
    if (texture != NULL) {
        texture->last = last < 0 ? 0 : last >= SOFT_MAX_LEVELS ? SOFT_MAX_LEVELS - 1 : last;
    }
}

static void soft_texture_sub_image(render_backend_t *backend, uintptr_t handle, int32_t x, int32_t y,
                                   int32_t width, int32_t height, const uint8_t *rgba)
{
    (void)backend;
    soft_texture_t *texture = (soft_texture_t *)handle;
    soft_level_t *level = texture != NULL ? &texture->faces[0][0] : NULL;
    if (level == NULL || level->rgba == NULL) {
        return;
    }
    for (int32_t row = 0; row < height; row++) {
        int32_t ty = y + row;
        if (ty < 0 || ty >= level->height) {
            continue;
        }
        for (int32_t col = 0; col < width; col++) {
            int32_t tx = x + col;
            if (tx >= 0 && tx < level->width) {
                memcpy(&level->rgba[((size_t)ty * (size_t)level->width + (size_t)tx) * 4u],
                       &rgba[((size_t)row * (size_t)width + (size_t)col) * 4u], 4u);
            }
        }
    }
}

static void soft_texture_target(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                                int32_t width, int32_t height, bool keep_faces)
{
    (void)backend;
    soft_texture_t *texture = (soft_texture_t *)handle;
    if (texture == NULL || width <= 0 || height <= 0) {
        return;
    }
    bool was_cube = texture->kind == RENDER_TEXTURE_CUBE;
    soft_texture_kind(texture, kind);
    bool cube = kind == RENDER_TEXTURE_CUBE;
    for (int face = 0; face < (cube ? 6 : 1); face++) {
        soft_level_t *level = &texture->faces[face][0];
        if (cube && keep_faces && was_cube && level->rgba != NULL && level->width == width &&
            level->height == height) {
            continue;
        }
        soft_level_set(level, width, height, NULL);
    }
    texture->last = 0;
}

/* ============================================================================
 * Targets
 * ============================================================================ */

static bool soft_begin_screen(render_backend_t *backend, int32_t width, int32_t height, bool preserve)
{
    soft_t *soft = (soft_t *)backend;
    soft->current = soft_screen_size(soft, width, height) ? &soft->screen : NULL;
    return preserve && soft->current != NULL;
}

static void soft_end_screen(render_backend_t *backend)
{
    soft_t *soft = (soft_t *)backend;
    soft_surface_t *screen = &soft->screen;
    soft->current = NULL;
    if (screen->color == NULL) {
        return;
    }
    size_t size = (size_t)screen->width * (size_t)screen->height * 4u;
    if (soft->last == NULL || soft->last_w != screen->width || soft->last_h != screen->height) {
        free(soft->last);
        soft->last = (uint8_t *)malloc(size);
        soft->last_w = screen->width;
        soft->last_h = screen->height;
    }
    soft->last_valid = soft->last != NULL;
    if (soft->last != NULL) {
        memcpy(soft->last, screen->color, size);
    }
}

static bool soft_begin_target(render_backend_t *backend, uintptr_t handle, render_texture_kind_t kind,
                              int32_t face, int32_t width, int32_t height)
{
    soft_t *soft = (soft_t *)backend;
    soft_texture_t *texture = (soft_texture_t *)handle;
    if (texture == NULL || texture->kind != kind || face < 0 || face >= 6) {
        return false;
    }
    soft_level_t *level = &texture->faces[face][0];
    if (level->rgba == NULL || level->width != width || level->height != height) {
        return false;
    }
    if (soft->target_buffers_w != width || soft->target_buffers_h != height) {
        size_t pixels = (size_t)width * (size_t)height;
        free(soft->target_depth);
        free(soft->target_stencil);
        soft->target_depth = (float *)malloc(pixels * sizeof(float));
        soft->target_stencil = (uint8_t *)calloc(pixels, 1u);
        if (soft->target_depth == NULL || soft->target_stencil == NULL) {
            free(soft->target_depth);
            free(soft->target_stencil);
            soft->target_depth = NULL;
            soft->target_stencil = NULL;
            soft->target_buffers_w = soft->target_buffers_h = 0;
            return false;
        }
        for (size_t i = 0; i < pixels; i++) {
            soft->target_depth[i] = 1.0f;
        }
        soft->target_buffers_w = width;
        soft->target_buffers_h = height;
    }
    soft->target.width = width;
    soft->target.height = height;
    soft->target.color = level->rgba;
    soft->target.depth = soft->target_depth;
    soft->target.stencil = soft->target_stencil;
    soft->current = &soft->target;
    return true;
}

static void soft_end_target(render_backend_t *backend)
{
    soft_t *soft = (soft_t *)backend;
    soft->current = soft->screen.color != NULL ? &soft->screen : NULL;
}

/* ============================================================================
 * Drawing and reading
 * ============================================================================ */

static void soft_backend_clear(render_backend_t *backend, const render_state_t *state, bool color,
                               bool depth, bool stencil, const float value[4])
{
    soft_t *soft = (soft_t *)backend;
    soft_clear(soft->current, state, color, depth, stencil, value);
}

static void soft_backend_draw(render_backend_t *backend, const render_state_t *state,
                              const render_geometry_t *geometry)
{
    soft_t *soft = (soft_t *)backend;
    soft_draw(soft->current, state, geometry);
}

static bool soft_last_frame_ready(render_backend_t *backend, int32_t width, int32_t height)
{
    const soft_t *soft = (const soft_t *)backend;
    return soft->last_valid && soft->last_w == width && soft->last_h == height;
}

/* `rect` of an image `width` x `height` into `rgba`, rows top first; false when it is not inside */
static bool soft_read(const uint8_t *image, int32_t width, int32_t height, const int32_t rect[4],
                      uint8_t *rgba)
{
    if (image == NULL || rect[0] < 0 || rect[1] < 0 || rect[2] < 0 || rect[3] < 0 ||
        rect[0] + rect[2] > width || rect[1] + rect[3] > height) {
        return false;
    }
    size_t row = (size_t)rect[2] * 4u;
    for (int32_t y = 0; y < rect[3]; y++) {
        memcpy(rgba + (size_t)y * row,
               image + (((size_t)(rect[1] + y) * (size_t)width) + (size_t)rect[0]) * 4u, row);
    }
    return true;
}

static bool soft_read_last_frame(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    const soft_t *soft = (const soft_t *)backend;
    return soft->last_valid && soft_read(soft->last, soft->last_w, soft->last_h, rect, rgba);
}

static nmo_status_t soft_read_pixels(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba)
{
    const soft_t *soft = (const soft_t *)backend;
    return soft_read(soft->screen.color, soft->screen.width, soft->screen.height, rect, rgba)
               ? NMO_OK
               : NMO_ERR_INVALID_ARGUMENT;
}

static void soft_destroy(render_backend_t *backend)
{
    soft_t *soft = (soft_t *)backend;
    soft_surface_free(&soft->screen);
    free(soft->target_depth);
    free(soft->target_stencil);
    free(soft->last);
    free(soft);
}

static const render_backend_ops_t soft_ops = {
    soft_destroy,
    soft_texture_create,
    soft_texture_destroy,
    soft_texture_image,
    soft_texture_levels,
    soft_texture_sub_image,
    soft_texture_target,
    soft_begin_screen,
    soft_end_screen,
    soft_begin_target,
    soft_end_target,
    soft_backend_clear,
    soft_backend_draw,
    soft_last_frame_ready,
    soft_read_last_frame,
    soft_read_pixels,
};

render_backend_t *render_soft_backend_create(void)
{
    soft_t *soft = (soft_t *)calloc(1, sizeof(*soft));
    if (soft == NULL) {
        return NULL;
    }
    soft->base.ops = &soft_ops;
    return &soft->base;
}

const uint8_t *render_soft_backend_screen(render_backend_t *backend, int32_t *out_width,
                                          int32_t *out_height)
{
    const soft_t *soft = (const soft_t *)backend;
    *out_width = soft->screen.width;
    *out_height = soft->screen.height;
    return soft->screen.color;
}

nmo_status_t nmo_render_create_soft(nmo_engine_t *engine, nmo_render_t **out_render)
{
    if (engine == NULL || out_render == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_render = NULL;
    render_backend_t *backend = render_soft_backend_create();
    if (backend == NULL) {
        return NMO_ERR_NOMEM;
    }
    return render_create(engine, backend, out_render);
}

const uint8_t *nmo_render_soft_pixels(const nmo_render_t *render, int *out_width, int *out_height)
{
    if (render == NULL || out_width == NULL || out_height == NULL) {
        return NULL;
    }
    int32_t width = 0, height = 0;
    const uint8_t *pixels = render_soft_backend_screen(render->backend, &width, &height);
    *out_width = width;
    *out_height = height;
    return pixels;
}
