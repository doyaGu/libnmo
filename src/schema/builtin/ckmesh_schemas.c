/**
 * @file ckmesh_schemas.c
 * @brief CKMesh schema implementation
 *
 * Implements schema for RCKMesh based on reverse engineering analysis.
 * 
 * Serialization format (from CK2_3D.dll analysis):
 * 
 * Modern format (version ≥9):
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

#include "schema/nmo_ckmesh_schemas.h"
#include "schema/nmo_ckbeobject_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

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
 * CKMesh DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize vertex data (identifier 0x20000)
 */
static nmo_result_t nmo_ckmesh_deserialize_vertices(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_mesh_state_t *out_state)
{
    nmo_result_t result;
    
    // Seek to vertex data identifier
    result = nmo_chunk_seek_identifier(chunk, 0x20000);
    if (result.code != NMO_OK) {
        // No vertex data (valid for some meshes)
        out_state->vertex_count = 0;
        return nmo_result_ok();
    }
    
    // Read vertex count
    int32_t vertex_count;
    result = nmo_chunk_read_int(chunk, &vertex_count);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read vertex count");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    if (vertex_count < 0 || vertex_count > 1000000) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid vertex count"));
    }
    
    out_state->vertex_count = (uint32_t)vertex_count;
    if (vertex_count == 0) {
        return nmo_result_ok();
    }
    
    // Read save flags
    uint32_t save_flags;
    result = nmo_chunk_read_dword(chunk, &save_flags);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read save flags");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Allocate vertex array
    out_state->vertices = (nmo_vx_vertex_t *)nmo_arena_alloc(
        arena, sizeof(nmo_vx_vertex_t) * vertex_count, alignof(nmo_vx_vertex_t));
    if (!out_state->vertices) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate vertex array"));
    }
    
    // Allocate color arrays
    out_state->vertex_colors = (uint32_t *)nmo_arena_alloc(
        arena, sizeof(uint32_t) * vertex_count, alignof(uint32_t));
    out_state->vertex_specular = (uint32_t *)nmo_arena_alloc(
        arena, sizeof(uint32_t) * vertex_count, alignof(uint32_t));
    
    if (!out_state->vertex_colors || !out_state->vertex_specular) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate color arrays"));
    }
    
    // Read positions (if not external)
    if (!(save_flags & NMO_VERTEX_POS_EXTERNAL)) {
        for (uint32_t i = 0; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.x);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.y);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].position.z);
            if (result.code != NMO_OK) return result;
        }
    }
    
    // Read vertex colors (at least one, then N if not uniform)
    uint32_t first_color;
    result = nmo_chunk_read_dword(chunk, &first_color);
    if (result.code != NMO_OK) return result;
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
            if (result.code != NMO_OK) return result;
        }
    }
    
    // Read specular colors (at least one, then N if not uniform)
    uint32_t first_specular;
    result = nmo_chunk_read_dword(chunk, &first_specular);
    if (result.code != NMO_OK) return result;
    out_state->vertex_specular[0] = first_specular;
    
    if (save_flags & NMO_VERTEX_SPECULAR_UNIFORM) {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            out_state->vertex_specular[i] = first_specular;
        }
    } else {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_dword(chunk, &out_state->vertex_specular[i]);
            if (result.code != NMO_OK) return result;
        }
    }
    
    // Read normals (if not missing)
    if (!(save_flags & NMO_VERTEX_NORMALS_MISSING)) {
        for (uint32_t i = 0; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.x);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.y);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].normal.z);
            if (result.code != NMO_OK) return result;
        }
    }
    
    // Read UVs (at least one, then N if not uniform)
    result = nmo_chunk_read_float(chunk, &out_state->vertices[0].uv.u);
    if (result.code != NMO_OK) return result;
    result = nmo_chunk_read_float(chunk, &out_state->vertices[0].uv.v);
    if (result.code != NMO_OK) return result;
    
    if (save_flags & NMO_VERTEX_UV_UNIFORM) {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            out_state->vertices[i].uv = out_state->vertices[0].uv;
        }
    } else {
        for (uint32_t i = 1; i < out_state->vertex_count; i++) {
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].uv.u);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->vertices[i].uv.v);
            if (result.code != NMO_OK) return result;
        }
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize CKMesh state from chunk (modern format v9+)
 */
