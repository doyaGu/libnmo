#include "format/nmo_chunk_writer.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_utils.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>   /* fprintf for IntList auditor */
#include <assert.h>  /* assert for IntList auditor */

#define LIST_SEQUENCE_MARKER 0xFFFFFFFFu

#define WRITER_INITIAL_CAPACITY 100  // DWORDs
#define WRITER_GROWTH_INCREMENT 500  // DWORDs (as per spec)

/**
 * @brief Sub-chunk context for nested chunks
 */
typedef struct nmo_subchunk_context {
    size_t start_pos;                    // Starting position in parent
    nmo_chunk_writer_t *writer;          // Sub-chunk writer
    struct nmo_subchunk_context *parent; // Parent context
} nmo_subchunk_context_t;

/**
 * @brief Chunk writer structure
 */
typedef struct nmo_chunk_writer {
    // Chunk being built
    nmo_chunk_t *chunk;

    // Optional file-context remap tables (borrowed)
    const nmo_chunk_file_context_t *file_context;

    // Arena allocator
    nmo_arena_t *arena;

    // Data buffer (managed separately from chunk until finalized)
    uint32_t *data;
    size_t data_size;     // In DWORDs
    size_t data_capacity; // In DWORDs

    // Tracking lists (built dynamically)
    uint32_t *id_list;
    size_t id_count;
    size_t id_capacity;

    uint32_t *manager_list;
    size_t manager_count;
    size_t manager_capacity;

    uint32_t *chunk_ref_list;
    size_t chunk_ref_count;
    size_t chunk_ref_capacity;

    nmo_chunk_t **chunk_list;
    size_t chunk_count;
    size_t chunk_capacity;

    // Sub-chunk context
    nmo_subchunk_context_t *subchunk_ctx;

    // Identifier linked-list tracking
    size_t prev_identifier_pos; // Position of previous identifier for linked-list chaining

    // Version context stack
    nmo_chunk_version_context_t version_stack[NMO_CHUNK_WRITER_MAX_DEPTH];
    int version_stack_top;  // -1 = empty, 0+ = current index

    // IntList auditor (DEBUG mode only)
#ifndef NDEBUG
    nmo_intlist_audit_t intlist_audit;
#endif

    // State
    int finalized;
} nmo_chunk_writer_t;

typedef int (*writer_track_position_fn)(nmo_chunk_writer_t *w, uint32_t position);

// Helper to ensure capacity
static int ensure_data_capacity(nmo_chunk_writer_t *w, size_t needed_dwords) {
    size_t required = 0;
    if (!nmo_safe_add_size(w->data_size, needed_dwords, &required)) {
        return NMO_ERR_CORRUPT;
    }
    if (required > UINT32_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (required <= w->data_capacity) {
        return NMO_OK;
    }

    // Grow by WRITER_GROWTH_INCREMENT
    size_t missing = required - w->data_capacity;
    size_t growth_steps = missing / WRITER_GROWTH_INCREMENT;
    if (missing % WRITER_GROWTH_INCREMENT != 0) {
        growth_steps++;
    }
    size_t growth = 0;
    size_t new_capacity = 0;
    if (!nmo_safe_mul_size(growth_steps, WRITER_GROWTH_INCREMENT, &growth) ||
        !nmo_safe_add_size(w->data_capacity, growth, &new_capacity)) {
        return NMO_ERR_CORRUPT;
    }

    size_t alloc_bytes = 0;
    if (!nmo_safe_mul_size(new_capacity, sizeof(uint32_t), &alloc_bytes)) {
        return NMO_ERR_CORRUPT;
    }

    uint32_t *new_data = (uint32_t *) nmo_arena_alloc(w->arena,
                                                      alloc_bytes,
                                                      sizeof(uint32_t));
    if (new_data == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy existing data
    if (w->data != NULL && w->data_size > 0) {
        size_t copy_bytes = 0;
        if (!nmo_safe_mul_size(w->data_size, sizeof(uint32_t), &copy_bytes)) {
            return NMO_ERR_CORRUPT;
        }
        memcpy(new_data, w->data, copy_bytes);
    }

    w->data = new_data;
    w->data_capacity = new_capacity;

    return NMO_OK;
}

static nmo_status_t writer_append_dword(nmo_chunk_writer_t *w, uint32_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    w->data[w->data_size++] = value;
    return NMO_OK;
}

static nmo_status_t writer_append_dword_pair(nmo_chunk_writer_t *w,
                                             uint32_t first,
                                             uint32_t second) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 2);
    if (result != NMO_OK) {
        return result;
    }

    w->data[w->data_size++] = first;
    w->data[w->data_size++] = second;
    return NMO_OK;
}

static nmo_status_t writer_append_empty_array_marker(nmo_chunk_writer_t *w) {
    return writer_append_dword_pair(w, 0, 0);
}

static uint32_t writer_pack_lendian16_word(uint16_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value = (uint16_t)((value >> 8) | (value << 8));
#endif
    return (uint32_t)value;
}

