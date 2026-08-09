/**
 * @file ckmesh_schemas.c
 * @brief CKMesh schema implementation
 *
 * Implements schema for RCKMesh based on reverse engineering analysis.
 * 
 * Serialization format (from CK2_3D.dll analysis):
 * 
 * Modern format:
 * - Identifier 0x2000: Mesh flags (DWORD masked with 0x7FE39A)
 * 
 * - Identifier 0x100000: Material groups
 *   - int: Material group count
 *   - Loop: CK_ID (material object) + int padding (0)
 * 
 * - Identifier 0x20000: Vertex data (variable compressed buffer)
 *   - int: Vertex count
 *   - DWORD: SaveFlags (compression indicators)
 *   - Variable buffer:
 *     - Positions (3×float, if !(flags & 0x10))
 *     - Vertex color1 (1 or N DWORDs)
 *     - Specular color (1 or N DWORDs)
 *     - Normals (3×float, if !(flags & 0x04))
 *     - UVs (2×float, if !(flags & 0x08))
 * 
 * - Identifier 0x10000: Face data
 *   - int: Face count
 *   - Loop: 
 *     - DWORD (packed): vertex indices 0,1 (2×WORD)
 *     - DWORD (packed): vertex index 2 + material group index (2×WORD)
 * 
 * - Identifier 0x40000: Line data (optional)
 *   - int: Line count
 *   - Line indices (WORD array)
 * 
 * - Identifier 0x4000: Material channels (optional)
 *   - int: Channel count
 *   - Loop:
 *     - CK_ID: Material
 *     - DWORD: Flags
 *     - DWORD: SourceBlend
 *     - DWORD: DestBlend
 *     - int: UV count (0 = use main UV)
 *     - Loop: 2×float (u, v)
 * 
 * - Identifier 0x80000: Vertex weights (skinning, optional)
 *   - int: Weight count
 *   - Data: float array OR single float (optimization)
 * 
 * - Identifier 0x8000: Face channel masks (optional)
 *   - int: Face count
 *   - DWORD array (packed 2×WORD per pair of faces)
 * 
 * - Identifier 0x800000: Progressive mesh (LOD, optional)
 *   - int: field_0
 *   - int: m_MorphEnabled
 *   - int: m_MorphStep
 *   - int array: Progressive data
 */

#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>

#define NMO_MESH_READ(expr, field_name) do { \
    nmo_status_t mesh_read_result__ = (expr); \
    if (mesh_read_result__ != NMO_OK) { \
        NMO_RETURN_ERROR(mesh_read_result__, NMO_SEVERITY_ERROR, \
                         "CKMesh %s is truncated", (field_name)); \
    } \
} while (0)
#include <string.h>
#include <math.h>

static nmo_status_t nmo_mesh_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) return NMO_ERR_INVALID_ARGUMENT;
    nmo_mesh_state_t *state = instance;
    memset(state, 0, sizeof(*state));
    return nmo_beobject_vtable.create(&state->beobject, NULL, context);
}

static void nmo_mesh_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_mesh_state_t *state = instance;
    if (!state) return;
    nmo_beobject_vtable.destroy(&state->beobject, NULL, context);
    memset(state, 0, sizeof(*state));
}

static nmo_status_t nmo_mesh_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_mesh_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("beobject", offsetof(nmo_mesh_state_t, beobject),
                        sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Mesh flags */
    NMO_FIELD(nmo_mesh_state_t, flags, NMO_GUID_ENUM_VXMESH_FLAGS),
    /* Bounding info */
    NMO_FIELD_NAMED("bary_center", offsetof(nmo_mesh_state_t, bary_center),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_mesh_state_t, radius, CKPGUID_FLOAT),
    NMO_FIELD_NAMED("local_box_min", offsetof(nmo_mesh_state_t, local_box_min),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("local_box_max", offsetof(nmo_mesh_state_t, local_box_max),
                    sizeof(nmo_vector_t), CKPGUID_VECTOR,
                    NMO_FIELD_REQUIRED, 0),
    /* Faces */
    NMO_FIELD(nmo_mesh_state_t, face_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, faces, face_count, 1, NMO_GUID_STRUCT_CKFACE),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, face_vertex_indices, face_count, 3, CKPGUID_UINT16),
    /* Lines */
    NMO_FIELD(nmo_mesh_state_t, line_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, line_indices, line_count, 2, CKPGUID_UINT16),
    /* Vertices */
    NMO_FIELD(nmo_mesh_state_t, vertex_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, vertices, vertex_count, 1, NMO_GUID_STRUCT_VXVERTEX),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, vertex_colors, vertex_count, 1, CKPGUID_COLOR),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, vertex_specular, vertex_count, 1, CKPGUID_COLOR),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, vertex_weights, vertex_weight_count, 1, CKPGUID_FLOAT),
    NMO_FIELD(nmo_mesh_state_t, vertex_weight_count, CKPGUID_UINT32),
    /* Materials */
    NMO_FIELD(nmo_mesh_state_t, material_group_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, material_groups, material_group_count, 1, NMO_GUID_STRUCT_CKMATERIALGROUP),
    NMO_FIELD(nmo_mesh_state_t, has_material_groups, CKPGUID_BOOL),
    NMO_FIELD(nmo_mesh_state_t, material_channel_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_mesh_state_t, material_channels, material_channel_count, 1, NMO_GUID_STRUCT_CKMATERIALCHANNEL),
    NMO_FIELD(nmo_mesh_state_t, has_material_channels, CKPGUID_BOOL),
    NMO_FIELD(nmo_mesh_state_t, has_progressive_mesh, CKPGUID_BOOL)
};

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Read packed DWORD as two WORDs (little-endian)
 */
static inline void nmo_unpack_dword_to_words(uint32_t dword, uint16_t *lo, uint16_t *hi) {
    *lo = (uint16_t)(dword & 0xFFFF);
    *hi = (uint16_t)((dword >> 16) & 0xFFFF);
}

/**
 * @brief Pack two WORDs into DWORD (little-endian)
 */
static inline uint32_t nmo_pack_words_to_dword(uint16_t lo, uint16_t hi) {
    return (uint32_t)lo | ((uint32_t)hi << 16);
}

/* =============================================================================
 * CKSTATECHUNK IDENTIFIERS
 * ============================================================================= */

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

static nmo_status_t nmo_mesh_peek_dword(nmo_chunk_t *chunk, uint32_t *out_value) {
    if (!chunk || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_mesh_peek_dword");
    }

    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, 1, "Cannot peek beyond data");

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    *out_value = data[state->current_pos];

    NMO_RETURN_OK();
}

static size_t nmo_mesh_identifier_remaining_dwords(nmo_chunk_t *chunk) {
    if (!chunk || !chunk->parser_state) {
        return 0;
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    size_t next_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        next_pos = data[state->prev_identifier_pos + 1];
    }
    if (next_pos == 0 || next_pos > chunk->data.count) {
        next_pos = chunk->data.count;
    }
    if (next_pos < state->current_pos) {
        return 0;
    }

    return next_pos - state->current_pos;
}

static nmo_status_t nmo_mesh_require_identifier_end(nmo_chunk_t *chunk) {
    return nmo_mesh_identifier_remaining_dwords(chunk) == 0u
        ? NMO_OK : NMO_ERR_INVALID_FORMAT;
}

static bool nmo_mesh_size_mul_overflows(size_t count, size_t element_size) {
    return count != 0u && element_size > SIZE_MAX / count;
}

static nmo_status_t nmo_mesh_validate_legacy_geometry(
    const nmo_mesh_state_t *state)
{
    for (uint32_t i = 0; i < state->vertex_count; ++i) {
        if (state->vertices[i].uv.x != 0.0f ||
            state->vertices[i].uv.y != 0.0f ||
            state->vertex_specular[i] != 0u) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "CKMesh data versions below 9 cannot store vertex UV or specular data");
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_mesh_validate_modern_geometry(
    const nmo_mesh_state_t *state)
{
    for (uint32_t i = 0; i < state->face_count; ++i) {
        if (state->faces[i].material_group_idx > UINT16_MAX) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "CKMesh data versions 9 and newer require 16-bit face material indices");
        }
    }
    return NMO_OK;
}

static bool nmo_mesh_vertex_payload_dwords(
    uint32_t vertex_count,
    uint32_t save_flags,
    size_t *out_dwords)
{
    size_t per_vertex = 0u;
    size_t fixed = 0u;
    if (!(save_flags & NMO_VERTEX_POS_EXTERNAL)) per_vertex += 3u;
    if (save_flags & NMO_VERTEX_COLOR1_UNIFORM) fixed += 1u;
    else per_vertex += 1u;
    if (save_flags & NMO_VERTEX_SPECULAR_UNIFORM) fixed += 1u;
    else per_vertex += 1u;
    if (!(save_flags & NMO_VERTEX_NORMALS_MISSING)) per_vertex += 3u;
    if (save_flags & NMO_VERTEX_UV_UNIFORM) fixed += 2u;
    else per_vertex += 2u;

    size_t variable = 0u;
    return nmo_safe_mul_size(
               (size_t)vertex_count, per_vertex, &variable) &&
        nmo_safe_add_size(variable, fixed, out_dwords);
}

