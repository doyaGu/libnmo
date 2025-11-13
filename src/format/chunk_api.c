/**
 * @file nmo_chunk_api.c
 * @brief Implementation of high-level chunk API
 */

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_math.h"
#include <string.h>
#include <assert.h>
#include <miniz.h>

// =============================================================================
// Internal Helpers
// =============================================================================

/**
 * Get or create parser state
 */
static nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    if (!chunk) return NULL;

    // Initialize parser state if not already created
    if (!chunk->parser_state) {
        chunk->parser_state = nmo_arena_alloc(chunk->arena,
                                              sizeof(nmo_chunk_parser_state_t),
                                              _Alignof(nmo_chunk_parser_state_t));
        if (!chunk->parser_state) return NULL;
        memset(chunk->parser_state, 0, sizeof(nmo_chunk_parser_state_t));
    }

    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

/**
 * Check if position is valid for reading
 */
static inline bool can_read(const nmo_chunk_t *chunk, size_t dwords) {
    nmo_chunk_parser_state_t *state = get_parser_state((nmo_chunk_t *) chunk);
    if (!state) return false;
    return (state->current_pos + dwords) <= chunk->data_size;
}

// =============================================================================
// Lifecycle & Mode Management
// =============================================================================

nmo_result_t nmo_chunk_start_read(nmo_chunk_t *chunk) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    state->current_pos = 0;
    state->prev_identifier_pos = 0;
    state->data_size = chunk->data_size;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_write(nmo_chunk_t *chunk) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    state->current_pos = 0;
    state->prev_identifier_pos = 0;
    state->data_size = chunk->data_capacity;

    // Initialize data if needed
    if (!chunk->data && chunk->arena) {
        size_t initial_capacity = 64; // Start with 64 DWORDs
        chunk->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                   initial_capacity * sizeof(uint32_t), sizeof(uint32_t));
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

void nmo_chunk_update_data_size(nmo_chunk_t *chunk) {
    if (!chunk) return;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (state && state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }
}

// =============================================================================
// Metadata
// =============================================================================

nmo_class_id_t nmo_chunk_get_class_id(const nmo_chunk_t *chunk) {
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
                                          NMO_SEVERITY_ERROR, "Position out of bounds"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
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
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    if (state->current_pos + dwords > chunk->data_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_OUT_OF_BOUNDS,
                                          NMO_SEVERITY_ERROR, "Skip would exceed data size"));
    }

    state->current_pos += dwords;
    return nmo_result_ok();
}

// =============================================================================
// Memory Management
// =============================================================================

nmo_result_t nmo_chunk_check_size(nmo_chunk_t *chunk, size_t needed_dwords) {
    if (!chunk || !chunk->arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk or missing arena"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    size_t required_capacity = state->current_pos + needed_dwords;

    if (required_capacity <= chunk->data_capacity) {
        return nmo_result_ok(); // Already have enough space
    }

    // Need to grow - double until we have enough
    size_t new_capacity = chunk->data_capacity;
    if (new_capacity == 0) new_capacity = 64;

    while (new_capacity < required_capacity) {
        new_capacity *= 2;
    }

    // Allocate new buffer
    uint32_t *new_data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                      new_capacity * sizeof(uint32_t), sizeof(uint32_t));
    if (!new_data) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate larger buffer"));
    }

    // Copy existing data
    if (chunk->data && state->current_pos > 0) {
        memcpy(new_data, chunk->data, state->current_pos * sizeof(uint32_t));
    }

    chunk->data = new_data;
    chunk->data_capacity = new_capacity;

    return nmo_result_ok();
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

// =============================================================================
// Sequences
// =============================================================================

nmo_result_t nmo_chunk_start_object_sequence(nmo_chunk_t *chunk, size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Set IDS option
    chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;

    // Write count
    return nmo_chunk_write_int(chunk, (int32_t) count);
}

nmo_result_t nmo_chunk_sequence_write_object_id(nmo_chunk_t *chunk, nmo_object_id_t id) {
    return nmo_chunk_write_object_id(chunk, id);
}