static nmo_status_t writer_append_aligned_bytes(nmo_chunk_writer_t *w,
                                                const void *data,
                                                size_t bytes) {
    if (w == NULL || w->finalized || data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    size_t dwords_needed = nmo_bytes_to_dwords(bytes);
    int result = ensure_data_capacity(w, dwords_needed);
    if (result != NMO_OK) {
        return result;
    }

    memset(&w->data[w->data_size], 0, dwords_needed * sizeof(uint32_t));
    memcpy(&w->data[w->data_size], data, bytes);
    w->data_size += dwords_needed;
    return NMO_OK;
}

static nmo_status_t writer_append_float_span(nmo_chunk_writer_t *w,
                                             const float *values,
                                             size_t count) {
    if (w == NULL || w->finalized || values == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, count);
    if (result != NMO_OK) {
        return result;
    }

    memcpy(&w->data[w->data_size], values, count * sizeof(float));
    w->data_size += count;
    return NMO_OK;
}

static void writer_append_reserved_dwords(nmo_chunk_writer_t *w,
                                          const uint32_t *data,
                                          size_t count) {
    if (count == 0) {
        return;
    }

    memcpy(&w->data[w->data_size], data, count * sizeof(uint32_t));
    w->data_size += count;
}

static nmo_status_t writer_copy_finalized_span(nmo_arena_array_t *dest,
                                               const void *src,
                                               size_t count,
                                               size_t element_size) {
    nmo_status_t result = nmo_arena_array_resize(dest, count);
    if (result != NMO_OK) {
        return result;
    }

    if (count > 0) {
        memcpy(nmo_arena_array_data(dest), src, count * element_size);
    }

    return NMO_OK;
}

static nmo_patch_token_t writer_reserve_dword_span(nmo_chunk_writer_t *w,
                                                   size_t dword_count) {
    nmo_patch_token_t token = NMO_PATCH_TOKEN_INVALID;

    if (w == NULL || w->finalized || dword_count == 0 || dword_count > 4) {
        return token;
    }

    int result = ensure_data_capacity(w, dword_count);
    if (result != NMO_OK) {
        return token;
    }

    token.offset = w->data_size;
    token.size = (uint8_t)dword_count;
    token.valid = 1;

    for (size_t i = 0; i < dword_count; i++) {
        w->data[w->data_size++] = 0;
    }

    return token;
}

static nmo_status_t writer_patch_dword_span(nmo_chunk_writer_t *w,
                                            nmo_patch_token_t token,
                                            size_t dword_count,
                                            uint32_t **out) {
    if (w == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!nmo_patch_token_valid(token) || token.size != dword_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (dword_count == 0 || token.offset > w->data_size ||
        dword_count > w->data_size - token.offset) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out = &w->data[token.offset];
    return NMO_OK;
}

static int ensure_u32_list_capacity(nmo_chunk_writer_t *w,
                                    uint32_t **list,
                                    size_t count,
                                    size_t *capacity,
                                    size_t needed_entries) {
    size_t required = 0;
    if (!nmo_safe_add_size(count, needed_entries, &required)) {
        return NMO_ERR_CORRUPT;
    }

    if (required <= *capacity) {
        return NMO_OK;
    }

    size_t new_capacity = (*capacity == 0) ? 16 : *capacity;
    while (new_capacity < required) {
        if (new_capacity > (SIZE_MAX / 2u)) {
            return NMO_ERR_CORRUPT;
        }
        new_capacity *= 2;
    }

    size_t alloc_bytes = 0;
    if (!nmo_safe_mul_size(new_capacity, sizeof(uint32_t), &alloc_bytes)) {
        return NMO_ERR_CORRUPT;
    }

    uint32_t *new_list = (uint32_t *) nmo_arena_alloc(w->arena,
                                                      alloc_bytes,
                                                      sizeof(uint32_t));
    if (new_list == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (*list != NULL && count > 0) {
        size_t copy_bytes = 0;
        if (!nmo_safe_mul_size(count, sizeof(uint32_t), &copy_bytes)) {
            return NMO_ERR_CORRUPT;
        }
        memcpy(new_list, *list, copy_bytes);
    }

    *list = new_list;
    *capacity = new_capacity;
    return NMO_OK;
}

static int ensure_chunk_list_capacity(nmo_chunk_writer_t *w, size_t needed_entries) {
    size_t required = 0;
    if (!nmo_safe_add_size(w->chunk_count, needed_entries, &required)) {
        return NMO_ERR_CORRUPT;
    }
    if (required <= w->chunk_capacity) {
        return NMO_OK;
    }

    size_t new_capacity = (w->chunk_capacity == 0) ? 16 : w->chunk_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            return NMO_ERR_CORRUPT;
        }
        new_capacity *= 2u;
    }

    size_t alloc_bytes = 0;
    if (!nmo_safe_mul_size(new_capacity, sizeof(nmo_chunk_t *), &alloc_bytes)) {
        return NMO_ERR_CORRUPT;
    }
    nmo_chunk_t **new_list = (nmo_chunk_t **)nmo_arena_alloc(
        w->arena, alloc_bytes, sizeof(void *));
    if (new_list == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (w->chunk_count > 0) {
        if (w->chunk_list == NULL) {
            return NMO_ERR_INVALID_STATE;
        }
        size_t copy_bytes = 0;
        if (!nmo_safe_mul_size(w->chunk_count, sizeof(nmo_chunk_t *), &copy_bytes)) {
            return NMO_ERR_CORRUPT;
        }
        memcpy(new_list, w->chunk_list, copy_bytes);
    }
    w->chunk_list = new_list;
    w->chunk_capacity = new_capacity;
    return NMO_OK;
}

static nmo_status_t writer_validate_dword_array(const nmo_arena_array_t *array) {
    if (array == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (array->count > UINT32_MAX ||
        array->count > SIZE_MAX / sizeof(uint32_t)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (array->count > 0 &&
        (array->data == NULL || array->count > array->capacity ||
         array->element_size != sizeof(uint32_t))) {
        return NMO_ERR_INVALID_STATE;
    }
    return NMO_OK;
}

static int writer_track_u32_position(nmo_chunk_writer_t *w,
                                     uint32_t **list,
                                     size_t *count,
                                     size_t *capacity,
                                     uint32_t position) {
    int result = ensure_u32_list_capacity(w, list, *count, capacity, 1);
    if (result != NMO_OK) {
        return result;
    }

    (*list)[(*count)++] = position;
    return NMO_OK;
}

static int writer_track_u32_sequence_start(nmo_chunk_writer_t *w,
                                           uint32_t **list,
                                           size_t *count,
                                           size_t *capacity,
                                           uint32_t position) {
    int result = ensure_u32_list_capacity(w, list, *count, capacity, 2);
    if (result != NMO_OK) {
        return result;
    }

    (*list)[(*count)++] = LIST_SEQUENCE_MARKER;
    (*list)[(*count)++] = position;
    return NMO_OK;
}

static int track_id_sequence_start(nmo_chunk_writer_t *w, uint32_t position) {
    return writer_track_u32_sequence_start(w,
                                           &w->id_list,
                                           &w->id_count,
                                           &w->id_capacity,
                                           position);
}

static int track_manager_sequence_start(nmo_chunk_writer_t *w, uint32_t position) {
    return writer_track_u32_sequence_start(w,
                                           &w->manager_list,
                                           &w->manager_count,
                                           &w->manager_capacity,
                                           position);
}

static int track_manager_position(nmo_chunk_writer_t *w, uint32_t position) {
    return writer_track_u32_position(w,
                                     &w->manager_list,
                                     &w->manager_count,
                                     &w->manager_capacity,
                                     position);
}

static int track_chunk_sequence_start(nmo_chunk_writer_t *w, uint32_t position) {
    return writer_track_u32_sequence_start(w,
                                           &w->chunk_ref_list,
                                           &w->chunk_ref_count,
                                           &w->chunk_ref_capacity,
                                           position);
}

static nmo_status_t writer_append_sequence_count(nmo_chunk_writer_t *w,
                                                 size_t count,
                                                 writer_track_position_fn track,
                                                 int should_track) {
    if (count > UINT32_MAX || (should_track && w->data_size > UINT32_MAX)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    if (should_track && track != NULL) {
        result = track(w, (uint32_t) w->data_size);
        if (result != NMO_OK) {
            return result;
        }
    }

    w->data[w->data_size++] = (uint32_t) count;
    return NMO_OK;
}

static nmo_status_t writer_reserve_tracked_dword_span(nmo_chunk_writer_t *w,
                                                      size_t count,
                                                      writer_track_position_fn track,
                                                      uint32_t **out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (track != NULL && w->data_size > UINT32_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, count);
    if (result != NMO_OK) {
        return result;
    }

    if (track != NULL) {
        result = track(w, (uint32_t) w->data_size);
        if (result != NMO_OK) {
            return result;
        }
    }

    *out = &w->data[w->data_size];
    w->data_size += count;
    return NMO_OK;
}

static inline int writer_has_file_context(const nmo_chunk_writer_t *w) {
    return (w != NULL) && (w->file_context != NULL) &&
           (w->file_context->runtime_to_file != NULL);
}

// Helper to add ID to tracking list
/**
 * @brief Track position for object ID (for later remapping)
 *
 * Adds the current write position to the ID list. This list will be used
 * during save/load to remap object IDs to file indices and vice versa.
 * Matches CKStateChunk behavior where m_Ids->AddEntry(CurrentPos) is called.
 *
 * @param w Writer context
 * @return NMO_OK on success, error code on failure
 */
static int track_id_position(nmo_chunk_writer_t *w) {
    // Track the current position (not the ID itself!)
    return writer_track_u32_position(w,
                                     &w->id_list,
                                     &w->id_count,
                                     &w->id_capacity,
                                     (uint32_t) w->data_size);
}

static int encode_object_id(const nmo_chunk_writer_t *w,
                            nmo_object_id_t id,
                            uint32_t *out_value) {
    if (out_value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!writer_has_file_context(w)) {
        *out_value = (uint32_t) id;
        return NMO_OK;
    }

    if (id == 0) {
        *out_value = NMO_OBJECT_ID_INVALID;
        return NMO_OK;
    }

    nmo_object_id_t file_id = 0;
    nmo_status_t remap = nmo_id_remap_lookup_id(w->file_context->runtime_to_file,
                                                id,
                                                &file_id);
    if (remap != NMO_OK) {
        *out_value = NMO_OBJECT_ID_INVALID;
        return NMO_OK;
    }

    *out_value = (uint32_t) file_id;
    return NMO_OK;
}

nmo_chunk_writer_t *nmo_chunk_writer_create(nmo_arena_t *arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_chunk_writer_t *w = (nmo_chunk_writer_t *) nmo_arena_alloc(arena,
                                                                   sizeof(nmo_chunk_writer_t),
                                                                   sizeof(void *));
    if (w == NULL) {
        return NULL;
    }

    memset(w, 0, sizeof(nmo_chunk_writer_t));
    w->arena = arena;
    w->data_capacity = WRITER_INITIAL_CAPACITY;
    w->version_stack_top = -1;  // Empty stack

    // Allocate initial buffer
    w->data = (uint32_t *) nmo_arena_alloc(arena,
                                           WRITER_INITIAL_CAPACITY * sizeof(uint32_t),
                                           sizeof(uint32_t));
    if (w->data == NULL) {
        return NULL;
    }

    return w;
}

void nmo_chunk_writer_set_file_context(nmo_chunk_writer_t *w,
                                       const nmo_chunk_file_context_t *ctx) {
    if (w == NULL) {
        return;
    }

    w->file_context = ctx;

    if (w->chunk != NULL) {
        if (writer_has_file_context(w)) {
            w->chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        } else {
            w->chunk->chunk_options &= ~NMO_CHUNK_OPTION_FILE;
        }
    }
}

void nmo_chunk_writer_start(nmo_chunk_writer_t *w, nmo_class_id_t class_id, uint32_t chunk_version) {
    if (w == NULL) {
        return;
    }

    // Create new chunk
    w->chunk = nmo_chunk_create(w->arena);
    if (w->chunk == NULL) {
        return;
    }

    w->chunk->class_id = class_id;
    w->chunk->chunk_version = chunk_version;
    w->chunk->chunk_class_id = (uint8_t) (class_id & 0xFF);
    w->chunk->data_version = 0;
    if (writer_has_file_context(w)) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    } else {
        w->chunk->chunk_options &= ~NMO_CHUNK_OPTION_FILE;
    }

    // Reset state
    w->data_size = 0;
    w->id_count = 0;
    w->manager_count = 0;
    w->chunk_ref_count = 0;
    w->chunk_count = 0;
    w->prev_identifier_pos = 0;
    w->finalized = 0;
}

nmo_status_t nmo_chunk_writer_write_byte(nmo_chunk_writer_t *w, uint8_t value) {
    return writer_append_dword(w, (uint32_t) value);
}

nmo_status_t nmo_chunk_writer_write_word(nmo_chunk_writer_t *w, uint16_t value) {
    return writer_append_dword(w, (uint32_t) value);
}

nmo_status_t nmo_chunk_writer_write_dword(nmo_chunk_writer_t *w, uint32_t value) {
    return writer_append_dword(w, value);
}

nmo_status_t nmo_chunk_writer_write_int(nmo_chunk_writer_t *w, int32_t value) {
    // Reinterpret int32 as uint32
    uint32_t uvalue;
    memcpy(&uvalue, &value, sizeof(uint32_t));
    return writer_append_dword(w, uvalue);
}

nmo_status_t nmo_chunk_writer_write_float(nmo_chunk_writer_t *w, float value) {
    // Reinterpret float as uint32
    uint32_t uvalue;
    memcpy(&uvalue, &value, sizeof(uint32_t));
    return writer_append_dword(w, uvalue);
}

nmo_status_t nmo_chunk_writer_write_guid(nmo_chunk_writer_t *w, nmo_guid_t guid) {
    return writer_append_dword_pair(w, guid.d1, guid.d2);
}

nmo_status_t nmo_chunk_writer_write_bytes(nmo_chunk_writer_t *w, const void *data, size_t bytes) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    if (data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return writer_append_aligned_bytes(w, data, bytes);
}

/**
 * @brief Write buffer without size prefix
 *
 * Writes raw buffer data without a size prefix, matching
 * CKStateChunk::WriteBufferNoSize_LEndian behavior.
 * Reference: CKStateChunk.cpp:1117-1124
 *
 * @param w Writer context
 * @param bytes Number of bytes to write
 * @param data Source data
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_writer_write_buffer_nosize(nmo_chunk_writer_t *w, size_t bytes, const void *data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }
    if (data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return writer_append_aligned_bytes(w, data, bytes);
}

nmo_status_t nmo_chunk_writer_write_buffer_nosize_lendian16(nmo_chunk_writer_t *w, size_t value_count, const void *data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (value_count == 0) {
        return NMO_OK;
    }

    if (data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, value_count);
    if (result != NMO_OK) {
        return result;
    }

    const uint16_t *values = (const uint16_t *)data;
    for (size_t i = 0; i < value_count; i++) {
        w->data[w->data_size++] = writer_pack_lendian16_word(values[i]);
    }

    return NMO_OK;
}

/**
 * @brief Lock write buffer for direct writing
 *
 * Returns a pointer to the chunk's data buffer for direct writing.
 * Caller must ensure they write exactly dword_count DWORDs.
 * Matches CKStateChunk::LockWriteBuffer behavior.
 * Reference: CKStateChunk.cpp:327-332
 *
 * @param w Writer context
 * @param dword_count Number of DWORDs to reserve
 * @return Pointer to write buffer, or NULL on error
 */
uint32_t *nmo_chunk_writer_lock_write_buffer(nmo_chunk_writer_t *w, size_t dword_count) {
    if (w == NULL || w->finalized) {
        return NULL;
    }

    int result = ensure_data_capacity(w, dword_count);
    if (result != NMO_OK) {
        return NULL;
    }

    uint32_t *ptr = &w->data[w->data_size];
    w->data_size += dword_count;
    return ptr;
}

nmo_status_t nmo_chunk_writer_write_string(nmo_chunk_writer_t *w, const char *str) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // CK2 WriteString writes size = strlen + 1 (includes null terminator)
    // Reference: CKStateChunk::WriteString() (CKStateChunk.cpp:1204-1214)
    if (str == NULL) {
        // Write size = 0 for NULL string
        return nmo_chunk_writer_write_dword(w, 0);
    }

    uint32_t len = (uint32_t) strlen(str);
    uint32_t size = len + 1;  // Include null terminator in size

    // Write size (includes null terminator)
    int result = nmo_chunk_writer_write_dword(w, size);
    if (result != NMO_OK) {
        return result;
    }

    // Write string data including null terminator
    if (size > 0) {
        result = nmo_chunk_writer_write_bytes(w, str, size);
        if (result != NMO_OK) {
            return result;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_write_buffer(nmo_chunk_writer_t *w, const void *data, size_t size) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Write size
    int result = nmo_chunk_writer_write_dword(w, (uint32_t) size);
    if (result != NMO_OK) {
        return result;
    }

    // Write buffer data (if not empty)
    if (size > 0) {
        if (data == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        result = nmo_chunk_writer_write_bytes(w, data, size);
        if (result != NMO_OK) {
            return result;
        }
    }

    return NMO_OK;
}

/**
 * @brief Write object ID and track position
 *
 * Writes an object ID to the chunk. If the ID is non-zero and we're not in
 * file context, tracks the position for later remapping.
 * Reference: CKStateChunk::WriteObjectID() (CKStateChunk.cpp:557-570)
 *
 * @param w Writer context
 * @param id Object ID to write
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_writer_write_object_id(nmo_chunk_writer_t *w, nmo_object_id_t id) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int in_file_context = writer_has_file_context(w);
    if (id != 0 && !in_file_context && w->data_size > UINT32_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Ensure capacity for the ID (1 DWORD)
    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    uint32_t encoded_value = 0;
    result = encode_object_id(w, id, &encoded_value);
    if (result != NMO_OK) {
        return result;
    }

    // If ID is non-zero and we don't have file context, track the position
    if (id != 0 && !in_file_context) {
        result = track_id_position(w);
        if (result != NMO_OK) {
            return result;
        }
    }

    w->data[w->data_size++] = encoded_value;

    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }

#ifndef NDEBUG
    if (w->intlist_audit.active) {
        w->intlist_audit.written_count++;
    }
#endif

    if (w->version_stack_top >= 0) {
        nmo_chunk_version_context_t *ctx = &w->version_stack[w->version_stack_top];
        if (ctx->expected_ids >= 0) {
            ctx->written_ids++;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_start_object_sequence(nmo_chunk_writer_t *w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int in_file_context = writer_has_file_context(w);
    nmo_status_t result = writer_append_sequence_count(w,
                                                       count,
                                                       track_id_sequence_start,
                                                       count > 0 && !in_file_context);
    if (result != NMO_OK) {
        return result;
    }

    if (w->version_stack_top >= 0 && count <= (size_t)INT_MAX) {
        nmo_chunk_writer_set_expected_ids(w, (int)count);
    }
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }
    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_start_manager_sequence(nmo_chunk_writer_t *w,
                                            nmo_guid_t manager,
                                            size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (count > UINT32_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t *span = NULL;
    int result = writer_reserve_tracked_dword_span(w,
                                                   3,
                                                   track_manager_sequence_start,
                                                   &span);
    if (result != NMO_OK) {
        return result;
    }

    span[0] = (uint32_t) count;
    span[1] = manager.d1;
    span[2] = manager.d2;
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
    }
    return NMO_OK;
}

/**
 * @brief Start sub-chunk sequence
 *
 * Writes the count of sub-chunks that will follow and tracks the position.
 * Matches CKStateChunk::StartSubChunkSequence behavior.
 * Reference: CKStateChunk.cpp:875-881
 *
 * @param w Writer context
 * @param count Number of sub-chunks in sequence
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_writer_start_subchunk_sequence(nmo_chunk_writer_t *w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t result = writer_append_sequence_count(w,
                                                       count,
                                                       track_chunk_sequence_start,
                                                       1);
    if (result != NMO_OK) {
        return result;
    }
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    }
    return NMO_OK;
}

/**
 * @brief Write sub-chunk to parent chunk
 *
 * Serializes a complete sub-chunk into the parent chunk's data buffer.
 * Matches CKStateChunk::WriteSubChunkSequence behavior.
 * Reference: CKStateChunk.cpp:887-919
 *
 * Format:
 * - Size (in DWORDs, minus 1)
 * - ClassID
 * - Version (data version in lower 16 bits, chunk version in upper 16 bits)
 * - ChunkSize (data size in DWORDs)
 * - HasFile flag (bool)
 * - ID count
 * - Chunk count
 * - Manager count (if chunk version > 4)
 * - Data buffer (if ChunkSize != 0)
 * - IDs buffer (if ID count > 0)
 * - Chunks buffer (if chunk count > 0)
 * - Managers buffer (if manager count > 0)
 *
 * @param w Writer context
 * @param sub Sub-chunk to write (can be NULL for empty slot)
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_writer_write_subchunk(nmo_chunk_writer_t *w, const nmo_chunk_t *sub) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t manager_count_field = 0;
    size_t payload_dwords = 0;
    const int has_subchunk = (sub != NULL);

    if (has_subchunk) {
        const int include_manager_field = (sub->chunk_version > 4); /* literal 4 */
        nmo_status_t result = writer_validate_dword_array(&sub->data);
        if (result != NMO_OK) return result;
        result = writer_validate_dword_array(&sub->ids);
        if (result != NMO_OK) return result;
        result = writer_validate_dword_array(&sub->chunk_refs);
        if (result != NMO_OK) return result;
        if (include_manager_field) {
            result = writer_validate_dword_array(&sub->managers);
            if (result != NMO_OK) return result;
        }

        manager_count_field = include_manager_field ? (uint32_t)sub->managers.count : 0u;
        payload_dwords = include_manager_field ? 7u : 6u;
        const size_t counts[] = {
            sub->data.count,
            sub->ids.count,
            sub->chunk_refs.count,
            manager_count_field,
        };
        for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
            if (!nmo_safe_add_size(payload_dwords, counts[i], &payload_dwords) ||
                payload_dwords > UINT32_MAX) {
                return NMO_ERR_INVALID_ARGUMENT;
            }
        }
        if (w->data_size > UINT32_MAX) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    /* Size field stores number of DWORDs following it */
    size_t total_needed = 0;
    if (!nmo_safe_add_size(payload_dwords, 1u, &total_needed)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, total_needed);
    if (result != NMO_OK) {
        return result;
    }
    if (has_subchunk) {
        result = ensure_chunk_list_capacity(w, 1u);
        if (result != NMO_OK) {
            return result;
        }
        result = ensure_u32_list_capacity(w,
                                          &w->chunk_ref_list,
                                          w->chunk_ref_count,
                                          &w->chunk_ref_capacity,
                                          1u);
        if (result != NMO_OK) {
            return result;
        }
    }

    const uint32_t size_field_position = (uint32_t)w->data_size;
    w->data[w->data_size++] = (uint32_t)payload_dwords;

    if (has_subchunk) {
        w->chunk_list[w->chunk_count++] = (nmo_chunk_t *)sub;
        w->chunk_ref_list[w->chunk_ref_count++] = size_field_position;

        /* Class ID (full 32-bit) */
        w->data[w->data_size++] = sub->class_id;

        /* CK2 sub-chunk header: version dword stores 16-bit data/chunk versions */
        uint32_t version_info = (uint32_t) (sub->data_version & 0xFFFFu) |
                    ((uint32_t) (sub->chunk_version & 0xFFFFu) << 16);
        w->data[w->data_size++] = version_info;

        /* Chunk size in DWORDs */
        w->data[w->data_size++] = (uint32_t) sub->data.count;

        /* HasFile flag */
        uint32_t has_file = (sub->chunk_options & NMO_CHUNK_OPTION_FILE) ? 1u : 0u;
        w->data[w->data_size++] = has_file;

        /* ID and sub-chunk counts are always written */
        w->data[w->data_size++] = (uint32_t) sub->ids.count;
        w->data[w->data_size++] = (uint32_t) sub->chunk_refs.count;

        if (sub->chunk_version > 4) {
            w->data[w->data_size++] = manager_count_field;
        }

        /* Data buffer */
        if (sub->data.count > 0) {
            const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->data);
            writer_append_reserved_dwords(w, data, sub->data.count);
        }

        /* IDs */
        if (sub->ids.count > 0) {
            const uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->ids);
            writer_append_reserved_dwords(w, ids, sub->ids.count);
        }

        /* Sub-chunk reference positions */
        if (sub->chunk_refs.count > 0) {
            const uint32_t *chunk_refs = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->chunk_refs);
            writer_append_reserved_dwords(w, chunk_refs, sub->chunk_refs.count);
        }

        /* Manager data */
        if (manager_count_field > 0) {
            const uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &sub->managers);
            writer_append_reserved_dwords(w, managers, manager_count_field);
        }
    }

    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    }

    return NMO_OK;
}