static nmo_status_t nmo_mesh_seek_optional(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    bool *out_found,
    size_t *out_dwords)
{
    nmo_status_t result = nmo_chunk_seek_identifier_with_size(
        chunk, identifier, out_dwords);
    if (result == NMO_OK) {
        *out_found = true;
        return NMO_OK;
    }
    *out_found = false;
    *out_dwords = 0u;
    return result == NMO_ERR_NOT_FOUND ? NMO_OK : result;
}

static nmo_status_t nmo_mesh_read_raw_bytes(nmo_chunk_t *chunk, void *buffer, size_t bytes) {
    if (!chunk || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_mesh_read_raw_bytes");
    }

    size_t dwords = (bytes + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, dwords, "Insufficient data for raw buffer");

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(buffer, &data[state->current_pos], bytes);
    state->current_pos += dwords;

    NMO_RETURN_OK();
}

static void nmo_mesh_recompute_bounds(nmo_mesh_state_t *state) {
    if (!state || !state->vertices || state->vertex_count == 0) {
        return;
    }

    float minx = state->vertices[0].position.x;
    float miny = state->vertices[0].position.y;
    float minz = state->vertices[0].position.z;
    float maxx = minx;
    float maxy = miny;
    float maxz = minz;

    for (uint32_t i = 1; i < state->vertex_count; ++i) {
        const nmo_vector_t *pos = &state->vertices[i].position;
        if (pos->x < minx) minx = pos->x;
        if (pos->y < miny) miny = pos->y;
        if (pos->z < minz) minz = pos->z;
        if (pos->x > maxx) maxx = pos->x;
        if (pos->y > maxy) maxy = pos->y;
        if (pos->z > maxz) maxz = pos->z;
    }

    state->local_box_min.x = minx;
    state->local_box_min.y = miny;
    state->local_box_min.z = minz;
    state->local_box_max.x = maxx;
    state->local_box_max.y = maxy;
    state->local_box_max.z = maxz;
    state->bary_center.x = (minx + maxx) * 0.5f;
    state->bary_center.y = (miny + maxy) * 0.5f;
    state->bary_center.z = (minz + maxz) * 0.5f;

    float max_dist_sq = 0.0f;
    for (uint32_t i = 0; i < state->vertex_count; ++i) {
        const nmo_vector_t *pos = &state->vertices[i].position;
        float dx = pos->x - state->bary_center.x;
        float dy = pos->y - state->bary_center.y;
        float dz = pos->z - state->bary_center.z;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > max_dist_sq) {
            max_dist_sq = dist_sq;
        }
    }
    state->radius = sqrtf(max_dist_sq);
}

/* =============================================================================
 * CKMesh DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize vertex data (identifier 0x20000)
 */
static nmo_status_t nmo_mesh_deserialize_vertices(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_mesh_state_t *out_state)
{
    nmo_status_t result;
    bool section_found = false;
    size_t section_dwords = 0u;
    
    // Seek to vertex data identifier
    result = nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHVERTICES, &section_found, &section_dwords);
    if (result != NMO_OK) {
        return result;
    }
    if (!section_found) {
        out_state->vertex_count = 0;
        NMO_RETURN_OK();
    }
    if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
    
    // Read vertex count
    int32_t vertex_count;
    result = nmo_chunk_read_int(chunk, &vertex_count);
    if (result != NMO_OK) {
        return result;
    }
    
    if (vertex_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid vertex count");
    }
    
    out_state->vertex_count = (uint32_t)vertex_count;
    if (vertex_count == 0) {
        return nmo_mesh_require_identifier_end(chunk);
    }
    
    // Read save flags
    uint32_t save_flags;
    result = nmo_chunk_read_dword(chunk, &save_flags);
    if (result != NMO_OK) {
        return result;
    }

    // Read and validate the complete serialized payload before allocating.
    uint32_t buffer_dwords = 0;
    result = nmo_chunk_read_dword(chunk, &buffer_dwords);
    if (result != NMO_OK) return result;
    if (buffer_dwords == 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Vertex buffer size is zero");
    }
    size_t buffer_payload_dwords = (size_t)buffer_dwords - 1u;
    size_t expected_dwords = 0;
    if (!nmo_mesh_vertex_payload_dwords(
            (uint32_t)vertex_count, save_flags, &expected_dwords)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Vertex payload size overflows");
    }
    if (buffer_payload_dwords < expected_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Vertex buffer shorter than expected");
    }
    if (!nmo_chunk_has_read_capacity(chunk, buffer_payload_dwords) ||
        buffer_payload_dwords > nmo_mesh_identifier_remaining_dwords(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Vertex buffer exceeds remaining DWORDs");
    }
    if (nmo_mesh_size_mul_overflows(
            (size_t)vertex_count, sizeof(nmo_vertex_t)) ||
        nmo_mesh_size_mul_overflows(
            (size_t)vertex_count, sizeof(uint32_t))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Vertex allocation size overflows");
    }

    // Allocate vertex array
    out_state->vertices = (nmo_vertex_t *)nmo_arena_alloc(
        arena, sizeof(nmo_vertex_t) * vertex_count, alignof(nmo_vertex_t));
    if (!out_state->vertices) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate vertex array");
    }
    memset(out_state->vertices, 0, sizeof(nmo_vertex_t) * (size_t)vertex_count);
    
    // Allocate color arrays
    out_state->vertex_colors = (uint32_t *)nmo_arena_alloc(
        arena, sizeof(uint32_t) * vertex_count, alignof(uint32_t));
    out_state->vertex_specular = (uint32_t *)nmo_arena_alloc(
        arena, sizeof(uint32_t) * vertex_count, alignof(uint32_t));
    
    if (!out_state->vertex_colors || !out_state->vertex_specular) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate color arrays");
    }
    
    // Read positions (if not external)
    if (!(save_flags & NMO_VERTEX_POS_EXTERNAL)) {
        for (uint32_t i = 0; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.x);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.y);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.z);
            if (result != NMO_OK) return result;
        }
    }
    
    // Read vertex colors (at least one, then N if not uniform)
    uint32_t first_color;
    result = nmo_chunk_read_dword(chunk, &first_color);
    if (result != NMO_OK) return result;
    out_state->vertex_colors[0] = first_color;
    
    if (save_flags & NMO_VERTEX_COLOR1_UNIFORM) {
        // All colors same as first
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            out_state->vertex_colors[i] = first_color;
        }
    } else {
        // Read remaining colors
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_dword(chunk, &out_state->vertex_colors[i]);
            if (result != NMO_OK) return result;
        }
    }
    
    // Read specular colors (at least one, then N if not uniform)
    uint32_t first_specular;
    result = nmo_chunk_read_dword(chunk, &first_specular);
    if (result != NMO_OK) return result;
    out_state->vertex_specular[0] = first_specular;
    
    if (save_flags & NMO_VERTEX_SPECULAR_UNIFORM) {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            out_state->vertex_specular[i] = first_specular;
        }
    } else {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_dword(chunk, &out_state->vertex_specular[i]);
            if (result != NMO_OK) return result;
        }
    }
    
    // Read normals (if not missing)
    if (!(save_flags & NMO_VERTEX_NORMALS_MISSING)) {
        for (uint32_t i = 0; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.x);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.y);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.z);
            if (result != NMO_OK) return result;
        }
    }
    
    // Read UVs (at least one, then N if not uniform)
    result = nmo_chunk_read_float(chunk, &out_state->vertices[0].uv.x);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_float(chunk, &out_state->vertices[0].uv.y);
    if (result != NMO_OK) return result;
    
    if (save_flags & NMO_VERTEX_UV_UNIFORM) {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            out_state->vertices[i].uv = out_state->vertices[0].uv;
        }
    } else {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].uv.x);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].uv.y);
            if (result != NMO_OK) return result;
        }
    }
    
    // Validate and skip any padding within the vertex buffer
    if (buffer_payload_dwords > expected_dwords) {
        result = nmo_chunk_skip(chunk, buffer_payload_dwords - expected_dwords);
        if (result != NMO_OK) {
            return result;
        }
    }

    return nmo_mesh_require_identifier_end(chunk);
}

