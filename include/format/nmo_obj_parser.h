/**
 * @file nmo_obj_parser.h
 * @brief Wavefront OBJ file parser
 *
 * Parses OBJ text into triangulated geometry data.
 * All output arrays are arena-allocated.
 */

#ifndef NMO_OBJ_PARSER_H
#define NMO_OBJ_PARSER_H

#include "nmo_types.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_obj_face_vertex {
    int32_t pos_idx;    /**< 0-based position index, -1 if missing */
    int32_t uv_idx;     /**< 0-based UV index, -1 if missing */
    int32_t normal_idx; /**< 0-based normal index, -1 if missing */
} nmo_obj_face_vertex_t;

typedef struct nmo_obj_face {
    nmo_obj_face_vertex_t verts[3]; /**< Triangulated face vertices */
    uint32_t material_group;        /**< Index into material_names */
} nmo_obj_face_t;

typedef struct nmo_obj_data {
    float *positions;               /**< [pos_count * 3] xyz */
    size_t pos_count;
    float *uvs;                     /**< [uv_count * 2] uv */
    size_t uv_count;
    float *normals;                 /**< [normal_count * 3] xyz */
    size_t normal_count;
    nmo_obj_face_t *faces;
    size_t face_count;
    const char **material_names;    /**< Unique material names from usemtl */
    size_t material_name_count;
} nmo_obj_data_t;

/**
 * @brief Parse a Wavefront OBJ text buffer into geometry data
 *
 * Handles v, vt, vn, f (triangles and quads), and usemtl directives.
 * Quads are triangulated as (0,1,2) + (0,2,3).
 * All output arrays are arena-allocated.
 *
 * @param arena     Arena for output allocations
 * @param text      OBJ file text (need not be null-terminated)
 * @param text_size Size of text in bytes
 * @param out       Output data structure
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_obj_parse(
    nmo_arena_t *arena,
    const char *text,
    size_t text_size,
    nmo_obj_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJ_PARSER_H */