/**
 * @brief Write manager int with GUID
 *
 * Writes [GUID.d1][GUID.d2][value] and tracks position in managers list.
 * Reference: CKStateChunk::WriteManagerInt() (CKStateChunk.cpp:433-441)
 *
 * @param w Writer context
 * @param manager Manager GUID
 * @param value Value to write
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_writer_write_manager_int(nmo_chunk_writer_t *w, nmo_guid_t manager, int32_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t *span = NULL;
    int result = writer_reserve_tracked_dword_span(w,
                                                   3,
                                                   track_manager_position,
                                                   &span);
    if (result != NMO_OK) {
        return result;
    }

    span[0] = manager.d1;
    span[1] = manager.d2;
    span[2] = (uint32_t) value;

    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
    }

    return NMO_OK;
}

/**
 * @brief Write array with little-endian byte order
 *
 * Writes array in format: [totalBytes][elementCount][data padded to DWORDs].
 * Includes overflow protection matching CKStateChunk behavior.
 * Reference: CKStateChunk::WriteArray_LEndian() (CKStateChunk.cpp:443-487)
 *
 * @param w Writer context
 * @param element_count Number of elements in array
 * @param element_size Size of each element in bytes
 * @param src_data Source data pointer (can be NULL for empty array)
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_writer_write_array_lendian(nmo_chunk_writer_t *w, int element_count, int element_size,
                                         const void *src_data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Write empty array marker if no data
    if (src_data == NULL || element_count <= 0) {
        return writer_append_empty_array_marker(w);
    }
    if (element_size <= 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check for integer overflow in total bytes calculation
    if (element_count > INT_MAX / element_size) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Calculate total bytes
    size_t total_bytes = (size_t) element_size * (size_t) element_count;
    if (total_bytes > (size_t) INT_MAX) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Calculate DWORDs needed (round up)
    size_t dword_count = (total_bytes + sizeof(uint32_t) - 1) / sizeof(uint32_t);

    // Ensure capacity for [totalBytes][elementCount][data]
    size_t needed_dwords = 2 + dword_count;
    int result = ensure_data_capacity(w, needed_dwords);
    if (result != NMO_OK) {
        return result;
    }

    // Write array header
    w->data[w->data_size++] = (uint32_t) total_bytes;
    w->data[w->data_size++] = (uint32_t) element_count;

    // Copy array data
    memset(&w->data[w->data_size], 0, dword_count * sizeof(uint32_t));
    memcpy(&w->data[w->data_size], src_data, total_bytes);
    w->data_size += dword_count;

    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_write_array_lendian16(nmo_chunk_writer_t *w, int element_count, int element_size,
                                           const void *src_data) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (src_data == NULL || element_count <= 0 || element_size <= 0) {
        return nmo_chunk_writer_write_array_lendian(w, element_count, element_size, src_data);
    }

    size_t total_bytes = (size_t) element_count * (size_t) element_size;
    nmo_allocator_t alloc = nmo_allocator_default();
    void *temp = nmo_alloc(&alloc, total_bytes, 1);
    if (temp == NULL) {
        return NMO_ERR_NOMEM;
    }

    memcpy(temp, src_data, total_bytes);
    if (total_bytes > 1) {
        nmo_swap_16bit_words(temp, total_bytes / 2);
    }

    int result = nmo_chunk_writer_write_array_lendian(w, element_count, element_size, temp);
    nmo_free(&alloc, temp);
    return result;
#else
    return nmo_chunk_writer_write_array_lendian(w, element_count, element_size, src_data);
#endif
}

nmo_status_t nmo_chunk_writer_write_dword_as_words(nmo_chunk_writer_t *w, uint32_t value) {
    uint16_t low = (uint16_t)(value & 0xFFFFu);
    uint16_t high = (uint16_t)((value >> 16) & 0xFFFFu);
    return writer_append_dword_pair(w,
                                    writer_pack_lendian16_word(low),
                                    writer_pack_lendian16_word(high));
}

nmo_status_t nmo_chunk_writer_write_array_dword_as_words(nmo_chunk_writer_t *w,
                                                const uint32_t *values,
                                                size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (count == 0) {
        return NMO_OK;
    }

    if (values == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (count > SIZE_MAX / 2u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t dword_count = count * 2u;
    int result = ensure_data_capacity(w, dword_count);
    if (result != NMO_OK) {
        return result;
    }

    for (size_t i = 0; i < count; i++) {
        uint32_t value = values[i];
        w->data[w->data_size++] =
            writer_pack_lendian16_word((uint16_t)(value & 0xFFFFu));
        w->data[w->data_size++] =
            writer_pack_lendian16_word((uint16_t)((value >> 16) & 0xFFFFu));
    }

    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_write_buffer_lendian16(nmo_chunk_writer_t *w, size_t bytes, const void *data) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (w == NULL || w->finalized || data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    void *temp = nmo_alloc(&alloc, bytes, 1);
    if (temp == NULL) {
        return NMO_ERR_NOMEM;
    }

    memcpy(temp, data, bytes);
    if (bytes > 1) {
        nmo_swap_16bit_words(temp, bytes / 2);
    }

    int result = nmo_chunk_writer_write_buffer_nosize(w, bytes, temp);
    nmo_free(&alloc, temp);
    return result;
#else
    return nmo_chunk_writer_write_buffer_nosize(w, bytes, data);
#endif
}

/**
 * @brief Write identifier with linked-list chaining
 *
 * Writes an identifier pair [ID][NextPos] and updates the previous
 * identifier's NextPos to point to this one, forming a linked list.
 *
 * Reference: CKStateChunk::WriteIdentifier() (CKStateChunk.cpp:213-223)
 *
 * @param w Writer context
 * @param identifier Identifier value to write
 * @return NMO_OK on success, error code otherwise
 */
