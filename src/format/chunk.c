/**
 * @file chunk.c
 * @brief CKStateChunk handling implementation
 */

#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include <string.h>
#include <stdint.h>

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
static nmo_status_t write_bytes(nmo_write_ctx_t *ctx, const void *data, size_t len) {
    if (ctx->pos > ctx->size || len > ctx->size - ctx->pos) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Write buffer overrun");
    }
    memcpy(ctx->buffer + ctx->pos, data, len);
    ctx->pos += len;
    NMO_RETURN_OK();
}

/**
 * @brief Write uint32_t value
 */
static nmo_status_t write_u32(nmo_write_ctx_t *ctx, uint32_t value) {
    return write_bytes(ctx, &value, sizeof(uint32_t));
}

/**
 * @brief Read bytes from buffer
 */
static nmo_status_t read_bytes(nmo_read_ctx_t *ctx, void *data, size_t len) {
    if (ctx->pos > ctx->size || len > ctx->size - ctx->pos) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Read buffer overrun");
    }
    memcpy(data, ctx->buffer + ctx->pos, len);
    ctx->pos += len;
    NMO_RETURN_OK();
}

/**
 * @brief Read uint32_t value
 */
static nmo_status_t read_u32(nmo_read_ctx_t *ctx, uint32_t *value) {
    return read_bytes(ctx, value, sizeof(uint32_t));
}

static int dword_range_fits(size_t start, size_t count, size_t total) {
    return start <= total && count <= total - start;
}

/**
 * @brief Compute option flags for serialization
 *
 * CKStateChunk does not serialize ID lists in file mode. We keep internal
 * ID tracking for remap, but omit IDS from serialized options when FILE is set.
 */
static uint32_t chunk_compute_option_flags(const nmo_chunk_t *chunk) {
    uint32_t option_flags = chunk ? chunk->chunk_options : 0;

    /* Only serialize IDS when not in file mode */
    if ((option_flags & NMO_CHUNK_OPTION_FILE) == 0) {
        if (chunk && chunk->ids.count > 0) {
            option_flags |= NMO_CHUNK_OPTION_IDS;
        }
    } else {
        option_flags &= ~NMO_CHUNK_OPTION_IDS;
    }

    if (chunk && chunk->chunk_refs.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_CHN;
    }
    if (chunk && chunk->managers.count > 0) {
        option_flags |= NMO_CHUNK_OPTION_MAN;
    }

    return option_flags;
}

static int chunk_array_state_is_valid(const nmo_arena_array_t *array,
                                      size_t element_size) {
    if (array == NULL) return 0;
    if (array->count == 0) return 1;
    return array->data != NULL &&
           array->count <= array->capacity &&
           array->element_size == element_size &&
           array->count <= SIZE_MAX / element_size;
}

