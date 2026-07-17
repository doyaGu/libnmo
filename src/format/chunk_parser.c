#include "format/nmo_chunk_parser.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_utils.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define NMO_CHUNK_PARSER_DATA(p) (NMO_ARENA_ARRAY_DATA(uint32_t, &((p)->chunk->data)))
#define NMO_CHUNK_PARSER_DATA_SIZE(p) ((p)->chunk->data.count)

#define NMO_PARSER_RETURN_ERROR(code, message) \
    NMO_RETURN_ERROR((code), NMO_SEVERITY_ERROR, (message))
#define NMO_PARSER_RETURN_INVALID_ARGUMENT(message) \
    NMO_PARSER_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, (message))
#define NMO_PARSER_RETURN_INVALID_OFFSET(message) \
    NMO_PARSER_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, (message))
#define NMO_PARSER_RETURN_TRUNCATED(message) \
    NMO_PARSER_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, (message))
#define NMO_PARSER_RETURN_NOMEM(message) \
    NMO_PARSER_RETURN_ERROR(NMO_ERR_NOMEM, (message))

#define NMO_PARSER_RESTORE_CURSOR(p, start_pos) \
    do { \
        if ((p) != NULL) { \
            (p)->cursor = (start_pos); \
        } \
    } while (0)

#define NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos) \
    do { \
        nmo_status_t parser_status__ = (result); \
        if (parser_status__ != NMO_OK) { \
            NMO_PARSER_RESTORE_CURSOR((p), (start_pos)); \
            return parser_status__; \
        } \
    } while (0)

#define NMO_PARSER_RETURN_ERROR_ROLLBACK(p, start_pos, code, message) \
    do { \
        NMO_PARSER_RESTORE_CURSOR((p), (start_pos)); \
        NMO_PARSER_RETURN_ERROR((code), (message)); \
    } while (0)

#define NMO_PARSER_RETURN_TRUNCATED_ROLLBACK(p, start_pos, message) \
    NMO_PARSER_RETURN_ERROR_ROLLBACK((p), (start_pos), NMO_ERR_TRUNCATED_CHUNK, (message))

#define NMO_PARSER_RETURN_NOMEM_ROLLBACK(p, start_pos, message) \
    NMO_PARSER_RETURN_ERROR_ROLLBACK((p), (start_pos), NMO_ERR_NOMEM, (message))

/**
 * @brief Chunk parser structure
 */
typedef struct nmo_chunk_parser {
    nmo_chunk_t *chunk;         /**< Chunk being parsed */
    size_t cursor;              /**< Current position in DWORDs */
    size_t prev_identifier_pos; /**< Position of previous identifier for linked-list traversal */
    nmo_allocator_t *alloc;     /**< Allocator for parser itself */
    const nmo_chunk_file_context_t *file_context; /**< Optional file remap context */
    size_t object_sequence_remaining; /**< Remaining entries in current object sequence */
    size_t manager_sequence_remaining; /**< Remaining entries in current manager sequence */
    size_t subchunk_sequence_remaining; /**< Remaining entries in current sub-chunk sequence */
    int in_object_sequence;     /**< Whether parser is inside an object ID sequence */
    int in_manager_sequence;    /**< Whether parser is inside a manager int sequence */
    int in_subchunk_sequence;   /**< Whether parser is inside a sub-chunk sequence */
    nmo_guid_t current_manager_guid; /**< Active manager GUID for sequence tracking */
} nmo_chunk_parser_t;

// Helper to check if enough data remains
static inline int check_bounds(nmo_chunk_parser_t *p, size_t dwords_needed) {
    if (p == NULL || p->chunk == NULL) {
        return 0;
    }
    size_t data_size = NMO_CHUNK_PARSER_DATA_SIZE(p);
    if (p->cursor > data_size) {
        return 0;
    }
    return dwords_needed <= (data_size - p->cursor);
}

