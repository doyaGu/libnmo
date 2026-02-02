/**
 * @file ck3dentity_schemas.c
 * @brief CK3dEntity schema definitions
 *
 * Implements schema for CK3dEntity and related 3D entity types.
 * 
 * Based on Virtools SDK reference:
 * - CK3dEntity is the base class for all 3D positioned objects
 * - Stores transformation matrix (position, rotation, scale)
 * - Manages parent-child hierarchy for scene graph
 * - Handles world/local transform computations
 * 
 * Format structure (from reference Load/Save):
 * - World Matrix (VxMatrix 4x4 = 64 bytes)
 * - Flags (DWORD)
 * - Optional: Parent object ID (if parented)
 * - Optional: Z-order data (rendering priority)
 * - Optional: Additional transform data (pivot, bounding box)
 * 
 * This is a PARTIAL schema as mentioned in TODO - we preserve unknown
 * data via raw_tail fields for future math/render schema integration.
 */

#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKSTATECHUNK IDENTIFIERS (CK3dEntity)
 * ============================================================================= */

#define CK_STATESAVE_3DENTITYSKINDATANORMALS 0x00001000u
#define CK_STATESAVE_ANIMATION              0x00002000u
#define CK_STATESAVE_MESHS                  0x00004000u
#define CK_STATESAVE_PARENT                 0x00008000u
#define CK_STATESAVE_3DENTITYFLAGS          0x00010000u
#define CK_STATESAVE_3DENTITYMATRIX         0x00020000u
#define CK_STATESAVE_3DENTITYHIERARCHY      0x00040000u
#define CK_STATESAVE_3DENTITYNDATA          0x00100000u
#define CK_STATESAVE_3DENTITYSKINDATA       0x00200000u

/* =============================================================================
 * CK_3DENTITY FLAGS (subset)
 * ============================================================================= */

#define CK_3DENTITY_PLACEVALID              0x00010000u
#define CK_3DENTITY_PARENTVALID             0x00020000u
#define CK_3DENTITY_BBOXVALID               0x00080000u
#define CK_3DENTITY_ZORDERVALID             0x00100000u
#define CK_3DENTITY_RESERVED0               0x00000020u
#define CK_3DENTITY_UPDATELASTFRAME         0x00001000u
#define CK_3DENTITY_PORTAL                  0x00080000u

/* =============================================================================
 * RAW BUFFER HELPERS
 * ============================================================================= */

static nmo_result_t nmo_ck3dentity_read_raw_bytes(nmo_chunk_t *chunk, void *buffer, size_t bytes) {
    if (!chunk || !buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ck3dentity_read_raw_bytes"));
    }

    size_t dwords = (bytes + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, dwords, "Insufficient data for raw buffer");

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(buffer, &data[state->current_pos], bytes);
    state->current_pos += dwords;

    return nmo_result_ok();
}

