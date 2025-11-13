/**
 * @file chunk.c
 * @brief CKStateChunk handling implementation
 */

#include "format/nmo_chunk.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_utils.h"
#include <string.h>

/* Helper macros */
#define ALIGN_4(x) nmo_align_dword(x)

/**
 * @brief Write helper for serialization
 */
typedef struct nmo_write_ctx {
    uint8_t *buffer;
    size_t pos;
    size_t size;
} nmo_write_ctx_t;

/**
 * @brief Read helper for deserialization
 */
typedef struct nmo_read_ctx {
    const uint8_t *buffer;
    size_t pos;
    size_t size;
} nmo_read_ctx_t;

/**
 * @brief Write bytes to buffer
 */
static nmo_result_t write_bytes(nmo_write_ctx_t *ctx, const void *data, size_t len) {
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
static nmo_result_t write_u32(nmo_write_ctx_t *ctx, uint32_t value) {
    return write_bytes(ctx, &value, sizeof(uint32_t));
}

/**
 * @brief Read bytes from buffer
 */
static nmo_result_t read_bytes(nmo_read_ctx_t *ctx, void *data, size_t len) {
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
static nmo_result_t read_u32(nmo_read_ctx_t *ctx, uint32_t *value) {
    return read_bytes(ctx, value, sizeof(uint32_t));
}

/**
 * @brief Calculate serialized size of a chunk
 */
static size_t chunk_calc_size(const nmo_chunk_t *chunk) {
    size_t size = 0;

    /* Version info + chunk size */
    size += 4 + 4;

    /* Data buffer */
    size += chunk->data_size * 4; /* data_size is in DWORDs */

    /* Optional IDs list */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        size += 4; /* count */
        size += chunk->id_count * 4;
    }

    /* Optional sub-chunks */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        size += 4; /* count */
        for (size_t i = 0; i < chunk->chunk_count; i++) {
            size += chunk_calc_size(chunk->chunks[i]);
        }
    }

    /* Optional managers list */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) {
        size += 4; /* count */
        size += chunk->manager_count * 4;
    }

    return size;
}

/**
 * @brief Serialize chunk recursively
 */
static nmo_result_t chunk_serialize_internal(const nmo_chunk_t *chunk, nmo_write_ctx_t *ctx) {
    nmo_result_t result;

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
    result = write_u32(ctx, (uint32_t) chunk->data_size);
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
        result = write_u32(ctx, (uint32_t) chunk->id_count);
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
        result = write_u32(ctx, (uint32_t) chunk->chunk_count);
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
        result = write_u32(ctx, (uint32_t) chunk->manager_count);
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
static nmo_result_t chunk_deserialize_internal(nmo_read_ctx_t *ctx, nmo_arena_t *arena, nmo_chunk_t **out_chunk) {
    nmo_result_t result;

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
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
            chunk->chunks = nmo_arena_alloc(arena, chunk_count * sizeof(nmo_chunk_t *), sizeof(void *));
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
nmo_chunk_t *nmo_chunk_create(nmo_arena_t *arena) {
    if (!arena) {
        return NULL;
    }

    nmo_chunk_t *chunk = nmo_arena_alloc(arena, sizeof(nmo_chunk_t), sizeof(void *));
    if (!chunk) {
        return NULL;
    }

    /* Initialize all fields to 0/NULL */
    memset(chunk, 0, sizeof(nmo_chunk_t));

    /* Set defaults */
    chunk->chunk_version = NMO_CHUNK_VERSION_4;
    chunk->owns_data = 1;
    chunk->arena = arena;

    return chunk;
}

/**
 * Serialize chunk to binary format
 */
nmo_result_t nmo_chunk_serialize(const nmo_chunk_t *chunk,
                                 void **out_data,
                                 size_t *out_size,
                                 nmo_arena_t *arena) {
    /* Validate arguments */
    if (!chunk || !out_data || !out_size || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to chunk_serialize"));
    }

    /* Calculate total size */
    size_t total_size = chunk_calc_size(chunk);

    /* Allocate output buffer */
    uint8_t *buffer = nmo_arena_alloc(arena, total_size, 4);
    if (!buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate output buffer"));
    }

    /* Setup write context */
    nmo_write_ctx_t ctx = {
        .buffer = buffer,
        .pos = 0,
        .size = total_size
    };

    /* Serialize chunk */
    nmo_result_t result = chunk_serialize_internal(chunk, &ctx);
    if (result.code != NMO_OK) {
        return result;
    }

    *out_data = buffer;
    *out_size = total_size;

    return nmo_result_ok();
}

/**
 * @brief Calculate serialized size for VERSION1 format
 */
static size_t chunk_calc_size_version1(const nmo_chunk_t *chunk) {
    size_t size = 0;

    /* VERSION1 header:
     * - version_info (4 bytes)
     * - chunk_class_id (4 bytes)
     * - chunk_size (4 bytes)
     * - reserved (4 bytes)
     * - id_count (4 bytes)
     * - chunk_count (4 bytes)
     */
    size += 6 * sizeof(uint32_t); /* 24 bytes header */

    /* Data buffer (in DWORDs) */
    size += chunk->data_size * sizeof(uint32_t);

    /* IDs array */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        size += chunk->id_count * sizeof(uint32_t);
    }

    /* Sub-chunks positions (just count, positions are relative) */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        size += chunk->chunk_count * sizeof(uint32_t);
    }

    return size;
}