static nmo_status_t nmo_mesh_deserialize_material_groups(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_mesh_state_t *out_state)
{
    size_t section_dwords = 0u;
    nmo_status_t result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_MESHMATERIALS, &section_dwords);
    if (result == NMO_ERR_NOT_FOUND) return NMO_OK;
    if (result != NMO_OK) return result;
    if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;

    out_state->has_material_groups = 1;
    int32_t group_count = 0;
    NMO_MESH_READ(nmo_chunk_read_int(chunk, &group_count),
                  "material group count");
    if (group_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid mesh material group count %d", group_count);
    }
    if ((size_t)group_count >
        nmo_mesh_identifier_remaining_dwords(chunk) / 2u) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "CKMesh material groups exceed remaining DWORDs");
    }
    if (nmo_mesh_size_mul_overflows(
            (size_t)group_count, sizeof(nmo_material_group_t))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "CKMesh material group allocation size overflows");
    }
    if (group_count == 0) return nmo_mesh_require_identifier_end(chunk);

    nmo_material_group_t *groups = nmo_arena_alloc(
        arena, (size_t)group_count * sizeof(*groups),
        alignof(nmo_material_group_t));
    if (!groups) return NMO_ERR_NOMEM;
    memset(groups, 0, (size_t)group_count * sizeof(*groups));
    for (int32_t i = 0; i < group_count; ++i) {
        NMO_MESH_READ(nmo_ref_read(chunk, &groups[i].material),
                      "material group reference");
        NMO_MESH_READ(nmo_chunk_read_int(chunk, &groups[i].padding),
                      "material group padding");
    }
    out_state->material_groups = groups;
    out_state->material_group_count = (uint32_t)group_count;
    return nmo_mesh_require_identifier_end(chunk);
}

static nmo_status_t nmo_mesh_deserialize_weights(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_mesh_state_t *out_state,
    const char *layout)
{
    int32_t weight_count = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &weight_count);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                         "CKMesh %s weight count is truncated", layout);
    }
    if (weight_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid %s mesh weight count %d",
                         layout, weight_count);
    }
    if (weight_count == 0) return nmo_mesh_require_identifier_end(chunk);

    size_t allocation_size = 0u;
    if (!nmo_safe_mul_size(
            (size_t)weight_count, sizeof(float), &allocation_size)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "CKMesh %s weight allocation size overflows",
                         layout);
    }

    const size_t remaining_dwords =
        nmo_mesh_identifier_remaining_dwords(chunk);
    if (remaining_dwords < 1u) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "CKMesh %s weight payload is truncated", layout);
    }

    bool buffered = false;
    if (allocation_size <= UINT32_MAX && remaining_dwords >= 2u) {
        uint32_t first_dword = 0u;
        result = nmo_mesh_peek_dword(chunk, &first_dword);
        if (result != NMO_OK) return result;
        buffered = first_dword == (uint32_t)allocation_size;
    }
    if (buffered &&
        (size_t)weight_count > remaining_dwords - 1u) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "CKMesh %s weight buffer is truncated", layout);
    }

    float *weights = nmo_arena_alloc(
        arena, allocation_size, alignof(float));
    if (weights == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate %s vertex weights", layout);
    }

    if (buffered) {
        uint32_t buffer_size = 0u;
        result = nmo_chunk_read_dword(chunk, &buffer_size);
        if (result != NMO_OK) return result;
        if (buffer_size != (uint32_t)allocation_size) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "CKMesh %s weight buffer size is invalid",
                             layout);
        }
        result = nmo_mesh_read_raw_bytes(chunk, weights, allocation_size);
        if (result != NMO_OK) return result;
        if (remaining_dwords >= (size_t)weight_count + 2u) {
            float tail_weight = 0.0f;
            result = nmo_chunk_read_float(chunk, &tail_weight);
            if (result != NMO_OK) return result;
        }
    } else {
        float uniform_weight = 0.0f;
        result = nmo_chunk_read_float(chunk, &uniform_weight);
        if (result != NMO_OK) return result;
        for (int32_t i = 0; i < weight_count; ++i) {
            weights[i] = uniform_weight;
        }
    }

    out_state->vertex_weights = weights;
    out_state->vertex_weight_count = (uint32_t)weight_count;
    return nmo_mesh_require_identifier_end(chunk);
}

/**
 * @brief Deserialize CKMesh state from chunk (modern format v9+)
 */
