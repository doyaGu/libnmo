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
    return (state->current_pos + dwords) <= chunk->data_size;
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

    // Write manager GUID (2 DWORDs)
    nmo_result_t result = nmo_chunk_write_guid(chunk, manager_guid);
    if (result.code != NMO_OK) return result;

    // Write count
    return nmo_chunk_write_dword(chunk, (uint32_t) count);
}

nmo_result_t nmo_chunk_write_manager_int(nmo_chunk_t *chunk,
                                         nmo_manager_id_t mgr_id,
                                         uint32_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 2);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    // Track manager position
    if (!chunk->managers) {
        chunk->manager_capacity = 16;
        chunk->managers = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                       chunk->manager_capacity * sizeof(uint32_t), sizeof(uint32_t));
        if (!chunk->managers) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate managers list"));
        }
    } else if (chunk->manager_count >= chunk->manager_capacity) {
        size_t new_capacity = chunk->manager_capacity * 2;
        uint32_t *new_managers = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                              new_capacity * sizeof(uint32_t), sizeof(uint32_t));
        if (!new_managers) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to grow managers list"));
        }
        memcpy(new_managers, chunk->managers, chunk->manager_count * sizeof(uint32_t));
        chunk->managers = new_managers;
        chunk->manager_capacity = new_capacity;
    }

    chunk->managers[chunk->manager_count++] = state->current_pos;

    // Write manager ID and value
    chunk->data[state->current_pos++] = mgr_id;
    chunk->data[state->current_pos++] = value;

    // Update data_size
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_manager_int(nmo_chunk_t *chunk,
                                        nmo_manager_id_t *out_mgr_id,
                                        uint32_t *out_value) {
    if (!chunk || !out_mgr_id || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 2)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Insufficient data for manager int"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    *out_mgr_id = chunk->data[state->current_pos++];
    *out_value = chunk->data[state->current_pos++];

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_manager_read_sequence(nmo_chunk_t *chunk,
                                                   nmo_guid_t *out_manager_guid,
                                                   size_t *out_count) {
    if (!chunk || !out_manager_guid || !out_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Read manager GUID (2 DWORDs)
    nmo_result_t result = nmo_chunk_read_guid(chunk, out_manager_guid);
    if (result.code != NMO_OK) return result;

    // Read count
    uint32_t count_u32 = 0;
    result = nmo_chunk_read_dword(chunk, &count_u32);
    if (result.code != NMO_OK) return result;

    *out_count = (size_t)count_u32;
    return nmo_result_ok();
}
