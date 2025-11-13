#include "format/nmo_chunk_parser.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdlib.h>

/* Helper functions for chunk parsing */
static inline size_t dword_align(size_t bytes) {
    return nmo_align_dword(bytes);
}

/**
 * @brief Chunk parser structure
 */
typedef struct nmo_chunk_parser {
    nmo_chunk_t *chunk;         /**< Chunk being parsed */
    size_t cursor;              /**< Current position in DWORDs */
    size_t prev_identifier_pos; /**< Position of previous identifier for linked-list traversal */
    nmo_allocator_t *alloc;     /**< Allocator for parser itself */
} nmo_chunk_parser_t;

// Helper to check if enough data remains
static inline int check_bounds(nmo_chunk_parser_t *p, size_t dwords_needed) {
    if (p == NULL || p->chunk == NULL) {
        return 0;
    }
    return (p->cursor + dwords_needed) <= p->chunk->data_size;
}

nmo_chunk_parser_t *nmo_chunk_parser_create(nmo_chunk_t *chunk) {
    if (chunk == NULL) {
        return NULL;
    }

    // Allocate parser (use malloc for now, could use arena later)
    nmo_chunk_parser_t *p = (nmo_chunk_parser_t *) malloc(sizeof(nmo_chunk_parser_t));
    if (p == NULL) {
        return NULL;
    }

    p->chunk = chunk;
    p->cursor = 0;
    p->prev_identifier_pos = 0;
    p->alloc = NULL;

    return p;
}

void nmo_chunk_parser_destroy(nmo_chunk_parser_t *p) {
    if (p != NULL) {
        free(p);
    }
}

size_t nmo_chunk_parser_tell(nmo_chunk_parser_t *p) {
    if (p == NULL) {
        return 0;
    }
    return p->cursor;
}

