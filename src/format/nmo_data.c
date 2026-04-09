/**
 * @file nmo_data.c
 * @brief NMO Data section parsing implementation
 */

#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_pool.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdalign.h>

/* Helper macros */
#define CHECK_BUFFER_SIZE(arena, pos, needed, total) \
    do { \
        if (!nmo_check_buffer_bounds((pos), (needed), (total))) { \
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR, \
                "Data section buffer overrun: pos=%zu needed=%zu total=%zu", \
                (size_t)(pos), (size_t)(needed), (size_t)(total)); \
        } \
    } while (0)

/**
 * @brief Parse manager data from buffer
 *
 * Manager data format (for file_version >= 6):
 *   For each manager:
 *     - CKGUID (8 bytes: d1, d2)
 *     - data_size (4 bytes int32)
 *     - chunk_data (data_size bytes)
 */
static nmo_chunk_t *allocate_chunk(nmo_chunk_pool_t *chunk_pool, nmo_arena_t *arena) {
    if (chunk_pool != NULL) {
        nmo_chunk_t *chunk = nmo_chunk_pool_acquire(chunk_pool);
        if (chunk != NULL) {
            return chunk;
        }
    }
    return nmo_chunk_create(arena);
}

static nmo_status_t parse_manager_data(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_data_section_t *section,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    if (section->manager_count == 0) {
        section->managers = NULL;
        NMO_RETURN_OK();
    }

    /* Allocate manager data array */
    size_t manager_bytes = 0;
    if (!nmo_safe_mul_size(sizeof(nmo_manager_data_t), section->manager_count, &manager_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Manager data allocation overflow");
    }
    section->managers = (nmo_manager_data_t *) nmo_arena_alloc(
        arena,
        manager_bytes,
        alignof(nmo_manager_data_t));
    if (section->managers == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate manager data array");
    }

    /* Parse each manager */
    for (uint32_t i = 0; i < section->manager_count; i++) {
        nmo_manager_data_t *mgr = &section->managers[i];

        /* Read CKGUID (8 bytes: d1, d2) */
        CHECK_BUFFER_SIZE(arena, *pos, 8, size);
        mgr->guid.d1 = nmo_read_u32_le(data + *pos);
        *pos += 4;
        mgr->guid.d2 = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read data size */
        CHECK_BUFFER_SIZE(arena, *pos, 4, size);
        mgr->data_size = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Parse chunk data if present */
        if (mgr->data_size > 0) {
            CHECK_BUFFER_SIZE(arena, *pos, mgr->data_size, size);

            /* Create chunk */
            mgr->chunk = allocate_chunk(chunk_pool, arena);
            if (mgr->chunk == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Failed to create manager chunk (index=%u, size=%u)",
                                        (unsigned)i, (unsigned)mgr->data_size);
            }

            /* Parse chunk from buffer */
            NMO_RETURN_IF_ERROR_CTX(nmo_chunk_parse(mgr->chunk, data + *pos, mgr->data_size),
                                    "Failed to parse manager chunk (index=%u, size=%u)",
                                    (unsigned)i,
                                    (unsigned)mgr->data_size);

            mgr->chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;

            *pos += mgr->data_size;
        } else {
            mgr->chunk = NULL;
        }

        mgr->flags = 0;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Parse object data from buffer
 *
 * Object data format (for file_version >= 4):
 *   For each object:
 *     - [only if version < 7] object_id (4 bytes int32)
 *     - data_size (4 bytes int32)
 *     - chunk_data (data_size bytes)
 */
static nmo_status_t parse_object_data(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    uint32_t file_version,
    nmo_data_section_t *section,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    if (section->object_count == 0) {
        section->objects = NULL;
        NMO_RETURN_OK();
    }

    /* Allocate object data array */
    size_t object_bytes = 0;
    if (!nmo_safe_mul_size(sizeof(nmo_object_data_t), section->object_count, &object_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Object data allocation overflow");
    }
    section->objects = (nmo_object_data_t *) nmo_arena_alloc(
        arena,
        object_bytes,
        alignof(nmo_object_data_t));
    if (section->objects == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object data array");
    }

    /* Parse each object */
    for (uint32_t i = 0; i < section->object_count; i++) {
        nmo_object_data_t *obj = &section->objects[i];

        obj->object_id = 0;

        /* For file_version < 7, object ID is stored here */
        /* For file_version >= 8, object IDs are in Header1 */
        if (file_version < 7) {
            CHECK_BUFFER_SIZE(arena, *pos, 4, size);
            obj->object_id = nmo_read_u32_le(data + *pos);
            *pos += 4;
            /* Object ID is not stored in nmo_object_data for version < 7
             * because it's redundant with Header1 in version >= 8 */
        }

        /* Read data size */
        CHECK_BUFFER_SIZE(arena, *pos, 4, size);
        obj->data_size = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Parse chunk data if present */
        if (obj->data_size > 0) {
            CHECK_BUFFER_SIZE(arena, *pos, obj->data_size, size);

            /* Create chunk */
            obj->chunk = allocate_chunk(chunk_pool, arena);
            if (obj->chunk == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Failed to create object chunk (index=%u, size=%u)",
                                        (unsigned)i, (unsigned)obj->data_size);
            }

            /* Parse chunk from buffer */
            NMO_RETURN_IF_ERROR_CTX(nmo_chunk_parse(obj->chunk, data + *pos, obj->data_size),
                                    "Failed to parse object chunk (index=%u, size=%u)",
                                    (unsigned)i,
                                    (unsigned)obj->data_size);

            obj->chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;

            *pos += obj->data_size;
        } else {
            obj->chunk = NULL;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_data_section_parse(
    const void *data,
    size_t size,
    uint32_t file_version,
    nmo_data_section_t *data_section,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL arena passed to nmo_data_section_parse");
    }

    if (data == NULL || data_section == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL pointer passed to nmo_data_section_parse");
    }

    /* Save counts which must be set by caller (from file header) */
    uint32_t manager_count = data_section->manager_count;
    uint32_t object_count = data_section->object_count;

    /* Initialize data section */
    memset(data_section, 0, sizeof(nmo_data_section_t));

    /* Restore counts */
    data_section->manager_count = manager_count;
    data_section->object_count = object_count;

    const uint8_t *buffer = (const uint8_t *) data;
    size_t pos = 0;

    /* Parse manager data (file_version >= 6) */
    if (file_version >= 6 && manager_count > 0) {
        NMO_RETURN_IF_ERROR_CTX(parse_manager_data(buffer, size, &pos, data_section, chunk_pool, arena),
                                "Failed to parse manager data (count=%u)",
                                (unsigned)manager_count);
    }

    /* Parse object data (file_version >= 4) */
    if (file_version >= 4 && object_count > 0) {
        NMO_RETURN_IF_ERROR_CTX(parse_object_data(
                                    buffer,
                                    size,
                                    &pos,
                                    file_version,
                                    data_section,
                                    chunk_pool,
                                    arena),
                                "Failed to parse object data (count=%u, file_version=%u)",
                                (unsigned)object_count,
                                (unsigned)file_version);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_data_section_serialize(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written,
    nmo_arena_t *arena) {
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL arena passed to nmo_data_section_serialize");
    }
    if (data_section == NULL || buffer == NULL || bytes_written == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to nmo_data_section_serialize");
    }

    uint8_t *buf = (uint8_t *) buffer;
    size_t pos = 0;

    /* Serialize manager data */
    if (data_section->managers != NULL) {
        for (uint32_t i = 0; i < data_section->manager_count; i++) {
            const nmo_manager_data_t *mgr = &data_section->managers[i];

            /* Write GUID (8 bytes: d1, d2) */
            NMO_ENSURE(pos + 8 <= buffer_size, NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                       "Buffer too small for manager GUID (index=%u)", (unsigned)i);
            nmo_write_u32_le(buf + pos, mgr->guid.d1);
            pos += 4;
            nmo_write_u32_le(buf + pos, mgr->guid.d2);
            pos += 4;

            /* Serialize chunk to get its data and size */
            const void *chunk_data = NULL;
            void *serialized_data = NULL;
            size_t chunk_size = 0;
            if (mgr->chunk != NULL) {
                /* Use raw_data if available, otherwise serialize */
                if (mgr->chunk->raw_data != NULL && mgr->chunk->raw_size > 0) {
                    chunk_data = mgr->chunk->raw_data;
                    chunk_size = mgr->chunk->raw_size;
                } else {
                    NMO_RETURN_IF_ERROR_CTX(nmo_chunk_serialize_version1(mgr->chunk, &serialized_data, &chunk_size, arena),
                                            "Failed to serialize manager chunk (index=%u)",
                                            (unsigned)i);
                    chunk_data = serialized_data;
                }
            }

            /* Write actual data size */
            NMO_ENSURE(pos + 4 <= buffer_size, NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                       "Buffer too small for manager data size (index=%u)", (unsigned)i);
            NMO_ENSURE(chunk_size <= (size_t)UINT32_MAX, NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR,
                       "Manager chunk too large to serialize (index=%u, size=%zu)",
                       (unsigned)i, chunk_size);
            nmo_write_u32_le(buf + pos, (uint32_t)chunk_size);
            pos += 4;

            /* Write chunk data */
            if (chunk_size > 0) {
                NMO_ENSURE(pos + chunk_size <= buffer_size, NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                           "Buffer too small for manager chunk data (index=%u, size=%zu)",
                           (unsigned)i, chunk_size);
                memcpy(buf + pos, chunk_data, chunk_size);
                pos += chunk_size;
            }
        }
    }

    /* Serialize object data */
    if (data_section->objects != NULL) {
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            const nmo_object_data_t *obj = &data_section->objects[i];

            /* For file_version < 7, write object ID */
            if (file_version < 7) {
                NMO_ENSURE(pos + 4 <= buffer_size, NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                           "Buffer too small for object ID (index=%u)", (unsigned)i);
                NMO_ENSURE(obj->object_id != 0, NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                           "Missing object ID for legacy data section (index=%u)", (unsigned)i);
                nmo_write_u32_le(buf + pos, obj->object_id);
                pos += 4;
            }

            /* Serialize chunk to get its data and size */
            const void *chunk_data = NULL;
            void *serialized_data = NULL;
            size_t chunk_size = 0;
            if (obj->chunk != NULL) {
                /* Use raw_data if available, otherwise serialize */
                if (obj->chunk->raw_data != NULL && obj->chunk->raw_size > 0) {
                    chunk_data = obj->chunk->raw_data;
                    chunk_size = obj->chunk->raw_size;
                } else {
                    NMO_RETURN_IF_ERROR_CTX(nmo_chunk_serialize_version1(obj->chunk, &serialized_data, &chunk_size, arena),
                                            "Failed to serialize object chunk (index=%u)",
                                            (unsigned)i);
                    chunk_data = serialized_data;
                }
            }

            /* Write actual data size */
            NMO_ENSURE(pos + 4 <= buffer_size, NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                       "Buffer too small for object data size (index=%u)", (unsigned)i);
            NMO_ENSURE(chunk_size <= (size_t)UINT32_MAX, NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR,
                       "Object chunk too large to serialize (index=%u, size=%zu)",
                       (unsigned)i, chunk_size);
            nmo_write_u32_le(buf + pos, (uint32_t)chunk_size);
            pos += 4;

            /* Write chunk data */
            if (chunk_size > 0) {
                NMO_ENSURE(pos + chunk_size <= buffer_size, NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                           "Buffer too small for object chunk data (index=%u, size=%zu)",
                           (unsigned)i, chunk_size);
                memcpy(buf + pos, chunk_data, chunk_size);
                pos += chunk_size;
            }
        }
    }

    *bytes_written = pos;
    NMO_RETURN_OK();
}

size_t nmo_data_section_calculate_size(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    nmo_arena_t *arena) {
    if (data_section == NULL || arena == NULL) {
        return 0;
    }

    size_t total_size = 0;

    /* Manager data */
    if (data_section->managers != NULL) {
        for (uint32_t i = 0; i < data_section->manager_count; i++) {
            const nmo_manager_data_t *mgr = &data_section->managers[i];
            if (!nmo_safe_add_size(total_size, 8u, &total_size) ||
                !nmo_safe_add_size(total_size, 4u, &total_size)) {
                return 0;
            }

            /* Use data_size if set, otherwise serialize to get size */
            size_t chunk_size = 0;
            if (mgr->data_size > 0) {
                chunk_size = mgr->data_size;
            } else if (mgr->chunk != NULL) {
                if (mgr->chunk->raw_data != NULL) {
                    chunk_size = mgr->chunk->raw_size;
                } else {
                    void *chunk_data = NULL;
                    chunk_size = 0;
                    // This is inefficient, but necessary to get the size.
                    nmo_status_t status = nmo_chunk_serialize_version1(mgr->chunk, &chunk_data, &chunk_size, arena);
                    if (status != NMO_OK) {
                        return 0;
                    }
                }
            }

            if (chunk_size > (size_t)UINT32_MAX || !nmo_safe_add_size(total_size, chunk_size, &total_size)) {
                return 0;
            }
        }
    }

    /* Object data */
    if (data_section->objects != NULL) {
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            const nmo_object_data_t *obj = &data_section->objects[i];

            if (file_version < 7) {
                if (!nmo_safe_add_size(total_size, 4u, &total_size)) {
                    return 0;
                }
            }

            if (!nmo_safe_add_size(total_size, 4u, &total_size)) {
                return 0;
            }

            /* Use data_size if set, otherwise serialize to get size */
            size_t chunk_size = 0;
            if (obj->data_size > 0) {
                chunk_size = obj->data_size;
            } else if (obj->chunk != NULL) {
                if (obj->chunk->raw_data != NULL) {
                    chunk_size = obj->chunk->raw_size;
                } else {
                    void *chunk_data = NULL;
                    chunk_size = 0;
                    // This is inefficient, but necessary to get the size.
                    nmo_status_t status = nmo_chunk_serialize_version1(obj->chunk, &chunk_data, &chunk_size, arena);
                    if (status != NMO_OK) {
                        return 0;
                    }
                }
            }

            if (chunk_size > (size_t)UINT32_MAX || !nmo_safe_add_size(total_size, chunk_size, &total_size)) {
                return 0;
            }
        }
    }

    return total_size;
}

void nmo_data_section_free(nmo_data_section_t *data_section) {
    if (data_section == NULL) {
        return;
    }

    /* Free manager chunks */
    if (data_section->managers != NULL) {
        for (uint32_t i = 0; i < data_section->manager_count; i++) {
            if (data_section->managers[i].chunk != NULL) {
                nmo_chunk_clear(data_section->managers[i].chunk);
            }
        }
    }

    /* Free object chunks */
    if (data_section->objects != NULL) {
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            if (data_section->objects[i].chunk != NULL) {
                nmo_chunk_clear(data_section->objects[i].chunk);
            }
        }
    }

    /* Note: managers and objects arrays are arena-allocated, no free needed */
    memset(data_section, 0, sizeof(nmo_data_section_t));
}
