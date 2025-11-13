/**
 * @file nmo_data.c
 * @brief NMO Data section parsing implementation
 */

#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdalign.h>

/* Helper macros */
#define CHECK_BUFFER_SIZE(pos, needed, total) \
    do { \
        if (!nmo_check_buffer_bounds((pos), (needed), (total))) { \
            fprintf(stderr, "[ERROR] Buffer overrun: pos=%zu, needed=%zu, size=%zu, total=%zu\n", \
                    (size_t)(pos), (size_t)(needed), (size_t)(total), (size_t)((pos) + (needed))); \
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF, \
                NMO_SEVERITY_ERROR, "Buffer overrun while reading Data section")); \
        } \
    } while(0)

/**
 * @brief Parse manager data from buffer
 *
 * Manager data format (for file_version >= 6):
 *   For each manager:
 *     - CKGUID (8 bytes: d1, d2)
 *     - data_size (4 bytes int32)
 *     - chunk_data (data_size bytes)
 */
static nmo_result_t parse_manager_data(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_data_section_t *section,
    nmo_arena_t *arena) {
    if (section->manager_count == 0) {
        section->managers = NULL;
        return nmo_result_ok();
    }

    /* Allocate manager data array */
    section->managers = (nmo_manager_data_t *) nmo_arena_alloc(
        arena,
        sizeof(nmo_manager_data_t) * section->manager_count,
        alignof(nmo_manager_data_t));
    if (section->managers == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate manager data array"));
    }

    /* Parse each manager */
    for (uint32_t i = 0; i < section->manager_count; i++) {
        nmo_manager_data_t *mgr = &section->managers[i];

        /* Read CKGUID (8 bytes: d1, d2) */
        CHECK_BUFFER_SIZE(*pos, 8, size);
        mgr->guid.d1 = nmo_read_u32_le(data + *pos);
        *pos += 4;
        mgr->guid.d2 = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read data size */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        mgr->data_size = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Parse chunk data if present */
        if (mgr->data_size > 0) {
            CHECK_BUFFER_SIZE(*pos, mgr->data_size, size);

            /* Create chunk */
            mgr->chunk = nmo_chunk_create(arena);
            if (mgr->chunk == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to create manager chunk"));
            }

            /* Parse chunk from buffer */
            nmo_result_t result = nmo_chunk_parse(mgr->chunk, data + *pos, mgr->data_size);
            if (result.code != NMO_OK) {
                return result;
            }

            *pos += mgr->data_size;
        } else {
            mgr->chunk = NULL;
        }
    }

    return nmo_result_ok();
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
static nmo_result_t parse_object_data(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    uint32_t file_version,
    nmo_data_section_t *section,
    nmo_arena_t *arena) {
    if (section->object_count == 0) {
        section->objects = NULL;
        return nmo_result_ok();
    }

    /* Allocate object data array */
    section->objects = (nmo_object_data_t *) nmo_arena_alloc(
        arena,
        sizeof(nmo_object_data_t) * section->object_count,
        alignof(nmo_object_data_t));
    if (section->objects == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate object data array"));
    }

    /* Parse each object */
    for (uint32_t i = 0; i < section->object_count; i++) {
        nmo_object_data_t *obj = &section->objects[i];

        /* For file_version < 7, object ID is stored here */
        /* For file_version >= 8, object IDs are in Header1 */
        if (file_version < 7) {
            CHECK_BUFFER_SIZE(*pos, 4, size);
            /* uint32_t object_id = */
            nmo_read_u32_le(data + *pos);
            *pos += 4;
            /* Object ID is not stored in nmo_object_data for version < 7
             * because it's redundant with Header1 in version >= 8 */
        }

        /* Read data size */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        obj->data_size = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Parse chunk data if present */
        if (obj->data_size > 0) {
            CHECK_BUFFER_SIZE(*pos, obj->data_size, size);

            /* Create chunk */
            obj->chunk = nmo_chunk_create(arena);
            if (obj->chunk == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR, "Failed to create object chunk"));
            }

            /* Parse chunk from buffer */
            nmo_result_t result = nmo_chunk_parse(obj->chunk, data + *pos, obj->data_size);
            if (result.code != NMO_OK) {
                return result;
            }

            *pos += obj->data_size;
        } else {
            obj->chunk = NULL;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_data_section_parse(
    const void *data,
    size_t size,
    uint32_t file_version,
    nmo_data_section_t *data_section,
    nmo_arena_t *arena) {
    if (data == NULL || data_section == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "NULL pointer passed to nmo_data_section_parse"));
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
        nmo_result_t result = parse_manager_data(buffer, size, &pos, data_section, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Parse object data (file_version >= 4) */
    if (file_version >= 4 && object_count > 0) {
        nmo_result_t result = parse_object_data(buffer, size, &pos, file_version, data_section, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_data_section_serialize(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written,
    nmo_arena_t *arena) {
    if (data_section == NULL || buffer == NULL || bytes_written == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments to data_section_serialize"));
    }

    uint8_t *buf = (uint8_t *) buffer;
    size_t pos = 0;

    /* Serialize manager data */
    if (data_section->managers != NULL) {
        for (uint32_t i = 0; i < data_section->manager_count; i++) {
            const nmo_manager_data_t *mgr = &data_section->managers[i];

            /* Write GUID (8 bytes: d1, d2) */
            if (pos + 8 > buffer_size) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for manager GUID"));
            }
            nmo_write_u32_le(buf + pos, mgr->guid.d1);
            pos += 4;
            nmo_write_u32_le(buf + pos, mgr->guid.d2);
            pos += 4;

            /* Serialize chunk to get its data and size */
            void *chunk_data = NULL;
            size_t chunk_size = 0;
            nmo_result_t result = nmo_chunk_serialize(mgr->chunk, &chunk_data, &chunk_size, arena);
            if (result.code != NMO_OK) {
                return result;
            }

            /* Write actual data size */
            if (pos + 4 > buffer_size) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for manager data size"));
            }
            nmo_write_u32_le(buf + pos, (uint32_t)chunk_size);
            pos += 4;

            /* Write chunk data */
            if (chunk_size > 0) {
                if (pos + chunk_size > buffer_size) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for manager chunk data"));
                }
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
                if (pos + 4 > buffer_size) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for object ID"));
                }
                /* TODO: Get actual object ID from somewhere */
                nmo_write_u32_le(buf + pos, 0); /* Placeholder */
                pos += 4;
            }

            /* Serialize chunk to get its data and size */
            void *chunk_data = NULL;
            size_t chunk_size = 0;
            nmo_result_t result = nmo_chunk_serialize(obj->chunk, &chunk_data, &chunk_size, arena);
            if (result.code != NMO_OK) {
                return result;
            }

            /* Write actual data size */
            if (pos + 4 > buffer_size) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for object data size"));
            }
            nmo_write_u32_le(buf + pos, (uint32_t)chunk_size);
            pos += 4;

            /* Write chunk data */
            if (chunk_size > 0) {
                if (pos + chunk_size > buffer_size) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for object chunk data"));
                }
                memcpy(buf + pos, chunk_data, chunk_size);
                pos += chunk_size;
            }
        }
    }

    *bytes_written = pos;
    return nmo_result_ok();
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
            total_size += 8; // GUID
            total_size += 4; // Data size

            if (mgr->chunk != NULL) {
                if (mgr->chunk->raw_data != NULL) {
                    total_size += mgr->chunk->raw_size;
                } else {
                    void *chunk_data = NULL;
                    size_t chunk_size = 0;
                    // This is inefficient, but necessary to get the size.
                    nmo_chunk_serialize(mgr->chunk, &chunk_data, &chunk_size, arena);
                    total_size += chunk_size;
                }
            }
        }
    }

    /* Object data */
    if (data_section->objects != NULL) {
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            const nmo_object_data_t *obj = &data_section->objects[i];

            if (file_version < 7) {
                total_size += 4; // object_id
            }

            total_size += 4; // Data size

            if (obj->chunk != NULL) {
                if (obj->chunk->raw_data != NULL) {
                    total_size += obj->chunk->raw_size;
                } else {
                    void *chunk_data = NULL;
                    size_t chunk_size = 0;
                    // This is inefficient, but necessary to get the size.
                    nmo_chunk_serialize(obj->chunk, &chunk_data, &chunk_size, arena);
                    total_size += chunk_size;
                }
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
                /* TODO: Free chunk when chunk implementation is done */
                /* nmo_chunk_free(data_section->managers[i].chunk); */
            }
        }
    }

    /* Free object chunks */
    if (data_section->objects != NULL) {
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            if (data_section->objects[i].chunk != NULL) {
                /* TODO: Free chunk when chunk implementation is done */
                /* nmo_chunk_free(data_section->objects[i].chunk); */
            }
        }
    }

    /* Note: managers and objects arrays are arena-allocated, no free needed */
    memset(data_section, 0, sizeof(nmo_data_section_t));
}