static nmo_status_t chunk_validate_serialized_array(
    const nmo_arena_array_t *array)
{
    if (array == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (array->count > UINT32_MAX) return NMO_ERR_INVALID_ARGUMENT;
    if (!chunk_array_state_is_valid(array, sizeof(uint32_t))) {
        return NMO_ERR_INVALID_STATE;
    }
    return NMO_OK;
}

static nmo_status_t chunk_validate_serializable(const nmo_chunk_t *chunk,
                                                uint32_t option_flags) {
    nmo_status_t result = chunk_validate_serialized_array(&chunk->data);
    if (result != NMO_OK) return result;
    if ((option_flags & NMO_CHUNK_OPTION_IDS) != 0) {
        result = chunk_validate_serialized_array(&chunk->ids);
        if (result != NMO_OK) return result;
    }
    if ((option_flags & NMO_CHUNK_OPTION_CHN) != 0) {
        result = chunk_validate_serialized_array(&chunk->chunk_refs);
        if (result != NMO_OK) return result;
    }
    if ((option_flags & NMO_CHUNK_OPTION_MAN) != 0) {
        result = chunk_validate_serialized_array(&chunk->managers);
        if (result != NMO_OK) return result;
    }
    return NMO_OK;
}

static int chunk_add_dwords(size_t *total_dwords, size_t count) {
    return nmo_safe_add_size(*total_dwords, count, total_dwords);
}

/**
 * @brief Calculate serialized size of a chunk
 */
static nmo_status_t chunk_calc_size(const nmo_chunk_t *chunk,
                                    size_t *out_size) {
    const uint32_t option_flags = chunk_compute_option_flags(chunk);
    size_t total_dwords = 2; /* Version info + chunk size. */

    if (!chunk_add_dwords(&total_dwords, chunk->data.count)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((option_flags & NMO_CHUNK_OPTION_IDS) != 0 &&
        (!chunk_add_dwords(&total_dwords, 1) ||
         !chunk_add_dwords(&total_dwords, chunk->ids.count))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((option_flags & NMO_CHUNK_OPTION_CHN) != 0 &&
        (!chunk_add_dwords(&total_dwords, 1) ||
         !chunk_add_dwords(&total_dwords, chunk->chunk_refs.count))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((option_flags & NMO_CHUNK_OPTION_MAN) != 0 &&
        (!chunk_add_dwords(&total_dwords, 1) ||
         !chunk_add_dwords(&total_dwords, chunk->managers.count))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!nmo_safe_mul_size(total_dwords, sizeof(uint32_t), out_size)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return NMO_OK;
}

static nmo_status_t chunk_build_subchunks_from_refs(nmo_chunk_t *chunk) {
    if (chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to subchunk builder");
    }

    if (chunk->chunk_refs.count == 0) {
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(nmo_chunk_start_read(chunk));

    const uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
    size_t ref_count = chunk->chunk_refs.count;

    for (size_t i = 0; i < ref_count; i++) {
        uint32_t ref = refs[i];

        if (ref == 0xFFFFFFFFu) {
            if (i + 1 >= ref_count) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Malformed sub-chunk sequence markers");
            }

            uint32_t seq_pos = refs[++i];
            if (seq_pos >= chunk->data.count) {
                continue;
            }

            nmo_status_t seek_result = nmo_chunk_goto(chunk, seq_pos);
            if (seek_result != NMO_OK) {
                continue;
            }

            size_t seq_count = 0;
            nmo_status_t seq_result =
                nmo_chunk_start_read_sub_chunk_sequence(chunk, &seq_count);
            if (seq_result != NMO_OK) {
                NMO_RETURN_ERROR(seq_result, NMO_SEVERITY_ERROR,
                                 "Failed to start sub-chunk sequence");
            }

            for (size_t s = 0; s < seq_count; s++) {
                nmo_chunk_t *sub = NULL;
                nmo_status_t read_result = nmo_chunk_read_sub_chunk(chunk, &sub);
                if (read_result != NMO_OK) {
                    NMO_RETURN_ERROR(read_result, NMO_SEVERITY_ERROR,
                                     "Failed to read sub-chunk");
                }

                if (sub != NULL) {
                    nmo_status_t append_result = nmo_arena_array_append(&chunk->chunks, &sub);
                    NMO_RETURN_IF_ERROR(append_result);

                    if (sub->chunk_refs.count > 0) {
                        nmo_status_t nested = chunk_build_subchunks_from_refs(sub);
                        NMO_RETURN_IF_ERROR(nested);
                    }
                }
            }

            continue;
        }

        if (ref >= chunk->data.count) {
            continue;
        }

        nmo_status_t seek_result = nmo_chunk_goto(chunk, ref);
        if (seek_result != NMO_OK) {
            continue;
        }

        nmo_chunk_t *sub = NULL;
        nmo_status_t read_result = nmo_chunk_read_sub_chunk(chunk, &sub);
        if (read_result != NMO_OK) {
            NMO_RETURN_ERROR(read_result, NMO_SEVERITY_ERROR,
                             "Failed to read sub-chunk");
        }

        if (sub != NULL) {
            nmo_status_t append_result = nmo_arena_array_append(&chunk->chunks, &sub);
            NMO_RETURN_IF_ERROR(append_result);

            if (sub->chunk_refs.count > 0) {
                nmo_status_t nested = chunk_build_subchunks_from_refs(sub);
                NMO_RETURN_IF_ERROR(nested);
            }
        }
    }

    return nmo_chunk_start_read(chunk);
}

static nmo_status_t chunk_validate_offset_list(const nmo_chunk_t *chunk,
                                               const nmo_arena_array_t *list,
                                               const char *label) {
    if (chunk == NULL || list == NULL || label == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to offset list validation");
    }

    (void)label;

    if (list->count == 0) {
        NMO_RETURN_OK();
    }

    if (chunk->data.count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Offset list present but chunk has no data");
    }
    if (list->data == NULL || chunk->data.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Offset list or chunk data is missing");
    }

    const uint32_t *entries = NMO_ARENA_ARRAY_DATA(uint32_t, list);
    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t entry_count = list->count;

    for (size_t i = 0; i < entry_count; i++) {
        uint32_t value = entries[i];

        if (value == 0xFFFFFFFFu) {
            if (i + 1 >= entry_count) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sequence marker missing offset in offset list");
            }

            uint32_t seq_pos = entries[++i];
            if (seq_pos >= chunk->data.count) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sequence offset out of bounds in offset list");
            }

            uint32_t seq_count = data[seq_pos];
            const size_t sequence_data_pos = (size_t)seq_pos + 1u;
            if (sequence_data_pos > chunk->data.count ||
                (size_t)seq_count > chunk->data.count - sequence_data_pos) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Sequence count exceeds chunk data bounds");
            }

            continue;
        }

        if (value >= chunk->data.count) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Offset entry out of bounds");
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize chunk recursively
 */
static nmo_status_t chunk_serialize_internal(const nmo_chunk_t *chunk, nmo_write_ctx_t *ctx) {
    nmo_status_t result;
    uint32_t option_flags = chunk_compute_option_flags(chunk);

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
    NMO_RETURN_IF_ERROR(result);

    /* Write chunk size in DWORDs */
    result = write_u32(ctx, (uint32_t) chunk->data.count);
    NMO_RETURN_IF_ERROR(result);

    /* Write data buffer */
    if (chunk->data.count > 0) {
        const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        result = write_bytes(ctx, data, chunk->data.count * 4);
        NMO_RETURN_IF_ERROR(result);
    }

    /* Write IDs list if present */
    if (option_flags & NMO_CHUNK_OPTION_IDS) {
        result = write_u32(ctx, (uint32_t) chunk->ids.count);
        NMO_RETURN_IF_ERROR(result);
        if (chunk->ids.count > 0) {
            const uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
            result = write_bytes(ctx, ids, chunk->ids.count * 4);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    /* Write sub-chunks if present */
    if (option_flags & NMO_CHUNK_OPTION_CHN) {
        result = write_u32(ctx, (uint32_t) chunk->chunk_refs.count);
        NMO_RETURN_IF_ERROR(result);
        if (chunk->chunk_refs.count > 0) {
            const uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
            result = write_bytes(ctx, refs, chunk->chunk_refs.count * 4);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    /* Write managers list if present */
    if (option_flags & NMO_CHUNK_OPTION_MAN) {
        result = write_u32(ctx, (uint32_t) chunk->managers.count);
        NMO_RETURN_IF_ERROR(result);
        if (chunk->managers.count > 0) {
            const uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->managers);
            result = write_bytes(ctx, managers, chunk->managers.count * 4);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Deserialize chunk recursively
 */
static nmo_status_t chunk_deserialize_internal(nmo_read_ctx_t *ctx, nmo_arena_t *arena, nmo_chunk_t **out_chunk) {
    nmo_status_t result;

    /* Create chunk */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (!chunk) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chunk");
    }

    /* Read version info */
    uint32_t version_info = 0;
    result = read_u32(ctx, &version_info);
    NMO_RETURN_IF_ERROR(result);

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
    NMO_RETURN_IF_ERROR(result);

    /* Read data buffer */
    size_t data_size = 0;
    if (!nmo_safe_mul_size((size_t) chunk_size_dwords,
                           sizeof(uint32_t), &data_size)) {
        return NMO_ERR_BUFFER_OVERRUN;
    }
    if (ctx->pos > ctx->size || data_size > ctx->size - ctx->pos) {
        return NMO_ERR_BUFFER_OVERRUN;
    }
    result = nmo_arena_array_resize(&chunk->data, chunk_size_dwords);
    NMO_RETURN_IF_ERROR(result);

    if (chunk_size_dwords > 0) {
        uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        result = read_bytes(ctx, data, data_size);
        NMO_RETURN_IF_ERROR(result);
    }

    /* Read IDs list if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_IDS) {
        uint32_t id_count = 0;
        result = read_u32(ctx, &id_count);
        NMO_RETURN_IF_ERROR(result);

        size_t ids_size = 0;
        if (!nmo_safe_mul_size((size_t) id_count,
                               sizeof(uint32_t), &ids_size)) {
            return NMO_ERR_BUFFER_OVERRUN;
        }
        if (ctx->pos > ctx->size || ids_size > ctx->size - ctx->pos) {
            return NMO_ERR_BUFFER_OVERRUN;
        }
        result = nmo_arena_array_resize(&chunk->ids, id_count);
        NMO_RETURN_IF_ERROR(result);

        if (id_count > 0) {
            uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
            result = read_bytes(ctx, ids, ids_size);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    /* Read sub-chunks if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) {
        uint32_t ref_count = 0;
        result = read_u32(ctx, &ref_count);
        NMO_RETURN_IF_ERROR(result);

        size_t refs_size = 0;
        if (!nmo_safe_mul_size((size_t) ref_count,
                               sizeof(uint32_t), &refs_size)) {
            return NMO_ERR_BUFFER_OVERRUN;
        }
        if (ctx->pos > ctx->size || refs_size > ctx->size - ctx->pos) {
            return NMO_ERR_BUFFER_OVERRUN;
        }
        result = nmo_arena_array_resize(&chunk->chunk_refs, ref_count);
        NMO_RETURN_IF_ERROR(result);

        if (ref_count > 0) {
            uint32_t *refs = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->chunk_refs);
            result = read_bytes(ctx, refs, refs_size);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    /* Read managers list if present */
    if (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) {
        uint32_t manager_count = 0;
        result = read_u32(ctx, &manager_count);
        NMO_RETURN_IF_ERROR(result);

        size_t managers_size = 0;
        if (!nmo_safe_mul_size((size_t) manager_count,
                               sizeof(uint32_t), &managers_size)) {
            return NMO_ERR_BUFFER_OVERRUN;
        }
        if (ctx->pos > ctx->size || managers_size > ctx->size - ctx->pos) {
            return NMO_ERR_BUFFER_OVERRUN;
        }
        result = nmo_arena_array_resize(&chunk->managers, manager_count);
        NMO_RETURN_IF_ERROR(result);

        if (manager_count > 0) {
            uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->managers);
            result = read_bytes(ctx, managers, managers_size);
            NMO_RETURN_IF_ERROR(result);
        }
    }

    *out_chunk = chunk;
    NMO_RETURN_OK();
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
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->owns_data = 1;
    chunk->arena = arena;
    chunk->file_context = NULL;

    return chunk;
}

/**
 * Serialize chunk to binary format
 */
nmo_status_t nmo_chunk_serialize(const nmo_chunk_t *chunk,
                                 void **out_data,
                                 size_t *out_size,
                                 nmo_arena_t *arena) {
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0;
    }

    /* Validate arguments */
    if (!chunk || !out_data || !out_size || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to chunk_serialize");
    }

    const uint32_t option_flags = chunk_compute_option_flags(chunk);
    nmo_status_t result = chunk_validate_serializable(chunk, option_flags);
    NMO_RETURN_IF_ERROR(result);

    /* Calculate total size */
    size_t total_size = 0;
    result = chunk_calc_size(chunk, &total_size);
    NMO_RETURN_IF_ERROR(result);

    /* Allocate output buffer */
    uint8_t *buffer = nmo_arena_alloc(arena, total_size, 4);
    if (!buffer) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate output buffer");
    }

    /* Setup write context */
    nmo_write_ctx_t ctx = {
        .buffer = buffer,
        .pos = 0,
        .size = total_size
    };

    /* Serialize chunk */
    result = chunk_serialize_internal(chunk, &ctx);
    NMO_RETURN_IF_ERROR(result);

    *out_data = buffer;
    *out_size = total_size;

    NMO_RETURN_OK();
}

/**
 * @brief Calculate serialized size for VERSION1 format
 */
static nmo_status_t chunk_calc_size_version1(const nmo_chunk_t *chunk,
                                             size_t *out_size) {
    size_t total_dwords = 0;
    uint32_t chunk_version = chunk->chunk_version;
    const size_t data_count = chunk->data.count;
    const uint32_t option_flags = chunk_compute_option_flags(chunk);
    const size_t id_count = (option_flags & NMO_CHUNK_OPTION_IDS) ? chunk->ids.count : 0;
    const size_t chunk_ref_count = chunk->chunk_refs.count;
    const size_t manager_count = chunk->managers.count;

    if (chunk_version < NMO_CHUNK_VERSION2) {
        /* VERSION1 header: version_info, class_id, chunk_size, reserved, id_count, chunk_count */
        total_dwords = 6;
        if (!chunk_add_dwords(&total_dwords, data_count) ||
            !chunk_add_dwords(&total_dwords, id_count) ||
            !chunk_add_dwords(&total_dwords, chunk_ref_count)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        goto finish;
    }

    if (chunk_version == NMO_CHUNK_VERSION2) {
        /* VERSION2 header adds manager_count */
        total_dwords = 7;
        if (!chunk_add_dwords(&total_dwords, data_count) ||
            !chunk_add_dwords(&total_dwords, id_count) ||
            !chunk_add_dwords(&total_dwords, chunk_ref_count) ||
            !chunk_add_dwords(&total_dwords, manager_count)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        goto finish;
    }

    /* VERSION3/VERSION4 compact header */
    const int has_ids = (option_flags & NMO_CHUNK_OPTION_IDS) != 0;
    const int has_chunks = (chunk->chunk_options & NMO_CHUNK_OPTION_CHN) || chunk_ref_count > 0;
    const int has_managers = (chunk->chunk_options & NMO_CHUNK_OPTION_MAN) || manager_count > 0;

    total_dwords = 2; /* version_info + chunk_size */
    if (!chunk_add_dwords(&total_dwords, data_count)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (has_ids) {
        if (!chunk_add_dwords(&total_dwords, 1) ||
            !chunk_add_dwords(&total_dwords, id_count)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    if (has_chunks) {
        if (!chunk_add_dwords(&total_dwords, 1) ||
            !chunk_add_dwords(&total_dwords, chunk_ref_count)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    if (has_managers) {
        if (!chunk_add_dwords(&total_dwords, 1) ||
            !chunk_add_dwords(&total_dwords, manager_count)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

finish:
    if (!nmo_safe_mul_size(
            total_dwords, sizeof(uint32_t), out_size)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return NMO_OK;
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
nmo_status_t nmo_chunk_serialize_version1(const nmo_chunk_t *chunk,
                                          void **out_data,
                                          size_t *out_size,
                                          nmo_arena_t *arena) {
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0;
    }

    /* Validate arguments */
    if (!chunk || !out_data || !out_size || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to chunk_serialize_version1");
    }

    const uint32_t serialized_options = chunk_compute_option_flags(chunk);
    nmo_status_t result = chunk_validate_serializable(
        chunk, serialized_options);
    NMO_RETURN_IF_ERROR(result);

    /* Calculate total size */
    size_t total_size = 0;
    result = chunk_calc_size_version1(chunk, &total_size);
    NMO_RETURN_IF_ERROR(result);

    /* Allocate output buffer */
    uint32_t *buffer = (uint32_t *) nmo_arena_alloc(arena, total_size, sizeof(uint32_t));
    if (!buffer) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate output buffer");
    }

    size_t pos = 0; /* Position in DWORDs */
    uint32_t chunk_version = chunk->chunk_version;
    const size_t data_count = chunk->data.count;
    const uint32_t option_flags = chunk_compute_option_flags(chunk);
    const size_t id_count = (option_flags & NMO_CHUNK_OPTION_IDS) ? chunk->ids.count : 0;
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
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has data_size but no data");
            }
            memcpy(&buffer[pos], data_u32, data_count * sizeof(uint32_t));
            pos += data_count;
        }

        if (id_count > 0) {
            if (!ids_u32) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has ID count but no ID list");
            }
            memcpy(&buffer[pos], ids_u32, id_count * sizeof(uint32_t));
            pos += id_count;
        }

        if (chunk_ref_count > 0) {
            if (!refs_u32) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has chunk references but no data");
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
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has data_size but no data");
            }
            memcpy(&buffer[pos], data_u32, data_count * sizeof(uint32_t));
            pos += data_count;
        }

        if (id_count > 0) {
            if (!ids_u32) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has ID count but no ID list");
            }
            memcpy(&buffer[pos], ids_u32, id_count * sizeof(uint32_t));
            pos += id_count;
        }

        if (chunk_ref_count > 0) {
            if (!refs_u32) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has chunk references but no data");
            }
            memcpy(&buffer[pos], refs_u32, chunk_ref_count * sizeof(uint32_t));
            pos += chunk_ref_count;
        }

        if (manager_count > 0) {
            if (!mgrs_u32) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has manager count but no manager data");
            }
            memcpy(&buffer[pos], mgrs_u32, manager_count * sizeof(uint32_t));
            pos += manager_count;
        }
    } else {
        /* VERSION3/VERSION4 compact layout */
        uint32_t option_flags = chunk_compute_option_flags(chunk);

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
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has data_size but no data");
            }
            memcpy(&buffer[pos], data_u32, data_count * sizeof(uint32_t));
            pos += data_count;
        }

        if (option_flags & NMO_CHUNK_OPTION_IDS) {
            buffer[pos++] = (uint32_t) id_count;
            if (id_count > 0) {
                if (!ids_u32) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has ID count but no ID list");
                }
                memcpy(&buffer[pos], ids_u32, id_count * sizeof(uint32_t));
                pos += id_count;
            }
        }

        if (option_flags & NMO_CHUNK_OPTION_CHN) {
            buffer[pos++] = (uint32_t) chunk_ref_count;
            if (chunk_ref_count > 0) {
                if (!refs_u32) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has chunk references but no data");
                }
                memcpy(&buffer[pos], refs_u32, chunk_ref_count * sizeof(uint32_t));
                pos += chunk_ref_count;
            }
        }

        if (option_flags & NMO_CHUNK_OPTION_MAN) {
            buffer[pos++] = (uint32_t) manager_count;
            if (manager_count > 0) {
                if (!mgrs_u32) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk has manager count but no manager data");
                }
                memcpy(&buffer[pos], mgrs_u32, manager_count * sizeof(uint32_t));
                pos += manager_count;
            }
        }
    }

    *out_data = buffer;
    *out_size = total_size;

    NMO_RETURN_OK();
}

/**
 * Deserialize chunk from binary format
 */
nmo_status_t nmo_chunk_deserialize(const void *data,
                                   size_t size,
                                   nmo_arena_t *arena,
                                   nmo_chunk_t **out_chunk) {
    if (out_chunk != NULL) {
        *out_chunk = NULL;
    }

    /* Validate arguments */
    if (!data || !arena || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to chunk_deserialize");
    }

    if (size < 8) {
        /* Minimum size: version info + chunk size */
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for chunk");
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
    if (!chunk_array_state_is_valid(&src->data, sizeof(uint32_t)) ||
        !chunk_array_state_is_valid(&src->ids, sizeof(uint32_t)) ||
        !chunk_array_state_is_valid(&src->chunk_refs, sizeof(uint32_t)) ||
        !chunk_array_state_is_valid(&src->managers, sizeof(uint32_t)) ||
        !chunk_array_state_is_valid(&src->chunks, sizeof(nmo_chunk_t *)) ||
        (src->raw_size > 0 && src->raw_data == NULL)) {
        nmo_last_error_setf(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Cannot clone malformed chunk state");
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
    clone->file_context = src->file_context;

    clone->raw_data = src->raw_data;
    clone->raw_size = src->raw_size;

    if (src->data.count > 0) {
        nmo_status_t result = nmo_arena_array_resize(&clone->data, src->data.count);
        NMO_RETURN_NULL_IF_ERROR(result);
        memcpy(clone->data.data, src->data.data, src->data.count * sizeof(uint32_t));
    }

    if (src->ids.count > 0) {
        nmo_status_t result = nmo_arena_array_resize(&clone->ids, src->ids.count);
        NMO_RETURN_NULL_IF_ERROR(result);
        memcpy(clone->ids.data, src->ids.data, src->ids.count * sizeof(uint32_t));
    }

    if (src->chunk_refs.count > 0) {
        nmo_status_t result = nmo_arena_array_resize(&clone->chunk_refs, src->chunk_refs.count);
        NMO_RETURN_NULL_IF_ERROR(result);
        memcpy(clone->chunk_refs.data, src->chunk_refs.data, src->chunk_refs.count * sizeof(uint32_t));
    }

    if (src->managers.count > 0) {
        nmo_status_t result = nmo_arena_array_resize(&clone->managers, src->managers.count);
        NMO_RETURN_NULL_IF_ERROR(result);
        memcpy(clone->managers.data, src->managers.data, src->managers.count * sizeof(uint32_t));
    }

    if (src->chunks.count > 0) {
        nmo_status_t result = nmo_arena_array_resize(&clone->chunks, src->chunks.count);
        NMO_RETURN_NULL_IF_ERROR(result);

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

void nmo_chunk_set_file_context(nmo_chunk_t *chunk,
                                const struct nmo_chunk_file_context *ctx) {
    if (chunk == NULL) {
        return;
    }
    chunk->file_context = ctx;
    if (ctx != NULL) {
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    }
}

const struct nmo_chunk_file_context *nmo_chunk_get_file_context(const nmo_chunk_t *chunk) {
    return chunk ? chunk->file_context : NULL;
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
static nmo_status_t chunk_parse_into(nmo_chunk_t *chunk,
                                     const void *data,
                                     size_t size) {
    if (chunk == NULL || data == NULL || size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_chunk_parse");
    }

    if ((size % sizeof(uint32_t)) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Chunk buffer size must be DWORD aligned");
    }

    if (((uintptr_t)data % sizeof(uint32_t)) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Chunk buffer must be 4-byte aligned");
    }

    /* Store raw data for round-trip saving */
    chunk->raw_data = data;
    chunk->raw_size = size;

    const uint32_t *buf = (const uint32_t *) data;
    size_t pos = 0; /* Position in DWORDs */
    size_t size_dwords = size / sizeof(uint32_t);

    if (size_dwords < 1) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk header");
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
        if (!dword_range_fits(pos, 5u, size_dwords)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for VERSION1 header");
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
            if (!dword_range_fits(pos, (size_t) chunk_size, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk data");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->data, chunk_size);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->data.data, &buf[pos], chunk_size * sizeof(uint32_t));
            pos += chunk_size;
        }

        /* Allocate and read IDs */
        if (id_count > 0) {
            if (!dword_range_fits(pos, (size_t) id_count, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for ID array");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->ids, id_count);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->ids.data, &buf[pos], id_count * sizeof(uint32_t));
            pos += id_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }

        /* Read sub-chunk positions */
        if (chunk_count > 0) {
            if (!dword_range_fits(pos, (size_t) chunk_count, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk array");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->chunk_refs, chunk_count);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->chunk_refs.data, &buf[pos], chunk_count * sizeof(uint32_t));
            pos += chunk_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
        }
    } else if (chunk_version == NMO_CHUNK_VERSION2) {
        /* CHUNK_VERSION2 format (adds manager data) */
        if (!dword_range_fits(pos, 5u, size_dwords)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for VERSION2 header");
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
            if (!dword_range_fits(pos, (size_t) chunk_size, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk data");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->data, chunk_size);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->data.data, &buf[pos], chunk_size * sizeof(uint32_t));
            pos += chunk_size;
        }

        /* Read IDs, chunks, and managers same as VERSION1 */
        if (id_count > 0) {
            if (!dword_range_fits(pos, (size_t) id_count, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for ID array");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->ids, id_count);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->ids.data, &buf[pos], id_count * sizeof(uint32_t));
            pos += id_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
        }

        if (chunk_count > 0) {
            if (!dword_range_fits(pos, (size_t) chunk_count, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk array");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->chunk_refs, chunk_count);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->chunk_refs.data, &buf[pos], chunk_count * sizeof(uint32_t));
            pos += chunk_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_CHN;
        }

        if (manager_count > 0) {
            if (!dword_range_fits(pos, (size_t) manager_count, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for manager array");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->managers, manager_count);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->managers.data, &buf[pos], manager_count * sizeof(uint32_t));
            pos += manager_count;
            chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
        }
    } else if (chunk_version <= NMO_CHUNK_VERSION4) {
        /* CHUNK_VERSION3/VERSION4 format (modern, compact header with options) */
        /* Extract chunk options and class ID from packed version field */
        uint8_t chunk_options = (uint8_t) ((packed_chunk_version & 0xFF00) >> 8);
        chunk->chunk_class_id = (uint8_t) ((packed_data_version & 0xFF00) >> 8);
        chunk->chunk_options = chunk_options |
            (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);

        /* Use 8-bit class ID as the best available class_id in VERSION3/4 */
        chunk->class_id = (uint32_t) chunk->chunk_class_id;

        /* Re-extract actual versions (low bytes only) */
        chunk->data_version = (uint8_t) (packed_data_version & 0x00FF);
        chunk->chunk_version = (uint8_t) (packed_chunk_version & 0x00FF);

        /* Read chunk size */
        if (pos >= size_dwords) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk size");
        }
        uint32_t chunk_size = buf[pos++];

        /* Allocate and read data buffer */
        if (chunk_size > 0) {
            if (!dword_range_fits(pos, (size_t) chunk_size, size_dwords)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk data");
            }

            nmo_status_t result = nmo_arena_array_resize(&chunk->data, chunk_size);
            NMO_RETURN_IF_ERROR(result);

            memcpy(chunk->data.data, &buf[pos], chunk_size * sizeof(uint32_t));
            pos += chunk_size;
        }

        /* Read optional sections based on chunk_options flags */
        if (chunk_options & NMO_CHUNK_OPTION_IDS) {
            if (pos >= size_dwords) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for ID count");
            }
            uint32_t id_count = buf[pos++];

            if (id_count > 0) {
                if (!dword_range_fits(pos, (size_t) id_count, size_dwords)) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for ID array");
                }

                nmo_status_t result = nmo_arena_array_resize(&chunk->ids, id_count);
                NMO_RETURN_IF_ERROR(result);

                memcpy(chunk->ids.data, &buf[pos], id_count * sizeof(uint32_t));
                pos += id_count;
            }
        }

        if (chunk_options & NMO_CHUNK_OPTION_CHN) {
            if (pos >= size_dwords) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk count");
            }
            uint32_t chunk_count = buf[pos++];

            if (chunk_count > 0) {
                if (!dword_range_fits(pos, (size_t) chunk_count, size_dwords)) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for chunk array");
                }
                nmo_status_t result = nmo_arena_array_resize(&chunk->chunk_refs, chunk_count);
                NMO_RETURN_IF_ERROR(result);

                memcpy(chunk->chunk_refs.data, &buf[pos], chunk_count * sizeof(uint32_t));
                pos += chunk_count;
            }
        }

        if (chunk_options & NMO_CHUNK_OPTION_MAN) {
            if (pos >= size_dwords) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for manager count");
            }
            uint32_t manager_count = buf[pos++];

            if (manager_count > 0) {
                if (!dword_range_fits(pos, (size_t) manager_count, size_dwords)) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Buffer too small for manager array");
                }

                nmo_status_t result = nmo_arena_array_resize(&chunk->managers, manager_count);
                NMO_RETURN_IF_ERROR(result);

                memcpy(chunk->managers.data, &buf[pos], manager_count * sizeof(uint32_t));
                pos += manager_count;
            }
        }
    } else {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR, "Unsupported chunk version");
    }

    if (chunk->chunk_refs.count > 0) {
        nmo_status_t validate_refs = chunk_validate_offset_list(chunk, &chunk->chunk_refs,
                                                                "chunk_refs");
        NMO_RETURN_IF_ERROR(validate_refs);

        nmo_status_t sub_result = chunk_build_subchunks_from_refs(chunk);
        NMO_RETURN_IF_ERROR(sub_result);
    }

    if (chunk->ids.count > 0) {
        nmo_status_t validate_ids = chunk_validate_offset_list(chunk, &chunk->ids, "ids");
        NMO_RETURN_IF_ERROR(validate_ids);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_parse(nmo_chunk_t *chunk, const void *data, size_t size) {
    if (chunk == NULL || chunk->arena == NULL || data == NULL || size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_chunk_parse");
    }
    if ((size % sizeof(uint32_t)) != 0 ||
        ((uintptr_t)data % sizeof(uint32_t)) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Chunk buffer must be DWORD aligned");
    }

    nmo_chunk_t *staged = nmo_chunk_create(chunk->arena);
    if (staged == NULL) {
        return NMO_ERR_NOMEM;
    }
    staged->file_context = chunk->file_context;
    staged->chunk_options = chunk->chunk_options & NMO_CHUNK_OPTION_FILE;

    nmo_status_t result = chunk_parse_into(staged, data, size);
    if (result != NMO_OK) {
        return result;
    }

    *chunk = *staged;
    return NMO_OK;
}

/**
 * Get chunk header
 */
nmo_status_t nmo_chunk_get_header(const nmo_chunk_t *chunk, nmo_chunk_header_t *out_header) {
    if (!chunk || !out_header) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }
    memset(out_header, 0, sizeof(*out_header));
    if (chunk->data.count > UINT32_MAX / sizeof(uint32_t) ||
        chunk->chunk_refs.count > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Chunk header fields do not fit the 32-bit format");
    }
    if (chunk->data.count > 0 && chunk->data.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Chunk has data size but no data");
    }

    uint32_t option_flags = chunk_compute_option_flags(chunk);

    out_header->chunk_id = chunk->class_id;
    out_header->chunk_size =
        (uint32_t)chunk->data.count * (uint32_t)sizeof(uint32_t);
    out_header->sub_chunk_count = (uint32_t) chunk->chunk_refs.count;
    out_header->flags = option_flags;

    NMO_RETURN_OK();
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

    if (chunk->data.count > SIZE_MAX / sizeof(uint32_t) ||
        (chunk->data.count > 0 && chunk->data.data == NULL)) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) {
        *out_size = chunk->data.count * sizeof(uint32_t);
    }
    return chunk->data.data;
}

// =============================================================================
// Internal Helpers
// =============================================================================

static nmo_chunk_parser_state_t *get_parser_state(nmo_chunk_t *chunk) {
    if (!chunk) return NULL;

    if (!chunk->parser_state) {
        if (!chunk->arena) {
            return NULL;
        }

        chunk->parser_state = nmo_arena_alloc(chunk->arena,
                                              sizeof(nmo_chunk_parser_state_t),
                                              _Alignof(nmo_chunk_parser_state_t));
        if (!chunk->parser_state) return NULL;
        memset(chunk->parser_state, 0, sizeof(nmo_chunk_parser_state_t));
    }

    return (nmo_chunk_parser_state_t *) chunk->parser_state;
}

// =============================================================================
// Lifecycle Management
// =============================================================================

nmo_status_t nmo_chunk_start_read(nmo_chunk_t *chunk) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate parser state");
    }

    state->current_pos = 0;
    state->prev_identifier_pos = 0; // Reset to beginning
    state->data_size = chunk->data.count;
    state->writing = 0;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_start_write(nmo_chunk_t *chunk) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (!state) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                               "Failed to allocate parser state");
    }

    state->current_pos = 0;
    state->prev_identifier_pos = 0;
    state->data_size = 0;
    state->writing = 1;

    /* Reset logical size; keep capacity */
    chunk->data.count = 0;

    /* CK2 behavior: StartWrite() sets m_ChunkVersion = CHUNK_VERSION4 (7) */
    chunk->chunk_version = NMO_CHUNK_VERSION4;

    NMO_RETURN_OK();
}

void nmo_chunk_close(nmo_chunk_t *chunk) {
    if (chunk) {
        nmo_chunk_update_data_size(chunk);
    }
}

void nmo_chunk_clear(nmo_chunk_t *chunk) {
    if (chunk) {
        chunk->class_id = 0;
        chunk->chunk_class_id = 0;
        chunk->data_version = 0;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->chunk_options = 0;

        chunk->data.count = 0;
        chunk->ids.count = 0;
        chunk->chunks.count = 0;
        chunk->chunk_refs.count = 0;
        chunk->managers.count = 0;

        chunk->uncompressed_size = 0;
        chunk->compressed_size = 0;
        chunk->is_compressed = 0;
        chunk->unpack_size = 0;

        chunk->raw_data = NULL;
        chunk->raw_size = 0;
        chunk->file_context = NULL;

        if (chunk->parser_state) {
            nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
            state->current_pos = 0;
            state->prev_identifier_pos = 0;
            state->data_size = 0;
            state->writing = 0;
        }
    }
}

// =============================================================================
// Metadata Access
// =============================================================================

uint32_t nmo_chunk_get_class_id(const nmo_chunk_t *chunk) {
    return chunk ? chunk->class_id : 0;
}

uint32_t nmo_chunk_get_data_version(const nmo_chunk_t *chunk) {
    return chunk ? chunk->data_version : 0;
}

void nmo_chunk_set_data_version(nmo_chunk_t *chunk, uint32_t version) {
    if (chunk) {
        chunk->data_version = version;
    }
}

uint32_t nmo_chunk_get_chunk_version(const nmo_chunk_t *chunk) {
    return chunk ? chunk->chunk_version : 0;
}

size_t nmo_chunk_get_data_size(const nmo_chunk_t *chunk) {
    return chunk ? (chunk->data.count * sizeof(uint32_t)) : 0;
}

uint32_t nmo_chunk_get_size(const nmo_chunk_t *chunk) {
    return (uint32_t)nmo_chunk_get_data_size(chunk);
}

void nmo_chunk_update_data_size(nmo_chunk_t *chunk) {
    if (!chunk) return;

    nmo_chunk_parser_state_t *state = get_parser_state(chunk);
    if (state && state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
        state->data_size = chunk->data.count;
    }
}

/**
 * Check if chunk is compressed
 */
int nmo_chunk_is_compressed(const nmo_chunk_t *chunk) {
    if (!chunk) {
        return 0;
    }
    return (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) != 0;
}

// =============================================================================
// Navigation
// =============================================================================

size_t nmo_chunk_get_position(const nmo_chunk_t *chunk) {
    if (!chunk) return (size_t)-1;

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state((nmo_chunk_t *) chunk);
    return state ? state->current_pos : (size_t)-1;
}

size_t nmo_chunk_get_remaining(const nmo_chunk_t *chunk) {
    if (chunk == NULL || chunk->parser_state == NULL) {
        return 0;
    }

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    if (state->writing) {
        return 0;
    }

    size_t readable_dwords = chunk->data.count;
    if (state->data_size < readable_dwords) {
        readable_dwords = state->data_size;
    }
    if (state->current_pos >= readable_dwords) {
        return 0;
    }
    return readable_dwords - state->current_pos;
}

int nmo_chunk_is_at_end(const nmo_chunk_t *chunk) {
    return nmo_chunk_get_remaining(chunk) == 0u;
}

nmo_status_t nmo_chunk_goto(nmo_chunk_t *chunk, size_t pos) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "No parser state");
    }

    size_t limit = chunk->data.count;
    if (!state->writing && state->data_size < limit) {
        limit = state->data_size;
    }
    if (pos > limit) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Cannot seek beyond chunk bounds");
    }

    state->current_pos = pos;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_skip(nmo_chunk_t *chunk, size_t dwords) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "No parser state");
    }

    if (dwords > SIZE_MAX - state->current_pos) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Skip overflow");
    }

    if (dwords == 0u) {
        NMO_RETURN_OK();
    }

    size_t new_pos = state->current_pos + dwords;

    if (!state->writing) {
        if (!nmo_chunk_has_read_capacity(chunk, dwords)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Cannot skip beyond readable data");
        }
        state->current_pos = new_pos;
        NMO_RETURN_OK();
    }

    if (dwords > SIZE_MAX / sizeof(uint32_t)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Skip size overflow");
    }

    /* CK2 behavior: Skip calls CheckSize to ensure capacity for writes. */
    nmo_status_t result = nmo_chunk_check_size(chunk, dwords * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memset(&data[state->current_pos], 0, dwords * sizeof(uint32_t));
    state->current_pos = new_pos;
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }
    NMO_RETURN_OK();
}

// =============================================================================
// Memory Management
// =============================================================================

nmo_status_t nmo_chunk_check_size(nmo_chunk_t *chunk, size_t needed_bytes) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Chunk not in write mode");
    }
    if (!state->writing) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Chunk is not in write mode");
    }

    size_t needed_dwords = needed_bytes / sizeof(uint32_t);
    if (needed_bytes % sizeof(uint32_t) != 0) {
        needed_dwords++;
    }
    if (needed_dwords > SIZE_MAX - state->current_pos) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_OFFSET, NMO_SEVERITY_ERROR,
                         "Chunk capacity request overflow");
    }
    size_t required_size = state->current_pos + needed_dwords;
    if (required_size > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Chunk data size does not fit the 32-bit format");
    }

    if (required_size > state->data_size) {
        size_t grow = needed_dwords;
        if (grow < 500) {
            grow = 500;
        }

        size_t new_size = required_size;
        if (grow <= SIZE_MAX - state->current_pos) {
            new_size = state->current_pos + grow;
        }

        /* Ensure reserve preserves everything up to the current cursor. */
        if (chunk->data.count < state->current_pos) {
            chunk->data.count = state->current_pos;
        }

        if (new_size > chunk->data.capacity) {
            nmo_status_t reserve_result = nmo_arena_array_reserve(&chunk->data, new_size);
            NMO_RETURN_IF_ERROR(reserve_result);
        }

        state->data_size = new_size;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_lock_write_buffer(
    nmo_chunk_t *chunk,
    size_t dword_count,
    uint32_t **out_data)
{
    if (out_data != NULL) {
        *out_data = NULL;
    }
    NMO_CHUNK_CHECK_ARGS(chunk, out_data, "Invalid direct write buffer arguments");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state == NULL || !state->writing) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                               "Chunk is not in write mode");
    }
    if (dword_count == 0u) {
        NMO_RETURN_OK();
    }
    if (dword_count > SIZE_MAX / sizeof(uint32_t)) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Direct write buffer size overflow");
    }

    const size_t start_pos = state->current_pos;
    nmo_status_t status = nmo_chunk_check_size(
        chunk, dword_count * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(status);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memset(&data[start_pos], 0, dword_count * sizeof(uint32_t));
    state->current_pos = start_pos + dword_count;
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }
    *out_data = &data[start_pos];
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_lock_read_buffer(
    nmo_chunk_t *chunk,
    size_t dword_count,
    const uint32_t **out_data)
{
    if (out_data != NULL) {
        *out_data = NULL;
    }
    NMO_CHUNK_CHECK_ARGS(chunk, out_data, "Invalid direct read buffer arguments");

    const nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state == NULL || state->writing) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                               "Chunk is not in read mode");
    }
    if (dword_count == 0u) {
        NMO_RETURN_OK();
    }
    if (!nmo_chunk_has_read_capacity(chunk, dword_count)) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                               "Cannot lock beyond readable data");
    }

    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    *out_data = &data[state->current_pos];
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_reserve_dwords(
    nmo_chunk_t *chunk,
    size_t dword_count,
    nmo_chunk_patch_token_t *out_token)
{
    if (out_token != NULL) {
        *out_token = NMO_CHUNK_PATCH_TOKEN_INVALID;
    }
    NMO_CHUNK_CHECK_ARGS(chunk, out_token, "Invalid chunk reservation arguments");
    if (dword_count == 0u) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Cannot reserve an empty DWORD span");
    }

    const size_t offset = nmo_chunk_get_position(chunk);
    uint32_t *reserved = NULL;
    nmo_status_t status = nmo_chunk_lock_write_buffer(
        chunk, dword_count, &reserved);
    NMO_RETURN_IF_ERROR(status);

    out_token->chunk = chunk;
    out_token->offset = offset;
    out_token->dword_count = dword_count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_reserve_u32(
    nmo_chunk_t *chunk,
    nmo_chunk_patch_token_t *out_token)
{
    return nmo_chunk_reserve_dwords(chunk, 1u, out_token);
}

