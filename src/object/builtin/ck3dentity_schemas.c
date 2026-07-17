/**
 * @file ck3dentity_schemas.c
 * @brief CK3dEntity schema definitions
 *
 * Implements schema for CK3dEntity and related 3D entity types.
 * Verified complete against RenderEngine reference (CK3dEntity Load/Save).
 *
 * Serialized chunk identifiers (all optional via SeekIdentifier):
 *   CK_STATESAVE_ANIMATION      (0x2000)   -- object animation ID array
 *   CK_STATESAVE_MESHS           (0x4000)   -- current mesh + mesh ID array
 *   CK_STATESAVE_3DENTITYNDATA   (0x100000) -- flags, moveable_flags,
 *       4x VxVector row (3x3 rotation + translation), optional place/parent/z_order
 *   CK_STATESAVE_PARENT          (0x8000)   -- legacy parent reference
 *   CK_STATESAVE_3DENTITYFLAGS   (0x10000)  -- legacy entity + moveable flags
 *   CK_STATESAVE_3DENTITYMATRIX  (0x20000)  -- legacy full 4x4 matrix
 *   CK_STATESAVE_3DENTITYSKINDATA (0x200000) -- skin bones/vertices/normals
 *
 * Note: Bounding box and pivot are NOT serialized fields.  They are
 * runtime-computed in the Virtools engine (bbox from mesh, pivot from
 * world matrix row 3).
 */

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include <stdint.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(3dentity, nmo_3dentity_state_t)

static void nmo_3dentity_dispose_base_arrays(nmo_3dentity_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_3dentity_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_3dentity_state_t, base),
                    sizeof(nmo_renderobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    /* Transform */
    NMO_FIELD_NAMED("world_matrix", offsetof(nmo_3dentity_state_t, world_matrix),
                    sizeof(float) * 16, CKPGUID_MATRIX,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_3dentity_state_t, entity_flags, NMO_GUID_ENUM_CK_3DENTITY_FLAGS),
    NMO_FIELD(nmo_3dentity_state_t, moveable_flags, NMO_GUID_ENUM_VX_MOVEABLE_FLAGS),
    /* Hierarchy */
    NMO_FIELD_REF(nmo_3dentity_state_t, parent),
    NMO_FIELD_REF(nmo_3dentity_state_t, place),
    NMO_FIELD(nmo_3dentity_state_t, z_order, CKPGUID_INT),
    /* Meshes */
    NMO_FIELD_REF(nmo_3dentity_state_t, current_mesh),
    NMO_FIELD(nmo_3dentity_state_t, mesh_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_3dentity_state_t, mesh_ids, mesh_count),
    /* Animations */
    NMO_FIELD(nmo_3dentity_state_t, animation_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_3dentity_state_t, animation_ids, animation_count),
    /* Skin (optional) */
    NMO_FIELD_OPT(nmo_3dentity_state_t, skin, CKPGUID_POINTER)
};

/* =============================================================================
 * CK_3DENTITY FLAGS (subset)
 *
 * Runtime flag sanitization (reference, NOT applied here -- raw values kept
 * for round-trip fidelity):
 *   entity_flags:   reference clears RESERVED0 (0x20) and
 *                   UPDATELASTFRAME (0x1000) during new-format load.
 *   moveable_flags (new format): reference clears UPTODATE | USERBOX |
 *                   BOXVALID | HASMOVED | INVERSEWORLDMATVALID |
 *                   DONTUPDATEFROMPARENT | STENCILONLY | RESERVED2.
 *   moveable_flags (legacy):     reference clears UPTODATE | USERBOX |
 *                   INVERSEWORLDMATVALID | DONTUPDATEFROMPARENT | 0xFF00.
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

static nmo_status_t nmo_3dentity_read_raw_bytes(nmo_chunk_t *chunk, void *buffer, size_t bytes) {
    if (!chunk || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_3dentity_read_raw_bytes");
    }

    size_t dwords = (bytes + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, dwords, "Insufficient data for raw buffer");

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(buffer, &data[state->current_pos], bytes);
    state->current_pos += dwords;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_3dentity_identifier_payload_size_bytes(nmo_chunk_t *chunk, size_t *out_size) {
    if (chunk == NULL || out_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to identifier payload size helper");
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    if (state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Chunk parser state not initialized");
    }

    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    const size_t data_count = chunk->data.count;
    const size_t start_pos = state->current_pos;
    size_t end_pos = data_count;

    if (state->prev_identifier_pos + 1 < data_count) {
        const uint32_t next_pos = data[state->prev_identifier_pos + 1];
        if (next_pos != 0 && next_pos < data_count) {
            end_pos = next_pos;
        }
    }

    if (end_pos < start_pos) {
        end_pos = start_pos;
    }

    *out_size = (end_pos - start_pos) * sizeof(uint32_t);
    return NMO_OK;
}

/* =============================================================================
 * CK3dEntity DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK3dEntity state from chunk
 *
 * Reads all serialized CK3dEntity data.  Each section is gated by a
 * chunk identifier (SeekIdentifier); missing sections are normal.
 *
 * CK_STATESAVE_3DENTITYNDATA layout:
 *   DWORD  entity_flags
 *   DWORD  moveable_flags
 *   4x VxVector  world matrix rows (3 floats each; [3]=0,0,0,1)
 *   [if PLACEVALID]  object_id  place
 *   [if PARENTVALID] object_id  parent
 *   [if ZORDERVALID] int32      z_order
 */
