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
#include "object/nmo_object_struct_guids.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include <stdint.h>

static nmo_status_t nmo_3dentity_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DEFINE_OBJECT_LIFECYCLE(
    3dentity,
    nmo_3dentity_state_t,
    do {
        nmo_status_t result = nmo_renderobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_renderobject_vtable.destroy(&state->base, NULL, context))

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
    NMO_FIELD_REF_VALUE(nmo_3dentity_state_t, parent),
    NMO_FIELD_REF_VALUE(nmo_3dentity_state_t, place),
    NMO_FIELD(nmo_3dentity_state_t, z_order, CKPGUID_INT),
    /* Meshes */
    NMO_FIELD_REF_VALUE(nmo_3dentity_state_t, current_mesh),
    NMO_FIELD(nmo_3dentity_state_t, mesh_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_3dentity_state_t, mesh_ids, mesh_count),
    /* Animations */
    NMO_FIELD(nmo_3dentity_state_t, animation_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_3dentity_state_t, animation_ids, animation_count),
    /* Skin (optional) */
    NMO_FIELD_PTR(nmo_3dentity_state_t, skin, CKPGUID_CK3DENTITYSKIN)
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
        const size_t bone_dwords = data_version < 6 ? 19u : 18u;
        if (bone_count > (SIZE_MAX - 1u) / bone_dwords) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Bone payload size exceeds limits");
        }
        size_t remaining_bytes = 0;
        NMO_RETURN_IF_ERROR(nmo_3dentity_identifier_payload_size_bytes(
            chunk, &remaining_bytes));
        if (bone_count * bone_dwords + 1u >
            remaining_bytes / sizeof(uint32_t)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Skin bones exceed remaining DWORDs");
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
            NMO_RETURN_IF_ERROR(nmo_ref_read(
                chunk, &out_state->skin->bones[i].bone));
            nmo_ref_check_class(
                &out_state->skin->bones[i].bone,
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_3DENTITY);
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
        const size_t min_vertex_dwords = data_version < 6 ? 7u : 4u;
        if ((size_t)vertex_count > SIZE_MAX / min_vertex_dwords ||
            (size_t)vertex_count >
                SIZE_MAX / sizeof(nmo_3dentity_skin_vertex_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Skin vertex payload size exceeds limits");
        }
        NMO_RETURN_IF_ERROR(nmo_3dentity_identifier_payload_size_bytes(
            chunk, &remaining_bytes));
        if ((size_t)vertex_count * min_vertex_dwords >
            remaining_bytes / sizeof(uint32_t)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Skin vertices exceed remaining DWORDs");
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
            const size_t fixed_vertex_dwords = data_version < 6 ? 6u : 3u;
            if ((size_t)bone_count_i >
                (SIZE_MAX - fixed_vertex_dwords) / 2u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Per-vertex bone payload size exceeds limits");
            }
            NMO_RETURN_IF_ERROR(nmo_3dentity_identifier_payload_size_bytes(
                chunk, &remaining_bytes));
            if (fixed_vertex_dwords + (size_t)bone_count_i * 2u >
                remaining_bytes / sizeof(uint32_t)) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Per-vertex bone data exceeds remaining DWORDs");
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
#if SIZE_MAX <= UINT32_MAX
            if ((size_t)vertex_count_u > SIZE_MAX / sizeof(nmo_vector_t)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Skin normals size exceeds limits");
            }
#endif
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
            result = nmo_ref_write_sequence_item(
                out_chunk, &skin->bones[i].bone);
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
    NMO_RETURN_IF_ERROR(nmo_3dentity_validate(instance, type, context));

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

static bool nmo_3dentity_ref_equals(
    const nmo_ref_t *lhs,
    const nmo_ref_t *rhs)
{
    return lhs->raw_id == rhs->raw_id &&
        lhs->id == rhs->id &&
        lhs->state == rhs->state;
}

static bool nmo_3dentity_ref_array_equals(
    const nmo_ref_t *lhs,
    const nmo_ref_t *rhs,
    uint32_t count)
{
    if (count > 0 && (lhs == NULL || rhs == NULL)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!nmo_3dentity_ref_equals(&lhs[i], &rhs[i])) return false;
    }
    return true;
}

static bool nmo_3dentity_skin_equals(
    const nmo_3dentity_skin_t *lhs,
    const nmo_3dentity_skin_t *rhs)
{
    if (lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL ||
        lhs->bone_count != rhs->bone_count ||
        lhs->vertex_count != rhs->vertex_count ||
        lhs->normal_count != rhs->normal_count ||
        lhs->normals_present != rhs->normals_present ||
        lhs->normals_have_count != rhs->normals_have_count ||
        memcmp(&lhs->object_init_matrix, &rhs->object_init_matrix,
               sizeof(lhs->object_init_matrix)) != 0 ||
        (lhs->bone_count > 0 && (!lhs->bones || !rhs->bones)) ||
        (lhs->vertex_count > 0 && (!lhs->vertices || !rhs->vertices)) ||
        (lhs->normal_count > 0 && (!lhs->normals || !rhs->normals))) {
        return false;
    }

    for (uint32_t i = 0; i < lhs->bone_count; ++i) {
        if (!nmo_3dentity_ref_equals(
                &lhs->bones[i].bone, &rhs->bones[i].bone) ||
            lhs->bones[i].bone_flags != rhs->bones[i].bone_flags ||
            memcmp(&lhs->bones[i].inverse_bind_matrix,
                   &rhs->bones[i].inverse_bind_matrix,
                   sizeof(lhs->bones[i].inverse_bind_matrix)) != 0) {
            return false;
        }
    }
    for (uint32_t i = 0; i < lhs->vertex_count; ++i) {
        const nmo_3dentity_skin_vertex_t *lhs_vertex = &lhs->vertices[i];
        const nmo_3dentity_skin_vertex_t *rhs_vertex = &rhs->vertices[i];
        if (lhs_vertex->bone_count != rhs_vertex->bone_count ||
            memcmp(&lhs_vertex->initial_pos, &rhs_vertex->initial_pos,
                   sizeof(lhs_vertex->initial_pos)) != 0 ||
            (lhs_vertex->bone_count > 0 &&
             (!lhs_vertex->bone_indices || !rhs_vertex->bone_indices ||
              !lhs_vertex->bone_weights || !rhs_vertex->bone_weights)) ||
            (lhs_vertex->bone_count > 0 &&
             (memcmp(lhs_vertex->bone_indices, rhs_vertex->bone_indices,
                     (size_t)lhs_vertex->bone_count * sizeof(uint32_t)) != 0 ||
              memcmp(lhs_vertex->bone_weights, rhs_vertex->bone_weights,
                     (size_t)lhs_vertex->bone_count * sizeof(float)) != 0))) {
            return false;
        }
    }
    return lhs->normal_count == 0 ||
        memcmp(lhs->normals, rhs->normals,
               (size_t)lhs->normal_count * sizeof(nmo_vector_t)) == 0;
}

static bool nmo_3dentity_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_3dentity_state_t *lhs = (const nmo_3dentity_state_t *)a;
    const nmo_3dentity_state_t *rhs = (const nmo_3dentity_state_t *)b;

    return nmo_renderobject_vtable.equals(&lhs->base, &rhs->base) &&
        memcmp(lhs->world_matrix, rhs->world_matrix,
               sizeof(lhs->world_matrix)) == 0 &&
        lhs->entity_flags == rhs->entity_flags &&
        lhs->moveable_flags == rhs->moveable_flags &&
        nmo_3dentity_ref_equals(&lhs->parent, &rhs->parent) &&
        nmo_3dentity_ref_equals(&lhs->place, &rhs->place) &&
        lhs->z_order == rhs->z_order &&
        nmo_3dentity_ref_equals(&lhs->current_mesh, &rhs->current_mesh) &&
        lhs->mesh_count == rhs->mesh_count &&
        nmo_3dentity_ref_array_equals(
            lhs->mesh_ids, rhs->mesh_ids, lhs->mesh_count) &&
        lhs->animation_count == rhs->animation_count &&
        nmo_3dentity_ref_array_equals(
            lhs->animation_ids, rhs->animation_ids, lhs->animation_count) &&
        nmo_3dentity_skin_equals(lhs->skin, rhs->skin) &&
        lhs->has_mesh_chunk == rhs->has_mesh_chunk &&
        lhs->has_animation_chunk == rhs->has_animation_chunk &&
        lhs->has_entityndata_chunk == rhs->has_entityndata_chunk &&
        lhs->has_parent_chunk == rhs->has_parent_chunk &&
        lhs->has_flags_chunk == rhs->has_flags_chunk &&
        lhs->has_matrix_chunk == rhs->has_matrix_chunk;
}

static uint32_t nmo_3dentity_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_3dentity_hash_ref(uint32_t hash, const nmo_ref_t *ref)
{
    hash = nmo_3dentity_hash_bytes(hash, &ref->raw_id, sizeof(ref->raw_id));
    hash = nmo_3dentity_hash_bytes(hash, &ref->id, sizeof(ref->id));
    return nmo_3dentity_hash_bytes(hash, &ref->state, sizeof(ref->state));
}

static uint32_t nmo_3dentity_hash_skin(
    uint32_t hash,
    const nmo_3dentity_skin_t *skin)
{
    const uint8_t present = skin != NULL;
    hash = nmo_3dentity_hash_bytes(hash, &present, sizeof(present));
    if (skin == NULL) return hash;

    hash = nmo_3dentity_hash_bytes(
        hash, &skin->object_init_matrix, sizeof(skin->object_init_matrix));
    hash = nmo_3dentity_hash_bytes(
        hash, &skin->bone_count, sizeof(skin->bone_count));
    for (uint32_t i = 0; i < skin->bone_count && skin->bones != NULL; ++i) {
        hash = nmo_3dentity_hash_ref(hash, &skin->bones[i].bone);
        hash = nmo_3dentity_hash_bytes(
            hash, &skin->bones[i].bone_flags,
            sizeof(skin->bones[i].bone_flags));
        hash = nmo_3dentity_hash_bytes(
            hash, &skin->bones[i].inverse_bind_matrix,
            sizeof(skin->bones[i].inverse_bind_matrix));
    }
    hash = nmo_3dentity_hash_bytes(
        hash, &skin->vertex_count, sizeof(skin->vertex_count));
    for (uint32_t i = 0;
         i < skin->vertex_count && skin->vertices != NULL;
         ++i) {
        const nmo_3dentity_skin_vertex_t *vertex = &skin->vertices[i];
        hash = nmo_3dentity_hash_bytes(
            hash, &vertex->bone_count, sizeof(vertex->bone_count));
        hash = nmo_3dentity_hash_bytes(
            hash, &vertex->initial_pos, sizeof(vertex->initial_pos));
        if (vertex->bone_count > 0 && vertex->bone_indices != NULL) {
            hash = nmo_3dentity_hash_bytes(
                hash, vertex->bone_indices,
                (size_t)vertex->bone_count * sizeof(uint32_t));
        }
        if (vertex->bone_count > 0 && vertex->bone_weights != NULL) {
            hash = nmo_3dentity_hash_bytes(
                hash, vertex->bone_weights,
                (size_t)vertex->bone_count * sizeof(float));
        }
    }
    hash = nmo_3dentity_hash_bytes(
        hash, &skin->normal_count, sizeof(skin->normal_count));
    if (skin->normal_count > 0 && skin->normals != NULL) {
        hash = nmo_3dentity_hash_bytes(
            hash, skin->normals,
            (size_t)skin->normal_count * sizeof(nmo_vector_t));
    }
    hash = nmo_3dentity_hash_bytes(
        hash, &skin->normals_present, sizeof(skin->normals_present));
    return nmo_3dentity_hash_bytes(
        hash, &skin->normals_have_count, sizeof(skin->normals_have_count));
}

static uint32_t nmo_3dentity_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_3dentity_state_t *state = (const nmo_3dentity_state_t *)instance;
    uint32_t hash = 2166136261u;
    const uint32_t base_hash = nmo_renderobject_vtable.hash(&state->base);
    hash = nmo_3dentity_hash_bytes(hash, &base_hash, sizeof(base_hash));
    hash = nmo_3dentity_hash_bytes(
        hash, state->world_matrix, sizeof(state->world_matrix));
    hash = nmo_3dentity_hash_bytes(
        hash, &state->entity_flags, sizeof(state->entity_flags));
    hash = nmo_3dentity_hash_bytes(
        hash, &state->moveable_flags, sizeof(state->moveable_flags));
    hash = nmo_3dentity_hash_ref(hash, &state->parent);
    hash = nmo_3dentity_hash_ref(hash, &state->place);
    hash = nmo_3dentity_hash_bytes(
        hash, &state->z_order, sizeof(state->z_order));
    hash = nmo_3dentity_hash_ref(hash, &state->current_mesh);
    hash = nmo_3dentity_hash_bytes(
        hash, &state->mesh_count, sizeof(state->mesh_count));
    for (uint32_t i = 0;
         i < state->mesh_count && state->mesh_ids != NULL;
         ++i) {
        hash = nmo_3dentity_hash_ref(hash, &state->mesh_ids[i]);
    }
    hash = nmo_3dentity_hash_bytes(
        hash, &state->animation_count, sizeof(state->animation_count));
    for (uint32_t i = 0;
         i < state->animation_count && state->animation_ids != NULL;
         ++i) {
        hash = nmo_3dentity_hash_ref(hash, &state->animation_ids[i]);
    }
    hash = nmo_3dentity_hash_skin(hash, state->skin);
    hash = nmo_3dentity_hash_bytes(
        hash, &state->has_mesh_chunk, sizeof(state->has_mesh_chunk));
    hash = nmo_3dentity_hash_bytes(
        hash, &state->has_animation_chunk,
        sizeof(state->has_animation_chunk));
    hash = nmo_3dentity_hash_bytes(
        hash, &state->has_entityndata_chunk,
        sizeof(state->has_entityndata_chunk));
    hash = nmo_3dentity_hash_bytes(
        hash, &state->has_parent_chunk, sizeof(state->has_parent_chunk));
    hash = nmo_3dentity_hash_bytes(
        hash, &state->has_flags_chunk, sizeof(state->has_flags_chunk));
    return nmo_3dentity_hash_bytes(
        hash, &state->has_matrix_chunk, sizeof(state->has_matrix_chunk));
}

