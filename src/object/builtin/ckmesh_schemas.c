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
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>
#include <string.h>
#include <math.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(mesh, nmo_mesh_state_t)

/* Mesh flags are persisted with a restricted bitmask in CK2_3D.dll.
 * Keep this local to the schema to avoid leaking format quirks into headers.
 */
#ifndef NMO_MESH_ALLOWED_FLAGS_MASK
#define NMO_MESH_ALLOWED_FLAGS_MASK 0x007FE39Au
#endif

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
    NMO_FIELD_ARRAY(nmo_mesh_state_t, faces, NMO_GUID_STRUCT_CKFACE),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, face_vertex_indices, CKPGUID_UINT16),
    /* Lines */
    NMO_FIELD(nmo_mesh_state_t, line_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, line_indices, CKPGUID_UINT16),
    /* Vertices */
    NMO_FIELD(nmo_mesh_state_t, vertex_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, vertices, NMO_GUID_STRUCT_VXVERTEX),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, vertex_colors, CKPGUID_COLOR),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, vertex_specular, CKPGUID_COLOR),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, vertex_weights, CKPGUID_FLOAT),
    NMO_FIELD(nmo_mesh_state_t, vertex_weight_count, CKPGUID_UINT32),
    /* Materials */
    NMO_FIELD(nmo_mesh_state_t, material_group_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, material_groups, NMO_GUID_STRUCT_CKMATERIALGROUP),
    NMO_FIELD(nmo_mesh_state_t, material_channel_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_mesh_state_t, material_channels, NMO_GUID_STRUCT_CKMATERIALCHANNEL)
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
    
    // Seek to vertex data identifier
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHVERTICES);
    if (result != NMO_OK) {
        // No vertex data (valid for some meshes)
        out_state->vertex_count = 0;
        NMO_RETURN_OK();
    }
    
    // Read vertex count
    int32_t vertex_count;
    result = nmo_chunk_read_int(chunk, &vertex_count);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read vertex count");
    }
    
    if (vertex_count < 0 || vertex_count > 1000000) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid vertex count");
    }
    
    out_state->vertex_count = (uint32_t)vertex_count;
    if (vertex_count == 0) {
        NMO_RETURN_OK();
    }
    
    // Read save flags
    uint32_t save_flags;
    result = nmo_chunk_read_dword(chunk, &save_flags);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read save flags");
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
    
    // Read buffer size in DWORDs (CKStateChunk-style vertex buffer, includes size dword)
    uint32_t buffer_dwords = 0;
    result = nmo_chunk_read_dword(chunk, &buffer_dwords);
    if (result != NMO_OK) {
        return result;
    }
    if (buffer_dwords == 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Vertex buffer size is zero");
    }
    size_t buffer_payload_dwords = (buffer_dwords > 0u) ? (size_t)(buffer_dwords - 1u) : 0u;

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
    size_t expected_dwords = 0;
    if (!(save_flags & NMO_VERTEX_POS_EXTERNAL)) {
        expected_dwords += 3u * out_state->vertex_count;
    }
    expected_dwords += (save_flags & NMO_VERTEX_COLOR1_UNIFORM) ? 1u : out_state->vertex_count;
    expected_dwords += (save_flags & NMO_VERTEX_SPECULAR_UNIFORM) ? 1u : out_state->vertex_count;
    if (!(save_flags & NMO_VERTEX_NORMALS_MISSING)) {
        expected_dwords += 3u * out_state->vertex_count;
    }
    expected_dwords += (save_flags & NMO_VERTEX_UV_UNIFORM) ? 2u : (2u * out_state->vertex_count);

    if (buffer_payload_dwords < expected_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Vertex buffer shorter than expected");
    }

    if (buffer_payload_dwords > expected_dwords) {
        result = nmo_chunk_skip(chunk, buffer_payload_dwords - expected_dwords);
        if (result != NMO_OK) {
            return result;
        }
    }

    NMO_RETURN_OK();
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
    uint32_t *group_map = NULL;
    uint32_t group_map_count = 0;

    // Load parent CKBeObject
    result = nmo_beobject_deserialize(&out_state->beobject, chunk, NULL, arena);
    if (result != NMO_OK) {
        return result;
    }
    
    // Read mesh flags (identifier CK_STATESAVE_MESHFLAGS)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHFLAGS);
    if (result == NMO_OK) {
        uint32_t flags;
        result = nmo_chunk_read_dword(chunk, &flags);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read mesh flags");
        }
            flags &= NMO_MESH_ALLOWED_FLAGS_MASK;
        out_state->flags = flags;
    }
    
    // Read material groups (identifier CK_STATESAVE_MESHMATERIALS)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHMATERIALS);
    if (result == NMO_OK) {
        int32_t group_count;
        result = nmo_chunk_read_int(chunk, &group_count);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read material group count");
        }
        
        if (group_count > 0 && group_count < 10000) {
            out_state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(
                arena, sizeof(nmo_material_group_t) * (uint32_t)group_count,
                alignof(nmo_material_group_t));
            group_map = (uint32_t *)nmo_arena_alloc(
                arena, sizeof(uint32_t) * (uint32_t)group_count, alignof(uint32_t));
            
            if (!out_state->material_groups || !group_map) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate material groups");
            }

            uint32_t kept = 0;
            for (uint32_t i = 0; i < (uint32_t)group_count; i++) {
                nmo_object_id_t material_id = NMO_OBJECT_ID_NONE;
                result = nmo_chunk_read_object_id(chunk, &material_id);
                if (result != NMO_OK) return result;
                
                // Skip padding
                int32_t padding;
                result = nmo_chunk_read_int(chunk, &padding);
                if (result != NMO_OK) return result;

                if (i > 0 && material_id == NMO_OBJECT_ID_NONE) {
                    group_map[i] = 0;
                    continue;
                }

                out_state->material_groups[kept].material_id = material_id;
                group_map[i] = kept;
                kept++;
            }

            out_state->material_group_count = kept;
            group_map_count = (uint32_t)group_count;
        }
    }
    
    // Read vertices (identifier CK_STATESAVE_MESHVERTICES)
    result = nmo_mesh_deserialize_vertices(chunk, arena, out_state);
    if (result != NMO_OK) {
        return result;
    }
    
    // Read faces (identifier CK_STATESAVE_MESHFACES)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHFACES);
    if (result == NMO_OK) {
        int32_t face_count;
        result = nmo_chunk_read_int(chunk, &face_count);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read face count");
        }
        
        if (face_count > 0 && face_count < 10000000) {
            out_state->face_count = (uint32_t)face_count;
            out_state->faces = (nmo_face_t *)nmo_arena_alloc(
                arena, sizeof(nmo_face_t) * face_count, alignof(nmo_face_t));
            out_state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * face_count * 3, alignof(uint16_t));
            
            if (!out_state->faces || !out_state->face_vertex_indices) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate face arrays");
            }
            
            // Read packed face data
            for (uint32_t i = 0; i < out_state->face_count; i++) {
                out_state->faces[i].channel_mask = 0xFFFFu;
                // Read vertex indices 0,1 (packed as DWORD)
                uint32_t packed01;
                result = nmo_chunk_read_dword(chunk, &packed01);
                if (result != NMO_OK) return result;
                nmo_unpack_dword_to_words(packed01,
                    &out_state->face_vertex_indices[i * 3 + 0],
                    &out_state->face_vertex_indices[i * 3 + 1]);
                
                // Read vertex index 2 + material group index (packed as DWORD)
                uint32_t packed2mat;
                result = nmo_chunk_read_dword(chunk, &packed2mat);
                if (result != NMO_OK) return result;
                uint16_t idx2;
                uint16_t mat_idx;
                nmo_unpack_dword_to_words(packed2mat, &idx2, &mat_idx);
                out_state->face_vertex_indices[i * 3 + 2] = idx2;
                if (group_map && mat_idx < group_map_count) {
                    out_state->faces[i].material_group_idx = (uint16_t)group_map[mat_idx];
                } else {
                    out_state->faces[i].material_group_idx = mat_idx;
                }
            }
        }
    }
    
    // Read lines (identifier CK_STATESAVE_MESHLINES, optional)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHLINES);
    if (result == NMO_OK) {
        int32_t line_count;
        result = nmo_chunk_read_int(chunk, &line_count);
        if (result == NMO_OK && line_count > 0 && line_count < 1000000) {
            out_state->line_count = (uint32_t)line_count;
            out_state->line_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * line_count * 2, alignof(uint16_t));
            
            if (out_state->line_indices) {
                size_t expected_bytes = (size_t)line_count * 2u * sizeof(uint16_t);
                size_t bytes_read = nmo_chunk_read_and_fill_buffer(
                    chunk, out_state->line_indices, expected_bytes);
                if (bytes_read == 0 && expected_bytes > 0) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read line indices buffer");
                }
            }
        }
    }
    
    // Read material channels (identifier CK_STATESAVE_MESHCHANNELS, optional)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHCHANNELS);
    if (result == NMO_OK) {
        int32_t channel_count;
        result = nmo_chunk_read_int(chunk, &channel_count);
        if (result == NMO_OK && channel_count > 0 && channel_count < 100) {
            out_state->material_channel_count = (uint32_t)channel_count;
            out_state->material_channels = (nmo_material_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_material_channel_t) * channel_count,
                alignof(nmo_material_channel_t));
            
            if (out_state->material_channels) {
                memset(out_state->material_channels, 0,
                       sizeof(nmo_material_channel_t) * out_state->material_channel_count);
                for (uint32_t i = 0; i < out_state->material_channel_count; i++) {
                    nmo_material_channel_t *ch = &out_state->material_channels[i];
                    
                    result = nmo_chunk_read_object_id(chunk, &ch->material_id);
                    if (result != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->flags);
                    if (result != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->source_blend);
                    if (result != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->dest_blend);
                    if (result != NMO_OK) break;
                    
                    int32_t uv_count;
                    result = nmo_chunk_read_int(chunk, &uv_count);
                    if (result != NMO_OK) break;
                    
                    if (uv_count > 0 && uv_count < 1000000) {
                        uint32_t alloc_uv_count = (out_state->vertex_count > 0)
                            ? out_state->vertex_count
                            : (uint32_t)uv_count;
                        ch->uv_count = alloc_uv_count;
                        ch->uv_coords = (nmo_vector2_t *)nmo_arena_alloc(
                            arena, sizeof(nmo_vector2_t) * alloc_uv_count,
                            alignof(nmo_vector2_t));
                        
                        if (ch->uv_coords) {
                            for (uint32_t j = 0; j < (uint32_t)uv_count; j++) {
                                result = nmo_chunk_read_float(chunk, &ch->uv_coords[j].x);
                                if (result != NMO_OK) break;
                                result = nmo_chunk_read_float(chunk, &ch->uv_coords[j].y);
                                if (result != NMO_OK) break;
                            }
                            for (uint32_t j = (uint32_t)uv_count; j < alloc_uv_count; j++) {
                                ch->uv_coords[j].x = 0.0f;
                                ch->uv_coords[j].y = 0.0f;
                            }
                        }
                    } else {
                        ch->uv_coords = NULL;
                        ch->uv_count = 0;
                    }
                }
            }
        }
    }
    
    // Read vertex weights (identifier CK_STATESAVE_MESHWEIGHTS, optional)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHWEIGHTS);
    if (result == NMO_OK) {
        size_t weights_bytes_total = nmo_mesh_identifier_remaining_dwords(chunk) * 4u;
        int32_t weight_count;
        result = nmo_chunk_read_int(chunk, &weight_count);
        if (result == NMO_OK && weight_count > 0 && weight_count < 10000000) {
            out_state->vertex_weight_count = (uint32_t)weight_count;

            size_t remaining_bytes = (weights_bytes_total > sizeof(uint32_t))
                ? (weights_bytes_total - sizeof(uint32_t))
                : 0u;

            out_state->vertex_weights = (float *)nmo_arena_alloc(
                arena, sizeof(float) * weight_count, alignof(float));

            if (!out_state->vertex_weights) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate vertex weights");
            }

            if (remaining_bytes == sizeof(float)) {
                float uniform_weight = 0.0f;
                result = nmo_chunk_read_float(chunk, &uniform_weight);
                if (result != NMO_OK) return result;
                for (int32_t i = 0; i < weight_count; ++i) {
                    out_state->vertex_weights[i] = uniform_weight;
                }
            } else if (remaining_bytes >= (sizeof(uint32_t) + sizeof(float))) {
                uint32_t peek = 0;
                result = nmo_mesh_peek_dword(chunk, &peek);
                if (result != NMO_OK) return result;

                if (peek == (uint32_t)(weight_count * sizeof(float))) {
                    uint32_t buffer_size = 0;
                    result = nmo_chunk_read_dword(chunk, &buffer_size);
                    if (result != NMO_OK) return result;

                    if (buffer_size > 0) {
                        result = nmo_mesh_read_raw_bytes(chunk, out_state->vertex_weights, buffer_size);
                        if (result != NMO_OK) return result;
                    }

                    if (remaining_bytes >= (size_t)sizeof(uint32_t) + buffer_size + sizeof(float)) {
                        float tail_weight = 0.0f;
                        result = nmo_chunk_read_float(chunk, &tail_weight);
                        if (result != NMO_OK) return result;
                    }
                } else {
                    float uniform_weight = 0.0f;
                    result = nmo_chunk_read_float(chunk, &uniform_weight);
                    if (result != NMO_OK) return result;
                    for (int32_t i = 0; i < weight_count; ++i) {
                        out_state->vertex_weights[i] = uniform_weight;
                    }
                }
            }
        }
    }
    
    // Read face channel masks (identifier CK_STATESAVE_MESHFACECHANMASK, optional)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHFACECHANMASK);
    if (result == NMO_OK) {
        int32_t mask_face_count;
        result = nmo_chunk_read_int(chunk, &mask_face_count);
        if (result == NMO_OK && mask_face_count > 0) {
            uint32_t face_count = out_state->face_count;
            uint32_t pair_count = (uint32_t)mask_face_count / 2u;
            uint32_t remainder = (uint32_t)mask_face_count % 2u;

            if (face_count < (uint32_t)mask_face_count) {
                pair_count = face_count / 2u;
                remainder = 0u;
            }

            for (uint32_t i = 0; i < pair_count; i++) {
                uint32_t packed_masks;
                result = nmo_chunk_read_dword(chunk, &packed_masks);
                if (result != NMO_OK) break;

                nmo_unpack_dword_to_words(packed_masks,
                    &out_state->faces[i * 2].channel_mask,
                    &out_state->faces[i * 2 + 1].channel_mask);
            }

            if (remainder) {
                result = nmo_chunk_read_word(chunk,
                    &out_state->faces[(uint32_t)mask_face_count - 1u].channel_mask);
            }
        }
    }
    
    // Read progressive mesh (identifier CK_STATESAVE_PROGRESSIVEMESH, optional)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PROGRESSIVEMESH);
    if (result == NMO_OK) {
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

    result = nmo_beobject_deserialize(&out_state->beobject, chunk, NULL, arena);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHFLAGS) == NMO_OK) {
        uint32_t flags;
        result = nmo_chunk_read_dword(chunk, &flags);
        if (result != NMO_OK) return result;
        flags &= NMO_MESH_ALLOWED_FLAGS_MASK;
        out_state->flags = flags;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHVERTICES) == NMO_OK) {
        int32_t vertex_count;
        result = nmo_chunk_read_int(chunk, &vertex_count);
        if (result != NMO_OK) return result;

        uint32_t save_flags = 0;
        result = nmo_chunk_read_dword(chunk, &save_flags);
        if (result != NMO_OK) return result;

        if (vertex_count > 0 && vertex_count < 1000000) {
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
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHFACES) == NMO_OK) {
        int32_t face_count;
        result = nmo_chunk_read_int(chunk, &face_count);
        if (result != NMO_OK) return result;

        if (face_count > 0 && face_count < 10000000) {
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
                out_state->faces[i].material_group_idx = (uint16_t)mat_idx;
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHLINES) == NMO_OK) {
        int32_t line_count;
        result = nmo_chunk_read_int(chunk, &line_count);
        if (result != NMO_OK) return result;

        if (line_count > 0 && line_count < 1000000) {
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
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHCHANNELS) == NMO_OK) {
        int32_t channel_count;
        result = nmo_chunk_read_int(chunk, &channel_count);
        if (result == NMO_OK && channel_count > 0 && channel_count < 100) {
            out_state->material_channel_count = (uint32_t)channel_count;
            out_state->material_channels = (nmo_material_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_material_channel_t) * out_state->material_channel_count,
                alignof(nmo_material_channel_t));

            if (out_state->material_channels) {
                memset(out_state->material_channels, 0,
                       sizeof(nmo_material_channel_t) * out_state->material_channel_count);
                for (uint32_t i = 0; i < out_state->material_channel_count; i++) {
                    nmo_material_channel_t *ch = &out_state->material_channels[i];

                    result = nmo_chunk_read_object_id(chunk, &ch->material_id);
                    if (result != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->flags);
                    if (result != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->source_blend);
                    if (result != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->dest_blend);
                    if (result != NMO_OK) break;

                    int32_t uv_count;
                    result = nmo_chunk_read_int(chunk, &uv_count);
                    if (result != NMO_OK) break;

                    if (uv_count > 0 && uv_count < 1000000) {
                        uint32_t alloc_uv_count = (out_state->vertex_count > 0)
                            ? out_state->vertex_count
                            : (uint32_t)uv_count;
                        ch->uv_count = alloc_uv_count;
                        ch->uv_coords = (nmo_vector2_t *)nmo_arena_alloc(
                            arena, sizeof(nmo_vector2_t) * alloc_uv_count,
                            alignof(nmo_vector2_t));

                        if (ch->uv_coords) {
                            for (uint32_t j = 0; j < (uint32_t)uv_count; j++) {
                                result = nmo_chunk_read_float(chunk, &ch->uv_coords[j].x);
                                if (result != NMO_OK) break;
                                result = nmo_chunk_read_float(chunk, &ch->uv_coords[j].y);
                                if (result != NMO_OK) break;
                            }
                            for (uint32_t j = (uint32_t)uv_count; j < alloc_uv_count; j++) {
                                ch->uv_coords[j].x = 0.0f;
                                ch->uv_coords[j].y = 0.0f;
                            }
                        }
                    } else {
                        ch->uv_coords = NULL;
                        ch->uv_count = 0;
                    }
                }
            }
        }
    }

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHWEIGHTS);
    if (result == NMO_OK) {
        size_t weights_bytes_total = nmo_mesh_identifier_remaining_dwords(chunk) * 4u;
        int32_t weight_count;
        result = nmo_chunk_read_int(chunk, &weight_count);
        if (result == NMO_OK && weight_count > 0 && weight_count < 10000000) {
            out_state->vertex_weight_count = (uint32_t)weight_count;
            size_t remaining_bytes = (weights_bytes_total > sizeof(uint32_t))
                ? (weights_bytes_total - sizeof(uint32_t))
                : 0u;

            out_state->vertex_weights = (float *)nmo_arena_alloc(
                arena, sizeof(float) * weight_count, alignof(float));
            if (!out_state->vertex_weights) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy vertex weights");
            }

            if (remaining_bytes == sizeof(float)) {
                float uniform_weight = 0.0f;
                result = nmo_chunk_read_float(chunk, &uniform_weight);
                if (result != NMO_OK) return result;
                for (int32_t i = 0; i < weight_count; ++i) {
                    out_state->vertex_weights[i] = uniform_weight;
                }
            } else if (remaining_bytes >= (sizeof(uint32_t) + sizeof(float))) {
                uint32_t peek = 0;
                result = nmo_mesh_peek_dword(chunk, &peek);
                if (result != NMO_OK) return result;

                if (peek == (uint32_t)(weight_count * sizeof(float))) {
                    uint32_t buffer_size = 0;
                    result = nmo_chunk_read_dword(chunk, &buffer_size);
                    if (result != NMO_OK) return result;

                    if (buffer_size > 0) {
                        result = nmo_mesh_read_raw_bytes(chunk, out_state->vertex_weights, buffer_size);
                        if (result != NMO_OK) return result;
                    }

                    if (remaining_bytes >= (size_t)sizeof(uint32_t) + buffer_size + sizeof(float)) {
                        float tail_weight = 0.0f;
                        result = nmo_chunk_read_float(chunk, &tail_weight);
                        if (result != NMO_OK) return result;
                    }
                } else {
                    float uniform_weight = 0.0f;
                    result = nmo_chunk_read_float(chunk, &uniform_weight);
                    if (result != NMO_OK) return result;
                    for (int32_t i = 0; i < weight_count; ++i) {
                        out_state->vertex_weights[i] = uniform_weight;
                    }
                }
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHFACECHANMASK) == NMO_OK) {
        int32_t mask_face_count;
        result = nmo_chunk_read_int(chunk, &mask_face_count);
        if (result == NMO_OK && mask_face_count > 0) {
            uint32_t face_count = out_state->face_count;
            uint32_t pair_count = (uint32_t)mask_face_count / 2u;
            uint32_t remainder = (uint32_t)mask_face_count % 2u;

            if (face_count < (uint32_t)mask_face_count) {
                pair_count = face_count / 2u;
                remainder = 0u;
            }

            for (uint32_t i = 0; i < pair_count; i++) {
                uint32_t packed_masks;
                result = nmo_chunk_read_dword(chunk, &packed_masks);
                if (result != NMO_OK) break;

                nmo_unpack_dword_to_words(packed_masks,
                    &out_state->faces[i * 2].channel_mask,
                    &out_state->faces[i * 2 + 1].channel_mask);
            }

            if (remainder) {
                result = nmo_chunk_read_word(chunk,
                    &out_state->faces[(uint32_t)mask_face_count - 1u].channel_mask);
            }
        }
    }

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PROGRESSIVEMESH);
    if (result == NMO_OK) {
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

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to deserialize");
    }
    
    // Get chunk data version
    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    
    if (data_version >= 9) {
        return nmo_mesh_deserialize_modern(chunk, arena, out_state);
    }

    return nmo_mesh_deserialize_legacy(chunk, arena, out_state);
}