static nmo_status_t nmo_3dentity_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_3dentity_state_t *out_state = (nmo_3dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CK3dEntity deserialize");
    }

    // First deserialize parent CKRenderObject data
    nmo_status_t result = nmo_renderobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->entity_flags = 0;
    out_state->moveable_flags = 0;
    out_state->parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->place = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->z_order = 0;
    out_state->current_mesh = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->mesh_count = 0;
    out_state->mesh_ids = NULL;
    out_state->animation_count = 0;
    out_state->animation_ids = NULL;
    out_state->skin = NULL;
    out_state->has_mesh_chunk = 0;
    out_state->has_animation_chunk = 0;
    out_state->has_entityndata_chunk = 0;
    out_state->has_parent_chunk = 0;
    out_state->has_flags_chunk = 0;
    out_state->has_matrix_chunk = 0;

    for (int i = 0; i < 16; ++i) {
        out_state->world_matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    // Load object animations (identifier CK_STATESAVE_ANIMATION)
    nmo_status_t seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_ANIMATION);
    if (seek_result == NMO_OK) {
        size_t anim_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &anim_count);
        if (result != NMO_OK) return result;
        if (anim_count > UINT32_MAX ||
            anim_count > SIZE_MAX / sizeof(nmo_ref_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Animation count exceeds limits");
        }
        size_t remaining_bytes = 0;
        NMO_RETURN_IF_ERROR(nmo_3dentity_identifier_payload_size_bytes(
            chunk, &remaining_bytes));
        if (anim_count > remaining_bytes / sizeof(uint32_t)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Animation count exceeds identifier payload");
        }
        nmo_ref_t *animation_ids = NULL;
        if (anim_count > 0) {
            animation_ids = (nmo_ref_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ref_t) * anim_count,
                _Alignof(nmo_ref_t));
            if (!animation_ids) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate animation IDs");
            }
            for (size_t i = 0; i < anim_count; ++i) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &animation_ids[i]));
                nmo_ref_check_class(
                    &animation_ids[i],
                    (const nmo_object_repository_t *)
                        nmo_deserialize_context_get_repository(context),
                    nmo_deserialize_context_get_type_registry(context),
                    NMO_CID_OBJECTANIMATION);
            }
        }
        out_state->animation_count = (uint32_t)anim_count;
        out_state->animation_ids = animation_ids;
        out_state->has_animation_chunk = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    // Load meshes (identifier CK_STATESAVE_MESHS)
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESHS);
    if (seek_result == NMO_OK) {
        nmo_ref_t current_mesh = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &current_mesh);
        if (result != NMO_OK) {
            return result;
        }

        size_t mesh_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &mesh_count);
        if (result != NMO_OK) return result;
        if (mesh_count > UINT32_MAX ||
            mesh_count > SIZE_MAX / sizeof(nmo_ref_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Mesh count exceeds limits");
        }
        size_t remaining_bytes = 0;
        NMO_RETURN_IF_ERROR(nmo_3dentity_identifier_payload_size_bytes(
            chunk, &remaining_bytes));
        if (mesh_count > remaining_bytes / sizeof(uint32_t)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Mesh count exceeds identifier payload");
        }
        nmo_ref_t *mesh_ids = NULL;
        if (mesh_count > 0) {
            mesh_ids = (nmo_ref_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ref_t) * mesh_count,
                _Alignof(nmo_ref_t));
            if (!mesh_ids) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate mesh IDs");
            }
            for (size_t i = 0; i < mesh_count; ++i) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &mesh_ids[i]));
                nmo_ref_check_class(
                    &mesh_ids[i],
                    (const nmo_object_repository_t *)
                        nmo_deserialize_context_get_repository(context),
                    nmo_deserialize_context_get_type_registry(context),
                    NMO_CID_MESH);
            }
        }
        nmo_ref_check_class(
            &current_mesh,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_MESH);
        out_state->current_mesh = current_mesh;
        out_state->mesh_count = (uint32_t)mesh_count;
        out_state->mesh_ids = mesh_ids;
        out_state->has_mesh_chunk = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    // Load new-format entity data (identifier CK_STATESAVE_3DENTITYNDATA)
    seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_3DENTITYNDATA);
    if (seek_result == NMO_OK) {
        nmo_3dentity_state_t data = *out_state;
        data.has_entityndata_chunk = 1;
        result = nmo_chunk_read_dword(chunk, &data.entity_flags);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_dword(chunk, &data.moveable_flags);
        if (result != NMO_OK) return result;

        nmo_vector_t row0, row1, row2, row3;
        result = nmo_chunk_read_vector3(chunk, &row0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_vector3(chunk, &row1);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_vector3(chunk, &row2);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_vector3(chunk, &row3);
        if (result != NMO_OK) return result;

        data.world_matrix[0] = row0.x;
        data.world_matrix[1] = row0.y;
        data.world_matrix[2] = row0.z;
        data.world_matrix[3] = 0.0f;
        data.world_matrix[4] = row1.x;
        data.world_matrix[5] = row1.y;
        data.world_matrix[6] = row1.z;
        data.world_matrix[7] = 0.0f;
        data.world_matrix[8] = row2.x;
        data.world_matrix[9] = row2.y;
        data.world_matrix[10] = row2.z;
        data.world_matrix[11] = 0.0f;
        data.world_matrix[12] = row3.x;
        data.world_matrix[13] = row3.y;
        data.world_matrix[14] = row3.z;
        data.world_matrix[15] = 1.0f;

        if (data.entity_flags & CK_3DENTITY_PLACEVALID) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &data.place));
        } else {
            data.place = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        }

        if (data.entity_flags & CK_3DENTITY_PARENTVALID) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &data.parent));
        } else {
            data.parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        }

        if (data.entity_flags & CK_3DENTITY_ZORDERVALID) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &data.z_order));
        } else {
            data.z_order = 0;
        }
        nmo_ref_check_class(
            &data.place,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_PLACE);
        nmo_ref_check_class(
            &data.parent,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_3DENTITY);
        *out_state = data;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    // Legacy parent chunk
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARENT);
    if (seek_result == NMO_OK) {
        nmo_ref_t parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &parent));
        nmo_ref_check_class(
            &parent,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_3DENTITY);
        out_state->parent = parent;
        out_state->has_parent_chunk = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    // Legacy flags chunk
    seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_3DENTITYFLAGS);
    if (seek_result == NMO_OK) {
        out_state->has_flags_chunk = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->entity_flags));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->moveable_flags));
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    // Legacy matrix chunk
    seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_3DENTITYMATRIX);
    if (seek_result == NMO_OK) {
        out_state->has_matrix_chunk = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 1));
        nmo_matrix_t mat;
        result = nmo_chunk_read_matrix(chunk, &mat);
        if (result != NMO_OK) return result;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out_state->world_matrix[r * 4 + c] = mat.m[r][c];
            }
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    // Skin data (identifier CK_STATESAVE_3DENTITYSKINDATA)
    seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_3DENTITYSKINDATA);
    if (seek_result == NMO_OK) {
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        out_state->skin = (nmo_3dentity_skin_t *)nmo_arena_alloc(
            arena, sizeof(nmo_3dentity_skin_t), _Alignof(nmo_3dentity_skin_t));
        if (!out_state->skin) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate skin data");
        }
        memset(out_state->skin, 0, sizeof(*out_state->skin));

        if (data_version < 6) {
            NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 1));
        }

        result = nmo_chunk_read_matrix(chunk, &out_state->skin->object_init_matrix);
        if (result != NMO_OK) return result;

        size_t bone_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &bone_count);
        if (result != NMO_OK) return result;
        if (bone_count > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Bone count exceeds limits");
        }
        if (bone_count > SIZE_MAX / sizeof(nmo_3dentity_skin_bone_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Bone array size exceeds limits");
        }

        out_state->skin->bone_count = (uint32_t)bone_count;
        if (bone_count > 0) {
            out_state->skin->bones = (nmo_3dentity_skin_bone_t *)nmo_arena_alloc(
                arena, sizeof(nmo_3dentity_skin_bone_t) * bone_count,
                _Alignof(nmo_3dentity_skin_bone_t));
            if (!out_state->skin->bones) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate skin bones");
            }
            memset(out_state->skin->bones, 0, sizeof(nmo_3dentity_skin_bone_t) * bone_count);
        }

        for (uint32_t i = 0; i < out_state->skin->bone_count; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_item(chunk, &out_state->skin->bones[i].bone_id));
        }

        for (uint32_t i = 0; i < out_state->skin->bone_count; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->skin->bones[i].bone_flags));
            if (data_version < 6) {
                NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 1));
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_read_matrix(chunk, &out_state->skin->bones[i].inverse_bind_matrix));
        }

        int32_t vertex_count = 0;
        result = nmo_chunk_read_int(chunk, &vertex_count);
        if (result != NMO_OK) return result;
        if (vertex_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid vertex count in skin data");
        }

        if (vertex_count > 0) {
            out_state->skin->vertex_count = (uint32_t)vertex_count;
            out_state->skin->vertices = (nmo_3dentity_skin_vertex_t *)nmo_arena_alloc(
                arena, sizeof(nmo_3dentity_skin_vertex_t) * out_state->skin->vertex_count,
                _Alignof(nmo_3dentity_skin_vertex_t));
            if (!out_state->skin->vertices) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate skin vertices");
            }
            memset(out_state->skin->vertices, 0,
                   sizeof(nmo_3dentity_skin_vertex_t) * out_state->skin->vertex_count);
        }

        for (uint32_t i = 0; i < out_state->skin->vertex_count; ++i) {
            nmo_3dentity_skin_vertex_t *vertex = &out_state->skin->vertices[i];
            int32_t bone_count_i = 0;
            result = nmo_chunk_read_int(chunk, &bone_count_i);
            if (result != NMO_OK) return result;
            if (bone_count_i < 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Invalid per-vertex bone count");
            }
            vertex->bone_count = (uint32_t)bone_count_i;

            if (data_version < 6) {
                NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 1));
            }

            NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &vertex->initial_pos));

            if (data_version < 6) {
                NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 1));
            }

            if (vertex->bone_count > 0) {
                vertex->bone_indices = (uint32_t *)nmo_arena_alloc(
                    arena, sizeof(uint32_t) * vertex->bone_count, _Alignof(uint32_t));
                if (!vertex->bone_indices) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate bone indices");
                }
                result = nmo_3dentity_read_raw_bytes(chunk,
                    vertex->bone_indices, sizeof(uint32_t) * vertex->bone_count);
                if (result != NMO_OK) {
                    return result;
                }
            }

            if (data_version < 6) {
                NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 1));
            }

            if (vertex->bone_count > 0) {
                vertex->bone_weights = (float *)nmo_arena_alloc(
                    arena, sizeof(float) * vertex->bone_count, _Alignof(float));
                if (!vertex->bone_weights) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate bone weights");
                }
                result = nmo_3dentity_read_raw_bytes(chunk,
                    vertex->bone_weights, sizeof(float) * vertex->bone_count);
                if (result != NMO_OK) {
                    return result;
                }
            }
        }

        seek_result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_3DENTITYSKINDATANORMALS);
        if (seek_result == NMO_OK) {
            out_state->skin->normals_present = 1;
            size_t payload_bytes = 0;
            result = nmo_3dentity_identifier_payload_size_bytes(chunk, &payload_bytes);
            if (result != NMO_OK) return result;

            const uint32_t vertex_count_u = out_state->skin->vertex_count;
            const size_t expected_bytes = (size_t)vertex_count_u * sizeof(nmo_vector_t);
            uint32_t normal_count = vertex_count_u;

            if (payload_bytes == expected_bytes + sizeof(uint32_t)) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &normal_count));
                out_state->skin->normals_have_count = 1;
            } else if (payload_bytes == expected_bytes) {
                out_state->skin->normals_have_count = 0;
            } else {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Unexpected skin normals payload size");
            }

            if (normal_count != vertex_count_u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Skin normal count mismatch");
            }

            out_state->skin->normal_count = normal_count;
            if (expected_bytes > 0 && normal_count > 0) {
                out_state->skin->normals = (nmo_vector_t *)nmo_arena_alloc(
                    arena, expected_bytes, _Alignof(nmo_vector_t));
                if (!out_state->skin->normals) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate skin normals");
                }
                result = nmo_3dentity_read_raw_bytes(chunk,
                    out_state->skin->normals, expected_bytes);
                if (result != NMO_OK) {
                    return result;
                }
            }
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_3dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_3dentity_state_t *out_state = (nmo_3dentity_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_3dentity_state_t decoded = {0};
    nmo_status_t result = nmo_3dentity_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_3dentity_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_3dentity_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
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
static nmo_status_t nmo_3dentity_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_3dentity_state_t *in_state = (const nmo_3dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CK3dEntity serialize");
    }

    // First serialize parent CKRenderObject data
    nmo_status_t result = nmo_renderobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_3DENTITYONLY) == 0) {
            return NMO_OK;
        }
    }

    const bool want_mesh_chunk = in_state->has_mesh_chunk ||
        in_state->current_mesh.state != NMO_REF_NONE ||
        (in_state->mesh_count > 0 && in_state->mesh_ids);
    if (want_mesh_chunk) {
        if (in_state->mesh_count > 0 && in_state->mesh_ids == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Missing mesh IDs for CK3dEntity");
        }
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_MESHS);
        if (result != NMO_OK) return result;

        result = nmo_ref_write(out_chunk, &in_state->current_mesh);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->mesh_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->mesh_count; ++i) {
            result = nmo_ref_write_sequence_item(out_chunk, &in_state->mesh_ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    const bool want_anim_chunk = in_state->has_animation_chunk ||
        (in_state->animation_count > 0 && in_state->animation_ids);
    if (want_anim_chunk) {
        if (in_state->animation_count > 0 && in_state->animation_ids == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Missing animation IDs for CK3dEntity");
        }
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATION);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->animation_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->animation_count; ++i) {
            result = nmo_ref_write_sequence_item(out_chunk, &in_state->animation_ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    const bool has_legacy_chunks = in_state->has_parent_chunk ||
        in_state->has_flags_chunk || in_state->has_matrix_chunk;
    const bool write_entityndata = in_state->has_entityndata_chunk || !has_legacy_chunks;
    if (write_entityndata) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYNDATA);
        if (result != NMO_OK) return result;

        uint32_t flags = in_state->entity_flags;
        if (in_state->place.state != NMO_REF_NONE) {
            flags |= CK_3DENTITY_PLACEVALID;
        } else {
            flags &= ~CK_3DENTITY_PLACEVALID;
        }
        if (in_state->parent.state != NMO_REF_NONE) {
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
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->moveable_flags);
        if (result != NMO_OK) return result;

        nmo_vector_t row0 = { in_state->world_matrix[0], in_state->world_matrix[1], in_state->world_matrix[2] };
        nmo_vector_t row1 = { in_state->world_matrix[4], in_state->world_matrix[5], in_state->world_matrix[6] };
        nmo_vector_t row2 = { in_state->world_matrix[8], in_state->world_matrix[9], in_state->world_matrix[10] };
        nmo_vector_t row3 = { in_state->world_matrix[12], in_state->world_matrix[13], in_state->world_matrix[14] };

        result = nmo_chunk_write_vector3(out_chunk, &row0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &row1);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &row2);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &row3);
        if (result != NMO_OK) return result;

        if (flags & CK_3DENTITY_PLACEVALID) {
            result = nmo_ref_write(out_chunk, &in_state->place);
            if (result != NMO_OK) return result;
        }
        if (flags & CK_3DENTITY_PARENTVALID) {
            result = nmo_ref_write(out_chunk, &in_state->parent);
            if (result != NMO_OK) return result;
        }
        if (flags & CK_3DENTITY_ZORDERVALID) {
            result = nmo_chunk_write_int(out_chunk, in_state->z_order);
            if (result != NMO_OK) return result;
        }
    }

    if (in_state->has_parent_chunk) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARENT);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->parent);
        if (result != NMO_OK) return result;
    }

    if (in_state->has_flags_chunk) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYFLAGS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->entity_flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->moveable_flags);
        if (result != NMO_OK) return result;
    }

    if (in_state->has_matrix_chunk) {
        nmo_matrix_t mat;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                mat.m[r][c] = in_state->world_matrix[r * 4 + c];
            }
        }

        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYMATRIX);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, 0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_matrix(out_chunk, &mat);
        if (result != NMO_OK) return result;
    }

    // Save skin data
    if (in_state->skin) {
        const nmo_3dentity_skin_t *skin = in_state->skin;
        const uint32_t data_version = nmo_chunk_get_data_version(out_chunk);
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYSKINDATA);
        if (result != NMO_OK) return result;

        if (data_version < 6) {
            result = nmo_chunk_write_dword(out_chunk, 0);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_matrix(out_chunk, &skin->object_init_matrix);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_object_sequence_start(out_chunk, skin->bone_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < skin->bone_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, skin->bones[i].bone_id);
            if (result != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < skin->bone_count; ++i) {
            result = nmo_chunk_write_dword(out_chunk, skin->bones[i].bone_flags);
            if (result != NMO_OK) return result;
            if (data_version < 6) {
                result = nmo_chunk_write_dword(out_chunk, 0);
                if (result != NMO_OK) return result;
            }
            result = nmo_chunk_write_matrix(out_chunk, &skin->bones[i].inverse_bind_matrix);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_int(out_chunk, (int32_t)skin->vertex_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < skin->vertex_count; ++i) {
            const nmo_3dentity_skin_vertex_t *vertex = &skin->vertices[i];
            result = nmo_chunk_write_int(out_chunk, (int32_t)vertex->bone_count);
            if (result != NMO_OK) return result;

            if (data_version < 6) {
                result = nmo_chunk_write_dword(out_chunk, 0);
                if (result != NMO_OK) return result;
            }

            result = nmo_chunk_write_vector3(out_chunk, &vertex->initial_pos);
            if (result != NMO_OK) return result;

            if (data_version < 6) {
                result = nmo_chunk_write_dword(out_chunk, 0);
                if (result != NMO_OK) return result;
            }

            if (vertex->bone_count > 0 && vertex->bone_indices && vertex->bone_weights) {
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                        vertex->bone_indices,
                                                        sizeof(uint32_t) * vertex->bone_count);
                if (result != NMO_OK) return result;
            } else if (vertex->bone_count > 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Missing skin bone data for vertex");
            }

            if (data_version < 6) {
                result = nmo_chunk_write_dword(out_chunk, 0);
                if (result != NMO_OK) return result;
            }

            if (vertex->bone_count > 0) {
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                        vertex->bone_weights,
                                                        sizeof(float) * vertex->bone_count);
                if (result != NMO_OK) return result;
            }
        }

        const bool want_normals_chunk = skin->normals_present ||
            (skin->normal_count > 0 && skin->normals);
        if (want_normals_chunk) {
            if (skin->normal_count != skin->vertex_count) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Skin normal count mismatch");
            }
            if (skin->normal_count > 0 && skin->normals == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Missing skin normals data");
            }
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_3DENTITYSKINDATANORMALS);
            if (result != NMO_OK) return result;

            if (skin->normals_have_count) {
                result = nmo_chunk_write_int(out_chunk, (int32_t)skin->normal_count);
                if (result != NMO_OK) return result;
            }

            if (skin->normal_count > 0) {
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                        skin->normals,
                                                        (size_t)skin->normal_count * sizeof(nmo_vector_t));
                if (result != NMO_OK) return result;
            }
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_3dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_3dentity_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_3dentity_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_3dentity_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_3dentity_remap_dependencies");
    }

    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)instance;

    if (state->mesh_count > 0 && state->mesh_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "CK3dEntity: mesh_ids missing");
    }
    if (state->animation_count > 0 && state->animation_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "CK3dEntity: animation_ids missing");
    }

    /* Dependency remapping is observational. Invalid and duplicate references
     * remain in serialized state so a later save can preserve their raw IDs.
     * Destructive cleanup is available only through explicit normalization. */
    NMO_RETURN_OK();
}