static nmo_result_t nmo_ckmesh_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_mesh_state_t *out_state)
{
    nmo_result_t result;
    
    // Initialize state
    memset(out_state, 0, sizeof(*out_state));
    
    // Load parent CKBeObject
    result = nmo_get_ckbeobject_deserialize()(chunk, arena, &out_state->beobject);
    if (result.code != NMO_OK) {
        return result;
    }
    
    // Read mesh flags (identifier 0x2000)
    result = nmo_chunk_seek_identifier(chunk, 0x2000);
    if (result.code == NMO_OK) {
        uint32_t flags;
        result = nmo_chunk_read_dword(chunk, &flags);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read mesh flags");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        out_state->flags = flags & NMO_MESH_FLAGS_VALID_MASK;
    }
    
    // Read material groups (identifier 0x100000)
    result = nmo_chunk_seek_identifier(chunk, 0x100000);
    if (result.code == NMO_OK) {
        int32_t group_count;
        result = nmo_chunk_read_int(chunk, &group_count);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read material group count");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        if (group_count > 0 && group_count < 10000) {
            out_state->material_group_count = (uint32_t)group_count;
            out_state->material_groups = (nmo_ck_material_group_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ck_material_group_t) * group_count,
                alignof(nmo_ck_material_group_t));
            
            if (!out_state->material_groups) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate material groups"));
            }
            
            for (uint32_t i = 0; i < out_state->material_group_count; i++) {
                result = nmo_chunk_read_object_id(chunk, &out_state->material_groups[i].material_id);
                if (result.code != NMO_OK) return result;
                
                // Skip padding
                int32_t padding;
                result = nmo_chunk_read_int(chunk, &padding);
                if (result.code != NMO_OK) return result;
            }
        }
    }
    
    // Read vertices (identifier 0x20000)
    result = nmo_ckmesh_deserialize_vertices(chunk, arena, out_state);
    if (result.code != NMO_OK) {
        return result;
    }
    
    // Read faces (identifier 0x10000)
    result = nmo_chunk_seek_identifier(chunk, 0x10000);
    if (result.code == NMO_OK) {
        int32_t face_count;
        result = nmo_chunk_read_int(chunk, &face_count);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read face count");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        if (face_count > 0 && face_count < 10000000) {
            out_state->face_count = (uint32_t)face_count;
            out_state->faces = (nmo_ck_face_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ck_face_t) * face_count, alignof(nmo_ck_face_t));
            out_state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * face_count * 3, alignof(uint16_t));
            
            if (!out_state->faces || !out_state->face_vertex_indices) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate face arrays"));
            }
            
            // Read packed face data
            for (uint32_t i = 0; i < out_state->face_count; i++) {
                // Read vertex indices 0,1 (packed as DWORD)
                uint32_t packed01;
                result = nmo_chunk_read_dword(chunk, &packed01);
                if (result.code != NMO_OK) return result;
                nmo_unpack_dword_to_words(packed01,
                    &out_state->face_vertex_indices[i * 3 + 0],
                    &out_state->face_vertex_indices[i * 3 + 1]);
                
                // Read vertex index 2 + material group index (packed as DWORD)
                uint32_t packed2mat;
                result = nmo_chunk_read_dword(chunk, &packed2mat);
                if (result.code != NMO_OK) return result;
                nmo_unpack_dword_to_words(packed2mat,
                    &out_state->face_vertex_indices[i * 3 + 2],
                    &out_state->faces[i].material_group_idx);
            }
        }
    }
    
    // Read lines (identifier 0x40000, optional)
    result = nmo_chunk_seek_identifier(chunk, 0x40000);
    if (result.code == NMO_OK) {
        int32_t line_count;
        result = nmo_chunk_read_int(chunk, &line_count);
        if (result.code == NMO_OK && line_count > 0 && line_count < 1000000) {
            out_state->line_count = (uint32_t)line_count;
            out_state->line_indices = (uint16_t *)nmo_arena_alloc(
                arena, sizeof(uint16_t) * line_count * 2, alignof(uint16_t));
            
            if (out_state->line_indices) {
                for (uint32_t i = 0; i < out_state->line_count * 2; i++) {
                    result = nmo_chunk_read_word(chunk, &out_state->line_indices[i]);
                    if (result.code != NMO_OK) break;
                }
            }
        }
    }
    
    // Read material channels (identifier 0x4000, optional)
    result = nmo_chunk_seek_identifier(chunk, 0x4000);
    if (result.code == NMO_OK) {
        int32_t channel_count;
        result = nmo_chunk_read_int(chunk, &channel_count);
        if (result.code == NMO_OK && channel_count > 0 && channel_count < 100) {
            out_state->material_channel_count = (uint32_t)channel_count;
            out_state->material_channels = (nmo_ck_material_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ck_material_channel_t) * channel_count,
                alignof(nmo_ck_material_channel_t));
            
            if (out_state->material_channels) {
                for (uint32_t i = 0; i < out_state->material_channel_count; i++) {
                    nmo_ck_material_channel_t *ch = &out_state->material_channels[i];
                    
                    result = nmo_chunk_read_object_id(chunk, &ch->material_id);
                    if (result.code != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->flags);
                    if (result.code != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->source_blend);
                    if (result.code != NMO_OK) break;
                    result = nmo_chunk_read_dword(chunk, &ch->dest_blend);
                    if (result.code != NMO_OK) break;
                    
                    int32_t uv_count;
                    result = nmo_chunk_read_int(chunk, &uv_count);
                    if (result.code != NMO_OK) break;
                    
                    ch->uv_count = (uint32_t)uv_count;
                    if (uv_count > 0 && uv_count < 1000000) {
                        ch->uv_coords = (nmo_vx_2d_vector_t *)nmo_arena_alloc(
                            arena, sizeof(nmo_vx_2d_vector_t) * uv_count,
                            alignof(nmo_vx_2d_vector_t));
                        
                        if (ch->uv_coords) {
                            for (uint32_t j = 0; j < ch->uv_count; j++) {
                                result = nmo_chunk_read_float(chunk, &ch->uv_coords[j].u);
                                if (result.code != NMO_OK) break;
                                result = nmo_chunk_read_float(chunk, &ch->uv_coords[j].v);
                                if (result.code != NMO_OK) break;
                            }
                        }
                    } else {
                        ch->uv_coords = NULL;
                    }
                }
            }
        }
    }
    
    // Read vertex weights (identifier 0x80000, optional)
    result = nmo_chunk_seek_identifier(chunk, 0x80000);
    if (result.code == NMO_OK) {
        int32_t weight_count;
        result = nmo_chunk_read_int(chunk, &weight_count);
        if (result.code == NMO_OK && weight_count > 0 && weight_count < 10000000) {
            out_state->vertex_weights = (float *)nmo_arena_alloc(
                arena, sizeof(float) * weight_count, alignof(float));
            
            if (out_state->vertex_weights) {
                // Read first weight
                float first_weight;
                result = nmo_chunk_read_float(chunk, &first_weight);
                if (result.code == NMO_OK) {
                    out_state->vertex_weights[0] = first_weight;
                    
                    // Try to read second weight to determine format
                    if (weight_count > 1) {
                        float second_weight;
                        result = nmo_chunk_read_float(chunk, &second_weight);
                        if (result.code == NMO_OK) {
                            // Full array format
                            out_state->vertex_weights[1] = second_weight;
                            for (int32_t i = 2; i < weight_count; i++) {
                                result = nmo_chunk_read_float(chunk, &out_state->vertex_weights[i]);
                                if (result.code != NMO_OK) break;
                            }
                        } else {
                            // Single value format (all same)
                            for (int32_t i = 1; i < weight_count; i++) {
                                out_state->vertex_weights[i] = first_weight;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Read face channel masks (identifier 0x8000, optional)
    result = nmo_chunk_seek_identifier(chunk, 0x8000);
    if (result.code == NMO_OK) {
        int32_t mask_face_count;
        result = nmo_chunk_read_int(chunk, &mask_face_count);
        if (result.code == NMO_OK && mask_face_count > 0 && 
            (uint32_t)mask_face_count <= out_state->face_count) {
            
            // Read packed masks (2 faces per DWORD)
            uint32_t pair_count = mask_face_count / 2;
            for (uint32_t i = 0; i < pair_count; i++) {
                uint32_t packed_masks;
                result = nmo_chunk_read_dword(chunk, &packed_masks);
                if (result.code != NMO_OK) break;
                
                nmo_unpack_dword_to_words(packed_masks,
                    &out_state->faces[i * 2].channel_mask,
                    &out_state->faces[i * 2 + 1].channel_mask);
            }
            
            // Odd face
            if (mask_face_count & 1) {
                result = nmo_chunk_read_word(chunk,
                    &out_state->faces[mask_face_count - 1].channel_mask);
            }
        }
    }
    
    // Read progressive mesh (identifier 0x800000, optional)
    result = nmo_chunk_seek_identifier(chunk, 0x800000);
    if (result.code == NMO_OK) {
        out_state->has_progressive_mesh = true;
        
        result = nmo_chunk_read_int(chunk, &out_state->pm_field_0);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->pm_morph_enabled);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->pm_morph_step);
        if (result.code != NMO_OK) return result;
        
        // Read remaining progressive data (size minus 12 bytes for the 3 ints)
        // Note: In real implementation, would need to track chunk size
        // For now, just mark as present
    }
    
    return nmo_result_ok();
}

/**
 * @brief Main deserialization dispatcher
 */
static nmo_result_t nmo_ckmesh_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_mesh_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to deserialize"));
    }
    
    // Get chunk data version
    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    
    if (data_version >= 9) {
        return nmo_ckmesh_deserialize_modern(chunk, arena, out_state);
    } else {
        // Legacy format not implemented yet
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_IMPLEMENTED,
                                          NMO_SEVERITY_ERROR,
                                          "Legacy mesh format (<v9) not implemented"));
    }
}

