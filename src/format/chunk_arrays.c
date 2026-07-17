// chunk_arrays.c - Array serialization for CKStateChunk
// Implements: generic arrays, typed arrays (object_id, int, float, dword, byte, string)

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include <string.h>
#include <limits.h>
#include <stdint.h>

static nmo_status_t preflight_dword_sequence(nmo_chunk_t *chunk,
                                             size_t payload_dwords) {
    size_t total_dwords = 0;
    size_t total_bytes = 0;
    if (!nmo_safe_add_size(payload_dwords, 1u, &total_dwords) ||
        !nmo_safe_mul_size(total_dwords, sizeof(uint32_t), &total_bytes)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_chunk_check_size(chunk, total_bytes);
}

// =============================================================================
// Generic Arrays
// =============================================================================

nmo_status_t nmo_chunk_write_array(nmo_chunk_t *chunk,
                                   const void *array,
                                   size_t count,
                                   size_t elem_size) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid arguments");

    if (array == NULL && count > 0 && elem_size > 0) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Non-zero count with NULL array");
    }

    if (count == 0 || elem_size == 0) {
        nmo_status_t result = nmo_chunk_check_size(
            chunk, 2u * sizeof(uint32_t));
        NMO_RETURN_IF_ERROR(result);
        result = nmo_chunk_write_dword(chunk, 0);
        NMO_RETURN_IF_ERROR(result);
        return nmo_chunk_write_dword(chunk, 0);
    }

    if (count > (size_t) INT_MAX || elem_size > (size_t) INT_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Array count or element size is not encodable");
    }

    if (count > SIZE_MAX / elem_size) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Array byte size overflow");
    }

    size_t total_size = count * elem_size;
    if (total_size > (size_t) INT_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Array byte size is not encodable");
    }

    const size_t payload_dwords = nmo_bytes_to_dwords(total_size);
    nmo_status_t result = nmo_chunk_check_size(
        chunk, (2u + payload_dwords) * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);
    result = nmo_chunk_write_dword(chunk, (uint32_t) total_size);
    NMO_RETURN_IF_ERROR(result);

    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    return nmo_chunk_write_buffer_no_size(chunk, array, total_size);
}

nmo_status_t nmo_chunk_read_array(nmo_chunk_t *chunk,
                                  void **out_array,
                                  size_t *out_count,
                                  size_t *out_elem_size) {
    NMO_CHUNK_CHECK_ARGS3(chunk, out_array, out_count, out_elem_size, "Invalid arguments");
    *out_array = NULL;
    *out_count = 0;
    *out_elem_size = 0;

    size_t start_pos = nmo_chunk_get_position(chunk);

    uint32_t total_size = 0;
    nmo_status_t result = nmo_chunk_read_dword(chunk, &total_size);
    NMO_RETURN_IF_ERROR(result);
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);

    uint32_t count = 0;
    result = nmo_chunk_read_dword(chunk, &count);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    if ((total_size == 0u) != (count == 0u)) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Array header has inconsistent size/count");
    }

    if (total_size == 0 || count == 0) {
        NMO_RETURN_OK();
    }

    if (total_size % count != 0) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Array size is not divisible by count");
    }

    size_t total_size_bytes = (size_t) total_size;
    size_t dwords = nmo_bytes_to_dwords(total_size_bytes);

    if (!nmo_chunk_has_read_capacity(chunk, dwords)) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                               "Insufficient data for array");
    }

    // Allocate array
    void *array = nmo_arena_alloc(chunk->arena, total_size_bytes, 4);
    if (!array) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate array");
    }

    // Copy data
    uint32_t *chunk_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(array, &chunk_data[state->current_pos], total_size_bytes);
    state->current_pos += dwords;

    *out_array = array;
    *out_count = count;
    *out_elem_size = total_size / count;

    NMO_RETURN_OK();
}

// =============================================================================
// Typed Array Sequence Helper
// =============================================================================

/* Common steps 1-4 shared by all typed read functions:
 *   1. Read count via nmo_chunk_read_object_sequence_start
 *   2. Set *out_count; if count==0 set *out_ptr=NULL and return OK
 *   3. Overflow check: count > SIZE_MAX/elem_sz
 *   4. Arena alloc + NULL check
 * The unique element-read loop is left to each caller. */
static nmo_status_t chunk_seq_alloc(nmo_chunk_t *chunk, nmo_arena_t *arena,
                                     size_t elem_sz, size_t elem_align,
                                     const char *overflow_msg,
                                     const char *alloc_msg,
                                     size_t *out_count, void **out_ptr) {
    *out_count = 0;
    *out_ptr = NULL;
    size_t start_pos = nmo_chunk_get_position(chunk);
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    if (count == 0) {
        NMO_RETURN_OK();
    }

    if (count > (SIZE_MAX / elem_sz)) {
        nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, overflow_msg);
    }

    void *ptr = nmo_arena_alloc(arena, count * elem_sz, elem_align);
    if (!ptr) {
        nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, alloc_msg);
    }

    *out_count = count;
    *out_ptr = ptr;
    NMO_RETURN_OK();
}

