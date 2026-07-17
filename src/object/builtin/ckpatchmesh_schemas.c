/**
 * @file ckpatchmesh_schemas.c
 * @brief CKPatchMesh schema implementation
 */

#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_object_struct_guids.h"
#include "core/nmo_utils.h"
#include <string.h>

#define NMO_PATCHMESH_READ(expr, field_name) do { \
    nmo_status_t patchmesh_read_result__ = (expr); \
    if (patchmesh_read_result__ != NMO_OK) { \
        NMO_RETURN_ERROR(patchmesh_read_result__, NMO_SEVERITY_ERROR, \
                         "CKPatchMesh %s is truncated", (field_name)); \
    } \
} while (0)

static nmo_status_t nmo_patchmesh_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_patchmesh_create");
    }
    nmo_patchmesh_state_t *state = instance;
    memset(state, 0, sizeof(*state));
    return nmo_beobject_vtable.create(&state->base.beobject, NULL, NULL);
}

static void nmo_patchmesh_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    nmo_patchmesh_state_t *state = instance;
    if (!state) return;
    nmo_array_dispose(&state->base.beobject.scripts);
    nmo_array_dispose(&state->base.beobject.attributes);
    nmo_array_dispose(&state->base.beobject.legacy_attributes);
    memset(state, 0, sizeof(*state));
}

static nmo_status_t nmo_patchmesh_check_buffer_layout(
    uint32_t byte_count,
    uint32_t item_count,
    size_t item_size,
    const char *label)
{
    if (item_size != 0 && (size_t)item_count > SIZE_MAX / item_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "CKPatchMesh %s size overflows", label);
    }
    const size_t expected = (size_t)item_count * item_size;
    if (expected != (size_t)byte_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "CKPatchMesh %s size does not match its count", label);
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_patchmesh_check_buffer_available(
    nmo_chunk_t *chunk,
    uint32_t byte_count,
    const char *label)
{
    const size_t dword_count = ((size_t)byte_count + 3u) / 4u;
    if (!nmo_chunk_has_read_capacity(chunk, dword_count)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "CKPatchMesh %s exceeds remaining DWORDs", label);
    }
    NMO_RETURN_OK();
}

static void nmo_patchmesh_dispose_beobject_arrays(nmo_patchmesh_state_t *state)
{
    if (!state) return;
    nmo_array_dispose(&state->base.beobject.scripts);
    nmo_array_dispose(&state->base.beobject.attributes);
    nmo_array_dispose(&state->base.beobject.legacy_attributes);
}

static void nmo_patchmesh_publish_state(
    nmo_patchmesh_state_t *out_state,
    const nmo_patchmesh_state_t *decoded)
{
    nmo_patchmesh_dispose_beobject_arrays(out_state);
    *out_state = *decoded;
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_patchmesh_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_patchmesh_state_t, base),
                    sizeof(nmo_mesh_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_patchmesh_state_t, format, NMO_GUID_ENUM_CK_PATCHMESH_FORMAT),
    NMO_FIELD(nmo_patchmesh_state_t, patch_flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, iteration_count, CKPGUID_INT),
    NMO_FIELD(nmo_patchmesh_state_t, vec_count, CKPGUID_INT),
    NMO_FIELD(nmo_patchmesh_state_t, total_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, vectors, total_count, 1, CKPGUID_VECTOR),
    NMO_FIELD(nmo_patchmesh_state_t, patch_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, patches, patch_count, 1, NMO_GUID_STRUCT_CKPATCHMESHPATCHRECORD),
    NMO_FIELD(nmo_patchmesh_state_t, edge_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, edge_data_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, edge_data, edge_data_size, 1, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, channel_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, channels, channel_count, 1, NMO_GUID_STRUCT_CKPATCHMESHCHANNEL),
    NMO_FIELD_REF(nmo_patchmesh_state_t, legacy_default_material),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_patch_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_patch_data_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, legacy_patch_data, legacy_patch_data_size, 1, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_edge_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_edge_data_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, legacy_edge_data, legacy_edge_data_size, 1, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_tvpatch_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_tvpatch_data_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, legacy_tvpatch_data, legacy_tvpatch_data_size, 1, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_uv_count, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_uv_data_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, legacy_uv_data, legacy_uv_data_size, 1, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_smoothing_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_patchmesh_state_t, legacy_smoothing_groups, legacy_smoothing_count, 1, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, has_legacy_smoothing, CKPGUID_BOOL),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_material_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_patchmesh_state_t, legacy_materials, legacy_material_count),
    NMO_FIELD(nmo_patchmesh_state_t, has_legacy_materials, CKPGUID_BOOL)
};

static nmo_status_t nmo_patchmesh_decode_payload(
    nmo_chunk_t *chunk,
    void *context,
    nmo_patchmesh_state_t *decoded);

