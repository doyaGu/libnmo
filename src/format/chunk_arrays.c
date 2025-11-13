// chunk_arrays.c - Array serialization for CKStateChunk
// Implements: generic arrays, typed arrays (object_id, int, float, dword, byte, string)

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
// Generic Arrays
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
// Object ID Arrays
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
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
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

// =============================================================================
// Integer Arrays
// =============================================================================

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
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
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

// =============================================================================
// Float Arrays
// =============================================================================

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
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
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
// DWORD Arrays
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
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
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

// =============================================================================
// Byte Arrays
// =============================================================================

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
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
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

// =============================================================================
// String Arrays
// =============================================================================

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
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
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