nmo_status_t nmo_chunk_reserve_u64(
    nmo_chunk_t *chunk,
    nmo_chunk_patch_token_t *out_token)
{
    return nmo_chunk_reserve_dwords(chunk, 2u, out_token);
}

nmo_status_t nmo_chunk_patch_dwords(
    nmo_chunk_t *chunk,
    nmo_chunk_patch_token_t token,
    const uint32_t *values,
    size_t dword_count)
{
    if (chunk == NULL || values == NULL || dword_count == 0u) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Invalid chunk patch arguments");
    }
    if (dword_count > SIZE_MAX / sizeof(uint32_t)) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Chunk patch size overflow");
    }
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state == NULL || !state->writing) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                               "Chunk is not in write mode");
    }
    if (!nmo_chunk_patch_token_is_valid(token) || token.chunk != chunk ||
        token.dword_count != dword_count || token.offset > chunk->data.count ||
        dword_count > chunk->data.count - token.offset) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Invalid chunk patch token");
    }

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    memcpy(&data[token.offset], values, dword_count * sizeof(uint32_t));
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_patch_u32(
    nmo_chunk_t *chunk,
    nmo_chunk_patch_token_t token,
    uint32_t value)
{
    return nmo_chunk_patch_dwords(chunk, token, &value, 1u);
}