static nmo_status_t nmo_patchmesh_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_patchmesh_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_patchmesh_deserialize");
    }

    nmo_patchmesh_state_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    nmo_status_t init_result = nmo_beobject_vtable.create(
        &decoded.base.beobject, NULL, NULL);
    if (init_result != NMO_OK) return init_result;

    {
        nmo_status_t result = nmo_mesh_deserialize(&decoded.base, chunk, NULL, context);
        if (result != NMO_OK) {
            char detail[256];
            size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));
            nmo_patchmesh_dispose_beobject_arrays(&decoded);
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR,
                             "CKPatchMesh base CKMesh data is invalid or truncated%s%s",
                             detail_len > 0u ? ": " : "",
                             detail_len > 0u ? detail : "");
        }
    }

    nmo_status_t result = nmo_patchmesh_decode_payload(
        chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_patchmesh_dispose_beobject_arrays(&decoded);
        return result;
    }
    nmo_patchmesh_publish_state(out_state, &decoded);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_patchmesh_decode_payload(
    nmo_chunk_t *chunk,
    void *context,
    nmo_patchmesh_state_t *decoded_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !decoded_state || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid CKPatchMesh payload arguments");
    }

#define decoded (*decoded_state)

    nmo_status_t seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PATCHMESHDATA3);
    if (seek_result == NMO_OK) {
        decoded.format = CKPATCHMESH_FORMAT_DATA3;

        uint32_t patch_flags = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &patch_flags), "flags");
        decoded.patch_flags = patch_flags;
        NMO_PATCHMESH_READ(nmo_chunk_read_int(chunk, &decoded.iteration_count),
                           "iteration count");
        NMO_PATCHMESH_READ(nmo_chunk_read_int(chunk, &decoded.vec_count),
                           "vector count");

        uint32_t buffer_size = 0;
        uint32_t total_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &buffer_size),
                           "vector buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &total_count),
                           "total vector count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            buffer_size, total_count, sizeof(nmo_vector_t), "vector buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, buffer_size, "vector buffer"));
        decoded.total_count = total_count;

        if (total_count > 0) {
            decoded.vectors = (nmo_vector_t *)nmo_arena_alloc(
                arena, (size_t)total_count * sizeof(nmo_vector_t),
                _Alignof(nmo_vector_t));
            if (!decoded.vectors) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh vectors");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.vectors, buffer_size), "vector buffer");
        }

        size_t patch_count = 0;
        nmo_status_t sequence_result = nmo_chunk_read_object_sequence_start(
            chunk, &patch_count);
        if (sequence_result != NMO_OK) {
            NMO_RETURN_ERROR(sequence_result, NMO_SEVERITY_ERROR,
                             "CKPatchMesh DATA3 patch sequence is truncated");
        }
        if (patch_count > UINT32_MAX ||
            patch_count > SIZE_MAX / sizeof(nmo_patchmesh_patch_record_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "CKPatchMesh patch count exceeds limits");
        }
        if (patch_count > SIZE_MAX / 13u ||
            !nmo_chunk_has_read_capacity(chunk, patch_count * 13u)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "CKPatchMesh patches exceed remaining DWORDs");
        }
        if (patch_count > 0) {
            decoded.patch_count = (uint32_t)patch_count;
            decoded.patches = (nmo_patchmesh_patch_record_t *)nmo_arena_alloc(
                arena, patch_count * sizeof(nmo_patchmesh_patch_record_t),
                _Alignof(nmo_patchmesh_patch_record_t));

            if (!decoded.patches) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh patches");
            }

            for (uint32_t i = 0; i < decoded.patch_count; ++i) {
                NMO_PATCHMESH_READ(nmo_ref_read(
                    chunk, &decoded.patches[i].material), "patch material ID");
            }

            for (uint32_t i = 0; i < decoded.patch_count; ++i) {
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(
                    chunk, &decoded.patches[i].patch.type), "patch type");
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(
                    chunk, &decoded.patches[i].patch.smoothing_group),
                    "patch smoothing group");
                NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                    chunk, decoded.patches[i].patch.data,
                    sizeof(decoded.patches[i].patch.data)), "patch data");
            }
        }

        uint32_t edge_bytes = 0;
        uint32_t edge_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &edge_bytes),
                           "edge buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &edge_count),
                           "edge count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            edge_bytes, edge_count, 12u, "edge buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, edge_bytes, "edge buffer"));
        decoded.edge_count = edge_count;
        decoded.edge_data_size = edge_bytes;
        if (edge_bytes > 0) {
            decoded.edge_data = (uint8_t *)nmo_arena_alloc(arena, edge_bytes, 1);
            if (!decoded.edge_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh edge buffer");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.edge_data, edge_bytes), "edge buffer");
        }

        size_t channel_count = 0;
        sequence_result = nmo_chunk_read_object_sequence_start(
            chunk, &channel_count);
        if (sequence_result != NMO_OK) {
            NMO_RETURN_ERROR(sequence_result, NMO_SEVERITY_ERROR,
                             "CKPatchMesh DATA3 channel sequence is truncated");
        }
        if (channel_count > UINT32_MAX ||
            channel_count > SIZE_MAX / sizeof(nmo_patchmesh_channel_t)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "CKPatchMesh channel count exceeds limits");
        }
        if (channel_count > SIZE_MAX / 8u ||
            !nmo_chunk_has_read_capacity(chunk, channel_count * 8u)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "CKPatchMesh channels exceed remaining DWORDs");
        }
        if (channel_count > 0) {
            decoded.channel_count = (uint32_t)channel_count;
            decoded.channels = (nmo_patchmesh_channel_t *)nmo_arena_alloc(
                arena, channel_count * sizeof(nmo_patchmesh_channel_t),
                _Alignof(nmo_patchmesh_channel_t));
            if (!decoded.channels) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh channels");
            }
            memset(decoded.channels, 0,
                   channel_count * sizeof(nmo_patchmesh_channel_t));

            for (uint32_t i = 0; i < decoded.channel_count; ++i) {
                NMO_PATCHMESH_READ(nmo_ref_read(
                    chunk, &decoded.channels[i].material),
                    "channel material ID");
            }

            for (uint32_t i = 0; i < decoded.channel_count; ++i) {
                nmo_patchmesh_channel_t *channel = &decoded.channels[i];
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &channel->flags),
                                   "channel flags");
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &channel->type),
                                   "channel type");
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &channel->subtype),
                                   "channel subtype");

                uint32_t patches_bytes = 0;
                uint32_t patches_count = 0;
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &patches_bytes),
                                   "channel patch buffer size");
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &patches_count),
                                   "channel patch count");
                NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
                    patches_bytes, patches_count, 8u,
                    "channel patch buffer"));
                NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
                    chunk, patches_bytes, "channel patch buffer"));
                channel->patch_count = patches_count;
                if (patches_bytes > 0) {
                    channel->patches_raw = (uint8_t *)nmo_arena_alloc(arena, patches_bytes, 1);
                    if (!channel->patches_raw) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate TVPatch buffer");
                    }
                    NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                        chunk, channel->patches_raw, patches_bytes),
                        "channel patch buffer");
                }

                uint32_t uv_bytes = 0;
                uint32_t uv_count = 0;
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &uv_bytes),
                                   "channel UV buffer size");
                NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &uv_count),
                                   "channel UV count");
                NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
                    uv_bytes, uv_count, sizeof(nmo_vector2_t),
                    "channel UV buffer"));
                NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
                    chunk, uv_bytes, "channel UV buffer"));
                channel->uv_count = uv_count;
                if (uv_count > 0) {
                    channel->uvs = (nmo_vector2_t *)nmo_arena_alloc(
                        arena, sizeof(nmo_vector2_t) * uv_count, _Alignof(nmo_vector2_t));
                    if (!channel->uvs) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate UV buffer");
                    }
                    NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                        chunk, channel->uvs, uv_bytes), "channel UV buffer");
                }
            }
        }

        const nmo_object_repository_t *repository =
            nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        for (uint32_t i = 0; i < decoded.patch_count; ++i) {
            nmo_ref_check_class(&decoded.patches[i].material, repository,
                                types, NMO_CID_MATERIAL);
        }
        for (uint32_t i = 0; i < decoded.channel_count; ++i) {
            nmo_ref_check_class(&decoded.channels[i].material, repository,
                                types, NMO_CID_MATERIAL);
        }
        NMO_RETURN_OK();
    }

    if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA2);
    if (seek_result == NMO_OK) {
        decoded.format = CKPATCHMESH_FORMAT_DATA2;

        uint32_t patch_flags = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &patch_flags),
                           "DATA2 flags");
        decoded.patch_flags = patch_flags;
        NMO_PATCHMESH_READ(nmo_ref_read(
            chunk, &decoded.legacy_default_material),
            "DATA2 default material");
        NMO_PATCHMESH_READ(nmo_chunk_read_int(chunk, &decoded.iteration_count),
                           "DATA2 iteration count");
        NMO_PATCHMESH_READ(nmo_chunk_read_int(chunk, &decoded.vec_count),
                           "DATA2 vector count");

        uint32_t buffer_size = 0;
        uint32_t total_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &buffer_size),
                           "DATA2 vector buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &total_count),
                           "DATA2 total vector count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            buffer_size, total_count, sizeof(nmo_vector_t),
            "DATA2 vector buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, buffer_size, "DATA2 vector buffer"));
        decoded.total_count = total_count;

        if (total_count > 0) {
            decoded.vectors = (nmo_vector_t *)nmo_arena_alloc(
                arena, (size_t)total_count * sizeof(nmo_vector_t),
                _Alignof(nmo_vector_t));
            if (!decoded.vectors) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh vectors");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.vectors, buffer_size), "DATA2 vector buffer");
        }

        uint32_t patch_bytes = 0;
        uint32_t patch_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &patch_bytes),
                           "DATA2 patch buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &patch_count),
                           "DATA2 patch count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            patch_bytes, patch_count, 88u, "DATA2 patch buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, patch_bytes, "DATA2 patch buffer"));
        decoded.legacy_patch_count = patch_count;
        decoded.legacy_patch_data_size = patch_bytes;
        if (patch_bytes > 0) {
            decoded.legacy_patch_data = (uint8_t *)nmo_arena_alloc(arena, patch_bytes, 1);
            if (!decoded.legacy_patch_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy patch buffer");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.legacy_patch_data, patch_bytes),
                "DATA2 patch buffer");
        }

        uint32_t edge_bytes = 0;
        uint32_t edge_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &edge_bytes),
                           "DATA2 edge buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &edge_count),
                           "DATA2 edge count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            edge_bytes, edge_count, 24u, "DATA2 edge buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, edge_bytes, "DATA2 edge buffer"));
        decoded.legacy_edge_count = edge_count;
        decoded.legacy_edge_data_size = edge_bytes;
        if (edge_bytes > 0) {
            decoded.legacy_edge_data = (uint8_t *)nmo_arena_alloc(arena, edge_bytes, 1);
            if (!decoded.legacy_edge_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy edge buffer");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.legacy_edge_data, edge_bytes),
                "DATA2 edge buffer");
        }

        uint32_t tv_bytes = 0;
        uint32_t tv_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &tv_bytes),
                           "DATA2 TVPatch buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &tv_count),
                           "DATA2 TVPatch count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            tv_bytes, tv_count, 16u, "DATA2 TVPatch buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, tv_bytes, "DATA2 TVPatch buffer"));
        decoded.legacy_tvpatch_count = tv_count;
        decoded.legacy_tvpatch_data_size = tv_bytes;
        if (tv_bytes > 0) {
            decoded.legacy_tvpatch_data = (uint8_t *)nmo_arena_alloc(arena, tv_bytes, 1);
            if (!decoded.legacy_tvpatch_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy TVPatch buffer");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.legacy_tvpatch_data, tv_bytes),
                "DATA2 TVPatch buffer");
        }

        uint32_t uv_bytes = 0;
        uint32_t uv_count = 0;
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &uv_bytes),
                           "DATA2 UV buffer size");
        NMO_PATCHMESH_READ(nmo_chunk_read_dword(chunk, &uv_count),
                           "DATA2 UV count");
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
            uv_bytes, uv_count, sizeof(nmo_vector2_t), "DATA2 UV buffer"));
        NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
            chunk, uv_bytes, "DATA2 UV buffer"));
        decoded.legacy_uv_count = uv_count;
        decoded.legacy_uv_data_size = uv_bytes;
        if (uv_bytes > 0) {
            decoded.legacy_uv_data = (uint8_t *)nmo_arena_alloc(arena, uv_bytes, 1);
            if (!decoded.legacy_uv_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy UV buffer");
            }
            NMO_PATCHMESH_READ(nmo_chunk_read_and_fill_buffer_nosize_checked(
                chunk, decoded.legacy_uv_data, uv_bytes),
                "DATA2 UV buffer");
        }

        seek_result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PATCHMESHSMOOTH);
        if (seek_result == NMO_OK) {
            decoded.has_legacy_smoothing = 1;
            uint32_t smooth_bytes = 0;
            uint32_t smooth_count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &smooth_bytes));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &smooth_count));
            NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_layout(
                smooth_bytes, smooth_count, sizeof(uint32_t),
                "DATA2 smoothing buffer"));
            NMO_RETURN_IF_ERROR(nmo_patchmesh_check_buffer_available(
                chunk, smooth_bytes, "DATA2 smoothing buffer"));
            decoded.legacy_smoothing_count = smooth_count;
            if (smooth_count > 0) {
                decoded.legacy_smoothing_groups = (uint32_t *)nmo_arena_alloc(
                    arena, (size_t)smooth_count * sizeof(uint32_t),
                    _Alignof(uint32_t));
                if (!decoded.legacy_smoothing_groups) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate smoothing groups");
                }
                NMO_RETURN_IF_ERROR(nmo_chunk_read_and_fill_buffer_nosize_checked(
                    chunk, decoded.legacy_smoothing_groups, smooth_bytes));
            }
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        seek_result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PATCHMESHMATERIALS);
        if (seek_result == NMO_OK) {
            decoded.has_legacy_materials = 1;
            size_t seq_count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(
                chunk, &seq_count));
            if (seq_count > UINT32_MAX ||
                seq_count > SIZE_MAX / sizeof(nmo_ref_t)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "CKPatchMesh legacy material count exceeds limits");
            }
            if (!nmo_chunk_has_read_capacity(chunk, seq_count)) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK,
                                 NMO_SEVERITY_ERROR,
                                 "CKPatchMesh legacy materials exceed remaining DWORDs");
            }
            if (seq_count > 0) {
                decoded.legacy_material_count = (uint32_t)seq_count;
                decoded.legacy_materials = (nmo_ref_t *)nmo_arena_alloc(
                    arena, seq_count * sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
                if (!decoded.legacy_materials) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy materials");
                }

                for (uint32_t i = 0; i < decoded.legacy_material_count; ++i) {
                    NMO_RETURN_IF_ERROR(nmo_ref_read(
                        chunk, &decoded.legacy_materials[i]));
                }
            }
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

        const nmo_object_repository_t *repository =
            nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        nmo_ref_check_class(&decoded.legacy_default_material, repository,
                            types, NMO_CID_MATERIAL);
        for (uint32_t i = 0; i < decoded.legacy_material_count; ++i) {
            nmo_ref_check_class(&decoded.legacy_materials[i], repository,
                                types, NMO_CID_MATERIAL);
        }
        NMO_RETURN_OK();
    }

    if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
