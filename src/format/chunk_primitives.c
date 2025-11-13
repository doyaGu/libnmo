// chunk_primitives.c - Primitive type serialization for CKStateChunk
// Implements: byte, word, int, dword, float, GUID, string, buffer, object_id

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include <string.h>

// =============================================================================
// Internal Helpers
// =============================================================================

static inline nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

static inline bool can_read(nmo_chunk_t *chunk, size_t dwords) {
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    return state && (state->current_pos + dwords <= chunk->data_size);
}

// =============================================================================
// Primitive Types - Write
// =============================================================================

nmo_result_t nmo_chunk_write_byte(nmo_chunk_t *chunk, uint8_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 1);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    chunk->data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_word(nmo_chunk_t *chunk, uint16_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 1);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    chunk->data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_int(nmo_chunk_t *chunk, int32_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 1);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    chunk->data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_dword(nmo_chunk_t *chunk, uint32_t value) {
    return nmo_chunk_write_int(chunk, (int32_t) value);
}

nmo_result_t nmo_chunk_write_float(nmo_chunk_t *chunk, float value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 1);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    // Store float as raw bits
    chunk->data[state->current_pos++] = *(uint32_t *) &value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_guid(nmo_chunk_t *chunk, nmo_guid_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 2);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    chunk->data[state->current_pos++] = value.d1;
    chunk->data[state->current_pos++] = value.d2;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

// =============================================================================
// Primitive Types - Read
// =============================================================================