/**
 * @brief Serialize chunk in Virtools VERSION1 format
 *
 * This matches the format expected by nmo_chunk_parse() for VERSION1 chunks.
 *
 * Format:
 *   Offset | Size | Field
 *   -------|------|-------
 *   0      | 4    | version_info = (data_version & 0xFF) | ((chunk_version & 0xFF) << 16)
 *   4      | 4    | chunk_class_id
 *   8      | 4    | chunk_size (in DWORDs)
 *   12     | 4    | reserved (0)
 *   16     | 4    | id_count
 *   20     | 4    | chunk_count
 *   24     | N*4  | data buffer
 *   ...    | N*4  | IDs array (if id_count > 0)
 *   ...    | N*4  | chunk positions (if chunk_count > 0, relative offsets)
 */
nmo_result_t nmo_chunk_serialize_version1(const nmo_chunk_t *chunk,
                                          void **out_data,
                                          size_t *out_size,
                                          nmo_arena_t *arena) {
    /* Validate arguments */
    if (!chunk || !out_data || !out_size || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to chunk_serialize_version1"));
    }

    /* Calculate total size */
    size_t total_size = chunk_calc_size_version1(chunk);

    /* Allocate output buffer */
    uint32_t *buffer = (uint32_t *) nmo_arena_alloc(arena, total_size, sizeof(uint32_t));
    if (!buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate output buffer"));
    }

    size_t pos = 0; /* Position in DWORDs */

    /* Write version info: (data_version & 0xFF) | ((chunk_version & 0xFF) << 16) */
    /* VERSION1 format uses chunk_version = 4 */
    uint32_t version_info = (chunk->data_version & 0xFF) | ((NMO_CHUNK_VERSION1 & 0xFF) << 16);
    buffer[pos++] = version_info;

    /* Write chunk_class_id (32-bit) */
    buffer[pos++] = chunk->class_id;

    /* Write chunk_size (in DWORDs) */
    buffer[pos++] = (uint32_t) chunk->data_size;

    /* Write reserved field */
    buffer[pos++] = 0;

    /* Write id_count */
    uint32_t id_count = (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) ? (uint32_t) chunk->id_count : 0;
    buffer[pos++] = id_count;

    /* Write chunk_count */
    uint32_t chunk_count = (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) ? (uint32_t) chunk->chunk_count : 0;
    buffer[pos++] = chunk_count;

    /* Write data buffer */
    if (chunk->data_size > 0) {
        memcpy(&buffer[pos], chunk->data, chunk->data_size * sizeof(uint32_t));
        pos += chunk->data_size;
    }

    /* Write IDs array */
    if (id_count > 0) {
        memcpy(&buffer[pos], chunk->ids, id_count * sizeof(uint32_t));
        pos += id_count;
    }

    /* Write chunk positions (placeholder - just write zeros for now) */
    if (chunk_count > 0) {
        /* TODO: Calculate actual relative positions for sub-chunks */
        memset(&buffer[pos], 0, chunk_count * sizeof(uint32_t));
        pos += chunk_count;
    }

    *out_data = buffer;
    *out_size = total_size;

    return nmo_result_ok();
}

/**
 * Deserialize chunk from binary format
 */
