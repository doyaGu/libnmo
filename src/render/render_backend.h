/**
 * @file render_backend.h
 * @brief What a renderer backend draws: Direct3D 7's fixed function state,
 *        vertex streams, textures and render targets, with no graphics API.
 *
 * The front end walks the engine's frame the way CK2's rasterizer does and
 * says, for each draw, the whole state to draw it with: the vertices are
 * lit already (Direct3D's lighting is done on the CPU), so a backend
 * transforms them, takes one texture stage (with its coordinate generation),
 * adds the specular, fogs, tests and blends. The enums are Virtools' (VXCMP,
 * VXBLEND_MODE, VXSTENCILOP, VXTEXTURE_ADDRESSMODE), as the engine has them.
 *
 * Rectangles are in pixels of the target being drawn, from its top left.
 * Matrices are VxMatrix's rows (row vectors); projections are Direct3D's
 * (depth 0..1): a backend converts them to its device's conventions,
 * including the orientation of a texture it draws into.
 */

#ifndef NMO_RENDER_BACKEND_H
#define NMO_RENDER_BACKEND_H

#include "render/nmo_render.h"

#include <stddef.h>
#include <stdint.h>

typedef enum render_texture_kind {
    RENDER_TEXTURE_NONE = 0,
    RENDER_TEXTURE_2D = 1,
    RENDER_TEXTURE_CUBE = 2, /* faces +X, -X, +Y, -Y, +Z, -Z; rows as CK has them */
} render_texture_kind_t;

/* How the texture combines with the vertex color (the texture stage's operation) */
typedef enum render_texenv {
    RENDER_TEXENV_MODULATE = 0, /* texture times color, alpha too */
    RENDER_TEXENV_DECAL = 1,    /* texture over color by the texture's alpha, the color's alpha */
    RENDER_TEXENV_REPLACE = 2,  /* the texture */
    RENDER_TEXENV_ADD = 3,      /* texture plus color, alphas multiplied */
} render_texenv_t;

/* Where the texture coordinates come from, before the texture matrix */
typedef enum render_texgen {
    RENDER_TEXGEN_NONE = 0,    /* the vertices' */
    RENDER_TEXGEN_REFLECT = 1, /* the eye space reflection vector */
    RENDER_TEXGEN_NORMAL = 2,  /* the eye space normal */
    RENDER_TEXGEN_EYE = 3,     /* the eye space position */
} render_texgen_t;

typedef enum render_filter {
    RENDER_FILTER_NEAREST = 0,
    RENDER_FILTER_LINEAR = 1,
} render_filter_t;

typedef enum render_mip {
    RENDER_MIP_NONE = 0, /* the image itself */
    RENDER_MIP_NEAREST = 1,
    RENDER_MIP_LINEAR = 2,
} render_mip_t;

typedef enum render_fill {
    RENDER_FILL_SOLID = 0,
    RENDER_FILL_LINE = 1,
    RENDER_FILL_POINT = 2,
} render_fill_t;

typedef enum render_projection_kind {
    /* `projection`: Direct3D's, for the 3D draws */
    RENDER_PROJECTION_MATRIX = 0,
    /* render context pixels: x 0..projection_rect[0] right, y 0..projection_rect[1] down,
       z -1..1 (the 2D entities') */
    RENDER_PROJECTION_2D = 1,
    /* pixels of the frame's viewport, projection_rect left, top, width, height, z its depth
       from 0 (near) to 1 (transformed vertices') */
    RENDER_PROJECTION_SCREEN = 2,
} render_projection_kind_t;

typedef enum render_primitive {
    RENDER_PRIMITIVE_TRIANGLES = 0,
    RENDER_PRIMITIVE_LINES = 1,
    RENDER_PRIMITIVE_TRIANGLE_FAN = 2,
} render_primitive_t;

typedef struct render_sampler {
    render_filter_t min_filter, mag_filter;
    render_mip_t mip;
    int32_t address;       /* VXTEXTURE_ADDRESSMODE: 1 wrap, 2 mirror, 3 clamp, 4 border, 5 mirror once */
    uint32_t border_color; /* ARGB, with address 4 */
    bool anisotropic;      /* mipmaps sampled anisotropically, as far as the device goes (8 at most) */
} render_sampler_t;

/*
 * The state a draw is made with: all of it, every draw. The front end keeps
 * one and changes what CK's rasterizer changes, so a backend may apply the
 * fields a draw uses and ignore the others.
 */