nmo_status_t nmo_chunk_writer_write_identifier(nmo_chunk_writer_t *w, uint32_t identifier) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Ensure capacity for [ID][NextPos] pair (2 DWORDs = 8 bytes)
    int result = ensure_data_capacity(w, 2);
    if (result != NMO_OK) {
        return result;
    }

    // If there was a previous identifier, link it to this one
    // by updating its "next" pointer (at prev_identifier_pos+1)
    if (w->prev_identifier_pos < w->data_size) {
        w->data[w->prev_identifier_pos + 1] = w->data_size;
    }

    // Update prev_identifier_pos to current position
    w->prev_identifier_pos = w->data_size;

    // Write [ID][0] pair
    // The 0 will be updated by the next WriteIdentifier call
    w->data[w->data_size++] = identifier;
    w->data[w->data_size++] = 0;

    return NMO_OK;
}

/* ============================================================================
 * Reserve-and-Patch API Reserve-and-Patch:
 * ============================================================================ */

nmo_patch_token_t nmo_chunk_writer_reserve_u32(nmo_chunk_writer_t *w) {
    return writer_reserve_dword_span(w, 1);
}

nmo_patch_token_t nmo_chunk_writer_reserve_u64(nmo_chunk_writer_t *w) {
    return writer_reserve_dword_span(w, 2);
}

