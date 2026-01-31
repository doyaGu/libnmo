/**
 * @file chunk.c
 * @brief CKStateChunk handling implementation
 */

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_parser.h"
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
    uint32_t option_flags = chunk->chunk_options;

    if (chunk->ids.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_IDS;
    }
    if (chunk->chunk_refs.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_CHN;
    }
    if (chunk->managers.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_MAN;
    }

    /* Version info + chunk size */
    size += 4 + 4;

    /* Data buffer */
    size += chunk->data.count * 4; /* count is in DWORDs */

    /* Optional IDs list */
    if (option_flags & NMO_CHUNK_OPTION_IDS) {
        size += 4; /* count */
        size += chunk->ids.count * 4;
    }

    /* Optional sub-chunk reference list */
    if (option_flags & NMO_CHUNK_OPTION_CHN) {
        size += 4; /* count */
        size += chunk->chunk_refs.count * 4;
    }

    /* Optional managers list */
    if (option_flags & NMO_CHUNK_OPTION_MAN) {
        size += 4; /* count */
        size += chunk->managers.count * 4;
    }

    return size;
}

static nmo_result_t chunk_build_subchunks_from_refs(nmo_chunk_t *chunk, nmo_arena_t *arena) {
    if (chunk == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to subchunk builder"));
    }

    if (chunk->chunk_refs.count == 0) {
        return nmo_result_ok();
    }

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate chunk parser"));
    }

    const uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
    size_t ref_count = chunk->chunk_refs.count;

    for (size_t i = 0; i < ref_count; i++) {
        uint32_t ref = refs[i];

        if (ref == 0xFFFFFFFFu) {
            if (i + 1 >= ref_count) {
                nmo_chunk_parser_destroy(parser);
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Malformed sub-chunk sequence markers"));
            }

            uint32_t seq_pos = refs[++i];
            if (seq_pos >= chunk->data.count) {
                continue;
            }

            if (nmo_chunk_parser_seek(parser, seq_pos) != NMO_OK) {
                continue;
            }

            int seq_count = nmo_chunk_parser_start_read_sequence(parser);
            if (seq_count < 0) {
                nmo_chunk_parser_destroy(parser);
                return nmo_result_error(NMO_ERROR(NULL, seq_count, NMO_SEVERITY_ERROR,
                                                  "Failed to start sub-chunk sequence"));
            }

            for (int s = 0; s < seq_count; s++) {
                nmo_chunk_t *sub = NULL;
                int read_result = nmo_chunk_parser_read_subchunk(parser, arena, &sub);
                if (read_result != NMO_OK) {
                    nmo_chunk_parser_destroy(parser);
                    return nmo_result_error(NMO_ERROR(NULL, read_result, NMO_SEVERITY_ERROR,
                                                      "Failed to read sub-chunk"));
                }

                if (sub != NULL) {
                    nmo_result_t append_result = nmo_arena_array_append(&chunk->chunks, &sub);
                    if (append_result.code != NMO_OK) {
                        nmo_chunk_parser_destroy(parser);
                        return append_result;
                    }

                    if (sub->chunk_refs.count > 0) {
                        nmo_result_t nested = chunk_build_subchunks_from_refs(sub, arena);
                        if (nested.code != NMO_OK) {
                            nmo_chunk_parser_destroy(parser);
                            return nested;
                        }
                    }
                }
            }

            continue;
        }

        if (ref >= chunk->data.count) {
            continue;
        }

        if (nmo_chunk_parser_seek(parser, ref) != NMO_OK) {
            continue;
        }

        nmo_chunk_t *sub = NULL;
        int read_result = nmo_chunk_parser_read_subchunk(parser, arena, &sub);
        if (read_result != NMO_OK) {
            nmo_chunk_parser_destroy(parser);
            return nmo_result_error(NMO_ERROR(NULL, read_result, NMO_SEVERITY_ERROR,
                                              "Failed to read sub-chunk"));
        }

        if (sub != NULL) {
            nmo_result_t append_result = nmo_arena_array_append(&chunk->chunks, &sub);
            if (append_result.code != NMO_OK) {
                nmo_chunk_parser_destroy(parser);
                return append_result;
            }

            if (sub->chunk_refs.count > 0) {
                nmo_result_t nested = chunk_build_subchunks_from_refs(sub, arena);
                if (nested.code != NMO_OK) {
                    nmo_chunk_parser_destroy(parser);
                    return nested;
                }
            }
        }
    }

    nmo_chunk_parser_destroy(parser);
    return nmo_result_ok();
}

