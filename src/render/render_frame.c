/**
 * @file render_frame.c
 * @brief A frame as CKRenderContext draws it: the render passes, the copies
 *        of the frame before, the clear, the background, the 3D draws, the
 *        additional views and the 2D entities, in the target's pixels.
 */

#include "render_internal.h"
#include "render_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const float frame_identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void frame_rect(int32_t out[4], const int32_t rect[4])
{
    memcpy(out, rect, 4 * sizeof(int32_t));
}

/* The 2D pass: the render context's pixels, no depth, no fog */
static void frame_begin_2d(nmo_render_t *render, const nmo_engine_frame_t *frame)
{
    render_state_t *s = &render->state;
    s->projection_kind = RENDER_PROJECTION_2D;
    s->projection_rect[0] = (float)frame->width;
    s->projection_rect[1] = (float)frame->height;
    memcpy(s->model_view, frame_identity, sizeof(s->model_view));
    s->fog = false;
    s->depth_test = false;
    s->depth_write = false;
    s->fill = RENDER_FILL_SOLID;
}

/* The 3D pass: CKRenderedScene's camera, default states and fog */
static void frame_begin_3d(nmo_render_t *render, const nmo_engine_frame_t *frame, float out_view[16])
{
    render_state_t *s = &render->state;
    render_set_projection(render, frame, frame->orthographic, frame->fov, frame->near_plane,
                          frame->far_plane);
    render_inverse(out_view, frame->view);
    s->front_ccw = false;
    s->depth_test = true;
    s->depth_func = 4; /* VXCMP_LESSEQUAL */
    s->depth_write = true;
    if (frame->fog_mode != 0) {
        s->fog = true;
        s->fog_mode = frame->fog_mode;
        s->fog_color = frame->fog_color;
        s->fog_start = frame->fog_start;
        s->fog_end = frame->fog_end;
        s->fog_density = frame->fog_density;
    } else {
        s->fog = false;
    }
    memcpy(s->model_view, out_view, sizeof(s->model_view));
}

/* A Place's viewport clipping */
static void frame_clip_draw(nmo_render_t *render, const nmo_engine_draw_t *draw)
{
    render_state_t *s = &render->state;
    if (!draw->clipped) {
        s->scissor = false;
        return;
    }
    const int32_t *scene = render->scene;
    int edges[4];
    for (int i = 0; i < 4; i++) {
        float normalized = fminf(fmaxf(draw->clip_rect[i], 0), 1);
        edges[i] = (int)(normalized * (float)((i & 1) ? scene[3] : scene[2]));
    }
    s->scissor = true;
    s->scissor_rect[0] = scene[0] + edges[0];
    s->scissor_rect[1] = scene[1] + edges[1];
    s->scissor_rect[2] = edges[2] > edges[0] ? edges[2] - edges[0] : 0;
    s->scissor_rect[3] = edges[3] > edges[1] ? edges[3] - edges[1] : 0;
}

static void frame_clear(nmo_render_t *render, bool color, bool depth, bool stencil,
                        const float value[4])
{
    render->backend->ops->clear(render->backend, &render->state, color, depth, stencil, value);
}

/* A frame's draws into render->view of its target: its clear, background, 2D entities and 3D
   draws */
