/**
 * @file ckgrid_schemas.c
 * @brief CKGrid schema implementation
 */

#include "object/nmo_ckgrid_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckgrid, nmo_ckgrid_state_t)

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
}

nmo_status_t nmo_ckgrid_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckgrid_state_t *out_state = (nmo_ckgrid_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgrid_deserialize");
    }

    nmo_status_t result = nmo_ck3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_GRIDDATA) != NMO_OK) {
        NMO_RETURN_OK();
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
        if (nmo_chunk_read_int(chunk, &file_flag) == NMO_OK) {
            out_state->has_file_flag = 1;
            out_state->file_flag = file_flag;
        }
    }

    size_t count = 0;
    result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result == NMO_OK && count > 0) {
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

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckgrid_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckgrid_state_t *in_state = (const nmo_ckgrid_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgrid_serialize");
    }

    nmo_status_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GRIDDATA);
    if (result != NMO_OK) return result;

    nmo_chunk_write_int(out_chunk, in_state->width);
    nmo_chunk_write_int(out_chunk, in_state->length);
    nmo_chunk_write_int(out_chunk, 0);
    nmo_chunk_write_int(out_chunk, in_state->priority);
    nmo_chunk_write_dword(out_chunk, in_state->orientation_mode);

    if (nmo_chunk_is_file_mode(out_chunk)) {
        nmo_chunk_write_int(out_chunk, in_state->has_file_flag ? in_state->file_flag : 1);
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->layer_count);
    if (result != NMO_OK) return result;

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

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_ckgrid_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckgrid_state_t, base),
                    sizeof(nmo_ck3dentity_state_t), NMO_GUID_FIELD_VOID,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckgrid_state_t, width, NMO_GUID_FIELD_INT32),
    NMO_FIELD(nmo_ckgrid_state_t, length, NMO_GUID_FIELD_INT32),
    NMO_FIELD(nmo_ckgrid_state_t, priority, NMO_GUID_FIELD_INT32),
    NMO_FIELD(nmo_ckgrid_state_t, orientation_mode, NMO_GUID_FIELD_UINT32),
    NMO_FIELD(nmo_ckgrid_state_t, has_file_flag, NMO_GUID_FIELD_UINT8),
    NMO_FIELD(nmo_ckgrid_state_t, file_flag, NMO_GUID_FIELD_INT32),
    NMO_FIELD_REF_ARRAY(nmo_ckgrid_state_t, layer_ids),
    NMO_FIELD(nmo_ckgrid_state_t, layer_count, NMO_GUID_FIELD_UINT32),
    NMO_FIELD(nmo_ckgrid_state_t, layer_chunk_count, NMO_GUID_FIELD_UINT32),
    NMO_FIELD_ARRAY(nmo_ckgrid_state_t, layer_chunks, NMO_GUID_FIELD_CHUNK)
};

static nmo_status_t ckgrid_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckgrid_state_t *s = src;
    nmo_ckgrid_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->layer_ids,
                                              s->layer_ids, sizeof(nmo_object_id_t), s->layer_count));
    return nmo_object_copy_chunk_array(arena, &d->layer_chunks,
                                       s->layer_chunks, s->layer_chunk_count);
}

static nmo_status_t ckgrid_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckgrid_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->layer_ids, s->layer_count, "layer_ids");
    NMO_VALIDATE_COUNT(s->layer_chunks, s->layer_chunk_count, "layer_chunks");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckgrid,
    nmo_ckgrid_state_t,
    nmo_ckgrid_serialize,
    nmo_ckgrid_deserialize,
    nmo_ckgrid_fields,
    NMO_GUID_CKGRID,
    "CKGrid",
    NMO_CID_GRID,
    NMO_GUID_CK3DENTITY
)