nmo_status_t nmo_chunk_patch_u64(
    nmo_chunk_t *chunk,
    nmo_chunk_patch_token_t token,
    uint64_t value)
{
    const uint32_t dwords[2] = {
        (uint32_t)(value & UINT64_C(0xFFFFFFFF)),
        (uint32_t)(value >> 32)
    };
    return nmo_chunk_patch_dwords(chunk, token, dwords, 2u);
}

// =============================================================================
// Identifiers
// =============================================================================

nmo_status_t nmo_chunk_write_identifier(nmo_chunk_t *chunk, uint32_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    /* CK2 behavior: Calls StartWrite() if no parser state */
    if (!chunk->parser_state) {
        nmo_status_t start_result = nmo_chunk_start_write(chunk);
        NMO_RETURN_IF_ERROR(start_result);
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state->current_pos > (size_t)UINT32_MAX - 2u) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Identifier entry does not fit the 32-bit chunk format");
    }
    if (state->prev_identifier_pos < state->current_pos &&
        state->prev_identifier_pos + 1u >= chunk->data.count) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                               "Previous identifier position is out of bounds");
    }

    nmo_status_t result = nmo_chunk_check_size(chunk, 2 * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    if (state->prev_identifier_pos < state->current_pos) {
        data[state->prev_identifier_pos + 1] = (uint32_t) state->current_pos;
    }

    data[state->current_pos++] = id;
    data[state->current_pos++] = 0;
    state->prev_identifier_pos = state->current_pos - 2;

    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_identifier(nmo_chunk_t *chunk, uint32_t *out_id) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_id, "Invalid arguments");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state || state->current_pos >= chunk->data.count) {
        *out_id = 0;
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_INFO,
                         "No identifier available at current position");
    }

    if (state->current_pos + 1 >= chunk->data.count) {
        *out_id = 0;
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Truncated identifier entry");
    }

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    *out_id = data[state->current_pos];

    state->prev_identifier_pos = state->current_pos;
    state->current_pos += 2;

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_seek_identifier(nmo_chunk_t *chunk, uint32_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    // Empty chunk cannot have identifiers
    if (chunk->data.count == 0 || chunk->data.data == NULL) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_INFO,
                               "Identifier not found in empty chunk");
    }

    /* CK2 behavior: Creates parser if NULL */
    if (!chunk->parser_state) {
        chunk->parser_state = nmo_arena_alloc(chunk->arena,
                                               sizeof(nmo_chunk_parser_state_t),
                                               _Alignof(nmo_chunk_parser_state_t));
        if (!chunk->parser_state) {
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                   "Failed to allocate parser state");
        }
        memset(chunk->parser_state, 0, sizeof(nmo_chunk_parser_state_t));
        ((nmo_chunk_parser_state_t *)chunk->parser_state)->data_size = chunk->data.count;
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    size_t start_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        start_pos = data[state->prev_identifier_pos + 1];
    }

    size_t current_pos = start_pos;
    if (current_pos != 0) {
        size_t guard = 0;
        while (current_pos < chunk->data.count && data[current_pos] != id) {
            if (current_pos + 1 >= chunk->data.count) {
                NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                       "Corrupt identifier chain");
            }
            current_pos = data[current_pos + 1];
            if (current_pos == 0) {
                break;
            }
            if (++guard > chunk->data.count) {
                NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                                       "Identifier chain cycle detected");
            }
        }

        if (current_pos != 0 && current_pos < chunk->data.count) {
            if (current_pos + 1 >= chunk->data.count) {
                NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                       "Truncated identifier entry");
            }
            state->prev_identifier_pos = current_pos;
            state->current_pos = current_pos + 2;
            NMO_RETURN_OK();
        }
    }

    current_pos = 0;
    size_t guard = 0;
    while (current_pos < chunk->data.count && data[current_pos] != id) {
        if (current_pos + 1 >= chunk->data.count) {
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                   "Corrupt identifier chain");
        }
        current_pos = data[current_pos + 1];
        if (current_pos == start_pos) {
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_INFO,
                                   "Identifier not found");
        }
        if (++guard > chunk->data.count) {
            NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                                   "Identifier chain cycle detected");
        }
    }

    if (current_pos >= chunk->data.count) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_INFO,
                               "Identifier not found");
    }

    if (current_pos + 1 >= chunk->data.count) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                               "Truncated identifier entry");
    }

    state->prev_identifier_pos = current_pos;
    state->current_pos = current_pos + 2;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_seek_identifier_with_size(
    nmo_chunk_t *chunk,
    uint32_t id,
    size_t *out_size)
{
    if (out_size != NULL) {
        *out_size = 0u;
    }
    NMO_CHUNK_CHECK_ARGS(chunk, out_size, "Invalid identifier size arguments");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (state == NULL || state->writing) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                               "Chunk is not in read mode");
    }

    const size_t saved_pos = state->current_pos;
    const size_t saved_prev = state->prev_identifier_pos;
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, id);
    if (result != NMO_OK) {
        state->current_pos = saved_pos;
        state->prev_identifier_pos = saved_prev;
        return result;
    }

    const size_t identifier_pos = state->prev_identifier_pos;
    const size_t payload_pos = state->current_pos;
    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    const size_t next_pos = data[identifier_pos + 1u];
    if (next_pos != 0u &&
        (next_pos < payload_pos || next_pos > chunk->data.count - 2u)) {
        state->current_pos = saved_pos;
        state->prev_identifier_pos = saved_prev;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Invalid next identifier position");
    }

    *out_size = next_pos == 0u
        ? chunk->data.count - payload_pos
        : next_pos - payload_pos;
    NMO_RETURN_OK();
}

