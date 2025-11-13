// chunk_subchunks.c - Sub-chunk operations
// Implements: write_sub_chunk, read_sub_chunk, start_sub_chunk_sequence

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
// Sub-chunks
// =============================================================================

nmo_result_t nmo_chunk_write_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t *sub) {
    if (!chunk || !sub) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk arguments"));
    }

    // Set CHN flag
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;

    // Track sub-chunk
    if (!chunk->chunks) {
        chunk->chunk_capacity = 8;
        chunk->chunks = (nmo_chunk_t **) nmo_arena_alloc(chunk->arena,
                                                         chunk->chunk_capacity * sizeof(nmo_chunk_t *),
                                                         sizeof(nmo_chunk_t *));
        if (!chunk->chunks) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate chunks list"));
        }
    } else if (chunk->chunk_count >= chunk->chunk_capacity) {
        size_t new_capacity = chunk->chunk_capacity * 2;
        nmo_chunk_t **new_chunks = (nmo_chunk_t **) nmo_arena_alloc(chunk->arena,
                                                                    new_capacity * sizeof(nmo_chunk_t *),
                                                                    sizeof(nmo_chunk_t *));
        if (!new_chunks) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to grow chunks list"));
        }
        memcpy(new_chunks, chunk->chunks, chunk->chunk_count * sizeof(nmo_chunk_t *));
        chunk->chunks = new_chunks;
        chunk->chunk_capacity = new_capacity;
    }

    chunk->chunks[chunk->chunk_count++] = sub;

    // Calculate total size
    size_t total_size = 8;        // Header: 8 DWORDs
    total_size += sub->data_size;
    if (sub->ids) total_size += sub->id_count;
    if (sub->chunks) total_size += sub->chunk_count;

    nmo_result_t result;
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    // Remember where embedded data starts
    size_t embedded_data_offset = state->current_pos + 8;

    // Write header
    result = nmo_chunk_write_dword(chunk, (uint32_t) (total_size - 1));
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_word(chunk, sub->chunk_class_id);
    if (result.code != NMO_OK) return result;

    uint32_t version_info = sub->data_version | (sub->chunk_version << 16);
    result = nmo_chunk_write_dword(chunk, version_info);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->data_size);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, 0); // file_flag
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->id_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->chunk_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, 0); // manager_count
    if (result.code != NMO_OK) return result;

    // Write data
    if (sub->data_size > 0) {
        result = nmo_chunk_write_buffer_no_size(chunk, sub->data, sub->data_size * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write IDs
    if (sub->ids && sub->id_count > 0) {
        result = nmo_chunk_write_buffer_no_size(chunk, sub->ids, sub->id_count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write chunks list (simplified)
    if (sub->chunks && sub->chunk_count > 0) {
        result = nmo_chunk_write_buffer_no_size(chunk, sub->chunks, sub->chunk_count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Update sub-chunk pointers to embedded data
    sub->data = &chunk->data[embedded_data_offset];
    if (sub->ids && sub->id_count > 0) {
        sub->ids = &chunk->data[embedded_data_offset + sub->data_size];
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t **out_sub) {
    if (!chunk || !out_sub) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result;

    // Read header
    uint32_t total_size, version_info, data_size, file_flag;
    uint32_t id_count, chunk_count, manager_count;
    uint16_t class_id;

    result = nmo_chunk_read_dword(chunk, &total_size);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_word(chunk, &class_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &version_info);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &data_size);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &file_flag);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &id_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &chunk_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_read_dword(chunk, &manager_count);
    if (result.code != NMO_OK) return result;

    // Create sub-chunk
    nmo_chunk_t *sub = nmo_chunk_create(chunk->arena);
    if (!sub) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to create sub-chunk"));
    }

    sub->chunk_class_id = class_id;
    sub->data_version = (uint8_t) (version_info & 0xFF);
    sub->chunk_version = (uint8_t) ((version_info >> 16) & 0xFF);

    // Read data
    if (data_size > 0) {
        if (!can_read(chunk, data_size)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient data"));
        }

        sub->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                 data_size * sizeof(uint32_t), sizeof(uint32_t));
        if (!sub->data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate data"));
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        memcpy(sub->data, &chunk->data[state->current_pos], data_size * sizeof(uint32_t));
        state->current_pos += data_size;

        sub->data_size = data_size;
        sub->data_capacity = data_size;
    }

    // Read IDs
    if (id_count > 0) {
        if (!can_read(chunk, id_count)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient IDs data"));
        }

        sub->ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                id_count * sizeof(uint32_t), sizeof(uint32_t));
        if (!sub->ids) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate IDs"));
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        memcpy(sub->ids, &chunk->data[state->current_pos], id_count * sizeof(uint32_t));
        state->current_pos += id_count;

        sub->id_count = id_count;
        sub->id_capacity = id_count;
    }

    // Skip chunks list
    if (chunk_count > 0) {
        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        state->current_pos += chunk_count;
    }

    *out_sub = sub;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_sub_chunk_sequence(nmo_chunk_t *chunk, size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    return nmo_chunk_write_dword(chunk, (uint32_t) count);
}

// =============================================================================
// Accessors
// =============================================================================

/**
 * Add sub-chunk
 */
nmo_result_t nmo_chunk_add_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t *sub_chunk) {
    if (!chunk || !sub_chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments"));
    }

    /* Check if we need to grow the array */
    if (chunk->chunk_count >= chunk->chunk_capacity) {
        size_t new_capacity = chunk->chunk_capacity == 0 ? 4 : chunk->chunk_capacity * 2;
        nmo_chunk_t **new_chunks = nmo_arena_alloc(chunk->arena,
                                                   new_capacity * sizeof(nmo_chunk_t *),
                                                   sizeof(void *));
        if (!new_chunks) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to grow sub-chunk array"));
        }

        /* Copy existing chunks */
        if (chunk->chunks) {
            memcpy(new_chunks, chunk->chunks, chunk->chunk_count * sizeof(nmo_chunk_t *));
        }

        chunk->chunks = new_chunks;
        chunk->chunk_capacity = new_capacity;
    }

    /* Add sub-chunk */
    chunk->chunks[chunk->chunk_count++] = sub_chunk;

    /* Set CHN flag */
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;

    return nmo_result_ok();
}

/**
 * Get sub-chunk count
 */
uint32_t nmo_chunk_get_sub_chunk_count(const nmo_chunk_t *chunk) {
    if (!chunk) {
        return 0;
    }
    return (uint32_t) chunk->chunk_count;
}

/**
 * Get sub-chunk by index
 */
nmo_chunk_t *nmo_chunk_get_sub_chunk(const nmo_chunk_t *chunk, uint32_t index) {
    if (!chunk || index >= chunk->chunk_count) {
        return NULL;
    }
    return chunk->chunks[index];
}