nmo_patch_token_t nmo_chunk_writer_reserve_dwords(nmo_chunk_writer_t *w, size_t dword_count) {
    return writer_reserve_dword_span(w, dword_count);
}

nmo_status_t nmo_chunk_writer_patch_u32(nmo_chunk_writer_t *w, nmo_patch_token_t token, uint32_t value) {
    uint32_t *span = NULL;
    nmo_status_t result = writer_patch_dword_span(w, token, 1, &span);
    if (result != NMO_OK) {
        return result;
    }

    span[0] = value;
    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_patch_u64(nmo_chunk_writer_t *w, nmo_patch_token_t token, uint64_t value) {
    uint32_t *span = NULL;
    nmo_status_t result = writer_patch_dword_span(w, token, 2, &span);
    if (result != NMO_OK) {
        return result;
    }

    span[0] = (uint32_t)(value & 0xFFFFFFFF);
    span[1] = (uint32_t)(value >> 32);
    return NMO_OK;
}

size_t nmo_chunk_writer_tell(const nmo_chunk_writer_t *w) {
    if (w == NULL) {
        return 0;
    }
    return w->data_size;
}

nmo_chunk_t *nmo_chunk_writer_finalize(nmo_chunk_writer_t *w) {
    if (w == NULL || w->finalized || w->chunk == NULL) {
        return NULL;
    }

    nmo_status_t result = writer_copy_finalized_span(
        &w->chunk->data,
        w->data,
        w->data_size,
        sizeof(uint32_t));
    NMO_RETURN_NULL_IF_ERROR(result);

    // Copy ID list
    if (w->id_count > 0) {
        result = writer_copy_finalized_span(
            &w->chunk->ids,
            w->id_list,
            w->id_count,
            sizeof(uint32_t));
        NMO_RETURN_NULL_IF_ERROR(result);
    }

    // Copy manager list
    if (w->manager_count > 0) {
        result = writer_copy_finalized_span(
            &w->chunk->managers,
            w->manager_list,
            w->manager_count,
            sizeof(uint32_t));
        NMO_RETURN_NULL_IF_ERROR(result);
    }

    // Copy chunk list
    if (w->chunk_count > 0) {
        result = writer_copy_finalized_span(
            &w->chunk->chunks,
            w->chunk_list,
            w->chunk_count,
            sizeof(nmo_chunk_t *));
        NMO_RETURN_NULL_IF_ERROR(result);
    }

    if (w->chunk_ref_count > 0) {
        result = writer_copy_finalized_span(
            &w->chunk->chunk_refs,
            w->chunk_ref_list,
            w->chunk_ref_count,
            sizeof(uint32_t));
        NMO_RETURN_NULL_IF_ERROR(result);
    }

    w->finalized = 1;
    return w->chunk;
}

void nmo_chunk_writer_destroy(nmo_chunk_writer_t *w) {
    // Writer is allocated from arena, so no explicit free needed
    // Arena will clean up everything when destroyed
    (void) w;
}

// Math type write functions

nmo_status_t nmo_chunk_writer_write_vector2(nmo_chunk_writer_t *w, const nmo_vector2_t *v) {
    if (v == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const float values[2] = { v->x, v->y };
    return writer_append_float_span(w, values, 2);
}

nmo_status_t nmo_chunk_writer_write_vector(nmo_chunk_writer_t *w, const nmo_vector_t *v) {
    if (v == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const float values[3] = { v->x, v->y, v->z };
    return writer_append_float_span(w, values, 3);
}

nmo_status_t nmo_chunk_writer_write_vector4(nmo_chunk_writer_t *w, const nmo_vector4_t *v) {
    if (v == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const float values[4] = { v->x, v->y, v->z, v->w };
    return writer_append_float_span(w, values, 4);
}

nmo_status_t nmo_chunk_writer_write_matrix(nmo_chunk_writer_t *w, const nmo_matrix_t *m) {
    if (m == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return writer_append_float_span(w, &m->m[0][0], 16);
}

nmo_status_t nmo_chunk_writer_write_quaternion(nmo_chunk_writer_t *w, const nmo_quaternion_t *q) {
    if (q == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const float values[4] = { q->x, q->y, q->z, q->w };
    return writer_append_float_span(w, values, 4);
}

nmo_status_t nmo_chunk_writer_write_color(nmo_chunk_writer_t *w, const nmo_color_t *c) {
    if (c == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const float values[4] = { c->r, c->g, c->b, c->a };
    return writer_append_float_span(w, values, 4);
}

/* ========================================================================
 * Version Context Stack Implementation
 * ======================================================================== */

nmo_status_t nmo_chunk_writer_push_context(nmo_chunk_writer_t *w, uint32_t version) {
    if (w == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check stack overflow */
    if (w->version_stack_top >= NMO_CHUNK_WRITER_MAX_DEPTH - 1) {
        return NMO_ERR_BUFFER_OVERRUN;
    }

    /* Push new context */
    w->version_stack_top++;
    nmo_chunk_version_context_t *ctx = &w->version_stack[w->version_stack_top];
    ctx->chunk_version = version;
    ctx->header_offset = w->data_size;  /* Current position as header start */
    ctx->expected_ids = -1;             /* Not tracking by default */
    ctx->written_ids = 0;

    return NMO_OK;
}

nmo_status_t nmo_chunk_writer_pop_context(nmo_chunk_writer_t *w) {
    if (w == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check stack underflow */
    if (w->version_stack_top < 0) {
        return NMO_ERR_INVALID_STATE;
    }

    /* Optionally validate ID count if tracking was enabled */
    nmo_chunk_version_context_t *ctx = &w->version_stack[w->version_stack_top];
    if (ctx->expected_ids >= 0 && ctx->expected_ids != ctx->written_ids) {
        /* ID count mismatch - this is a validation error but we still pop */
        /* The caller should check this via debug mode or logging */
    }

    w->version_stack_top--;
    return NMO_OK;
}

uint32_t nmo_chunk_writer_parent_version(const nmo_chunk_writer_t *w) {
    if (w == NULL) {
        return 0;
    }

    /* Need at least 2 contexts: current (index 0+) and parent (index-1) */
    if (w->version_stack_top < 1) {
        return 0;  /* No parent available */
    }

    /* Return parent's version (one level up from current) */
    return w->version_stack[w->version_stack_top - 1].chunk_version;
}

int nmo_chunk_writer_depth(const nmo_chunk_writer_t *w) {
    if (w == NULL) {
        return 0;
    }

    /* stack_top of -1 means 0 depth, 0 means depth 1, etc. */
    return w->version_stack_top + 1;
}

void nmo_chunk_writer_set_expected_ids(nmo_chunk_writer_t *w, int expected_count) {
    if (w == NULL || w->version_stack_top < 0) {
        return;
    }

    w->version_stack[w->version_stack_top].expected_ids = expected_count;
}

/* ========================================================================
 * IntList Auditor Implementation
 * ======================================================================== */

void nmo_chunk_writer_begin_intlist(nmo_chunk_writer_t *w,
                                    int expected_count,
                                    const char *context) {
#ifndef NDEBUG
    if (w == NULL) {
        return;
    }

    /* Initialize audit state */
    w->intlist_audit.expected_count = expected_count;
    w->intlist_audit.written_count = 0;
    w->intlist_audit.start_offset = w->data_size;
    w->intlist_audit.active = 1;

    /* Copy context string (truncate if too long) */
    if (context != NULL) {
        size_t len = strlen(context);
        if (len >= NMO_INTLIST_CONTEXT_MAX) {
            len = NMO_INTLIST_CONTEXT_MAX - 1;
        }
        memcpy(w->intlist_audit.context, context, len);
        w->intlist_audit.context[len] = '\0';
    } else {
        w->intlist_audit.context[0] = '\0';
    }
#else
    (void)w;
    (void)expected_count;
    (void)context;
#endif
}

nmo_status_t nmo_chunk_writer_write_object_id_audited(nmo_chunk_writer_t *w, nmo_object_id_t id) {
    if (w == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Delegate to regular write_object_id */
    return nmo_chunk_writer_write_object_id(w, id);
}

nmo_status_t nmo_chunk_writer_end_intlist(nmo_chunk_writer_t *w) {
#ifndef NDEBUG
    if (w == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check if audit was active */
    if (!w->intlist_audit.active) {
        return NMO_ERR_INVALID_STATE;
    }

    /* Validate counts */
    int expected = w->intlist_audit.expected_count;
    int written = w->intlist_audit.written_count;

    /* Clear audit state before potential assertion */
    w->intlist_audit.active = 0;

    if (expected != written) {
        /* Log detailed error for debugging */
        fprintf(stderr,
                "[NMO IntList Auditor] Count mismatch in '%s': "
                "expected %d, wrote %d (offset: %zu DWORDs)\n",
                w->intlist_audit.context[0] ? w->intlist_audit.context : "<unknown>",
                expected, written, w->intlist_audit.start_offset);

#ifdef NMO_INTLIST_AUDIT_HARD
        /* Hard mode: assertion failure (opt-in via compile flag) */
        assert(expected == written && "IntList count mismatch - see stderr for details");
#endif
        /* Default: return error code for caller to handle */
        return NMO_ERR_CORRUPT;
    }

    return NMO_OK;
#else
    (void)w;
    return NMO_OK;
#endif
}

int nmo_chunk_writer_intlist_audit_active(const nmo_chunk_writer_t *w) {
#ifndef NDEBUG
    if (w == NULL) {
        return 0;
    }
    return w->intlist_audit.active;
#else
    (void)w;
    return 0;
#endif
}

const char* nmo_chunk_writer_intlist_audit_context(const nmo_chunk_writer_t *w) {
#ifndef NDEBUG
    if (w == NULL || !w->intlist_audit.active) {
        return NULL;
    }
    return w->intlist_audit.context;
#else
    (void)w;
    return NULL;
#endif
}