// =============================================================================
// Object Sequences
// =============================================================================

nmo_status_t nmo_chunk_write_object_sequence_start(nmo_chunk_t *chunk, size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    if (count > (size_t)INT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Object sequence count does not fit the signed 32-bit format");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                               "Parser state not initialized");
    }

    /* CK2 behavior: Only track when count > 0 AND not in file mode */
    const nmo_chunk_file_context_t *ctx = NULL;
    if (chunk != NULL && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        ctx = chunk->file_context;
    }
    const int in_file_context = (ctx != NULL && ctx->runtime_to_file != NULL);

    nmo_status_t result = nmo_chunk_check_size(chunk, sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    if (count > 0 && !in_file_context) {
        if (state->current_pos > UINT32_MAX) {
            NMO_CHUNK_RETURN_INVALID_ARGUMENT(
                "Object sequence position does not fit the 32-bit format");
        }
        result = nmo_arena_array_ensure_space(&chunk->ids, 2u);
        NMO_RETURN_IF_ERROR(result);

        /* CK2: AddEntries adds -1 marker followed by position */
        uint32_t sentinel = 0xFFFFFFFFu;
        nmo_status_t list_result = nmo_arena_array_append(&chunk->ids, &sentinel);
        NMO_RETURN_IF_ERROR(list_result);

        uint32_t pos = (uint32_t) state->current_pos;
        list_result = nmo_arena_array_append(&chunk->ids, &pos);
        NMO_RETURN_IF_ERROR(list_result);

        /* Set IDS option since we added tracking entries */
        chunk->chunk_options |= NMO_CHUNK_OPTION_IDS;
    }

    /* Write count */
    return nmo_chunk_write_int(chunk, (int32_t) count);
}

nmo_status_t nmo_chunk_write_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t id) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    // Sequence items should not add entries to the IDs list (CK2 behavior)
    const nmo_chunk_file_context_t *ctx = NULL;
    if (chunk != NULL && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) {
        ctx = chunk->file_context;
    }

    uint32_t encoded_value = (uint32_t) id;
    if (ctx != NULL && ctx->runtime_to_file != NULL) {
        if (id == 0) {
            encoded_value = NMO_OBJECT_ID_INVALID;
        } else {
            nmo_object_id_t unresolved_raw = NMO_OBJECT_ID_NONE;
            if (ctx->repository != NULL &&
                nmo_object_repository_get_unresolved_ref_raw(
                    ctx->repository, id, &unresolved_raw)) {
                encoded_value = (uint32_t)unresolved_raw;
                return nmo_chunk_write_int(chunk, (int32_t)encoded_value);
            }
            nmo_object_id_t file_id = 0;
            if (nmo_id_remap_lookup_id(ctx->runtime_to_file, id, &file_id) == NMO_OK) {
                encoded_value = (uint32_t) file_id;
            } else {
                NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                 "Cannot serialize unmapped runtime object sequence ID %u",
                                 (unsigned)id);
            }
        }
    }

    return nmo_chunk_write_int(chunk, (int32_t) encoded_value);
}