static inline nmo_status_t parser_read_u32_rollback(nmo_chunk_parser_t *p,
                                                    size_t start_pos,
                                                    uint32_t *out_value,
                                                    const char *eof_message) {
    if (p == NULL || out_value == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (!check_bounds(p, 1)) {
        NMO_PARSER_RESTORE_CURSOR(p, start_pos);
        NMO_PARSER_RETURN_TRUNCATED(eof_message);
    }

    *out_value = NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
    NMO_RETURN_OK();
}

static inline nmo_status_t parser_read_u32(nmo_chunk_parser_t *p,
                                           uint32_t *out_value,
                                           const char *eof_message) {
    return parser_read_u32_rollback(p, p != NULL ? p->cursor : 0, out_value, eof_message);
}

static inline nmo_status_t parser_read_guid_rollback(nmo_chunk_parser_t *p,
                                                     size_t start_pos,
                                                     nmo_guid_t *out_guid,
                                                     const char *eof_message) {
    if (p == NULL || out_guid == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (!check_bounds(p, 2)) {
        NMO_PARSER_RESTORE_CURSOR(p, start_pos);
        NMO_PARSER_RETURN_TRUNCATED(eof_message);
    }

    out_guid->d1 = NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
    out_guid->d2 = NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
    NMO_RETURN_OK();
}

static inline nmo_status_t parser_read_guid(nmo_chunk_parser_t *p,
                                            nmo_guid_t *out_guid,
                                            const char *eof_message) {
    return parser_read_guid_rollback(p, p != NULL ? p->cursor : 0, out_guid, eof_message);
}

static inline uint16_t parser_unpack_lendian16_word(uint32_t word) {
    uint16_t value = (uint16_t)(word & 0xFFFFu);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value = (uint16_t)((value >> 8) | (value << 8));
#endif
    return value;
}

static inline nmo_status_t parser_copy_aligned_bytes(nmo_chunk_parser_t *p,
                                                     void *dest,
                                                     size_t bytes,
                                                     const char *eof_message) {
    if (p == NULL || dest == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (bytes == 0) {
        NMO_RETURN_OK();
    }

    size_t dwords_needed = nmo_bytes_to_dwords(bytes);
    if (!check_bounds(p, dwords_needed)) {
        NMO_PARSER_RETURN_TRUNCATED(eof_message);
    }

    memcpy(dest, &NMO_CHUNK_PARSER_DATA(p)[p->cursor], bytes);
    p->cursor += dwords_needed;
    NMO_RETURN_OK();
}

static nmo_status_t parser_read_dword_array_rollback(nmo_chunk_parser_t *p,
                                                     size_t start_pos,
                                                     nmo_arena_array_t *dest,
                                                     size_t count,
                                                     const char *eof_message) {
    if (count == 0) {
        NMO_RETURN_OK();
    }

    if (!check_bounds(p, count)) {
        NMO_PARSER_RETURN_TRUNCATED_ROLLBACK(p, start_pos, eof_message);
    }

    nmo_status_t result = nmo_arena_array_resize(dest, count);
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    uint32_t *out = NMO_ARENA_ARRAY_DATA(uint32_t, dest);
    memcpy(out, &NMO_CHUNK_PARSER_DATA(p)[p->cursor], count * sizeof(uint32_t));
    p->cursor += count;
    NMO_RETURN_OK();
}

static nmo_status_t parser_read_float_span(nmo_chunk_parser_t *p,
                                           float *dest,
                                           size_t count,
                                           const char *eof_message) {
    if (p == NULL || dest == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (!check_bounds(p, count)) {
        NMO_PARSER_RETURN_TRUNCATED(eof_message);
    }

    memcpy(dest, &NMO_CHUNK_PARSER_DATA(p)[p->cursor], count * sizeof(float));
    p->cursor += count;
    NMO_RETURN_OK();
}

static nmo_status_t parser_read_array_lendian_impl(nmo_chunk_parser_t *p,
                                                   void **array,
                                                   size_t *out_count,
                                                   nmo_arena_t *arena,
                                                   int swap_16bit) {
    if (p == NULL || array == NULL || out_count == NULL || arena == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    *array = NULL;
    *out_count = 0;

    size_t start_pos = p->cursor;

    uint32_t data_size_bytes = 0;
    uint32_t element_count = 0;
    nmo_status_t result = parser_read_u32_rollback(
        p,
        start_pos,
        &data_size_bytes,
        "Cannot read array metadata");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &element_count,
        "Cannot read array metadata");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    if ((data_size_bytes == 0u) != (element_count == 0u)) {
        NMO_PARSER_RETURN_ERROR_ROLLBACK(
            p, start_pos, NMO_ERR_INVALID_FORMAT,
            "Array metadata has inconsistent size and count");
    }
    if (data_size_bytes == 0) {
        NMO_RETURN_OK();
    }

#if SIZE_MAX == UINT32_MAX
    if (data_size_bytes > (UINT32_MAX - 3u)) {
        NMO_PARSER_RETURN_ERROR_ROLLBACK(p, start_pos, NMO_ERR_INVALID_FORMAT, "Array size overflow");
    }
#endif

    size_t dword_count = nmo_bytes_to_dwords((size_t)data_size_bytes);
    if (!check_bounds(p, dword_count)) {
        NMO_PARSER_RETURN_TRUNCATED_ROLLBACK(p, start_pos, "Cannot read array data");
    }

    void *array_data = nmo_arena_alloc(arena, (size_t)data_size_bytes, 1);
    if (array_data == NULL) {
        NMO_PARSER_RETURN_NOMEM_ROLLBACK(p, start_pos, "Failed to allocate array data");
    }

    memcpy(array_data, &NMO_CHUNK_PARSER_DATA(p)[p->cursor], (size_t) data_size_bytes);
    p->cursor += (size_t) dword_count;

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (swap_16bit && data_size_bytes >= 2) {
        nmo_swap_16bit_words(array_data, (size_t) data_size_bytes / 2);
    }
#else
    (void) swap_16bit;
#endif

    *array = array_data;
    *out_count = (size_t) element_count;
    NMO_RETURN_OK();
}

static inline void consume_sequence_slot(size_t *remaining, int *active) {
    if (remaining == NULL || active == NULL) {
        return;
    }

    if (*active && *remaining > 0) {
        (*remaining)--;
        if (*remaining == 0) {
            *active = 0;
        }
    }
}

static inline void start_sequence(size_t count,
                                  size_t *remaining,
                                  int *active,
                                  size_t *out_count) {
    if (remaining != NULL) {
        *remaining = count;
    }
    if (active != NULL) {
        *active = (count > 0);
    }
    if (out_count != NULL) {
        *out_count = count;
    }
}

static inline void consume_subchunk_slot(nmo_chunk_parser_t *p) {
    if (p == NULL) {
        return;
    }

    consume_sequence_slot(&p->subchunk_sequence_remaining,
                          &p->in_subchunk_sequence);
}

nmo_chunk_parser_t *nmo_chunk_parser_create(nmo_chunk_t *chunk) {
    if (chunk == NULL) {
        return NULL;
    }

    // Allocate parser using default allocator
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_chunk_parser_t *p = (nmo_chunk_parser_t *) nmo_alloc(&alloc, sizeof(nmo_chunk_parser_t), _Alignof(nmo_chunk_parser_t));
    if (p == NULL) {
        return NULL;
    }

    p->chunk = chunk;
    p->cursor = 0;
    p->prev_identifier_pos = 0;
    p->alloc = NULL;
    p->file_context = NULL;
    p->object_sequence_remaining = 0;
    p->manager_sequence_remaining = 0;
    p->subchunk_sequence_remaining = 0;
    p->in_object_sequence = 0;
    p->in_manager_sequence = 0;
    p->in_subchunk_sequence = 0;
    p->current_manager_guid.d1 = 0;
    p->current_manager_guid.d2 = 0;

    return p;
}

void nmo_chunk_parser_set_file_context(nmo_chunk_parser_t *p,
                                       const nmo_chunk_file_context_t *ctx) {
    if (p == NULL) {
        return;
    }
    p->file_context = ctx;
}

void nmo_chunk_parser_destroy(nmo_chunk_parser_t *p) {
    if (p != NULL) {
        nmo_allocator_t alloc = nmo_allocator_default();
        nmo_free(&alloc, p);
    }
}

size_t nmo_chunk_parser_tell(nmo_chunk_parser_t *p) {
    if (p == NULL) {
        return 0;
    }
    return p->cursor;
}

nmo_status_t nmo_chunk_parser_seek(nmo_chunk_parser_t *p, size_t pos) {
    if (p == NULL || p->chunk == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser");
    }

    if (pos > NMO_CHUNK_PARSER_DATA_SIZE(p)) {
        NMO_PARSER_RETURN_INVALID_OFFSET("Seek beyond end of chunk");
    }

    p->cursor = pos;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_skip(nmo_chunk_parser_t *p, size_t dwords) {
    if (p == NULL || p->chunk == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser");
    }

    size_t data_size = NMO_CHUNK_PARSER_DATA_SIZE(p);
    if (p->cursor > data_size || dwords > (data_size - p->cursor)) {
        NMO_PARSER_RETURN_INVALID_OFFSET("Skip beyond end of chunk");
    }

    p->cursor += dwords;
    NMO_RETURN_OK();
}

size_t nmo_chunk_parser_remaining(nmo_chunk_parser_t *p) {
    if (p == NULL || p->chunk == NULL) {
        return 0;
    }

    if (p->cursor > NMO_CHUNK_PARSER_DATA_SIZE(p)) {
        return 0;
    }

    return NMO_CHUNK_PARSER_DATA_SIZE(p) - p->cursor;
}

int nmo_chunk_parser_at_end(nmo_chunk_parser_t *p) {
    return nmo_chunk_parser_remaining(p) == 0;
}

nmo_status_t nmo_chunk_parser_read_byte(nmo_chunk_parser_t *p, uint8_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    uint32_t dword = 0;
    nmo_status_t result = parser_read_u32(p, &dword, "Cannot read byte");
    NMO_RETURN_IF_ERROR(result);

    *out = (uint8_t) (dword & 0xFF);

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_word(nmo_chunk_parser_t *p, uint16_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    uint32_t dword = 0;
    nmo_status_t result = parser_read_u32(p, &dword, "Cannot read word");
    NMO_RETURN_IF_ERROR(result);

    *out = (uint16_t) (dword & 0xFFFF);

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_dword(nmo_chunk_parser_t *p, uint32_t *out) {
    return parser_read_u32(p, out, "Cannot read dword");
}

nmo_status_t nmo_chunk_parser_read_int(nmo_chunk_parser_t *p, int32_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    uint32_t value = 0;
    nmo_status_t result = parser_read_u32(p, &value, "Cannot read int");
    NMO_RETURN_IF_ERROR(result);

    memcpy(out, &value, sizeof(int32_t));

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_float(nmo_chunk_parser_t *p, float *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    uint32_t value = 0;
    nmo_status_t result = parser_read_u32(p, &value, "Cannot read float");
    NMO_RETURN_IF_ERROR(result);

    memcpy(out, &value, sizeof(float));

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_guid(nmo_chunk_parser_t *p, nmo_guid_t *out) {
    return parser_read_guid(p, out, "Cannot read guid");
}

/**
 * @brief Read manager int with GUID
 *
 * Reads [GUID.d1][GUID.d2][value] and advances cursor.
 * Reference: CKStateChunk::ReadManagerInt() (CKStateChunk.cpp:501-506)
 *
 * @param p Parser
 * @param manager Output manager GUID (can be NULL)
 * @return Manager int value, or 0 on error
 */
nmo_status_t nmo_chunk_parser_read_manager_int(nmo_chunk_parser_t *p,
                                               nmo_guid_t *manager,
                                               int32_t *out_value) {
    if (p == NULL || out_value == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    // Need 3 DWORDs for [GUID.d1][GUID.d2][value]
    if (!check_bounds(p, 3)) {
        NMO_PARSER_RETURN_TRUNCATED("Cannot read manager int");
    }

    nmo_guid_t parsed_manager = {0, 0};
    nmo_status_t result = parser_read_guid(p, &parsed_manager, "Cannot read manager int");
    NMO_RETURN_IF_ERROR(result);

    if (manager != NULL) {
        *manager = parsed_manager;
    }

    uint32_t raw_value = 0;
    result = parser_read_u32(p, &raw_value, "Cannot read manager int");
    NMO_RETURN_IF_ERROR(result);

    *out_value = (int32_t) raw_value;
    NMO_RETURN_OK();
}

/**
 * @brief Read manager int sequence value
 *
 * Reads just the value without GUID (used after start_manager_sequence).
 * Reference: CKStateChunk::ReadManagerIntSequence() (CKStateChunk.cpp:553-555)
 *
 * @param p Parser
 * @return Manager int value, or 0 on error
 */
nmo_status_t nmo_chunk_parser_read_manager_int_sequence(nmo_chunk_parser_t *p,
                                                        int32_t *out_value) {
    if (p == NULL || out_value == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (!p->in_manager_sequence || p->manager_sequence_remaining == 0) {
        NMO_PARSER_RETURN_ERROR(NMO_ERR_INVALID_STATE, "No active manager sequence");
    }

    uint32_t raw_value = 0;
    nmo_status_t result = parser_read_u32(p, &raw_value, "Cannot read manager sequence value");
    NMO_RETURN_IF_ERROR(result);

    *out_value = (int32_t) raw_value;
    consume_sequence_slot(&p->manager_sequence_remaining,
                          &p->in_manager_sequence);
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_start_manager_sequence(nmo_chunk_parser_t *p,
                                                     nmo_guid_t *out_manager,
                                                     size_t *out_count) {
    if (p == NULL || p->chunk == NULL || out_count == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    size_t start_pos = p->cursor;

    uint32_t count = 0;
    nmo_status_t result = parser_read_u32_rollback(
        p,
        start_pos,
        &count,
        "Cannot start manager sequence");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    nmo_guid_t guid;
    result = parser_read_guid_rollback(
        p,
        start_pos,
        &guid,
        "Cannot start manager sequence");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    p->current_manager_guid = guid;
    start_sequence(count,
                   &p->manager_sequence_remaining,
                   &p->in_manager_sequence,
                   out_count);

    if (out_manager != NULL) {
        *out_manager = guid;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Read array with little-endian byte order
 *
 * Reads array in format: [totalBytes][elementCount][data padded to DWORDs].
 * Allocates memory from arena and returns element count.
 * Reference: CKStateChunk::ReadArray_LEndian() (CKStateChunk.cpp:508-547)
 *
 * @param p Parser
 * @param array Output pointer to allocated array data (NULL on error)
 * @param arena Arena for allocation
 * @return Element count, or 0 on error/empty array
 */
nmo_status_t nmo_chunk_parser_read_array_lendian(nmo_chunk_parser_t *p,
                                                 void **array,
                                                 size_t *out_count,
                                                 nmo_arena_t *arena) {
    return parser_read_array_lendian_impl(p, array, out_count, arena, 0);
}

nmo_status_t nmo_chunk_parser_read_array_lendian16(nmo_chunk_parser_t *p,
                                                   void **array,
                                                   size_t *out_count,
                                                   nmo_arena_t *arena) {
    return parser_read_array_lendian_impl(p, array, out_count, arena, 1);
}

nmo_status_t nmo_chunk_parser_read_buffer_lendian16(nmo_chunk_parser_t *p,
                                                    size_t bytes,
                                                    void *buffer) {
    nmo_status_t result = nmo_chunk_parser_read_buffer_nosize(p, bytes, buffer);
    NMO_RETURN_IF_ERROR(result);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (bytes > 1 && buffer != NULL) {
        nmo_swap_16bit_words(buffer, bytes / 2);
    }
#endif

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_bytes(nmo_chunk_parser_t *p, void *dest, size_t bytes) {
    if (p == NULL || dest == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    return parser_copy_aligned_bytes(p, dest, bytes, "Cannot read bytes");
}

nmo_status_t nmo_chunk_parser_read_string(nmo_chunk_parser_t *p, char **out, nmo_arena_t *arena) {
    if (p == NULL || out == NULL || arena == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    size_t start_pos = p->cursor;

    // CK2 WriteString stores size = strlen + 1 (includes null terminator)
    // Reference: CKStateChunk::ReadString() (CKStateChunk.cpp:1218-1244)
    uint32_t size;
    nmo_status_t result = parser_read_u32_rollback(
        p,
        start_pos,
        &size,
        "Cannot read string size");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    if (size == 0) {
        // Empty string case
        char *str = (char *) nmo_arena_alloc(arena, 1, 1);
        if (str == NULL) {
            NMO_PARSER_RETURN_NOMEM_ROLLBACK(p, start_pos, "Failed to allocate string");
        }
        str[0] = '\0';
        *out = str;
        NMO_RETURN_OK();
    }

    // Allocate buffer for string (size already includes null terminator)
    char *str = (char *) nmo_arena_alloc(arena, size, 1);
    if (str == NULL) {
        NMO_PARSER_RETURN_NOMEM_ROLLBACK(p, start_pos, "Failed to allocate string");
    }

    // Read string data (includes null terminator from file)
    result = nmo_chunk_parser_read_bytes(p, str, size);
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    // Ensure null termination (in case file data is corrupted)
    str[size - 1] = '\0';
    *out = str;

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_buffer(nmo_chunk_parser_t *p,
                                         void **out,
                                         size_t *size,
                                         nmo_arena_t *arena) {
    if (p == NULL || out == NULL || size == NULL || arena == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    *out = NULL;
    *size = 0;

    size_t start_pos = p->cursor;

    // Read buffer size (4 bytes)
    uint32_t buf_size;
    nmo_status_t result = parser_read_u32_rollback(
        p,
        start_pos,
        &buf_size,
        "Cannot read buffer size");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    *size = buf_size;

    if (buf_size == 0) {
        *out = NULL;
        NMO_RETURN_OK();
    }

    // Allocate buffer
    void *buffer = nmo_arena_alloc(arena, buf_size, 4);
    if (buffer == NULL) {
        NMO_PARSER_RETURN_NOMEM_ROLLBACK(p, start_pos, "Failed to allocate buffer");
    }

    // Read buffer data (DWORD-aligned)
    result = nmo_chunk_parser_read_bytes(p, buffer, buf_size);
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    *out = buffer;
    NMO_RETURN_OK();
}

/**
 * @brief Read buffer without size prefix
 *
 * Reads raw buffer data without a size prefix. Caller provides the size.
 * Matches CKStateChunk::ReadAndFillBuffer_LEndian(int size, void *buffer) behavior.
 * Reference: CKStateChunk.cpp:1186-1190
 *
 * @param p Parser
 * @param bytes Number of bytes to read
 * @param buffer Destination buffer (must be pre-allocated)
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_parser_read_buffer_nosize(nmo_chunk_parser_t *p, size_t bytes, void *buffer) {
    if (p == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser");
    }

    if (bytes == 0) {
        NMO_RETURN_OK();
    }

    if (buffer == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid buffer");
    }

    return parser_copy_aligned_bytes(p, buffer, bytes, "Cannot read buffer");
}

nmo_status_t nmo_chunk_parser_read_buffer_nosize_lendian16(nmo_chunk_parser_t *p,
                                                          size_t value_count,
                                                          void *buffer) {
    if (p == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser");
    }

    if (value_count == 0) {
        NMO_RETURN_OK();
    }

    if (buffer == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid buffer");
    }

    if (!check_bounds(p, value_count)) {
        NMO_PARSER_RETURN_TRUNCATED("Cannot read buffer");
    }

    uint16_t *out = (uint16_t *)buffer;
    for (size_t i = 0; i < value_count; i++) {
        uint32_t word = NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
        out[i] = parser_unpack_lendian16_word(word);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_dword_as_words(nmo_chunk_parser_t *p, uint32_t *out) {
    if (p == NULL || out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (!check_bounds(p, 2)) {
        NMO_PARSER_RETURN_TRUNCATED("Cannot read dword as words");
    }

    uint32_t low_word = NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
    uint32_t high_word = NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
    uint16_t low = parser_unpack_lendian16_word(low_word);
    uint16_t high = parser_unpack_lendian16_word(high_word);

    *out = (uint32_t)low | ((uint32_t)high << 16);
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_dword_array_as_words(nmo_chunk_parser_t *p,
                                                        uint32_t *out,
                                                        size_t count) {
    if (p == NULL || out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (count == 0) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < count; i++) {
        nmo_status_t result = nmo_chunk_parser_read_dword_as_words(p, &out[i]);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

/**
 * @brief Lock read buffer for direct reading
 *
 * Returns a pointer to the chunk's data buffer for direct reading.
 * Matches CKStateChunk::LockReadBuffer behavior.
 * Reference: CKStateChunk.cpp:334-337
 *
 * @param p Parser
 * @return Pointer to read buffer at current position, or NULL on error
 */
const uint32_t *nmo_chunk_parser_lock_read_buffer(nmo_chunk_parser_t *p) {
    if (p == NULL) {
        return NULL;
    }

    // Check if we're still within bounds
    if (p->cursor >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
        return NULL;
    }

    return &NMO_CHUNK_PARSER_DATA(p)[p->cursor];
}

/**
 * @brief Read object ID from chunk
 *
 * Reads an object ID from the chunk. In file context mode, the value would
 * be converted from file index to runtime ID, but without file context we
 * just return the raw value.
 *
 * Handles both VERSION1+ format (single DWORD) and legacy pre-VERSION1 format
 * (4 DWORDs: flag, skip, skip, actual_id).
 *
 * Reference: CKStateChunk::ReadObjectID() (CKStateChunk.cpp:602-628)
 *
 * @param p Parser
 * @param out Output object ID
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_parser_read_object_id(nmo_chunk_parser_t *p, nmo_object_id_t *out) {
    if (p == NULL || out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    size_t start_pos = p->cursor;

    uint32_t raw_id = 0;
    nmo_status_t result = parser_read_u32_rollback(p, start_pos, &raw_id, "Cannot read object id");
    NMO_RETURN_IF_ERROR(result);
    nmo_object_id_t resolved_id = (nmo_object_id_t) raw_id;

    // Handle legacy pre-VERSION1 format (chunk version < 4)
    // Reference: CKStateChunk.cpp:618-627
    if (p->chunk->chunk_version < NMO_CHUNK_VERSION1) {
        if (raw_id != 0) {
            // Legacy format: [flag][skip][skip][actual_id]
            // Need 3 more DWORDs after the flag
            if (!check_bounds(p, 3)) {
                NMO_PARSER_RETURN_TRUNCATED_ROLLBACK(p, start_pos, "Cannot read legacy object id");
            }
            p->cursor += 2;  // Skip 2 DWORDs
            resolved_id = (nmo_object_id_t) NMO_CHUNK_PARSER_DATA(p)[p->cursor++];
        } else {
            resolved_id = 0;
        }
        *out = resolved_id;
        NMO_RETURN_OK();
    }

    // VERSION1+ format: single DWORD, possibly remapped via file context
    if ((p->chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0 &&
        p->file_context != NULL &&
        p->file_context->file_to_runtime != NULL) {
        if (raw_id == NMO_OBJECT_ID_INVALID) {
            resolved_id = 0;
        } else {
            nmo_object_id_t remapped = 0;
            nmo_status_t remap = nmo_id_remap_lookup_id(
                p->file_context->file_to_runtime,
                (nmo_object_id_t) raw_id,
                &remapped);
            resolved_id = (remap == NMO_OK) ? remapped : 0;
        }
    }

    *out = resolved_id;

    consume_sequence_slot(&p->object_sequence_remaining,
                          &p->in_object_sequence);

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_start_object_sequence(nmo_chunk_parser_t *p, size_t *out_count) {
    if (p == NULL || p->chunk == NULL || out_count == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    uint32_t count = 0;
    nmo_status_t result = parser_read_u32(p, &count, "Cannot start object sequence");
    NMO_RETURN_IF_ERROR(result);

    start_sequence(count,
                   &p->object_sequence_remaining,
                   &p->in_object_sequence,
                   out_count);
    NMO_RETURN_OK();
}

/**
 * @brief Read identifier from current position
 *
 * Reads an identifier pair [ID][NextPos] from the chunk.
 * Updates prev_identifier_pos to track position for SeekIdentifier.
 *
 * Reference: CKStateChunk::ReadIdentifier() (CKStateChunk.cpp:225-231)
 */
nmo_status_t nmo_chunk_parser_read_identifier(nmo_chunk_parser_t *p, uint32_t *identifier) {
    if (p == NULL || identifier == NULL || p->chunk == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (!check_bounds(p, 2)) {
        NMO_PARSER_RETURN_TRUNCATED("Cannot read identifier");
    }

    // Save current position as previous identifier position
    p->prev_identifier_pos = p->cursor;

    // Read identifier (first DWORD)
    *identifier = NMO_CHUNK_PARSER_DATA(p)[p->cursor];

    // Advance cursor by 2 (skip [ID][NextPos] pair)
    p->cursor += 2;

    NMO_RETURN_OK();
}

/**
 * @brief Seek to identifier in linked list
 *
 * Follows the identifier linked list starting from prev_identifier_pos+1.
 * Each identifier is stored as [ID][NextPos], forming a chain.
 *
 * This implementation precisely matches CKStateChunk::SeekIdentifier:
 * Phase 1: Search from startPos following the chain until found or hit 0
 * Phase 2: If hit 0, wrap around from position 0 until reaching startPos
 *
 * Reference: CKStateChunk::SeekIdentifier() (CKStateChunk.cpp:234-275)
 *
 * @param p Parser context
 * @param identifier Target identifier to find
 * @return NMO_OK if found, NMO_ERR_TRUNCATED_CHUNK if not found
 */
nmo_status_t nmo_chunk_parser_seek_identifier(nmo_chunk_parser_t *p, uint32_t identifier) {
    if (p == NULL || p->chunk == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser");
    }

    // Check for empty chunk first
    // Reference: if (!m_Data || m_ChunkSize == 0) return FALSE;
    if (NMO_CHUNK_PARSER_DATA_SIZE(p) == 0 || NMO_CHUNK_PARSER_DATA(p) == NULL) {
        NMO_PARSER_RETURN_TRUNCATED("Identifier list is empty");
    }

    // Read the start position from previous identifier's next pointer
    // Reference: int startPos = m_Data[m_ChunkParser->PrevIdentifierPos + 1];
    if (p->prev_identifier_pos + 1 >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
        NMO_PARSER_RETURN_TRUNCATED("Invalid identifier chain start");
    }
    size_t start_pos = NMO_CHUNK_PARSER_DATA(p)[p->prev_identifier_pos + 1];
    if (start_pos >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
        NMO_PARSER_RETURN_TRUNCATED("Identifier chain start out of bounds");
    }
    size_t current_pos = start_pos;
    size_t steps = 0;

    // Phase 1: Search from startPos to the end of the chain
    // Reference: if (currentPos != 0) { ... }
    if (current_pos != 0) {
        // Search following the chain
        while (NMO_CHUNK_PARSER_DATA(p)[current_pos] != identifier) {
            if (current_pos + 1 >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
                NMO_PARSER_RETURN_TRUNCATED("Identifier chain out of bounds");
            }
            current_pos = NMO_CHUNK_PARSER_DATA(p)[current_pos + 1];
            if (current_pos == 0)
                break;
            if (current_pos >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
                NMO_PARSER_RETURN_TRUNCATED("Identifier chain out of bounds");
            }
            if (++steps > NMO_CHUNK_PARSER_DATA_SIZE(p)) {
                NMO_PARSER_RETURN_TRUNCATED("Identifier chain cycle detected");
            }
        }

        // Check if found
        if (current_pos != 0) {
            p->prev_identifier_pos = current_pos;
            p->cursor = current_pos + 2;
            NMO_RETURN_OK();
        }
    }

    // Phase 2: Search from beginning of list until reaching startPos
    // Reference: currentPos = 0; while (m_Data[currentPos] != identifier) { ... }
    current_pos = 0;
    steps = 0;
    while (NMO_CHUNK_PARSER_DATA(p)[current_pos] != identifier) {
        if (current_pos + 1 >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
            NMO_PARSER_RETURN_TRUNCATED("Identifier chain out of bounds");
        }
        current_pos = NMO_CHUNK_PARSER_DATA(p)[current_pos + 1];
        // Cycle detection: back to start means not found
        // Reference: if (currentPos == startPos) return FALSE;
        if (current_pos == start_pos)
                NMO_PARSER_RETURN_TRUNCATED("Identifier not found");
        if (current_pos >= NMO_CHUNK_PARSER_DATA_SIZE(p)) {
            NMO_PARSER_RETURN_TRUNCATED("Identifier chain out of bounds");
        }
        if (++steps > NMO_CHUNK_PARSER_DATA_SIZE(p)) {
            NMO_PARSER_RETURN_TRUNCATED("Identifier chain cycle detected");
        }
    }

    // Found the identifier - update parser state
    p->prev_identifier_pos = current_pos;
    p->cursor = current_pos + 2;

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_seek_identifier_with_size(nmo_chunk_parser_t *p,
                                                        uint32_t identifier,
                                                        size_t *out_size) {
    if (p == NULL || p->chunk == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser");
    }

    // Save current cursor position
    size_t saved_cursor = p->cursor;
    size_t saved_prev_id = p->prev_identifier_pos;

    // Seek to the identifier
    nmo_status_t result = nmo_chunk_parser_seek_identifier(p, identifier);
    if (result != NMO_OK) {
        // Restore position on failure
        p->cursor = saved_cursor;
        p->prev_identifier_pos = saved_prev_id;
        return result;
    }

    // If out_size is requested, calculate size until next identifier
    if (out_size != NULL) {
        // Current position is after [identifier][next_pos]
        // prev_identifier_pos points to the identifier we just found
        size_t start_pos = p->cursor;

        // Check if there's a next identifier in the chain
        if (p->prev_identifier_pos + 1 < NMO_CHUNK_PARSER_DATA_SIZE(p)) {
            uint32_t next_pos = NMO_CHUNK_PARSER_DATA(p)[p->prev_identifier_pos + 1];

            if (next_pos != 0 && next_pos < NMO_CHUNK_PARSER_DATA_SIZE(p)) {
                // Size is from current position to next identifier position
                *out_size = next_pos - start_pos;
            } else {
                // No next identifier, size is from current to end
                *out_size = NMO_CHUNK_PARSER_DATA_SIZE(p) - start_pos;
            }
        } else {
            // At end of chunk
            *out_size = 0;
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Start reading sub-chunk sequence
 *
 * Reads and returns the count of sub-chunks in the sequence.
 * Matches CKStateChunk::StartReadSequence behavior.
 * Reference: CKStateChunk.cpp:883-885
 *
 * @param p Parser
 * @return Number of sub-chunks, or negative error code on failure
 */
nmo_status_t nmo_chunk_parser_start_read_sequence(nmo_chunk_parser_t *p, size_t *out_count) {
    if (p == NULL || out_count == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    uint32_t count = 0;
    nmo_status_t result = parser_read_u32(p, &count, "Cannot start read sequence");
    NMO_RETURN_IF_ERROR(result);

    start_sequence(count,
                   &p->subchunk_sequence_remaining,
                   &p->in_subchunk_sequence,
                   out_count);
    NMO_RETURN_OK();
}

/**
 * @brief Read sub-chunk from parent chunk
 *
 * Reconstructs a sub-chunk from the parent chunk's data buffer.
 * Matches CKStateChunk::ReadSubChunk behavior.
 * Reference: CKStateChunk.cpp:921-1016
 *
 * @param p Parser
 * @param arena Arena for allocations
 * @param out_chunk Output sub-chunk pointer
 * @return NMO_OK on success, error code on failure
 */
nmo_status_t nmo_chunk_parser_read_subchunk(nmo_chunk_parser_t *p,
                                            nmo_arena_t *arena,
                                            nmo_chunk_t **out_chunk) {
    if (p == NULL || arena == NULL || out_chunk == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    if (p->in_subchunk_sequence && p->subchunk_sequence_remaining == 0) {
        NMO_PARSER_RETURN_TRUNCATED("No remaining subchunks");
    }

    size_t start_pos = p->cursor;

    *out_chunk = NULL;

    // Read size (in DWORDs, includes the size field itself - so actual data is size-1)
    uint32_t size_dwords = 0;
    nmo_status_t result = parser_read_u32_rollback(
        p,
        start_pos,
        &size_dwords,
        "Cannot read subchunk size");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    if (size_dwords == 0) {
        // Empty sub-chunk (NULL marker)
        consume_subchunk_slot(p);
        NMO_RETURN_OK();
    }

    // Check if we have enough data for the header
    if (!check_bounds(p, size_dwords)) {
        NMO_PARSER_RETURN_TRUNCATED_ROLLBACK(p, start_pos, "Subchunk header out of bounds");
    }

    uint32_t class_id_raw = 0;
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &class_id_raw,
        "Cannot read subchunk class id");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    nmo_class_id_t class_id = (nmo_class_id_t)class_id_raw;

    // Create sub-chunk
    nmo_chunk_t *sub = nmo_chunk_create(arena);
    if (sub == NULL) {
        NMO_PARSER_RETURN_NOMEM_ROLLBACK(p, start_pos, "Failed to allocate subchunk");
    }

    sub->class_id = class_id;
    sub->chunk_class_id = (uint8_t) (class_id & 0xFFu);

    uint32_t version_info = 0;
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &version_info,
        "Cannot read subchunk version");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    sub->data_version = version_info & 0xFFFFu;
    sub->chunk_version = (version_info >> 16) & 0xFFFFu;
    sub->chunk_options = 0;

    uint32_t chunk_size = 0;
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &chunk_size,
        "Cannot read subchunk size");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    uint32_t has_file = 0;
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &has_file,
        "Cannot read subchunk file flag");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    (void) has_file; // Not used in non-file context

    uint32_t id_count = 0;
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &id_count,
        "Cannot read subchunk id count");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    uint32_t chunk_count = 0;
    result = parser_read_u32_rollback(
        p,
        start_pos,
        &chunk_count,
        "Cannot read subchunk chunk count");
    NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

    uint32_t manager_count = 0;
    {
        const uint32_t header_without_manager_dwords = 6u;
        if (size_dwords < header_without_manager_dwords) {
            NMO_PARSER_RETURN_ERROR_ROLLBACK(
                p,
                start_pos,
                NMO_ERR_INVALID_FORMAT,
                "Subchunk size is too small");
        }

        size_t payload_consumed = (size_t)chunk_size + (size_t)id_count + (size_t)chunk_count;
        size_t payload_capacity = (size_t)size_dwords - (size_t)header_without_manager_dwords;
        if (payload_consumed > payload_capacity) {
            NMO_PARSER_RETURN_ERROR_ROLLBACK(
                p,
                start_pos,
                NMO_ERR_INVALID_FORMAT,
                "Subchunk payload exceeds declared size");
        }

        size_t payload_remaining = payload_capacity - payload_consumed;
        if (payload_remaining > 0) {
            result = parser_read_u32_rollback(
                p,
                start_pos,
                &manager_count,
                "Cannot read subchunk manager count");
            NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);

            if (manager_count != (uint32_t)(payload_remaining - 1u)) {
                NMO_PARSER_RETURN_ERROR_ROLLBACK(
                    p,
                    start_pos,
                    NMO_ERR_INVALID_FORMAT,
                    "Subchunk manager count does not match declared size");
            }
        }
    }

    // Allocate and read data buffer
    if (chunk_size > 0) {
        nmo_status_t result = parser_read_dword_array_rollback(
            p,
            start_pos,
            &sub->data,
            chunk_size,
            "Cannot read subchunk data");
        NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    }

    // Allocate and read IDs buffer
    if (id_count > 0) {
        nmo_status_t result = parser_read_dword_array_rollback(
            p,
            start_pos,
            &sub->ids,
            id_count,
            "Cannot read subchunk ids");
        NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    }

    // Allocate and read chunks buffer (positions)
    if (chunk_count > 0) {
        nmo_status_t result = parser_read_dword_array_rollback(
            p,
            start_pos,
            &sub->chunk_refs,
            chunk_count,
            "Cannot read subchunk refs");
        NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    }

    // Allocate and read managers buffer
    if (manager_count > 0) {
        nmo_status_t result = parser_read_dword_array_rollback(
            p,
            start_pos,
            &sub->managers,
            manager_count,
            "Cannot read subchunk managers");
        NMO_PARSER_RETURN_IF_ERROR_ROLLBACK(result, p, start_pos);
    }

    if (has_file) sub->chunk_options |= NMO_CHUNK_OPTION_FILE;
    if (id_count > 0) sub->chunk_options |= NMO_CHUNK_OPTION_IDS;
    if (chunk_count > 0) sub->chunk_options |= NMO_CHUNK_OPTION_CHN;
    if (manager_count > 0) sub->chunk_options |= NMO_CHUNK_OPTION_MAN;

    *out_chunk = sub;
    consume_subchunk_slot(p);
    NMO_RETURN_OK();
}

// Math type read functions

nmo_status_t nmo_chunk_parser_read_vector2(nmo_chunk_parser_t *p, nmo_vector2_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    float values[2];
    nmo_status_t result = parser_read_float_span(p, values, 2, "Cannot read vector2");
    NMO_RETURN_IF_ERROR(result);

    out->x = values[0];
    out->y = values[1];
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_vector(nmo_chunk_parser_t *p, nmo_vector_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    float values[3];
    nmo_status_t result = parser_read_float_span(p, values, 3, "Cannot read vector");
    NMO_RETURN_IF_ERROR(result);

    out->x = values[0];
    out->y = values[1];
    out->z = values[2];
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_vector4(nmo_chunk_parser_t *p, nmo_vector4_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    float values[4];
    nmo_status_t result = parser_read_float_span(p, values, 4, "Cannot read vector4");
    NMO_RETURN_IF_ERROR(result);

    out->x = values[0];
    out->y = values[1];
    out->z = values[2];
    out->w = values[3];
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_matrix(nmo_chunk_parser_t *p, nmo_matrix_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    return parser_read_float_span(p, &out->m[0][0], 16, "Cannot read matrix");
}

nmo_status_t nmo_chunk_parser_read_quaternion(nmo_chunk_parser_t *p, nmo_quaternion_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    float values[4];
    nmo_status_t result = parser_read_float_span(p, values, 4, "Cannot read quaternion");
    NMO_RETURN_IF_ERROR(result);

    out->x = values[0];
    out->y = values[1];
    out->z = values[2];
    out->w = values[3];
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parser_read_color(nmo_chunk_parser_t *p, nmo_color_t *out) {
    if (out == NULL) {
        NMO_PARSER_RETURN_INVALID_ARGUMENT("Invalid parser or output");
    }

    float values[4];
    nmo_status_t result = parser_read_float_span(p, values, 4, "Cannot read color");
    NMO_RETURN_IF_ERROR(result);

    out->r = values[0];
    out->g = values[1];
    out->b = values[2];
    out->a = values[3];
    NMO_RETURN_OK();
}
