/**
 * @file render_internal.h
 * @brief The renderer and what a backend gives it.
 *
 * The front end (render.c) knows no graphics API: it takes the engine's
 * frame, places it in the target, keeps the sprite texts' pixels and picks.
 * A backend draws the frame with its API and reads back what it drew.
 */

#ifndef NMO_RENDER_INTERNAL_H
#define NMO_RENDER_INTERNAL_H

#include "render/nmo_render.h"
#include "engine/nmo_engine_render.h"
#include "render_text.h"

typedef struct render_backend render_backend_t;

typedef struct render_backend_ops {
    /* Free what the backend made; its API's context is still current. */
    void (*destroy)(render_backend_t *backend);
    /*
     * Draw `frame` into the target `target_w` x `target_h`, the frame shown
     * at `rect` (left, top from the target's top, width, height in pixels).
     */
    nmo_status_t (*draw)(render_backend_t *backend, const nmo_engine_frame_t *frame,
                         int target_w, int target_h, const int rect[4]);
    /*
     * The pixels of `rect` (left, top from the top, width, height) of the
     * target drawn last, `target_h` high: RGBA, rows top first.
     */
    nmo_status_t (*read_pixels)(render_backend_t *backend, int target_h, const int rect[4],
                                uint8_t *rgba);
    /* A sprite text's image the backend made from its pixels goes */
    void (*release_text)(render_backend_t *backend, uintptr_t handle);
} render_backend_ops_t;

struct render_backend {
    const render_backend_ops_t *ops;
    nmo_render_t *render; /* the renderer it draws for, set when it is made */
};

struct nmo_render {
    nmo_engine_t *engine;
    render_backend_t *backend;
    render_text_t *text;      /* the sprite texts' fonts and pixels */
    bool mipmap_all;          /* mipmap every texture, not only CK's mipmapped ones */
    int frame_width, frame_height; /* the render context of the frame drawn last */
    int rect[4];              /* where it shows in the target: left, top, width, height */
    nmo_engine_frame_t frame; /* the frame drawn last (the engine's until the next one) */
    bool has_frame;
};

/* A renderer drawing through `backend`, which it frees when it is destroyed */
nmo_status_t render_create(nmo_engine_t *engine, render_backend_t *backend,
                           nmo_render_t **out_render);

#endif /* NMO_RENDER_INTERNAL_H */
