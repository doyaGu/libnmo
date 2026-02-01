/**
 * @file ckpatchmesh_schemas.c
 * @brief CKPatchMesh schema implementation
 */

#include "object/nmo_ckpatchmesh_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
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

static nmo_result_t nmo_ckpatchmesh_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckpatchmesh_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckpatchmesh_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckmesh_deserialize_fn base_deserialize = nmo_get_ckmesh_deserialize();
    if (base_deserialize) {
        nmo_result_t result = base_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA3).code == NMO_OK) {
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate patchmesh vectors"));
            }
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->vectors, buffer_size);
        }

        size_t patch_count = 0;
        if (nmo_chunk_read_object_sequence_start(chunk, &patch_count).code == NMO_OK && patch_count > 0) {
            out_state->patch_count = (uint32_t)patch_count;
            out_state->patch_material_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, sizeof(nmo_object_id_t) * out_state->patch_count,
                _Alignof(nmo_object_id_t));
            out_state->patches = (nmo_ckpatchmesh_patch_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckpatchmesh_patch_t) * out_state->patch_count,
                _Alignof(nmo_ckpatchmesh_patch_t));

            if (!out_state->patch_material_ids || !out_state->patches) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate patchmesh patches"));
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate patchmesh edge buffer"));
            }
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->edge_data, edge_bytes);
        }

        size_t channel_count = 0;
        if (nmo_chunk_read_object_sequence_start(chunk, &channel_count).code == NMO_OK && channel_count > 0) {
            out_state->channel_count = (uint32_t)channel_count;
            out_state->channels = (nmo_ckpatchmesh_channel_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckpatchmesh_channel_t) * out_state->channel_count,
                _Alignof(nmo_ckpatchmesh_channel_t));
            if (!out_state->channels) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate patchmesh channels"));
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
                        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                            NMO_SEVERITY_ERROR, "Failed to allocate TVPatch buffer"));
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
                        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                            NMO_SEVERITY_ERROR, "Failed to allocate UV buffer"));
                    }
                    (void)nmo_chunk_read_and_fill_buffer(chunk, channel->uvs, uv_bytes);
                }
            }
        }

        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHDATA2).code == NMO_OK) {
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate patchmesh vectors"));
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate legacy patch buffer"));
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate legacy edge buffer"));
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate legacy TVPatch buffer"));
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
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate legacy UV buffer"));
            }
            (void)nmo_chunk_read_and_fill_buffer(chunk, out_state->legacy_uv_data, uv_bytes);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHSMOOTH).code == NMO_OK) {
            uint32_t smooth_bytes = 0;
            uint32_t smooth_count = 0;
            (void)nmo_chunk_read_dword(chunk, &smooth_bytes);
            (void)nmo_chunk_read_dword(chunk, &smooth_count);
            out_state->legacy_smoothing_count = smooth_count;
            if (smooth_bytes > 0 && smooth_count > 0) {
                out_state->legacy_smoothing_groups = (uint32_t *)nmo_arena_alloc(
                    arena, sizeof(uint32_t) * smooth_count, _Alignof(uint32_t));
                if (!out_state->legacy_smoothing_groups) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate smoothing groups"));
                }
                (void)nmo_chunk_read_and_fill_buffer(chunk,
                                                     out_state->legacy_smoothing_groups,
                                                     smooth_bytes);
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PATCHMESHMATERIALS).code == NMO_OK) {
            size_t seq_count = 0;
            if (nmo_chunk_read_object_sequence_start(chunk, &seq_count).code == NMO_OK && seq_count > 0) {
                out_state->legacy_material_count = (uint32_t)seq_count;
                out_state->legacy_material_ids = (nmo_object_id_t *)nmo_arena_alloc(
                    arena, sizeof(nmo_object_id_t) * out_state->legacy_material_count,
                    _Alignof(nmo_object_id_t));
                if (!out_state->legacy_material_ids) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate legacy material IDs"));
                }

                for (uint32_t i = 0; i < out_state->legacy_material_count; ++i) {
                    (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->legacy_material_ids[i]);
                }
            }
        }

        return nmo_result_ok();
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckpatchmesh_serialize_internal(
    const nmo_ckpatchmesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    nmo_result_t result;

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckpatchmesh_serialize"));
    }

    nmo_ckmesh_serialize_fn base_serialize = nmo_get_ckmesh_serialize();
    if (base_serialize) {
        nmo_result_t result = base_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (in_state->format == NMO_PATCHMESH_FORMAT_DATA3) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHDATA3);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->patch_flags);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->iteration_count);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->vec_count);
        if (result.code != NMO_OK) return result;

        uint32_t total_count = in_state->total_count;
        uint32_t buffer_size = total_count * (uint32_t)sizeof(nmo_vector_t);
        result = nmo_chunk_write_dword(out_chunk, buffer_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, total_count);
        if (result.code != NMO_OK) return result;
        if (buffer_size > 0 && in_state->vectors) {
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->vectors, buffer_size);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->patch_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->patch_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->patch_material_ids[i]);
            if (result.code != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < in_state->patch_count; ++i) {
            result = nmo_chunk_write_dword(out_chunk, in_state->patches[i].type);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->patches[i].smoothing_group);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->patches[i].data, sizeof(in_state->patches[i].data));
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->edge_data_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->edge_count);
        if (result.code != NMO_OK) return result;
        if (in_state->edge_data_size > 0 && in_state->edge_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->edge_data, in_state->edge_data_size);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->channel_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->channel_count; ++i) {
            result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->channels[i].material_id);
            if (result.code != NMO_OK) return result;
        }

        for (uint32_t i = 0; i < in_state->channel_count; ++i) {
            const nmo_ckpatchmesh_channel_t *channel = &in_state->channels[i];
            result = nmo_chunk_write_dword(out_chunk, channel->flags);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->type);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->subtype);
            if (result.code != NMO_OK) return result;

            uint32_t patches_bytes = channel->patch_count * 8u;
            result = nmo_chunk_write_dword(out_chunk, patches_bytes);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->patch_count);
            if (result.code != NMO_OK) return result;
            if (patches_bytes > 0 && channel->patches_raw) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, channel->patches_raw, patches_bytes);
                if (result.code != NMO_OK) return result;
            }

            uint32_t uv_bytes = channel->uv_count * (uint32_t)sizeof(nmo_vector2_t);
            result = nmo_chunk_write_dword(out_chunk, uv_bytes);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, channel->uv_count);
            if (result.code != NMO_OK) return result;
            if (uv_bytes > 0 && channel->uvs) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, channel->uvs, uv_bytes);
                if (result.code != NMO_OK) return result;
            }
        }

        return nmo_result_ok();
    }

    if (in_state->format == NMO_PATCHMESH_FORMAT_DATA2) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHDATA2);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_dword(out_chunk, in_state->patch_flags);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->legacy_default_material_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->iteration_count);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->vec_count);
        if (result.code != NMO_OK) return result;

        uint32_t total_count = in_state->total_count;
        uint32_t buffer_size = total_count * (uint32_t)sizeof(nmo_vector_t);
        result = nmo_chunk_write_dword(out_chunk, buffer_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, total_count);
        if (result.code != NMO_OK) return result;
        if (buffer_size > 0 && in_state->vectors) {
            result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->vectors, buffer_size);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_patch_data_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_patch_count);
        if (result.code != NMO_OK) return result;
        if (in_state->legacy_patch_data_size > 0 && in_state->legacy_patch_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_patch_data,
                                                    in_state->legacy_patch_data_size);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_edge_data_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_edge_count);
        if (result.code != NMO_OK) return result;
        if (in_state->legacy_edge_data_size > 0 && in_state->legacy_edge_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_edge_data,
                                                    in_state->legacy_edge_data_size);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_tvpatch_data_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_tvpatch_count);
        if (result.code != NMO_OK) return result;
        if (in_state->legacy_tvpatch_data_size > 0 && in_state->legacy_tvpatch_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_tvpatch_data,
                                                    in_state->legacy_tvpatch_data_size);
            if (result.code != NMO_OK) return result;
        }

        result = nmo_chunk_write_dword(out_chunk, (uint32_t)in_state->legacy_uv_data_size);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->legacy_uv_count);
        if (result.code != NMO_OK) return result;
        if (in_state->legacy_uv_data_size > 0 && in_state->legacy_uv_data) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_uv_data,
                                                    in_state->legacy_uv_data_size);
            if (result.code != NMO_OK) return result;
        }

        if (in_state->legacy_smoothing_count > 0 && in_state->legacy_smoothing_groups) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHSMOOTH);
            if (result.code != NMO_OK) return result;
            uint32_t smooth_bytes = in_state->legacy_smoothing_count * (uint32_t)sizeof(uint32_t);
            result = nmo_chunk_write_dword(out_chunk, smooth_bytes);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->legacy_smoothing_count);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    in_state->legacy_smoothing_groups,
                                                    smooth_bytes);
            if (result.code != NMO_OK) return result;
        }

        if (in_state->legacy_material_count > 0 && in_state->legacy_material_ids) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PATCHMESHMATERIALS);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->legacy_material_count);
            if (result.code != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->legacy_material_count; ++i) {
                result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->legacy_material_ids[i]);
                if (result.code != NMO_OK) return result;
            }
        }

        return nmo_result_ok();
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckpatchmesh_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckpatchmesh_deserialize_internal(chunk, arena, (nmo_ckpatchmesh_state_t *)out_ptr);
}