nmo_status_t nmo_chunk_write_raw_object_sequence_item(
    nmo_chunk_t *chunk,
    nmo_object_id_t raw_id)
{
    return nmo_chunk_write_int(chunk, (int32_t)raw_id);
}

nmo_status_t nmo_chunk_read_object_sequence_start(nmo_chunk_t *chunk, size_t *out_count) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_count, "Invalid arguments");
    *out_count = 0;
    size_t start_pos = nmo_chunk_get_position(chunk);

    int32_t count;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count);
    NMO_RETURN_IF_ERROR(result);
    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);

    if (count < 0) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                               "Object sequence count cannot be negative");
    }

    if (!nmo_chunk_has_read_capacity(chunk, (size_t)count)) {
        state->current_pos = start_pos;
        NMO_CHUNK_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                               "Object sequence count exceeds remaining DWORDs");
    }

    *out_count = (size_t) count;
    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_object_sequence_item(nmo_chunk_t *chunk, nmo_object_id_t *out_id) {
    return nmo_chunk_read_object_id(chunk, out_id);
}

size_t nmo_chunk_get_id_count(const nmo_chunk_t *chunk) {
    return chunk ? chunk->ids.count : 0;
}

uint32_t nmo_chunk_get_object_id(const nmo_chunk_t *chunk, size_t index) {
    if (!chunk || index >= chunk->ids.count || chunk->ids.data == NULL) {
        return 0;
    }
    // ids array contains positions in data buffer, not the IDs themselves
    const uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
    uint32_t pos = ids[index];
    if (pos == 0xFFFFFFFFu) {
        return 0;
    }
    if (pos < chunk->data.count) {
        const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
        return data[pos];
    }
    return 0;
}