/* =============================================================================
 * CK3dEntity DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK3dEntity state from chunk
 * 
 * Reads the 3D entity transformation data and parent references.
 * This is a PARTIAL implementation - some fields are preserved in raw_tail
 * for future schema refinement.
 * 
 * Chunk format (version 7):
 * - DWORD flags (visibility, activity, etc from CKBeObject)
 * - 16 floats: 4x4 world transformation matrix
 * - DWORD entity_flags (local/world transform, etc)
 * - Optional data (preserved as raw_tail):
 *   - Parent object reference
 *   - Z-order/rendering data
 *   - Bounding box
 *   - Pivot point
 * 
 * @param chunk Chunk containing CK3dEntity data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ck3dentity_state_t *out_state = (nmo_ck3dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dEntity deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    // First deserialize parent CKRenderObject data
    nmo_result_t result = nmo_ckrenderobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    // Initialize defaults
    out_state->entity_flags = 0;
    out_state->moveable_flags = 0;
    out_state->parent_id = 0;
    out_state->place_id = 0;
    out_state->z_order = 0;
    out_state->current_mesh_id = 0;

    // Load object animations (identifier CK_STATESAVE_ANIMATION)
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATION).code == NMO_OK) {
        size_t anim_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &anim_count);
        if (result.code == NMO_OK && anim_count > 0) {
            out_state->animation_count = (uint32_t)anim_count;
            out_state->animation_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, sizeof(nmo_object_id_t) * out_state->animation_count,
                _Alignof(nmo_object_id_t));
            if (!out_state->animation_ids) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate animation IDs"));
            }
            for (uint32_t i = 0; i < out_state->animation_count; ++i) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->animation_ids[i]);
            }
        }
    }

    // Load meshes (identifier CK_STATESAVE_MESHS)
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHS).code == NMO_OK) {
        result = nmo_chunk_read_object_id(chunk, &out_state->current_mesh_id);
        if (result.code != NMO_OK) {
            return result;
        }

        size_t mesh_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &mesh_count);
        if (result.code == NMO_OK && mesh_count > 0) {
            out_state->mesh_count = (uint32_t)mesh_count;
            out_state->mesh_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, sizeof(nmo_object_id_t) * out_state->mesh_count,
                _Alignof(nmo_object_id_t));
            if (!out_state->mesh_ids) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate mesh IDs"));
            }
            for (uint32_t i = 0; i < out_state->mesh_count; ++i) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->mesh_ids[i]);
            }
        }
    }

    // Load new-format entity data (identifier CK_STATESAVE_3DENTITYNDATA)
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_3DENTITYNDATA).code == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->entity_flags);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_dword(chunk, &out_state->moveable_flags);
        if (result.code != NMO_OK) return result;

        nmo_vector_t row0, row1, row2, row3;
        result = nmo_chunk_read_vector3(chunk, &row0);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_read_vector3(chunk, &row1);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_read_vector3(chunk, &row2);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_read_vector3(chunk, &row3);
        if (result.code != NMO_OK) return result;

        out_state->world_matrix[0] = row0.x;
        out_state->world_matrix[1] = row0.y;
        out_state->world_matrix[2] = row0.z;
        out_state->world_matrix[3] = 0.0f;
        out_state->world_matrix[4] = row1.x;
        out_state->world_matrix[5] = row1.y;
        out_state->world_matrix[6] = row1.z;
        out_state->world_matrix[7] = 0.0f;
        out_state->world_matrix[8] = row2.x;
        out_state->world_matrix[9] = row2.y;
        out_state->world_matrix[10] = row2.z;
        out_state->world_matrix[11] = 0.0f;
        out_state->world_matrix[12] = row3.x;
        out_state->world_matrix[13] = row3.y;
        out_state->world_matrix[14] = row3.z;
        out_state->world_matrix[15] = 1.0f;

        if (out_state->entity_flags & CK_3DENTITY_PLACEVALID) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->place_id);
        }

        if (out_state->entity_flags & CK_3DENTITY_PARENTVALID) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->parent_id);
        }

        if (out_state->entity_flags & CK_3DENTITY_ZORDERVALID) {
            (void)nmo_chunk_read_int(chunk, &out_state->z_order);
        }
    }

    // Legacy parent chunk
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARENT).code == NMO_OK) {
        (void)nmo_chunk_read_object_id(chunk, &out_state->parent_id);
    }

    // Legacy flags chunk
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_3DENTITYFLAGS).code == NMO_OK) {
        (void)nmo_chunk_read_dword(chunk, &out_state->entity_flags);
        (void)nmo_chunk_read_dword(chunk, &out_state->moveable_flags);
    }

    // Legacy matrix chunk
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_3DENTITYMATRIX).code == NMO_OK) {
        (void)nmo_chunk_skip(chunk, 1);
        nmo_matrix_t mat;
        result = nmo_chunk_read_matrix(chunk, &mat);
        if (result.code == NMO_OK) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out_state->world_matrix[r * 4 + c] = mat.m[r][c];
                }
            }
        }
    }

    // Skin data (identifier CK_STATESAVE_3DENTITYSKINDATA)
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_3DENTITYSKINDATA).code == NMO_OK) {
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        out_state->skin = (nmo_ck3dentity_skin_t *)nmo_arena_alloc(
            arena, sizeof(nmo_ck3dentity_skin_t), _Alignof(nmo_ck3dentity_skin_t));
        if (!out_state->skin) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                NMO_SEVERITY_ERROR, "Failed to allocate skin data"));
        }
        memset(out_state->skin, 0, sizeof(*out_state->skin));

        if (data_version < 6) {
            (void)nmo_chunk_skip(chunk, 1);
        }

        result = nmo_chunk_read_matrix(chunk, &out_state->skin->object_init_matrix);
        if (result.code != NMO_OK) return result;

        size_t bone_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &bone_count);
        if (result.code != NMO_OK) return result;

        out_state->skin->bone_count = (uint32_t)bone_count;
        if (bone_count > 0) {
            out_state->skin->bones = (nmo_ck3dentity_skin_bone_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ck3dentity_skin_bone_t) * bone_count,
                _Alignof(nmo_ck3dentity_skin_bone_t));
            if (!out_state->skin->bones) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate skin bones"));
            }
            memset(out_state->skin->bones, 0, sizeof(nmo_ck3dentity_skin_bone_t) * bone_count);
        }

        for (uint32_t i = 0; i < out_state->skin->bone_count; ++i) {
            (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->skin->bones[i].bone_id);
        }

        for (uint32_t i = 0; i < out_state->skin->bone_count; ++i) {
            (void)nmo_chunk_read_dword(chunk, &out_state->skin->bones[i].bone_flags);
            if (data_version < 6) {
                (void)nmo_chunk_skip(chunk, 1);
            }
            (void)nmo_chunk_read_matrix(chunk, &out_state->skin->bones[i].inverse_bind_matrix);
        }

        int32_t vertex_count = 0;
        result = nmo_chunk_read_int(chunk, &vertex_count);
        if (result.code != NMO_OK) return result;

        if (vertex_count > 0) {
            out_state->skin->vertex_count = (uint32_t)vertex_count;
            out_state->skin->vertices = (nmo_ck3dentity_skin_vertex_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ck3dentity_skin_vertex_t) * out_state->skin->vertex_count,
                _Alignof(nmo_ck3dentity_skin_vertex_t));
            if (!out_state->skin->vertices) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate skin vertices"));
            }
            memset(out_state->skin->vertices, 0,
                   sizeof(nmo_ck3dentity_skin_vertex_t) * out_state->skin->vertex_count);
        }

        for (uint32_t i = 0; i < out_state->skin->vertex_count; ++i) {
            nmo_ck3dentity_skin_vertex_t *vertex = &out_state->skin->vertices[i];
            int32_t bone_count_i = 0;
            result = nmo_chunk_read_int(chunk, &bone_count_i);
            if (result.code != NMO_OK) return result;
            vertex->bone_count = (uint32_t)bone_count_i;

            if (data_version < 6) {
                (void)nmo_chunk_skip(chunk, 1);
            }

            (void)nmo_chunk_read_vector3(chunk, &vertex->initial_pos);

            if (data_version < 6) {
                (void)nmo_chunk_skip(chunk, 1);
            }

            if (vertex->bone_count > 0) {
                vertex->bone_indices = (uint32_t *)nmo_arena_alloc(
                    arena, sizeof(uint32_t) * vertex->bone_count, _Alignof(uint32_t));
                if (!vertex->bone_indices) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate bone indices"));
                }
                result = nmo_ck3dentity_read_raw_bytes(chunk,
                    vertex->bone_indices, sizeof(uint32_t) * vertex->bone_count);
                if (result.code != NMO_OK) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                        NMO_SEVERITY_ERROR, "Failed to read bone indices"));
                }
            }

            if (data_version < 6) {
                (void)nmo_chunk_skip(chunk, 1);
            }

            if (vertex->bone_count > 0) {
                vertex->bone_weights = (float *)nmo_arena_alloc(
                    arena, sizeof(float) * vertex->bone_count, _Alignof(float));
                if (!vertex->bone_weights) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate bone weights"));
                }
                result = nmo_ck3dentity_read_raw_bytes(chunk,
                    vertex->bone_weights, sizeof(float) * vertex->bone_count);
                if (result.code != NMO_OK) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                        NMO_SEVERITY_ERROR, "Failed to read bone weights"));
                }
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_3DENTITYSKINDATANORMALS).code == NMO_OK) {
            size_t total_bytes = nmo_chunk_get_data_size(chunk);
            size_t current_bytes = nmo_chunk_get_position(chunk) * 4u;
            size_t remaining_bytes = (total_bytes > current_bytes) ? (total_bytes - current_bytes) : 0u;
            uint32_t expected_bytes = out_state->skin->vertex_count * (uint32_t)sizeof(nmo_vector_t);

            if (remaining_bytes >= expected_bytes + sizeof(uint32_t)) {
                uint32_t normal_count = 0;
                (void)nmo_chunk_read_dword(chunk, &normal_count);
                (void)normal_count;
                remaining_bytes -= sizeof(uint32_t);
            }

            out_state->skin->normal_count = out_state->skin->vertex_count;
            if (expected_bytes > 0) {
                out_state->skin->normals = (nmo_vector_t *)nmo_arena_alloc(
                    arena, expected_bytes, _Alignof(nmo_vector_t));
                if (!out_state->skin->normals) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate skin normals"));
                }
                result = nmo_ck3dentity_read_raw_bytes(chunk,
                    out_state->skin->normals, expected_bytes);
                if (result.code != NMO_OK) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                        NMO_SEVERITY_ERROR, "Failed to read skin normals"));
                }
            }
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CK3dEntity SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK3dEntity state to chunk
 * 
 * Writes the 3D entity transformation data in the original format.
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ck3dentity_state_t *in_state = (const nmo_ck3dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dEntity serialize"));
    }

    // First serialize parent CKRenderObject data
    nmo_result_t result = nmo_ckrenderobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    // Save mesh list
    if (in_state->current_mesh_id || (in_state->mesh_count > 0 && in_state->mesh_ids)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHS);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_object_id(out_chunk, in_state->current_mesh_id);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->mesh_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->mesh_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->mesh_ids[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    // Save animations list
    if (in_state->animation_count > 0 && in_state->animation_ids) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATION);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->animation_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->animation_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->animation_ids[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    // Save main entity data
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYNDATA);
    if (result.code != NMO_OK) return result;

    uint32_t flags = in_state->entity_flags;
    if (in_state->place_id) {
        flags |= CK_3DENTITY_PLACEVALID;
    } else {
        flags &= ~CK_3DENTITY_PLACEVALID;
    }
    if (in_state->parent_id) {
        flags |= CK_3DENTITY_PARENTVALID;
    } else {
        flags &= ~CK_3DENTITY_PARENTVALID;
    }
    if (in_state->z_order != 0) {
        flags |= CK_3DENTITY_ZORDERVALID;
    } else {
        flags &= ~CK_3DENTITY_ZORDERVALID;
    }

    result = nmo_chunk_write_dword(out_chunk, flags);
    if (result.code != NMO_OK) return result;
    result = nmo_chunk_write_dword(out_chunk, in_state->moveable_flags);
    if (result.code != NMO_OK) return result;

    nmo_vector_t row0 = { in_state->world_matrix[0], in_state->world_matrix[1], in_state->world_matrix[2] };
    nmo_vector_t row1 = { in_state->world_matrix[4], in_state->world_matrix[5], in_state->world_matrix[6] };
    nmo_vector_t row2 = { in_state->world_matrix[8], in_state->world_matrix[9], in_state->world_matrix[10] };
    nmo_vector_t row3 = { in_state->world_matrix[12], in_state->world_matrix[13], in_state->world_matrix[14] };

    result = nmo_chunk_write_vector3(out_chunk, &row0);
    if (result.code != NMO_OK) return result;
    result = nmo_chunk_write_vector3(out_chunk, &row1);
    if (result.code != NMO_OK) return result;
    result = nmo_chunk_write_vector3(out_chunk, &row2);
    if (result.code != NMO_OK) return result;
    result = nmo_chunk_write_vector3(out_chunk, &row3);
    if (result.code != NMO_OK) return result;

    if (in_state->place_id) {
        result = nmo_chunk_write_object_id(out_chunk, in_state->place_id);
        if (result.code != NMO_OK) return result;
    }
    if (in_state->parent_id) {
        result = nmo_chunk_write_object_id(out_chunk, in_state->parent_id);
        if (result.code != NMO_OK) return result;
    }
    if (in_state->z_order != 0) {
        result = nmo_chunk_write_int(out_chunk, in_state->z_order);
        if (result.code != NMO_OK) return result;
    }

    // Save skin data
    if (in_state->skin) {
        const nmo_ck3dentity_skin_t *skin = in_state->skin;
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYSKINDATA);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_matrix(out_chunk, &skin->object_init_matrix);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, skin->bone_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < skin->bone_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, skin->bones[i].bone_id);
            if (result.code != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < skin->bone_count; ++i) {
            result = nmo_chunk_write_dword(out_chunk, skin->bones[i].bone_flags);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_matrix(out_chunk, &skin->bones[i].inverse_bind_matrix);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_int(out_chunk, (int32_t)skin->vertex_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < skin->vertex_count; ++i) {
            const nmo_ck3dentity_skin_vertex_t *vertex = &skin->vertices[i];
            result = nmo_chunk_write_int(out_chunk, (int32_t)vertex->bone_count);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_vector3(out_chunk, &vertex->initial_pos);
            if (result.code != NMO_OK) return result;

            if (vertex->bone_count > 0 && vertex->bone_indices && vertex->bone_weights) {
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                        vertex->bone_indices,
                                                        sizeof(uint32_t) * vertex->bone_count);
                if (result.code != NMO_OK) return result;
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                        vertex->bone_weights,
                                                        sizeof(float) * vertex->bone_count);
                if (result.code != NMO_OK) return result;
            }
        }

        if (skin->normal_count > 0 && skin->normals) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYSKINDATANORMALS);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_write_int(out_chunk, (int32_t)skin->normal_count);
            if (result.code != NMO_OK) return result;
            for (uint32_t i = 0; i < skin->normal_count; ++i) {
                result = nmo_chunk_write_vector3(out_chunk, &skin->normals[i]);
                if (result.code != NMO_OK) return result;
            }
        }
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ck3dentity,
    nmo_ck3dentity_state_t,
    nmo_ck3dentity_serialize,
    nmo_ck3dentity_deserialize,
    NMO_GUID_CK3DENTITY,
    "CK3dEntity",
    NMO_CID_3DENTITY,
    NMO_GUID_CKRENDEROBJECT
)


/**
 * @brief Finish loading CK3dEntity
 * 
 * Performs reference resolution and runtime initialization.
 * Base implementation for 3D entities - derived classes may extend.
 * 
 * @param state Entity state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dentity_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    /* Base implementation does nothing special beyond RenderObject */
    (void)instance;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}

