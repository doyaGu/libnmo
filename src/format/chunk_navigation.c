// chunk_navigation.c - Chunk position navigation
// Implements: get_position, goto, skip, check_size

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <string.h>

// =============================================================================
// Navigation
// =============================================================================

size_t nmo_chunk_get_position(const nmo_chunk_t *chunk) {
    if (!chunk) return (size_t)-1;

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state((nmo_chunk_t *) chunk);
    return state ? state->current_pos : (size_t)-1;
}

nmo_status_t nmo_chunk_goto(nmo_chunk_t *chunk, size_t pos) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "No parser state");
    }

    if (pos > state->data_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Cannot seek beyond chunk bounds");
    }

    state->current_pos = pos;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_skip(nmo_chunk_t *chunk, size_t dwords) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "No parser state");
    }

    if (dwords > SIZE_MAX - state->current_pos) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Skip overflow");
    }

    size_t new_pos = state->current_pos + dwords;

    if (state->data_size == chunk->data.count) {
        if (new_pos > chunk->data.count) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Cannot skip beyond readable data");
        }
        state->current_pos = new_pos;
        NMO_RETURN_OK();
    }

    if (dwords > SIZE_MAX / sizeof(uint32_t)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Skip size overflow");
    }

    /* CK2 behavior: Skip calls CheckSize to ensure capacity for writes. */
    nmo_status_t result = nmo_chunk_check_size(chunk, dwords * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    state->current_pos = new_pos;
    NMO_RETURN_OK();
}

// =============================================================================
// Memory Management
// =============================================================================

nmo_status_t nmo_chunk_check_size(nmo_chunk_t *chunk, size_t needed_bytes) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk not in write mode");
    }

    size_t needed_dwords = needed_bytes / sizeof(uint32_t);
    if (needed_bytes % sizeof(uint32_t) != 0) {
        needed_dwords++;
    }
    if (needed_dwords > SIZE_MAX - state->current_pos) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Chunk capacity request overflow");
    }
    size_t required_size = state->current_pos + needed_dwords;

    if (required_size > state->data_size) {
        size_t grow = needed_dwords;
        if (grow < 500) {
            grow = 500;
        }

        size_t new_size = required_size;
        if (grow <= SIZE_MAX - state->current_pos) {
            new_size = state->current_pos + grow;
        }

        /* Ensure reserve preserves everything up to the current cursor. */
        if (chunk->data.count < state->current_pos) {
            chunk->data.count = state->current_pos;
        }

        if (new_size > chunk->data.capacity) {
            nmo_status_t reserve_result = nmo_arena_array_reserve(&chunk->data, new_size);
            NMO_RETURN_IF_ERROR(reserve_result);
        }

        state->data_size = new_size;
    }

    NMO_RETURN_OK();
}
