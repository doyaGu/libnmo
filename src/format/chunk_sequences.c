// chunk_sequences.c - Object sequence operations
// Implements: write/read_object_sequence_start/item

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <string.h>

// =============================================================================
// Object Sequences
// =============================================================================

nmo_result_t nmo_chunk_write_object_sequence_start(nmo_chunk_t *chunk, size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Set IDS option
    chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *) chunk->parser_state;
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Parser state not initialized"));
    }

    if (count > 0) {
        uint32_t sentinel = 0xFFFFFFFFu;
        nmo_result_t list_result = nmo_arena_array_append(&chunk->ids, &sentinel);
        if (list_result.code != NMO_OK) {
            return list_result;
        }

        uint32_t pos = (uint32_t) state->current_pos;
        list_result = nmo_arena_array_append(&chunk->ids, &pos);
        if (list_result.code != NMO_OK) {
            return list_result;
        }
    }

    // Write count
    return nmo_chunk_write_int(chunk, (int32_t) count);
}

nmo_result_t nmo_chunk_write_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t id) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Sequence items should not add entries to the IDs list (CK2 behavior)
    return nmo_chunk_write_int(chunk, (int32_t) id);
}

nmo_result_t nmo_chunk_read_object_sequence_start(nmo_chunk_t *chunk, size_t *out_count) {
    if (!chunk || !out_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int32_t count;
    nmo_result_t result = nmo_chunk_read_int(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = (size_t) count;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t *out_id) {
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
