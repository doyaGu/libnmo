// chunk_primitives.c - Primitive type serialization for CKStateChunk
// Implements: byte, word, int, dword, float, GUID, string, buffer, object_id

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_utils.h"
#include <string.h>

static inline uint32_t *get_data_u32(nmo_chunk_t *chunk) {
    return NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
}

static inline const nmo_chunk_file_context_t *get_file_context(const nmo_chunk_t *chunk) {
    if (chunk == NULL) {
        return NULL;
    }
    if ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) == 0) {
        return NULL;
    }
    return chunk->file_context;
}

static nmo_status_t encode_object_id(const nmo_chunk_t *chunk,
                                     nmo_object_id_t id,
                                     uint32_t *out_value) {
    if (out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_chunk_file_context_t *ctx = get_file_context(chunk);
    if (ctx == NULL || ctx->runtime_to_file == NULL) {
        *out_value = (uint32_t) id;
        return NMO_OK;
    }

    if (id == 0) {
        *out_value = NMO_OBJECT_ID_INVALID;
        return NMO_OK;
    }

    nmo_object_id_t unresolved_raw = NMO_OBJECT_ID_NONE;
    if (ctx->repository != NULL &&
        nmo_object_repository_get_unresolved_ref_raw(
            ctx->repository, id, &unresolved_raw)) {
        *out_value = (uint32_t)unresolved_raw;
        return NMO_OK;
    }

    nmo_object_id_t file_id = 0;
    if (nmo_id_remap_lookup_id(ctx->runtime_to_file, id, &file_id) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "Cannot serialize unmapped runtime object ID %u",
                         (unsigned)id);
    }

    *out_value = (uint32_t) file_id;
    return NMO_OK;
}

static nmo_status_t decode_object_id(
    const nmo_chunk_t *chunk,
    uint32_t raw_id,
    bool preserve_only,
    nmo_object_id_t *out_id)
{
    const nmo_chunk_file_context_t *ctx = get_file_context(chunk);
    if (ctx == NULL || ctx->file_to_runtime == NULL) {
        *out_id = (nmo_object_id_t)raw_id;
        return NMO_OK;
    }

    if (raw_id == NMO_OBJECT_ID_INVALID) {
        *out_id = NMO_OBJECT_ID_NONE;
        return NMO_OK;
    }

    nmo_object_id_t runtime_id = 0;
    if (nmo_id_remap_lookup_id(ctx->file_to_runtime, (nmo_object_id_t) raw_id, &runtime_id) == NMO_OK) {
        *out_id = runtime_id;
        return NMO_OK;
    }

    if (preserve_only || ctx->repository == NULL) {
        *out_id = NMO_OBJECT_ID_NONE;
        return NMO_OK;
    }
    return nmo_object_repository_intern_unresolved_ref(
        ctx->repository, (nmo_object_id_t)raw_id, out_id);
}

// =============================================================================
// Primitive Types - Write
// =============================================================================