// =============================================================================
// Manager Sequences
// =============================================================================

nmo_status_t nmo_chunk_start_manager_sequence(nmo_chunk_t *chunk,
                                              nmo_guid_t manager_guid,
                                              size_t count) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");
    if (count > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Manager sequence count does not fit the 32-bit format");
    }

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to get parser state");
    }
    if (state->current_pos > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Manager sequence position does not fit the 32-bit format");
    }
    nmo_status_t result = nmo_chunk_check_size(
        chunk, 3u * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    const size_t managers_count_before = chunk->managers.count;
    const uint32_t options_before = chunk->chunk_options;
    chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;

    // Track sequence start in manager list (CK2 AddEntries)
    uint32_t sentinel = 0xFFFFFFFFu;
    nmo_status_t list_result = nmo_arena_array_append(&chunk->managers, &sentinel);
    if (list_result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
        return list_result;
    }

    uint32_t pos = (uint32_t) state->current_pos;
    list_result = nmo_arena_array_append(&chunk->managers, &pos);
    if (list_result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
        return list_result;
    }

    // Write count then manager GUID
    result = nmo_chunk_write_dword(chunk, (uint32_t) count);
    if (result == NMO_OK) {
        result = nmo_chunk_write_guid(chunk, manager_guid);
    }
    if (result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
    }
    return result;
}

