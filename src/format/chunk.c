/**
 * @file chunk.c
 * @brief CKStateChunk handling implementation
 */

#include "format/nmo_chunk.h"
#include <string.h>

/* Helper macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ALIGN_4(x) (((x) + 3) & ~3)

/**
 * @brief Write helper for serialization
 */
typedef struct {
    uint8_t* buffer;
    size_t   pos;
    size_t   size;
} nmo_write_ctx;

/**
 * @brief Read helper for deserialization
 */
typedef struct {
    const uint8_t* buffer;
    size_t         pos;
    size_t         size;
} nmo_read_ctx;

/**
 * @brief Write bytes to buffer
 */
static nmo_result write_bytes(nmo_write_ctx* ctx, const void* data, size_t len) {
    if (ctx->pos + len > ctx->size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR,
                                          "Write buffer overrun"));
    }
    memcpy(ctx->buffer + ctx->pos, data, len);
    ctx->pos += len;
    return nmo_result_ok();
}

/**
 * @brief Write uint32_t value
 */
static nmo_result write_u32(nmo_write_ctx* ctx, uint32_t value) {
    return write_bytes(ctx, &value, sizeof(uint32_t));
}

/**
 * @brief Read bytes from buffer
 */
static nmo_result read_bytes(nmo_read_ctx* ctx, void* data, size_t len) {
    if (ctx->pos + len > ctx->size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR,
                                          "Read buffer overrun"));
    }
    memcpy(data, ctx->buffer + ctx->pos, len);
    ctx->pos += len;
    return nmo_result_ok();
}

/**
 * @brief Read uint32_t value
 */
static nmo_result read_u32(nmo_read_ctx* ctx, uint32_t* value) {
    return read_bytes(ctx, value, sizeof(uint32_t));
}

/**
 * @brief Calculate serialized size of a chunk
 */
static size_t chunk_calc_size(const nmo_chunk* chunk) {
    size_t size = 0;

    /* Version info + chunk size */
    size += 4 + 4;

    /* Data buffer */
    size += chunk->data_size * 4;  /* data_size is in DWORDs */

    /* Optional IDs list */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        size += 4;  /* count */
        size += chunk->id_count * 4;
    }

    /* Optional sub-chunks */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        size += 4;  /* count */
        for (size_t i = 0; i < chunk->chunk_count; i++) {
            size += chunk_calc_size(chunk->chunks[i]);
        }
    }

    /* Optional managers list */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) {
        size += 4;  /* count */
        size += chunk->manager_count * 4;
    }

    return size;
}

/**
 * @brief Serialize chunk recursively
 */