typedef struct render_state {
    int32_t viewport[4];     /* left, top, width, height */
    bool scissor;
    int32_t scissor_rect[4]; /* left, top, width, height */
    bool antialias;          /* VXRENDERSTATE_ANTIALIAS: the target's samples smooth edges */

    render_projection_kind_t projection_kind;
    float projection[16];
    float projection_rect[4];
    float depth_scale;       /* not 0: the projection's depth translation ([3][2]) times this */
    float model_view[16];    /* model to eye space */

    uintptr_t texture;       /* a backend texture; 0 with RENDER_TEXTURE_NONE */
    render_texture_kind_t texture_kind;
    render_sampler_t sampler;
    render_texenv_t texenv;
    render_texgen_t texgen;
    float texture_matrix[16]; /* the coordinates (s, t, r, 1) times this */

    float color[4];          /* r, g, b, a: the vertices' color when the geometry has none */
    uint8_t secondary[3];    /* r, g, b: their specular, likewise */
    bool color_sum;          /* the specular added after texturing */
    bool flat;               /* flat shading: a face takes one vertex's color */
    render_fill_t fill;
    bool cull;               /* back faces culled */
    bool front_ccw;          /* front faces are counter-clockwise on the target, not clockwise */

    bool alpha_test;
    int32_t alpha_func;      /* VXCMP */
    float alpha_ref;         /* 0..1 */
    bool blend;
    int32_t source_blend, dest_blend; /* VXBLEND_MODE */
    bool depth_test;
    int32_t depth_func;      /* VXCMP */
    bool depth_write;
    bool color_write;
    bool stencil;
    int32_t stencil_func;    /* VXCMP */
    uint32_t stencil_ref, stencil_mask, stencil_write_mask;
    int32_t stencil_fail, stencil_zfail, stencil_pass; /* VXSTENCILOP */
    uint32_t clear_stencil;  /* the value a stencil clear writes */

    bool fog;
    int32_t fog_mode;        /* VXFOG_MODE: 1 exp, 2 exp2, 3 linear */
    uint32_t fog_color;      /* ARGB */
    float fog_start, fog_end, fog_density;
} render_state_t;

/*
 * Vertices to draw: positions, and what each has of normals (for texgen),
 * texture coordinates, colors (bytes, or floats) and specular colors (bytes:
 * red, green, blue at each stride); without a stream the state's value.
 * With indices, `index_count` of them name the vertices; else the vertices
 * are drawn in order.
 */
typedef struct render_geometry {
    render_primitive_t primitive;
    int32_t vertex_count;
    const float *positions;   /* x, y, z */
    size_t position_stride;
    const float *normals;
    size_t normal_stride;
    const float *uvs;         /* u, v */
    size_t uv_stride;
    const uint8_t *colors;    /* r, g, b, a: 4 bytes a vertex */
    const float *float_colors; /* r, g, b, a: 4 floats a vertex */
    const uint8_t *speculars; /* r, g, b at each specular_stride */
    size_t specular_stride;
    const uint16_t *indices;
    int32_t index_count;
} render_geometry_t;

typedef struct render_backend render_backend_t;

typedef struct render_backend_ops {
    /* Free what the backend made; its device is still there. */
    void (*destroy)(render_backend_t *backend);

    /* ---- textures: images of RGBA bytes, rows top first ---- */
    /* A new, empty texture; 0 when it cannot be made */
    uintptr_t (*texture_create)(render_backend_t *backend);
    void (*texture_destroy)(render_backend_t *backend, uintptr_t texture);
    /* Level `level` of face `face` (0 for a 2D texture) */
    void (*texture_image)(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                          int32_t face, int32_t level, int32_t width, int32_t height,
                          const uint8_t *rgba);
    /* The levels uploaded: 0 to `last` */
    void (*texture_levels)(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                           int32_t last);
    /* A part of level 0 of a 2D texture */
    void (*texture_sub_image)(render_backend_t *backend, uintptr_t texture, int32_t x, int32_t y,
                              int32_t width, int32_t height, const uint8_t *rgba);
    /*
     * Make the texture something to draw into: `width` x `height`, one level,
     * sampled linearly. A cube's faces that are already that size keep their
     * images when `keep_faces`.
     */
    void (*texture_target)(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                           int32_t width, int32_t height, bool keep_faces);

    /* ---- targets ---- */
    /*
     * Draw into the screen, `width` x `height`. With `preserve` the frame
     * before stays under what is drawn; true when it does (a backend that
     * cannot keep it draws over whatever is there).
     */
    bool (*begin_screen)(render_backend_t *backend, int32_t width, int32_t height, bool preserve);
    /* Show what was drawn, and keep it for read_last_frame */
    void (*end_screen)(render_backend_t *backend);
    /* Draw into face `face` of a texture made a target; false when it cannot be drawn into */
    bool (*begin_target)(render_backend_t *backend, uintptr_t texture, render_texture_kind_t kind,
                         int32_t face, int32_t width, int32_t height);
    void (*end_target)(render_backend_t *backend);

    /* ---- drawing ---- */
    /*
     * Clear what the state's scissor leaves (all of the target without one):
     * the color to `color` (r, g, b, a) where it writes color, the depth to 1
     * where it writes depth, the stencil to clear_stencil under its write mask.
     */
    void (*clear)(render_backend_t *backend, const render_state_t *state, bool color, bool depth,
                  bool stencil, const float color_value[4]);
    void (*draw)(render_backend_t *backend, const render_state_t *state,
                 const render_geometry_t *geometry);

    /* ---- reading ---- */
    /* The screen drawn last, `width` x `height`, is there to read */
    bool (*last_frame_ready)(render_backend_t *backend, int32_t width, int32_t height);
    /* Its pixels in `rect` (left, top, width, height): RGBA, rows top first */
    bool (*read_last_frame)(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba);
    /* The pixels of `rect` of the screen being drawn, before it is shown; rows top first */
    nmo_status_t (*read_pixels)(render_backend_t *backend, const int32_t rect[4], uint8_t *rgba);
} render_backend_ops_t;

struct render_backend {
    const render_backend_ops_t *ops;
};

#endif /* NMO_RENDER_BACKEND_H */