/* =============================================================================
 * CKMesh SERIALIZATION
 * ============================================================================= */

static uint32_t nmo_mesh_compute_save_flags(
    const nmo_mesh_state_t *state,
    nmo_arena_t *arena)
{
    uint32_t flags = 0x0Fu;

    if (!state) {
        return flags;
    }

    const uint32_t vertex_count = state->vertex_count;

    if (state->flags & VXMESH_PROCEDURALPOS) {
        flags |= NMO_VERTEX_POS_EXTERNAL;
    }

    if (!(state->flags & VXMESH_PROCEDURALUV) && state->vertices && vertex_count > 0) {
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

    if (state->vertices && state->faces && state->face_vertex_indices &&
        vertex_count > 0 && state->face_count > 0 &&
        !(state->flags & (VXMESH_GENNORMALS | VXMESH_PROCEDURALPOS))) {

        nmo_vector_t *tmp_normals = (nmo_vector_t *)nmo_arena_alloc(
            arena, sizeof(nmo_vector_t) * vertex_count, alignof(nmo_vector_t));
        if (tmp_normals) {
            for (uint32_t i = 0; i < vertex_count; ++i) {
                tmp_normals[i].x = 0.0f;
                tmp_normals[i].y = 0.0f;
                tmp_normals[i].z = 0.0f;
            }

            for (uint32_t f = 0; f < state->face_count; ++f) {
                uint16_t i0 = state->face_vertex_indices[f * 3 + 0];
                uint16_t i1 = state->face_vertex_indices[f * 3 + 1];
                uint16_t i2 = state->face_vertex_indices[f * 3 + 2];
                if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
                    continue;
                }

                nmo_vector_t v0 = state->vertices[i0].position;
                nmo_vector_t v1 = state->vertices[i1].position;
                nmo_vector_t v2 = state->vertices[i2].position;

                nmo_vector_t e1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                nmo_vector_t e2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                nmo_vector_t n = {
                    e1.y * e2.z - e1.z * e2.y,
                    e1.z * e2.x - e1.x * e2.z,
                    e1.x * e2.y - e1.y * e2.x
                };

                tmp_normals[i0].x += n.x;
                tmp_normals[i0].y += n.y;
                tmp_normals[i0].z += n.z;
                tmp_normals[i1].x += n.x;
                tmp_normals[i1].y += n.y;
                tmp_normals[i1].z += n.z;
                tmp_normals[i2].x += n.x;
                tmp_normals[i2].y += n.y;
                tmp_normals[i2].z += n.z;
            }

            nmo_vector_t total_diff = {0.0f, 0.0f, 0.0f};
            for (uint32_t i = 0; i < vertex_count; ++i) {
                nmo_vector_t computed = tmp_normals[i];
                float len = sqrtf(computed.x * computed.x + computed.y * computed.y + computed.z * computed.z);
                if (len > 0.000001f) {
                    computed.x /= len;
                    computed.y /= len;
                    computed.z /= len;
                }

                nmo_vector_t stored = state->vertices[i].normal;
                float s_len = sqrtf(stored.x * stored.x + stored.y * stored.y + stored.z * stored.z);
                if (s_len > 0.000001f) {
                    stored.x /= s_len;
                    stored.y /= s_len;
                    stored.z /= s_len;
                }

                nmo_vector_t diff = {
                    computed.x - stored.x,
                    computed.y - stored.y,
                    computed.z - stored.z
                };
                total_diff.x += diff.x;
                total_diff.y += diff.y;
                total_diff.z += diff.z;
            }

            total_diff.x /= (float)vertex_count;
            total_diff.y /= (float)vertex_count;
            total_diff.z /= (float)vertex_count;

            float diff_mag = sqrtf(total_diff.x * total_diff.x + total_diff.y * total_diff.y + total_diff.z * total_diff.z);
            if (diff_mag < 0.001f) {
                flags &= ~NMO_VERTEX_NORMALS_MISSING;
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
nmo_status_t nmo_mesh_serialize_ex(
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
    const bool is_file = (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to serialize");
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
    result = nmo_chunk_write_dword(out_chunk, in_state->flags & NMO_MESH_ALLOWED_FLAGS_MASK);
    if (result != NMO_OK) return result;

    if (!skip_geometry && in_state->material_group_count > 0 && in_state->material_groups) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHMATERIALS);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->material_group_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->material_group_count; ++i) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->material_groups[i].material_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, 0);
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
            uint16_t mat_idx = in_state->faces[i].material_group_idx;

            uint32_t packed01 = nmo_pack_words_to_dword(idx0, idx1);
            uint32_t packed2mat = nmo_pack_words_to_dword(idx2, mat_idx);

            result = nmo_chunk_write_dword(out_chunk, packed01);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, packed2mat);
            if (result != NMO_OK) return result;
        }
    }

    if (!skip_geometry && in_state->line_count > 0 && in_state->line_indices) {
        size_t line_bytes = (size_t)in_state->line_count * 2u * sizeof(uint16_t);
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHLINES);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->line_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_buffer(out_chunk, in_state->line_indices, line_bytes);
        if (result != NMO_OK) return result;
    }

    if (!skip_geometry && in_state->vertex_count > 0 && in_state->vertices) {
        uint32_t vertex_save_flags = nmo_mesh_compute_save_flags(in_state, arena);

        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHVERTICES);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->vertex_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, vertex_save_flags);
        if (result != NMO_OK) return result;

        size_t buffer_dwords = 0;
        if (!(vertex_save_flags & NMO_VERTEX_POS_EXTERNAL)) {
            buffer_dwords += 3u * in_state->vertex_count;
        }
        buffer_dwords += (vertex_save_flags & NMO_VERTEX_COLOR1_UNIFORM) ? 1u : in_state->vertex_count;
        buffer_dwords += (vertex_save_flags & NMO_VERTEX_SPECULAR_UNIFORM) ? 1u : in_state->vertex_count;
        if (!(vertex_save_flags & NMO_VERTEX_NORMALS_MISSING)) {
            buffer_dwords += 3u * in_state->vertex_count;
        }
        buffer_dwords += (vertex_save_flags & NMO_VERTEX_UV_UNIFORM) ? 2u : (2u * in_state->vertex_count);

        uint32_t *buffer = (uint32_t *)nmo_arena_alloc(
            arena, buffer_dwords * sizeof(uint32_t), alignof(uint32_t));
        if (!buffer) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate vertex buffer");
        }

        size_t offset = 0;
        if (!(vertex_save_flags & NMO_VERTEX_POS_EXTERNAL)) {
            for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                memcpy(&buffer[offset++], &in_state->vertices[i].position.x, sizeof(float));
                memcpy(&buffer[offset++], &in_state->vertices[i].position.y, sizeof(float));
                memcpy(&buffer[offset++], &in_state->vertices[i].position.z, sizeof(float));
            }
        }

        if (in_state->vertex_colors && in_state->vertex_count > 0) {
            buffer[offset++] = in_state->vertex_colors[0];
            if (!(vertex_save_flags & NMO_VERTEX_COLOR1_UNIFORM)) {
                for (uint32_t i = 1; i < in_state->vertex_count; ++i) {
                    buffer[offset++] = in_state->vertex_colors[i];
                }
            }
        } else {
            buffer[offset++] = 0;
        }

        if (in_state->vertex_specular && in_state->vertex_count > 0) {
            buffer[offset++] = in_state->vertex_specular[0];
            if (!(vertex_save_flags & NMO_VERTEX_SPECULAR_UNIFORM)) {
                for (uint32_t i = 1; i < in_state->vertex_count; ++i) {
                    buffer[offset++] = in_state->vertex_specular[i];
                }
            }
        } else {
            buffer[offset++] = 0;
        }

        if (!(vertex_save_flags & NMO_VERTEX_NORMALS_MISSING)) {
            for (uint32_t i = 0; i < in_state->vertex_count; ++i) {
                memcpy(&buffer[offset++], &in_state->vertices[i].normal.x, sizeof(float));
                memcpy(&buffer[offset++], &in_state->vertices[i].normal.y, sizeof(float));
                memcpy(&buffer[offset++], &in_state->vertices[i].normal.z, sizeof(float));
            }
        }

        if (in_state->vertex_count > 0) {
            memcpy(&buffer[offset++], &in_state->vertices[0].uv.x, sizeof(float));
            memcpy(&buffer[offset++], &in_state->vertices[0].uv.y, sizeof(float));
            if (!(vertex_save_flags & NMO_VERTEX_UV_UNIFORM)) {
                for (uint32_t i = 1; i < in_state->vertex_count; ++i) {
                    memcpy(&buffer[offset++], &in_state->vertices[i].uv.x, sizeof(float));
                    memcpy(&buffer[offset++], &in_state->vertices[i].uv.y, sizeof(float));
                }
            }
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)(buffer_dwords + 1u));
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_buffer_no_size(out_chunk, buffer, buffer_dwords * sizeof(uint32_t));
        if (result != NMO_OK) return result;
    }

    if (!skip_geometry && in_state->material_channel_count > 0 && in_state->material_channels) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHCHANNELS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->material_channel_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->material_channel_count; ++i) {
            const nmo_material_channel_t *channel = &in_state->material_channels[i];
            result = nmo_chunk_write_object_id(out_chunk, channel->material_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->flags);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->source_blend);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->dest_blend);
            if (result != NMO_OK) return result;

            if (channel->uv_coords && channel->uv_count > 0 &&
                channel->uv_count < in_state->vertex_count) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                 "Material channel UV count less than vertex count");
            }

            uint32_t uv_count = (channel->uv_coords && in_state->vertex_count > 0)
                ? in_state->vertex_count
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
                uint32_t buffer_size = weight_count * (uint32_t)sizeof(float);
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
                result = nmo_chunk_write_dword(out_chunk, packed);
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
    const nmo_mesh_state_t *s = src;
    nmo_mesh_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->beobject.base.raw_tail,
                                              s->beobject.base.raw_tail, s->beobject.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->beobject.script_ids, &d->beobject.script_ids,
                                        &s->beobject.script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->beobject.attribute_parameter_ids,
                                        &d->beobject.attribute_parameter_ids,
                                        &s->beobject.attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->beobject.attribute_types,
                                        &d->beobject.attribute_types,
                                        &s->beobject.attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->beobject.attribute_chunks,
                                                     &s->beobject.attribute_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->beobject.legacy_attributes_raw,
                                        &d->beobject.legacy_attributes_raw,
                                        &s->beobject.legacy_attributes_raw.allocator));

    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->faces,
                                              s->faces, sizeof(nmo_face_t), s->face_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->face_vertex_indices,
                                              s->face_vertex_indices, sizeof(uint16_t),
                                              (uint32_t)(s->face_count * 3u)));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->line_indices,
                                              s->line_indices, sizeof(uint16_t),
                                              (uint32_t)(s->line_count * 2u)));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->vertices,
                                              s->vertices, sizeof(nmo_vertex_t), s->vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->vertex_colors,
                                              s->vertex_colors, sizeof(uint32_t), s->vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->vertex_specular,
                                              s->vertex_specular, sizeof(uint32_t), s->vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->vertex_weights,
                                              s->vertex_weights, sizeof(float), s->vertex_weight_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->material_groups,
                                              s->material_groups, sizeof(nmo_material_group_t),
                                              s->material_group_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->material_channels,
                                              s->material_channels, sizeof(nmo_material_channel_t),
                                              s->material_channel_count));
    for (uint32_t i = 0; i < s->material_channel_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena,
                                                  (void **)&d->material_channels[i].uv_coords,
                                                  s->material_channels[i].uv_coords,
                                                  sizeof(nmo_vector2_t),
                                                  s->material_channels[i].uv_count));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, &d->pm_data, s->pm_data, s->pm_data_size));
    NMO_RETURN_OK();
}