static nmo_status_t nmo_mesh_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_mesh_state_t *out_state)
{
    nmo_status_t result;
    bool section_found = false;
    size_t section_dwords = 0u;

    // Load parent CKBeObject
    result = nmo_beobject_deserialize(&out_state->beobject, chunk, NULL, arena);
    if (result != NMO_OK) {
        return result;
    }
    
    // Read mesh flags (identifier CK_STATESAVE_MESHFLAGS)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHFLAGS, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        uint32_t flags;
        result = nmo_chunk_read_dword(chunk, &flags);
        if (result != NMO_OK) {
            return result;
        }
        out_state->flags = flags;
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }
    
    NMO_RETURN_IF_ERROR(nmo_mesh_deserialize_material_groups(
        chunk, arena, out_state));
    
    // Read vertices (identifier CK_STATESAVE_MESHVERTICES)
    result = nmo_mesh_deserialize_vertices(chunk, arena, out_state);
    if (result != NMO_OK) {
        return result;
    }
    
    // Read faces (identifier CK_STATESAVE_MESHFACES)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHFACES, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t face_count;
        result = nmo_chunk_read_int(chunk, &face_count);
        if (result != NMO_OK) {
            return result;
        }
        
        if (face_count < 0 ||
            (uint32_t)face_count > UINT32_MAX / 3u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid modern mesh face count %d", face_count);
        }
        if (face_count > 0) {
            if ((size_t)face_count >
                nmo_mesh_identifier_remaining_dwords(chunk) / 4u) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Modern mesh faces exceed remaining DWORDs");
            }
            if (nmo_mesh_size_mul_overflows(
                    (size_t)face_count, sizeof(nmo_face_t)) ||
                nmo_mesh_size_mul_overflows(
                    (size_t)face_count, 3u * sizeof(uint16_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Modern mesh face allocation size overflows");
            }
            out_state->face_count = (uint32_t)face_count;
            out_state->faces = (nmo_face_t *)nmo_arena_alloc(
                arena, sizeof(nmo_face_t) * face_count, alignof(nmo_face_t));
            out_state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * face_count * 3, alignof(uint16_t));
            
            if (!out_state->faces || !out_state->face_vertex_indices) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate face arrays");
            }
            memset(out_state->faces, 0,
                   sizeof(nmo_face_t) * (size_t)face_count);
            
            // Read packed face data
            for (uint32_t i = 0; i < out_state->face_count; i++) {
                out_state->faces[i].channel_mask = 0xFFFFu;
                // Read vertex indices 0,1 (packed as DWORD)
                uint32_t packed01;
                result = nmo_chunk_read_dword_as_words(chunk, &packed01);
                if (result != NMO_OK) return result;
                nmo_unpack_dword_to_words(packed01,
                    &out_state->face_vertex_indices[i * 3 + 0],
                    &out_state->face_vertex_indices[i * 3 + 1]);
                
                // Read vertex index 2 + material group index (packed as DWORD)
                uint32_t packed2mat;
                result = nmo_chunk_read_dword_as_words(chunk, &packed2mat);
                if (result != NMO_OK) return result;
                uint16_t idx2;
                uint16_t mat_idx;
                nmo_unpack_dword_to_words(packed2mat, &idx2, &mat_idx);
                out_state->face_vertex_indices[i * 3 + 2] = idx2;
                out_state->faces[i].material_group_idx = mat_idx;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }
    
    // Read lines (identifier CK_STATESAVE_MESHLINES, optional)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHLINES, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t line_count;
        result = nmo_chunk_read_int(chunk, &line_count);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                             "CKMesh modern line count is truncated");
        }
        if (line_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid mesh line count");
        }
        if (line_count > 0) {
            size_t expected_bytes = 0;
            if (!nmo_safe_mul_size(
                    (size_t)line_count, 2u * sizeof(uint16_t),
                    &expected_bytes)) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Modern mesh line allocation size overflows");
            }
            size_t required_dwords =
                1u + (expected_bytes + sizeof(uint32_t) - 1u) /
                    sizeof(uint32_t);
            if (required_dwords >
                nmo_mesh_identifier_remaining_dwords(chunk)) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Modern mesh lines exceed remaining DWORDs");
            }
            out_state->line_count = (uint32_t)line_count;
            out_state->line_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * line_count * 2, alignof(uint16_t));
            if (!out_state->line_indices) return NMO_ERR_NOMEM;
            uint32_t serialized_bytes = 0u;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &serialized_bytes));
            if ((size_t)serialized_bytes != expected_bytes) {
                NMO_RETURN_ERROR(
                    NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                    "Modern mesh line buffer size does not match line count");
            }
            result = nmo_chunk_read_buffer_lendian16(
                chunk, out_state->line_indices, expected_bytes);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                                 "CKMesh modern line indices are truncated");
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }
    
    // Read material channels (identifier CK_STATESAVE_MESHCHANNELS, optional)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHCHANNELS, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_material_channels = 1;
        int32_t channel_count;
        result = nmo_chunk_read_int(chunk, &channel_count);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                             "CKMesh modern channel count is truncated");
        }
        if (channel_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid mesh material channel count");
        }
        if (channel_count > 0) {
            if ((size_t)channel_count >
                nmo_mesh_identifier_remaining_dwords(chunk) / 5u) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Modern mesh channels exceed remaining DWORDs");
            }
            if (nmo_mesh_size_mul_overflows(
                    (size_t)channel_count,
                    sizeof(nmo_material_channel_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Modern mesh channel allocation size overflows");
            }
            out_state->material_channel_count = (uint32_t)channel_count;
            out_state->material_channels = (nmo_material_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_material_channel_t) * channel_count,
                alignof(nmo_material_channel_t));
            if (!out_state->material_channels) return NMO_ERR_NOMEM;
            memset(out_state->material_channels, 0,
                   sizeof(nmo_material_channel_t) * out_state->material_channel_count);
            for (uint32_t i = 0; i < out_state->material_channel_count; i++) {
                    nmo_material_channel_t *ch = &out_state->material_channels[i];
                    NMO_MESH_READ(nmo_ref_read(chunk, &ch->material),
                                  "modern channel material ID");
                    NMO_MESH_READ(nmo_chunk_read_dword(chunk, &ch->flags),
                                  "modern channel flags");
                    NMO_MESH_READ(nmo_chunk_read_dword(chunk, &ch->source_blend),
                                  "modern channel source blend");
                    NMO_MESH_READ(nmo_chunk_read_dword(chunk, &ch->dest_blend),
                                  "modern channel destination blend");
                    
                    int32_t uv_count;
                    NMO_MESH_READ(nmo_chunk_read_int(chunk, &uv_count),
                                  "modern channel UV count");
                    
                    if (uv_count < 0) {
                        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT,
                                         NMO_SEVERITY_ERROR,
                                         "Invalid modern mesh UV count %d",
                                         uv_count);
                    }
                    if (uv_count > 0) {
                        if ((size_t)uv_count >
                            nmo_mesh_identifier_remaining_dwords(chunk) / 2u) {
                            NMO_RETURN_ERROR(
                                NMO_ERR_TRUNCATED_CHUNK,
                                NMO_SEVERITY_ERROR,
                                "Modern mesh channel UVs exceed remaining DWORDs");
                        }
                        if (nmo_mesh_size_mul_overflows(
                                (size_t)uv_count,
                                sizeof(nmo_vector2_t))) {
                            NMO_RETURN_ERROR(
                                NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                "Modern mesh UV allocation size overflows");
                        }
                        ch->uv_count = (uint32_t)uv_count;
                        ch->uv_coords = (nmo_vector2_t *)nmo_arena_alloc(
                            arena, sizeof(nmo_vector2_t) * (uint32_t)uv_count,
                            alignof(nmo_vector2_t));
                        
                        if (!ch->uv_coords) return NMO_ERR_NOMEM;
                        for (uint32_t j = 0; j < (uint32_t)uv_count; j++) {
                            NMO_MESH_READ(nmo_chunk_read_float(
                                chunk, &ch->uv_coords[j].x),
                                "modern channel UV x");
                            NMO_MESH_READ(nmo_chunk_read_float(
                                chunk, &ch->uv_coords[j].y),
                                "modern channel UV y");
                        }
                    } else {
                        ch->uv_coords = NULL;
                        ch->uv_count = 0;
                    }
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }
    
    // Read vertex weights (identifier CK_STATESAVE_MESHWEIGHTS, optional)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHWEIGHTS, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        NMO_RETURN_IF_ERROR(nmo_mesh_deserialize_weights(
            chunk, arena, out_state, "modern"));
    }
    
    // Read face channel masks (identifier CK_STATESAVE_MESHFACECHANMASK, optional)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHFACECHANMASK, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t mask_face_count;
        result = nmo_chunk_read_int(chunk, &mask_face_count);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                             "CKMesh modern face-mask count is truncated");
        }
        if (mask_face_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid modern mesh face-mask count %d",
                             mask_face_count);
        }
        if (mask_face_count > 0) {
            uint32_t face_count = out_state->face_count;
            if ((uint32_t)mask_face_count > face_count) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Modern mesh face-mask count exceeds face count");
            }
            uint32_t pair_count = (uint32_t)mask_face_count / 2u;
            uint32_t remainder = (uint32_t)mask_face_count % 2u;
            if (face_count > 0u && out_state->faces == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                 NMO_SEVERITY_ERROR,
                                 "Modern mesh faces missing for channel masks");
            }
            if ((size_t)pair_count * 2u + remainder >
                nmo_mesh_identifier_remaining_dwords(chunk)) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }

            for (uint32_t i = 0; i < pair_count; i++) {
                uint32_t packed_masks;
                result = nmo_chunk_read_dword_as_words(
                    chunk, &packed_masks);
                if (result != NMO_OK) {
                    NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                                     "CKMesh modern face masks are truncated");
                }

                nmo_unpack_dword_to_words(packed_masks,
                    &out_state->faces[i * 2].channel_mask,
                    &out_state->faces[i * 2 + 1].channel_mask);
            }

            if (remainder) {
                result = nmo_chunk_read_word(chunk,
                    &out_state->faces[(uint32_t)mask_face_count - 1u].channel_mask);
                if (result != NMO_OK) {
                    NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                                     "CKMesh modern trailing face mask is truncated");
                }
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }
    
    // Read progressive mesh (identifier CK_STATESAVE_PROGRESSIVEMESH, optional)
    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_PROGRESSIVEMESH, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        size_t pm_bytes_total = nmo_mesh_identifier_remaining_dwords(chunk) * 4u;
        out_state->has_progressive_mesh = true;
        
        result = nmo_chunk_read_int(chunk, &out_state->pm_field_0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->pm_morph_enabled);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->pm_morph_step);
        if (result != NMO_OK) return result;

        if (pm_bytes_total > 12u) {
            size_t pm_bytes = pm_bytes_total - 12u;
            out_state->pm_data_size = (uint32_t)pm_bytes;
            out_state->pm_data = nmo_arena_alloc(arena, pm_bytes, 4);
            if (!out_state->pm_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate progressive mesh data");
            }
            result = nmo_mesh_read_raw_bytes(chunk, out_state->pm_data, pm_bytes);
            if (result != NMO_OK) return result;
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CKMesh state from chunk (legacy format < v9)
 */
static nmo_status_t nmo_mesh_deserialize_legacy(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_mesh_state_t *out_state)
{
    nmo_status_t result;
    bool section_found = false;
    size_t section_dwords = 0u;

    result = nmo_beobject_deserialize(&out_state->beobject, chunk, NULL, arena);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHFLAGS, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        uint32_t flags;
        result = nmo_chunk_read_dword(chunk, &flags);
        if (result != NMO_OK) return result;
        out_state->flags = flags;
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_deserialize_material_groups(
        chunk, arena, out_state));

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHVERTICES, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t vertex_count;
        result = nmo_chunk_read_int(chunk, &vertex_count);
        if (result != NMO_OK) return result;

        uint32_t save_flags = 0;
        result = nmo_chunk_read_dword(chunk, &save_flags);
        if (result != NMO_OK) return result;

        if (vertex_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid legacy mesh vertex count %d", vertex_count);
        }
        if (vertex_count > 0) {
            size_t dwords_per_vertex = 3u;
            if (save_flags & 0x02u) dwords_per_vertex += 3u;
            if (save_flags & 0x04u) dwords_per_vertex += 1u;
            size_t vertex_dwords = 0u;
            if (!nmo_safe_mul_size(
                    (size_t)vertex_count, dwords_per_vertex,
                    &vertex_dwords) ||
                nmo_mesh_size_mul_overflows(
                    (size_t)vertex_count, sizeof(nmo_vertex_t)) ||
                nmo_mesh_size_mul_overflows(
                    (size_t)vertex_count, sizeof(uint32_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Legacy mesh vertex allocation size overflows");
            }
            if (vertex_dwords >
                nmo_mesh_identifier_remaining_dwords(chunk)) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Legacy mesh vertices exceed remaining DWORDs");
            }
            out_state->vertex_count = (uint32_t)vertex_count;
            out_state->vertices = (nmo_vertex_t *)nmo_arena_alloc(
                arena, sizeof(nmo_vertex_t) * out_state->vertex_count,
                alignof(nmo_vertex_t));
            out_state->vertex_colors = (uint32_t *)nmo_arena_alloc(
                arena, sizeof(uint32_t) * out_state->vertex_count, alignof(uint32_t));
            out_state->vertex_specular = (uint32_t *)nmo_arena_alloc(
                arena, sizeof(uint32_t) * out_state->vertex_count, alignof(uint32_t));

            if (!out_state->vertices || !out_state->vertex_colors || !out_state->vertex_specular) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy vertex arrays");
            }
            memset(out_state->vertices, 0, sizeof(nmo_vertex_t) * out_state->vertex_count);

            for (uint32_t i = 0; i < out_state->vertex_count; ++i) {
                result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.x);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.y);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.z);
                if (result != NMO_OK) return result;
            }

            if (save_flags & 0x02) {
                for (uint32_t i = 0; i < out_state->vertex_count; ++i) {
                    result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.x);
                    if (result != NMO_OK) return result;
                    result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.y);
                    if (result != NMO_OK) return result;
                    result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.z);
                    if (result != NMO_OK) return result;
                }
            }

            if (save_flags & 0x04) {
                for (uint32_t i = 0; i < out_state->vertex_count; ++i) {
                    result = nmo_chunk_read_dword(chunk, &out_state->vertex_colors[i]);
                    if (result != NMO_OK) return result;
                    out_state->vertex_specular[i] = 0;
                }
            } else {
                for (uint32_t i = 0; i < out_state->vertex_count; ++i) {
                    out_state->vertex_colors[i] = 0;
                    out_state->vertex_specular[i] = 0;
                }
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHFACES, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t face_count;
        result = nmo_chunk_read_int(chunk, &face_count);
        if (result != NMO_OK) return result;

        if (face_count < 0 ||
            (uint32_t)face_count > UINT32_MAX / 3u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid legacy mesh face count %d", face_count);
        }
        if (face_count > 0) {
            size_t face_dwords = 0;
            if (!nmo_safe_mul_size(
                    (size_t)face_count, 4u, &face_dwords) ||
                nmo_mesh_size_mul_overflows(
                    (size_t)face_count, sizeof(nmo_face_t)) ||
                nmo_mesh_size_mul_overflows(
                    (size_t)face_count, 3u * sizeof(uint16_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Legacy mesh face allocation size overflows");
            }
            if (face_dwords > nmo_mesh_identifier_remaining_dwords(chunk)) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Legacy mesh faces exceed remaining DWORDs");
            }
            out_state->face_count = (uint32_t)face_count;
            out_state->faces = (nmo_face_t *)nmo_arena_alloc(
                arena, sizeof(nmo_face_t) * out_state->face_count,
                alignof(nmo_face_t));
            out_state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * out_state->face_count * 3,
                alignof(uint16_t));

            if (!out_state->faces || !out_state->face_vertex_indices) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy face arrays");
            }
            memset(out_state->faces, 0,
                   sizeof(nmo_face_t) * (size_t)face_count);

            for (uint32_t i = 0; i < out_state->face_count; ++i) {
                out_state->faces[i].channel_mask = 0xFFFFu;
                uint16_t idx0, idx1, idx2;
                uint32_t mat_idx;
                result = nmo_chunk_read_word(chunk, &idx0);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_word(chunk, &idx1);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_word(chunk, &idx2);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_dword(chunk, &mat_idx);
                if (result != NMO_OK) return result;

                out_state->face_vertex_indices[i * 3 + 0] = idx0;
                out_state->face_vertex_indices[i * 3 + 1] = idx1;
                out_state->face_vertex_indices[i * 3 + 2] = idx2;
                out_state->faces[i].material_group_idx = mat_idx;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHLINES, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t line_count;
        result = nmo_chunk_read_int(chunk, &line_count);
        if (result != NMO_OK) return result;

        if (line_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid legacy mesh line count %d", line_count);
        }
        if (line_count > 0) {
            if ((size_t)line_count >
                nmo_mesh_identifier_remaining_dwords(chunk)) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Legacy mesh lines exceed remaining DWORDs");
            }
            if (nmo_mesh_size_mul_overflows(
                    (size_t)line_count, 2u * sizeof(uint16_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Legacy mesh line allocation size overflows");
            }
            out_state->line_count = (uint32_t)line_count;
            out_state->line_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * out_state->line_count * 2,
                alignof(uint16_t));
            if (!out_state->line_indices) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy line indices");
            }

            for (uint32_t i = 0; i < out_state->line_count; ++i) {
                uint16_t idx0, idx1;
                result = nmo_chunk_read_word(chunk, &idx0);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_word(chunk, &idx1);
                if (result != NMO_OK) return result;
                out_state->line_indices[i * 2 + 0] = idx0;
                out_state->line_indices[i * 2 + 1] = idx1;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHCHANNELS, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_material_channels = 1;
        int32_t channel_count;
        result = nmo_chunk_read_int(chunk, &channel_count);
        if (result != NMO_OK) return result;
        if (channel_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid legacy mesh channel count %d", channel_count);
        }
        if (channel_count > 0) {
            if ((size_t)channel_count >
                nmo_mesh_identifier_remaining_dwords(chunk) / 5u) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "Legacy mesh channels exceed remaining DWORDs");
            }
            if (nmo_mesh_size_mul_overflows(
                    (size_t)channel_count,
                    sizeof(nmo_material_channel_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Legacy mesh channel allocation size overflows");
            }
            out_state->material_channel_count = (uint32_t)channel_count;
            out_state->material_channels = (nmo_material_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_material_channel_t) * out_state->material_channel_count,
                alignof(nmo_material_channel_t));

            if (!out_state->material_channels) return NMO_ERR_NOMEM;
            memset(out_state->material_channels, 0,
                   sizeof(nmo_material_channel_t) * out_state->material_channel_count);
            for (uint32_t i = 0; i < out_state->material_channel_count; i++) {
                    nmo_material_channel_t *ch = &out_state->material_channels[i];

                    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &ch->material));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &ch->flags));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &ch->source_blend));
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &ch->dest_blend));

                    int32_t uv_count;
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &uv_count));
                    if (uv_count < 0) {
                        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                         "Invalid legacy mesh UV count %d", uv_count);
                    }

                    if (uv_count > 0) {
                        if ((size_t)uv_count >
                            nmo_mesh_identifier_remaining_dwords(chunk) / 2u) {
                            NMO_RETURN_ERROR(
                                NMO_ERR_TRUNCATED_CHUNK,
                                NMO_SEVERITY_ERROR,
                                "Legacy mesh channel UVs exceed remaining DWORDs");
                        }
                        if (nmo_mesh_size_mul_overflows(
                                (size_t)uv_count,
                                sizeof(nmo_vector2_t))) {
                            NMO_RETURN_ERROR(
                                NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                "Legacy mesh UV allocation size overflows");
                        }
                        ch->uv_count = (uint32_t)uv_count;
                        ch->uv_coords = (nmo_vector2_t *)nmo_arena_alloc(
                            arena, sizeof(nmo_vector2_t) * (uint32_t)uv_count,
                            alignof(nmo_vector2_t));

                        if (!ch->uv_coords) return NMO_ERR_NOMEM;
                        for (uint32_t j = 0; j < (uint32_t)uv_count; j++) {
                            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &ch->uv_coords[j].x));
                            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &ch->uv_coords[j].y));
                        }
                    } else {
                        ch->uv_coords = NULL;
                        ch->uv_count = 0;
                    }
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHWEIGHTS, &section_found, &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        NMO_RETURN_IF_ERROR(nmo_mesh_deserialize_weights(
            chunk, arena, out_state, "legacy"));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_MESHFACECHANMASK, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        int32_t mask_face_count;
        result = nmo_chunk_read_int(chunk, &mask_face_count);
        if (result != NMO_OK) return result;
        if (mask_face_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid legacy mesh face-mask count %d",
                             mask_face_count);
        }
        if (mask_face_count > 0) {
            uint32_t face_count = out_state->face_count;
            if ((uint32_t)mask_face_count > face_count) {
                NMO_RETURN_ERROR(
                    NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                    "Legacy mesh face-mask count exceeds face count");
            }
            uint32_t pair_count = (uint32_t)mask_face_count / 2u;
            uint32_t remainder = (uint32_t)mask_face_count % 2u;
            if (face_count > 0u && out_state->faces == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                 NMO_SEVERITY_ERROR,
                                 "Legacy mesh faces missing for channel masks");
            }
            if ((size_t)pair_count * 2u + remainder >
                nmo_mesh_identifier_remaining_dwords(chunk)) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }

            for (uint32_t i = 0; i < pair_count; i++) {
                uint32_t packed_masks;
                result = nmo_chunk_read_dword_as_words(
                    chunk, &packed_masks);
                if (result != NMO_OK) return result;

                nmo_unpack_dword_to_words(packed_masks,
                    &out_state->faces[i * 2].channel_mask,
                    &out_state->faces[i * 2 + 1].channel_mask);
            }

            if (remainder) {
                result = nmo_chunk_read_word(chunk,
                    &out_state->faces[(uint32_t)mask_face_count - 1u].channel_mask);
                if (result != NMO_OK) return result;
            }
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_seek_optional(
        chunk, CK_STATESAVE_PROGRESSIVEMESH, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        size_t pm_bytes_total = nmo_mesh_identifier_remaining_dwords(chunk) * 4u;
        out_state->has_progressive_mesh = true;

        result = nmo_chunk_read_int(chunk, &out_state->pm_field_0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->pm_morph_enabled);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->pm_morph_step);
        if (result != NMO_OK) return result;

        if (pm_bytes_total > 12u) {
            size_t pm_bytes = pm_bytes_total - 12u;
            out_state->pm_data_size = (uint32_t)pm_bytes;
            out_state->pm_data = nmo_arena_alloc(arena, pm_bytes, 4);
            if (!out_state->pm_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate progressive mesh data");
            }
            result = nmo_mesh_read_raw_bytes(chunk, out_state->pm_data, pm_bytes);
            if (result != NMO_OK) return result;
        }
        NMO_RETURN_IF_ERROR(nmo_mesh_require_identifier_end(chunk));
    }

    NMO_RETURN_OK();
}

