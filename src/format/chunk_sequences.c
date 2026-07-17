// chunk_sequences.c - Object sequence operations
// Implements: write/read_object_sequence_start/item

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "object/nmo_object_repository.h"
#include <string.h>

// =============================================================================
// Object Sequences
// =============================================================================

nmo_status_t nmo_chunk_write_object_sequence_start(nmo_chunk_t *chunk, size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Object sequence count does not fit the signed 32-bit format");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                               "Parser state not initialized");
    }

    /* CK2 behavior: Only track when count > 0 AND not in file mode */
    const nmo_chunk_file_context_t *ctx = NULL;
    if (chunk != NULL && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        ctx = chunk->file_context;
    }
    const int in_file_context = (ctx != NULL && ctx->runtime_to_file != NULL);

    if (count > 0 && !in_file_context) {
        /* CK2: AddEntries adds -1 marker followed by position */
        uint32_t sentinel = 0xFFFFFFFFu;
        nmo_status_t list_result = nmo_arena_array_append(&chunk->ids, &sentinel);
        NMO_RETURN_IF_ERROR(list_result);

        uint32_t pos = (uint32_t) state->current_pos;
        list_result = nmo_arena_array_append(&chunk->ids, &pos);
        NMO_RETURN_IF_ERROR(list_result);

        /* Set IDS option since we added tracking entries */
        chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }

    /* Write count */
    return nmo_chunk_write_int(chunk, (int32_t) count);
}

nmo_status_t nmo_chunk_write_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    // Sequence items should not add entries to the IDs list (CK2 behavior)
    const nmo_chunk_file_context_t *ctx = NULL;
    if (chunk != NULL && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        ctx = chunk->file_context;
    }

    uint32_t encoded_value = (uint32_t) id;
    if (ctx != NULL && ctx->runtime_to_file != NULL) {
        if (id == 0) {
            encoded_value = NMO_OBJECT_ID_INVALID;
        } else {
            nmo_object_id_t unresolved_raw = NMO_OBJECT_ID_NONE;
            if (ctx->repository != NULL &&
                nmo_object_repository_get_unresolved_ref_raw(
                    ctx->repository, id, &unresolved_raw)) {
                encoded_value = (uint32_t)unresolved_raw;
                return nmo_chunk_write_int(chunk, (int32_t)encoded_value);
            }
            nmo_object_id_t file_id = 0;
            if (nmo_id_remap_lookup_id(ctx->runtime_to_file, id, &file_id) == NMO_OK) {
                encoded_value = (uint32_t) file_id;
            } else {
                NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                 "Cannot serialize unmapped runtime object sequence ID %u",
                                 (unsigned)id);
            }
        }
    }

    return nmo_chunk_write_int(chunk, (int32_t) encoded_value);
}

nmo_status_t nmo_chunk_write_raw_object_sequence_item(
    nmo_chunk_t *chunk,
    nmo_object_id_t raw_id)
{
    return nmo_chunk_write_int(chunk, (int32_t)raw_id);
}

nmo_status_t nmo_chunk_read_object_sequence_start(nmo_chunk_t *chunk, size_t *out_count) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_count, "Invalid arguments");

    int32_t count;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    if (count < 0) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Object sequence count cannot be negative");
    }

    if (!nmo_chunk_has_read_capacity(chunk, (size_t)count)) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                               "Object sequence count exceeds remaining DWORDs");
    }

    *out_count = (size_t) count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t *out_id) {
    return nmo_chunk_read_object_id(chunk, out_id);
}

size_t nmo_chunk_get_id_count(const nmo_chunk_t *chunk) {
    return chunk ? chunk->ids.count : 0;
}

uint32_t nmo_chunk_get_object_id(const nmo_chunk_t *chunk, size_t index) {
    if (!chunk || index >= chunk->ids.count || chunk->ids.data == NULL) {
        return 0;
    }
    // ids array contains positions in data buffer, not the IDs themselves
    const uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
    uint32_t pos = ids[index];
    if (pos == 0xFFFFFFFFu) {
        return 0;
    }
    if (pos < chunk->data.count) {
        const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        return data[pos];
    }
    return 0;
}