static nmo_status_t nmo_3dentity_copy_skin(
    nmo_arena_t *arena,
    const nmo_3dentity_skin_t *source,
    nmo_3dentity_skin_t **out_skin)
{
    if (arena == NULL || out_skin == NULL) return NMO_ERR_INVALID_ARGUMENT;
    *out_skin = NULL;
    if (source == NULL) return NMO_OK;
    if ((source->bone_count > 0 && source->bones == NULL) ||
        (source->vertex_count > 0 && source->vertices == NULL) ||
        (source->normal_count > 0 && source->normals == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < source->vertex_count; ++i) {
        if (source->vertices[i].bone_count > 0 &&
            (source->vertices[i].bone_indices == NULL ||
             source->vertices[i].bone_weights == NULL)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }

    nmo_3dentity_skin_t *copy = nmo_arena_alloc(
        arena, sizeof(*copy), _Alignof(nmo_3dentity_skin_t));
    if (copy == NULL) return NMO_ERR_NOMEM;
    *copy = *source;
    copy->bones = NULL;
    copy->vertices = NULL;
    copy->normals = NULL;

    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&copy->bones, source->bones,
        sizeof(*copy->bones), source->bone_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&copy->vertices, source->vertices,
        sizeof(*copy->vertices), source->vertex_count));
    for (uint32_t i = 0; i < source->vertex_count; ++i) {
        copy->vertices[i].bone_indices = NULL;
        copy->vertices[i].bone_weights = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(
            arena, (void **)&copy->vertices[i].bone_indices,
            source->vertices[i].bone_indices, sizeof(uint32_t),
            source->vertices[i].bone_count));
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(
            arena, (void **)&copy->vertices[i].bone_weights,
            source->vertices[i].bone_weights, sizeof(float),
            source->vertices[i].bone_count));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&copy->normals, source->normals,
        sizeof(*copy->normals), source->normal_count));
    *out_skin = copy;
    return NMO_OK;
}

