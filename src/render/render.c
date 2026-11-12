/**
 * @file render.c
 * @brief The renderer's front end: the engine's frame placed in the target,
 *        handed to the backend, picked and saved. No graphics API here.
 */

#include "render_internal.h"
#include "core/nmo_arena.h"
#include "format/nmo_image.h"
#include "format/nmo_stb_adapter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void render_release_text(void *user_data, uintptr_t handle)
{
    render_backend_t *backend = (render_backend_t *)user_data;
    backend->ops->texture_destroy(backend, handle);
}

/* The state a device starts in (OpenGL's and Direct3D's defaults) */
static void render_default_state(render_state_t *s)
{
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memset(s, 0, sizeof(*s));
    s->antialias = true;
    memcpy(s->projection, identity, sizeof(identity));
    memcpy(s->model_view, identity, sizeof(identity));
    memcpy(s->texture_matrix, identity, sizeof(identity));
    s->sampler.min_filter = RENDER_FILTER_NEAREST;
    s->sampler.mip = RENDER_MIP_LINEAR;
    s->sampler.mag_filter = RENDER_FILTER_LINEAR;
    s->sampler.address = 1;
    s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;
    s->front_ccw = true;
    s->alpha_func = 8;       /* VXCMP_ALWAYS */
    s->source_blend = 2;     /* VXBLEND_ONE */
    s->dest_blend = 1;       /* VXBLEND_ZERO */
    s->depth_func = 2;       /* VXCMP_LESS */
    s->depth_write = true;
    s->color_write = true;
    s->stencil_func = 8;
    s->stencil_mask = s->stencil_write_mask = 0xFFFFFFFFu;
    s->stencil_fail = s->stencil_zfail = s->stencil_pass = 1; /* VXSTENCILOP_KEEP */
    s->fog_mode = 1;
    s->fog_end = 1.0f;
    s->fog_density = 1.0f;
}

nmo_status_t render_create(nmo_engine_t *engine, render_backend_t *backend,
                           nmo_render_t **out_render)
{
    nmo_render_t *render = (nmo_render_t *)calloc(1, sizeof(*render));
    if (render == NULL) {
        backend->ops->destroy(backend);
        return NMO_ERR_NOMEM;
    }
    render->engine = engine;
    render->backend = backend;
    render_default_state(&render->state);
    render->frame_width = 640;
    render->frame_height = 480;
    render->text = render_text_open();
    *out_render = render;
    return NMO_OK;
}

void nmo_render_destroy(nmo_render_t *render)
{
    if (render == NULL) {
        return;
    }
    render_text_close(render->text, render_release_text, render->backend);
    render_textures_free(render);
    render_mesh_cache_free(&render->meshes);
    free(render->lit_colors);
    free(render->lit_speculars);
    render->backend->ops->destroy(render->backend);
    free(render);
}

void nmo_render_set_mipmaps(nmo_render_t *render, bool all)
{
    if (render != NULL) {
        render->mipmap_all = all;
    }
}

void nmo_render_set_font_loader(nmo_render_t *render, nmo_render_font_loader_fn loader,
                                void *user_data)
{
    if (render != NULL) {
        render_text_set_font_loader(render->text, loader, user_data);
    }
}

/* The engine's frame, kept in its aspect and centered in the target, drawn by the backend */
static nmo_status_t render_frame(nmo_render_t *render, int target_w, int target_h)
{
    nmo_engine_frame_t frame;
    nmo_status_t status = nmo_engine_render_frame(render->engine, &frame);
    if (status != NMO_OK) {
        return status;
    }
    render->frame_width = frame.width;
    render->frame_height = frame.height;
    float scale = fminf((float)target_w / (float)frame.width, (float)target_h / (float)frame.height);
    render->rect[2] = (int)((float)frame.width * scale);
    render->rect[3] = (int)((float)frame.height * scale);
    render->rect[0] = (target_w - render->rect[2]) / 2;
    render->rect[1] = (target_h - render->rect[3]) / 2;
    status = render_draw_main(render, &frame, target_w, target_h);
    render_text_end_frame(render->text, render_release_text, render->backend);
    render->frame = frame;
    render->has_frame = true;
    return status;
}

nmo_status_t nmo_render_draw(nmo_render_t *render, int width, int height)
{
    if (render == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return render_frame(render, width, height);
}

void nmo_render_pick(nmo_render_t *render)
{
    if (render == NULL || !render->has_frame) {
        return;
    }
    const nmo_engine_frame_t *frame = &render->frame;
    float x = 0.0f, y = 0.0f;
    nmo_engine_mouse_position(render->engine, &x, &y);
    nmo_object_id_t under = 0;
    for (int32_t i = frame->draw_count - 1; i >= 0 && under == 0; i--) {
        const nmo_engine_draw_t *draw = &frame->draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_QUAD && draw->material != 0 && x >= draw->rect[0] &&
            x <= draw->rect[2] && y >= draw->rect[1] && y <= draw->rect[3]) {
            under = draw->object;
        }
    }
    nmo_engine_set_entity_under_mouse(render->engine, under);
}

void nmo_render_target_to_frame(const nmo_render_t *render, float x, float y, float *out_x,
                                float *out_y)
{
    float px = x - (float)render->rect[0];
    float py = y - (float)render->rect[1];
    *out_x = render->rect[2] > 0 ? px * (float)render->frame_width / (float)render->rect[2] : 0.0f;
    *out_y = render->rect[3] > 0 ? py * (float)render->frame_height / (float)render->rect[3] : 0.0f;
}

void nmo_render_frame_size(const nmo_render_t *render, int *out_width, int *out_height)
{
    *out_width = render != NULL ? render->frame_width : 0;
    *out_height = render != NULL ? render->frame_height : 0;
}

nmo_status_t nmo_render_screenshot(nmo_render_t *render, int width, int height, const char *path)
{
    if (render == NULL || path == NULL || width <= 0 || height <= 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t render_status = render_frame(render, width, height);
    if (render_status != NMO_OK) {
        return render_status;
    }
    const int *rect = render->rect;
    uint8_t *pixels = (uint8_t *)malloc((size_t)rect[2] * 4u * (size_t)rect[3]);
    nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 20);
    nmo_status_t status = NMO_ERR_NOMEM;
    if (pixels != NULL && arena != NULL) {
        status = render->backend->ops->read_pixels(render->backend, rect, pixels);
    }
    if (status == NMO_OK) {
        size_t size = 0;
        uint8_t *png = nmo_stbi_write_to_memory(arena, NMO_BITMAP_FORMAT_PNG, rect[2], rect[3], 4,
                                                pixels, 0, &size);
        FILE *file = png != NULL ? fopen(path, "wb") : NULL;
        status = file != NULL && fwrite(png, 1, size, file) == size ? NMO_OK : NMO_ERR_CANT_WRITE_FILE;
        if (file != NULL) {
            fclose(file);
        }
    }
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    free(pixels);
    return status;
}
