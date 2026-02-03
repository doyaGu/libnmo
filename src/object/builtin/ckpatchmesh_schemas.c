/**
 * @file ckpatchmesh_schemas.c
 * @brief CKPatchMesh schema implementation
 */

#include "object/nmo_ckpatchmesh_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_PATCHMESHDATA2     0x01000000u
#define CK_STATESAVE_PATCHMESHSMOOTH    0x02000000u
#define CK_STATESAVE_PATCHMESHMATERIALS 0x04000000u
#define CK_STATESAVE_PATCHMESHDATA3     0x08000000u

static nmo_status_t nmo_ckpatchmesh_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckpatchmesh_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckpatchmesh_deserialize");
    }

    memset(out_state, 0, sizeof(*out_state));

    {
        nmo_status_t result = nmo_ckmesh_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA3) == NMO_OK) {
        out_state->format = NMO_PATCHMESH_FORMAT_DATA3;

        (void)nmo_chunk_read_dword(chunk, &out_state->patch_flags);
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->vectors, buffer_size);
        }

        size_t patch_count = 0;
        if (nmo_chunk_read_object_sequence_start(chunk, &patch_count) == NMO_OK && patch_count > 0) {
            out_state->patch_count = (uint32_t)patch_count;
            out_state->patch_material_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, sizeof(nmo_object_id_t) * out_state->patch_count,
                _Alignof(nmo_object_id_t));
            out_state->patches = (nmo_ckpatchmesh_patch_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckpatchmesh_patch_t) * out_state->patch_count,
                _Alignof(nmo_ckpatchmesh_patch_t));

            if (!out_state->patch_material_ids || !out_state->patches) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh patches");
            }

            for (uint32_t i = 0; i < out_state->patch_count; ++i) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->patch_material_ids[i]);
            }

            for (uint32_t i = 0; i < out_state->patch_count; ++i) {
                (void)nmo_chunk_read_dword(chunk, &out_state->patches[i].type);
                (void)nmo_chunk_read_dword(chunk, &out_state->patches[i].smoothing_group);
                (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->patches[i].data, sizeof(out_state->patches[i].data));
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->edge_data, edge_bytes);
        }

        size_t channel_count = 0;
        if (nmo_chunk_read_object_sequence_start(chunk, &channel_count) == NMO_OK && channel_count > 0) {
            out_state->channel_count = (uint32_t)channel_count;
            out_state->channels = (nmo_ckpatchmesh_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckpatchmesh_channel_t) * out_state->channel_count,
                _Alignof(nmo_ckpatchmesh_channel_t));
            if (!out_state->channels) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate patchmesh channels");
            }
            memset(out_state->channels, 0, sizeof(nmo_ckpatchmesh_channel_t) * out_state->channel_count);

            for (uint32_t i = 0; i < out_state->channel_count; ++i) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->channels[i].material_id);
            }

            for (uint32_t i = 0; i < out_state->channel_count; ++i) {
                nmo_ckpatchmesh_channel_t *channel = &out_state->channels[i];
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
                    (void)nmo_chunk_read_and_fill_buffer(chunk, channel->patches_raw, patches_bytes);
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
                    (void)nmo_chunk_read_and_fill_buffer(chunk, channel->uvs, uv_bytes);
                }
            }
        }

        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA2) == NMO_OK) {
        out_state->format = NMO_PATCHMESH_FORMAT_DATA2;

        (void)nmo_chunk_read_dword(chunk, &out_state->patch_flags);
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->vectors, buffer_size);
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->legacy_patch_data, patch_bytes);
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->legacy_edge_data, edge_bytes);
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->legacy_tvpatch_data, tv_bytes);
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
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->legacy_uv_data, uv_bytes);
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
                (void)nmo_chunk_read_and_fill_buffer(chunk,
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

        NMO_RETURN_OK();
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckpatchmesh,
    nmo_ckpatchmesh_state_t,
    nmo_ckpatchmesh_serialize,
    nmo_ckpatchmesh_deserialize,
    NMO_GUID_CKPATCHMESH,
    "CKPatchMesh",
    NMO_CID_PATCHMESH,
    NMO_GUID_CKMESH
)

static nmo_status_t nmo_ckpatchmesh_serialize_internal(
    const nmo_ckpatchmesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_status_t result;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckpatchmesh_serialize");
    }

    {
        nmo_status_t result = nmo_ckmesh_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (in_state->format == NMO_PATCHMESH_FORMAT_DATA3) {
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

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->edge_data_size);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->edge_count);
        if (result != NMO_OK) return result;
        if (in_state->edge_data_size > 0 && in_state->edge_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->edge_data, in_state->edge_data_size);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->channel_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->channel_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->channels[i].material_id);
            if (result != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < in_state->channel_count; ++i) {
            const nmo_ckpatchmesh_channel_t *channel = &in_state->channels[i];
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

    if (in_state->format == NMO_PATCHMESH_FORMAT_DATA2) {
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

nmo_status_t nmo_ckpatchmesh_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckpatchmesh_state_t *out_state = (nmo_ckpatchmesh_state_t *)instance;
    return nmo_ckpatchmesh_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_ckpatchmesh_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckpatchmesh_state_t *in_state = (const nmo_ckpatchmesh_state_t *)instance;
    return nmo_ckpatchmesh_serialize_internal(in_state, out_chunk, context);
}