static nmo_status_t nmo_3dentity_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_3dentity_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_3dentity_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static bool nmo_3dentity_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_3dentity_state_t *lhs = (const nmo_3dentity_state_t *)a;
    const nmo_3dentity_state_t *rhs = (const nmo_3dentity_state_t *)b;
    if (lhs->mesh_count != rhs->mesh_count ||
        lhs->animation_count != rhs->animation_count) {
        return false;
    }
    if ((lhs->mesh_count > 0 && (!lhs->mesh_ids || !rhs->mesh_ids)) ||
        (lhs->animation_count > 0 &&
         (!lhs->animation_ids || !rhs->animation_ids))) {
        return false;
    }
    nmo_3dentity_state_t lhs_value = *lhs;
    nmo_3dentity_state_t rhs_value = *rhs;
    lhs_value.mesh_ids = NULL;
    rhs_value.mesh_ids = NULL;
    lhs_value.animation_ids = NULL;
    rhs_value.animation_ids = NULL;
    if (memcmp(&lhs_value, &rhs_value, sizeof(lhs_value)) != 0) return false;
    if (lhs->mesh_count > 0 && memcmp(
            lhs->mesh_ids, rhs->mesh_ids,
            (size_t)lhs->mesh_count * sizeof(nmo_ref_t)) != 0) {
        return false;
    }
    return lhs->animation_count == 0 || memcmp(
        lhs->animation_ids, rhs->animation_ids,
        (size_t)lhs->animation_count * sizeof(nmo_ref_t)) == 0;
}

