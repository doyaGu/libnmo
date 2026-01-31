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

// =============================================================================
// Identifiers
// =============================================================================

nmo_result_t nmo_chunk_write_identifier(nmo_chunk_t *chunk, uint32_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, 2);
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    if (state->prev_identifier_pos < state->current_pos) {
        data[state->prev_identifier_pos + 1] = (uint32_t) state->current_pos;
    }

    data[state->current_pos++] = id;
    data[state->current_pos++] = 0;
    state->prev_identifier_pos = state->current_pos - 2;

    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_identifier(nmo_chunk_t *chunk, uint32_t *out_id) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_id, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 2);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    *out_id = data[state->current_pos];

    state->prev_identifier_pos = state->current_pos;
    state->current_pos += 2;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_seek_identifier(nmo_chunk_t *chunk, uint32_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    
    // Empty chunk cannot have identifiers
    if (chunk->data.count == 0 || chunk->data.data == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                          NMO_SEVERITY_INFO, "Identifier not found in empty chunk"));
    }

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    size_t start_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        start_pos = data[state->prev_identifier_pos + 1];
    }

    size_t current_pos = start_pos;
    if (current_pos != 0) {
        while (current_pos < chunk->data.count && data[current_pos] != id) {
            current_pos = data[current_pos + 1];
            if (current_pos == 0) {
                break;
            }
        }

        if (current_pos != 0 && current_pos < chunk->data.count) {
            state->prev_identifier_pos = current_pos;
            state->current_pos = current_pos + 2;
            return nmo_result_ok();
        }
    }

    current_pos = 0;
    while (current_pos < chunk->data.count && data[current_pos] != id) {
        current_pos = data[current_pos + 1];
        if (current_pos == start_pos) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                              NMO_SEVERITY_INFO, "Identifier not found"));
        }
    }

    if (current_pos >= chunk->data.count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_FOUND,
                                          NMO_SEVERITY_INFO, "Identifier not found"));
    }

    state->prev_identifier_pos = current_pos;
    state->current_pos = current_pos + 2;
    return nmo_result_ok();
}
