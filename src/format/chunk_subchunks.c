// chunk_subchunks.c - Sub-chunk operations
// Implements: write_sub_chunk, read_sub_chunk, start_sub_chunk_sequence

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_context.h"
#include <string.h>

// =============================================================================
// Sub-chunks
// =============================================================================

static nmo_status_t write_sub_chunk_payload(nmo_chunk_t *chunk, const nmo_chunk_t *sub) {
    nmo_status_t result;

    /* CK2 header layout: size, class_id, version, data_size, file_flag,
       id_count, chunk_count, manager_count (always present in CK2 writer) */
    const uint32_t data_count = (uint32_t) sub->data.count;
    const uint32_t id_count = (uint32_t) sub->ids.count;
    const uint32_t chunk_count = (uint32_t) sub->chunk_refs.count;
    const uint32_t manager_count = (uint32_t) sub->managers.count;

    const uint32_t header_dwords = 8u;
    const uint32_t total_dwords = header_dwords + data_count + id_count +
                                  chunk_count + manager_count;

    result = nmo_chunk_write_dword(chunk, total_dwords - 1u);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->class_id);
    NMO_RETURN_IF_ERROR(result);

    uint32_t version_info = (uint32_t)(sub->data_version & 0xFFFFu) |
                            ((uint32_t)(sub->chunk_version & 0xFFFFu) << 16);
    result = nmo_chunk_write_dword(chunk, version_info);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, data_count);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(
        chunk,
        (sub->chunk_options & NMO_CHUNK_OPTION_FILE) ? 1u : 0u);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, id_count);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, chunk_count);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, manager_count);
    NMO_RETURN_IF_ERROR(result);

    if (data_count > 0) {
        const uint32_t *sub_data = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->data);
        result = nmo_chunk_write_buffer_no_size(chunk, sub_data, data_count * sizeof(uint32_t));
        NMO_RETURN_IF_ERROR(result);
    }

    if (id_count > 0) {
        const uint32_t *sub_ids = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->ids);
        result = nmo_chunk_write_buffer_no_size(chunk, sub_ids, id_count * sizeof(uint32_t));
        NMO_RETURN_IF_ERROR(result);
    }

    if (chunk_count > 0) {
        const uint32_t *sub_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->chunk_refs);
        result = nmo_chunk_write_buffer_no_size(chunk, sub_refs, chunk_count * sizeof(uint32_t));
        NMO_RETURN_IF_ERROR(result);
    }

    if (manager_count > 0) {
        const uint32_t *sub_mgrs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->managers);
        result = nmo_chunk_write_buffer_no_size(chunk, sub_mgrs, manager_count * sizeof(uint32_t));
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t *sub) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (sub == NULL) {
        return nmo_chunk_write_dword(chunk, 0u);
    }

    if (chunk->file_context != NULL && sub->file_context == NULL) {
        sub->file_context = chunk->file_context;
        sub->chunk_options |= NMO_CHUNK_OPTION_FILE;
    }

    /* Set CHN flag and track sub-chunk */
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    nmo_status_t list_result = nmo_arena_array_append(&chunk->chunks, &sub);
    NMO_RETURN_IF_ERROR(list_result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                               "Failed to get parser state");
    }

    size_t size_header_offset = state->current_pos;

    /* Track sub-chunk position before writing (CK2 AddEntry uses CurrentPos-1). */
    size_t refs_count_before = chunk->chunk_refs.count;
    nmo_status_t result;
    {
        uint32_t header_pos = (uint32_t) size_header_offset;
        result = nmo_arena_array_append(&chunk->chunk_refs, &header_pos);
    }
    NMO_RETURN_IF_ERROR(result);

    /* Write payload */
    result = write_sub_chunk_payload(chunk, sub);
    if (result != NMO_OK) {
        /* Roll back the appended entry to preserve consistency. */
        chunk->chunk_refs.count = refs_count_before;
        return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_sub_chunk_sequence(nmo_chunk_t *chunk, nmo_chunk_t *sub) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (sub == NULL) {
        return nmo_chunk_write_dword(chunk, 0u);
    }

    if (chunk->file_context != NULL && sub->file_context == NULL) {
        sub->file_context = chunk->file_context;
        sub->chunk_options |= NMO_CHUNK_OPTION_FILE;
    }

    /* In CK2, WriteSubChunkSequence does not add entries to the chunk refs list.
       The sequence marker added by StartSubChunkSequence tracks the sequence. */
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    nmo_status_t list_result = nmo_arena_array_append(&chunk->chunks, &sub);
    NMO_RETURN_IF_ERROR(list_result);

    return write_sub_chunk_payload(chunk, sub);
}