/**
 * @brief Main deserialization dispatcher
 */
nmo_status_t nmo_mesh_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_mesh_state_t *out_state = (nmo_mesh_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !out_state || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to deserialize");
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_start_read(chunk));

    nmo_mesh_state_t decoded;
    nmo_status_t result = nmo_mesh_create(&decoded, NULL, NULL);
    if (result != NMO_OK) return result;
    
    // Get chunk data version
    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    
    result = (data_version >= 9)
        ? nmo_mesh_deserialize_modern(chunk, arena, &decoded)
        : nmo_mesh_deserialize_legacy(chunk, arena, &decoded);
    if (result != NMO_OK) {
        char detail[256];
        size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));
        nmo_mesh_destroy(&decoded, NULL, NULL);
        NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                         "CKMesh data version %u deserialization failed%s%s",
                         data_version,
                         detail_len > 0u ? ": " : "",
                         detail_len > 0u ? detail : "");
    }

    const nmo_object_repository_t *repository =
        nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    for (uint32_t i = 0; i < decoded.material_group_count; ++i) {
        nmo_ref_check_class(&decoded.material_groups[i].material,
                            repository, types, NMO_CID_MATERIAL);
    }
    for (uint32_t i = 0; i < decoded.material_channel_count; ++i) {
        nmo_ref_check_class(&decoded.material_channels[i].material,
                            repository, types, NMO_CID_MATERIAL);
    }

    nmo_mesh_recompute_bounds(&decoded);
    nmo_mesh_destroy(out_state, NULL, NULL);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKMesh SERIALIZATION
 * ============================================================================= */