// Deprecated alias for backwards compatibility
nmo_result_t nmo_chunk_write_object_id_sequence(nmo_chunk_t *chunk, nmo_object_id_t id) {
    return nmo_chunk_sequence_write_object_id(chunk, id);
}

nmo_result_t nmo_chunk_start_read_sequence(nmo_chunk_t *chunk, size_t *out_count) {
    if (!chunk || !out_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int32_t count;
    nmo_result_t result = nmo_chunk_read_int(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = (size_t) count;
    return nmo_result_ok();
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

// =============================================================================
// Manager Sequences
// =============================================================================

nmo_result_t nmo_chunk_start_manager_sequence(nmo_chunk_t *chunk,
                                              nmo_guid_t manager_guid,
                                              size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Set MAN flag
    chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;

    // Write manager GUID (2 DWORDs)
    nmo_result_t result = nmo_chunk_write_guid(chunk, manager_guid);
    if (result.code != NMO_OK) return result;

    // Write count
    return nmo_chunk_write_dword(chunk, (uint32_t) count);
}

nmo_result_t nmo_chunk_write_manager_int(nmo_chunk_t *chunk,
                                         nmo_manager_id_t mgr_id,
                                         uint32_t value) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    nmo_result_t result = nmo_chunk_check_size(chunk, 2);
    if (result.code != NMO_OK) return result;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Failed to get parser state"));
    }

    // Track manager position
    if (!chunk->managers) {
        chunk->manager_capacity = 16;
        chunk->managers = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                       chunk->manager_capacity * sizeof(uint32_t), sizeof(uint32_t));
        if (!chunk->managers) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate managers list"));
        }
    } else if (chunk->manager_count >= chunk->manager_capacity) {
        size_t new_capacity = chunk->manager_capacity * 2;
        uint32_t *new_managers = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                              new_capacity * sizeof(uint32_t), sizeof(uint32_t));
        if (!new_managers) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to grow managers list"));
        }
        memcpy(new_managers, chunk->managers, chunk->manager_count * sizeof(uint32_t));
        chunk->managers = new_managers;
        chunk->manager_capacity = new_capacity;
    }

    chunk->managers[chunk->manager_count++] = state->current_pos;

    // Write manager ID and value
    chunk->data[state->current_pos++] = mgr_id;
    chunk->data[state->current_pos++] = value;

    // Update data_size
    if (state->current_pos > chunk->data_size) {
        chunk->data_size = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_manager_int(nmo_chunk_t *chunk,
                                        nmo_manager_id_t *out_mgr_id,
                                        uint32_t *out_value) {
    if (!chunk || !out_mgr_id || !out_value) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (!can_read(chunk, 2)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Insufficient data for manager int"));
    }

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    *out_mgr_id = chunk->data[state->current_pos++];
    *out_value = chunk->data[state->current_pos++];

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_manager_read_sequence(nmo_chunk_t *chunk,
                                                   nmo_guid_t *out_manager_guid,
                                                   size_t *out_count) {
    if (!chunk || !out_manager_guid || !out_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Read manager GUID (2 DWORDs)
    nmo_result_t result = nmo_chunk_read_guid(chunk, out_manager_guid);
    if (result.code != NMO_OK) return result;

    // Read count
    uint32_t count_u32 = 0;
    result = nmo_chunk_read_dword(chunk, &count_u32);
    if (result.code != NMO_OK) return result;

    *out_count = (size_t)count_u32;
    return nmo_result_ok();
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

    // Track sub-chunk - record the position where we'll write the header
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

    // Calculate total size of embedded sub-chunk (in DWORDs)
    // Format: [total_size][classid][version][datasize][file_flag][id_count][chunk_count][manager_count]
    //         [data...][ids...][chunks...][managers...]
    size_t total_size = 8;        // Header: 8 DWORDs
    total_size += sub->data_size; // Data buffer
    if (sub->ids) total_size += sub->id_count;
    if (sub->chunks) total_size += sub->chunk_count;
    // Managers not yet implemented

    nmo_result_t result;
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    // Remember where the embedded data section will start (after the 8-word header)
    size_t embedded_data_offset = state->current_pos + 8;

    // Write total size (excluding the size field itself, so -1)
    result = nmo_chunk_write_dword(chunk, (uint32_t) (total_size - 1));
    if (result.code != NMO_OK) return result;

    // Write header
    result = nmo_chunk_write_word(chunk, sub->chunk_class_id);
    if (result.code != NMO_OK) return result;

    // Pack version info (data_version | chunk_version << 16)
    uint32_t version_info = sub->data_version | (sub->chunk_version << 16);
    result = nmo_chunk_write_dword(chunk, version_info);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->data_size);
    if (result.code != NMO_OK) return result;

    // File flag (not used in our implementation yet)
    result = nmo_chunk_write_dword(chunk, 0);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->id_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, (uint32_t) sub->chunk_count);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(chunk, 0); // Manager count (not implemented)
    if (result.code != NMO_OK) return result;

    // Write data buffer
    if (sub->data_size > 0) {
        result = nmo_chunk_write_buffer_no_size(chunk, sub->data, sub->data_size * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write IDs list
    if (sub->ids && sub->id_count > 0) {
        result = nmo_chunk_write_buffer_no_size(chunk, sub->ids, sub->id_count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Write chunks list (offsets)
    if (sub->chunks && sub->chunk_count > 0) {
        // Note: This writes pointers as integers, which is wrong for real file format
        // but we're simplifying for now
        result = nmo_chunk_write_buffer_no_size(chunk, sub->chunks, sub->chunk_count * sizeof(uint32_t));
        if (result.code != NMO_OK) return result;
    }

    // Managers not implemented

    // CRITICAL FIX: Update the sub-chunk's data pointer to point to the embedded copy in the parent!
    // This ensures that when we remap IDs, we're modifying the embedded data, not the original.
    sub->data = &chunk->data[embedded_data_offset];
    // Also update the IDs pointer if it exists
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

    // Read sub-chunk header
    uint32_t total_size;
    result = nmo_chunk_read_dword(chunk, &total_size);
    if (result.code != NMO_OK) return result;

    uint16_t class_id;
    result = nmo_chunk_read_word(chunk, &class_id);
    if (result.code != NMO_OK) return result;

    uint32_t version_info;
    result = nmo_chunk_read_dword(chunk, &version_info);
    if (result.code != NMO_OK) return result;

    uint32_t data_size;
    result = nmo_chunk_read_dword(chunk, &data_size);
    if (result.code != NMO_OK) return result;

    uint32_t file_flag;
    result = nmo_chunk_read_dword(chunk, &file_flag);
    if (result.code != NMO_OK) return result;

    uint32_t id_count;
    result = nmo_chunk_read_dword(chunk, &id_count);
    if (result.code != NMO_OK) return result;

    uint32_t chunk_count;
    result = nmo_chunk_read_dword(chunk, &chunk_count);
    if (result.code != NMO_OK) return result;

    uint32_t manager_count;
    result = nmo_chunk_read_dword(chunk, &manager_count);
    if (result.code != NMO_OK) return result;

    // Create new sub-chunk
    nmo_chunk_t *sub = nmo_chunk_create(chunk->arena);
    if (!sub) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to create sub-chunk"));
    }

    sub->chunk_class_id = class_id;
    sub->data_version = (uint8_t) (version_info & 0xFF);
    sub->chunk_version = (uint8_t) ((version_info >> 16) & 0xFF);

    // Read data buffer
    if (data_size > 0) {
        if (!can_read(chunk, data_size)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient data for sub-chunk data"));
        }

        sub->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                 data_size * sizeof(uint32_t), sizeof(uint32_t));
        if (!sub->data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate sub-chunk data"));
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        memcpy(sub->data, &chunk->data[state->current_pos], data_size * sizeof(uint32_t));
        state->current_pos += data_size;

        sub->data_size = data_size;
        sub->data_capacity = data_size;
    }

    // Read IDs list
    if (id_count > 0) {
        if (!can_read(chunk, id_count)) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR, "Insufficient data for sub-chunk IDs"));
        }

        sub->ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                id_count * sizeof(uint32_t), sizeof(uint32_t));
        if (!sub->ids) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate sub-chunk IDs"));
        }

        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        memcpy(sub->ids, &chunk->data[state->current_pos], id_count * sizeof(uint32_t));
        state->current_pos += id_count;

        sub->id_count = id_count;
        sub->id_capacity = id_count;
    }

    // Read chunks list (skip for now - not fully implemented)
    if (chunk_count > 0) {
        nmo_chunk_parser_state_t *state = get_parser_state(chunk);
        state->current_pos += chunk_count; // Skip
    }

    // Managers not implemented

    *out_sub = sub;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_start_sub_chunk_sequence(nmo_chunk_t *chunk, size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Set CHN flag
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;

    // Write count
    return nmo_chunk_write_dword(chunk, (uint32_t) count);
}