// =============================================================================
// Object ID Arrays
// =============================================================================

nmo_status_t nmo_chunk_read_object_id_array(nmo_chunk_t *chunk,
                                             nmo_object_id_t **out_ids,
                                             size_t *out_count,
                                             nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_ids, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");
    *out_ids = NULL;
    *out_count = 0;
    size_t start_pos = nmo_chunk_get_position(chunk);

    // Start sequence and get count
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    // Handle empty array
    if (count == 0) {
        NMO_RETURN_OK();
    }

    // Allocate array
    if (count > (SIZE_MAX / sizeof(nmo_object_id_t))) {
        nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "ID array size overflow");
    }
    nmo_object_id_t *ids = (nmo_object_id_t *) nmo_arena_alloc(arena,
                                                                count * sizeof(nmo_object_id_t),
                                                                _Alignof(nmo_object_id_t));
    if (!ids) {
        nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate ID array");
    }

    // Read IDs
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_object_id(chunk, &ids[i]);
        if (result != NMO_OK) {
            nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
            return result;
        }
    }

    *out_ids = ids;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_object_id_array(nmo_chunk_t *chunk,
                                              const nmo_object_id_t *ids,
                                              size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, ids, "Non-zero count with NULL array");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("ID array count is not encodable");
    }

    const nmo_chunk_file_context_t *ctx = NULL;
    if ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        ctx = chunk->file_context;
    }
    if (ctx != NULL && ctx->runtime_to_file != NULL) {
        for (size_t i = 0; i < count; i++) {
            if (ids[i] == NMO_OBJECT_ID_NONE) {
                continue;
            }
            nmo_object_id_t unresolved_raw = NMO_OBJECT_ID_NONE;
            if (ctx->repository != NULL &&
                nmo_object_repository_get_unresolved_ref_raw(
                    ctx->repository, ids[i], &unresolved_raw)) {
                continue;
            }
            nmo_object_id_t file_id = NMO_OBJECT_ID_NONE;
            if (nmo_id_remap_lookup_id(ctx->runtime_to_file,
                                       ids[i], &file_id) != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                 "Cannot serialize unmapped runtime object ID %u",
                                 (unsigned)ids[i]);
            }
        }
    }

    nmo_status_t result = preflight_dword_sequence(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write count with sequence marker
    result = nmo_chunk_write_object_sequence_start(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write IDs
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

// =============================================================================
// Integer Arrays
// =============================================================================

nmo_status_t nmo_chunk_read_int_array(nmo_chunk_t *chunk,
                                       int32_t **out_array,
                                       size_t *out_count,
                                       nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");
    *out_array = NULL;
    *out_count = 0;

    size_t count = 0;
    void *ptr = NULL;
    nmo_status_t result = chunk_seq_alloc(chunk, arena,
                                          sizeof(int32_t), _Alignof(int32_t),
                                          "Int array size overflow",
                                          "Failed to allocate int array",
                                          &count, &ptr);
    NMO_RETURN_IF_ERROR(result);

    int32_t *array = (int32_t *) ptr;
    for (size_t i = 0; i < count; i++) {
        int32_t value;
        result = nmo_chunk_read_int(chunk, &value);
        NMO_RETURN_IF_ERROR(result);
        array[i] = value;
    }

    *out_array = array;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_int_array(nmo_chunk_t *chunk,
                                        const int32_t *array,
                                        size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Int array count is not encodable");
    }
    nmo_status_t result = preflight_dword_sequence(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write count
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write ints
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_int(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

// =============================================================================
// Float Arrays
// =============================================================================

nmo_status_t nmo_chunk_read_float_array(nmo_chunk_t *chunk,
                                         float **out_array,
                                         size_t *out_count,
                                         nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");
    *out_array = NULL;
    *out_count = 0;

    size_t count = 0;
    void *ptr = NULL;
    nmo_status_t result = chunk_seq_alloc(chunk, arena,
                                          sizeof(float), _Alignof(float),
                                          "Float array size overflow",
                                          "Failed to allocate float array",
                                          &count, &ptr);
    NMO_RETURN_IF_ERROR(result);

    float *array = (float *) ptr;
    for (size_t i = 0; i < count; i++) {
        float value;
        result = nmo_chunk_read_float(chunk, &value);
        NMO_RETURN_IF_ERROR(result);
        array[i] = value;
    }

    *out_array = array;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_float_array(nmo_chunk_t *chunk,
                                          const float *array,
                                          size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Float array count is not encodable");
    }
    nmo_status_t result = preflight_dword_sequence(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write count
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write floats
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_float(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

// =============================================================================
// DWORD Arrays
// =============================================================================

nmo_status_t nmo_chunk_read_dword_array(nmo_chunk_t *chunk,
                                         uint32_t **out_array,
                                         size_t *out_count,
                                         nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");
    *out_array = NULL;
    *out_count = 0;

    size_t count = 0;
    void *ptr = NULL;
    nmo_status_t result = chunk_seq_alloc(chunk, arena,
                                          sizeof(uint32_t), _Alignof(uint32_t),
                                          "Dword array size overflow",
                                          "Failed to allocate dword array",
                                          &count, &ptr);
    NMO_RETURN_IF_ERROR(result);

    uint32_t *array = (uint32_t *) ptr;
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_dword(chunk, &array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    *out_array = array;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_dword_array(nmo_chunk_t *chunk,
                                          const uint32_t *array,
                                          size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Dword array count is not encodable");
    }
    nmo_status_t result = preflight_dword_sequence(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write count
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write dwords
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_dword(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

// =============================================================================
// Byte Arrays
// =============================================================================

nmo_status_t nmo_chunk_read_byte_array(nmo_chunk_t *chunk,
                                        uint8_t **out_array,
                                        size_t *out_count,
                                        nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_array, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");
    *out_array = NULL;
    *out_count = 0;

    size_t count = 0;
    void *ptr = NULL;
    nmo_status_t result = chunk_seq_alloc(chunk, arena,
                                          sizeof(uint8_t), _Alignof(uint8_t),
                                          "Byte array size overflow",
                                          "Failed to allocate byte array",
                                          &count, &ptr);
    NMO_RETURN_IF_ERROR(result);

    uint8_t *array = (uint8_t *) ptr;
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_read_byte(chunk, &array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    *out_array = array;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_byte_array(nmo_chunk_t *chunk,
                                         const uint8_t *array,
                                         size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    NMO_CHUNK_CHECK_COUNT_ARRAY(count, array, "Non-zero count with NULL array");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Byte array count is not encodable");
    }
    nmo_status_t result = preflight_dword_sequence(chunk, count);
    NMO_RETURN_IF_ERROR(result);

    // Write count
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write bytes
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_byte(chunk, array[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

// =============================================================================
// String Arrays
// =============================================================================

nmo_status_t nmo_chunk_read_string_array(nmo_chunk_t *chunk,
                                          char ***out_strings,
                                          size_t *out_count,
                                          nmo_arena_t *arena) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_strings, out_count, "Invalid arguments");
    NMO_CHUNK_CHECK_PTR(arena, "Invalid arguments");
    *out_strings = NULL;
    *out_count = 0;
    size_t start_pos = nmo_chunk_get_position(chunk);

    // Start sequence and get count
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    NMO_RETURN_IF_ERROR(result);

    // Handle empty array
    if (count == 0) {
        NMO_RETURN_OK();
    }

    // Allocate string pointer array
    if (count > (SIZE_MAX / sizeof(char *))) {
        nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "String array size overflow");
    }
    char **strings = (char **) nmo_arena_alloc(arena,
                                                count * sizeof(char *),
                                                _Alignof(char *));
    if (!strings) {
        nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate string array");
    }

    // Read strings
    for (size_t i = 0; i < count; i++) {
        size_t len = 0;
        result = nmo_chunk_read_string_checked(
            chunk, &strings[i], &len);
        if (result != NMO_OK) {
            nmo_chunk_get_parser_state(chunk)->current_pos = start_pos;
            return result;
        }
    }

    *out_strings = strings;
    *out_count = count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_string_array(nmo_chunk_t *chunk,
                                           const char * const *strings,
                                           size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    NMO_CHUNK_CHECK_COUNT_ARRAY(count, strings, "Non-zero count with NULL array");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("String array count is not encodable");
    }

    size_t total_dwords = 1u;
    for (size_t i = 0; i < count; i++) {
        size_t string_bytes = 0;
        if (strings[i] != NULL) {
            const size_t length = strlen(strings[i]);
            if (length >= UINT32_MAX) {
                NMO_CHUNK_RETURN_INVALID_ARGUMENT("String array item is too long");
            }
            string_bytes = length + 1u;
        }
        const size_t item_dwords = nmo_bytes_to_dwords(string_bytes);
        if (!nmo_safe_add_size(total_dwords, 1u, &total_dwords) ||
            !nmo_safe_add_size(total_dwords, item_dwords, &total_dwords)) {
            NMO_CHUNK_RETURN_INVALID_ARGUMENT("String array size overflow");
        }
    }
    size_t total_bytes = 0;
    if (!nmo_safe_mul_size(total_dwords, sizeof(uint32_t), &total_bytes)) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("String array size overflow");
    }
    nmo_status_t result = nmo_chunk_check_size(chunk, total_bytes);
    NMO_RETURN_IF_ERROR(result);

    // Write count
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    NMO_RETURN_IF_ERROR(result);

    // Write strings
    for (size_t i = 0; i < count; i++) {
        result = nmo_chunk_write_string(chunk, strings[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}
