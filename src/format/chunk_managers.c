// chunk_managers.c - Manager sequence operations
// Implements: start_manager_sequence, write/read_manager_int, start_manager_read_sequence

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <string.h>

// =============================================================================
// Internal Helpers
// =============================================================================

static inline nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

static inline bool can_read(const nmo_chunk_t *chunk, size_t dwords) {
    nmo_chunk_parser_state_t *state = get_parser_state((nmo_chunk_t *) chunk);
    if (!state) return false;
    return (state->current_pos + dwords) <= chunk->data.count;
}

// =============================================================================
// Manager Sequences
// =============================================================================

nmo_result_t nmo_chunk_start_manager_sequence(nmo_chunk_t *chunk,
                                              nmo_guid_t manager_guid,
                                              size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Set MAN flag
    chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    // Track sequence start in manager list (CK2 AddEntries)
    uint32_t sentinel = 0xFFFFFFFFu;
    nmo_result_t list_result = nmo_arena_array_append(&chunk->managers, &sentinel);
    if (list_result.code != NMO_OK) {
        return list_result;
    }

    uint32_t pos = (uint32_t) state->current_pos;
    list_result = nmo_arena_array_append(&chunk->managers, &pos);
    if (list_result.code != NMO_OK) {
        return list_result;
    }

    // Write count then manager GUID
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_guid(chunk, manager_guid);
}

nmo_result_t nmo_chunk_write_manager_int(nmo_chunk_t *chunk,
                                         nmo_guid_t manager_guid,
                                         uint32_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 3);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    // Track manager position
    uint32_t pos = (uint32_t) state->current_pos;
    nmo_result_t list_result = nmo_arena_array_append(&chunk->managers, &pos);
    if (list_result.code != NMO_OK) {
        return list_result;
    }

    // Write manager GUID and value
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    data[state->current_pos++] = manager_guid.d1;
    data[state->current_pos++] = manager_guid.d2;
    data[state->current_pos++] = value;

    // Update data_size
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_manager_int(nmo_chunk_t *chunk,
                                        nmo_guid_t *out_manager_guid,
                                        uint32_t *out_value) {
    if (!chunk || !out_manager_guid || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 3)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Insufficient data for manager int"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    out_manager_guid->d1 = data[state->current_pos++];
    out_manager_guid->d2 = data[state->current_pos++];
    *out_value = data[state->current_pos++];

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_manager_read_sequence(nmo_chunk_t *chunk,
                                                   nmo_guid_t *out_manager_guid,
                                                   size_t *out_count) {
    if (!chunk || !out_manager_guid || !out_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Read count then manager GUID
    uint32_t count_u32 = 0;
    nmo_result_t result = nmo_chunk_read_dword(chunk, &count_u32);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_guid(chunk, out_manager_guid);
    if (result.code != NMO_OK) return result;

    *out_count = (size_t)count_u32;
    return nmo_result_ok();
}
