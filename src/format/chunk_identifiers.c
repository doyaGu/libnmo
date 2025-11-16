// chunk_identifiers.c - Identifier operations
// Implements: write/read/seek_identifier

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"

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
// Identifiers
// =============================================================================

nmo_result_t nmo_chunk_write_identifier(nmo_chunk_t *chunk, uint32_t id) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 1);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    // Write identifier (compatible with original Virtools format - just the identifier, no next pointer)
    chunk->data[state->current_pos++] = id;
    state->prev_identifier_pos = state->current_pos - 1;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_identifier(nmo_chunk_t *chunk, uint32_t *out_id) {
    if (!chunk || !out_id) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 1)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    *out_id = chunk->data[state->current_pos++];

    // Update prev_identifier_pos
    state->prev_identifier_pos = state->current_pos - 1;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_seek_identifier(nmo_chunk_t *chunk, uint32_t id) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    
    // Empty chunk cannot have identifiers
    if (chunk->data_size == 0 || !chunk->data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                          NMO_SEVERITY_INFO, "Identifier not found in empty chunk"));
    }

    // Linear search from beginning (compatible with original Virtools format)
    // Identifiers are just DWORD values scattered in the data stream
    for (size_t i = 0; i < chunk->data_size; i++) {
        if (chunk->data[i] == id) {
            // Found it! Position after the identifier
            state->current_pos = i + 1;
            state->prev_identifier_pos = i;
            return nmo_result_ok();
        }
    }

    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                      NMO_SEVERITY_INFO, "Identifier not found"));
}