static nmo_result chunk_serialize_internal(const nmo_chunk* chunk, nmo_write_ctx* ctx) {
    nmo_result result;

    /* Pack version info:
     * versionInfo = (dataVersion | (chunkClassID << 8)) |
     *               ((chunkVersion | (chunkOptions << 8)) << 16)
     */
    uint32_t version_info = 0;
    version_info |= (chunk->data_version & 0xFF);
    version_info |= ((chunk->chunk_class_id & 0xFF) << 8);
    version_info |= ((chunk->chunk_version & 0xFF) << 16);
    version_info |= ((chunk->chunk_options & 0xFF) << 24);

    /* Write version info */
    result = write_u32(ctx, version_info);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Write chunk size in DWORDs */
    result = write_u32(ctx, (uint32_t)chunk->data_size);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Write data buffer */
    if (chunk->data_size > 0) {
        result = write_bytes(ctx, chunk->data, chunk->data_size * 4);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Write IDs list if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        result = write_u32(ctx, (uint32_t)chunk->id_count);
        if (result.code != NMO_OK) {
            return result;
        }
        if (chunk->id_count > 0) {
            result = write_bytes(ctx, chunk->ids, chunk->id_count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    /* Write sub-chunks if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        result = write_u32(ctx, (uint32_t)chunk->chunk_count);
        if (result.code != NMO_OK) {
            return result;
        }
        for (size_t i = 0; i < chunk->chunk_count; i++) {
            result = chunk_serialize_internal(chunk->chunks[i], ctx);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    /* Write managers list if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) {
        result = write_u32(ctx, (uint32_t)chunk->manager_count);
        if (result.code != NMO_OK) {
            return result;
        }
        if (chunk->manager_count > 0) {
            result = write_bytes(ctx, chunk->managers, chunk->manager_count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Deserialize chunk recursively
 */
static nmo_result chunk_deserialize_internal(nmo_read_ctx* ctx, nmo_arena* arena, nmo_chunk** out_chunk) {
    nmo_result result;

    /* Create chunk */
    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate chunk"));
    }

    /* Read version info */
    uint32_t version_info = 0;
    result = read_u32(ctx, &version_info);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Unpack version info */
    chunk->data_version = version_info & 0xFF;
    chunk->chunk_class_id = (version_info >> 8) & 0xFF;
    chunk->chunk_version = (version_info >> 16) & 0xFF;
    chunk->chunk_options = (version_info >> 24) & 0xFF;

    /* Read chunk size in DWORDs */
    uint32_t chunk_size_dwords = 0;
    result = read_u32(ctx, &chunk_size_dwords);
    if (result.code != NMO_OK) {
        return result;
    }

    chunk->data_size = chunk_size_dwords;
    chunk->data_capacity = chunk_size_dwords;

    /* Read data buffer */
    if (chunk_size_dwords > 0) {
        chunk->data = nmo_arena_alloc(arena, chunk_size_dwords * 4, 4);
        if (!chunk->data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to allocate chunk data"));
        }
        result = read_bytes(ctx, chunk->data, chunk_size_dwords * 4);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Read IDs list if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        uint32_t id_count = 0;
        result = read_u32(ctx, &id_count);
        if (result.code != NMO_OK) {
            return result;
        }

        chunk->id_count = id_count;
        chunk->id_capacity = id_count;

        if (id_count > 0) {
            chunk->ids = nmo_arena_alloc(arena, id_count * 4, 4);
            if (!chunk->ids) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate IDs"));
            }
            result = read_bytes(ctx, chunk->ids, id_count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    /* Read sub-chunks if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        uint32_t chunk_count = 0;
        result = read_u32(ctx, &chunk_count);
        if (result.code != NMO_OK) {
            return result;
        }

        chunk->chunk_count = chunk_count;
        chunk->chunk_capacity = chunk_count;

        if (chunk_count > 0) {
            chunk->chunks = nmo_arena_alloc(arena, chunk_count * sizeof(nmo_chunk*), sizeof(void*));
            if (!chunk->chunks) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate sub-chunks"));
            }

            for (size_t i = 0; i < chunk_count; i++) {
                result = chunk_deserialize_internal(ctx, arena, &chunk->chunks[i]);
                if (result.code != NMO_OK) {
                    return result;
                }
            }
        }
    }

    /* Read managers list if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) {
        uint32_t manager_count = 0;
        result = read_u32(ctx, &manager_count);
        if (result.code != NMO_OK) {
            return result;
        }

        chunk->manager_count = manager_count;
        chunk->manager_capacity = manager_count;

        if (manager_count > 0) {
            chunk->managers = nmo_arena_alloc(arena, manager_count * 4, 4);
            if (!chunk->managers) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate managers"));
            }
            result = read_bytes(ctx, chunk->managers, manager_count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    *out_chunk = chunk;
    return nmo_result_ok();
}

/**
 * Create empty chunk
 */
nmo_chunk* nmo_chunk_create(nmo_arena* arena) {
    if (!arena) {
        return NULL;
    }

    nmo_chunk* chunk = nmo_arena_alloc(arena, sizeof(nmo_chunk), sizeof(void*));
    if (!chunk) {
        return NULL;
    }

    /* Initialize all fields to 0/NULL */
    memset(chunk, 0, sizeof(nmo_chunk));

    /* Set defaults */
    chunk->chunk_version = NMO_CHUNK_VERSION_4;
    chunk->owns_data = 1;
    chunk->arena = arena;

    return chunk;
}

/**
 * Serialize chunk to binary format
 */
nmo_result nmo_chunk_serialize(const nmo_chunk* chunk,
                                void** out_data,
                                size_t* out_size,
                                nmo_arena* arena) {
    /* Validate arguments */
    if (!chunk || !out_data || !out_size || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to chunk_serialize"));
    }

    /* Calculate total size */
    size_t total_size = chunk_calc_size(chunk);

    /* Allocate output buffer */
    uint8_t* buffer = nmo_arena_alloc(arena, total_size, 4);
    if (!buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate output buffer"));
    }

    /* Setup write context */
    nmo_write_ctx ctx = {
        .buffer = buffer,
        .pos = 0,
        .size = total_size
    };

    /* Serialize chunk */
    nmo_result result = chunk_serialize_internal(chunk, &ctx);
    if (result.code != NMO_OK) {
        return result;
    }

    *out_data = buffer;
    *out_size = total_size;

    return nmo_result_ok();
}

/**
 * Deserialize chunk from binary format
 */
nmo_result nmo_chunk_deserialize(const void* data,
                                  size_t size,
                                  nmo_arena* arena,
                                  nmo_chunk** out_chunk) {
    /* Validate arguments */
    if (!data || !arena || !out_chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to chunk_deserialize"));
    }

    if (size < 8) {  /* Minimum size: version info + chunk size */
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR,
                                          "Buffer too small for chunk"));
    }

    /* Setup read context */
    nmo_read_ctx ctx = {
        .buffer = (const uint8_t*)data,
        .pos = 0,
        .size = size
    };

    /* Deserialize chunk */
    return chunk_deserialize_internal(&ctx, arena, out_chunk);
}

/**
 * Destroy chunk
 */
void nmo_chunk_destroy(nmo_chunk* chunk) {
    /* Since we use arena allocation, this is mostly a no-op.
     * The arena itself will handle cleanup when destroyed.
     * We just mark it as unused for safety.
     */
    (void)chunk;
}

/* Legacy API implementation */

/**
 * Create chunk (legacy)
 */
nmo_chunk_t* nmo_chunk_create_legacy(uint32_t chunk_id) {
    (void)chunk_id;
    /* Legacy API requires arena but none provided.
     * This is a stub for compatibility.
     */
    return NULL;
}

/**
 * Destroy chunk (legacy)
 */
void nmo_chunk_destroy_legacy(nmo_chunk_t* chunk) {
    (void)chunk;
}

/**
 * Parse chunk from data
 */
nmo_result_t nmo_chunk_parse(nmo_chunk_t* chunk, const void* data, size_t size) {
    (void)chunk;
    (void)data;
    (void)size;
    return nmo_result_ok();
}

/**
 * Write chunk to data
 */
ssize_t nmo_chunk_write(const nmo_chunk_t* chunk, void* buffer, size_t size) {
    (void)chunk;
    (void)buffer;
    (void)size;
    return -1;
}

/**
 * Get chunk header
 */
nmo_result_t nmo_chunk_get_header(const nmo_chunk_t* chunk, nmo_chunk_header_t* out_header) {
    if (!chunk || !out_header) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments"));
    }

    out_header->chunk_id = chunk->class_id;
    out_header->chunk_size = (uint32_t)(chunk->data_size * 4);  /* Convert to bytes */
    out_header->sub_chunk_count = (uint32_t)chunk->chunk_count;
    out_header->flags = chunk->chunk_options;

    return nmo_result_ok();
}

/**
 * Get chunk ID
 */
uint32_t nmo_chunk_get_id(const nmo_chunk_t* chunk) {
    if (!chunk) {
        return 0;
    }
    return chunk->class_id;
}

/**
 * Get chunk size
 */
uint32_t nmo_chunk_get_size(const nmo_chunk_t* chunk) {
    if (!chunk) {
        return 0;
    }
    return (uint32_t)(chunk->data_size * 4);  /* Convert to bytes */
}

/**
 * Get chunk data
 */
const void* nmo_chunk_get_data(const nmo_chunk_t* chunk, size_t* out_size) {
    if (!chunk) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }

    if (out_size) {
        *out_size = chunk->data_size * 4;  /* Convert to bytes */
    }
    return chunk->data;
}

/**
 * Add sub-chunk
 */
nmo_result_t nmo_chunk_add_sub_chunk(nmo_chunk_t* chunk, nmo_chunk_t* sub_chunk) {
    if (!chunk || !sub_chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments"));
    }

    /* Check if we need to grow the array */
    if (chunk->chunk_count >= chunk->chunk_capacity) {
        size_t new_capacity = chunk->chunk_capacity == 0 ? 4 : chunk->chunk_capacity * 2;
        nmo_chunk** new_chunks = nmo_arena_alloc(chunk->arena,
                                                  new_capacity * sizeof(nmo_chunk*),
                                                  sizeof(void*));
        if (!new_chunks) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to grow sub-chunk array"));
        }

        /* Copy existing chunks */
        if (chunk->chunks) {
            memcpy(new_chunks, chunk->chunks, chunk->chunk_count * sizeof(nmo_chunk*));
        }

        chunk->chunks = new_chunks;
        chunk->chunk_capacity = new_capacity;
    }

    /* Add sub-chunk */
    chunk->chunks[chunk->chunk_count++] = sub_chunk;

    /* Set CHN flag */
    chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;

    return nmo_result_ok();
}

/**
 * Get sub-chunk count
 */
uint32_t nmo_chunk_get_sub_chunk_count(const nmo_chunk_t* chunk) {
    if (!chunk) {
        return 0;
    }
    return (uint32_t)chunk->chunk_count;
}

/**
 * Get sub-chunk by index
 */
nmo_chunk_t* nmo_chunk_get_sub_chunk(const nmo_chunk_t* chunk, uint32_t index) {
    if (!chunk || index >= chunk->chunk_count) {
        return NULL;
    }
    return chunk->chunks[index];
}