#undef decoded
    NMO_RETURN_OK();
}

static nmo_status_t nmo_patchmesh_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_patchmesh_state_t *s = src;
    nmo_patchmesh_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));

    const nmo_type_descriptor_t beobject_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.copy(
        &s->base.beobject, &d->base.beobject, &beobject_type, arena));

    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.faces,
                                              s->base.faces, sizeof(nmo_face_t), s->base.face_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.face_vertex_indices,
                                              s->base.face_vertex_indices, sizeof(uint16_t),
                                              (uint32_t)(s->base.face_count * 3u)));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.line_indices,
                                              s->base.line_indices, sizeof(uint16_t),
                                              (uint32_t)(s->base.line_count * 2u)));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.vertices,
                                              s->base.vertices, sizeof(nmo_vertex_t), s->base.vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.vertex_colors,
                                              s->base.vertex_colors, sizeof(uint32_t), s->base.vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.vertex_specular,
                                              s->base.vertex_specular, sizeof(uint32_t), s->base.vertex_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.vertex_weights,
                                              s->base.vertex_weights, sizeof(float), s->base.vertex_weight_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.material_groups,
                                              s->base.material_groups, sizeof(nmo_material_group_t),
                                              s->base.material_group_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.material_channels,
                                              s->base.material_channels, sizeof(nmo_material_channel_t),
                                              s->base.material_channel_count));
    for (uint32_t i = 0; i < s->base.material_channel_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena,
                                                  (void **)&d->base.material_channels[i].uv_coords,
                                                  s->base.material_channels[i].uv_coords,
                                                  sizeof(nmo_vector2_t),
                                                  s->base.material_channels[i].uv_count));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, &d->base.pm_data, s->base.pm_data, s->base.pm_data_size));

    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->vectors,
                                              s->vectors, sizeof(nmo_vector_t), s->total_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->patches,
                                              s->patches, sizeof(nmo_patchmesh_patch_record_t),
                                              s->patch_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->edge_data,
                                              s->edge_data, s->edge_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->channels,
                                              s->channels, sizeof(nmo_patchmesh_channel_t), s->channel_count));
    for (uint32_t i = 0; i < s->channel_count; ++i) {
        size_t patch_bytes = (size_t)s->channels[i].patch_count * 8u;
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->channels[i].patches_raw,
                                                  s->channels[i].patches_raw, patch_bytes));
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->channels[i].uvs,
                                                  s->channels[i].uvs, sizeof(nmo_vector2_t),
                                                  s->channels[i].uv_count));
    }
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->legacy_patch_data,
                                              s->legacy_patch_data, s->legacy_patch_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->legacy_edge_data,
                                              s->legacy_edge_data, s->legacy_edge_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->legacy_tvpatch_data,
                                              s->legacy_tvpatch_data, s->legacy_tvpatch_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->legacy_uv_data,
                                              s->legacy_uv_data, s->legacy_uv_data_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->legacy_smoothing_groups,
                                              s->legacy_smoothing_groups, sizeof(uint32_t),
                                              s->legacy_smoothing_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->legacy_materials,
                                              s->legacy_materials, sizeof(nmo_ref_t),
                                              s->legacy_material_count));
    NMO_RETURN_OK();
}

