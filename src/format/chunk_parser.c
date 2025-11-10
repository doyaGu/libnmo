#include "format/nmo_chunk_parser.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Chunk parser structure
 */
struct nmo_chunk_parser {
    nmo_chunk* chunk;      /**< Chunk being parsed */
    size_t cursor;         /**< Current position in DWORDs */
    nmo_allocator* alloc;  /**< Allocator for parser itself */
};

// Helper to calculate DWORD padding
static inline size_t dword_align(size_t bytes) {
    return (bytes + 3) / 4;  // Round up to DWORDs
}

// Helper to check if enough data remains
static inline int check_bounds(nmo_chunk_parser* p, size_t dwords_needed) {
    if (p == NULL || p->chunk == NULL) {
        return 0;
    }
    return (p->cursor + dwords_needed) <= p->chunk->data_size;
}

nmo_chunk_parser* nmo_chunk_parser_create(nmo_chunk* chunk) {
    if (chunk == NULL) {
        return NULL;
    }

    // Allocate parser (use malloc for now, could use arena later)
    nmo_chunk_parser* p = (nmo_chunk_parser*)malloc(sizeof(nmo_chunk_parser));
    if (p == NULL) {
        return NULL;
    }

    p->chunk = chunk;
    p->cursor = 0;
    p->alloc = NULL;

    return p;
}

void nmo_chunk_parser_destroy(nmo_chunk_parser* p) {
    if (p != NULL) {
        free(p);
    }
}

size_t nmo_chunk_parser_tell(nmo_chunk_parser* p) {
    if (p == NULL) {
        return 0;
    }
    return p->cursor;
}

int nmo_chunk_parser_seek(nmo_chunk_parser* p, size_t pos) {
    if (p == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (pos > p->chunk->data_size) {
        return NMO_ERR_INVALID_OFFSET;
    }

    p->cursor = pos;
    return NMO_OK;
}

int nmo_chunk_parser_skip(nmo_chunk_parser* p, size_t dwords) {
    if (p == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (p->cursor + dwords > p->chunk->data_size) {
        return NMO_ERR_INVALID_OFFSET;
    }

    p->cursor += dwords;
    return NMO_OK;
}

size_t nmo_chunk_parser_remaining(nmo_chunk_parser* p) {
    if (p == NULL || p->chunk == NULL) {
        return 0;
    }

    if (p->cursor > p->chunk->data_size) {
        return 0;
    }

    return p->chunk->data_size - p->cursor;
}

int nmo_chunk_parser_at_end(nmo_chunk_parser* p) {
    return nmo_chunk_parser_remaining(p) == 0;
}

int nmo_chunk_parser_read_byte(nmo_chunk_parser* p, uint8_t* out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read byte from current DWORD (little-endian)
    uint32_t dword = p->chunk->data[p->cursor];
    *out = (uint8_t)(dword & 0xFF);
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_word(nmo_chunk_parser* p, uint16_t* out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!check_bounds(p, 1)) {
        return NMO_ERR_EOF;
    }

    // Read word from current DWORD (little-endian)
    uint32_t dword = p->chunk->data[p->cursor];
    *out = (uint16_t)(dword & 0xFFFF);
    p->cursor++;

    return NMO_OK;
}

int nmo_chunk_parser_read_dword(nmo_chunk_parser* p, uint32_t* out) {
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

int nmo_chunk_parser_read_int(nmo_chunk_parser* p, int32_t* out) {
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

int nmo_chunk_parser_read_float(nmo_chunk_parser* p, float* out) {
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

int nmo_chunk_parser_read_guid(nmo_chunk_parser* p, nmo_guid* out) {
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

int nmo_chunk_parser_read_bytes(nmo_chunk_parser* p, void* dest, size_t bytes) {
    if (p == NULL || dest == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = dword_align(bytes);

    if (!check_bounds(p, dwords_needed)) {
        return NMO_ERR_EOF;
    }

    // Copy bytes from DWORD buffer
    memcpy(dest, &p->chunk->data[p->cursor], bytes);
    p->cursor += dwords_needed;

    return NMO_OK;
}

int nmo_chunk_parser_read_string(nmo_chunk_parser* p, char** out, nmo_arena* arena) {
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
    char* str = (char*)nmo_arena_alloc(arena, length + 1, 1);
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

int nmo_chunk_parser_read_buffer(nmo_chunk_parser* p, void** out, size_t* size, nmo_arena* arena) {
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
    void* buffer = nmo_arena_alloc(arena, buf_size, 4);
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

int nmo_chunk_parser_read_object_id(nmo_chunk_parser* p, nmo_object_id* out) {
    if (p == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Read the object ID
    uint32_t id;
    int result = nmo_chunk_parser_read_dword(p, &id);
    if (result != NMO_OK) {
        return result;
    }

    *out = id;

    // Track ID in chunk's ID list if not already present
    if (p->chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        // Check if ID is already in the list
        int found = 0;
        for (size_t i = 0; i < p->chunk->id_count; i++) {
            if (p->chunk->ids[i] == id) {
                found = 1;
                break;
            }
        }

        // Add to list if not found
        if (!found && p->chunk->arena != NULL) {
            // Grow ID list if needed
            size_t new_count = p->chunk->id_count + 1;
            uint32_t* new_ids = (uint32_t*)nmo_arena_alloc(p->chunk->arena,
                                                           new_count * sizeof(uint32_t),
                                                           sizeof(uint32_t));
            if (new_ids == NULL) {
                return NMO_ERR_NOMEM;
            }

            // Copy existing IDs
            if (p->chunk->ids != NULL) {
                memcpy(new_ids, p->chunk->ids, p->chunk->id_count * sizeof(uint32_t));
            }

            // Add new ID
            new_ids[p->chunk->id_count] = id;
            p->chunk->ids = new_ids;
            p->chunk->id_count = new_count;
        }
    }

    return NMO_OK;
}

int nmo_chunk_parser_seek_identifier(nmo_chunk_parser* p, uint32_t identifier) {
    if (p == NULL || p->chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Scan forward for identifier
    while (p->cursor < p->chunk->data_size) {
        if (p->chunk->data[p->cursor] == identifier) {
            // Found it, position cursor after identifier
            p->cursor++;
            return NMO_OK;
        }
        p->cursor++;
    }

    // Not found
    return NMO_ERR_EOF;
}

int nmo_chunk_parser_read_identifier(nmo_chunk_parser* p, uint32_t* identifier) {
    if (p == NULL || identifier == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Just read as DWORD
    return nmo_chunk_parser_read_dword(p, identifier);
}
