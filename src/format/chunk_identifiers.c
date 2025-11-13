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

    nmo_result_t result = nmo_chunk_check_size(chunk, 2);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    // Write identifier
    chunk->data[state->current_pos++] = id;

    // Write "next" pointer (initialized to 0, can be patched later)
    size_t next_pos = state->current_pos;
    chunk->data[state->current_pos++] = 0;

    // Link to previous identifier if exists
    if (state->prev_identifier_pos < state->data_size) {
        chunk->data[state->prev_identifier_pos + 1] = (uint32_t) next_pos;
    }

    state->prev_identifier_pos = next_pos - 1;

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

    // Start from prev_identifier_pos if valid, otherwise from beginning
    size_t start_pos = 0;
    if (state->prev_identifier_pos < chunk->data_size - 1) {
        // Follow the "next" pointer
        uint32_t next = chunk->data[state->prev_identifier_pos + 1];
        if (next != 0 && next < chunk->data_size) {
            start_pos = next;
        }
    }

    // Search through identifiers
    for (size_t i = start_pos; i < chunk->data_size; i++) {
        if (chunk->data[i] == id) {
            // Found it! Position after the identifier and its "next" pointer
            state->current_pos = i + 2;
            state->prev_identifier_pos = i;
            return nmo_result_ok();
        }

        // Try to follow "next" pointer if this looks like an identifier
        if (i + 1 < chunk->data_size) {
            uint32_t next = chunk->data[i + 1];
            if (next != 0 && next < chunk->data_size && next > i) {
                i = next - 1; // -1 because loop will increment
            }
        }
    }

    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                      NMO_SEVERITY_INFO, "Identifier not found"));
}
