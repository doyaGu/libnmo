#include "object/nmo_ref.h"

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"

#include <limits.h>
#include <stdint.h>

static size_t nmo_ref_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

nmo_status_t nmo_ref_read(nmo_chunk_t *chunk, nmo_ref_t *out_ref)
{
    if (!chunk || !out_ref) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_id_t raw_id = NMO_OBJECT_ID_NONE;
    nmo_object_id_t id = NMO_OBJECT_ID_NONE;
    nmo_status_t result = nmo_chunk_read_object_id_preserve(chunk, &raw_id, &id);
    if (result != NMO_OK) {
        return result;
    }

    nmo_ref_t ref = nmo_ref_from_raw(raw_id);
    if (id != NMO_OBJECT_ID_NONE) {
        ref.id = id;
        ref.state = NMO_REF_RESOLVED;
    } else if (ref.state == NMO_REF_UNRESOLVED) {
        const nmo_chunk_file_context_t *file_context =
            nmo_chunk_get_file_context(chunk);
        if (file_context != NULL && file_context->repository != NULL) {
            result = nmo_object_repository_intern_unresolved_ref(
                file_context->repository, raw_id, &ref.id);
            if (result != NMO_OK) {
                return result;
            }
        }
    }
    *out_ref = ref;
    return NMO_OK;
}

nmo_status_t nmo_ref_write(nmo_chunk_t *chunk, const nmo_ref_t *ref)
{
    if (!chunk || !ref) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ref->state == NMO_REF_RESOLVED) {
        return nmo_chunk_write_object_id(chunk, ref->id);
    }
    if (ref->state == NMO_REF_NONE) {
        return nmo_chunk_write_object_id(chunk, NMO_OBJECT_ID_NONE);
    }
    return nmo_chunk_write_raw_object_id(chunk, ref->raw_id);
}

nmo_status_t nmo_ref_write_sequence_item(nmo_chunk_t *chunk, const nmo_ref_t *ref)
{
    if (!chunk || !ref) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ref->state == NMO_REF_RESOLVED) {
        return nmo_chunk_write_object_sequence_item(chunk, ref->id);
    }
    if (ref->state == NMO_REF_NONE) {
        return nmo_chunk_write_object_sequence_item(
            chunk, NMO_OBJECT_ID_NONE);
    }
    return nmo_chunk_write_raw_object_sequence_item(chunk, ref->raw_id);
}

nmo_status_t nmo_ref_read_sequence(
    nmo_chunk_t *chunk,
    nmo_ref_t **out_refs,
    size_t *out_count,
    nmo_arena_t *arena)
{
    if (!chunk || !out_refs || !out_count || !arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;
    if (count > SIZE_MAX / sizeof(nmo_ref_t)) return NMO_ERR_INVALID_FORMAT;
    if (count > nmo_ref_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    nmo_ref_t *refs = NULL;
    if (count > 0) {
        refs = (nmo_ref_t *)nmo_arena_alloc(
            arena, count * sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
        if (!refs) return NMO_ERR_NOMEM;
        for (size_t i = 0; i < count; ++i) {
            result = nmo_ref_read(chunk, &refs[i]);
            if (result != NMO_OK) return result;
        }
    }
    *out_refs = refs;
    *out_count = count;
    return NMO_OK;
}

nmo_status_t nmo_ref_write_sequence(
    nmo_chunk_t *chunk,
    const nmo_ref_t *refs,
    size_t count)
{
    if (!chunk || (count > 0 && !refs)) return NMO_ERR_INVALID_ARGUMENT;
    if (count > INT32_MAX) return NMO_ERR_INVALID_ARGUMENT;
    nmo_status_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result != NMO_OK) return result;
    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_write_sequence_item(chunk, &refs[i]);
        if (result != NMO_OK) return result;
    }
    return NMO_OK;
}

void nmo_ref_check_class(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types,
    nmo_class_id_t expected_class_id)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED ||
        repository == NULL || types == NULL) {
        return;
    }
    const nmo_object_t *target = nmo_object_repository_find_by_id(
        repository, ref->id);
    if (target != NULL && !nmo_type_registry_is_class_derived_from(
            types, (uint32_t)nmo_object_get_class_id(target),
            (uint32_t)expected_class_id)) {
        ref->state = NMO_REF_CLASS_MISMATCH;
    }
}