// =============================================================================
// Arrays
// =============================================================================

nmo_result_t nmo_chunk_write_array(nmo_chunk_t *chunk,
                                   const void *array,
                                   size_t count,
                                   size_t elem_size) {
    if (!chunk || !array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write element size
    result = nmo_chunk_write_dword(chunk, (uint32_t) elem_size);
    if (result.code != NMO_OK) return result;

    // Write array data
    size_t total_size = count * elem_size;
    return nmo_chunk_write_buffer_no_size(chunk, array, total_size);
}

nmo_result_t nmo_chunk_read_array(nmo_chunk_t *chunk,
                                  void **out_array,
                                  size_t *out_count,
                                  size_t *out_elem_size) {
    if (!chunk || !out_array || !out_count || !out_elem_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Read count
    uint32_t count;
    nmo_result_t result = nmo_chunk_read_dword(chunk, &count);
    if (result.code != NMO_OK) return result;

    // Read element size
    uint32_t elem_size;
    result = nmo_chunk_read_dword(chunk, &elem_size);
    if (result.code != NMO_OK) return result;

    // Calculate total size
    size_t total_size = count * elem_size;
    size_t dwords = (total_size + 3) / 4;

    if (!can_read(chunk, dwords)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR, "Insufficient data for array"));
    }

    // Allocate array
    void *array = nmo_arena_alloc(chunk->arena, total_size, 4);
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate array"));
    }

    // Copy data
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    memcpy(array, &chunk->data[state->current_pos], total_size);
    state->current_pos += dwords;

    *out_array = array;
    *out_count = count;
    *out_elem_size = elem_size;

    return nmo_result_ok();
}