static nmo_status_t nmo_3dentity_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_3dentity_state_t *source = (const nmo_3dentity_state_t *)src;
    nmo_3dentity_state_t *target = (nmo_3dentity_state_t *)dst;
    (void)type;
    if (source == NULL || target == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_3dentity_validate(source, NULL, NULL));

    nmo_3dentity_state_t copied;
    nmo_status_t result = nmo_3dentity_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;

    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_renderobject_state_t),
    };
    result = nmo_renderobject_vtable.copy(
        &source->base, &copied.base, &base_type, arena);
    if (result != NMO_OK) goto fail;

    memcpy(copied.world_matrix, source->world_matrix,
           sizeof(copied.world_matrix));
    copied.entity_flags = source->entity_flags;
    copied.moveable_flags = source->moveable_flags;
    copied.parent = source->parent;
    copied.place = source->place;
    copied.z_order = source->z_order;
    copied.current_mesh = source->current_mesh;
    copied.mesh_count = source->mesh_count;
    copied.animation_count = source->animation_count;
    copied.has_mesh_chunk = source->has_mesh_chunk;
    copied.has_animation_chunk = source->has_animation_chunk;
    copied.has_entityndata_chunk = source->has_entityndata_chunk;
    copied.has_parent_chunk = source->has_parent_chunk;
    copied.has_flags_chunk = source->has_flags_chunk;
    copied.has_matrix_chunk = source->has_matrix_chunk;

    result = nmo_object_copy_array(
        arena, (void **)&copied.mesh_ids, source->mesh_ids,
        sizeof(nmo_ref_t), source->mesh_count);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_array(
        arena, (void **)&copied.animation_ids, source->animation_ids,
        sizeof(nmo_ref_t), source->animation_count);
    if (result != NMO_OK) goto fail;
    result = nmo_3dentity_copy_skin(arena, source->skin, &copied.skin);
    if (result != NMO_OK) goto fail;

    nmo_beobject_state_t *target_base = &target->base.base;
    const nmo_beobject_state_t *source_base = &source->base.base;
    if (target_base->scripts.data == source_base->scripts.data) {
        memset(&target_base->scripts, 0, sizeof(target_base->scripts));
    }
    if (target_base->attributes.data == source_base->attributes.data) {
        memset(&target_base->attributes, 0, sizeof(target_base->attributes));
    }
    if (target_base->legacy_attributes.data ==
        source_base->legacy_attributes.data) {
        memset(&target_base->legacy_attributes, 0,
               sizeof(target_base->legacy_attributes));
    }
    nmo_3dentity_destroy(target, NULL, NULL);
    *target = copied;
    return NMO_OK;