/* =============================================================================
 * CKMesh SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKMesh state to chunk (placeholder)
 */
static nmo_result_t nmo_ckmesh_serialize(
    const nmo_ck_mesh_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena)
{
    if (!state || !chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to serialize"));
    }
    
    // Not implemented yet
    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_IMPLEMENTED,
                                      NMO_SEVERITY_ERROR,
                                      "CKMesh serialization not implemented"));
}

/* =============================================================================
 * FINISH LOADING
 * ============================================================================= */

/**
 * @brief Finish loading (resolve references, build normals if needed)
 */
static nmo_result_t nmo_ckmesh_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    if (!state || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to finish_loading"));
    }
    
    // Cast state
    nmo_ck_mesh_state_t *mesh_state = (nmo_ck_mesh_state_t *)state;
    
    // Mesh-specific finish loading tasks would go here:
    // - Build face normals if missing
    // - Resolve material group references
    // - Initialize progressive mesh
    // - Create hardware buffers
    
    // Note: CKBeObject doesn't have finish_loading, only CKObject does.
    // Mesh doesn't need to chain to parent finish_loading.
    
    (void)mesh_state;  // Suppress unused warning
    (void)repository;  // Suppress unused warning
    
    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKMesh schema
 */
nmo_result_t nmo_register_ckmesh_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to register_ckmesh_schemas"));
    }
    
    // Get base types
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "float");
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    
    if (float_type == NULL || uint32_type == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    // Register CKMesh state structure
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKMeshState",
                                                      sizeof(nmo_ck_mesh_state_t),
                                                      alignof(nmo_ck_mesh_state_t));
    
    // Add basic fields
    nmo_builder_add_field_ex(&builder, "flags", uint32_type,
                            offsetof(nmo_ck_mesh_state_t, flags),
                            0);
    
    nmo_builder_add_field_ex(&builder, "radius", float_type,
                            offsetof(nmo_ck_mesh_state_t, radius),
                            0);
    
    nmo_builder_add_field_ex(&builder, "vertex_count", uint32_type,
                            offsetof(nmo_ck_mesh_state_t, vertex_count),
                            0);
    
    nmo_builder_add_field_ex(&builder, "face_count", uint32_type,
                            offsetof(nmo_ck_mesh_state_t, face_count),
                            0);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

nmo_ckmesh_deserialize_fn nmo_get_ckmesh_deserialize(void) {
    return nmo_ckmesh_deserialize;
}

nmo_ckmesh_serialize_fn nmo_get_ckmesh_serialize(void) {
    return nmo_ckmesh_serialize;
}

nmo_ckmesh_finish_loading_fn nmo_get_ckmesh_finish_loading(void) {
    return nmo_ckmesh_finish_loading;
}
