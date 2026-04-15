/**
 * @file nmo_obj_parser.h
 * @brief Wavefront OBJ file parser
 *
 * Parses OBJ text into triangulated geometry data.
 * All output arrays are arena-allocated.
 */

#ifndef NMO_OBJ_PARSER_H
#define NMO_OBJ_PARSER_H

#include <stdint.h>

#include "nmo_types.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_OBJ_NO_MATERIAL UINT32_MAX
#define NMO_OBJ_NO_NAME UINT32_MAX

typedef struct nmo_obj_face_vertex {
    int32_t pos_idx;    /**< 0-based position index, -1 if missing */
    int32_t uv_idx;     /**< 0-based UV index, -1 if missing */
    int32_t normal_idx; /**< 0-based normal index, -1 if missing */
} nmo_obj_face_vertex_t;

typedef struct nmo_obj_face {
    nmo_obj_face_vertex_t verts[3]; /**< Triangulated face vertices */
    uint32_t material_group;        /**< Index into material_names, or NMO_OBJ_NO_MATERIAL */
    uint32_t smoothing_group;       /**< OBJ smoothing group, 0 when off */
    uint32_t object_idx;            /**< Index into object_names, or NMO_OBJ_NO_NAME */
    uint32_t group_idx;             /**< Index into group_names, or NMO_OBJ_NO_NAME */
    uint32_t source_line;           /**< 1-based OBJ source line */
} nmo_obj_face_t;

typedef struct nmo_obj_line {
    nmo_obj_face_vertex_t verts[2]; /**< Line segment endpoints */
    uint32_t material_group;        /**< Index into material_names, or NMO_OBJ_NO_MATERIAL */
    uint32_t smoothing_group;       /**< OBJ smoothing group, 0 when off */
    uint32_t object_idx;            /**< Index into object_names, or NMO_OBJ_NO_NAME */
    uint32_t group_idx;             /**< Index into group_names, or NMO_OBJ_NO_NAME */
    uint32_t source_line;           /**< 1-based OBJ source line */
} nmo_obj_line_t;

typedef struct nmo_obj_point {
    nmo_obj_face_vertex_t verts;    /**< Point vertex index tuple */
    uint32_t material_group;        /**< Index into material_names, or NMO_OBJ_NO_MATERIAL */
    uint32_t smoothing_group;       /**< OBJ smoothing group, 0 when off */
    uint32_t object_idx;            /**< Index into object_names, or NMO_OBJ_NO_NAME */
    uint32_t group_idx;             /**< Index into group_names, or NMO_OBJ_NO_NAME */
    uint32_t source_line;           /**< 1-based OBJ source line */
} nmo_obj_point_t;

typedef struct nmo_obj_data {
    float *positions;               /**< [pos_count * 3] xyz */
    size_t pos_count;
    float *colors;                  /**< [pos_count * 3] rgb, valid when position_has_color[i] */
    bool *position_has_color;       /**< [pos_count] true when vertex had explicit RGB */
    float *uvs;                     /**< [uv_count * 2] uv */
    size_t uv_count;
    float *normals;                 /**< [normal_count * 3] xyz */
    size_t normal_count;
    nmo_obj_face_t *faces;
    size_t face_count;
    nmo_obj_line_t *lines;
    size_t line_count;
    nmo_obj_point_t *points;
    size_t point_count;
    const char **material_names;    /**< Unique material names from usemtl */
    size_t material_name_count;
    const char **object_names;      /**< Unique object names from o */
    size_t object_name_count;
    const char **group_names;       /**< Unique group names from g */
    size_t group_name_count;
    const char **mtllib_names;      /**< Unique filenames from mtllib */
    size_t mtllib_name_count;
} nmo_obj_data_t;

/**
 * @brief Parse a Wavefront OBJ text buffer into geometry data
 *
 * Handles v, vt, vn, f, l, p, o, g, s, usemtl, and mtllib directives.
 * Faces are triangulated. Triangles are copied, quads use the shorter
 * diagonal, and larger polygons use simple ear clipping.
 *
 * OBJ index semantics match tinyobjloader:
 * - positive indices are 1-based OBJ indices and become 0-based;
 * - negative indices are relative to the current attribute count;
 * - position index 0 is invalid;
 * - UV and normal index 0 are treated as missing.
 *
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
