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
    return (state->current_pos + dwords) <= chunk->data.count;
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
    nmo_result_t list_result = nmo_arena_array_append(&chunk->chunks, &sub);
    if (list_result.code != NMO_OK) {
        return list_result;
    }

    // Calculate total size (in DWORDs)
    const bool include_manager = (chunk->chunk_version > 4);
    size_t header_bytes =
        sizeof(uint32_t) + /* size */
        sizeof(uint32_t) + /* class_id */
        sizeof(uint32_t) + /* version */
        sizeof(uint32_t) + /* data_size */
        sizeof(uint32_t) + /* file_flag */
        sizeof(uint32_t) + /* id_count */
        sizeof(uint32_t);  /* chunk_count */
    if (include_manager) {
        header_bytes += sizeof(uint32_t); /* manager_count */
    }
    size_t header_dwords = header_bytes / sizeof(uint32_t);
    size_t total_size = header_dwords;
    total_size += sub->data.count;
    total_size += sub->ids.count;
    total_size += sub->chunk_refs.count;
    if (include_manager) {
        total_size += sub->managers.count;
    }

    nmo_result_t result;
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    size_t size_header_offset = state->current_pos;

    // Write header
    result = nmo_chunk_write_dword(chunk, (uint32_t) (total_size - 1));
    if (result.code != NMO_OK) return result;

    // Track sub-chunk position (CK2 AddEntry)
    {
        uint32_t header_pos = (uint32_t) size_header_offset;
        result = nmo_arena_array_append(&chunk->chunk_refs, &header_pos);
    }
    if (result.code != NMO_OK) return result;

    // CK2 writes class_id as full DWORD - use class_id field (32-bit)
    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->class_id);
    if (result.code != NMO_OK) return result;

    uint32_t version_info = (uint32_t)(sub->data_version & 0xFFFFu) |
                            ((uint32_t)(sub->chunk_version & 0xFFFFu) << 16);
    result = nmo_chunk_write_dword(chunk, version_info);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->data.count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(
        chunk,
        (sub->chunk_options & NMO_CHUNK_OPTION_FILE) ? 1u : 0u);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->ids.count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->chunk_refs.count);
    if (result.code != NMO_OK) return result;

    if (include_manager) {
        result = nmo_chunk_write_dword(chunk, (uint32_t) sub->managers.count);
        if (result.code != NMO_OK) return result;
    }

    // Write data
    if (sub->data.count > 0) {
        const uint32_t *sub_data = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->data);
        result = nmo_chunk_write_buffer_no_size(chunk, sub_data, sub->data.count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write IDs
    if (sub->ids.count > 0) {
        const uint32_t *sub_ids = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->ids);
        result = nmo_chunk_write_buffer_no_size(chunk, sub_ids, sub->ids.count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write chunk refs (offset list)
    if (sub->chunk_refs.count > 0) {
        const uint32_t *sub_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->chunk_refs);
        result = nmo_chunk_write_buffer_no_size(chunk,
                                                sub_refs,
                                                sub->chunk_refs.count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write managers list
    if (include_manager && sub->managers.count > 0) {
        const uint32_t *sub_mgrs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->managers);
        result = nmo_chunk_write_buffer_no_size(chunk,
                                                sub_mgrs,
                                                sub->managers.count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_read_sub_chunk_sequence(nmo_chunk_t *chunk, size_t *out_count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Read sequence count (stored as single DWORD)
    uint32_t count;
    nmo_result_t result = nmo_chunk_read_dword(chunk, &count);
    if (result.code != NMO_OK) {
        return result;
    }

    // Store count if requested
    if (out_count) {
        *out_count = (size_t)count;
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
    uint32_t id_count, chunk_count, manager_count = 0;
    uint32_t class_id;  // CK2 reads as full DWORD

    result = nmo_chunk_read_dword(chunk, &total_size);
    if (result.code != NMO_OK) return result;

    // CK2 reads class_id as full DWORD, not WORD
    result = nmo_chunk_read_dword(chunk, &class_id);
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

    if (chunk->chunk_version > 4) {
        result = nmo_chunk_read_dword(chunk, &manager_count);
        if (result.code != NMO_OK) return result;
    }

    // Create sub-chunk
    nmo_chunk_t *sub = nmo_chunk_create(chunk->arena);
    if (!sub) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to create sub-chunk"));
    }

    sub->class_id = class_id;  // Use 32-bit class_id field for sub-chunks
    sub->chunk_class_id = (uint8_t) (class_id & 0xFFu);
    sub->data_version = (uint16_t) (version_info & 0xFFFFu);
    sub->chunk_version = (uint16_t) ((version_info >> 16) & 0xFFFFu);
    sub->chunk_options = 0;
    if (file_flag) sub->chunk_options |= NMO_CHUNK_OPTION_FILE;

    // Read data
    if (data_size > 0) {
        if (!can_read(chunk, data_size)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient data"));
        }

        result = nmo_arena_array_resize(&sub->data, data_size);
        if (result.code != NMO_OK) {
            return result;
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_data = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->data);
        memcpy(sub_data,
               &parent_data[state->current_pos],
               data_size * sizeof(uint32_t));
        state->current_pos += data_size;
    }

    // Read IDs
    if (id_count > 0) {
        if (!can_read(chunk, id_count)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient IDs data"));
        }

        result = nmo_arena_array_resize(&sub->ids, id_count);
        if (result.code != NMO_OK) {
            return result;
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_ids = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->ids);
        memcpy(sub_ids,
               &parent_data[state->current_pos],
               id_count * sizeof(uint32_t));
        state->current_pos += id_count;
    }

    // Read chunk refs (offset list)
    if (chunk_count > 0) {
        if (!can_read(chunk, chunk_count)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient chunk refs data"));
        }

        result = nmo_arena_array_resize(&sub->chunk_refs, chunk_count);
        if (result.code != NMO_OK) {
            return result;
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->chunk_refs);
        memcpy(sub_refs,
               &parent_data[state->current_pos],
               chunk_count * sizeof(uint32_t));
        state->current_pos += chunk_count;
    }

    // Read managers list
    if (manager_count > 0) {
        if (!can_read(chunk, manager_count)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient manager refs data"));
        }

        result = nmo_arena_array_resize(&sub->managers, manager_count);
        if (result.code != NMO_OK) {
            return result;
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_mgrs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->managers);
        memcpy(sub_mgrs,
               &parent_data[state->current_pos],
               manager_count * sizeof(uint32_t));
        state->current_pos += manager_count;
    }

    if (id_count > 0) sub->chunk_options |= NMO_CHUNK_OPTION_IDS;
    if (chunk_count > 0) sub->chunk_options |= NMO_CHUNK_OPTION_CHN;
    if (manager_count > 0) sub->chunk_options |= NMO_CHUNK_OPTION_MAN;

    *out_sub = sub;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_sub_chunk_sequence(nmo_chunk_t *chunk, size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    // Track sequence start (CK2 AddEntries)
    uint32_t sentinel = 0xFFFFFFFFu;
    nmo_result_t result = nmo_arena_array_append(&chunk->chunk_refs, &sentinel);
    if (result.code != NMO_OK) return result;
    {
        uint32_t pos = (uint32_t) state->current_pos;
        result = nmo_arena_array_append(&chunk->chunk_refs, &pos);
    }
    if (result.code != NMO_OK) return result;

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

    /* Add sub-chunk */
    nmo_result_t result = nmo_arena_array_append(&chunk->chunks, &sub_chunk);
    if (result.code != NMO_OK) {
        return result;
    }

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
    return (uint32_t) chunk->chunks.count;
}

/**
 * Get sub-chunk by index
 */
nmo_chunk_t *nmo_chunk_get_sub_chunk(const nmo_chunk_t *chunk, uint32_t index) {
    if (!chunk || index >= chunk->chunks.count) {
        return NULL;
    }
    nmo_chunk_t **children = NMO_ARENA_ARRAY_DATA(nmo_chunk_t *, &chunk->chunks);
    return children[index];
}