static uint32_t nmo_mesh_compute_save_flags(
    const nmo_mesh_state_t *state)
{
    uint32_t flags = 0x0Fu;

    if (!state) {
        return flags;
    }

    const uint32_t vertex_count = state->vertex_count;

    if (state->flags & VXMESH_PROCEDURALPOS) {
        bool positions_are_zero = true;
        for (uint32_t i = 0; i < vertex_count; ++i) {
            if (state->vertices[i].position.x != 0.0f ||
                state->vertices[i].position.y != 0.0f ||
                state->vertices[i].position.z != 0.0f) {
                positions_are_zero = false;
                break;
            }
        }
        if (positions_are_zero) {
            flags |= NMO_VERTEX_POS_EXTERNAL;
        }
    }

    if (state->vertices && vertex_count > 0) {
        float first_u = state->vertices[0].uv.x;
        float first_v = state->vertices[0].uv.y;
        for (uint32_t i = 0; i < vertex_count; ++i) {
            if (state->vertices[i].uv.x != first_u || state->vertices[i].uv.y != first_v) {
                flags &= ~NMO_VERTEX_UV_UNIFORM;
                break;
            }
        }
    }

    if (state->vertex_colors && vertex_count > 0) {
        uint32_t first_color = state->vertex_colors[0];
        for (uint32_t i = 0; i < vertex_count; ++i) {
            if (state->vertex_colors[i] != first_color) {
                flags &= ~NMO_VERTEX_COLOR1_UNIFORM;
                break;
            }
        }
    }

    if (state->vertex_specular && vertex_count > 0) {
        uint32_t first_specular = state->vertex_specular[0];
        for (uint32_t i = 0; i < vertex_count; ++i) {
            if (state->vertex_specular[i] != first_specular) {
                flags &= ~NMO_VERTEX_SPECULAR_UNIFORM;
                break;
            }
        }
    }

    if (state->vertices && vertex_count > 0) {
        for (uint32_t i = 0; i < vertex_count; ++i) {
            if (state->vertices[i].normal.x != 0.0f ||
                state->vertices[i].normal.y != 0.0f ||
                state->vertices[i].normal.z != 0.0f) {
                flags &= ~NMO_VERTEX_NORMALS_MISSING;
                break;
            }
        }
    }

    return flags;
}

/**
 * @brief Serialize CKMesh state to chunk (placeholder)
 * 
 * @param in_state  Input state to serialize (must not be NULL)
 * @param out_chunk Output chunk to write to (must not be NULL)
 * @param arena     Arena for temporary allocations (must not be NULL)
 * @return Result indicating success or error
 */
