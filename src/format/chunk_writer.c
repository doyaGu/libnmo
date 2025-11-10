#include "format/nmo_chunk_writer.h"
#include <string.h>
#include <stdlib.h>

#define WRITER_INITIAL_CAPACITY 100  // DWORDs
#define WRITER_GROWTH_INCREMENT 500  // DWORDs (as per spec)

/**
 * @brief Sub-chunk context for nested chunks
 */
typedef struct nmo_subchunk_context {
    size_t start_pos;              // Starting position in parent
    nmo_chunk_writer* writer;      // Sub-chunk writer
    struct nmo_subchunk_context* parent;  // Parent context
} nmo_subchunk_context;

/**
 * @brief Chunk writer structure
 */
struct nmo_chunk_writer {
    // Chunk being built
    nmo_chunk* chunk;

    // Arena allocator
    nmo_arena* arena;

    // Data buffer (managed separately from chunk until finalized)
    uint32_t* data;
    size_t data_size;      // In DWORDs
    size_t data_capacity;  // In DWORDs

    // Tracking lists (built dynamically)
    uint32_t* id_list;
    size_t id_count;
    size_t id_capacity;

    uint32_t* manager_list;
    size_t manager_count;
    size_t manager_capacity;

    nmo_chunk** chunk_list;
    size_t chunk_count;
    size_t chunk_capacity;

    // Sub-chunk context
    nmo_subchunk_context* subchunk_ctx;

    // State
    int finalized;
};

// Helper to calculate DWORD padding
static inline size_t dword_align(size_t bytes) {
    return (bytes + 3) / 4;  // Round up to DWORDs
}

// Helper to ensure capacity
static int ensure_data_capacity(nmo_chunk_writer* w, size_t needed_dwords) {
    if (w->data_size + needed_dwords <= w->data_capacity) {
        return NMO_OK;
    }

    // Grow by WRITER_GROWTH_INCREMENT
    size_t new_capacity = w->data_capacity + WRITER_GROWTH_INCREMENT;
    while (new_capacity < w->data_size + needed_dwords) {
        new_capacity += WRITER_GROWTH_INCREMENT;
    }

    uint32_t* new_data = (uint32_t*)nmo_arena_alloc(w->arena,
                                                     new_capacity * sizeof(uint32_t),
                                                     sizeof(uint32_t));
    if (new_data == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy existing data
    if (w->data != NULL && w->data_size > 0) {
        memcpy(new_data, w->data, w->data_size * sizeof(uint32_t));
    }

    w->data = new_data;
    w->data_capacity = new_capacity;

    return NMO_OK;
}

// Helper to add ID to tracking list
static int track_id(nmo_chunk_writer* w, nmo_object_id id) {
    // Check if already tracked
    for (size_t i = 0; i < w->id_count; i++) {
        if (w->id_list[i] == id) {
            return NMO_OK;
        }
    }

    // Grow list if needed
    if (w->id_count >= w->id_capacity) {
        size_t new_capacity = w->id_capacity == 0 ? 16 : w->id_capacity * 2;
        uint32_t* new_list = (uint32_t*)nmo_arena_alloc(w->arena,
                                                        new_capacity * sizeof(uint32_t),
                                                        sizeof(uint32_t));
        if (new_list == NULL) {
            return NMO_ERR_NOMEM;
        }

        if (w->id_list != NULL && w->id_count > 0) {
            memcpy(new_list, w->id_list, w->id_count * sizeof(uint32_t));
        }

        w->id_list = new_list;
        w->id_capacity = new_capacity;
    }

    w->id_list[w->id_count++] = id;
    return NMO_OK;
}

// Helper to add manager to tracking list
static int track_manager(nmo_chunk_writer* w, uint32_t manager) __attribute__((unused));
static int track_manager(nmo_chunk_writer* w, uint32_t manager) {
    // Grow list if needed
    if (w->manager_count >= w->manager_capacity) {
        size_t new_capacity = w->manager_capacity == 0 ? 16 : w->manager_capacity * 2;
        uint32_t* new_list = (uint32_t*)nmo_arena_alloc(w->arena,
                                                        new_capacity * sizeof(uint32_t),
                                                        sizeof(uint32_t));
        if (new_list == NULL) {
            return NMO_ERR_NOMEM;
        }

        if (w->manager_list != NULL && w->manager_count > 0) {
            memcpy(new_list, w->manager_list, w->manager_count * sizeof(uint32_t));
        }

        w->manager_list = new_list;
        w->manager_capacity = new_capacity;
    }

    w->manager_list[w->manager_count++] = manager;
    return NMO_OK;
}

nmo_chunk_writer* nmo_chunk_writer_create(nmo_arena* arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_chunk_writer* w = (nmo_chunk_writer*)nmo_arena_alloc(arena,
                                                              sizeof(nmo_chunk_writer),
                                                              sizeof(void*));
    if (w == NULL) {
        return NULL;
    }

    memset(w, 0, sizeof(nmo_chunk_writer));
    w->arena = arena;
    w->data_capacity = WRITER_INITIAL_CAPACITY;

    // Allocate initial buffer
    w->data = (uint32_t*)nmo_arena_alloc(arena,
                                         WRITER_INITIAL_CAPACITY * sizeof(uint32_t),
                                         sizeof(uint32_t));
    if (w->data == NULL) {
        return NULL;
    }

    return w;
}

void nmo_chunk_writer_start(nmo_chunk_writer* w, nmo_class_id class_id, uint32_t chunk_version) {
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
    w->chunk->chunk_class_id = 0;  // Will be set during serialization
    w->chunk->data_version = 0;

    // Reset state
    w->data_size = 0;
    w->id_count = 0;
    w->manager_count = 0;
    w->chunk_count = 0;
    w->finalized = 0;
}

int nmo_chunk_writer_write_byte(nmo_chunk_writer* w, uint8_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Write byte in low 8 bits of DWORD
    w->data[w->data_size++] = (uint32_t)value;
    return NMO_OK;
}

int nmo_chunk_writer_write_word(nmo_chunk_writer* w, uint16_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Write word in low 16 bits of DWORD
    w->data[w->data_size++] = (uint32_t)value;
    return NMO_OK;
}

int nmo_chunk_writer_write_dword(nmo_chunk_writer* w, uint32_t value) {
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

int nmo_chunk_writer_write_int(nmo_chunk_writer* w, int32_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Reinterpret int32 as uint32
    uint32_t uvalue;
    memcpy(&uvalue, &value, sizeof(uint32_t));
    w->data[w->data_size++] = uvalue;
    return NMO_OK;
}

int nmo_chunk_writer_write_float(nmo_chunk_writer* w, float value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Reinterpret float as uint32
    uint32_t uvalue;
    memcpy(&uvalue, &value, sizeof(uint32_t));
    w->data[w->data_size++] = uvalue;
    return NMO_OK;
}

int nmo_chunk_writer_write_guid(nmo_chunk_writer* w, nmo_guid guid) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 2);
    if (result != NMO_OK) {
        return result;
    }

    w->data[w->data_size++] = guid.d1;
    w->data[w->data_size++] = guid.d2;
    return NMO_OK;
}

int nmo_chunk_writer_write_bytes(nmo_chunk_writer* w, const void* data, size_t bytes) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    if (data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = dword_align(bytes);

    int result = ensure_data_capacity(w, dwords_needed);
    if (result != NMO_OK) {
        return result;
    }

    // Zero out the padding area first
    memset(&w->data[w->data_size], 0, dwords_needed * sizeof(uint32_t));

    // Copy data
    memcpy(&w->data[w->data_size], data, bytes);
    w->data_size += dwords_needed;

    return NMO_OK;
}

int nmo_chunk_writer_write_string(nmo_chunk_writer* w, const char* str) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t length = str != NULL ? (uint32_t)strlen(str) : 0;

    // Write length
    int result = nmo_chunk_writer_write_dword(w, length);
    if (result != NMO_OK) {
        return result;
    }

    // Write string data (if not empty)
    if (length > 0) {
        result = nmo_chunk_writer_write_bytes(w, str, length);
        if (result != NMO_OK) {
            return result;
        }
    }

    return NMO_OK;
}

int nmo_chunk_writer_write_buffer(nmo_chunk_writer* w, const void* data, size_t size) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Write size
    int result = nmo_chunk_writer_write_dword(w, (uint32_t)size);
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

int nmo_chunk_writer_write_object_id(nmo_chunk_writer* w, nmo_object_id id) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Track the ID
    int result = track_id(w, id);
    if (result != NMO_OK) {
        return result;
    }

    // Write the ID
    return nmo_chunk_writer_write_dword(w, id);
}

int nmo_chunk_writer_start_object_sequence(nmo_chunk_writer* w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Set the IDS option flag
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }

    (void)count;  // Count is informational, not stored
    return NMO_OK;
}

int nmo_chunk_writer_start_manager_sequence(nmo_chunk_writer* w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Set the MAN option flag
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
    }

    (void)count;  // Count is informational, not stored
    return NMO_OK;
}

int nmo_chunk_writer_start_sub_chunk(nmo_chunk_writer* w) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Set the CHN option flag
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    }

    // Create sub-chunk context (not fully implemented - would need nested writer)
    // This is a placeholder for now
    return NMO_OK;
}

int nmo_chunk_writer_end_sub_chunk(nmo_chunk_writer* w) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Finalize sub-chunk (placeholder)
    return NMO_OK;
}

int nmo_chunk_writer_write_identifier(nmo_chunk_writer* w, uint32_t identifier) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Write identifier as a DWORD
    return nmo_chunk_writer_write_dword(w, identifier);
}

nmo_chunk* nmo_chunk_writer_finalize(nmo_chunk_writer* w) {
    if (w == NULL || w->finalized || w->chunk == NULL) {
        return NULL;
    }

    // Copy data to chunk
    w->chunk->data = w->data;
    w->chunk->data_size = w->data_size;
    w->chunk->data_capacity = w->data_capacity;

    // Copy ID list
    if (w->id_count > 0) {
        w->chunk->ids = w->id_list;
        w->chunk->id_count = w->id_count;
    }

    // Copy manager list
    if (w->manager_count > 0) {
        w->chunk->managers = w->manager_list;
        w->chunk->manager_count = w->manager_count;
    }

    // Copy chunk list
    if (w->chunk_count > 0) {
        w->chunk->chunks = w->chunk_list;
        w->chunk->chunk_count = w->chunk_count;
    }

    w->finalized = 1;
    return w->chunk;
}

void nmo_chunk_writer_destroy(nmo_chunk_writer* w) {
    // Writer is allocated from arena, so no explicit free needed
    // Arena will clean up everything when destroyed
    (void)w;
}