nmo_status_t nmo_chunk_start_read_sub_chunk_sequence(nmo_chunk_t *chunk, size_t *out_count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    // Read sequence count (stored as single DWORD)
    uint32_t count;
    nmo_status_t result = nmo_chunk_read_dword(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    // Store count if requested
    if (out_count) {
        *out_count = (size_t)count;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t **out_sub) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_sub, "Invalid arguments");

    nmo_status_t result;
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state == NULL) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                               "Failed to get parser state");
    }

    size_t start_pos = state->current_pos;
    *out_sub = NULL;

    // Read header
    uint32_t total_size, version_info, data_size, file_flag;
    uint32_t id_count, chunk_count, manager_count = 0;
    uint32_t class_id;  // CK2 reads as full DWORD

    result = nmo_chunk_read_dword(chunk, &total_size);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    if (total_size == 0) {
        *out_sub = NULL;
        NMO_RETURN_OK();
    }

    if (!nmo_chunk_has_read_capacity(chunk, (size_t) total_size)) {
        state->current_pos = start_pos;
        *out_sub = NULL;
        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Sub-chunk out of bounds");
    }

    // CK2 reads class_id as full DWORD, not WORD
    result = nmo_chunk_read_dword(chunk, &class_id);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_dword(chunk, &version_info);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_dword(chunk, &data_size);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_dword(chunk, &file_flag);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_dword(chunk, &id_count);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_dword(chunk, &chunk_count);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    {
        const uint32_t header_without_manager_dwords = 6u;
        if (total_size < header_without_manager_dwords) {
            state->current_pos = start_pos;
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Sub-chunk size is too small");
        }

        size_t payload_consumed = (size_t)data_size + (size_t)id_count + (size_t)chunk_count;
        size_t payload_capacity = (size_t)total_size - (size_t)header_without_manager_dwords;
        if (payload_consumed > payload_capacity) {
            state->current_pos = start_pos;
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Sub-chunk payload exceeds declared size");
        }

        size_t payload_remaining = payload_capacity - payload_consumed;
        if (payload_remaining > 0) {
            result = nmo_chunk_read_dword(chunk, &manager_count);
            if (result != NMO_OK) {
                state->current_pos = start_pos;
                return result;
            }

            if (manager_count != (uint32_t)(payload_remaining - 1u)) {
                state->current_pos = start_pos;
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Sub-chunk manager count does not match declared size");
            }
        }
    }

    // Create sub-chunk
    nmo_chunk_t *sub = nmo_chunk_create(chunk->arena);
    if (!sub) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to create sub-chunk");
    }

    sub->class_id = class_id;  // Use 32-bit class_id field for sub-chunks
    sub->chunk_class_id = (uint8_t) (class_id & 0xFFu);
    sub->data_version = (uint16_t) (version_info & 0xFFFFu);
    sub->chunk_version = (uint16_t) ((version_info >> 16) & 0xFFFFu);
    sub->chunk_options = 0;
    if (file_flag) sub->chunk_options |= NMO_CHUNK_OPTION_FILE;
    if (file_flag && chunk->file_context != NULL) {
        sub->file_context = chunk->file_context;
    }

    // Read data
    if (data_size > 0) {
        if (!nmo_chunk_has_read_capacity(chunk, data_size)) {
            state->current_pos = start_pos;
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR,
                                   "Insufficient data");
        }

        result = nmo_arena_array_resize(&sub->data, data_size);
        if (result != NMO_OK) {
            state->current_pos = start_pos;
            return result;
        }

        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_data = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->data);
        memcpy(sub_data,
               &parent_data[state->current_pos],
               data_size * sizeof(uint32_t));
        state->current_pos += data_size;
    }

    // Read IDs
    if (id_count > 0) {
        if (!nmo_chunk_has_read_capacity(chunk, id_count)) {
            state->current_pos = start_pos;
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR,
                                   "Insufficient IDs data");
        }

        result = nmo_arena_array_resize(&sub->ids, id_count);
        if (result != NMO_OK) {
            state->current_pos = start_pos;
            return result;
        }

        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_ids = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->ids);
        memcpy(sub_ids,
               &parent_data[state->current_pos],
               id_count * sizeof(uint32_t));
        state->current_pos += id_count;
    }

    // Read chunk refs (offset list)
    if (chunk_count > 0) {
        if (!nmo_chunk_has_read_capacity(chunk, chunk_count)) {
            state->current_pos = start_pos;
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR,
                                   "Insufficient chunk refs data");
        }

        result = nmo_arena_array_resize(&sub->chunk_refs, chunk_count);
        if (result != NMO_OK) {
            state->current_pos = start_pos;
            return result;
        }

        uint32_t *parent_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        uint32_t *sub_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->chunk_refs);
        memcpy(sub_refs,
               &parent_data[state->current_pos],
               chunk_count * sizeof(uint32_t));
        state->current_pos += chunk_count;
    }

    // Read managers list
    if (manager_count > 0) {
        if (!nmo_chunk_has_read_capacity(chunk, manager_count)) {
            state->current_pos = start_pos;
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR,
                                   "Insufficient manager refs data");
        }

        result = nmo_arena_array_resize(&sub->managers, manager_count);
        if (result != NMO_OK) {
            state->current_pos = start_pos;
            return result;
        }

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
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_start_sub_chunk_sequence(nmo_chunk_t *chunk, size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                               "Failed to get parser state");
    }

    // Track sequence start (CK2 AddEntries)
    uint32_t sentinel = 0xFFFFFFFFu;
    nmo_status_t result = nmo_arena_array_append(&chunk->chunk_refs, &sentinel);
    NMO_RETURN_IF_ERROR(result);
    {
        uint32_t pos = (uint32_t) state->current_pos;
        result = nmo_arena_array_append(&chunk->chunk_refs, &pos);
    }
    NMO_RETURN_IF_ERROR(result);

    return nmo_chunk_write_dword(chunk, (uint32_t) count);
}

// =============================================================================
// Accessors
// =============================================================================

/**
 * Add sub-chunk
 */
nmo_status_t nmo_chunk_add_sub_chunk(nmo_chunk_t *chunk, nmo_chunk_t *sub_chunk) {
    NMO_CHUNK_CHECK_ARGS(chunk, sub_chunk, "Invalid arguments");

    /* Add sub-chunk */
    nmo_status_t result = nmo_arena_array_append(&chunk->chunks, &sub_chunk);
    NMO_RETURN_IF_ERROR(result);

    /* Set CHN flag */
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;

    NMO_RETURN_OK();
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