nmo_result_t nmo_chunk_deserialize(const void *data,
                                   size_t size,
                                   nmo_arena_t *arena,
                                   nmo_chunk_t **out_chunk) {
    /* Validate arguments */
    if (!data || !arena || !out_chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to chunk_deserialize"));
    }

    if (size < 8) {
        /* Minimum size: version info + chunk size */
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR,
                                          "Buffer too small for chunk"));
    }

    /* Setup read context */
    nmo_read_ctx_t ctx = {
        .buffer = (const uint8_t *) data,
        .pos = 0,
        .size = size
    };

    /* Deserialize chunk */
    return chunk_deserialize_internal(&ctx, arena, out_chunk);
}

/**
 * Destroy chunk
 */
void nmo_chunk_destroy(nmo_chunk_t *chunk) {
    /* Since we use arena allocation, this is mostly a no-op.
     * The arena itself will handle cleanup when destroyed.
     * We just mark it as unused for safety.
     */
    (void) chunk;
}

nmo_chunk_t *nmo_chunk_clone(const nmo_chunk_t *src, nmo_arena_t *arena) {
    if (src == NULL || arena == NULL) {
        return NULL;
    }

    // Allocate new chunk
    nmo_chunk_t *clone = (nmo_chunk_t *) nmo_arena_alloc(arena, sizeof(nmo_chunk_t), 16);
    if (clone == NULL) {
        return NULL;
    }

    // Initialize chunk
    memset(clone, 0, sizeof(nmo_chunk_t));

    // Copy basic fields
    clone->class_id = src->class_id;
    clone->data_version = src->data_version;
    clone->chunk_version = src->chunk_version;
    clone->chunk_class_id = src->chunk_class_id;
    clone->chunk_options = src->chunk_options;

    // Clone data buffer
    if (src->data != NULL && src->data_size > 0) {
        clone->data = (uint32_t *) nmo_arena_alloc(arena, src->data_size * sizeof(uint32_t), 4);
        if (clone->data == NULL) {
            return NULL;
        }
        memcpy(clone->data, src->data, src->data_size * sizeof(uint32_t));
        clone->data_size = src->data_size;
        clone->data_capacity = src->data_size;
    }

    // Clone ID list
    if (src->ids != NULL && src->id_count > 0) {
        clone->ids = (nmo_object_id_t *) nmo_arena_alloc(arena, src->id_count * sizeof(nmo_object_id_t), 4);
        if (clone->ids == NULL) {
            return NULL;
        }
        memcpy(clone->ids, src->ids, src->id_count * sizeof(nmo_object_id_t));
        clone->id_count = src->id_count;
        clone->id_capacity = src->id_count;
    }

    // Clone manager list
    if (src->managers != NULL && src->manager_count > 0) {
        clone->managers = (uint32_t *) nmo_arena_alloc(arena, src->manager_count * sizeof(uint32_t), 4);
        if (clone->managers == NULL) {
            return NULL;
        }
        memcpy(clone->managers, src->managers, src->manager_count * sizeof(uint32_t));
        clone->manager_count = src->manager_count;
        clone->manager_capacity = src->manager_count;
    }

    // Clone sub-chunks (recursive)
    if (src->chunks != NULL && src->chunk_count > 0) {
        clone->chunks = (nmo_chunk_t **) nmo_arena_alloc(arena, src->chunk_count * sizeof(nmo_chunk_t *), 8);
        if (clone->chunks == NULL) {
            return NULL;
        }

        for (size_t i = 0; i < src->chunk_count; i++) {
            if (src->chunks[i] != NULL) {
                clone->chunks[i] = nmo_chunk_clone(src->chunks[i], arena);
                if (clone->chunks[i] == NULL) {
                    return NULL;
                }
            } else {
                clone->chunks[i] = NULL;
            }
        }

        clone->chunk_count = src->chunk_count;
        clone->chunk_capacity = src->chunk_count;
    }

    return clone;
}


/**
 * Parse chunk from data
 */
/**
 * @brief Parse chunk from buffer
 *
 * Implements CKStateChunk::ConvertFromBuffer functionality.
 * Reference: CKStateChunk.cpp:1355-1524
 *
 * Format depends on chunk version:
 * - VERSION1/VERSION2: [Version][ClassID][Size][Reserved][IDCount][ChunkCount][...Data...]
 * - VERSION4: [PackedVersion][Size][...Data...] where PackedVersion contains class ID and options
 */