nmo_result_t nmo_chunk_read_byte(nmo_chunk_t *chunk, uint8_t *out_value) {
    if (!chunk || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 1)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    *out_value = (uint8_t) (chunk->data[state->current_pos++] & 0xFF);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_word(nmo_chunk_t *chunk, uint16_t *out_value) {
    if (!chunk || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 1)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    *out_value = (uint16_t) (chunk->data[state->current_pos++] & 0xFFFF);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_int(nmo_chunk_t *chunk, int32_t *out_value) {
    if (!chunk || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 1)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    *out_value = (int32_t) chunk->data[state->current_pos++];

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_dword(nmo_chunk_t *chunk, uint32_t *out_value) {
    return nmo_chunk_read_int(chunk, (int32_t *) out_value);
}

nmo_result_t nmo_chunk_read_float(nmo_chunk_t *chunk, float *out_value) {
    if (!chunk || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 1)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    // Read float as raw bits
    *out_value = *(float *) &chunk->data[state->current_pos++];

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_guid(nmo_chunk_t *chunk, nmo_guid_t *out_value) {
    if (!chunk || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 2)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    out_value->d1 = chunk->data[state->current_pos++];
    out_value->d2 = chunk->data[state->current_pos++];

    return nmo_result_ok();
}

// =============================================================================
// Complex Types - String
// =============================================================================

nmo_result_t nmo_chunk_write_string(nmo_chunk_t *chunk, const char *str) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Calculate size
    size_t len = str ? strlen(str) + 1 : 0; // Include null terminator
    size_t dwords = (len + 3) / 4;          // Round up to DWORDs

    // Write length
    nmo_result_t result = nmo_chunk_check_size(chunk, 1 + dwords);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    chunk->data[state->current_pos++] = (uint32_t) len;

    // Write string data
    if (len > 0) {
        memcpy(&chunk->data[state->current_pos], str, len);
        state->current_pos += dwords;
    }

    // Update data_size
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

size_t nmo_chunk_read_string(nmo_chunk_t *chunk, char **out_str) {
    if (!chunk || !out_str) {
        return 0;
    }

    if (!can_read(chunk, 1)) {
        *out_str = NULL;
        return 0;
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t len = chunk->data[state->current_pos++];

    if (len == 0) {
        *out_str = NULL;
        return 0;
    }

    size_t dwords = (len + 3) / 4;
    if (!can_read(chunk, dwords)) {
        *out_str = NULL;
        return 0;
    }

    // Allocate from arena
    char *str = (char *) nmo_arena_alloc(chunk->arena, len, 1);
    if (!str) {
        *out_str = NULL;
        return 0;
    }

    memcpy(str, &chunk->data[state->current_pos], len);
    state->current_pos += dwords;

    *out_str = str;
    return len - 1; // Exclude null terminator
}

// =============================================================================
// Complex Types - Buffer
// =============================================================================

nmo_result_t nmo_chunk_write_buffer(nmo_chunk_t *chunk,
                                    const void *data,
                                    size_t size) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    size_t dwords = (size + 3) / 4;

    // Write size
    nmo_result_t result = nmo_chunk_check_size(chunk, 1 + dwords);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    chunk->data[state->current_pos++] = (uint32_t) size;

    // Write data
    if (size > 0 && data) {
        memcpy(&chunk->data[state->current_pos], data, size);
        state->current_pos += dwords;
    }

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_buffer_no_size(nmo_chunk_t *chunk,
                                            const void *data,
                                            size_t size) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (size == 0) {
        return nmo_result_ok();
    }

    size_t dwords = (size + 3) / 4;

    nmo_result_t result = nmo_chunk_check_size(chunk, dwords);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    memcpy(&chunk->data[state->current_pos], data, size);
    state->current_pos += dwords;

    // Update data_size
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_buffer(nmo_chunk_t *chunk,
                                   void **out_data,
                                   size_t *out_size) {
    if (!chunk || !out_data || !out_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 1)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t size = chunk->data[state->current_pos++];

    *out_size = size;

    if (size == 0) {
        *out_data = NULL;
        return nmo_result_ok();
    }

    size_t dwords = (size + 3) / 4;
    if (!can_read(chunk, dwords)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Cannot read beyond data"));
    }

    // Allocate from arena
    void *data = nmo_arena_alloc(chunk->arena, size, 1);
    if (!data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate buffer"));
    }

    memcpy(data, &chunk->data[state->current_pos], size);
    state->current_pos += dwords;

    *out_data = data;
    return nmo_result_ok();
}

size_t nmo_chunk_read_and_fill_buffer(nmo_chunk_t *chunk,
                                      void *buffer,
                                      size_t buffer_size) {
    if (!chunk || !buffer) {
        return 0;
    }

    if (!can_read(chunk, 1)) {
        return 0;
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t size = chunk->data[state->current_pos++];

    if (size == 0) {
        return 0;
    }

    if (size > buffer_size) {
        return 0; // Buffer too small
    }

    size_t dwords = (size + 3) / 4;
    if (!can_read(chunk, dwords)) {
        return 0;
    }

    memcpy(buffer, &chunk->data[state->current_pos], size);
    state->current_pos += dwords;

    return size;
}

// =============================================================================
// Object References
// =============================================================================

nmo_result_t nmo_chunk_write_object_id(nmo_chunk_t *chunk, nmo_object_id_t id) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 1);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    // Track position if ID is non-zero
    if (id != 0) {
        // Ensure IDS list exists
        if (!chunk->ids) {
            size_t initial_capacity = 16;
            chunk->ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                      initial_capacity * sizeof(uint32_t), sizeof(uint32_t));
            if (!chunk->ids) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate IDS list"));
            }
            chunk->id_capacity = initial_capacity;
            chunk->id_count = 0;
        }

        // Grow list if needed
        if (chunk->id_count >= chunk->id_capacity) {
            size_t new_capacity = chunk->id_capacity * 2;
            uint32_t *new_ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                             new_capacity * sizeof(uint32_t), sizeof(uint32_t));
            if (!new_ids) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to grow IDS list"));
            }
            memcpy(new_ids, chunk->ids, chunk->id_count * sizeof(uint32_t));
            chunk->ids = new_ids;
            chunk->id_capacity = new_capacity;
        }

        // Record position
        chunk->ids[chunk->id_count++] = (uint32_t) state->current_pos;
        chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }

    chunk->data[state->current_pos++] = id;

    // Update data_size to track written data
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_object_id(nmo_chunk_t *chunk, nmo_object_id_t *out_id) {
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

    return nmo_result_ok();
}
