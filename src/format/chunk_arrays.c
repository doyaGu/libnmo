// chunk_arrays.c - Array serialization for CKStateChunk
// Implements: generic arrays, typed arrays (object_id, int, float, dword, byte, string)

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include <string.h>
#include <limits.h>
#include <stdint.h>

// =============================================================================
// Internal Helpers
// =============================================================================

static inline nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

// =============================================================================
// Generic Arrays
// =============================================================================

nmo_result_t nmo_chunk_write_array(nmo_chunk_t *chunk,
                                   const void *array,
                                   size_t count,
                                   size_t elem_size) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid arguments");

    if (array == NULL && count > 0 && elem_size > 0) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Non-zero count with NULL array");
    }

    if (count == 0 || elem_size == 0) {
        nmo_result_t result = nmo_chunk_write_dword(chunk, 0);
        NMO_RETURN_IF_ERROR(result);
        return nmo_chunk_write_dword(chunk, 0);
    }

    if (count > (size_t) INT_MAX || elem_size > (size_t) INT_MAX) {
        nmo_result_t result = nmo_chunk_write_dword(chunk, 0);
        NMO_RETURN_IF_ERROR(result);
        return nmo_chunk_write_dword(chunk, 0);
    }

    if (count > SIZE_MAX / elem_size) {
        nmo_result_t result = nmo_chunk_write_dword(chunk, 0);
        NMO_RETURN_IF_ERROR(result);
        return nmo_chunk_write_dword(chunk, 0);
    }

    size_t total_size = count * elem_size;
    if (total_size > (size_t) INT_MAX) {
        nmo_result_t result = nmo_chunk_write_dword(chunk, 0);
        NMO_RETURN_IF_ERROR(result);
        return nmo_chunk_write_dword(chunk, 0);
    }

    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) total_size);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    return nmo_chunk_write_buffer_no_size(chunk, array, total_size);
}

nmo_result_t nmo_chunk_read_array(nmo_chunk_t *chunk,
                                  void **out_array,
                                  size_t *out_count,
                                  size_t *out_elem_size) {
    NMO_CHUNK_CHECK_ARGS3(chunk, out_array, out_count, out_elem_size, "Invalid arguments");

    uint32_t total_size = 0;
    nmo_result_t result = nmo_chunk_read_dword(chunk, &total_size);
    NMO_RETURN_IF_ERROR(result);

    uint32_t count = 0;
    result = nmo_chunk_read_dword(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    if (total_size == 0 || count == 0) {
        *out_array = NULL;
        *out_count = 0;
        *out_elem_size = 0;
        return nmo_result_ok();
    }

    if (total_size % count != 0) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Array size is not divisible by count");
    }

    size_t total_size_bytes = (size_t) total_size;
    size_t dwords = (total_size_bytes + 3) / 4;

    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, dwords, "Insufficient data for array");

    // Allocate array
    void *array = nmo_arena_alloc(chunk->arena, total_size_bytes, 4);
    if (!array) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate array");
    }

    // Copy data
    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(array, &chunk_data[state->current_pos], total_size_bytes);
    state->current_pos += dwords;

    *out_array = array;
    *out_count = count;
    *out_elem_size = total_size / count;

    return nmo_result_ok();
}

// =============================================================================
// Object ID Arrays
// =============================================================================

nmo_result_t nmo_chunk_read_object_id_array(nmo_chunk_t *chunk,
                                             nmo_object_id_t **out_ids,
                                             size_t *out_count,
                                             nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_ids, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_ids = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    if (count > (SIZE_MAX / sizeof(nmo_object_id_t))) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "ID array size overflow");
    }
    nmo_object_id_t *ids = (nmo_object_id_t *) nmo_arena_alloc(arena,
                                                                count * sizeof(nmo_object_id_t),
                                                                _Alignof(nmo_object_id_t));
    if (!ids) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate ID array");
    }

    // Read IDs
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_object_id(chunk, &ids[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    *out_ids = ids;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_object_id_array(nmo_chunk_t *chunk,
                                              const nmo_object_id_t *ids,
                                              size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, ids, "Non-zero count with NULL array");

    // Write count with sequence marker
    nmo_result_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write IDs
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        NMO_RETURN_IF_ERROR(result);
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
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    if (count > (SIZE_MAX / sizeof(int32_t))) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Int array size overflow");
    }
    int32_t *array = (int32_t *) nmo_arena_alloc(arena,
                                                  count * sizeof(int32_t),
                                                  _Alignof(int32_t));
    if (!array) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate int array");
    }

    // Read ints
    for (size_t i = 0; i < count; i++) {
        int32_t value;
        result = nmo_chunk_read_int(chunk, &value);
        NMO_RETURN_IF_ERROR(result);
        array[i] = value;
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_int_array(nmo_chunk_t *chunk,
                                        const int32_t *array,
                                        size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write ints
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_int(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
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
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    if (count > (SIZE_MAX / sizeof(float))) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Float array size overflow");
    }
    float *array = (float *) nmo_arena_alloc(arena,
                                              count * sizeof(float),
                                              _Alignof(float));
    if (!array) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate float array");
    }

    // Read floats
    for (size_t i = 0; i < count; i++) {
        float value;
        result = nmo_chunk_read_float(chunk, &value);
        NMO_RETURN_IF_ERROR(result);
        array[i] = value;
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_float_array(nmo_chunk_t *chunk,
                                          const float *array,
                                          size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write floats
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_float(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
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
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    if (count > (SIZE_MAX / sizeof(uint32_t))) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Dword array size overflow");
    }
    uint32_t *array = (uint32_t *) nmo_arena_alloc(arena,
                                                    count * sizeof(uint32_t),
                                                    _Alignof(uint32_t));
    if (!array) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate dword array");
    }

    // Read dwords
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_dword(chunk, &array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_dword_array(nmo_chunk_t *chunk,
                                          const uint32_t *array,
                                          size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write dwords
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_dword(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
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
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_array = NULL;
        return nmo_result_ok();
    }

    // Allocate array
    if (count > (SIZE_MAX / sizeof(uint8_t))) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Byte array size overflow");
    }
    uint8_t *array = (uint8_t *) nmo_arena_alloc(arena,
                                                  count * sizeof(uint8_t),
                                                  _Alignof(uint8_t));
    if (!array) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate byte array");
    }

    // Read bytes
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_byte(chunk, &array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    *out_array = array;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_byte_array(nmo_chunk_t *chunk,
                                         const uint8_t *array,
                                         size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write bytes
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_byte(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
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
    NMO_CHUNK_CHECK_ARGS2(chunk, out_strings, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");

    // Start sequence and get count
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    *out_count = count;

    // Handle empty array
    if (count == 0) {
        *out_strings = NULL;
        return nmo_result_ok();
    }

    // Allocate string pointer array
    if (count > (SIZE_MAX / sizeof(char *))) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "String array size overflow");
    }
    char **strings = (char **) nmo_arena_alloc(arena,
                                                count * sizeof(char *),
                                                _Alignof(char *));
    if (!strings) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate string array");
    }

    // Read strings
    for (size_t i = 0; i < count; i++) {
        size_t len = nmo_chunk_read_string(chunk, &strings[i]);
        if (len == 0 && strings[i] == NULL) {
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                   "Failed to read string");
        }
    }

    *out_strings = strings;
    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_string_array(nmo_chunk_t *chunk,
                                           const char * const *strings,
                                           size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, strings, "Non-zero count with NULL array");

    // Write count
    nmo_result_t result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write strings
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_string(chunk, strings[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    return nmo_result_ok();
}