static nmo_result_t nmo_ckpatchmesh_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckpatchmesh_serialize_internal((const nmo_ckpatchmesh_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckpatchmesh_vtable = {
    .read = nmo_ckpatchmesh_vtable_read,
    .write = nmo_ckpatchmesh_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckpatchmesh_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckpatchmesh_schemas"));
    }

    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    if (!uint32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKPatchMeshState",
                                                      sizeof(nmo_ckpatchmesh_state_t),
                                                      alignof(nmo_ckpatchmesh_state_t));

    nmo_builder_add_field_ex(&builder, "patch_flags", uint32_type,
                            offsetof(nmo_ckpatchmesh_state_t, patch_flags), 0);
    nmo_builder_add_field_ex(&builder, "patch_count", uint32_type,
                            offsetof(nmo_ckpatchmesh_state_t, patch_count), 0);
    nmo_builder_add_field_ex(&builder, "edge_count", uint32_type,
                            offsetof(nmo_ckpatchmesh_state_t, edge_count), 0);
    nmo_builder_add_field_ex(&builder, "channel_count", uint32_type,
                            offsetof(nmo_ckpatchmesh_state_t, channel_count), 0);

    nmo_builder_set_vtable(&builder, &nmo_ckpatchmesh_vtable);

    return nmo_builder_build(&builder, registry);
}

nmo_result_t nmo_ckpatchmesh_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckpatchmesh_state_t *out_state)
{
    return nmo_ckpatchmesh_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckpatchmesh_serialize(
    const nmo_ckpatchmesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckpatchmesh_serialize_internal(in_state, out_chunk, arena);
}