nmo_result_t nmo_chunk_parse(nmo_chunk_t *chunk, const void *data, size_t size) {
    if (chunk == NULL || data == NULL || size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments to nmo_chunk_parse"));
    }

    /* Store raw data for round-trip saving */
    chunk->raw_data = data;
    chunk->raw_size = size;

    const uint32_t *buf = (const uint32_t *) data;
    size_t pos = 0; /* Position in DWORDs */
    size_t size_dwords = size / sizeof(uint32_t);

    if (size_dwords < 1) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR, "Buffer too small for chunk header"));
    }

    /* Read first DWORD which contains version info */
    uint32_t val = buf[pos++];
    uint16_t data_version = (uint16_t) (val & 0x0000FFFF);
    data_version &= 0x00FF; /* Only low byte is data version */
    uint16_t chunk_version = (uint16_t) ((val & 0xFFFF0000) >> 16);
    chunk_version &= 0x00FF; /* Only low byte is chunk version */

    chunk->data_version = data_version;
    chunk->chunk_version = chunk_version;

    /* Parse based on chunk version */
    if (chunk_version < NMO_CHUNK_VERSION2) {
        /* CHUNK_VERSION1 format */
        if (pos + 5 > size_dwords) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR, "Buffer too small for VERSION1 header"));
        }

        /* Read 32-bit class_id */
        chunk->class_id = buf[pos++];
        chunk->chunk_class_id = (uint8_t)(chunk->class_id & 0xFF);
        uint32_t chunk_size = buf[pos++];
        pos++; /* Reserved field */
        uint32_t id_count = buf[pos++];
        uint32_t chunk_count = buf[pos++];

        /* Allocate and read data buffer */
        if (chunk_size > 0) {
            if (pos + chunk_size > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk data"));
            }

            chunk->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                       chunk_size * sizeof(uint32_t), sizeof(uint32_t));
            if (chunk->data == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate chunk data"));
            }

            memcpy(chunk->data, &buf[pos], chunk_size * sizeof(uint32_t));
            chunk->data_size = chunk_size;
            chunk->data_capacity = chunk_size;
            pos += chunk_size;
        }

        /* Allocate and read IDs */
        if (id_count > 0) {
            if (pos + id_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for ID array"));
            }

            chunk->ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                      id_count * sizeof(uint32_t), sizeof(uint32_t));
            if (chunk->ids == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate ID array"));
            }

            memcpy(chunk->ids, &buf[pos], id_count * sizeof(uint32_t));
            chunk->id_count = id_count;
            chunk->id_capacity = id_count;
            pos += id_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }

        /* Skip sub-chunks array (just positions, not actual chunks) */
        if (chunk_count > 0) {
            if (pos + chunk_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk array"));
            }
            pos += chunk_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
        }
    } else if (chunk_version == NMO_CHUNK_VERSION2) {
        /* CHUNK_VERSION2 format (adds manager data) */
        if (pos + 5 > size_dwords) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR, "Buffer too small for VERSION2 header"));
        }

        chunk->chunk_class_id = (uint8_t) buf[pos++];
        uint32_t chunk_size = buf[pos++];
        pos++; /* Reserved field */
        uint32_t id_count = buf[pos++];
        uint32_t chunk_count = buf[pos++];
        uint32_t manager_count = buf[pos++];

        /* Allocate and read data buffer */
        if (chunk_size > 0) {
            if (pos + chunk_size > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk data"));
            }

            chunk->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                       chunk_size * sizeof(uint32_t), sizeof(uint32_t));
            if (chunk->data == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate chunk data"));
            }

            memcpy(chunk->data, &buf[pos], chunk_size * sizeof(uint32_t));
            chunk->data_size = chunk_size;
            chunk->data_capacity = chunk_size;
            pos += chunk_size;
        }

        /* Read IDs, chunks, and managers same as VERSION1 */
        if (id_count > 0) {
            if (pos + id_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for ID array"));
            }

            chunk->ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                      id_count * sizeof(uint32_t), sizeof(uint32_t));
            if (chunk->ids == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate ID array"));
            }

            memcpy(chunk->ids, &buf[pos], id_count * sizeof(uint32_t));
            chunk->id_count = id_count;
            chunk->id_capacity = id_count;
            pos += id_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }

        if (chunk_count > 0) {
            if (pos + chunk_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk array"));
            }
            pos += chunk_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
        }

        if (manager_count > 0) {
            if (pos + manager_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for manager array"));
            }

            chunk->managers = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                           manager_count * sizeof(uint32_t), sizeof(uint32_t));
            if (chunk->managers == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate manager array"));
            }

            memcpy(chunk->managers, &buf[pos], manager_count * sizeof(uint32_t));
            chunk->manager_count = manager_count;
            chunk->manager_capacity = manager_count;
            pos += manager_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
        }
    } else if (chunk_version <= NMO_CHUNK_VERSION4) {
        /* CHUNK_VERSION3/VERSION4 format (modern, compact header with options) */
        /* Extract chunk options and class ID from packed version field */
        uint8_t chunk_options = (uint8_t) ((chunk_version & 0xFF00) >> 8);
        chunk->chunk_class_id = (uint8_t) ((data_version & 0xFF00) >> 8);
        chunk->chunk_options = chunk_options;

        /* Re-extract actual versions (low bytes only) */
        chunk->data_version = data_version & 0x00FF;
        chunk->chunk_version = chunk_version & 0x00FF;

        /* Read chunk size */
        if (pos >= size_dwords) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR, "Buffer too small for chunk size"));
        }
        uint32_t chunk_size = buf[pos++];

        /* Allocate and read data buffer */
        if (chunk_size > 0) {
            if (pos + chunk_size > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk data"));
            }

            chunk->data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                       chunk_size * sizeof(uint32_t), sizeof(uint32_t));
            if (chunk->data == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to allocate chunk data"));
            }

            memcpy(chunk->data, &buf[pos], chunk_size * sizeof(uint32_t));
            chunk->data_size = chunk_size;
            chunk->data_capacity = chunk_size;
            pos += chunk_size;
        }

        /* Read optional sections based on chunk_options flags */
        if (chunk_options & NMO_CHUNK_OPTION_IDS) {
            if (pos >= size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for ID count"));
            }
            uint32_t id_count = buf[pos++];

            if (id_count > 0) {
                if (pos + id_count > size_dwords) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for ID array"));
                }

                chunk->ids = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          id_count * sizeof(uint32_t), sizeof(uint32_t));
                if (chunk->ids == NULL) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                      NMO_SEVERITY_ERROR, "Failed to allocate ID array"));
                }

                memcpy(chunk->ids, &buf[pos], id_count * sizeof(uint32_t));
                chunk->id_count = id_count;
                chunk->id_capacity = id_count;
                pos += id_count;
            }
        }

        if (chunk_options & NMO_CHUNK_OPTION_CHN) {
            if (pos >= size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk count"));
            }
            uint32_t chunk_count = buf[pos++];

            if (chunk_count > 0) {
                if (pos + chunk_count > size_dwords) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for chunk array"));
                }
                /* Skip chunk positions for now */
                pos += chunk_count;
            }
        }

        if (chunk_options & NMO_CHUNK_OPTION_MAN) {
            if (pos >= size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for manager count"));
            }
            uint32_t manager_count = buf[pos++];

            if (manager_count > 0) {
                if (pos + manager_count > size_dwords) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for manager array"));
                }

                chunk->managers = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                               manager_count * sizeof(uint32_t), sizeof(uint32_t));
                if (chunk->managers == NULL) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                      NMO_SEVERITY_ERROR, "Failed to allocate manager array"));
                }

                memcpy(chunk->managers, &buf[pos], manager_count * sizeof(uint32_t));
                chunk->manager_count = manager_count;
                chunk->manager_capacity = manager_count;
                pos += manager_count;
            }
        }
    } else {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_UNSUPPORTED_VERSION,
                                          NMO_SEVERITY_ERROR, "Unsupported chunk version"));
    }

    return nmo_result_ok();
}

/**
 * Get chunk header
 */
nmo_result_t nmo_chunk_get_header(const nmo_chunk_t *chunk, nmo_chunk_header_t *out_header) {
    if (!chunk || !out_header) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments"));
    }

    out_header->chunk_id = chunk->class_id;
    out_header->chunk_size = (uint32_t) (chunk->data_size * 4); /* Convert to bytes */
    out_header->sub_chunk_count = (uint32_t) chunk->chunk_count;
    out_header->flags = chunk->chunk_options;

    return nmo_result_ok();
}

/**
 * Get chunk data
 */
const void *nmo_chunk_get_data(const nmo_chunk_t *chunk, size_t *out_size) {
    if (!chunk) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }

    if (out_size) {
        *out_size = chunk->data_size * 4; /* Convert to bytes */
    }
    return chunk->data;
}
