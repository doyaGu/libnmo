// chunk_lifecycle.c - Chunk lifecycle and mode management
// Implements: start_read, start_write, close, clear, metadata access

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <string.h>
#include <stdint.h>

// =============================================================================
// Internal Helpers
// =============================================================================

static nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    if (!chunk) return NULL;

    if (!chunk->parser_state) {
        if (!chunk->arena) {
            return NULL;
        }
        
        chunk->parser_state = nmo_arena_alloc(chunk->arena,
                                              sizeof(nmo_chunk_parser_state_t),
                                              _Alignof(nmo_chunk_parser_state_t));
        if (!chunk->parser_state) return NULL;
        memset(chunk->parser_state, 0, sizeof(nmo_chunk_parser_state_t));
    }

    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

// =============================================================================
// Lifecycle Management
// =============================================================================

nmo_result_t nmo_chunk_start_read(nmo_chunk_t *chunk) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate parser state"));
    }

    state->current_pos = 0;
    state->prev_identifier_pos = 0; // Reset to beginning
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_write(nmo_chunk_t *chunk) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate parser state"));
    }

    state->current_pos = 0;
    chunk->data_size = 0;

    // Initialize data buffer if needed
    if (!chunk->data && chunk->arena) {
        size_t initial_capacity = 64; // Start with 64 DWORDs
        chunk->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                   initial_capacity * sizeof(uint32_t),
                                                   sizeof(uint32_t));
        if (!chunk->data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate data buffer"));
        }
        chunk->data_capacity = initial_capacity;
        chunk->data_size = 0;
    }

    return nmo_result_ok();
}

void nmo_chunk_close(nmo_chunk_t *chunk) {
    if (chunk) {
        nmo_chunk_update_data_size(chunk);
    }
}

void nmo_chunk_clear(nmo_chunk_t *chunk) {
    if (chunk) {
        chunk->data_size = 0;
        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        if (state) {
            state->current_pos = 0;
            state->prev_identifier_pos = 0;
        }
    }
}

// =============================================================================
// Metadata Access
// =============================================================================

uint32_t nmo_chunk_get_class_id(const nmo_chunk_t *chunk) {
    return chunk ? chunk->class_id : 0;
}

uint32_t nmo_chunk_get_data_version(const nmo_chunk_t *chunk) {
    return chunk ? chunk->data_version : 0;
}

void nmo_chunk_set_data_version(nmo_chunk_t *chunk, uint32_t version) {
    if (chunk) {
        chunk->data_version = version;
    }
}

uint32_t nmo_chunk_get_chunk_version(const nmo_chunk_t *chunk) {
    return chunk ? chunk->chunk_version : 0;
}

size_t nmo_chunk_get_data_size(const nmo_chunk_t *chunk) {
    return chunk ? (chunk->data_size * sizeof(uint32_t)) : 0;
}

uint32_t nmo_chunk_get_size(const nmo_chunk_t *chunk) {
    return (uint32_t)nmo_chunk_get_data_size(chunk);
}

void nmo_chunk_update_data_size(nmo_chunk_t *chunk) {
    if (!chunk) return;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (state && state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }
}

/**
 * Check if chunk is compressed
 */
int nmo_chunk_is_compressed(const nmo_chunk_t *chunk) {
    if (!chunk) {
        return 0;
    }
    return (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) != 0;
}
