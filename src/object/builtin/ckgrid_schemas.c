/**
 * @file ckgrid_schemas.c
 * @brief CKGrid schema implementation
 */

#include "object/nmo_ckgrid_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_GRIDDATA 0x00400000u

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
}

nmo_result_t nmo_ckgrid_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckgrid_state_t *out_state = (nmo_ckgrid_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgrid_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_GRIDDATA).code != NMO_OK) {
        return nmo_result_ok();
    }

    nmo_chunk_read_int(chunk, &out_state->width);
    nmo_chunk_read_int(chunk, &out_state->length);
    {
        int32_t reserved = 0;
        nmo_chunk_read_int(chunk, &reserved);
    }
    nmo_chunk_read_int(chunk, &out_state->priority);
    nmo_chunk_read_dword(chunk, &out_state->orientation_mode);

    if (nmo_chunk_is_file_mode(chunk)) {
        int32_t file_flag = 0;
        if (nmo_chunk_read_int(chunk, &file_flag).code == NMO_OK) {
            out_state->has_file_flag = 1;
            out_state->file_flag = file_flag;
        }
    }

    size_t count = 0;
    result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result.code == NMO_OK && count > 0) {
        out_state->layer_ids = (nmo_object_id_t *)nmo_arena_alloc(
            arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));
        if (out_state->layer_ids) {
            out_state->layer_count = (uint32_t)count;
            for (size_t i = 0; i < count; ++i) {
                nmo_chunk_read_object_sequence_item(chunk, &out_state->layer_ids[i]);
            }
        }
    }

    if (!nmo_chunk_is_file_mode(chunk) && out_state->layer_count > 0) {
        out_state->layer_chunk_count = out_state->layer_count;
        out_state->layer_chunks = (nmo_chunk_t **)nmo_arena_alloc(
            arena, out_state->layer_chunk_count * sizeof(nmo_chunk_t *),
            _Alignof(nmo_chunk_t *));
        if (out_state->layer_chunks) {
            for (uint32_t i = 0; i < out_state->layer_chunk_count; ++i) {
                (void)nmo_chunk_read_sub_chunk(chunk, &out_state->layer_chunks[i]);
            }
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckgrid_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckgrid_state_t *in_state = (const nmo_ckgrid_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgrid_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GRIDDATA);
    if (result.code != NMO_OK) return result;

    nmo_chunk_write_int(out_chunk, in_state->width);
    nmo_chunk_write_int(out_chunk, in_state->length);
    nmo_chunk_write_int(out_chunk, 0);
    nmo_chunk_write_int(out_chunk, in_state->priority);
    nmo_chunk_write_dword(out_chunk, in_state->orientation_mode);

    if (nmo_chunk_is_file_mode(out_chunk)) {
        nmo_chunk_write_int(out_chunk, in_state->has_file_flag ? in_state->file_flag : 1);
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->layer_count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < in_state->layer_count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, in_state->layer_ids[i]);
    }

    if (!nmo_chunk_is_file_mode(out_chunk) && in_state->layer_count > 0) {
        for (uint32_t i = 0; i < in_state->layer_count; ++i) {
            nmo_chunk_t *sub = NULL;
            if (in_state->layer_chunks && i < in_state->layer_chunk_count) {
                sub = in_state->layer_chunks[i];
            }
            nmo_chunk_write_sub_chunk(out_chunk, sub);
        }
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckgrid,
    nmo_ckgrid_state_t,
    nmo_ckgrid_serialize,
    nmo_ckgrid_deserialize,
    NMO_GUID_CKGRID,
    "CKGrid",
    NMO_CID_GRID,
    NMO_GUID_CK3DENTITY
)

