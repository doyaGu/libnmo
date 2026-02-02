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

static inline uint32_t *get_data_u32(nmo_chunk_t *chunk) {
    return NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
}

// =============================================================================
// Primitive Types - Write
// =============================================================================

nmo_result_t nmo_chunk_write_byte(nmo_chunk_t *chunk, uint8_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_word(nmo_chunk_t *chunk, uint16_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_int(nmo_chunk_t *chunk, int32_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) value;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_dword(nmo_chunk_t *chunk, uint32_t value) {
    return nmo_chunk_write_int(chunk, (int32_t) value);
}

nmo_result_t nmo_chunk_write_float(nmo_chunk_t *chunk, float value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    // Store float as raw bits
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = raw;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_guid(nmo_chunk_t *chunk, nmo_guid_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, 2 * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = value.d1;
    data[state->current_pos++] = value.d2;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

// =============================================================================
// Primitive Types - Read
// =============================================================================

nmo_result_t nmo_chunk_read_byte(nmo_chunk_t *chunk, uint8_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    *out_value = (uint8_t) (data[state->current_pos++] & 0xFF);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_word(nmo_chunk_t *chunk, uint16_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    *out_value = (uint16_t) (data[state->current_pos++] & 0xFFFF);

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_int(nmo_chunk_t *chunk, int32_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    *out_value = (int32_t) data[state->current_pos++];

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_dword(nmo_chunk_t *chunk, uint32_t *out_value) {
    return nmo_chunk_read_int(chunk, (int32_t *) out_value);
}

nmo_result_t nmo_chunk_read_float(nmo_chunk_t *chunk, float *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    // Read float as raw bits
    uint32_t *data = get_data_u32(chunk);
    uint32_t raw = data[state->current_pos++];
    memcpy(out_value, &raw, sizeof(raw));

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_guid(nmo_chunk_t *chunk, nmo_guid_t *out_value) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 2);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    out_value->d1 = data[state->current_pos++];
    out_value->d2 = data[state->current_pos++];

    return nmo_result_ok();
}

// =============================================================================
// Complex Types - String
// =============================================================================

nmo_result_t nmo_chunk_write_string(nmo_chunk_t *chunk, const char *str) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    // Calculate size
    size_t len = str ? strlen(str) + 1 : 0; // Include null terminator
    size_t dwords = (len + 3) / 4;          // Round up to DWORDs

    // Write length
    nmo_result_t result = nmo_chunk_check_size(chunk, (1 + dwords) * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    data[state->current_pos++] = (uint32_t) len;

    // Write string data
    if (len > 0) {
        memcpy(&data[state->current_pos], str, len);
        state->current_pos += dwords;
    }

    // Update data_size
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

size_t nmo_chunk_read_string(nmo_chunk_t *chunk, char **out_str) {
    if (!chunk || !out_str) {
        return 0;
    }

    NMO_CHUNK_CHECK_BOUNDS_OR(chunk, 1, {
        *out_str = NULL;
        return 0;
    });

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data = get_data_u32(chunk);
    uint32_t len = data[state->current_pos++];

    if (len == 0) {
        *out_str = NULL;
        return 0;
    }

    size_t dwords = (len + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_OR(chunk, dwords, {
        *out_str = NULL;
        return 0;
    });

    // Allocate from arena
    char *str = (char *) nmo_arena_alloc(chunk->arena, len, 1);
    if (!str) {
        *out_str = NULL;
        return 0;
    }

    memcpy(str, &data[state->current_pos], len);
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
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    size_t dwords = (size + 3) / 4;

    // Write size
    nmo_result_t result = nmo_chunk_check_size(chunk, (1 + dwords) * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    data_dwords[state->current_pos++] = (uint32_t) size;

    // Write data
    if (size > 0 && data) {
        memcpy(&data_dwords[state->current_pos], data, size);
        state->current_pos += dwords;
    }

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_write_buffer_no_size(nmo_chunk_t *chunk,
                                            const void *data,
                                            size_t size) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (size == 0) {
        return nmo_result_ok();
    }

    size_t dwords = (size + 3) / 4;

    nmo_result_t result = nmo_chunk_check_size(chunk, dwords * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    memcpy(&data_dwords[state->current_pos], data, size);
    state->current_pos += dwords;

    // Update data_size
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_buffer(nmo_chunk_t *chunk,
                                   void **out_data,
                                   size_t *out_size) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_data, out_size, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    uint32_t size = data_dwords[state->current_pos++];

    *out_size = size;

    if (size == 0) {
        *out_data = NULL;
        return nmo_result_ok();
    }

    size_t dwords = (size + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS(chunk, dwords);

    // Allocate from arena
    void *data = nmo_arena_alloc(chunk->arena, size, 1);
    if (!data) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate buffer");
    }

    memcpy(data, &data_dwords[state->current_pos], size);
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

    NMO_CHUNK_CHECK_BOUNDS_OR(chunk, 1, {
        return 0;
    });

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    uint32_t size = data_dwords[state->current_pos++];

    if (size == 0) {
        return 0;
    }

    if (size > buffer_size) {
        return 0; // Buffer too small
    }

    size_t dwords = (size + 3) / 4;
    NMO_CHUNK_CHECK_BOUNDS_OR(chunk, dwords, {
        return 0;
    });

    memcpy(buffer, &data_dwords[state->current_pos], size);
    state->current_pos += dwords;

    return size;
}

// =============================================================================
// Object References
// =============================================================================

nmo_result_t nmo_chunk_write_object_id(nmo_chunk_t *chunk, nmo_object_id_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_result_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);

    // Track position if ID is non-zero (internal remap list)
    if (id != 0) {
        uint32_t pos = (uint32_t) state->current_pos;
        nmo_result_t list_result = nmo_arena_array_append(&chunk->ids, &pos);
        NMO_RETURN_IF_ERROR(list_result);
        /* CKStateChunk: file-mode does NOT serialize ID lists */
        if ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) == 0) {
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }
    }

    uint32_t *data_dwords = get_data_u32(chunk);
    data_dwords[state->current_pos++] = id;

    // Update data_size to track written data
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_read_object_id(nmo_chunk_t *chunk, nmo_object_id_t *out_id) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_id, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS(chunk, 1);

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    uint32_t *data_dwords = get_data_u32(chunk);
    *out_id = data_dwords[state->current_pos++];

    return nmo_result_ok();
}