static void frame_draw(nmo_render_t *render, const nmo_engine_frame_t *frame)
{
    render_state_t *s = &render->state;
    render->wireframe_layer = frame->wireframe_layer;
    /* VXRENDERSTATE_ANTIALIAS: the samples are there; they smooth only while a block turned it on */
    s->antialias = frame->antialias;
    /* The frame's viewport in the target, for its 3D draws (the whole frame without one) */
    int32_t *scene = render->scene;
    const int32_t *view = render->view;
    const int32_t *viewport = frame->viewport;
    if (viewport[2] > 0 && viewport[3] > 0 && frame->width > 0 && frame->height > 0) {
        float sx = (float)view[2] / (float)frame->width, sy = (float)view[3] / (float)frame->height;
        scene[1] = view[1] + (int)((float)viewport[1] * sy);
        scene[2] = (int)((float)viewport[2] * sx);
        scene[3] = (int)((float)viewport[3] * sy);
        scene[0] = view[0] + (int)((float)viewport[0] * sx);
    } else {
        frame_rect(scene, view);
    }
    frame_rect(s->viewport, view);
    s->scissor = true;
    /* CK_RENDER_CLEARVIEWPORT: the viewport alone */
    frame_rect(s->scissor_rect, (frame->render_options & 0x100u) ? scene : view);
    float background[4];
    render_color(frame->background_color, background);
    background[3] = 1.0f;
    /* RCKRenderContext::Clear: the background only with CK_RENDER_CLEARBACK, depth with CLEARZ */
    bool clear_color = (frame->render_options & 0x20u) != 0;
    bool clear_depth = (frame->render_options & 0x10u) != 0;
    bool clear_stencil = (frame->render_options & 0x40u) != 0;
    if (clear_color || clear_depth || clear_stencil) {
        s->depth_write = true; /* a clear writes only what the masks let through */
        s->color_write = true;
        s->stencil_write_mask = 0xFF;
        frame_clear(render, clear_color, clear_depth, clear_stencil, background);
    }
    s->scissor = false;

    if (frame->background_texture != 0 && (frame->render_options & 0x20u)) {
        nmo_engine_draw_t quad;
        memset(&quad, 0, sizeof(quad));
        quad.rect[2] = (float)frame->width;
        quad.rect[3] = (float)frame->height;
        quad.uv[2] = quad.uv[3] = 1.0f;
        quad.texture = frame->background_texture;
        quad.color = quad.color2 = frame->background_color | 0xFF000000u;
        frame_begin_2d(render, frame);
        render_draw_quad(render, &quad);
    }

    /* Background 2D entities, then the 3D draws in the frame's order (the engine
       puts the transparent objects after the opaque ones, sorted as the scene
       graph sorts them), the post primitives, and the foreground 2D entities */
    int32_t first_mesh = 0;
    while (first_mesh < frame->draw_count && frame->draws[first_mesh].kind == NMO_ENGINE_DRAW_QUAD &&
           frame->draws[first_mesh].background) {
        first_mesh++;
    }
    frame_begin_2d(render, frame);
    for (int32_t i = 0; i < first_mesh; i++) {
        render_draw_quad(render, &frame->draws[i]);
    }
    int32_t end_mesh = first_mesh;
    while (end_mesh < frame->draw_count && frame->draws[end_mesh].kind != NMO_ENGINE_DRAW_QUAD &&
           frame->draws[end_mesh].kind != NMO_ENGINE_DRAW_SPRITE_TEXT) {
        end_mesh++;
    }
    float view_m[16];
    frame_rect(s->viewport, scene);
    frame_begin_3d(render, frame, view_m);
    memcpy(render->eye, frame->view, sizeof(render->eye));
    for (int32_t i = first_mesh; i < end_mesh; i++) {
        const nmo_engine_draw_t *draw = &frame->draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_PRIMITIVE && draw->post) {
            continue; /* after the others */
        }
        frame_clip_draw(render, draw);
        render_draw_state(render, draw);
        if (draw->kind == NMO_ENGINE_DRAW_TRIANGLES || draw->kind == NMO_ENGINE_DRAW_LINES) {
            render_draw_triangles(render, frame, draw, view_m);
        } else if (draw->kind == NMO_ENGINE_DRAW_SPRITE3D) {
            render_draw_sprite3d(render, frame, draw, view_m);
        } else if (draw->kind == NMO_ENGINE_DRAW_PRIMITIVE) {
            render_draw_primitive(render, frame, draw, view_m);
        } else {
            render_draw_mesh(render, frame, draw, view_m);
        }
    }
    for (int32_t i = first_mesh; i < end_mesh; i++) {
        const nmo_engine_draw_t *draw = &frame->draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_PRIMITIVE && draw->post) {
            frame_clip_draw(render, draw);
            if (draw->clears_stencil) {
                s->stencil_write_mask = 0xFF;
                s->clear_stencil = draw->stencil_clear;
                frame_clear(render, false, false, true, background);
                continue;
            }
            render_draw_state(render, draw);
            render_draw_primitive(render, frame, draw, view_m);
        }
    }
    s->stencil = false;
    s->front_ccw = false;
    s->scissor = false;
    /* Additional View's renders after the 3D, then the 2D, then those over the 2D */
    for (int32_t v = 0; v < frame->view_count; v++) {
        if (!frame->views[v].over_2d) {
            frame_draw(render, &frame->views[v].frame);
        }
    }
    frame_rect(s->viewport, view);
    frame_begin_2d(render, frame);
    for (int32_t i = end_mesh; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_SPRITE_TEXT) {
            render_draw_sprite_text(render, &frame->draws[i]);
        } else if (frame->draws[i].stencil.enabled) {
            render_draw_state(render, &frame->draws[i]); /* a quad drawn where the stencil says (a shadow's) */
            render_draw_quad(render, &frame->draws[i]);
            s->stencil = false;
        } else {
            render_draw_quad(render, &frame->draws[i]);
        }
    }
    for (int32_t v = 0; v < frame->view_count; v++) {
        if (frame->views[v].over_2d) {
            frame_draw(render, &frame->views[v].frame);
        }
    }
    frame_rect(s->viewport, view);
}

/* A render pass: into the texture its pass names (a cube map's face of its slot), which then shows
   what was drawn */