static uint32_t nmo_3dentity_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_3dentity_state_t *state = (const nmo_3dentity_state_t *)instance;
    nmo_3dentity_state_t value = *state;
    value.mesh_ids = NULL;
    value.animation_ids = NULL;
    uint32_t hash = (uint32_t)nmo_hash_fnv1a(&value, sizeof(value));
    if (state->mesh_ids != NULL && state->mesh_count > 0) {
        hash ^= (uint32_t)nmo_hash_fnv1a(
            state->mesh_ids, (size_t)state->mesh_count * sizeof(nmo_ref_t));
    }
    if (state->animation_ids != NULL && state->animation_count > 0) {
        hash ^= (uint32_t)nmo_hash_fnv1a(
            state->animation_ids,
            (size_t)state->animation_count * sizeof(nmo_ref_t));
    }
    return hash;
}

static nmo_status_t nmo_3dentity_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_3dentity_state_t *source = (const nmo_3dentity_state_t *)src;
    nmo_3dentity_state_t *target = (nmo_3dentity_state_t *)dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&target->mesh_ids, source->mesh_ids,
        sizeof(nmo_ref_t), source->mesh_count));
    return nmo_object_copy_array(
        arena, (void **)&target->animation_ids, source->animation_ids,
        sizeof(nmo_ref_t), source->animation_count);
}

static nmo_status_t nmo_3dentity_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    NMO_RETURN_IF_ERROR(nmo_object_default_validate(instance, type, context));
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)instance;
    if ((state->mesh_count > 0 && state->mesh_ids == NULL) ||
        (state->animation_count > 0 && state->animation_ids == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return NMO_OK;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_3dentity_vtable = {
    .prepare_dependencies = nmo_3dentity_prepare_dependencies,
    .remap_dependencies = nmo_3dentity_remap_dependencies,
    .pre_delete = nmo_3dentity_pre_delete,
    .post_delete = nmo_3dentity_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_3dentity_create,
        nmo_3dentity_destroy,
        nmo_3dentity_serialize,
        nmo_3dentity_deserialize,
        nmo_3dentity_copy,
        nmo_3dentity_validate,
        nmo_3dentity_equals,
        nmo_3dentity_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_3dentity_type,
    CKPGUID_3DENTITY,
    "CK3dEntity",
    NMO_CID_3DENTITY,
    CKPGUID_RENDEROBJECT,
    nmo_3dentity_state_t,
    &nmo_3dentity_vtable,
    nmo_3dentity_fields)