// =============================================================================
// P2: Advanced Features - Compression & CRC
// =============================================================================

nmo_result_t nmo_chunk_pack(nmo_chunk_t *chunk, int compression_level) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (chunk->data_size == 0) {
        return nmo_result_ok(); // Nothing to compress
    }

    // Already packed?
    if (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) {
        return nmo_result_ok();
    }

    // Clamp compression level
    if (compression_level < 0) compression_level = 6; // Default
    if (compression_level > 9) compression_level = 9;

    // Calculate maximum compressed size
    mz_ulong src_len = chunk->data_size * sizeof(uint32_t);
    mz_ulong dest_len = mz_compressBound(src_len);

    // Allocate temporary buffer for compressed data
    uint8_t *compressed = (uint8_t *) nmo_arena_alloc(chunk->arena, dest_len, 4);
    if (!compressed) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate compression buffer"));
    }

    // Compress
    int result = mz_compress2(compressed, &dest_len,
                              (const unsigned char *) chunk->data, src_len,
                              compression_level);

    if (result != MZ_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Compression failed"));
    }

    // Only use compressed data if it's actually smaller
    if (dest_len < src_len) {
        // Store original size
        chunk->unpack_size = chunk->data_size;

        // Calculate DWORDs needed for compressed data
        size_t compressed_dwords = (dest_len + 3) / 4;

        // Allocate new data buffer
        uint32_t *new_data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          compressed_dwords * sizeof(uint32_t), sizeof(uint32_t));
        if (!new_data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate packed data buffer"));
        }

        // Copy compressed data
        memcpy(new_data, compressed, dest_len);

        // Update chunk
        chunk->data = new_data;
        chunk->data_size = compressed_dwords;
        chunk->data_capacity = compressed_dwords;
        chunk->chunk_options |= NMO_CHUNK_OPTION_PACKED;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_unpack(nmo_chunk_t *chunk) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    // Not packed?
    if (!(chunk->chunk_options & NMO_CHUNK_OPTION_PACKED)) {
        return nmo_result_ok(); // Nothing to decompress
    }

    if (chunk->unpack_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR, "No unpack size specified"));
    }

    // Allocate buffer for decompressed data
    mz_ulong dest_len = chunk->unpack_size * sizeof(uint32_t);
    uint32_t *decompressed = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          dest_len, sizeof(uint32_t));
    if (!decompressed) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate decompression buffer"));
    }

    // Decompress
    mz_ulong src_len = chunk->data_size * sizeof(uint32_t);
    int result = mz_uncompress((unsigned char *) decompressed, &dest_len,
                               (const unsigned char *) chunk->data, src_len);

    if (result != MZ_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Decompression failed"));
    }

    // Verify decompressed size
    if (dest_len != chunk->unpack_size * sizeof(uint32_t)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CORRUPT,
                                          NMO_SEVERITY_ERROR, "Decompressed size mismatch"));
    }

    // Update chunk
    chunk->data = decompressed;
    chunk->data_size = chunk->unpack_size;
    chunk->data_capacity = chunk->unpack_size;
    chunk->chunk_options &= ~NMO_CHUNK_OPTION_PACKED;
    chunk->unpack_size = 0;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_compute_crc(nmo_chunk_t *chunk,
                                   uint32_t initial_crc,
                                   uint32_t *out_crc) {
    if (!chunk || !out_crc) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (chunk->data_size == 0) {
        *out_crc = initial_crc;
        return nmo_result_ok();
    }

    // Compute Adler32 CRC
    size_t byte_size = chunk->data_size * sizeof(uint32_t);
    mz_ulong crc = mz_adler32(initial_crc,
                              (const unsigned char *) chunk->data,
                              byte_size);

    *out_crc = (uint32_t) crc;
    return nmo_result_ok();
}