nmo_status_t nmo_chunk_write_manager_int(nmo_chunk_t *chunk,
                                         nmo_guid_t manager_guid,
                                         uint32_t value) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    /* CK2 behavior: CheckSize(12) BEFORE checking/creating m_Managers */
    nmo_status_t result = nmo_chunk_check_size(chunk, 3 * sizeof(uint32_t));
    NMO_RETURN_IF_ERROR(result);

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to get parser state");
    }
    if (state->current_pos > UINT32_MAX) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT(
            "Manager entry position does not fit the 32-bit format");
    }

    /* CK2 behavior: AddEntry(CurrentPos) - track position of GUID start */
    const size_t managers_count_before = chunk->managers.count;
    const uint32_t options_before = chunk->chunk_options;
    chunk->chunk_options |= NMO_CHUNK_OPTION_MAN;
    uint32_t pos = (uint32_t) state->current_pos;
    nmo_status_t list_result = nmo_arena_array_append(&chunk->managers, &pos);
    if (list_result != NMO_OK) {
        chunk->managers.count = managers_count_before;
        chunk->chunk_options = options_before;
        return list_result;
    }

    /* Write manager GUID and value (CK2 order: d1, d2, value) */
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    data[state->current_pos++] = manager_guid.d1;
    data[state->current_pos++] = manager_guid.d2;
    data[state->current_pos++] = value;

    /* Update data_size */
    if (state->current_pos > chunk->data.count) {
        chunk->data.count = state->current_pos;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_read_manager_int(nmo_chunk_t *chunk,
                                        nmo_guid_t *out_manager_guid,
                                        uint32_t *out_value) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_manager_guid, out_value, "Invalid arguments");

    NMO_CHUNK_CHECK_BOUNDS_MSG(chunk, 3, "Insufficient data for manager int");

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    out_manager_guid->d1 = data[state->current_pos++];
    out_manager_guid->d2 = data[state->current_pos++];
    *out_value = data[state->current_pos++];

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_start_manager_read_sequence(nmo_chunk_t *chunk,
                                                   nmo_guid_t *out_manager_guid,
                                                   size_t *out_count) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_manager_guid, out_count, "Invalid arguments");
    out_manager_guid->d1 = 0;
    out_manager_guid->d2 = 0;
    *out_count = 0;

    nmo_chunk_parser_state_t *state = nmo_chunk_get_parser_state(chunk);
    if (!state) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to get parser state");
    }
    size_t start_pos = state->current_pos;

    // Read count then manager GUID
    uint32_t count_u32 = 0;
    nmo_guid_t manager_guid = {0, 0};
    nmo_status_t result = nmo_chunk_read_dword(chunk, &count_u32);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    result = nmo_chunk_read_guid(chunk, &manager_guid);
    if (result != NMO_OK) {
        state->current_pos = start_pos;
        return result;
    }

    if (!nmo_chunk_has_read_capacity(chunk, (size_t)count_u32)) {
        state->current_pos = start_pos;
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Manager sequence count exceeds remaining DWORDs");
    }

    *out_manager_guid = manager_guid;
    *out_count = (size_t)count_u32;
    NMO_RETURN_OK();
}