static nmo_result_t chunk_validate_offset_list(const nmo_chunk_t *chunk,
                                               const nmo_arena_array_t *list,
                                               const char *label) {
    if (chunk == NULL || list == NULL || label == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to offset list validation"));
    }

    (void)label;

    if (list->count == 0) {
        return nmo_result_ok();
    }

    if (chunk->data.count == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR,
                                          "Offset list present but chunk has no data"));
    }

    const uint32_t *entries = NMO_ARENA_ARRAY_DATA(uint32_t, list);
    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t entry_count = list->count;

    for (size_t i = 0; i < entry_count; i++) {
        uint32_t value = entries[i];

        if (value == 0xFFFFFFFFu) {
            if (i + 1 >= entry_count) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Sequence marker missing offset in offset list"));
            }

            uint32_t seq_pos = entries[++i];
            if (seq_pos >= chunk->data.count) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Sequence offset out of bounds in offset list"));
            }

            uint32_t seq_count = data[seq_pos];
            if (seq_pos + 1u + seq_count > chunk->data.count) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Sequence count exceeds chunk data bounds"));
            }

            continue;
        }

        if (value >= chunk->data.count) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR,
                                              "Offset entry out of bounds"));
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize chunk recursively
 */
static nmo_result_t chunk_serialize_internal(const nmo_chunk_t *chunk, nmo_write_ctx_t *ctx) {
    nmo_result_t result;
    uint32_t option_flags = chunk->chunk_options;

    if (chunk->ids.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_IDS;
    }
    if (chunk->chunk_refs.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_CHN;
    }
    if (chunk->managers.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_MAN;
    }

    /* Pack version info:
     * versionInfo = (dataVersion | (chunkClassID << 8)) |
     *               ((chunkVersion | (chunkOptions << 8)) << 16)
     */
    uint32_t version_info = 0;
    uint8_t class_id_byte = (chunk->chunk_class_id != 0) ?
                            chunk->chunk_class_id :
                            (uint8_t) (chunk->class_id & 0xFF);
    version_info |= (chunk->data_version & 0xFF);
    version_info |= ((uint32_t) (class_id_byte & 0xFFu) << 8);
    version_info |= ((chunk->chunk_version & 0xFF) << 16);
    version_info |= ((option_flags & 0xFF) << 24);

    /* Write version info */
    result = write_u32(ctx, version_info);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Write chunk size in DWORDs */
    result = write_u32(ctx, (uint32_t) chunk->data.count);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Write data buffer */
    if (chunk->data.count > 0) {
        const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        result = write_bytes(ctx, data, chunk->data.count * 4);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Write IDs list if present */
    if (option_flags & NMO_CHUNK_OPTION_IDS) {
        result = write_u32(ctx, (uint32_t) chunk->ids.count);
        if (result.code != NMO_OK) {
            return result;
        }
        if (chunk->ids.count > 0) {
            const uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
            result = write_bytes(ctx, ids, chunk->ids.count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    /* Write sub-chunks if present */
    if (option_flags & NMO_CHUNK_OPTION_CHN) {
        result = write_u32(ctx, (uint32_t) chunk->chunk_refs.count);
        if (result.code != NMO_OK) {
            return result;
        }
        if (chunk->chunk_refs.count > 0) {
            const uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
            result = write_bytes(ctx, refs, chunk->chunk_refs.count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    /* Write managers list if present */
    if (option_flags & NMO_CHUNK_OPTION_MAN) {
        result = write_u32(ctx, (uint32_t) chunk->managers.count);
        if (result.code != NMO_OK) {
            return result;
        }
        if (chunk->managers.count > 0) {
            const uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->managers);
            result = write_bytes(ctx, managers, chunk->managers.count * 4);
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

    if (chunk->class_id == 0 && chunk->chunk_class_id != 0) {
        chunk->class_id = (uint32_t) chunk->chunk_class_id;
    }

    /* Read chunk size in DWORDs */
    uint32_t chunk_size_dwords = 0;
    result = read_u32(ctx, &chunk_size_dwords);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Read data buffer */
    result = nmo_arena_array_resize(&chunk->data, chunk_size_dwords);
    if (result.code != NMO_OK) {
        return result;
    }

    if (chunk_size_dwords > 0) {
        uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        result = read_bytes(ctx, data, chunk_size_dwords * 4);
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

        result = nmo_arena_array_resize(&chunk->ids, id_count);
        if (result.code != NMO_OK) {
            return result;
        }

        if (id_count > 0) {
            uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
            result = read_bytes(ctx, ids, id_count * 4);
            if (result.code != NMO_OK) {
                return result;
            }
        }
    }

    /* Read sub-chunks if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        uint32_t ref_count = 0;
        result = read_u32(ctx, &ref_count);
        if (result.code != NMO_OK) {
            return result;
        }

        result = nmo_arena_array_resize(&chunk->chunk_refs, ref_count);
        if (result.code != NMO_OK) {
            return result;
        }

        if (ref_count > 0) {
            uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
            result = read_bytes(ctx, refs, ref_count * 4);
            if (result.code != NMO_OK) {
                return result;
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

        result = nmo_arena_array_resize(&chunk->managers, manager_count);
        if (result.code != NMO_OK) {
            return result;
        }

        if (manager_count > 0) {
            uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->managers);
            result = read_bytes(ctx, managers, manager_count * 4);
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

    /* Initialize arena-backed arrays */
    chunk->data = (nmo_arena_array_t)NMO_ARENA_ARRAY_INIT(uint32_t, arena);
    chunk->ids = (nmo_arena_array_t)NMO_ARENA_ARRAY_INIT(uint32_t, arena);
    chunk->chunks = (nmo_arena_array_t)NMO_ARENA_ARRAY_INIT(nmo_chunk_t *, arena);
    chunk->chunk_refs = (nmo_arena_array_t)NMO_ARENA_ARRAY_INIT(uint32_t, arena);
    chunk->managers = (nmo_arena_array_t)NMO_ARENA_ARRAY_INIT(uint32_t, arena);

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
    uint32_t chunk_version = chunk->chunk_version;
    const size_t data_count = chunk->data.count;
    const size_t id_count = chunk->ids.count;
    const size_t chunk_ref_count = chunk->chunk_refs.count;
    const size_t manager_count = chunk->managers.count;

    if (chunk_version < NMO_CHUNK_VERSION2) {
        /* VERSION1 header: version_info, class_id, chunk_size, reserved, id_count, chunk_count */
        size += 6 * sizeof(uint32_t);
        size += data_count * sizeof(uint32_t);
        size += id_count * sizeof(uint32_t);
        size += chunk_ref_count * sizeof(uint32_t);
        return size;
    }

    if (chunk_version == NMO_CHUNK_VERSION2) {
        /* VERSION2 header adds manager_count */
        size += 7 * sizeof(uint32_t);
        size += data_count * sizeof(uint32_t);
        size += id_count * sizeof(uint32_t);
        size += chunk_ref_count * sizeof(uint32_t);
        size += manager_count * sizeof(uint32_t);
        return size;
    }

    /* VERSION3/VERSION4 compact header */
    const int has_ids = (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) || id_count > 0;
    const int has_chunks = (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) || chunk_ref_count > 0;
    const int has_managers = (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) || manager_count > 0;

    size += 2 * sizeof(uint32_t); /* version_info + chunk_size */
    size += data_count * sizeof(uint32_t);

    if (has_ids) {
        size += sizeof(uint32_t); /* count */
        size += id_count * sizeof(uint32_t);
    }

    if (has_chunks) {
        size += sizeof(uint32_t);
        size += chunk_ref_count * sizeof(uint32_t);
    }

    if (has_managers) {
        size += sizeof(uint32_t);
        size += manager_count * sizeof(uint32_t);
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
 *   0      | 4    | version_info = (data_version | (chunk_class_id << 8)) |
 *                ((chunk_version | (chunk_options << 8)) << 16)
 *   4      | 4    | chunk_size (in DWORDs)
 *   8      | N*4  | data buffer
 *   ...    |      | [if IDS] id_count + id_count entries
 *   ...    |      | [if CHN] chunk_count + chunk_count entries
 *   ...    |      | [if MAN] manager_count + manager_count entries
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
    uint32_t chunk_version = chunk->chunk_version;
    const size_t data_count = chunk->data.count;
    const size_t id_count = chunk->ids.count;
    const size_t chunk_ref_count = chunk->chunk_refs.count;
    const size_t manager_count = chunk->managers.count;
    const uint32_t *data_u32 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    const uint32_t *ids_u32 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
    const uint32_t *refs_u32 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
    const uint32_t *mgrs_u32 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->managers);

    if (chunk_version < NMO_CHUNK_VERSION1) {
        chunk_version = NMO_CHUNK_VERSION1;
    } else if (chunk_version > NMO_CHUNK_VERSION4) {
        chunk_version = NMO_CHUNK_VERSION4;
    }

    if (chunk_version < NMO_CHUNK_VERSION2) {
        /* VERSION1 layout (24-byte header) */
        uint32_t version_info = (chunk->data_version & 0xFFu) |
                                ((chunk_version & 0xFFu) << 16);
        buffer[pos++] = version_info;
        buffer[pos++] = chunk->class_id;
        buffer[pos++] = (uint32_t) data_count;
        buffer[pos++] = 0u; /* Reserved */
        buffer[pos++] = (uint32_t) id_count;
        buffer[pos++] = (uint32_t) chunk_ref_count;

        if (data_count > 0) {
            if (!data_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has data_size but no data"));
            }
            memcpy(&buffer[pos], data_u32, data_count * sizeof(uint32_t));
            pos += data_count;
        }

        if (id_count > 0) {
            if (!ids_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has ID count but no ID list"));
            }
            memcpy(&buffer[pos], ids_u32, id_count * sizeof(uint32_t));
            pos += id_count;
        }

        if (chunk_ref_count > 0) {
            if (!refs_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has chunk references but no data"));
            }
            memcpy(&buffer[pos], refs_u32, chunk_ref_count * sizeof(uint32_t));
            pos += chunk_ref_count;
        }
    } else if (chunk_version == NMO_CHUNK_VERSION2) {
        /* VERSION2 layout adds manager count */
        uint32_t version_info = (chunk->data_version & 0xFFu) |
                                ((chunk_version & 0xFFu) << 16);
        buffer[pos++] = version_info;
        buffer[pos++] = (uint32_t) chunk->class_id;
        buffer[pos++] = (uint32_t) data_count;
        buffer[pos++] = 0u;
        buffer[pos++] = (uint32_t) id_count;
        buffer[pos++] = (uint32_t) chunk_ref_count;
        buffer[pos++] = (uint32_t) manager_count;

        if (data_count > 0) {
            if (!data_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has data_size but no data"));
            }
            memcpy(&buffer[pos], data_u32, data_count * sizeof(uint32_t));
            pos += data_count;
        }

        if (id_count > 0) {
            if (!ids_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has ID count but no ID list"));
            }
            memcpy(&buffer[pos], ids_u32, id_count * sizeof(uint32_t));
            pos += id_count;
        }

        if (chunk_ref_count > 0) {
            if (!refs_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has chunk references but no data"));
            }
            memcpy(&buffer[pos], refs_u32, chunk_ref_count * sizeof(uint32_t));
            pos += chunk_ref_count;
        }

        if (manager_count > 0) {
            if (!mgrs_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has manager count but no manager data"));
            }
            memcpy(&buffer[pos], mgrs_u32, manager_count * sizeof(uint32_t));
            pos += manager_count;
        }
    } else {
        /* VERSION3/VERSION4 compact layout */
        uint32_t option_flags = chunk->chunk_options;
        if (id_count > 0) {
            option_flags |= NMO_CHUNK_OPTION_IDS;
        }
        if (chunk_ref_count > 0) {
            option_flags |= NMO_CHUNK_OPTION_CHN;
        }
        if (manager_count > 0) {
            option_flags |= NMO_CHUNK_OPTION_MAN;
        }

        uint8_t chunk_options = (uint8_t) (option_flags & 0xFFu);
        uint8_t data_version = (uint8_t) (chunk->data_version & 0xFFu);
        uint8_t class_id_byte = (chunk->chunk_class_id != 0) ?
                                chunk->chunk_class_id :
                                (uint8_t) (chunk->class_id & 0xFFu);

        uint16_t data_packed = (uint16_t) (data_version | (class_id_byte << 8));
        uint16_t chunk_packed = (uint16_t) ((chunk_version & 0xFFu) | (chunk_options << 8));
        uint32_t version_info = (uint32_t) data_packed | ((uint32_t) chunk_packed << 16);
        buffer[pos++] = version_info;
        buffer[pos++] = (uint32_t) data_count;

        if (data_count > 0) {
            if (!data_u32) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Chunk has data_size but no data"));
            }
            memcpy(&buffer[pos], data_u32, data_count * sizeof(uint32_t));
            pos += data_count;
        }

        if (option_flags & NMO_CHUNK_OPTION_IDS) {
            buffer[pos++] = (uint32_t) id_count;
            if (id_count > 0) {
                if (!ids_u32) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                      NMO_SEVERITY_ERROR,
                                                      "Chunk has ID count but no ID list"));
                }
                memcpy(&buffer[pos], ids_u32, id_count * sizeof(uint32_t));
                pos += id_count;
            }
        }

        if (option_flags & NMO_CHUNK_OPTION_CHN) {
            buffer[pos++] = (uint32_t) chunk_ref_count;
            if (chunk_ref_count > 0) {
                if (!refs_u32) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                      NMO_SEVERITY_ERROR,
                                                      "Chunk has chunk references but no data"));
                }
                memcpy(&buffer[pos], refs_u32, chunk_ref_count * sizeof(uint32_t));
                pos += chunk_ref_count;
            }
        }

        if (option_flags & NMO_CHUNK_OPTION_MAN) {
            buffer[pos++] = (uint32_t) manager_count;
            if (manager_count > 0) {
                if (!mgrs_u32) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                      NMO_SEVERITY_ERROR,
                                                      "Chunk has manager count but no manager data"));
                }
                memcpy(&buffer[pos], mgrs_u32, manager_count * sizeof(uint32_t));
                pos += manager_count;
            }
        }
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

    nmo_chunk_t *clone = nmo_chunk_create(arena);
    if (clone == NULL) {
        return NULL;
    }

    // Copy basic fields
    clone->class_id = src->class_id;
    clone->data_version = src->data_version;
    clone->chunk_version = src->chunk_version;
    clone->chunk_class_id = src->chunk_class_id;
    clone->chunk_options = src->chunk_options;

    clone->raw_data = src->raw_data;
    clone->raw_size = src->raw_size;

    if (src->data.count > 0) {
        nmo_result_t result = nmo_arena_array_resize(&clone->data, src->data.count);
        if (result.code != NMO_OK) {
            return NULL;
        }
        memcpy(clone->data.data, src->data.data, src->data.count * sizeof(uint32_t));
    }

    if (src->ids.count > 0) {
        nmo_result_t result = nmo_arena_array_resize(&clone->ids, src->ids.count);
        if (result.code != NMO_OK) {
            return NULL;
        }
        memcpy(clone->ids.data, src->ids.data, src->ids.count * sizeof(uint32_t));
    }

    if (src->chunk_refs.count > 0) {
        nmo_result_t result = nmo_arena_array_resize(&clone->chunk_refs, src->chunk_refs.count);
        if (result.code != NMO_OK) {
            return NULL;
        }
        memcpy(clone->chunk_refs.data, src->chunk_refs.data, src->chunk_refs.count * sizeof(uint32_t));
    }

    if (src->managers.count > 0) {
        nmo_result_t result = nmo_arena_array_resize(&clone->managers, src->managers.count);
        if (result.code != NMO_OK) {
            return NULL;
        }
        memcpy(clone->managers.data, src->managers.data, src->managers.count * sizeof(uint32_t));
    }

    if (src->chunks.count > 0) {
        nmo_result_t result = nmo_arena_array_resize(&clone->chunks, src->chunks.count);
        if (result.code != NMO_OK) {
            return NULL;
        }

        nmo_chunk_t **src_chunks = NMO_ARENA_ARRAY_DATA(nmo_chunk_t*, &src->chunks);
        nmo_chunk_t **dst_chunks = NMO_ARENA_ARRAY_DATA(nmo_chunk_t*, &clone->chunks);

        for (size_t i = 0; i < src->chunks.count; i++) {
            if (src_chunks[i] != NULL) {
                dst_chunks[i] = nmo_chunk_clone(src_chunks[i], arena);
                if (dst_chunks[i] == NULL) {
                    return NULL;
                }
            } else {
                dst_chunks[i] = NULL;
            }
        }
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
    uint16_t packed_data_version = (uint16_t) (val & 0x0000FFFF);
    uint16_t packed_chunk_version = (uint16_t) ((val & 0xFFFF0000) >> 16);
    uint8_t data_version = (uint8_t) (packed_data_version & 0x00FF);
    uint8_t chunk_version = (uint8_t) (packed_chunk_version & 0x00FF);

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

            nmo_result_t result = nmo_arena_array_resize(&chunk->data, chunk_size);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->data.data, &buf[pos], chunk_size * sizeof(uint32_t));
            pos += chunk_size;
        }

        /* Allocate and read IDs */
        if (id_count > 0) {
            if (pos + id_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for ID array"));
            }

            nmo_result_t result = nmo_arena_array_resize(&chunk->ids, id_count);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->ids.data, &buf[pos], id_count * sizeof(uint32_t));
            pos += id_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }

        /* Read sub-chunk positions */
        if (chunk_count > 0) {
            if (pos + chunk_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk array"));
            }

            nmo_result_t result = nmo_arena_array_resize(&chunk->chunk_refs, chunk_count);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->chunk_refs.data, &buf[pos], chunk_count * sizeof(uint32_t));
            pos += chunk_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
        }
    } else if (chunk_version == NMO_CHUNK_VERSION2) {
        /* CHUNK_VERSION2 format (adds manager data) */
        if (pos + 5 > size_dwords) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR, "Buffer too small for VERSION2 header"));
        }

        chunk->class_id = buf[pos++];
        chunk->chunk_class_id = (uint8_t)(chunk->class_id & 0xFFu);
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

            nmo_result_t result = nmo_arena_array_resize(&chunk->data, chunk_size);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->data.data, &buf[pos], chunk_size * sizeof(uint32_t));
            pos += chunk_size;
        }

        /* Read IDs, chunks, and managers same as VERSION1 */
        if (id_count > 0) {
            if (pos + id_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for ID array"));
            }

            nmo_result_t result = nmo_arena_array_resize(&chunk->ids, id_count);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->ids.data, &buf[pos], id_count * sizeof(uint32_t));
            pos += id_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }

        if (chunk_count > 0) {
            if (pos + chunk_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for chunk array"));
            }

            nmo_result_t result = nmo_arena_array_resize(&chunk->chunk_refs, chunk_count);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->chunk_refs.data, &buf[pos], chunk_count * sizeof(uint32_t));
            pos += chunk_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
        }

        if (manager_count > 0) {
            if (pos + manager_count > size_dwords) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for manager array"));
            }

            nmo_result_t result = nmo_arena_array_resize(&chunk->managers, manager_count);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->managers.data, &buf[pos], manager_count * sizeof(uint32_t));
            pos += manager_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
        }
    } else if (chunk_version <= NMO_CHUNK_VERSION4) {
        /* CHUNK_VERSION3/VERSION4 format (modern, compact header with options) */
        /* Extract chunk options and class ID from packed version field */
        uint8_t chunk_options = (uint8_t) ((packed_chunk_version & 0xFF00) >> 8);
        chunk->chunk_class_id = (uint8_t) ((packed_data_version & 0xFF00) >> 8);
        chunk->chunk_options = chunk_options;

        /* Use 8-bit class ID as the best available class_id in VERSION3/4 */
        chunk->class_id = (uint32_t) chunk->chunk_class_id;

        /* Re-extract actual versions (low bytes only) */
        chunk->data_version = (uint8_t) (packed_data_version & 0x00FF);
        chunk->chunk_version = (uint8_t) (packed_chunk_version & 0x00FF);

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

            nmo_result_t result = nmo_arena_array_resize(&chunk->data, chunk_size);
            if (result.code != NMO_OK) {
                return result;
            }

            memcpy(chunk->data.data, &buf[pos], chunk_size * sizeof(uint32_t));
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

                nmo_result_t result = nmo_arena_array_resize(&chunk->ids, id_count);
                if (result.code != NMO_OK) {
                    return result;
                }

                memcpy(chunk->ids.data, &buf[pos], id_count * sizeof(uint32_t));
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
                nmo_result_t result = nmo_arena_array_resize(&chunk->chunk_refs, chunk_count);
                if (result.code != NMO_OK) {
                    return result;
                }

                memcpy(chunk->chunk_refs.data, &buf[pos], chunk_count * sizeof(uint32_t));
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

                nmo_result_t result = nmo_arena_array_resize(&chunk->managers, manager_count);
                if (result.code != NMO_OK) {
                    return result;
                }

                memcpy(chunk->managers.data, &buf[pos], manager_count * sizeof(uint32_t));
                pos += manager_count;
            }
        }
    } else {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_UNSUPPORTED_VERSION,
                                          NMO_SEVERITY_ERROR, "Unsupported chunk version"));
    }

    if (chunk->chunk_refs.count > 0) {
        nmo_result_t validate_refs = chunk_validate_offset_list(chunk, &chunk->chunk_refs,
                                                                "chunk_refs");
        if (validate_refs.code != NMO_OK) {
            return validate_refs;
        }

        nmo_result_t sub_result = chunk_build_subchunks_from_refs(chunk, chunk->arena);
        if (sub_result.code != NMO_OK) {
            return sub_result;
        }
    }

    if (chunk->ids.count > 0) {
        nmo_result_t validate_ids = chunk_validate_offset_list(chunk, &chunk->ids, "ids");
        if (validate_ids.code != NMO_OK) {
            return validate_ids;
        }
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

    uint32_t option_flags = chunk->chunk_options;
    if (chunk->ids.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_IDS;
    }
    if (chunk->chunk_refs.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_CHN;
    }
    if (chunk->managers.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_MAN;
    }

    out_header->chunk_id = chunk->class_id;
    out_header->chunk_size = (uint32_t) (chunk->data.count * sizeof(uint32_t));
    out_header->sub_chunk_count = (uint32_t) chunk->chunk_refs.count;
    out_header->flags = option_flags;

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
        *out_size = chunk->data.count * 4; /* Convert to bytes */
    }
    return chunk->data.data;
}