static nmo_status_t nmo_mesh_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context,
    bool skip_geometry)
{
    (void)type;
    const nmo_mesh_state_t *in_state = (const nmo_mesh_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file =
        ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL &&
         (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to serialize");
    }

    NMO_RETURN_IF_ERROR(nmo_mesh_validate(in_state, NULL, NULL));

    if (nmo_chunk_get_data_version(out_chunk) == 0u) {
        out_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
    }
    const bool modern_layout =
        nmo_chunk_get_data_version(out_chunk) >= 9u;
    if (!skip_geometry) {
        if (modern_layout) {
            NMO_RETURN_IF_ERROR(
                nmo_mesh_validate_modern_geometry(in_state));
        } else {
            NMO_RETURN_IF_ERROR(
                nmo_mesh_validate_legacy_geometry(in_state));
        }
    }

    nmo_status_t result = nmo_beobject_serialize(&in_state->beobject, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (!is_file && (save_flags & CK_STATESAVE_MESHONLY) == 0) {
        NMO_RETURN_OK();
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHFLAGS);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_dword(out_chunk, in_state->flags);
    if (result != NMO_OK) return result;

    if (!skip_geometry &&
        (in_state->has_material_groups || in_state->material_group_count > 0)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHMATERIALS);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->material_group_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->material_group_count; ++i) {
            result = nmo_ref_write(
                out_chunk, &in_state->material_groups[i].material);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(
                out_chunk, in_state->material_groups[i].padding);
            if (result != NMO_OK) return result;
        }
    }

    if (!skip_geometry && in_state->face_count > 0 && in_state->faces && in_state->face_vertex_indices) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHFACES);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->face_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->face_count; ++i) {
            uint16_t idx0 = in_state->face_vertex_indices[i * 3 + 0];
            uint16_t idx1 = in_state->face_vertex_indices[i * 3 + 1];
            uint16_t idx2 = in_state->face_vertex_indices[i * 3 + 2];
            uint32_t mat_idx = in_state->faces[i].material_group_idx;

            if (modern_layout) {
                uint32_t packed01 = nmo_pack_words_to_dword(idx0, idx1);
                uint32_t packed2mat = nmo_pack_words_to_dword(
                    idx2, (uint16_t)mat_idx);
                result = nmo_chunk_write_dword_as_words(
                    out_chunk, packed01);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword_as_words(
                    out_chunk, packed2mat);
                if (result != NMO_OK) return result;
            } else {
                result = nmo_chunk_write_word(out_chunk, idx0);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_word(out_chunk, idx1);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_word(out_chunk, idx2);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, mat_idx);
                if (result != NMO_OK) return result;
            }
        }
    }

    if (!skip_geometry && in_state->line_count > 0 && in_state->line_indices) {
        size_t line_bytes = (size_t)in_state->line_count * 2u * sizeof(uint16_t);
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHLINES);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->line_count);
        if (result != NMO_OK) return result;
        if (modern_layout) {
            if (line_bytes > UINT32_MAX) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            result = nmo_chunk_write_dword(
                out_chunk, (uint32_t)line_bytes);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_buffer_lendian16(
                out_chunk, in_state->line_indices, line_bytes);
        } else {
            result = nmo_chunk_write_buffer_no_size_lendian16(
                out_chunk, in_state->line_indices,
                (size_t)in_state->line_count * 2u);
        }
        if (result != NMO_OK) return result;
    }

    if (!skip_geometry && in_state->vertex_count > 0 && in_state->vertices) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHVERTICES);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->vertex_count);
        if (result != NMO_OK) return result;
        if (!modern_layout) {
            const uint32_t legacy_save_flags = 0x06u;
            result = nmo_chunk_write_dword(
                out_chunk, legacy_save_flags);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                result = nmo_chunk_write_vector3(
                    out_chunk, &in_state->vertices[i].position);
                if (result != NMO_OK) return result;
            }
            for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                result = nmo_chunk_write_vector3(
                    out_chunk, &in_state->vertices[i].normal);
                if (result != NMO_OK) return result;
            }
            for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                result = nmo_chunk_write_dword(
                    out_chunk, in_state->vertex_colors[i]);
                if (result != NMO_OK) return result;
            }
        } else {
            uint32_t vertex_save_flags =
                nmo_mesh_compute_save_flags(in_state);
            result = nmo_chunk_write_dword(
                out_chunk, vertex_save_flags);
            if (result != NMO_OK) return result;

            size_t buffer_dwords = 0;
            if (!nmo_mesh_vertex_payload_dwords(
                    in_state->vertex_count, vertex_save_flags,
                    &buffer_dwords) ||
                buffer_dwords > UINT32_MAX - 1u ||
                nmo_mesh_size_mul_overflows(
                    buffer_dwords, sizeof(uint32_t))) {
                NMO_RETURN_ERROR(
                    NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                    "Vertex buffer exceeds serialized limits");
            }

            uint32_t *buffer = (uint32_t *)nmo_arena_alloc(
                arena, buffer_dwords * sizeof(uint32_t),
                alignof(uint32_t));
            if (!buffer) {
                NMO_RETURN_ERROR(
                    NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                    "Failed to allocate vertex buffer");
            }

            size_t offset = 0;
            if (!(vertex_save_flags & NMO_VERTEX_POS_EXTERNAL)) {
                for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].position.x, sizeof(float));
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].position.y, sizeof(float));
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].position.z, sizeof(float));
                }
            }

            buffer[offset++] = in_state->vertex_colors[0];
            if (!(vertex_save_flags & NMO_VERTEX_COLOR1_UNIFORM)) {
                for (uint32_t i = 1; i < in_state->vertex_count; ++i) {
                    buffer[offset++] = in_state->vertex_colors[i];
                }
            }

            buffer[offset++] = in_state->vertex_specular[0];
            if (!(vertex_save_flags & NMO_VERTEX_SPECULAR_UNIFORM)) {
                for (uint32_t i = 1; i < in_state->vertex_count; ++i) {
                    buffer[offset++] = in_state->vertex_specular[i];
                }
            }

            if (!(vertex_save_flags & NMO_VERTEX_NORMALS_MISSING)) {
                for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].normal.x, sizeof(float));
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].normal.y, sizeof(float));
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].normal.z, sizeof(float));
                }
            }

            memcpy(&buffer[offset++],
                   &in_state->vertices[0].uv.x, sizeof(float));
            memcpy(&buffer[offset++],
                   &in_state->vertices[0].uv.y, sizeof(float));
            if (!(vertex_save_flags & NMO_VERTEX_UV_UNIFORM)) {
                for (uint32_t i = 1; i < in_state->vertex_count; ++i) {
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].uv.x, sizeof(float));
                    memcpy(&buffer[offset++],
                           &in_state->vertices[i].uv.y, sizeof(float));
                }
            }

            result = nmo_chunk_write_dword(
                out_chunk, (uint32_t)(buffer_dwords + 1u));
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(
                out_chunk, buffer,
                buffer_dwords * sizeof(uint32_t));
            if (result != NMO_OK) return result;
        }
    }

    if (!skip_geometry &&
        (in_state->has_material_channels ||
         in_state->material_channel_count > 0)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHCHANNELS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->material_channel_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->material_channel_count; ++i) {
            const nmo_material_channel_t *channel = &in_state->material_channels[i];
            result = nmo_ref_write(out_chunk, &channel->material);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->flags);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->source_blend);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->dest_blend);
            if (result != NMO_OK) return result;

            uint32_t uv_count = channel->uv_coords
                ? channel->uv_count
                : 0u;
            result = nmo_chunk_write_int(out_chunk, (int32_t)uv_count);
            if (result != NMO_OK) return result;
            for (uint32_t j = 0; j < uv_count; ++j) {
                result = nmo_chunk_write_float(out_chunk, channel->uv_coords[j].x);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_float(out_chunk, channel->uv_coords[j].y);
                if (result != NMO_OK) return result;
            }
        }
    }

    if (in_state->vertex_weights && (in_state->vertex_weight_count > 0 || in_state->vertex_count > 0)) {
        uint32_t weight_count = in_state->vertex_weight_count > 0
            ? in_state->vertex_weight_count
            : in_state->vertex_count;

        if (weight_count > 0) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHWEIGHTS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)weight_count);
            if (result != NMO_OK) return result;

            float first_weight = in_state->vertex_weights[0];
            bool uniform = true;
            for (uint32_t i = 1; i < weight_count; ++i) {
                if (in_state->vertex_weights[i] != first_weight) {
                    uniform = false;
                    break;
                }
            }

            if (!uniform) {
                size_t weight_bytes = 0u;
                if (!nmo_safe_mul_size(
                        (size_t)weight_count, sizeof(float),
                        &weight_bytes) ||
                    weight_bytes > UINT32_MAX) {
                    NMO_RETURN_ERROR(
                        NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                        "Vertex weight buffer exceeds serialized limits");
                }
                uint32_t buffer_size = (uint32_t)weight_bytes;
                result = nmo_chunk_write_dword(out_chunk, buffer_size);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                       in_state->vertex_weights,
                                                       buffer_size);
                if (result != NMO_OK) return result;
            }

            result = nmo_chunk_write_float(out_chunk, first_weight);
            if (result != NMO_OK) return result;
        }
    }

    if (in_state->material_channel_count > 0 && in_state->faces && in_state->face_count > 0) {
        uint16_t mask_and = 0xFFFFu;
        for (uint32_t i = 0; i < in_state->face_count; ++i) {
            mask_and &= in_state->faces[i].channel_mask;
        }

        if (mask_and != 0xFFFFu) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHFACECHANMASK);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->face_count);
            if (result != NMO_OK) return result;

            uint32_t pair_count = in_state->face_count / 2u;
            for (uint32_t i = 0; i < pair_count; ++i) {
                uint16_t m0 = in_state->faces[i * 2].channel_mask;
                uint16_t m1 = in_state->faces[i * 2 + 1].channel_mask;
                uint32_t packed = nmo_pack_words_to_dword(m0, m1);
                result = nmo_chunk_write_dword_as_words(
                    out_chunk, packed);
                if (result != NMO_OK) return result;
            }

            if (in_state->face_count & 1u) {
                result = nmo_chunk_write_word(out_chunk, in_state->faces[in_state->face_count - 1u].channel_mask);
                if (result != NMO_OK) return result;
            }
        }
    }

    if (in_state->has_progressive_mesh && in_state->pm_data && in_state->pm_data_size > 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PROGRESSIVEMESH);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->pm_field_0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->pm_morph_enabled);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->pm_morph_step);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->pm_data, in_state->pm_data_size);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_mesh_serialize_ex(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context,
    bool skip_geometry)
{
    if (!instance || !out_chunk || !out_chunk->arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (!staged) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_mesh_serialize_internal(
        instance, staged, type, context, skip_geometry);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_mesh_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_mesh_serialize_ex(instance, out_chunk, type, context, false);
}

static nmo_status_t nmo_mesh_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    const nmo_mesh_state_t *s = src;
    nmo_mesh_state_t *d = dst;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_mesh_validate(s, NULL, NULL));

    nmo_mesh_state_t copied;
    nmo_status_t result = nmo_mesh_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_beobject_vtable.copy(
        &s->beobject, &copied.beobject, NULL, arena);
    if (result != NMO_OK) goto fail;

    copied.flags = s->flags;
    copied.bary_center = s->bary_center;
    copied.radius = s->radius;
    copied.local_box_min = s->local_box_min;
    copied.local_box_max = s->local_box_max;
    copied.face_count = s->face_count;
    copied.line_count = s->line_count;
    copied.vertex_count = s->vertex_count;
    copied.vertex_weight_count = s->vertex_weight_count;
    copied.material_group_count = s->material_group_count;
    copied.has_material_groups = s->has_material_groups;
    copied.material_channel_count = s->material_channel_count;
    copied.has_material_channels = s->has_material_channels;
    copied.is_valid = s->is_valid;
    copied.vertex_buffer_handle = s->vertex_buffer_handle;
    copied.index_buffer_handle = s->index_buffer_handle;
    copied.has_progressive_mesh = s->has_progressive_mesh;
    copied.pm_field_0 = s->pm_field_0;
    copied.pm_morph_enabled = s->pm_morph_enabled;
    copied.pm_morph_step = s->pm_morph_step;
    copied.pm_data_size = s->pm_data_size;
    const uint32_t copied_weight_count = s->vertex_weight_count > 0u
        ? s->vertex_weight_count
        : (s->vertex_weights != NULL ? s->vertex_count : 0u);

    result = nmo_object_copy_array(arena, (void **)&copied.faces,
        s->faces, sizeof(nmo_face_t), s->face_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(
        arena, (void **)&copied.face_vertex_indices,
        s->face_vertex_indices, sizeof(uint16_t),
        (uint32_t)(s->face_count * 3u));
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(arena, (void **)&copied.line_indices,
        s->line_indices, sizeof(uint16_t),
        (uint32_t)(s->line_count * 2u));
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(arena, (void **)&copied.vertices,
        s->vertices, sizeof(nmo_vertex_t), s->vertex_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(arena, (void **)&copied.vertex_colors,
        s->vertex_colors, sizeof(uint32_t), s->vertex_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(arena, (void **)&copied.vertex_specular,
        s->vertex_specular, sizeof(uint32_t), s->vertex_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(arena, (void **)&copied.vertex_weights,
        s->vertex_weights, sizeof(float), copied_weight_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(arena, (void **)&copied.material_groups,
        s->material_groups, sizeof(nmo_material_group_t),
        s->material_group_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(
        arena, (void **)&copied.material_channels,
        s->material_channels, sizeof(nmo_material_channel_t),
        s->material_channel_count);
    if (result != NMO_OK) goto fail;
    for (uint32_t i = 0; i < s->material_channel_count; ++i) {
        result = nmo_object_copy_array(
            arena, (void **)&copied.material_channels[i].uv_coords,
            s->material_channels[i].uv_coords, sizeof(nmo_vector2_t),
            s->material_channels[i].uv_count);
        if (result != NMO_OK) goto fail;
    }
    result = nmo_object_copy_bytes(
        arena, &copied.pm_data, s->pm_data, s->pm_data_size);
    if (result != NMO_OK) goto fail;

    if (d->beobject.scripts.data == s->beobject.scripts.data) {
        memset(&d->beobject.scripts, 0, sizeof(d->beobject.scripts));
    }
    if (d->beobject.attributes.data == s->beobject.attributes.data) {
        memset(&d->beobject.attributes, 0, sizeof(d->beobject.attributes));
    }
    if (d->beobject.legacy_attributes.data ==
        s->beobject.legacy_attributes.data) {
        memset(&d->beobject.legacy_attributes, 0,
               sizeof(d->beobject.legacy_attributes));
    }
    nmo_mesh_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_mesh_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_mesh_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_mesh_state_t *s = instance;
    if (!s) return NMO_ERR_INVALID_ARGUMENT;
    const uint32_t effective_weight_count = s->vertex_weight_count > 0u
        ? s->vertex_weight_count
        : (s->vertex_weights != NULL ? s->vertex_count : 0u);
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &s->beobject, NULL, context));
    if (s->face_count > UINT32_MAX / 3u ||
        s->line_count > UINT32_MAX / 2u ||
        s->face_count > INT32_MAX || s->line_count > INT32_MAX ||
        s->vertex_count > INT32_MAX || s->vertex_weight_count > INT32_MAX ||
        s->material_group_count > INT32_MAX ||
        s->material_channel_count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "CKMesh count exceeds serialized limits");
    }
    if (nmo_mesh_size_mul_overflows(
            s->face_count, sizeof(*s->faces)) ||
        nmo_mesh_size_mul_overflows(
            s->face_count, 3u * sizeof(*s->face_vertex_indices)) ||
        nmo_mesh_size_mul_overflows(
            s->line_count, 2u * sizeof(*s->line_indices)) ||
        nmo_mesh_size_mul_overflows(
            s->vertex_count, sizeof(*s->vertices)) ||
        nmo_mesh_size_mul_overflows(
            s->vertex_count, sizeof(*s->vertex_colors)) ||
        nmo_mesh_size_mul_overflows(
            effective_weight_count, sizeof(*s->vertex_weights)) ||
        nmo_mesh_size_mul_overflows(
            s->material_group_count, sizeof(*s->material_groups)) ||
        nmo_mesh_size_mul_overflows(
            s->material_channel_count, sizeof(*s->material_channels))) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "CKMesh allocation size overflows");
    }
    NMO_VALIDATE_COUNT(s->faces, s->face_count, "faces");
    if (s->face_count > 0) {
        NMO_VALIDATE_COUNT(s->face_vertex_indices, (uint32_t)(s->face_count * 3u),
                           "face_vertex_indices");
    }
    if (s->line_count > 0) {
        NMO_VALIDATE_COUNT(s->line_indices, (uint32_t)(s->line_count * 2u), "line_indices");
    }
    NMO_VALIDATE_COUNT(s->vertices, s->vertex_count, "vertices");
    NMO_VALIDATE_COUNT(s->vertex_colors, s->vertex_count, "vertex_colors");
    NMO_VALIDATE_COUNT(s->vertex_specular, s->vertex_count, "vertex_specular");
    NMO_VALIDATE_COUNT(s->vertex_weights, effective_weight_count, "vertex_weights");
    NMO_VALIDATE_COUNT(s->material_groups, s->material_group_count, "material_groups");
    NMO_VALIDATE_COUNT(s->material_channels, s->material_channel_count, "material_channels");
    if (s->material_channels) {
        for (uint32_t i = 0; i < s->material_channel_count; ++i) {
            if (s->material_channels[i].uv_count > INT32_MAX ||
                nmo_mesh_size_mul_overflows(
                    s->material_channels[i].uv_count,
                    sizeof(*s->material_channels[i].uv_coords))) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                 NMO_SEVERITY_ERROR,
                                 "CKMesh channel UV count exceeds serialized limits");
            }
            if (s->material_channels[i].uv_count > 0) {
                NMO_VALIDATE_COUNT(s->material_channels[i].uv_coords,
                                   s->material_channels[i].uv_count,
                                   "material_channels.uv_coords");
            }
        }
    }
    NMO_VALIDATE_BYTES(s->pm_data, s->pm_data_size, "pm_data");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_status_t nmo_mesh_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_mesh_validate(instance, type, context);
}

