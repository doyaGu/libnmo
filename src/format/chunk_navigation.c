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

    if (pos > chunk->data_size) {
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

    if (state->current_pos + dwords > chunk->data_size) {
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
    if (required_size > chunk->data_capacity) {
        size_t new_capacity = chunk->data_capacity * 2;
        while (new_capacity < required_size) {
            new_capacity *= 2;
        }

        uint32_t *new_data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          new_capacity * sizeof(uint32_t),
                                                          sizeof(uint32_t));
        if (!new_data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to grow chunk buffer"));
        }

        if (chunk->data && state->current_pos > 0) {
            memcpy(new_data, chunk->data, state->current_pos * sizeof(uint32_t));
        }

        chunk->data = new_data;
        chunk->data_capacity = new_capacity;
    }

    return nmo_result_ok();
}
