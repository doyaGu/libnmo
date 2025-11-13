// chunk_sequences.c - Object sequence operations
// Implements: write/read_object_sequence_start/item

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"

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

    // Write count
    return nmo_chunk_write_int(chunk, (int32_t) count);
}

nmo_result_t nmo_chunk_write_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t id) {
    return nmo_chunk_write_object_id(chunk, id);
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
    return chunk ? chunk->id_count : 0;
}

uint32_t nmo_chunk_get_object_id(const nmo_chunk_t *chunk, size_t index) {
    if (!chunk || index >= chunk->id_count || !chunk->ids) {
        return 0;
    }
    // ids array contains positions in data buffer, not the IDs themselves
    uint32_t pos = chunk->ids[index];
    if (pos < chunk->data_size) {
        return chunk->data[pos];
    }
    return 0;
}
