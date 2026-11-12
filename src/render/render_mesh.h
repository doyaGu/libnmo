/**
 * @file render_mesh.h
 * @brief A mesh made ready to draw: its faces split by material group, its
 *        prelit colors as bytes, and the Direct3D rules a mesh draws by
 *        (wrapped coordinates, folded channels).
 */

#ifndef NMO_RENDER_MESH_H
#define NMO_RENDER_MESH_H

#include "engine/nmo_engine_render.h"

#include <stddef.h>
#include <stdint.h>

/* A mesh's faces split by material group, and its prelit colors as RGBA bytes */
typedef struct render_mesh {
    nmo_object_id_t id;
    int32_t group_count;
    uint16_t **indices;
    int32_t *counts;
    uint8_t *colors;
    uint8_t *speculars; /* the prelit specular colors as RGBA bytes; NULL when all black */
    uint32_t version;   /* the mesh's colors were converted at */
} render_mesh_t;

/* The mesh's faces as index lists, one per material group, and its colors */
void render_mesh_fill(render_mesh_t *cached, const nmo_engine_mesh_t *mesh);
/* The index lists go (the colors stay: render_mesh_fill replaces them) */
void render_mesh_release(render_mesh_t *cached);

/* The meshes made ready, by id; made again when a mesh's version changes */
typedef struct render_mesh_cache {
    render_mesh_t *meshes;
    size_t count, capacity;
} render_mesh_cache_t;

render_mesh_t *render_mesh_cache_get(render_mesh_cache_t *cache, nmo_object_id_t id,
                                     const nmo_engine_mesh_t *mesh);
void render_mesh_cache_free(render_mesh_cache_t *cache);

/*
 * RCKMesh folds a lit channel that multiplies (ZERO/SRCCOLOR or DESTCOLOR/ZERO)
 * into the mesh's pass as a texture stage: only its texture modulates the lit
 * color, the channel material's own lighting takes no part.
 */
bool render_channel_folds(const nmo_engine_channel_t *channel, const nmo_engine_material_t *material,
                          bool prelit);

/* A mesh with VXMESH_WRAPU/WRAPV drawn as faces of their own, each face's UVs moved the short way */
typedef struct render_wrapped {
    nmo_vertex_t *vertices;
    uint32_t *colors, *speculars;
    uint16_t *indices;
    nmo_engine_channel_t *channels;
    float **channel_uvs;
    int32_t channel_count;
} render_wrapped_t;

/*
 * Direct3D interpolates a wrapped coordinate along the shorter way round, per
 * triangle: each face gets vertices of its own, the second and third
 * coordinates taken to within half a unit of the first. Only the first
 * coordinate set wraps (WRAP0); the channels' sets are copied as they are.
 * `mesh` then points at `w`'s arrays; false when they could not be made.
 */
bool render_wrap_mesh(nmo_engine_mesh_t *mesh, render_wrapped_t *w);
void render_wrapped_free(render_wrapped_t *w);

#endif /* NMO_RENDER_MESH_H */
