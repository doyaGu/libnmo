/**
 * @file render_internal.h
 * @brief The renderer: the front end that walks the engine's frame as CK2's
 *        rasterizer does, and the backend it draws through.
 *
 * The front end (render.c, render_frame.c, render_draw.c, render_texture.c)
 * knows no graphics API. It keeps the textures' and meshes' prepared data and
 * one render_state_t that it changes as CK's rasterizer changes Direct3D's
 * states, from frame to frame, and hands the backend each draw with the
 * state as it is then (render_backend.h).
 */

#ifndef NMO_RENDER_INTERNAL_H
#define NMO_RENDER_INTERNAL_H

#include "render/nmo_render.h"
#include "engine/nmo_engine_render.h"
#include "render_backend.h"
#include "render_mesh.h"
#include "render_text.h"

/* A CKTexture as the backend has it */
typedef struct render_texture {
    nmo_object_id_t id;
    uintptr_t handle;
    uint32_t version;
    bool empty;     /* no image: drawn untextured */
    bool mipmapped; /* uploaded with its mipmaps */
    bool forced;    /* mipmapped by nmo_render_set_mipmaps, not by CK */
    bool target;    /* a render pass's: what it drew, not the engine's image */
    bool cube;      /* a cube map: its six faces, no mipmaps */
    int32_t target_w, target_h;
} render_texture_t;

struct nmo_render {
    nmo_engine_t *engine;
    render_backend_t *backend;
    render_text_t *text;      /* the sprite texts' fonts and pixels */
    bool mipmap_all;          /* mipmap every texture, not only CK's mipmapped ones */
    int frame_width, frame_height; /* the render context of the frame drawn last */
    int rect[4];              /* where it shows in the target: left, top, width, height */
    nmo_engine_frame_t frame; /* the frame drawn last (the engine's until the next one) */
    bool has_frame;

    render_state_t state;     /* the device's state, as the draws left it */
    render_texture_t *textures;
    size_t texture_count, texture_capacity;
    render_mesh_cache_t meshes;
    /* The vertex colors and speculars of the lit mesh being drawn */
    uint8_t *lit_colors, *lit_speculars;
    int32_t lit_capacity;

    /* The frame being drawn */
    int32_t view[4];      /* where it shows in its target: left, top, width, height */
    int32_t scene[4];     /* where its 3D draws go: left, top, width, height */
    float eye[16];        /* its camera's world matrix: the scene's root, the texgen referential */
    int32_t shading;      /* the main frame's global render mode: 0 wireframe, 1 flat */
    bool use_textures;
    bool wireframe_layer; /* the frame being drawn's global render mode draws a wireframe over meshes */
};

/* A renderer drawing through `backend`, which it frees when it is destroyed */
nmo_status_t render_create(nmo_engine_t *engine, render_backend_t *backend,
                           nmo_render_t **out_render);

/* render_texture.c */
/* The backend's texture of a CKTexture, uploaded again when its image changes; 0 for none */
uintptr_t render_texture(nmo_render_t *render, nmo_object_id_t id);
/* Its entry as render_texture() last left it; NULL for none */
render_texture_t *render_texture_entry(nmo_render_t *render, nmo_object_id_t id);
void render_textures_free(nmo_render_t *render);

/* render_draw.c */
/* RCKMaterial::SetAsCurrent: texture, blending and tests (`depth`: its z states too) */
void render_set_material(nmo_render_t *render, const nmo_engine_material_t *m, bool depth,
                         const float *world);
/* RCKMaterial::TexGenEffect; NULL for none */
void render_set_texgen(nmo_render_t *render, const nmo_engine_material_t *m, const float *world);
void render_draw_mesh(nmo_render_t *render, const nmo_engine_frame_t *frame,
                      const nmo_engine_draw_t *draw, const float view_m[16]);
void render_draw_quad(nmo_render_t *render, const nmo_engine_draw_t *draw);
void render_draw_sprite_text(nmo_render_t *render, const nmo_engine_draw_t *draw);
void render_draw_triangles(nmo_render_t *render, const nmo_engine_frame_t *frame,
                           const nmo_engine_draw_t *draw, const float view_m[16]);
void render_draw_sprite3d(nmo_render_t *render, const nmo_engine_frame_t *frame,
                          const nmo_engine_draw_t *draw, const float view_m[16]);
void render_draw_primitive(nmo_render_t *render, const nmo_engine_frame_t *frame,
                           const nmo_engine_draw_t *draw, const float view_m[16]);
/* The draw's stencil test (the render context's states it was made with) and its winding */
void render_draw_state(nmo_render_t *render, const nmo_engine_draw_t *draw);
/* The projection of the frame, or of a draw with its own */
void render_set_projection(nmo_render_t *render, const nmo_engine_frame_t *frame,
                           bool orthographic, float fov, float near_plane, float far_plane);

/* render_frame.c */
/* Draw the frame into the screen `width` x `height`, the frame at render->rect */
nmo_status_t render_draw_main(nmo_render_t *render, const nmo_engine_frame_t *frame, int width,
                              int height);

#endif /* NMO_RENDER_INTERNAL_H */