nmo_status_t nmo_mesh_remap_dependencies(
    void *state,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_mesh_remap_dependencies");
    }

    nmo_mesh_state_t *mesh_state = (nmo_mesh_state_t *)state;
    (void)context;

    if (mesh_state->material_group_count > 0 && mesh_state->material_groups == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing material groups");
    }
    if (mesh_state->material_channel_count > 0 && mesh_state->material_channels == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing material channels");
    }

    /* Preserve material references and face group indices verbatim. */
    return nmo_mesh_validate(mesh_state, NULL, NULL);
}

static nmo_status_t nmo_mesh_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_mesh_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_mesh_validate(state, NULL, NULL));
    for (uint32_t i = 0; i < state->material_group_count; ++i) {
        nmo_object_id_t id = nmo_ref_runtime_id(
            &state->material_groups[i].material);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "material_groups.material", i)) {
            return NMO_OK;
        }
    }
    for (uint32_t i = 0; i < state->material_channel_count; ++i) {
        nmo_object_id_t id = nmo_ref_runtime_id(
            &state->material_channels[i].material);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "material_channels.material", i)) {
            return NMO_OK;
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_mesh_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_mesh_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_mesh_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static const nmo_object_serialize_pass_t nmo_mesh_compare_passes[] = {
    {
        .class_id = NMO_CID_MESH,
        .data_version = 9,
        .chunk_options = NMO_CHUNK_OPTION_FILE,
        .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
        .use_context = 1,
    },
};

static bool nmo_mesh_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_mesh_serialize,
        nmo_mesh_compare_passes,
        sizeof(nmo_mesh_compare_passes) /
            sizeof(nmo_mesh_compare_passes[0]),
        16384);
}

static uint32_t nmo_mesh_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_mesh_serialize,
        nmo_mesh_compare_passes,
        sizeof(nmo_mesh_compare_passes) /
            sizeof(nmo_mesh_compare_passes[0]),
        16384);
}

nmo_type_vtable_t nmo_mesh_vtable = {
    .prepare_dependencies = nmo_mesh_prepare_dependencies,
    .remap_dependencies = nmo_mesh_remap_dependencies,
    .pre_delete = nmo_mesh_pre_delete,
    .post_delete = nmo_mesh_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_mesh_create,
        nmo_mesh_destroy,
        nmo_mesh_serialize,
        nmo_mesh_deserialize,
        nmo_mesh_copy,
        nmo_mesh_validate,
        nmo_mesh_equals,
        nmo_mesh_hash,
        nmo_mesh_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_mesh_type,
    CKPGUID_MESH,
    "CKMesh",
    NMO_CID_MESH,
    CKPGUID_BEOBJECT,
    nmo_mesh_state_t,
    &nmo_mesh_vtable,
    nmo_mesh_fields)

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */
