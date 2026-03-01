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
#include "type/nmo_reflection.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_object_struct_guids.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(patchmesh, nmo_patchmesh_state_t)

#ifndef CK_PATCHMESH_UPTODATE
#define CK_PATCHMESH_UPTODATE          0x00000001u
#define CK_PATCHMESH_BUILDNORMALS      0x00000002u
#define CK_PATCHMESH_MATERIALSUPTODATE 0x00000004u
#define CK_PATCHMESH_AUTOSMOOTH        0x00000008u
#endif

static inline uint32_t nmo_patchmesh_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static inline int16_t nmo_patchmesh_read_i16_le(const uint8_t *p) {
    return (int16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t nmo_patchmesh_apply_flags(uint32_t flags) {
    flags |= CK_PATCHMESH_BUILDNORMALS;
    flags &= ~(CK_PATCHMESH_UPTODATE | CK_PATCHMESH_MATERIALSUPTODATE);
    return flags;
}

static nmo_status_t nmo_patchmesh_convert_legacy_to_data3(
    nmo_patchmesh_state_t *out_state,
    nmo_arena_t *arena)
{
    if (!out_state || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to legacy patchmesh conversion");
    }

    if (out_state->legacy_patch_count > 0) {
        size_t expected_bytes = (size_t)out_state->legacy_patch_count * 88u;
        if (!out_state->legacy_patch_data || out_state->legacy_patch_data_size < expected_bytes) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Legacy patch data size mismatch");
        }
        out_state->patch_count = out_state->legacy_patch_count;
        out_state->patch_material_ids = (nmo_object_id_t *)nmo_arena_alloc(
            arena, sizeof(nmo_object_id_t) * out_state->patch_count, _Alignof(nmo_object_id_t));
        out_state->patches = (nmo_patchmesh_patch_t *)nmo_arena_alloc(
            arena, sizeof(nmo_patchmesh_patch_t) * out_state->patch_count, _Alignof(nmo_patchmesh_patch_t));
        if (!out_state->patch_material_ids || !out_state->patches) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy patches");
        }

        memset(out_state->patches, 0, sizeof(nmo_patchmesh_patch_t) * out_state->patch_count);

        const uint8_t *raw = (const uint8_t *)out_state->legacy_patch_data;
        for (uint32_t i = 0; i < out_state->patch_count; ++i) {
            const uint8_t *rec = raw + (size_t)i * 88u;
            nmo_patchmesh_patch_t *patch = &out_state->patches[i];
            patch->type = nmo_patchmesh_read_u32_le(rec + 0);
            patch->smoothing_group = 0xFFFFFFFFu;
            out_state->patch_material_ids[i] = out_state->legacy_default_material_id;

            int16_t data16[20] = {0};
            data16[0] = nmo_patchmesh_read_i16_le(rec + 4);
            data16[1] = nmo_patchmesh_read_i16_le(rec + 8);
            data16[2] = nmo_patchmesh_read_i16_le(rec + 12);
            data16[3] = nmo_patchmesh_read_i16_le(rec + 16);
            data16[4] = nmo_patchmesh_read_i16_le(rec + 20);
            data16[5] = nmo_patchmesh_read_i16_le(rec + 24);
            data16[6] = nmo_patchmesh_read_i16_le(rec + 28);
            data16[7] = nmo_patchmesh_read_i16_le(rec + 32);
            data16[8] = nmo_patchmesh_read_i16_le(rec + 36);
            data16[9] = nmo_patchmesh_read_i16_le(rec + 40);
            data16[10] = nmo_patchmesh_read_i16_le(rec + 44);
            data16[11] = nmo_patchmesh_read_i16_le(rec + 48);
            data16[12] = nmo_patchmesh_read_i16_le(rec + 52);
            data16[13] = nmo_patchmesh_read_i16_le(rec + 56);
            data16[14] = nmo_patchmesh_read_i16_le(rec + 60);
            data16[15] = nmo_patchmesh_read_i16_le(rec + 64);
            memcpy(patch->data, data16, sizeof(data16));
        }
    }

    if (out_state->legacy_edge_count > 0) {
        size_t expected_bytes = (size_t)out_state->legacy_edge_count * 24u;
        if (!out_state->legacy_edge_data || out_state->legacy_edge_data_size < expected_bytes) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Legacy edge data size mismatch");
        }
        out_state->edge_count = out_state->legacy_edge_count;
        out_state->edge_data_size = (size_t)out_state->edge_count * 12u;
        out_state->edge_data = (uint8_t *)nmo_arena_alloc(arena, out_state->edge_data_size, 1);
        if (!out_state->edge_data) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate edge data");
        }

        const uint8_t *raw = (const uint8_t *)out_state->legacy_edge_data;
        for (uint32_t i = 0; i < out_state->edge_count; ++i) {
            const uint8_t *rec = raw + (size_t)i * 24u;
            int16_t edge16[6];
            edge16[0] = nmo_patchmesh_read_i16_le(rec + 0);
            edge16[1] = nmo_patchmesh_read_i16_le(rec + 4);
            edge16[2] = nmo_patchmesh_read_i16_le(rec + 8);
            edge16[3] = nmo_patchmesh_read_i16_le(rec + 12);
            edge16[4] = nmo_patchmesh_read_i16_le(rec + 16);
            edge16[5] = nmo_patchmesh_read_i16_le(rec + 20);
            memcpy(out_state->edge_data + (size_t)i * 12u, edge16, sizeof(edge16));
        }
    }

    out_state->channel_count = 1;
    out_state->channels = (nmo_patchmesh_channel_t *)nmo_arena_alloc(
        arena, sizeof(nmo_patchmesh_channel_t) * out_state->channel_count, _Alignof(nmo_patchmesh_channel_t));
    if (!out_state->channels) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy channels");
    }
    memset(out_state->channels, 0, sizeof(nmo_patchmesh_channel_t) * out_state->channel_count);

    nmo_patchmesh_channel_t *channel = &out_state->channels[0];
    channel->material_id = NMO_OBJECT_ID_NONE;
    channel->flags = 0;
    channel->type = 0;
    channel->subtype = 0;

    if (out_state->legacy_tvpatch_count > 0) {
        size_t expected_bytes = (size_t)out_state->legacy_tvpatch_count * 16u;
        if (!out_state->legacy_tvpatch_data || out_state->legacy_tvpatch_data_size < expected_bytes) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Legacy TVPatch data size mismatch");
        }
        channel->patch_count = out_state->legacy_tvpatch_count;
        size_t patches_bytes = (size_t)channel->patch_count * 8u;
        channel->patches_raw = (uint8_t *)nmo_arena_alloc(arena, patches_bytes, 1);
        if (!channel->patches_raw) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate TVPatch buffer");
        }

        const uint8_t *raw = (const uint8_t *)out_state->legacy_tvpatch_data;
        for (uint32_t i = 0; i < channel->patch_count; ++i) {
            const uint8_t *rec = raw + (size_t)i * 16u;
            int16_t tv[4];
            tv[0] = nmo_patchmesh_read_i16_le(rec + 0);
            tv[1] = nmo_patchmesh_read_i16_le(rec + 4);
            tv[2] = nmo_patchmesh_read_i16_le(rec + 8);
            tv[3] = nmo_patchmesh_read_i16_le(rec + 12);
            memcpy(channel->patches_raw + (size_t)i * 8u, tv, sizeof(tv));
        }
    }

    if (out_state->legacy_uv_count > 0) {
        size_t expected_bytes = (size_t)out_state->legacy_uv_count * sizeof(nmo_vector2_t);
        if (!out_state->legacy_uv_data || out_state->legacy_uv_data_size < expected_bytes) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Legacy UV data size mismatch");
        }
        channel->uv_count = out_state->legacy_uv_count;
        channel->uvs = (nmo_vector2_t *)nmo_arena_alloc(
            arena, sizeof(nmo_vector2_t) * channel->uv_count, _Alignof(nmo_vector2_t));
        if (!channel->uvs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate UV buffer");
        }
        memcpy(channel->uvs, out_state->legacy_uv_data,
               (size_t)channel->uv_count * sizeof(nmo_vector2_t));
    }

    if (out_state->legacy_smoothing_count > 0 && out_state->legacy_smoothing_groups &&
        out_state->patches) {
        uint32_t apply = out_state->legacy_smoothing_count;
        if (out_state->patch_count < apply) apply = out_state->patch_count;
        for (uint32_t i = 0; i < apply; ++i) {
            out_state->patches[i].smoothing_group = out_state->legacy_smoothing_groups[i];
        }
    }

    if (out_state->legacy_material_count > 0 && out_state->legacy_material_ids &&
        out_state->patch_material_ids) {
        uint32_t apply = out_state->legacy_material_count;
        if (out_state->patch_count < apply) apply = out_state->patch_count;
        for (uint32_t i = 0; i < apply; ++i) {
            out_state->patch_material_ids[i] = out_state->legacy_material_ids[i];
        }
    }

    out_state->format = CKPATCHMESH_FORMAT_DATA3;
    return NMO_OK;
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
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, vectors, CKPGUID_VECTOR),
    NMO_FIELD(nmo_patchmesh_state_t, patch_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_patchmesh_state_t, patch_material_ids),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, patches, NMO_GUID_STRUCT_CKPATCHMESHPATCH),
    NMO_FIELD(nmo_patchmesh_state_t, edge_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, edge_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, edge_data_size, CKPGUID_UINT64),
    NMO_FIELD(nmo_patchmesh_state_t, channel_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, channels, NMO_GUID_STRUCT_CKPATCHMESHCHANNEL),
    NMO_FIELD_REF(nmo_patchmesh_state_t, legacy_default_material_id),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_patch_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, legacy_patch_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_patch_data_size, CKPGUID_UINT64),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_edge_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, legacy_edge_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_edge_data_size, CKPGUID_UINT64),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_tvpatch_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, legacy_tvpatch_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_tvpatch_data_size, CKPGUID_UINT64),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_uv_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, legacy_uv_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_uv_data_size, CKPGUID_UINT64),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_smoothing_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_patchmesh_state_t, legacy_smoothing_groups, CKPGUID_UINT32),
    NMO_FIELD(nmo_patchmesh_state_t, legacy_material_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_patchmesh_state_t, legacy_material_ids)
};