int nmo_chunk_parser_seek(nmo_chunk_parser_t *p, size_t pos) {
    if (p == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (pos > p->chunk->data_size) {
        return NMO_ERR_INVALID_OFFSET;
    }

    p->cursor = pos;
    return NMO_OK;
}

int nmo_chunk_parser_skip(nmo_chunk_parser_t *p, size_t dwords) {
    if (p == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (p->cursor + dwords > p->chunk->data_size) {
        return NMO_ERR_INVALID_OFFSET;
    }

    p->cursor += dwords;
    return NMO_OK;
}

size_t nmo_chunk_parser_remaining(nmo_chunk_parser_t *p) {
    if (p == NULL || p->chunk == NULL) {
        return 0;
    }

    if (p->cursor > p->chunk->data_size) {
        return 0;
    }

    return p->chunk->data_size - p->cursor;
}

int nmo_chunk_parser_at_end(nmo_chunk_parser_t *p) {
    return nmo_chunk_parser_remaining(p) == 0;
}

int nmo_chunk_parser_read_byte(nmo_chunk_parser_t *p, uint8_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read byte from current DWORD (little-endian)
    uint32_t dword = p->chunk->data[p->cursor];
    *out = (uint8_t) (dword & 0xFF);
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_word(nmo_chunk_parser_t *p, uint16_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read word from current DWORD (little-endian)
    uint32_t dword = p->chunk->data[p->cursor];
    *out = (uint16_t) (dword & 0xFFFF);
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_dword(nmo_chunk_parser_t *p, uint32_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    *out = p->chunk->data[p->cursor];
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_dword_as_words(nmo_chunk_parser_t *p, uint32_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Read low 16 bits, then high 16 bits, each from a DWORD
    // Matches CKStateChunk::ReadDwordAsWords behavior
    uint16_t low, high;

    int result = nmo_chunk_parser_read_word(p, &low);
    if (result != NMO_OK) {
        return result;
    }

    result = nmo_chunk_parser_read_word(p, &high);
    if (result != NMO_OK) {
        return result;
    }

    *out = ((uint32_t)high << 16) | (uint32_t)low;
    return NMO_OK;
}

int nmo_chunk_parser_read_int(nmo_chunk_parser_t *p, int32_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Reinterpret uint32 as int32
    uint32_t value = p->chunk->data[p->cursor];
    memcpy(out, &value, sizeof(int32_t));
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_float(nmo_chunk_parser_t *p, float *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Reinterpret uint32 as float
    uint32_t value = p->chunk->data[p->cursor];
    memcpy(out, &value, sizeof(float));
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_guid(nmo_chunk_parser_t *p, nmo_guid_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 2)) {
        return NMO_ERR_EOF;
    }

    out->d1 = p->chunk->data[p->cursor];
    out->d2 = p->chunk->data[p->cursor + 1];
    p->cursor += 2;

    return NMO_OK;
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
int32_t nmo_chunk_parser_read_manager_int(nmo_chunk_parser_t *p, nmo_guid_t *manager) {
    if (p == NULL) {
        return 0;
    }

    // Need 3 DWORDs for [GUID.d1][GUID.d2][value]
    if (!check_bounds(p, 3)) {
        return 0;
    }

    // Read GUID if requested
    if (manager != NULL) {
        manager->d1 = p->chunk->data[p->cursor];
        manager->d2 = p->chunk->data[p->cursor + 1];
    }

    // Advance past GUID
    p->cursor += 2;

    // Read and return value
    int32_t value = (int32_t) p->chunk->data[p->cursor++];
    return value;
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
int32_t nmo_chunk_parser_read_manager_int_sequence(nmo_chunk_parser_t *p) {
    if (p == NULL) {
        return 0;
    }

    if (!check_bounds(p, 1)) {
        return 0;
    }

    return (int32_t) p->chunk->data[p->cursor++];
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
int nmo_chunk_parser_read_array_lendian(nmo_chunk_parser_t *p, void **array, nmo_arena_t *arena) {
    if (p == NULL || array == NULL || arena == NULL) {
        if (array != NULL) {
            *array = NULL;
        }
        return 0;
    }

    // Initialize output to NULL
    *array = NULL;

    // Need at least 2 DWORDs for [dataSizeBytes][elementCount]
    if (!check_bounds(p, 2)) {
        return 0;
    }

    // Read array metadata
    int32_t data_size_bytes = (int32_t) p->chunk->data[p->cursor++];
    int32_t element_count = (int32_t) p->chunk->data[p->cursor++];

    // Check for valid parameters
    if (data_size_bytes <= 0 || element_count <= 0) {
        return 0;
    }

    // Calculate needed DWORDs (round up)
    int32_t dword_count = (data_size_bytes + 3) / 4;

    // Bounds check before allocation
    if (!check_bounds(p, (size_t) dword_count)) {
        return 0;
    }

    // Allocate array data
    void *array_data = nmo_arena_alloc(arena, (size_t) data_size_bytes, 1);
    if (array_data == NULL) {
        return 0;
    }

    // Copy data
    memcpy(array_data, &p->chunk->data[p->cursor], (size_t) data_size_bytes);

    // Update parser position
    p->cursor += (size_t) dword_count;

    // Set output
    *array = array_data;
    return element_count;
}

int nmo_chunk_parser_read_array_lendian16(nmo_chunk_parser_t *p, void **array, nmo_arena_t *arena) {
    if (p == NULL || array == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Read array size (in bytes) and element count
    uint32_t size_bytes;
    int result = nmo_chunk_parser_read_dword(p, &size_bytes);
    if (result != NMO_OK) {
        return result;
    }

    uint32_t element_count;
    result = nmo_chunk_parser_read_dword(p, &element_count);
    if (result != NMO_OK) {
        return result;
    }

    // Handle empty array
    if (size_bytes == 0 || element_count == 0) {
        *array = NULL;
        return 0;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = nmo_align_dword(size_bytes);

    if (!check_bounds(p, dwords_needed)) {
        return NMO_ERR_EOF;
    }

    // Allocate buffer
    void *data = nmo_arena_alloc(arena, size_bytes, 4);
    if (data == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy data from chunk
    memcpy(data, &p->chunk->data[p->cursor], size_bytes);
    p->cursor += dwords_needed;

    // Perform 16-bit word swapping
    // Swap every 16-bit word in the entire buffer
    size_t word_count = size_bytes / 2; // Number of 16-bit words
    nmo_swap_16bit_words(data, word_count);

    *array = data;
    return (int)element_count;
}

int nmo_chunk_parser_read_buffer_lendian16(nmo_chunk_parser_t *p, size_t bytes, void *buffer) {
    if (p == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = nmo_align_dword(bytes);

    if (!check_bounds(p, dwords_needed)) {
        return NMO_ERR_EOF;
    }

    // Copy bytes from DWORD buffer
    memcpy(buffer, &p->chunk->data[p->cursor], bytes);
    p->cursor += dwords_needed;

    // Perform 16-bit word swapping
    size_t word_count = bytes / 2; // Number of 16-bit words
    nmo_swap_16bit_words(buffer, word_count);

    return NMO_OK;
}

int nmo_chunk_parser_read_bytes(nmo_chunk_parser_t *p, void *dest, size_t bytes) {
    if (p == NULL || dest == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = nmo_align_dword(bytes);

    if (!check_bounds(p, dwords_needed)) {
        return NMO_ERR_EOF;
    }

    // Copy bytes from DWORD buffer
    memcpy(dest, &p->chunk->data[p->cursor], bytes);
    p->cursor += dwords_needed;

    return NMO_OK;
}

int nmo_chunk_parser_read_string(nmo_chunk_parser_t *p, char **out, nmo_arena_t *arena) {
    if (p == NULL || out == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Read string length (4 bytes)
    uint32_t length;
    int result = nmo_chunk_parser_read_dword(p, &length);
    if (result != NMO_OK) {
        return result;
    }

    // Allocate string buffer (including null terminator)
    char *str = (char *) nmo_arena_alloc(arena, length + 1, 1);
    if (str == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Read string data (DWORD-aligned)
    if (length > 0) {
        result = nmo_chunk_parser_read_bytes(p, str, length);
        if (result != NMO_OK) {
            return result;
        }
    }

    // Ensure null termination
    str[length] = '\0';
    *out = str;

    return NMO_OK;
}

int nmo_chunk_parser_read_buffer(nmo_chunk_parser_t *p, void **out, size_t *size, nmo_arena_t *arena) {
    if (p == NULL || out == NULL || size == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Read buffer size (4 bytes)
    uint32_t buf_size;
    int result = nmo_chunk_parser_read_dword(p, &buf_size);
    if (result != NMO_OK) {
        return result;
    }

    *size = buf_size;

    if (buf_size == 0) {
        *out = NULL;
        return NMO_OK;
    }

    // Allocate buffer
    void *buffer = nmo_arena_alloc(arena, buf_size, 4);
    if (buffer == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Read buffer data (DWORD-aligned)
    result = nmo_chunk_parser_read_bytes(p, buffer, buf_size);
    if (result != NMO_OK) {
        return result;
    }

    *out = buffer;
    return NMO_OK;
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
int nmo_chunk_parser_read_buffer_nosize(nmo_chunk_parser_t *p, size_t bytes, void *buffer) {
    if (p == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0 || buffer == NULL) {
        // Nothing to read if zero size or NULL buffer
        return NMO_OK;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = (bytes + 3) / 4;

    // Check bounds
    if (!check_bounds(p, dwords_needed)) {
        return NMO_ERR_EOF;
    }

    // Copy bytes from DWORD buffer
    memcpy(buffer, &p->chunk->data[p->cursor], bytes);
    p->cursor += dwords_needed;

    return NMO_OK;
}

int nmo_chunk_parser_read_buffer_nosize_lendian16(nmo_chunk_parser_t *p, size_t value_count, void *buffer) {
    if (p == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (value_count == 0 || buffer == NULL) {
        // Nothing to read if zero count or NULL buffer
        return NMO_OK;
    }

    // Each 16-bit value is read as a separate DWORD-aligned word
    // This matches CKStateChunk::ReadAndFillBuffer_LEndian16 behavior
    uint16_t *values = (uint16_t *)buffer;

    for (size_t i = 0; i < value_count; ++i) {
        int result = nmo_chunk_parser_read_word(p, &values[i]);
        if (result != NMO_OK) {
            return result;
        }
    }

    return NMO_OK;
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
    if (p->cursor >= p->chunk->data_size) {
        return NULL;
    }

    return &p->chunk->data[p->cursor];
}

/**
 * @brief Read object ID from chunk
 *
 * Reads an object ID from the chunk. In file context mode, the value would
 * be converted from file index to runtime ID, but without file context we
 * just return the raw value.
 * Reference: CKStateChunk::ReadObjectID() (CKStateChunk.cpp:605-633)
 *
 * @param p Parser
 * @param out Output object ID
 * @return NMO_OK on success, error code on failure
 */
int nmo_chunk_parser_read_object_id(nmo_chunk_parser_t *p, nmo_object_id_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check bounds
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read the object ID (or file index in file context mode)
    uint32_t id = p->chunk->data[p->cursor++];

    // In file context mode, we would call file->LoadFindObject(id) here
    // to convert file index to runtime ID. For now, just return the raw value.
    *out = id;

    return NMO_OK;
}

/**
 * @brief Read identifier from current position
 *
 * Reads an identifier pair [ID][NextPos] from the chunk.
 * Updates prev_identifier_pos to track position for SeekIdentifier.
 *
 * Reference: CKStateChunk::ReadIdentifier() (CKStateChunk.cpp:225-231)
 */
int nmo_chunk_parser_read_identifier(nmo_chunk_parser_t *p, uint32_t *identifier) {
    if (p == NULL || identifier == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check bounds (need 2 DWORDs for [ID][NextPos])
    if (p->cursor + 2 > p->chunk->data_size) {
        return NMO_ERR_EOF;
    }

    // Save current position as previous identifier position
    p->prev_identifier_pos = p->cursor;

    // Read identifier (first DWORD)
    *identifier = p->chunk->data[p->cursor];

    // Advance cursor by 2 (skip [ID][NextPos] pair)
    p->cursor += 2;

    return NMO_OK;
}

/**
 * @brief Seek to identifier in linked list
 *
 * Follows the identifier linked list starting from prev_identifier_pos+1.
 * Each identifier is stored as [ID][NextPos], forming a chain.
 *
 * This implementation precisely matches CKStateChunk::SeekIdentifier:
 * - Reads next pointer from prev_identifier_pos + 1
 * - If next != 0, searches from that position
 * - If next == 0, searches from position 0
 * - Includes cycle detection logic
 *
 * Reference: CKStateChunk::SeekIdentifier() (CKStateChunk.cpp:234-284)
 *
 * @param p Parser context
 * @param identifier Target identifier to find
 * @return NMO_OK if found, NMO_ERR_EOF if not found
 */
int nmo_chunk_parser_seek_identifier(nmo_chunk_parser_t *p, uint32_t identifier) {
    if (p == NULL || p->chunk == NULL || p->chunk->data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (p->chunk->data_size == 0) {
        return NMO_ERR_EOF;
    }

    // Check if prev_identifier_pos is out of bounds
    // Reference: if (m_ChunkParser->PrevIdentifierPos >= m_ChunkSize - 1) return FALSE;
    if (p->prev_identifier_pos >= p->chunk->data_size - 1) {
        return NMO_ERR_EOF;
    }

    // Read the 'next' pointer from previous identifier position
    // Reference: int j = m_Data[m_ChunkParser->PrevIdentifierPos + 1];
    uint32_t j = p->chunk->data[p->prev_identifier_pos + 1];
    size_t i;

    // Reference implementation has two main branches based on j
    if (j != 0) {
        // Start from the next pointer position
        // Reference: i = j;
        i = j;

        // First search loop
        // Reference: while (i < m_ChunkSize && m_Data[i] != identifier)
        while (i < p->chunk->data_size && p->chunk->data[i] != identifier) {
            if (i + 1 >= p->chunk->data_size)
                return NMO_ERR_EOF;

            // Move to next in chain
            // Reference: i = m_Data[i + 1];
            i = p->chunk->data[i + 1];

            // If we hit 0, do a second search from position 0
            // Reference: if (i == 0) { ... }
            if (i == 0) {
                while (i < p->chunk->data_size && p->chunk->data[i] != identifier) {
                    if (i + 1 >= p->chunk->data_size)
                        return NMO_ERR_EOF;

                    i = p->chunk->data[i + 1];

                    // Cycle detection: if we're back to j, we've looped
                    // Reference: if (i == j) return FALSE;
                    if (i == j)
                        return NMO_ERR_EOF;
                }
            }
        }
    } else {
        // Next pointer is 0, start from position 0
        // Reference: i = 0;
        i = 0;

        // Search from beginning
        // Reference: while (i < m_ChunkSize && m_Data[i] != identifier)
        while (i < p->chunk->data_size && p->chunk->data[i] != identifier) {
            if (i + 1 >= p->chunk->data_size)
                return NMO_ERR_EOF;

            i = p->chunk->data[i + 1];

            // Cycle detection: if we're back to j (which is 0), we've looped
            // Reference: if (i == j) return FALSE;
            if (i == j)
                return NMO_ERR_EOF;
        }
    }

    // Final bounds check
    // Reference: if (i >= m_ChunkSize) return FALSE;
    if (i >= p->chunk->data_size)
        return NMO_ERR_EOF;

    // Found the identifier - update parser state
    // Reference:
    //   m_ChunkParser->PrevIdentifierPos = i;
    //   m_ChunkParser->CurrentPos = i + 2;
    p->prev_identifier_pos = i;
    p->cursor = i + 2;  // Position after [identifier][next_pos]

    return NMO_OK;
}

int nmo_chunk_parser_seek_identifier_with_size(nmo_chunk_parser_t *p, uint32_t identifier, size_t *out_size) {
    if (p == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Save current cursor position
    size_t saved_cursor = p->cursor;
    size_t saved_prev_id = p->prev_identifier_pos;

    // Seek to the identifier
    int result = nmo_chunk_parser_seek_identifier(p, identifier);
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
        if (p->prev_identifier_pos + 1 < p->chunk->data_size) {
            uint32_t next_pos = p->chunk->data[p->prev_identifier_pos + 1];

            if (next_pos != 0 && next_pos < p->chunk->data_size) {
                // Size is from current position to next identifier position
                *out_size = next_pos - start_pos;
            } else {
                // No next identifier, size is from current to end
                *out_size = p->chunk->data_size - start_pos;
            }
        } else {
            // At end of chunk
            *out_size = 0;
        }
    }

    return NMO_OK;
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
int nmo_chunk_parser_start_read_sequence(nmo_chunk_parser_t *p) {
    if (p == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check bounds
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read the count
    uint32_t count = p->chunk->data[p->cursor++];
    return (int) count;
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
int nmo_chunk_parser_read_subchunk(nmo_chunk_parser_t *p, nmo_arena_t *arena, nmo_chunk_t **out_chunk) {
    if (p == NULL || arena == NULL || out_chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_chunk = NULL;

    // Check if we have enough data
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read size (in DWORDs, includes the size field itself - so actual data is size-1)
    uint32_t size_dwords = p->chunk->data[p->cursor++];

    if (size_dwords == 0) {
        // Empty sub-chunk (NULL marker)
        return NMO_OK;
    }

    // Check if we have enough data for the header
    if (!check_bounds(p, size_dwords)) {
        return NMO_ERR_EOF;
    }

    // Read class ID
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }
    nmo_class_id_t class_id = p->chunk->data[p->cursor++];

    // Create sub-chunk
    nmo_chunk_t *sub = nmo_chunk_create(arena);
    if (sub == NULL) {
        return NMO_ERR_NOMEM;
    }

    sub->class_id = class_id;

    // Read version info
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }
    uint32_t version_info = p->chunk->data[p->cursor++];
    sub->data_version = (version_info) & 0xFF;
    sub->chunk_class_id = (version_info >> 8) & 0xFF;
    sub->chunk_version = (version_info >> 16) & 0xFF;
    sub->chunk_options = (version_info >> 24) & 0xFF;

    // Read chunk size
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }
    uint32_t chunk_size = p->chunk->data[p->cursor++];

    // Read hasFile flag
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }
    uint32_t has_file = p->chunk->data[p->cursor++];
    (void) has_file; // Not used in non-file context

    // Read ID count
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }
    uint32_t id_count = p->chunk->data[p->cursor++];

    // Read chunk count
    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }
    uint32_t chunk_count = p->chunk->data[p->cursor++];

    // Read manager count (if version > 4, meaning literal 4, not CHUNK_VERSION_4)
    // Note: CHUNK_VERSION_4 = 7, but the check is against the literal value 4
    uint32_t manager_count = 0;
    if (sub->chunk_version > 4) {
        if (!check_bounds(p, 1)) {
            return NMO_ERR_EOF;
        }
        manager_count = p->chunk->data[p->cursor++];
    }

    // Allocate and read data buffer
    if (chunk_size > 0) {
        if (!check_bounds(p, chunk_size)) {
            return NMO_ERR_EOF;
        }

        sub->data = (uint32_t *) nmo_arena_alloc(arena, chunk_size * sizeof(uint32_t), sizeof(uint32_t));
        if (sub->data == NULL) {
            return NMO_ERR_NOMEM;
        }

        memcpy(sub->data, &p->chunk->data[p->cursor], chunk_size * sizeof(uint32_t));
        sub->data_size = chunk_size;
        sub->data_capacity = chunk_size;
        p->cursor += chunk_size;
    }

    // Allocate and read IDs buffer
    if (id_count > 0) {
        if (!check_bounds(p, id_count)) {
            return NMO_ERR_EOF;
        }

        sub->ids = (uint32_t *) nmo_arena_alloc(arena, id_count * sizeof(uint32_t), sizeof(uint32_t));
        if (sub->ids == NULL) {
            return NMO_ERR_NOMEM;
        }

        memcpy(sub->ids, &p->chunk->data[p->cursor], id_count * sizeof(uint32_t));
        sub->id_count = id_count;
        sub->id_capacity = id_count;
        p->cursor += id_count;
    }

    // Allocate and read chunks buffer (positions)
    if (chunk_count > 0) {
        if (!check_bounds(p, chunk_count)) {
            return NMO_ERR_EOF;
        }

        // For now, we just skip the chunk positions
        // In a full implementation, we would track these
        p->cursor += chunk_count;
    }

    // Allocate and read managers buffer
    if (manager_count > 0) {
        if (!check_bounds(p, manager_count)) {
            return NMO_ERR_EOF;
        }

        sub->managers = (uint32_t *) nmo_arena_alloc(arena, manager_count * sizeof(uint32_t), sizeof(uint32_t));
        if (sub->managers == NULL) {
            return NMO_ERR_NOMEM;
        }

        memcpy(sub->managers, &p->chunk->data[p->cursor], manager_count * sizeof(uint32_t));
        sub->manager_count = manager_count;
        sub->manager_capacity = manager_count;
        p->cursor += manager_count;
    }

    *out_chunk = sub;
    return NMO_OK;
}

// Math type read functions

int nmo_chunk_parser_read_vector2(nmo_chunk_parser_t *p, nmo_vector2_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 2)) {
        return NMO_ERR_EOF;
    }

    // Read 2 floats
    float *data = (float *) &p->chunk->data[p->cursor];
    out->x = data[0];
    out->y = data[1];
    p->cursor += 2;

    return NMO_OK;
}

int nmo_chunk_parser_read_vector(nmo_chunk_parser_t *p, nmo_vector_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 3)) {
        return NMO_ERR_EOF;
    }

    // Read 3 floats
    float *data = (float *) &p->chunk->data[p->cursor];
    out->x = data[0];
    out->y = data[1];
    out->z = data[2];
    p->cursor += 3;

    return NMO_OK;
}

int nmo_chunk_parser_read_vector4(nmo_chunk_parser_t *p, nmo_vector4_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 4)) {
        return NMO_ERR_EOF;
    }

    // Read 4 floats
    float *data = (float *) &p->chunk->data[p->cursor];
    out->x = data[0];
    out->y = data[1];
    out->z = data[2];
    out->w = data[3];
    p->cursor += 4;

    return NMO_OK;
}

int nmo_chunk_parser_read_matrix(nmo_chunk_parser_t *p, nmo_matrix_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 16)) {
        return NMO_ERR_EOF;
    }

    // Read 16 floats (4x4 matrix)
    memcpy(out->m, &p->chunk->data[p->cursor], 16 * sizeof(float));
    p->cursor += 16;

    return NMO_OK;
}

int nmo_chunk_parser_read_quaternion(nmo_chunk_parser_t *p, nmo_quaternion_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 4)) {
        return NMO_ERR_EOF;
    }

    // Read 4 floats (quaternion)
    float *data = (float *) &p->chunk->data[p->cursor];
    out->x = data[0];
    out->y = data[1];
    out->z = data[2];
    out->w = data[3];
    p->cursor += 4;

    return NMO_OK;
}

int nmo_chunk_parser_read_color(nmo_chunk_parser_t *p, nmo_color_t *out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 4)) {
        return NMO_ERR_EOF;
    }

    // Read 4 floats (RGBA color)
    float *data = (float *) &p->chunk->data[p->cursor];
    out->r = data[0];
    out->g = data[1];
    out->b = data[2];
    out->a = data[3];
    p->cursor += 4;

    return NMO_OK;
}