static void frame_draw_pass(nmo_render_t *render, const nmo_engine_pass_t *pass)
{
    render_backend_t *backend = render->backend;
    render_texture(render, pass->texture); /* its entry */
    render_texture_t *texture = render_texture_entry(render, pass->texture);
    if (texture == NULL || texture->handle == 0) {
        return;
    }
    /* A cube map's pass draws the face of its slot (SetRenderTarget's CubeMapFace) */
    bool cube = nmo_engine_texture_cube_map(render->engine, pass->texture);
    render_texture_kind_t kind = cube ? RENDER_TEXTURE_CUBE : RENDER_TEXTURE_2D;
    if (!texture->target || texture->cube != cube || texture->target_w != pass->width ||
        texture->target_h != pass->height) {
        bool kept = texture->cube; /* faces uploaded from the image stay until drawn over */
        texture->target = true;
        texture->cube = cube;
        texture->empty = false;
        texture->mipmapped = texture->forced = false;
        texture->target_w = pass->width;
        texture->target_h = pass->height;
        backend->ops->texture_target(backend, texture->handle, kind, pass->width, pass->height, kept);
    }
    int32_t face = cube ? (pass->slot >= 0 && pass->slot < 6 ? pass->slot : 0) : 0;
    if (backend->ops->begin_target(backend, texture->handle, kind, face, pass->width, pass->height)) {
        frame_rect(render->view, pass->viewport);
        frame_draw(render, &pass->frame);
        backend->ops->end_target(backend);
    }
}

/*
 * The frame's buffer copies (CopyToTexture), before it is drawn: CK copies
 * the back buffer as the block runs, so the copies hold the frame drawn
 * before this one. Each source rectangle of the render context, as the
 * target scaled it, is taken nearest to its texel size into the texture's
 * uploaded image, top row first. Only a 2D texture's face 0 is copied.
 */
static void frame_copy_buffers(nmo_render_t *render, const nmo_engine_frame_t *frame, int width,
                               int height)
{
    render_backend_t *backend = render->backend;
    if (frame->width <= 0 || frame->height <= 0 || frame->copy_count <= 0) {
        return;
    }
    if (!backend->ops->last_frame_ready(backend, width, height)) {
        return;
    }
    const int32_t *view = render->view;
    float sx = (float)view[2] / (float)frame->width, sy = (float)view[3] / (float)frame->height;
    for (int32_t c = 0; c < frame->copy_count; c++) {
        const nmo_engine_buffer_copy_t *copy = &frame->copies[c];
        uintptr_t handle = render_texture(render, copy->texture);
        const render_texture_t *uploaded = render_texture_entry(render, copy->texture);
        if (handle == 0 || uploaded == NULL || uploaded->cube || copy->face != 0) {
            continue;
        }
        int w = (int)(copy->dest[2] - copy->dest[0]), h = (int)(copy->dest[3] - copy->dest[1]);
        int32_t rect[4] = {view[0] + (int)(copy->src[0] * sx), view[1] + (int)(copy->src[1] * sy),
                           (int)((float)w * sx), (int)((float)h * sy)};
        if (w <= 0 || h <= 0 || rect[2] <= 0 || rect[3] <= 0) {
            continue;
        }
        uint8_t *read = (uint8_t *)malloc((size_t)rect[2] * (size_t)rect[3] * 4u);
        uint8_t *texels = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
        if (read != NULL && texels != NULL && backend->ops->read_last_frame(backend, rect, read)) {
            for (int ty = 0; ty < h; ty++) {
                int ry = (int)((float)ty * sy);
                for (int tx = 0; tx < w; tx++) {
                    int rx = (int)((float)tx * sx);
                    memcpy(&texels[((size_t)ty * (size_t)w + (size_t)tx) * 4u],
                           &read[((size_t)ry * (size_t)rect[2] + (size_t)rx) * 4u], 4u);
                }
            }
            backend->ops->texture_sub_image(backend, handle, (int32_t)copy->dest[0],
                                            (int32_t)copy->dest[1], w, h, texels);
        }
        free(read);
        free(texels);
    }
}

nmo_status_t render_draw_main(nmo_render_t *render, const nmo_engine_frame_t *frame, int width,
                              int height)
{
    render_backend_t *backend = render->backend;
    render_state_t *s = &render->state;
    render->shading = frame->shading;
    render->use_textures = frame->use_textures;
    for (int32_t p = 0; p < frame->pass_count; p++) {
        frame_draw_pass(render, &frame->passes[p]);
    }
    frame_rect(render->view, render->rect);
    frame_copy_buffers(render, frame, width, height);
    /* Without CK_RENDER_CLEARBACK the frames before stay under the new one (Global Blur) */
    if (!backend->ops->begin_screen(backend, width, height, !(frame->render_options & 0x20u))) {
        const int32_t whole[4] = {0, 0, width, height};
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        frame_rect(s->viewport, whole);
        s->depth_write = true; /* a clear writes only what the masks let through */
        s->color_write = true;
        frame_clear(render, true, true, false, black);
    }
    frame_draw(render, frame);
    backend->ops->end_screen(backend);
    return NMO_OK;
}