static nmo_status_t nmo_patchmesh_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_patchmesh_state_t *s = instance;
    if (!s) return NMO_ERR_INVALID_ARGUMENT;
    if (s->format != CKPATCHMESH_FORMAT_NONE &&
        s->format != CKPATCHMESH_FORMAT_DATA2 &&
        s->format != CKPATCHMESH_FORMAT_DATA3) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Unknown CKPatchMesh format");
    }
    if (s->total_count > UINT32_MAX / sizeof(nmo_vector_t) ||
        s->patch_count > INT32_MAX || s->channel_count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "CKPatchMesh count exceeds serialized limits");
    }
    NMO_VALIDATE_COUNT(s->vectors, s->total_count, "vectors");
    NMO_VALIDATE_COUNT(s->patches, s->patch_count, "patches");
    if (s->edge_count > 0) {
        if (s->edge_count > UINT32_MAX / 12u) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        size_t expected = (size_t)s->edge_count * 12u;
        if (!s->edge_data || s->edge_data_size != expected) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "edge_data size mismatch");
        }
    } else if (s->edge_data_size != 0) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->channels, s->channel_count, "channels");
    if (s->channels) {
        for (uint32_t i = 0; i < s->channel_count; ++i) {
            if (s->channels[i].patch_count > 0) {
                if (s->channels[i].patch_count > UINT32_MAX / 8u) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
                NMO_VALIDATE_BYTES(s->channels[i].patches_raw,
                                   (size_t)s->channels[i].patch_count * 8u,
                                   "channels.patches_raw");
            }
            if (s->channels[i].uv_count > 0) {
                if (s->channels[i].uv_count >
                    UINT32_MAX / sizeof(nmo_vector2_t)) {
                    return NMO_ERR_VALIDATION_FAILED;
                }
                NMO_VALIDATE_COUNT(s->channels[i].uvs,
                                   s->channels[i].uv_count,
                                   "channels.uvs");
            }
        }
    }
    NMO_VALIDATE_BYTES(s->legacy_patch_data, s->legacy_patch_data_size, "legacy_patch_data");
    NMO_VALIDATE_BYTES(s->legacy_edge_data, s->legacy_edge_data_size, "legacy_edge_data");
    NMO_VALIDATE_BYTES(s->legacy_tvpatch_data, s->legacy_tvpatch_data_size, "legacy_tvpatch_data");
    NMO_VALIDATE_BYTES(s->legacy_uv_data, s->legacy_uv_data_size, "legacy_uv_data");
    NMO_VALIDATE_COUNT(s->legacy_smoothing_groups, s->legacy_smoothing_count,
                       "legacy_smoothing_groups");
    NMO_VALIDATE_COUNT(s->legacy_materials, s->legacy_material_count, "legacy_materials");

    if (s->format == CKPATCHMESH_FORMAT_DATA2) {
        if (s->legacy_patch_count > UINT32_MAX / 88u ||
            s->legacy_edge_count > UINT32_MAX / 24u ||
            s->legacy_tvpatch_count > UINT32_MAX / 16u ||
            s->legacy_uv_count > UINT32_MAX / sizeof(nmo_vector2_t) ||
            s->legacy_smoothing_count > UINT32_MAX / sizeof(uint32_t) ||
            s->legacy_material_count > INT32_MAX ||
            s->legacy_patch_data_size > UINT32_MAX ||
            s->legacy_edge_data_size > UINT32_MAX ||
            s->legacy_tvpatch_data_size > UINT32_MAX ||
            s->legacy_uv_data_size > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "CKPatchMesh DATA2 count exceeds serialized limits");
        }
        if (s->legacy_patch_data_size != (size_t)s->legacy_patch_count * 88u ||
            s->legacy_edge_data_size != (size_t)s->legacy_edge_count * 24u ||
            s->legacy_tvpatch_data_size != (size_t)s->legacy_tvpatch_count * 16u ||
            s->legacy_uv_data_size !=
                (size_t)s->legacy_uv_count * sizeof(nmo_vector2_t)) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "CKPatchMesh DATA2 buffer size mismatch");
        }
        if ((s->legacy_patch_count > 0 && !s->legacy_patch_data) ||
            (s->legacy_edge_count > 0 && !s->legacy_edge_data) ||
            (s->legacy_tvpatch_count > 0 && !s->legacy_tvpatch_data) ||
            (s->legacy_uv_count > 0 && !s->legacy_uv_data)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_patchmesh_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_patchmesh_validate(instance, type, context);
}