nmo_status_t nmo_chunk_write_byte(nmo_chunk_t *chunk, uint8_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_status_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_word(nmo_chunk_t *chunk, uint16_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_status_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_int(nmo_chunk_t *chunk, int32_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_status_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_dword(nmo_chunk_t *chunk, uint32_t value) {
    return nmo_chunk_write_int(chunk, (int32_t) value);
}

nmo_status_t nmo_chunk_write_float(nmo_chunk_t *chunk, float value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_status_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    // Store float as raw bits
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = raw;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_guid(nmo_chunk_t *chunk, nmo_guid_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_status_t result = nmo_chunk_check_size(chunk, 2 * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = value.d1;
    data[state->current_pos++] = value.d2;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

// =============================================================================
// Primitive Types - Read
// =============================================================================

nmo_status_t nmo_chunk_read_byte(nmo_chunk_t *chunk, uint8_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    *out_value = (uint8_t) (data[state->current_pos++] & 0xFF);

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_word(nmo_chunk_t *chunk, uint16_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    *out_value = (uint16_t) (data[state->current_pos++] & 0xFFFF);

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_int(nmo_chunk_t *chunk, int32_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    *out_value = (int32_t) data[state->current_pos++];

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_dword(nmo_chunk_t *chunk, uint32_t *out_value) {
    return nmo_chunk_read_int(chunk, (int32_t *) out_value);
}

nmo_status_t nmo_chunk_read_float(nmo_chunk_t *chunk, float *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    // Read float as raw bits
    uint32_t *data = get_data_u32(chunk);
    uint32_t raw = data[state->current_pos++];
    memcpy(out_value, &raw, sizeof(raw));

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_guid(nmo_chunk_t *chunk, nmo_guid_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 2);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    out_value->d1 = data[state->current_pos++];
    out_value->d2 = data[state->current_pos++];

    NMO_RETURN_OK();
}

// =============================================================================
// Complex Types - String
// =============================================================================

nmo_status_t nmo_chunk_write_string(nmo_chunk_t *chunk, const char *str) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    // Calculate size
    size_t len = str ? strlen(str) + 1 : 0; // Include null terminator
    size_t dwords = (len + 3) / 4;          // Round up to DWORDs

    // Write length
    nmo_status_t result = nmo_chunk_check_size(chunk, (1 + dwords) * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) len;

    // Write string data
    if (len > 0) {
        memset(&data[state->current_pos], 0, dwords * sizeof(uint32_t));
        memcpy(&data[state->current_pos], str, len);
        state->current_pos += dwords;
    }

    // Update data_size
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

size_t nmo_chunk_read_string(nmo_chunk_t *chunk, char **out_str) {
    size_t length = 0;
    return nmo_chunk_read_string_checked(chunk, out_str, &length) == NMO_OK
        ? length : 0;
}

nmo_status_t nmo_chunk_read_string_checked(
    nmo_chunk_t *chunk,
    char **out_str,
    size_t *out_length)
{
    NMO_CHUNK_CHECK_ARGS(chunk, out_str, "Invalid string read arguments");
    *out_str = NULL;
    if (out_length) *out_length = 0;
    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    size_t start_pos = state->current_pos;
    uint32_t len = data[state->current_pos++];
    if (len == 0) return NMO_OK;

    size_t dwords = ((size_t)len + 3u) / 4u;
    if (!nmo_chunk_has_read_capacity(chunk, dwords)) {
        state->current_pos = start_pos;
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    char *str = (char *)nmo_arena_alloc(chunk->arena, len, 1);
    if (!str) {
        state->current_pos = start_pos;
        return NMO_ERR_NOMEM;
    }
    memcpy(str, &data[state->current_pos], len);
    state->current_pos += dwords;
    *out_str = str;
    if (out_length) *out_length = (size_t)len - 1u;
    return NMO_OK;
}

// =============================================================================
// Complex Types - Buffer
// =============================================================================

nmo_status_t nmo_chunk_write_buffer(nmo_chunk_t *chunk,
                                    const void *data,
                                    size_t size) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (size > 0 && data == NULL) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Non-zero size with NULL buffer");
    }
    if (size > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Buffer size does not fit the 32-bit length prefix");
    }

    const size_t dwords = nmo_bytes_to_dwords(size);
    size_t total_dwords = 0;
    size_t total_bytes = 0;
    if (!nmo_safe_add_size(dwords, 1u, &total_dwords) ||
        !nmo_safe_mul_size(total_dwords, sizeof(uint32_t), &total_bytes)) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Buffer size overflow");
    }

    // Write size
    nmo_status_t result = nmo_chunk_check_size(chunk, total_bytes);
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    data_dwords[state->current_pos++] = (uint32_t) size;

    // Write data
    if (size > 0 && data) {
        memset(&data_dwords[state->current_pos], 0, dwords * sizeof(uint32_t));
        memcpy(&data_dwords[state->current_pos], data, size);
        state->current_pos += dwords;
    }

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_buffer_no_size(nmo_chunk_t *chunk,
                                            const void *data,
                                            size_t size) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (size > 0 && data == NULL) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Non-zero size with NULL buffer");
    }

    if (size == 0) {
        NMO_RETURN_OK();
    }
    if (size > SIZE_MAX - 3u) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Buffer size overflow");
    }

    size_t dwords = (size + 3u) / 4u;

    nmo_status_t result = nmo_chunk_check_size(chunk, dwords * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    memset(&data_dwords[state->current_pos], 0, dwords * sizeof(uint32_t));
    memcpy(&data_dwords[state->current_pos], data, size);
    state->current_pos += dwords;

    // Update data_size
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_buffer(nmo_chunk_t *chunk,
                                   void **out_data,
                                   size_t *out_size) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_data, out_size, "Invalid arguments");
    *out_data = NULL;
    *out_size = 0;

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    size_t start_pos = state->current_pos;
    uint32_t size = data_dwords[state->current_pos++];

    if (size == 0) {
        NMO_RETURN_OK();
    }

    size_t dwords = ((size_t) size + 3u) / 4u;
    if (!nmo_chunk_has_read_capacity(chunk, dwords)) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                               "Cannot read beyond data");
    }

    // Allocate from arena
    void *data = nmo_arena_alloc(chunk->arena, size, 1);
    if (!data) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate buffer");
    }

    memcpy(data, &data_dwords[state->current_pos], size);
    state->current_pos += dwords;

    *out_data = data;
    *out_size = size;
    NMO_RETURN_OK();
}

size_t nmo_chunk_read_and_fill_buffer(nmo_chunk_t *chunk,
                                      void *buffer,
                                      size_t buffer_size) {
    size_t size = 0;
    return nmo_chunk_read_and_fill_buffer_checked(
        chunk, buffer, buffer_size, &size) == NMO_OK ? size : 0;
}

nmo_status_t nmo_chunk_read_and_fill_buffer_checked(
    nmo_chunk_t *chunk,
    void *buffer,
    size_t buffer_size,
    size_t *out_size)
{
    NMO_CHUNK_CHECK_ARGS(chunk, buffer, "Invalid buffer read arguments");
    if (out_size) *out_size = 0;
    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    size_t start_pos = state->current_pos;
    uint32_t size = data_dwords[state->current_pos++];
    if (size == 0) return NMO_OK;
    if ((size_t)size > buffer_size) {
        state->current_pos = start_pos;
        return NMO_ERR_OUT_OF_BOUNDS;
    }
    size_t dwords = ((size_t)size + 3u) / 4u;
    if (!nmo_chunk_has_read_capacity(chunk, dwords)) {
        state->current_pos = start_pos;
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    memcpy(buffer, &data_dwords[state->current_pos], size);
    state->current_pos += dwords;
    if (out_size) *out_size = size;
    return NMO_OK;
}

size_t nmo_chunk_read_and_fill_buffer_nosize(nmo_chunk_t *chunk,
                                             void *buffer,
                                             size_t buffer_size) {
    return nmo_chunk_read_and_fill_buffer_nosize_checked(
        chunk, buffer, buffer_size) == NMO_OK ? buffer_size : 0;
}

nmo_status_t nmo_chunk_read_and_fill_buffer_nosize_checked(
    nmo_chunk_t *chunk,
    void *buffer,
    size_t buffer_size)
{
    NMO_CHUNK_CHECK_ARGS(chunk, buffer, "Invalid buffer read arguments");
    if (buffer_size == 0) return NMO_OK;
    size_t dwords = (buffer_size + 3u) / 4u;
    NMO_CHUNK_CHECK_BOUNDS(chunk, dwords);
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    memcpy(buffer, &data_dwords[state->current_pos], buffer_size);
    state->current_pos += dwords;
    return NMO_OK;
}

// =============================================================================
// Object References
// =============================================================================

nmo_status_t nmo_chunk_write_object_id(nmo_chunk_t *chunk, nmo_object_id_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_status_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    const nmo_chunk_file_context_t *ctx = get_file_context(chunk);
    const int in_file_context = (ctx != NULL && ctx->runtime_to_file != NULL);

    // Track position if ID is non-zero (internal remap list)
    if (id != 0 && !in_file_context) {
        uint32_t pos = (uint32_t) state->current_pos;
        nmo_status_t list_result = nmo_arena_array_append(&chunk->ids, &pos);
        NMO_RETURN_IF_ERROR(list_result);
        /* CKStateChunk: file-mode does NOT serialize ID lists */
        if ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) == 0) {
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }
    }

    uint32_t *data_dwords = get_data_u32(chunk);
    uint32_t encoded_value = 0;
    result = encode_object_id(chunk, id, &encoded_value);
    NMO_RETURN_IF_ERROR(result);
    data_dwords[state->current_pos++] = encoded_value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_write_raw_object_id(nmo_chunk_t *chunk, nmo_object_id_t raw_id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    return nmo_chunk_write_dword(chunk, (uint32_t)raw_id);
}

nmo_status_t nmo_chunk_read_object_id(nmo_chunk_t *chunk, nmo_object_id_t *out_id) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_id, "Invalid arguments");
    *out_id = NMO_OBJECT_ID_NONE;

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    const size_t start_pos = state->current_pos;
    const uint32_t raw_id = data_dwords[state->current_pos++];
    nmo_object_id_t id = NMO_OBJECT_ID_NONE;
    nmo_status_t result = decode_object_id(chunk, raw_id, false, &id);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }
    *out_id = id;
    return NMO_OK;
}

nmo_status_t nmo_chunk_read_object_id_preserve(nmo_chunk_t *chunk,
                                                nmo_object_id_t *out_raw_id,
                                                nmo_object_id_t *out_id) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_raw_id, "Invalid arguments");
    NMO_CHUNK_CHECK_ARG(out_id, "Invalid decoded object ID output");
    *out_raw_id = NMO_OBJECT_ID_NONE;
    *out_id = NMO_OBJECT_ID_NONE;
    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    const size_t start_pos = state->current_pos;
    const uint32_t raw_id = data_dwords[state->current_pos++];
    nmo_object_id_t id = NMO_OBJECT_ID_NONE;
    nmo_status_t result = decode_object_id(chunk, raw_id, true, &id);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }
    *out_raw_id = (nmo_object_id_t)raw_id;
    *out_id = id;
    return NMO_OK;
}
