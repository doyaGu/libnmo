// chunk_navigation.c - Chunk position navigation
// Implements: get_position, goto, skip, check_size

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <string.h>

// =============================================================================
// Internal Helpers
// =============================================================================

static inline nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

// =============================================================================
// Navigation
// =============================================================================

size_t nmo_chunk_get_position(const nmo_chunk_t *chunk) {
    if (!chunk) return 0;

    nmo_chunk_parser_state_t *state = get_parser_state((nmo_chunk_t *) chunk);
    return state ? state->current_pos : 0;
}

nmo_result_t nmo_chunk_goto(nmo_chunk_t *chunk, size_t pos) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (pos > chunk->data.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_OUT_OF_BOUNDS,
                                          NMO_SEVERITY_ERROR, "Position beyond data size"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR, "No parser state"));
    }

    state->current_pos = pos;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_skip(nmo_chunk_t *chunk, size_t dwords) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR, "No parser state"));
    }

    if (state->current_pos + dwords > chunk->data.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Skip beyond data size"));
    }

    state->current_pos += dwords;
    return nmo_result_ok();
}

// =============================================================================
// Memory Management
// =============================================================================

nmo_result_t nmo_chunk_check_size(nmo_chunk_t *chunk, size_t needed_dwords) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR, "Chunk not in write mode"));
    }

    size_t required_size = state->current_pos + needed_dwords;
    if (required_size > chunk->data.capacity) {
        /* Ensure reserve preserves everything up to the current cursor. */
        if (chunk->data.count < state->current_pos) {
            chunk->data.count = state->current_pos;
        }

        nmo_result_t reserve_result = nmo_arena_array_reserve(&chunk->data, required_size);
        if (reserve_result.code != NMO_OK) {
            return reserve_result;
        }
    }

    return nmo_result_ok();
}