static nmo_status_t nmo_mesh_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_mesh_state_t *s = instance;
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
    NMO_VALIDATE_COUNT(s->vertex_weights, s->vertex_weight_count, "vertex_weights");
    NMO_VALIDATE_COUNT(s->material_groups, s->material_group_count, "material_groups");
    NMO_VALIDATE_COUNT(s->material_channels, s->material_channel_count, "material_channels");
    if (s->material_channels) {
        for (uint32_t i = 0; i < s->material_channel_count; ++i) {
            if (s->material_channels[i].uv_count > 0) {
                NMO_VALIDATE_COUNT(s->material_channels[i].uv_coords,
                                   s->material_channels[i].uv_count,
                                   "material_channels.uv_coords");
                if (s->vertex_count > 0 &&
                    s->material_channels[i].uv_count < s->vertex_count) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                     "material channel UV count less than vertex count");
                }
            }
        }
    }
    NMO_VALIDATE_BYTES(s->pm_data, s->pm_data_size, "pm_data");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    mesh,
    nmo_mesh_state_t,
    nmo_mesh_serialize,
    nmo_mesh_deserialize,
    nmo_mesh_fields,
    CKPGUID_MESH,
    "CKMesh",
    NMO_CID_MESH,
    CKPGUID_BEOBJECT
)

/* =============================================================================
 * FINISH LOADING
 * ============================================================================= */

/**
 * @brief Finish loading (resolve references, build normals if needed)
 */
nmo_status_t nmo_mesh_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    if (!state || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to finish_loading");
    }
    
    // Cast state
    nmo_mesh_state_t *mesh_state = (nmo_mesh_state_t *)state;
    
    // Mesh-specific finish loading tasks would go here:
    // - Build face normals if missing
    // - Resolve material group references
    // - Initialize progressive mesh
    // - Create hardware buffers
    
    // Note: CKBeObject doesn't have finish_loading, only CKObject does.
    // Mesh doesn't need to chain to parent finish_loading.
    
    (void)mesh_state;  // Suppress unused warning
    (void)repository;  // Suppress unused warning
    
    NMO_RETURN_OK();
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */


