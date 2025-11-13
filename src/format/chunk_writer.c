#include "format/nmo_chunk_writer.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdlib.h>

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

    nmo_chunk_t **chunk_list;
    size_t chunk_count;
    size_t chunk_capacity;

    // Sub-chunk context
    nmo_subchunk_context_t *subchunk_ctx;

    // Identifier linked-list tracking
    size_t prev_identifier_pos; // Position of previous identifier for linked-list chaining

    // State
    int finalized;
} nmo_chunk_writer_t;

// Helper to ensure capacity
static int ensure_data_capacity(nmo_chunk_writer_t *w, size_t needed_dwords) {
    if (w->data_size + needed_dwords <= w->data_capacity) {
        return NMO_OK;
    }

    // Grow by WRITER_GROWTH_INCREMENT
    size_t new_capacity = w->data_capacity + WRITER_GROWTH_INCREMENT;
    while (new_capacity < w->data_size + needed_dwords) {
        new_capacity += WRITER_GROWTH_INCREMENT;
    }

    uint32_t *new_data = (uint32_t *) nmo_arena_alloc(w->arena,
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
    // Grow list if needed
    if (w->id_count >= w->id_capacity) {
        size_t new_capacity = w->id_capacity == 0 ? 16 : w->id_capacity * 2;
        uint32_t *new_list = (uint32_t *) nmo_arena_alloc(w->arena,
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

    // Track the current position (not the ID itself!)
    w->id_list[w->id_count++] = (uint32_t) w->data_size;
    return NMO_OK;
}

// Helper to add manager to tracking list
static int track_manager(nmo_chunk_writer_t *w, uint32_t manager) __attribute__((unused));

static int track_manager(nmo_chunk_writer_t *w, uint32_t manager) {
    // Grow list if needed
    if (w->manager_count >= w->manager_capacity) {
        size_t new_capacity = w->manager_capacity == 0 ? 16 : w->manager_capacity * 2;
        uint32_t *new_list = (uint32_t *) nmo_arena_alloc(w->arena,
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

/**
 * @brief Track position for sub-chunk (for sequence tracking)
 *
 * Adds the current write position to the chunks list. This list will be used
 * during serialization to track where sub-chunk sequences begin.
 * Matches CKStateChunk behavior where m_Chunks->AddEntries(CurrentPos - 1) is called.
 *
 * @param w Writer context
 * @param position Position to track
 * @return NMO_OK on success, error code on failure
 */
static int track_chunk_position(nmo_chunk_writer_t *w, uint32_t position) {
    // For now, we just track the position. The actual chunk list will be built
    // during finalization from the chunk_list array.
    // This matches CKStateChunk's m_Chunks list which tracks positions.

    // Note: We're not actually using this yet because we're building chunks
    // in the chunk_list array. This is here for API completeness.
    (void) position; // Unused for now
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

    // Allocate initial buffer
    w->data = (uint32_t *) nmo_arena_alloc(arena,
                                           WRITER_INITIAL_CAPACITY * sizeof(uint32_t),
                                           sizeof(uint32_t));
    if (w->data == NULL) {
        return NULL;
    }

    return w;
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
    w->chunk->chunk_class_id = 0; // Will be set during serialization
    w->chunk->data_version = 0;

    // Reset state
    w->data_size = 0;
    w->id_count = 0;
    w->manager_count = 0;
    w->chunk_count = 0;
    w->prev_identifier_pos = 0;
    w->finalized = 0;
}

int nmo_chunk_writer_write_byte(nmo_chunk_writer_t *w, uint8_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Write byte in low 8 bits of DWORD
    w->data[w->data_size++] = (uint32_t) value;
    return NMO_OK;
}

int nmo_chunk_writer_write_word(nmo_chunk_writer_t *w, uint16_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Write word in low 16 bits of DWORD
    w->data[w->data_size++] = (uint32_t) value;
    return NMO_OK;
}

int nmo_chunk_writer_write_dword(nmo_chunk_writer_t *w, uint32_t value) {
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

int nmo_chunk_writer_write_dword_as_words(nmo_chunk_writer_t *w, uint32_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Write low 16 bits, then high 16 bits, each padded to DWORD
    // Matches CKStateChunk::WriteDwordAsWords behavior
    uint16_t low = (uint16_t)(value & 0xFFFF);
    uint16_t high = (uint16_t)((value >> 16) & 0xFFFF);

    int result = nmo_chunk_writer_write_word(w, low);
    if (result != NMO_OK) {
        return result;
    }

    return nmo_chunk_writer_write_word(w, high);
}

int nmo_chunk_writer_write_int(nmo_chunk_writer_t *w, int32_t value) {
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

int nmo_chunk_writer_write_float(nmo_chunk_writer_t *w, float value) {
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

int nmo_chunk_writer_write_guid(nmo_chunk_writer_t *w, nmo_guid_t guid) {
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

int nmo_chunk_writer_write_bytes(nmo_chunk_writer_t *w, const void *data, size_t bytes) {
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
    size_t dwords_needed = nmo_align_dword(bytes) / 4;

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
int nmo_chunk_writer_write_buffer_nosize(nmo_chunk_writer_t *w, size_t bytes, const void *data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0 || data == NULL) {
        // Nothing to write if NULL or zero size
        return NMO_OK;
    }

    // Calculate DWORDs needed (with padding)
    size_t dwords_needed = nmo_align_dword(bytes) / 4;

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

int nmo_chunk_writer_write_buffer_nosize_lendian16(nmo_chunk_writer_t *w, size_t value_count, const void *data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (value_count == 0 || data == NULL) {
        // Nothing to write if NULL or zero count
        return NMO_OK;
    }

    // Each 16-bit value is written as a separate DWORD-aligned word
    // This matches CKStateChunk::WriteBufferNoSize_LEndian16 behavior
    const uint16_t *values = (const uint16_t *)data;

    for (size_t i = 0; i < value_count; ++i) {
        int result = nmo_chunk_writer_write_word(w, values[i]);
        if (result != NMO_OK) {
            return result;
        }
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

int nmo_chunk_writer_write_string(nmo_chunk_writer_t *w, const char *str) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t length = str != NULL ? (uint32_t) strlen(str) : 0;

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

int nmo_chunk_writer_write_buffer(nmo_chunk_writer_t *w, const void *data, size_t size) {
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
int nmo_chunk_writer_write_object_id(nmo_chunk_writer_t *w, nmo_object_id_t id) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Ensure capacity for the ID (1 DWORD)
    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // If ID is non-zero and we don't have file context, track the position
    // (In file context mode, IDs would be converted to indices by the file)
    if (id != 0) {
        result = track_id_position(w);
        if (result != NMO_OK) {
            return result;
        }
    }

    // Write the ID
    w->data[w->data_size++] = id;
    return NMO_OK;
}

int nmo_chunk_writer_start_object_sequence(nmo_chunk_writer_t *w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Set the IDS option flag
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }

    (void) count; // Count is informational, not stored
    return NMO_OK;
}

int nmo_chunk_writer_start_manager_sequence(nmo_chunk_writer_t *w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Set the MAN option flag
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
    }

    (void) count; // Count is informational, not stored
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
int nmo_chunk_writer_start_subchunk_sequence(nmo_chunk_writer_t *w, size_t count) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Set the CHN option flag
    if (w->chunk != NULL) {
        w->chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
    }

    // Ensure capacity for the count (1 DWORD)
    int result = ensure_data_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Track the position (CurrentPos - 1 in CKStateChunk, but we track before writing)
    result = track_chunk_position(w, (uint32_t) w->data_size);
    if (result != NMO_OK) {
        return result;
    }

    // Write the count
    w->data[w->data_size++] = (uint32_t) count;
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
int nmo_chunk_writer_write_subchunk(nmo_chunk_writer_t *w, const nmo_chunk_t *sub) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Add sub-chunk to chunk list for tracking
    if (sub != NULL) {
        // Grow chunk list if needed
        if (w->chunk_count >= w->chunk_capacity) {
            size_t new_capacity = w->chunk_capacity == 0 ? 16 : w->chunk_capacity * 2;
            nmo_chunk_t **new_list = (nmo_chunk_t **) nmo_arena_alloc(w->arena,
                                                                      new_capacity * sizeof(nmo_chunk_t *),
                                                                      sizeof(void *));
            if (new_list == NULL) {
                return NMO_ERR_NOMEM;
            }

            // Copy existing entries
            if (w->chunk_count > 0 && w->chunk_list != NULL) {
                memcpy(new_list, w->chunk_list, w->chunk_count * sizeof(nmo_chunk_t *));
            }

            w->chunk_list = new_list;
            w->chunk_capacity = new_capacity;
        }

        // Add sub-chunk to list
        w->chunk_list[w->chunk_count++] = (nmo_chunk_t *)sub;
    }

    // Calculate total size needed
    size_t size_dwords = 1; // For the size field itself
    if (sub != NULL) {
        size_dwords = 8;
        // Header fields (size, classID, version, chunkSize, hasFile, idCount, chunkCount, managerCount)
        if (sub->data_size != 0) {
            size_dwords += sub->data_size;
        }
        if (sub->id_count > 0) {
            size_dwords += sub->id_count;
        }
        if (sub->chunk_count > 0) {
            // For sub-chunks, we need to calculate the size recursively
            // For now, we'll just reserve space for the chunk positions
            size_dwords += sub->chunk_count;
        }
        if (sub->manager_count > 0) {
            size_dwords += sub->manager_count;
        }
    }

    // Ensure capacity
    int result = ensure_data_capacity(w, size_dwords);
    if (result != NMO_OK) {
        return result;
    }

    // Write size (in DWORDs, minus 1)
    w->data[w->data_size++] = (uint32_t) (size_dwords - 1);

    if (sub != NULL) {
        // Write class ID
        w->data[w->data_size++] = sub->class_id;

        // Write combined version info (DataVersion | ChunkClassID | ChunkVersion | ChunkOptions)
        uint32_t version_info = ((uint8_t)sub->data_version) |
                                (((uint8_t)sub->chunk_class_id) << 8) |
                                (((uint8_t)sub->chunk_version) << 16) |
                                (((uint8_t)sub->chunk_options) << 24);
        w->data[w->data_size++] = version_info;

        // Write chunk size
        w->data[w->data_size++] = (uint32_t) sub->data_size;

        // Write hasFile flag (0 for non-file context)
        w->data[w->data_size++] = 0;

        // Write ID count
        w->data[w->data_size++] = (uint32_t) sub->id_count;

        // Write chunk count
        w->data[w->data_size++] = (uint32_t) sub->chunk_count;

        // Write manager count
        w->data[w->data_size++] = (uint32_t) sub->manager_count;

        // Write data buffer
        if (sub->data_size != 0 && sub->data != NULL) {
            memcpy(&w->data[w->data_size], sub->data, sub->data_size * sizeof(uint32_t));
            w->data_size += sub->data_size;
        }

        // Write IDs buffer
        if (sub->id_count > 0 && sub->ids != NULL) {
            memcpy(&w->data[w->data_size], sub->ids, sub->id_count * sizeof(uint32_t));
            w->data_size += sub->id_count;
        }

        // Write chunks buffer (positions)
        if (sub->chunk_count > 0 && sub->chunks != NULL) {
            // For now, we write placeholder positions
            // In a full implementation, this would write the chunk positions
            for (size_t i = 0; i < sub->chunk_count; i++) {
                w->data[w->data_size++] = 0; // Placeholder
            }
        }

        // Write managers buffer
        if (sub->manager_count > 0 && sub->managers != NULL) {
            memcpy(&w->data[w->data_size], sub->managers, sub->manager_count * sizeof(uint32_t));
            w->data_size += sub->manager_count;
        }
    }

    return NMO_OK;
}

/**
 * @brief Ensure manager list capacity
 *
 * Grows the manager list if needed to accommodate more entries.
 *
 * @param w Writer context
 * @param needed Number of entries needed
 * @return NMO_OK on success, error code on failure
 */
static int ensure_manager_capacity(nmo_chunk_writer_t *w, size_t needed) {
    if (w->manager_count + needed <= w->manager_capacity) {
        return NMO_OK;
    }

    // Grow capacity (similar to ID list growth)
    size_t new_capacity = w->manager_capacity == 0 ? 16 : w->manager_capacity * 2;
    while (new_capacity < w->manager_count + needed) {
        new_capacity *= 2;
    }

    uint32_t *new_list = (uint32_t *) nmo_arena_alloc(w->arena,
                                                      new_capacity * sizeof(uint32_t),
                                                      sizeof(uint32_t));
    if (new_list == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy existing entries
    if (w->manager_count > 0 && w->manager_list != NULL) {
        memcpy(new_list, w->manager_list, w->manager_count * sizeof(uint32_t));
    }

    w->manager_list = new_list;
    w->manager_capacity = new_capacity;
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
int nmo_chunk_writer_write_manager_int(nmo_chunk_writer_t *w, nmo_guid_t manager, int32_t value) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Ensure capacity for [GUID.d1][GUID.d2][value] (3 DWORDs)
    int result = ensure_data_capacity(w, 3);
    if (result != NMO_OK) {
        return result;
    }

    // Ensure manager list can track this position
    result = ensure_manager_capacity(w, 1);
    if (result != NMO_OK) {
        return result;
    }

    // Track current position in manager list
    w->manager_list[w->manager_count++] = w->data_size;

    // Write [GUID.d1][GUID.d2][value]
    w->data[w->data_size++] = manager.d1;
    w->data[w->data_size++] = manager.d2;
    w->data[w->data_size++] = (uint32_t) value;

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
int nmo_chunk_writer_write_array_lendian(nmo_chunk_writer_t *w, int element_count, int element_size,
                                         const void *src_data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Write empty array marker if no data
    if (src_data == NULL || element_count <= 0 || element_size <= 0) {
        int result = ensure_data_capacity(w, 2);
        if (result != NMO_OK) {
            return result;
        }
        w->data[w->data_size++] = 0;
        w->data[w->data_size++] = 0;
        return NMO_OK;
    }

    // Check for integer overflow in total bytes calculation
    if (element_count > INT_MAX / element_size) {
        // Write empty array marker on overflow
        int result = ensure_data_capacity(w, 2);
        if (result != NMO_OK) {
            return result;
        }
        w->data[w->data_size++] = 0;
        w->data[w->data_size++] = 0;
        return NMO_OK;
    }

    // Calculate total bytes
    size_t total_bytes = (size_t) element_size * (size_t) element_count;
    if (total_bytes > (size_t) INT_MAX) {
        // Write empty array marker on overflow
        int result = ensure_data_capacity(w, 2);
        if (result != NMO_OK) {
            return result;
        }
        w->data[w->data_size++] = 0;
        w->data[w->data_size++] = 0;
        return NMO_OK;
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
    memcpy(&w->data[w->data_size], src_data, total_bytes);
    w->data_size += dword_count;

    return NMO_OK;
}

int nmo_chunk_writer_write_array_lendian16(nmo_chunk_writer_t *w, int element_count, int element_size,
                                           const void *src_data) {
    if (w == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Handle empty array
    if (src_data == NULL || element_count <= 0 || element_size <= 0) {
        // Write empty array marker
        int result = ensure_data_capacity(w, 2);
        if (result != NMO_OK) {
            return result;
        }
        w->data[w->data_size++] = 0;
        w->data[w->data_size++] = 0;
        return NMO_OK;
    }

    // Calculate total bytes and DWORDs needed
    size_t total_bytes = (size_t) element_count * (size_t) element_size;
    size_t dword_count = nmo_align_dword(total_bytes) / 4;

    // Ensure capacity (size + count + data)
    int result = ensure_data_capacity(w, 2 + dword_count);
    if (result != NMO_OK) {
        return result;
    }

    // Write array header
    w->data[w->data_size++] = (uint32_t) total_bytes;
    w->data[w->data_size++] = (uint32_t) element_count;

    // Allocate temporary buffer for byte swapping
    void *temp_buffer = malloc(total_bytes);
    if (temp_buffer == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy data to temporary buffer
    memcpy(temp_buffer, src_data, total_bytes);

    // Perform 16-bit word swapping
    size_t word_count = total_bytes / 2;
    nmo_swap_16bit_words(temp_buffer, word_count);

    // Copy swapped data to chunk
    memcpy(&w->data[w->data_size], temp_buffer, total_bytes);
    w->data_size += dword_count;

    free(temp_buffer);
    return NMO_OK;
}

int nmo_chunk_writer_write_buffer_lendian16(nmo_chunk_writer_t *w, size_t bytes, const void *data) {
    if (w == NULL || w->finalized || data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (bytes == 0) {
        return NMO_OK;
    }

    // Calculate DWORDs needed (round up)
    size_t dword_count = nmo_align_dword(bytes) / 4;

    // Ensure capacity
    int result = ensure_data_capacity(w, dword_count);
    if (result != NMO_OK) {
        return result;
    }

    // Allocate temporary buffer for byte swapping
    void *temp_buffer = malloc(bytes);
    if (temp_buffer == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy data to temporary buffer
    memcpy(temp_buffer, data, bytes);

    // Perform 16-bit word swapping
    size_t word_count = bytes / 2;
    nmo_swap_16bit_words(temp_buffer, word_count);

    // Clear the destination area (for padding)
    memset(&w->data[w->data_size], 0, dword_count * sizeof(uint32_t));

    // Copy swapped data to chunk
    memcpy(&w->data[w->data_size], temp_buffer, bytes);
    w->data_size += dword_count;

    free(temp_buffer);
    return NMO_OK;
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
int nmo_chunk_writer_write_identifier(nmo_chunk_writer_t *w, uint32_t identifier) {
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

nmo_chunk_t *nmo_chunk_writer_finalize(nmo_chunk_writer_t *w) {
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

void nmo_chunk_writer_destroy(nmo_chunk_writer_t *w) {
    // Writer is allocated from arena, so no explicit free needed
    // Arena will clean up everything when destroyed
    (void) w;
}

// Math type write functions

int nmo_chunk_writer_write_vector2(nmo_chunk_writer_t *w, const nmo_vector2_t *v) {
    if (w == NULL || v == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 2);
    if (result != NMO_OK) {
        return result;
    }

    // Write 2 floats
    float *data = (float *) &w->data[w->data_size];
    data[0] = v->x;
    data[1] = v->y;
    w->data_size += 2;

    return NMO_OK;
}

int nmo_chunk_writer_write_vector(nmo_chunk_writer_t *w, const nmo_vector_t *v) {
    if (w == NULL || v == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 3);
    if (result != NMO_OK) {
        return result;
    }

    // Write 3 floats
    float *data = (float *) &w->data[w->data_size];
    data[0] = v->x;
    data[1] = v->y;
    data[2] = v->z;
    w->data_size += 3;

    return NMO_OK;
}

int nmo_chunk_writer_write_vector4(nmo_chunk_writer_t *w, const nmo_vector4_t *v) {
    if (w == NULL || v == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 4);
    if (result != NMO_OK) {
        return result;
    }

    // Write 4 floats
    float *data = (float *) &w->data[w->data_size];
    data[0] = v->x;
    data[1] = v->y;
    data[2] = v->z;
    data[3] = v->w;
    w->data_size += 4;

    return NMO_OK;
}

int nmo_chunk_writer_write_matrix(nmo_chunk_writer_t *w, const nmo_matrix_t *m) {
    if (w == NULL || m == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 16);
    if (result != NMO_OK) {
        return result;
    }

    // Write 16 floats (4x4 matrix)
    memcpy(&w->data[w->data_size], m->m, 16 * sizeof(float));
    w->data_size += 16;

    return NMO_OK;
}

int nmo_chunk_writer_write_quaternion(nmo_chunk_writer_t *w, const nmo_quaternion_t *q) {
    if (w == NULL || q == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 4);
    if (result != NMO_OK) {
        return result;
    }

    // Write 4 floats (quaternion)
    float *data = (float *) &w->data[w->data_size];
    data[0] = q->x;
    data[1] = q->y;
    data[2] = q->z;
    data[3] = q->w;
    w->data_size += 4;

    return NMO_OK;
}

int nmo_chunk_writer_write_color(nmo_chunk_writer_t *w, const nmo_color_t *c) {
    if (w == NULL || c == NULL || w->finalized) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = ensure_data_capacity(w, 4);
    if (result != NMO_OK) {
        return result;
    }

    // Write 4 floats (RGBA color)
    float *data = (float *) &w->data[w->data_size];
    data[0] = c->r;
    data[1] = c->g;
    data[2] = c->b;
    data[3] = c->a;
    w->data_size += 4;

    return NMO_OK;
}
