/**
 * @file ckgrid_schemas.c
 * @brief CKGrid schema implementation
 */

#include "object/nmo_grid_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    grid,
    nmo_grid_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->layer_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->layer_chunks, sizeof(nmo_chunk_t *), 0, NULL);
        if (result != NMO_OK) return result;
        nmo_object_array_set_chunk_lifecycle(&state->layer_chunks);
    } while (0),
    ((void)0))

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
}

nmo_status_t nmo_grid_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_grid_state_t *out_state = (nmo_grid_state_t *)instance;
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_grid_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
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
    nmo_array_clear(&out_state->layer_ids);
    if (result == NMO_OK && count > 0) {
        result = nmo_array_reserve(&out_state->layer_ids, count);
        if (result != NMO_OK) return result;

        nmo_object_id_t *layer_ids = NULL;
        result = nmo_array_extend(&out_state->layer_ids, count, (void **)&layer_ids);
        if (result != NMO_OK) return result;

        for (size_t i = 0; i < count; ++i) {
            nmo_chunk_read_object_sequence_item(chunk, &layer_ids[i]);
        }
    }

    nmo_array_clear(&out_state->layer_chunks);
    if (!nmo_chunk_is_file_mode(chunk) && out_state->layer_ids.count > 0) {
        result = nmo_array_reserve(&out_state->layer_chunks, out_state->layer_ids.count);
        if (result != NMO_OK) return result;

        nmo_chunk_t **chunks = NULL;
        result = nmo_array_extend(&out_state->layer_chunks, out_state->layer_ids.count, (void **)&chunks);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < out_state->layer_chunks.count; ++i) {
            (void)nmo_chunk_read_sub_chunk(chunk, &chunks[i]);
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_grid_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_grid_state_t *in_state = (const nmo_grid_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_grid_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
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

    result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->layer_ids.count);
    if (result != NMO_OK) return result;

    const nmo_object_id_t *layer_ids = NMO_ARRAY_DATA(nmo_object_id_t, &in_state->layer_ids);
    for (uint32_t i = 0; i < in_state->layer_ids.count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, layer_ids[i]);
    }

    if (!nmo_chunk_is_file_mode(out_chunk) && in_state->layer_ids.count > 0) {
        const nmo_chunk_t *const *chunks = NMO_ARRAY_DATA(const nmo_chunk_t *, &in_state->layer_chunks);
        for (uint32_t i = 0; i < in_state->layer_ids.count; ++i) {
            nmo_chunk_t *sub = NULL;
            if (chunks && i < in_state->layer_chunks.count) {
                sub = (nmo_chunk_t *)chunks[i];
            }
            nmo_chunk_write_sub_chunk(out_chunk, sub);
        }
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_grid_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_grid_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_grid_state_t, width, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, length, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, priority, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, orientation_mode, CKPGUID_UINT32),
    NMO_FIELD(nmo_grid_state_t, has_file_flag, CKPGUID_UINT8),
    NMO_FIELD(nmo_grid_state_t, file_flag, CKPGUID_INT),
    NMO_FIELD_REF_ARRAY(nmo_grid_state_t, layer_ids),
    NMO_FIELD_ARRAY(nmo_grid_state_t, layer_chunks, CKPGUID_STATECHUNK)
};

static nmo_status_t nmo_grid_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_grid_state_t *s = src;
    nmo_grid_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->layer_ids, &d->layer_ids, &s->layer_ids.allocator));
    return nmo_object_clone_chunk_array(arena, &d->layer_chunks, &s->layer_chunks);
}

static nmo_status_t nmo_grid_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_grid_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->layer_ids.data, s->layer_ids.count, "layer_ids");
    NMO_VALIDATE_COUNT(s->layer_chunks.data, s->layer_chunks.count, "layer_chunks");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    grid,
    nmo_grid_state_t,
    nmo_grid_serialize,
    nmo_grid_deserialize,
    nmo_grid_fields,
    CKPGUID_GRID,
    "CKGrid",
    NMO_CID_GRID,
    CKPGUID_3DENTITY
)


