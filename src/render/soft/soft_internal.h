/**
 * @file soft_internal.h
 * @brief The software backend's surfaces and textures.
 */

#ifndef NMO_RENDER_SOFT_INTERNAL_H
#define NMO_RENDER_SOFT_INTERNAL_H

#include "../render_backend.h"

#define SOFT_MAX_LEVELS 16

typedef struct soft_level {
    int32_t width, height;
    uint8_t *rgba; /* rows top first */
} soft_level_t;

typedef struct soft_texture {
    render_texture_kind_t kind;
    int32_t last; /* the last level sampled */
    soft_level_t faces[6][SOFT_MAX_LEVELS];
} soft_texture_t;

/* What draws go into: RGBA rows top first, a depth (0 near .. 1 far) and an 8 bit stencil a pixel */
typedef struct soft_surface {
    int32_t width, height;
    uint8_t *color;
    float *depth;
    uint8_t *stencil;
} soft_surface_t;

typedef struct soft {
    render_backend_t base;
    soft_surface_t screen;
    /* A texture being drawn into: its level 0 face as color, with depth and stencil of its own */
    soft_surface_t target;
    float *target_depth;
    uint8_t *target_stencil;
    int32_t target_buffers_w, target_buffers_h;
    soft_surface_t *current;
    /* The screen drawn last, for read_last_frame */
    uint8_t *last;
    int32_t last_w, last_h;
    bool last_valid;
} soft_t;

/* soft_raster.c */
void soft_draw(soft_surface_t *surface, const render_state_t *state, const render_geometry_t *geometry);
void soft_clear(soft_surface_t *surface, const render_state_t *state, bool color, bool depth,
                bool stencil, const float value[4]);

#endif /* NMO_RENDER_SOFT_INTERNAL_H */