fail:
    nmo_3dentity_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_3dentity_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)instance;
    NMO_RETURN_IF_ERROR(nmo_renderobject_vtable.validate(
        &state->base, NULL, context));
    if ((state->mesh_count > 0 && state->mesh_ids == NULL) ||
        (state->animation_count > 0 && state->animation_ids == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (state->mesh_count > (uint32_t)INT32_MAX ||
        state->animation_count > (uint32_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    const nmo_3dentity_skin_t *skin = state->skin;
    if (skin == NULL) return NMO_OK;
    if ((skin->bone_count > 0 && skin->bones == NULL) ||
        (skin->vertex_count > 0 && skin->vertices == NULL) ||
        (skin->normal_count > 0 && skin->normals == NULL) ||
        skin->bone_count > (uint32_t)INT32_MAX ||
        skin->vertex_count > (uint32_t)INT32_MAX ||
        skin->normal_count > (uint32_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < skin->vertex_count; ++i) {
        const nmo_3dentity_skin_vertex_t *vertex = &skin->vertices[i];
        if (vertex->bone_count > (uint32_t)INT32_MAX ||
            (vertex->bone_count > 0 &&
             (vertex->bone_indices == NULL ||
              vertex->bone_weights == NULL))) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    if ((skin->normals_present || skin->normal_count > 0) &&
        skin->normal_count != skin->vertex_count) {
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