// =============================================================================
// ID Remapping
// =============================================================================

#include "format/nmo_id_remap.h"

/**
 * @brief Helper to remap a single object ID
 *
 * @param id_ref Pointer to ID in chunk data
 * @param remap Remap table
 * @return 1 if remapped, 0 if not found or unchanged
 */
static int remap_single_id(nmo_object_id_t *id_ref, const nmo_id_remap_t *remap) {
    if (!id_ref || *id_ref == 0) return 0;

    nmo_object_id_t old_id = *id_ref;
    nmo_object_id_t new_id;

    nmo_result_t result = nmo_id_remap_lookup_id(remap, old_id, &new_id);
    if (result.code == NMO_OK) {
        if (new_id != 0 && new_id != old_id) {
            *id_ref = new_id;
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Recursively remap object IDs in chunk and sub-chunks
 *
 * Based on CKStateChunk::ObjectRemapper and IterateAndDo.
 * This version parses embedded sub-chunks from the parent's data buffer
 * and remaps them in-place, matching CKStateChunk behavior.
 *
 * @param chunk_data Pointer to chunk's data buffer
 * @param data_size Size of data buffer in DWORDs
 * @param ids Pointer to IDs list (offsets into data buffer)
 * @param id_count Number of entries in IDs list
 * @param remap Remap table
 * @param remapped_count Output for total remapped count
 * @return NMO_OK on success, error code on failure
 */
static nmo_result_t remap_chunk_data_recursive(uint32_t *chunk_data,
                                               size_t data_size,
                                               uint32_t *ids,
                                               size_t id_count,
                                               const nmo_id_remap_t *remap,
                                               int *remapped_count) {
    if (!chunk_data || !remap || !remapped_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int local_count = 0;

    // Process object IDs using the ids list
    if (ids && id_count > 0) {
        size_t i = 0;
        while (i < id_count) {
            int id_offset = (int) ids[i];

            if (id_offset >= 0) {
                // Single object ID at this offset
                if ((size_t) id_offset < data_size) {
                    nmo_object_id_t *id_ref = (nmo_object_id_t *) &chunk_data[id_offset];
                    local_count += remap_single_id(id_ref, remap);
                }
                i++;
            } else {
                // Sequence of object IDs
                i++;
                if (i >= id_count) break;

                int sequence_header_offset = (int) ids[i];
                if (sequence_header_offset >= 0 &&
                    (size_t) sequence_header_offset < data_size) {
                    // Sequence format: [count, id1, id2, ...]
                    int count = (int) chunk_data[sequence_header_offset];
                    size_t sequence_start = sequence_header_offset + 1;

                    if (count > 0 && sequence_start + count <= data_size) {
                        for (int k = 0; k < count; k++) {
                            nmo_object_id_t *id_ref = (nmo_object_id_t *) &chunk_data[sequence_start + k];
                            local_count += remap_single_id(id_ref, remap);
                        }
                    }
                }
                i++;
            }
        }
    }

    *remapped_count += local_count;
    return nmo_result_ok();
}

/**
 * @brief Recursively remap object IDs in chunk and embedded sub-chunks
 *
 * This function processes the chunk's own IDs, then parses any embedded
 * sub-chunks from the data buffer and recursively remaps them in-place.
 *
 * @param chunk Chunk to process
 * @param remap Remap table
 * @param remapped_count Output for total remapped count
 * @return NMO_OK on success, error code on failure
 */
static nmo_result_t remap_object_ids_recursive(nmo_chunk_t *chunk,
                                               const nmo_id_remap_t *remap,
                                               int *remapped_count) {
    if (!chunk || !remap || !remapped_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int local_count = 0;

    // Only process chunks with version >= CHUNK_VERSION1 (4)
    // Older formats with magic markers are not supported
    if (chunk->chunk_version < NMO_CHUNK_VERSION1) {
        return nmo_result_ok(); // Skip legacy format
    }

    // Remap IDs in this chunk's data buffer
    nmo_result_t result = remap_chunk_data_recursive(chunk->data, chunk->data_size,
                                                     chunk->ids, chunk->id_count,
                                                     remap, &local_count);
    if (result.code != NMO_OK) return result;

    // Recursively process sub-chunks
    // After nmo_chunk_write_sub_chunk(), the sub-chunk's data pointer points to the embedded
    // copy in the parent's buffer, so we can safely remap it
    if (chunk->chunks && chunk->chunk_count > 0) {
        for (size_t i = 0; i < chunk->chunk_count; i++) {
            if (chunk->chunks[i]) {
                int sub_remapped = 0;
                result = remap_object_ids_recursive(chunk->chunks[i], remap, &sub_remapped);
                if (result.code != NMO_OK) return result;
                local_count += sub_remapped;
            }
        }
    }

    *remapped_count += local_count;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_remap_object_ids(nmo_chunk_t *chunk,
                                        const nmo_id_remap_t *remap) {
    if (!chunk || !remap) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    int remapped_count = 0;
    return remap_object_ids_recursive(chunk, remap, &remapped_count);
}

// =============================================================================
// Helper Functions for Common Patterns
// =============================================================================

nmo_result_t nmo_chunk_read_object_id_array(nmo_chunk_t *chunk,
                                             nmo_object_id_t **out_ids,
                                             size_t *out_count,
                                             nmo_arena_t *arena) {
    if (!chunk || !out_ids || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_start_read_sequence(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_ids = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    nmo_object_id_t *ids = (nmo_object_id_t *) nmo_arena_alloc(arena, 
                                                                count * sizeof(nmo_object_id_t),
                                                                _Alignof(nmo_object_id_t));
    if (!ids) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate ID array"));
    }

    // Read IDs
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_object_id(chunk, &ids[i]);
        if (result.code != NMO_OK) return result;
    }

    *out_ids = ids;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_object_id_array(nmo_chunk_t *chunk,
                                              const nmo_object_id_t *ids,
                                              size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (count > 0 && !ids) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Non-zero count with NULL array"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write IDs
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_object_id(chunk, ids[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_int_array(nmo_chunk_t *chunk,
                                       int32_t **out_array,
                                       size_t *out_count,
                                       nmo_arena_t *arena) {
    if (!chunk || !out_array || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_start_read_sequence(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    int32_t *array = (int32_t *) nmo_arena_alloc(arena, 
                                                  count * sizeof(int32_t),
                                                  _Alignof(int32_t));
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate int array"));
    }

    // Read ints
    for (size_t i = 0; i < count; i++) {
        int32_t value;
        result = nmo_chunk_read_int(chunk, &value);
        if (result.code != NMO_OK) return result;
        array[i] = value;
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_int_array(nmo_chunk_t *chunk,
                                        const int32_t *array,
                                        size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (count > 0 && !array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Non-zero count with NULL array"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write ints
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_int(chunk, array[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_float_array(nmo_chunk_t *chunk,
                                         float **out_array,
                                         size_t *out_count,
                                         nmo_arena_t *arena) {
    if (!chunk || !out_array || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_start_read_sequence(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    float *array = (float *) nmo_arena_alloc(arena, 
                                             count * sizeof(float),
                                             _Alignof(float));
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate float array"));
    }

    // Read floats
    for (size_t i = 0; i < count; i++) {
        float value;
        result = nmo_chunk_read_float(chunk, &value);
        if (result.code != NMO_OK) return result;
        array[i] = value;
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_float_array(nmo_chunk_t *chunk,
                                          const float *array,
                                          size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (count > 0 && !array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Non-zero count with NULL array"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write floats
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_float(chunk, array[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

// =============================================================================
// Additional Type-Specialized Helpers
// =============================================================================

nmo_result_t nmo_chunk_read_dword_array(nmo_chunk_t *chunk,
                                         uint32_t **out_array,
                                         size_t *out_count,
                                         nmo_arena_t *arena) {
    if (!chunk || !out_array || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_start_read_sequence(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    uint32_t *array = (uint32_t *) nmo_arena_alloc(arena, 
                                                    count * sizeof(uint32_t),
                                                    _Alignof(uint32_t));
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate dword array"));
    }

    // Read dwords
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_dword(chunk, &array[i]);
        if (result.code != NMO_OK) return result;
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_dword_array(nmo_chunk_t *chunk,
                                          const uint32_t *array,
                                          size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (count > 0 && !array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Non-zero count with NULL array"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write dwords
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_dword(chunk, array[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_byte_array(nmo_chunk_t *chunk,
                                        uint8_t **out_array,
                                        size_t *out_count,
                                        nmo_arena_t *arena) {
    if (!chunk || !out_array || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_start_read_sequence(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    uint8_t *array = (uint8_t *) nmo_arena_alloc(arena, 
                                                  count * sizeof(uint8_t),
                                                  _Alignof(uint8_t));
    if (!array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate byte array"));
    }

    // Read bytes
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_byte(chunk, &array[i]);
        if (result.code != NMO_OK) return result;
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_byte_array(nmo_chunk_t *chunk,
                                         const uint8_t *array,
                                         size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (count > 0 && !array) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Non-zero count with NULL array"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write bytes
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_byte(chunk, array[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_string_array(nmo_chunk_t *chunk,
                                          char ***out_strings,
                                          size_t *out_count,
                                          nmo_arena_t *arena) {
    if (!chunk || !out_strings || !out_count || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_start_read_sequence(chunk, &count);
    if (result.code != NMO_OK) return result;

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_strings = NULL;
        return nmo_result_ok();
    }

    // Allocate string pointer array
    char **strings = (char **) nmo_arena_alloc(arena, 
                                                count * sizeof(char *),
                                                _Alignof(char *));
    if (!strings) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate string array"));
    }

    // Read strings
    for (size_t i = 0; i < count; i++) {
        size_t len = nmo_chunk_read_string(chunk, &strings[i]);
        if (len == 0 && strings[i] == NULL) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
                                              NMO_SEVERITY_ERROR, "Failed to read string"));
        }
    }

    *out_strings = strings;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_string_array(nmo_chunk_t *chunk,
                                           const char * const *strings,
                                           size_t count) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (count > 0 && !strings) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Non-zero count with NULL array"));
    }

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result.code != NMO_OK) return result;

    // Write strings
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_string(chunk, strings[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

// =============================================================================
// Math Type Helpers
// =============================================================================

nmo_result_t nmo_chunk_read_vector2(nmo_chunk_t *chunk, nmo_vector2_t *out_vec) {
    if (!chunk || !out_vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_read_float(chunk, &out_vec->x);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_read_float(chunk, &out_vec->y);
}

nmo_result_t nmo_chunk_write_vector2(nmo_chunk_t *chunk, const nmo_vector2_t *vec) {
    if (!chunk || !vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_write_float(chunk, vec->x);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_write_float(chunk, vec->y);
}

nmo_result_t nmo_chunk_read_vector3(nmo_chunk_t *chunk, nmo_vector_t *out_vec) {
    if (!chunk || !out_vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_read_float(chunk, &out_vec->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_vec->y);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_read_float(chunk, &out_vec->z);
}

nmo_result_t nmo_chunk_write_vector3(nmo_chunk_t *chunk, const nmo_vector_t *vec) {
    if (!chunk || !vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_write_float(chunk, vec->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, vec->y);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_write_float(chunk, vec->z);
}

nmo_result_t nmo_chunk_read_vector4(nmo_chunk_t *chunk, nmo_vector4_t *out_vec) {
    if (!chunk || !out_vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_read_float(chunk, &out_vec->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_vec->y);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_vec->z);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_read_float(chunk, &out_vec->w);
}

nmo_result_t nmo_chunk_write_vector4(nmo_chunk_t *chunk, const nmo_vector4_t *vec) {
    if (!chunk || !vec) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_write_float(chunk, vec->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, vec->y);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, vec->z);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_write_float(chunk, vec->w);
}

nmo_result_t nmo_chunk_read_quaternion(nmo_chunk_t *chunk, nmo_quaternion_t *out_quat) {
    if (!chunk || !out_quat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_read_float(chunk, &out_quat->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_quat->y);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_quat->z);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_read_float(chunk, &out_quat->w);
}

nmo_result_t nmo_chunk_write_quaternion(nmo_chunk_t *chunk, const nmo_quaternion_t *quat) {
    if (!chunk || !quat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_write_float(chunk, quat->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, quat->y);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, quat->z);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_write_float(chunk, quat->w);
}

nmo_result_t nmo_chunk_read_matrix(nmo_chunk_t *chunk, nmo_matrix_t *out_mat) {
    if (!chunk || !out_mat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Read 16 floats (4x4 matrix)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            nmo_result_t result = nmo_chunk_read_float(chunk, &out_mat->m[i][j]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_matrix(nmo_chunk_t *chunk, const nmo_matrix_t *mat) {
    if (!chunk || !mat) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    // Write 16 floats (4x4 matrix)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            nmo_result_t result = nmo_chunk_write_float(chunk, mat->m[i][j]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_color(nmo_chunk_t *chunk, nmo_color_t *out_color) {
    if (!chunk || !out_color) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_read_float(chunk, &out_color->r);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_color->g);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &out_color->b);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_read_float(chunk, &out_color->a);
}

nmo_result_t nmo_chunk_write_color(nmo_chunk_t *chunk, const nmo_color_t *color) {
    if (!chunk || !color) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    nmo_result_t result = nmo_chunk_write_float(chunk, color->r);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, color->g);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, color->b);
    if (result.code != NMO_OK) return result;
    
    return nmo_chunk_write_float(chunk, color->a);
}