static nmo_status_t nmo_patchmesh_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_patchmesh_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_patchmesh_deserialize");
    }

    {
        nmo_status_t result = nmo_mesh_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA3) == NMO_OK) {
        out_state->format = CKPATCHMESH_FORMAT_DATA3;

        uint32_t patch_flags = 0;
        (void)nmo_chunk_read_dword(chunk, &patch_flags);
        out_state->patch_flags = nmo_patchmesh_apply_flags(patch_flags);
        (void)nmo_chunk_read_int(chunk, &out_state->iteration_count);
        (void)nmo_chunk_read_int(chunk, &out_state->vec_count);

        uint32_t buffer_size = 0;
        uint32_t total_count = 0;
        (void)nmo_chunk_read_dword(chunk, &buffer_size);
        (void)nmo_chunk_read_dword(chunk, &total_count);
        out_state->total_count = total_count;

        if (total_count > 0 && buffer_size > 0) {
            out_state->vectors = (nmo_vector_t *)nmo_arena_alloc(
                arena, sizeof(nmo_vector_t) * total_count, _Alignof(nmo_vector_t));
            if (!out_state->vectors) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh vectors");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->vectors, buffer_size);
        }

        size_t patch_count = 0;
        if (nmo_chunk_read_object_sequence_start(chunk, &patch_count) == NMO_OK && patch_count > 0) {
            out_state->patch_count = (uint32_t)patch_count;
            out_state->patch_material_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, sizeof(nmo_object_id_t) * out_state->patch_count,
                _Alignof(nmo_object_id_t));
            out_state->patches = (nmo_patchmesh_patch_t *)nmo_arena_alloc(
                arena, sizeof(nmo_patchmesh_patch_t) * out_state->patch_count,
                _Alignof(nmo_patchmesh_patch_t));

            if (!out_state->patch_material_ids || !out_state->patches) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh patches");
            }

            for (uint32_t i = 0; i < out_state->patch_count; ++i) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->patch_material_ids[i]);
            }

            for (uint32_t i = 0; i < out_state->patch_count; ++i) {
                (void)nmo_chunk_read_dword(chunk, &out_state->patches[i].type);
                (void)nmo_chunk_read_dword(chunk, &out_state->patches[i].smoothing_group);
                (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->patches[i].data, sizeof(out_state->patches[i].data));
            }
        }

        uint32_t edge_bytes = 0;
        uint32_t edge_count = 0;
        (void)nmo_chunk_read_dword(chunk, &edge_bytes);
        (void)nmo_chunk_read_dword(chunk, &edge_count);
        out_state->edge_count = edge_count;
        out_state->edge_data_size = edge_bytes;
        if (edge_bytes > 0) {
            out_state->edge_data = (uint8_t *)nmo_arena_alloc(arena, edge_bytes, 1);
            if (!out_state->edge_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh edge buffer");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->edge_data, edge_bytes);
        }

        size_t channel_count = 0;
        if (nmo_chunk_read_object_sequence_start(chunk, &channel_count) == NMO_OK && channel_count > 0) {
            out_state->channel_count = (uint32_t)channel_count;
            out_state->channels = (nmo_patchmesh_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_patchmesh_channel_t) * out_state->channel_count,
                _Alignof(nmo_patchmesh_channel_t));
            if (!out_state->channels) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh channels");
            }
            memset(out_state->channels, 0, sizeof(nmo_patchmesh_channel_t) * out_state->channel_count);

            for (uint32_t i = 0; i < out_state->channel_count; ++i) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->channels[i].material_id);
            }

            for (uint32_t i = 0; i < out_state->channel_count; ++i) {
                nmo_patchmesh_channel_t *channel = &out_state->channels[i];
                (void)nmo_chunk_read_dword(chunk, &channel->flags);
                (void)nmo_chunk_read_dword(chunk, &channel->type);
                (void)nmo_chunk_read_dword(chunk, &channel->subtype);

                uint32_t patches_bytes = 0;
                uint32_t patches_count = 0;
                (void)nmo_chunk_read_dword(chunk, &patches_bytes);
                (void)nmo_chunk_read_dword(chunk, &patches_count);
                channel->patch_count = patches_count;
                if (patches_bytes > 0) {
                    channel->patches_raw = (uint8_t *)nmo_arena_alloc(arena, patches_bytes, 1);
                    if (!channel->patches_raw) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate TVPatch buffer");
                    }
                    (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, channel->patches_raw, patches_bytes);
                }

                uint32_t uv_bytes = 0;
                uint32_t uv_count = 0;
                (void)nmo_chunk_read_dword(chunk, &uv_bytes);
                (void)nmo_chunk_read_dword(chunk, &uv_count);
                channel->uv_count = uv_count;
                if (uv_bytes > 0 && uv_count > 0) {
                    channel->uvs = (nmo_vector2_t *)nmo_arena_alloc(
                        arena, sizeof(nmo_vector2_t) * uv_count, _Alignof(nmo_vector2_t));
                    if (!channel->uvs) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate UV buffer");
                    }
                    (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, channel->uvs, uv_bytes);
                }
            }
        }

        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA2) == NMO_OK) {
        out_state->format = CKPATCHMESH_FORMAT_DATA2;

        uint32_t patch_flags = 0;
        (void)nmo_chunk_read_dword(chunk, &patch_flags);
        out_state->patch_flags = nmo_patchmesh_apply_flags(patch_flags);
        (void)nmo_chunk_read_object_id(chunk, &out_state->legacy_default_material_id);
        (void)nmo_chunk_read_int(chunk, &out_state->iteration_count);
        (void)nmo_chunk_read_int(chunk, &out_state->vec_count);

        uint32_t buffer_size = 0;
        uint32_t total_count = 0;
        (void)nmo_chunk_read_dword(chunk, &buffer_size);
        (void)nmo_chunk_read_dword(chunk, &total_count);
        out_state->total_count = total_count;

        if (total_count > 0 && buffer_size > 0) {
            out_state->vectors = (nmo_vector_t *)nmo_arena_alloc(
                arena, sizeof(nmo_vector_t) * total_count, _Alignof(nmo_vector_t));
            if (!out_state->vectors) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh vectors");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->vectors, buffer_size);
        }

        uint32_t patch_bytes = 0;
        uint32_t patch_count = 0;
        (void)nmo_chunk_read_dword(chunk, &patch_bytes);
        (void)nmo_chunk_read_dword(chunk, &patch_count);
        out_state->legacy_patch_count = patch_count;
        out_state->legacy_patch_data_size = patch_bytes;
        if (patch_bytes > 0) {
            out_state->legacy_patch_data = (uint8_t *)nmo_arena_alloc(arena, patch_bytes, 1);
            if (!out_state->legacy_patch_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy patch buffer");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->legacy_patch_data, patch_bytes);
        }

        uint32_t edge_bytes = 0;
        uint32_t edge_count = 0;
        (void)nmo_chunk_read_dword(chunk, &edge_bytes);
        (void)nmo_chunk_read_dword(chunk, &edge_count);
        out_state->legacy_edge_count = edge_count;
        out_state->legacy_edge_data_size = edge_bytes;
        if (edge_bytes > 0) {
            out_state->legacy_edge_data = (uint8_t *)nmo_arena_alloc(arena, edge_bytes, 1);
            if (!out_state->legacy_edge_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy edge buffer");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->legacy_edge_data, edge_bytes);
        }

        uint32_t tv_bytes = 0;
        uint32_t tv_count = 0;
        (void)nmo_chunk_read_dword(chunk, &tv_bytes);
        (void)nmo_chunk_read_dword(chunk, &tv_count);
        out_state->legacy_tvpatch_count = tv_count;
        out_state->legacy_tvpatch_data_size = tv_bytes;
        if (tv_bytes > 0) {
            out_state->legacy_tvpatch_data = (uint8_t *)nmo_arena_alloc(arena, tv_bytes, 1);
            if (!out_state->legacy_tvpatch_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy TVPatch buffer");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->legacy_tvpatch_data, tv_bytes);
        }

        uint32_t uv_bytes = 0;
        uint32_t uv_count = 0;
        (void)nmo_chunk_read_dword(chunk, &uv_bytes);
        (void)nmo_chunk_read_dword(chunk, &uv_count);
        out_state->legacy_uv_count = uv_count;
        out_state->legacy_uv_data_size = uv_bytes;
        if (uv_bytes > 0) {
            out_state->legacy_uv_data = (uint8_t *)nmo_arena_alloc(arena, uv_bytes, 1);
            if (!out_state->legacy_uv_data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy UV buffer");
            }
            (void)nmo_chunk_read_and_fill_buffer_nosize(chunk, out_state->legacy_uv_data, uv_bytes);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHSMOOTH) == NMO_OK) {
            uint32_t smooth_bytes = 0;
            uint32_t smooth_count = 0;
            (void)nmo_chunk_read_dword(chunk, &smooth_bytes);
            (void)nmo_chunk_read_dword(chunk, &smooth_count);
            out_state->legacy_smoothing_count = smooth_count;
            if (smooth_bytes > 0 && smooth_count > 0) {
                out_state->legacy_smoothing_groups = (uint32_t *)nmo_arena_alloc(
                    arena, sizeof(uint32_t) * smooth_count, _Alignof(uint32_t));
                if (!out_state->legacy_smoothing_groups) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate smoothing groups");
                }
                (void)nmo_chunk_read_and_fill_buffer_nosize(chunk,
                                                     out_state->legacy_smoothing_groups,
                                                     smooth_bytes);
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHMATERIALS) == NMO_OK) {
            size_t seq_count = 0;
            if (nmo_chunk_read_object_sequence_start(chunk, &seq_count) == NMO_OK && seq_count > 0) {
                out_state->legacy_material_count = (uint32_t)seq_count;
                out_state->legacy_material_ids = (nmo_object_id_t *)nmo_arena_alloc(
                    arena, sizeof(nmo_object_id_t) * out_state->legacy_material_count,
                    _Alignof(nmo_object_id_t));
                if (!out_state->legacy_material_ids) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate legacy material IDs");
                }

                for (uint32_t i = 0; i < out_state->legacy_material_count; ++i) {
                    (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->legacy_material_ids[i]);
                }
            }
        }

        return nmo_patchmesh_convert_legacy_to_data3(out_state, arena);
    }

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

    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.beobject.base.raw_tail,
                                              s->base.beobject.base.raw_tail, s->base.beobject.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.beobject.script_ids,
                                        &d->base.beobject.script_ids,
                                        &s->base.beobject.script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.beobject.attribute_parameter_ids,
                                        &d->base.beobject.attribute_parameter_ids,
                                        &s->base.beobject.attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.beobject.attribute_types,
                                        &d->base.beobject.attribute_types,
                                        &s->base.beobject.attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->base.beobject.attribute_chunks,
                                                     &s->base.beobject.attribute_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.beobject.legacy_attributes_raw,
                                        &d->base.beobject.legacy_attributes_raw,
                                        &s->base.beobject.legacy_attributes_raw.allocator));

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
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->patch_material_ids,
                                              s->patch_material_ids, sizeof(nmo_object_id_t), s->patch_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->patches,
                                              s->patches, sizeof(nmo_patchmesh_patch_t), s->patch_count));
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
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->legacy_material_ids,
                                              s->legacy_material_ids, sizeof(nmo_object_id_t),
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
    NMO_VALIDATE_COUNT(s->vectors, s->total_count, "vectors");
    NMO_VALIDATE_COUNT(s->patch_material_ids, s->patch_count, "patch_material_ids");
    NMO_VALIDATE_COUNT(s->patches, s->patch_count, "patches");
    if (s->edge_count > 0) {
        size_t expected = (size_t)s->edge_count * 12u;
        if (!s->edge_data || s->edge_data_size < expected) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "edge_data too small");
        }
    }
    NMO_VALIDATE_COUNT(s->channels, s->channel_count, "channels");
    if (s->channels) {
        for (uint32_t i = 0; i < s->channel_count; ++i) {
            if (s->channels[i].patch_count > 0) {
                NMO_VALIDATE_BYTES(s->channels[i].patches_raw,
                                   (size_t)s->channels[i].patch_count * 8u,
                                   "channels.patches_raw");
            }
            if (s->channels[i].uv_count > 0) {
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
    NMO_VALIDATE_COUNT(s->legacy_material_ids, s->legacy_material_count, "legacy_material_ids");
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
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_mesh_remap_dependencies(&state->base, NULL, context));

    if (state->patch_count > 0 && state->patch_material_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing patch material IDs");
    }
    if (state->channel_count > 0 && state->channels == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing patch channels");
    }
    if (state->legacy_material_count > 0 && state->legacy_material_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing legacy material IDs");
    }

    if (repo) {
        for (uint32_t i = 0; i < state->patch_count; ++i) {
            nmo_object_id_t id = state->patch_material_ids[i];
            if (id == NMO_OBJECT_ID_NONE) {
                continue;
            }
            if (nmo_object_repository_find_by_id(repo, id) == NULL) {
                state->patch_material_ids[i] = NMO_OBJECT_ID_NONE;
            }
        }

        for (uint32_t i = 0; i < state->channel_count; ++i) {
            nmo_patchmesh_channel_t *channel = &state->channels[i];
            if (channel->material_id == NMO_OBJECT_ID_NONE) {
                continue;
            }
            if (nmo_object_repository_find_by_id(repo, channel->material_id) == NULL) {
                channel->material_id = NMO_OBJECT_ID_NONE;
            }
        }

        if (state->legacy_default_material_id != NMO_OBJECT_ID_NONE) {
            if (nmo_object_repository_find_by_id(repo, state->legacy_default_material_id) == NULL) {
                state->legacy_default_material_id = NMO_OBJECT_ID_NONE;
            }
        }

        for (uint32_t i = 0; i < state->legacy_material_count; ++i) {
            nmo_object_id_t id = state->legacy_material_ids[i];
            if (id == NMO_OBJECT_ID_NONE) {
                continue;
            }
            if (nmo_object_repository_find_by_id(repo, id) == NULL) {
                state->legacy_material_ids[i] = NMO_OBJECT_ID_NONE;
            }
        }
    }

    NMO_RETURN_OK();
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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(patchmesh, nmo_patchmesh_state_t)

nmo_type_vtable_t nmo_patchmesh_vtable = {
    .prepare_dependencies = nmo_patchmesh_prepare_dependencies,
    .remap_dependencies = nmo_patchmesh_remap_dependencies,
    .pre_delete = nmo_patchmesh_pre_delete,
    .post_delete = nmo_patchmesh_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_patchmesh_create,
        nmo_patchmesh_destroy,
        nmo_patchmesh_serialize,
        nmo_patchmesh_deserialize,
        nmo_patchmesh_copy,
        nmo_patchmesh_validate,
        nmo_patchmesh_equals,
        nmo_patchmesh_hash)
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
    if (format != CKPATCHMESH_FORMAT_DATA3) {
        format = CKPATCHMESH_FORMAT_DATA3;
    }
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
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->patch_material_ids[i]);
            if (result != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < in_state->patch_count; ++i) {
            result = nmo_chunk_write_dword(out_chunk, in_state->patches[i].type);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->patches[i].smoothing_group);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->patches[i].data, sizeof(in_state->patches[i].data));
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
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->channels[i].material_id);
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
        result = nmo_chunk_write_object_id(out_chunk, in_state->legacy_default_material_id);
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

        if (in_state->legacy_smoothing_count > 0 && in_state->legacy_smoothing_groups) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHSMOOTH);
            if (result != NMO_OK) return result;
            uint32_t smooth_bytes = in_state->legacy_smoothing_count * (uint32_t)sizeof(uint32_t);
            result = nmo_chunk_write_dword(out_chunk, smooth_bytes);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->legacy_smoothing_count);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_smoothing_groups,
                                                    smooth_bytes);
            if (result != NMO_OK) return result;
        }

        if (in_state->legacy_material_count > 0 && in_state->legacy_material_ids) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHMATERIALS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->legacy_material_count);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->legacy_material_count; ++i) {
                result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->legacy_material_ids[i]);
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
    return nmo_patchmesh_serialize_internal(in_state, out_chunk, context);
}