nmo_status_t nmo_patchmesh_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_patchmesh_remap_dependencies");
    }

    nmo_patchmesh_state_t *state = (nmo_patchmesh_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_mesh_remap_dependencies(&state->base, NULL, context));

    if (state->channel_count > 0 && state->channels == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing patch channels");
    }
    if (state->legacy_material_count > 0 && state->legacy_materials == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing legacy materials");
    }

    return nmo_patchmesh_validate(state, NULL, NULL);
}

static nmo_status_t nmo_patchmesh_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_patchmesh_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_patchmesh_validate(state, NULL, NULL));

    if (state->format == CKPATCHMESH_FORMAT_DATA3) {
        for (uint32_t i = 0; i < state->patch_count; ++i) {
            const nmo_object_id_t id = nmo_ref_runtime_id(
                &state->patches[i].material);
            if (id != NMO_OBJECT_ID_NONE &&
                !visitor(user_data, id, 0, "patches.material", i)) {
                return NMO_OK;
            }
        }
        for (uint32_t i = 0; i < state->channel_count; ++i) {
            const nmo_object_id_t id = nmo_ref_runtime_id(
                &state->channels[i].material);
            if (id != NMO_OBJECT_ID_NONE &&
                !visitor(user_data, id, 0, "channels.material", i)) {
                return NMO_OK;
            }
        }
        return NMO_OK;
    }

    if (state->format == CKPATCHMESH_FORMAT_DATA2) {
        nmo_object_id_t id = nmo_ref_runtime_id(
            &state->legacy_default_material);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "legacy_default_material", 0)) {
            return NMO_OK;
        }
        for (uint32_t i = 0; i < state->legacy_material_count; ++i) {
            id = nmo_ref_runtime_id(&state->legacy_materials[i]);
            if (id != NMO_OBJECT_ID_NONE &&
                !visitor(user_data, id, 0, "legacy_materials", i)) {
                return NMO_OK;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_patchmesh_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_patchmesh_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_patchmesh_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_patchmesh_canonical_bytes(
    const nmo_patchmesh_state_t *state,
    nmo_arena_t **out_arena,
    void **out_data,
    size_t *out_size)
{
    if (!state || !out_arena || !out_data || !out_size) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_arena = NULL;
    *out_data = NULL;
    *out_size = 0;

    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    if (!arena) return NMO_ERR_NOMEM;
    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    nmo_chunk_t *modern = nmo_chunk_create(arena);
    if (!legacy || !modern) {
        nmo_arena_destroy(arena);
        return NMO_ERR_NOMEM;
    }
    legacy->class_id = NMO_CID_PATCHMESH;
    legacy->chunk_version = NMO_CHUNK_VERSION4;
    legacy->data_version = 7;
    legacy->chunk_options = NMO_CHUNK_OPTION_FILE;
    modern->class_id = NMO_CID_PATCHMESH;
    modern->chunk_version = NMO_CHUNK_VERSION4;
    modern->data_version = 9;
    modern->chunk_options = NMO_CHUNK_OPTION_FILE;

    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_status_t result = nmo_patchmesh_serialize(
        state, legacy, NULL, &serialize_context);
    if (result == NMO_OK) {
        nmo_chunk_close(legacy);
        result = nmo_patchmesh_serialize(
            state, modern, NULL, &serialize_context);
    }

    void *legacy_data = NULL;
    void *modern_data = NULL;
    size_t legacy_size = 0;
    size_t modern_size = 0;
    if (result == NMO_OK) {
        nmo_chunk_close(modern);
        result = nmo_chunk_serialize_version1(
            legacy, &legacy_data, &legacy_size, arena);
    }
    if (result == NMO_OK) {
        result = nmo_chunk_serialize_version1(
            modern, &modern_data, &modern_size, arena);
    }
    if (result == NMO_OK) {
        const size_t header_size = 2u * sizeof(size_t);
        if (legacy_size > SIZE_MAX - header_size ||
            modern_size > SIZE_MAX - header_size - legacy_size) {
            result = NMO_ERR_NOMEM;
        } else {
            *out_size = header_size + legacy_size + modern_size;
            uint8_t *combined = nmo_arena_alloc(
                arena, *out_size, _Alignof(size_t));
            if (!combined) {
                result = NMO_ERR_NOMEM;
            } else {
                memcpy(combined, &legacy_size, sizeof(legacy_size));
                memcpy(combined + sizeof(legacy_size),
                       &modern_size, sizeof(modern_size));
                memcpy(combined + header_size, legacy_data, legacy_size);
                memcpy(combined + header_size + legacy_size,
                       modern_data, modern_size);
                *out_data = combined;
            }
        }
    }
    if (result != NMO_OK) {
        nmo_arena_destroy(arena);
        return result;
    }
    *out_arena = arena;
    return NMO_OK;
}

static bool nmo_patchmesh_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    nmo_arena_t *arena_a = NULL;
    nmo_arena_t *arena_b = NULL;
    void *data_a = NULL;
    void *data_b = NULL;
    size_t size_a = 0;
    size_t size_b = 0;
    const nmo_status_t result_a = nmo_patchmesh_canonical_bytes(
        a, &arena_a, &data_a, &size_a);
    const nmo_status_t result_b = nmo_patchmesh_canonical_bytes(
        b, &arena_b, &data_b, &size_b);
    const bool equal = result_a == NMO_OK && result_b == NMO_OK &&
        size_a == size_b &&
        (size_a == 0 || memcmp(data_a, data_b, size_a) == 0);
    nmo_arena_destroy(arena_a);
    nmo_arena_destroy(arena_b);
    return equal;
}

static uint32_t nmo_patchmesh_hash(const void *instance)
{
    if (!instance) return 0;
    nmo_arena_t *arena = NULL;
    void *data = NULL;
    size_t size = 0;
    if (nmo_patchmesh_canonical_bytes(
            instance, &arena, &data, &size) != NMO_OK) {
        return 0;
    }
    const uint32_t hash = (uint32_t)nmo_hash_fnv1a(data, size);
    nmo_arena_destroy(arena);
    return hash;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_patchmesh_vtable = {
    .prepare_dependencies = nmo_patchmesh_prepare_dependencies,
    .remap_dependencies = nmo_patchmesh_remap_dependencies,
    .pre_delete = nmo_patchmesh_pre_delete,
    .post_delete = nmo_patchmesh_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_patchmesh_create,
        nmo_patchmesh_destroy,
        nmo_patchmesh_serialize,
        nmo_patchmesh_deserialize,
        nmo_patchmesh_copy,
        nmo_patchmesh_validate,
        nmo_patchmesh_equals,
        nmo_patchmesh_hash,
        nmo_patchmesh_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_patchmesh_type,
    CKPGUID_PATCHMESH,
    "CKPatchMesh",
    NMO_CID_PATCHMESH,
    CKPGUID_MESH,
    nmo_patchmesh_state_t,
    &nmo_patchmesh_vtable,
    nmo_patchmesh_fields)

static nmo_status_t nmo_patchmesh_serialize_internal(
    const nmo_patchmesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_status_t result;
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_patchmesh_serialize");
    }

    NMO_RETURN_IF_ERROR(nmo_patchmesh_validate(in_state, NULL, NULL));

    {
        nmo_status_t result = nmo_mesh_serialize_ex(&in_state->base, out_chunk, NULL, context, true);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (!is_file && (save_flags & CK_STATESAVE_PATCHMESHONLY) == 0) {
        NMO_RETURN_OK();
    }

    nmo_patchmesh_format_t format = in_state->format;
    if (format == CKPATCHMESH_FORMAT_DATA3) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHDATA3);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->patch_flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->iteration_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->vec_count);
        if (result != NMO_OK) return result;

        uint32_t total_count = in_state->total_count;
        uint32_t buffer_size = total_count * (uint32_t)sizeof(nmo_vector_t);
        result = nmo_chunk_write_dword(out_chunk, buffer_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, total_count);
        if (result != NMO_OK) return result;
        if (buffer_size > 0 && in_state->vectors) {
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->vectors, buffer_size);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->patch_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->patch_count; ++i) {
            result = nmo_ref_write_sequence_item(
                out_chunk, &in_state->patches[i].material);
            if (result != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < in_state->patch_count; ++i) {
            result = nmo_chunk_write_dword(out_chunk, in_state->patches[i].patch.type);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->patches[i].patch.smoothing_group);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(
                out_chunk, in_state->patches[i].patch.data,
                sizeof(in_state->patches[i].patch.data));
            if (result != NMO_OK) return result;
        }

        uint32_t edge_bytes = in_state->edge_count * 12u;
        result = nmo_chunk_write_dword(out_chunk, edge_bytes);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->edge_count);
        if (result != NMO_OK) return result;
        if (edge_bytes > 0) {
            if (!in_state->edge_data || in_state->edge_data_size < edge_bytes) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Edge data buffer too small");
            }
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->edge_data, edge_bytes);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->channel_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->channel_count; ++i) {
            result = nmo_ref_write_sequence_item(
                out_chunk, &in_state->channels[i].material);
            if (result != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < in_state->channel_count; ++i) {
            const nmo_patchmesh_channel_t *channel = &in_state->channels[i];
            result = nmo_chunk_write_dword(out_chunk, channel->flags);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->type);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->subtype);
            if (result != NMO_OK) return result;

            uint32_t patches_bytes = channel->patch_count * 8u;
            result = nmo_chunk_write_dword(out_chunk, patches_bytes);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->patch_count);
            if (result != NMO_OK) return result;
            if (patches_bytes > 0 && channel->patches_raw) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, channel->patches_raw, patches_bytes);
                if (result != NMO_OK) return result;
            }

            uint32_t uv_bytes = channel->uv_count * (uint32_t)sizeof(nmo_vector2_t);
            result = nmo_chunk_write_dword(out_chunk, uv_bytes);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->uv_count);
            if (result != NMO_OK) return result;
            if (uv_bytes > 0 && channel->uvs) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, channel->uvs, uv_bytes);
                if (result != NMO_OK) return result;
            }
        }

        NMO_RETURN_OK();
    }

    if (format == CKPATCHMESH_FORMAT_DATA2) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHDATA2);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->patch_flags);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->legacy_default_material);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->iteration_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->vec_count);
        if (result != NMO_OK) return result;

        uint32_t total_count = in_state->total_count;
        uint32_t buffer_size = total_count * (uint32_t)sizeof(nmo_vector_t);
        result = nmo_chunk_write_dword(out_chunk, buffer_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, total_count);
        if (result != NMO_OK) return result;
        if (buffer_size > 0 && in_state->vectors) {
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->vectors, buffer_size);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_patch_data_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_patch_count);
        if (result != NMO_OK) return result;
        if (in_state->legacy_patch_data_size > 0 && in_state->legacy_patch_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_patch_data,
                                                    in_state->legacy_patch_data_size);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_edge_data_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_edge_count);
        if (result != NMO_OK) return result;
        if (in_state->legacy_edge_data_size > 0 && in_state->legacy_edge_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_edge_data,
                                                    in_state->legacy_edge_data_size);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_tvpatch_data_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_tvpatch_count);
        if (result != NMO_OK) return result;
        if (in_state->legacy_tvpatch_data_size > 0 && in_state->legacy_tvpatch_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_tvpatch_data,
                                                    in_state->legacy_tvpatch_data_size);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_uv_data_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_uv_count);
        if (result != NMO_OK) return result;
        if (in_state->legacy_uv_data_size > 0 && in_state->legacy_uv_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_uv_data,
                                                    in_state->legacy_uv_data_size);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_legacy_smoothing ||
            in_state->legacy_smoothing_count > 0) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHSMOOTH);
            if (result != NMO_OK) return result;
            uint32_t smooth_bytes = in_state->legacy_smoothing_count * (uint32_t)sizeof(uint32_t);
            result = nmo_chunk_write_dword(out_chunk, smooth_bytes);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->legacy_smoothing_count);
            if (result != NMO_OK) return result;
            if (smooth_bytes > 0) {
                result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                        in_state->legacy_smoothing_groups,
                                                        smooth_bytes);
                if (result != NMO_OK) return result;
            }
        }

        if (in_state->has_legacy_materials ||
            in_state->legacy_material_count > 0) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHMATERIALS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->legacy_material_count);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->legacy_material_count; ++i) {
                result = nmo_ref_write_sequence_item(
                    out_chunk, &in_state->legacy_materials[i]);
                if (result != NMO_OK) return result;
            }
        }

        NMO_RETURN_OK();
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_patchmesh_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_patchmesh_state_t *out_state = (nmo_patchmesh_state_t *)instance;
    return nmo_patchmesh_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_patchmesh_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_patchmesh_state_t *in_state = (const nmo_patchmesh_state_t *)instance;
    if (!in_state || !out_chunk || !out_chunk->arena) {
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

    nmo_status_t result = nmo_patchmesh_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;

    *out_chunk = *staged;
    return NMO_OK;
}
