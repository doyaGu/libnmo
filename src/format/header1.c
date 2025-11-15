/**
 * @file header1.c
 * @brief NMO Header1 (object descriptors and plugin dependencies) implementation
 */

#include "format/nmo_header1.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdio.h>

/* Helper macros for safe buffer reading */
#define CHECK_BUFFER_SIZE(pos, needed, size) \
    do { \
        if (!nmo_check_buffer_bounds((pos), (needed), (size))) { \
            fprintf(stderr, "[ERROR] Buffer overrun: pos=%zu, needed=%zu, size=%zu, total=%zu\n", \
                    (size_t)(pos), (size_t)(needed), (size_t)(size), (size_t)((pos)+(needed))); \
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN, \
                NMO_SEVERITY_ERROR, "Buffer overrun while reading Header1")); \
        } \
    } while (0)

/**
 * @brief Parse object descriptors from buffer
 */
static nmo_result_t parse_objects(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    /* NOTE: Object count is already set from file header, not read from buffer */
    /* In Virtools file version 8+, Header1 does not contain object count */

    if (header->object_count == 0) {
        header->objects = NULL;
        return nmo_result_ok();
    }

    /* Allocate object array */
    size_t objects_size = header->object_count * sizeof(nmo_object_desc_t);
    header->objects = (nmo_object_desc_t *) nmo_arena_alloc(arena, objects_size, 8);
    if (header->objects == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate object descriptor array"));
    }

    /* Parse each object descriptor */
    for (uint32_t i = 0; i < header->object_count; i++) {
        nmo_object_desc_t *obj = &header->objects[i];

        /* Read file ID (Object) - may have bit 23 set for reference-only */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        obj->file_id = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Extract reference-only flag from bit 23 */
        obj->flags = (obj->file_id & NMO_OBJECT_REFERENCE_FLAG) ? NMO_OBJECT_REFERENCE_FLAG : 0;
        obj->file_id &= ~NMO_OBJECT_REFERENCE_FLAG; /* Clear flag bit from ID */

        /* Read class ID (ObjectCid) */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        obj->class_id = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read file index (FileIndex) */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        obj->file_index = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read name length (does NOT include null terminator in buffer) */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        uint32_t name_len = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Allocate for name + null terminator */
        obj->name = (char *) nmo_arena_alloc(arena, name_len + 1, 1);
        if (obj->name == NULL) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate object name"));
        }

        /* Read name string (if any) */
        if (name_len > 0) {
            CHECK_BUFFER_SIZE(*pos, name_len, size);
            memcpy(obj->name, data + *pos, name_len);
            *pos += name_len;
        }

        /* Add null terminator */
        obj->name[name_len] = '\0';
    }

    return nmo_result_ok();
}

/**
 * @brief Parse plugin dependencies from buffer
 */
static nmo_result_t parse_plugin_deps(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    /* Read category count */
    CHECK_BUFFER_SIZE(*pos, 4, size);
    uint32_t category_count = nmo_read_u32_le(data + *pos);
    *pos += 4;

    if (category_count == 0) {
        header->plugin_dep_count = 0;
        header->plugin_deps = NULL;
        return nmo_result_ok();
    }

    /* Count total number of plugins across all categories */
    uint32_t total_plugins = 0;
    size_t saved_pos = *pos;

    for (uint32_t i = 0; i < category_count; i++) {
        /* Read category type */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        *pos += 4;

        /* Read GUID count for this category */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        uint32_t guid_count = nmo_read_u32_le(data + *pos);
        *pos += 4;

        total_plugins += guid_count;

        /* Skip GUIDs (8 bytes each) */
        CHECK_BUFFER_SIZE(*pos, guid_count * 8, size);
        *pos += guid_count * 8;
    }

    /* Reset position to start of categories */
    *pos = saved_pos;
    header->plugin_dep_count = total_plugins;

    if (total_plugins == 0) {
        header->plugin_deps = NULL;
        return nmo_result_ok();
    }

    /* Allocate plugin dependency array */
    size_t deps_size = total_plugins * sizeof(nmo_plugin_dep_t);
    header->plugin_deps = (nmo_plugin_dep_t *) nmo_arena_alloc(arena, deps_size, 8);
    if (header->plugin_deps == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate plugin dependency array"));
    }

    /* Parse each category */
    uint32_t plugin_index = 0;
    for (uint32_t i = 0; i < category_count; i++) {
        /* Read category type */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        uint32_t category = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read GUID count */
        CHECK_BUFFER_SIZE(*pos, 4, size);
        uint32_t guid_count = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read each GUID */
        for (uint32_t j = 0; j < guid_count; j++) {
            CHECK_BUFFER_SIZE(*pos, 8, size);

            nmo_plugin_dep_t *dep = &header->plugin_deps[plugin_index++];
            dep->category = category;
            dep->guid.d1 = nmo_read_u32_le(data + *pos);
            *pos += 4;
            dep->guid.d2 = nmo_read_u32_le(data + *pos);
            *pos += 4;
            dep->version = 0; /* Version not stored in format */
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Parse Header1 from buffer
 */
nmo_result_t nmo_header1_parse(
    const void *data,
    size_t size,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    if (data == NULL || header == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "NULL pointer passed to nmo_header1_parse"));
    }

    /* Save object_count which must be set by caller (from file header) */
    uint32_t object_count = header->object_count;

    /* Initialize header */
    memset(header, 0, sizeof(nmo_header1_t));

    /* Restore object_count */
    header->object_count = object_count;

    const uint8_t *buffer = (const uint8_t *) data;
    size_t pos = 0;

    /* Parse object descriptors */
    nmo_result_t result = parse_objects(buffer, size, &pos, header, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Parse plugin dependencies (if data remains) */
    if (pos < size) {
        result = parse_plugin_deps(buffer, size, &pos, header, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    header->included_file_count = 0;
    header->included_files = NULL;

    if (pos + 8 <= size) {
        uint32_t included_count = nmo_read_u32_le(buffer + pos);
        pos += 4;

        uint32_t included_table_size = nmo_read_u32_le(buffer + pos);
        pos += 4;

        header->included_file_count = included_count;

        if (included_table_size > 0 && pos + included_table_size > size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR,
                                              "Buffer too small for included file table"));
        }

        if (included_count == 0 || included_table_size == 0) {
            /* Table descriptors are absent; metadata lives outside Header1 */
            pos += included_table_size;
        } else {
            header->included_files = (nmo_included_file_desc_t *) nmo_arena_alloc(
                arena,
                sizeof(nmo_included_file_desc_t) * included_count,
                sizeof(void *)
            );

            if (header->included_files == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate included file descriptors"));
            }

            memset(header->included_files, 0,
                   sizeof(nmo_included_file_desc_t) * included_count);

            size_t table_end = pos + included_table_size;

            for (uint32_t i = 0; i < included_count; i++) {
                if (pos + 4 > table_end) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                      NMO_SEVERITY_ERROR,
                                                      "Buffer too small for included name length"));
                }

                uint32_t name_len = nmo_read_u32_le(buffer + pos);
                pos += 4;

                char *name_ptr = NULL;
                if (name_len > 0) {
                    if (pos + name_len > table_end) {
                        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                          NMO_SEVERITY_ERROR,
                                                          "Buffer too small for included filename"));
                    }

                    name_ptr = (char *) nmo_arena_alloc(arena, name_len + 1, 1);
                    if (name_ptr == NULL) {
                        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                          NMO_SEVERITY_ERROR,
                                                          "Failed to allocate included filename"));
                    }

                    memcpy(name_ptr, buffer + pos, name_len);
                    name_ptr[name_len] = '\0';
                    pos += name_len;
                } else {
                    name_ptr = (char *) nmo_arena_alloc(arena, 1, 1);
                    if (name_ptr == NULL) {
                        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                          NMO_SEVERITY_ERROR,
                                                          "Failed to allocate empty filename"));
                    }
                    name_ptr[0] = '\0';
                }

                if (pos + 4 > table_end) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                      NMO_SEVERITY_ERROR,
                                                      "Buffer too small for included size"));
                }

                uint32_t data_size = nmo_read_u32_le(buffer + pos);
                pos += 4;

                header->included_files[i].name = name_ptr;
                header->included_files[i].data_size = data_size;
            }

            pos = table_end;
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize object descriptors to buffer
 */
static nmo_result_t serialize_objects(
    const nmo_header1_t *header,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *pos) {
    /* NOTE: Object count is NOT written to buffer - it's in file header */
    /* In Virtools file version 8+, Header1 does not contain object count */

    /* Write each object descriptor */
    for (uint32_t i = 0; i < header->object_count; i++) {
        const nmo_object_desc_t *obj = &header->objects[i];

        /* Write file ID (Object) - with reference flag if set */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for file ID"));
        }
        uint32_t file_id = obj->file_id;
        if (obj->flags & NMO_OBJECT_REFERENCE_FLAG) {
            file_id |= NMO_OBJECT_REFERENCE_FLAG;
        }
        nmo_write_u32_le(buffer + *pos, file_id);
        *pos += 4;

        /* Write class ID (ObjectCid) */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for class ID"));
        }
        nmo_write_u32_le(buffer + *pos, obj->class_id);
        *pos += 4;

        /* Write file index (FileIndex) */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for file index"));
        }
        nmo_write_u32_le(buffer + *pos, obj->file_index);
        *pos += 4;

        /* Calculate name length (does NOT include null terminator) */
        uint32_t name_len = obj->name ? (uint32_t) strlen(obj->name) : 0;

        /* Write name length */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for name length"));
        }
        nmo_write_u32_le(buffer + *pos, name_len);
        *pos += 4;

        /* Write name string (without null terminator) */
        if (name_len > 0) {
            if (*pos + name_len > buffer_size) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                  NMO_SEVERITY_ERROR, "Buffer too small for name string"));
            }
            memcpy(buffer + *pos, obj->name, name_len);
            *pos += name_len;
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize plugin dependencies to buffer
 */
static nmo_result_t serialize_plugin_deps(
    const nmo_header1_t *header,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *pos) {
    if (header->plugin_dep_count == 0) {
        /* Write category count of 0 */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for category count"));
        }
        nmo_write_u32_le(buffer + *pos, 0);
        *pos += 4;
        return nmo_result_ok();
    }

    /* Group plugins by category */
    uint32_t category_counts[5] = {0}; /* 5 standard categories */
    for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
        uint32_t cat = header->plugin_deps[i].category;
        if (cat < 5) {
            category_counts[cat]++;
        }
    }

    /* Count non-empty categories */
    uint32_t num_categories = 0;
    for (int i = 0; i < 5; i++) {
        if (category_counts[i] > 0) {
            num_categories++;
        }
    }

    /* Write category count */
    if (*pos + 4 > buffer_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR, "Buffer too small for category count"));
    }
    nmo_write_u32_le(buffer + *pos, num_categories);
    *pos += 4;

    /* Write each category */
    for (uint32_t cat = 0; cat < 5; cat++) {
        if (category_counts[cat] == 0) {
            continue;
        }

        /* Write category type */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for category type"));
        }
        nmo_write_u32_le(buffer + *pos, cat);
        *pos += 4;

        /* Write GUID count */
        if (*pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for GUID count"));
        }
        nmo_write_u32_le(buffer + *pos, category_counts[cat]);
        *pos += 4;

        /* Write GUIDs for this category */
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            if (header->plugin_deps[i].category == cat) {
                if (*pos + 8 > buffer_size) {
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                      NMO_SEVERITY_ERROR, "Buffer too small for GUID"));
                }
                nmo_write_u32_le(buffer + *pos, header->plugin_deps[i].guid.d1);
                *pos += 4;
                nmo_write_u32_le(buffer + *pos, header->plugin_deps[i].guid.d2);
                *pos += 4;
            }
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Calculate required buffer size for serialization
 */
static size_t calculate_serialize_size(const nmo_header1_t *header) {
    size_t size = 0;

    /* NOTE: Object count is NOT in buffer - it's in file header */

    /* Object descriptors */
    for (uint32_t i = 0; i < header->object_count; i++) {
        size += 4; /* file_id */
        size += 4; /* class_id */
        size += 4; /* file_index */
        size += 4; /* name_len */
        uint32_t name_len = header->objects[i].name ? (uint32_t) strlen(header->objects[i].name) : 0;
        size += name_len; /* name string (without null) */
    }

    /* Plugin dependencies */
    size += 4; /* category count */
    if (header->plugin_dep_count > 0) {
        /* Count categories */
        uint32_t category_counts[5] = {0};
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            uint32_t cat = header->plugin_deps[i].category;
            if (cat < 5) {
                category_counts[cat]++;
            }
        }

        /* Add size for each category */
        for (int i = 0; i < 5; i++) {
            if (category_counts[i] > 0) {
                size += 4;                      /* category type */
                size += 4;                      /* GUID count */
                size += category_counts[i] * 8; /* GUIDs */
            }
        }
    }

    size += 4;
    size += 4;

    for (uint32_t i = 0; i < header->included_file_count; i++) {
        uint32_t name_len = header->included_files[i].name
                              ? (uint32_t) strlen(header->included_files[i].name)
                              : 0;
        size += 4;
        size += name_len;
        size += 4;
    }

    return size;
}

/**
 * @brief Serialize Header1 to buffer
 */
nmo_result_t nmo_header1_serialize(
    const nmo_header1_t *header,
    void **out_data,
    size_t *out_size,
    nmo_arena_t *arena) {
    if (header == NULL || out_data == NULL || out_size == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "NULL pointer passed to nmo_header1_serialize"));
    }

    /* Calculate required buffer size */
    size_t buffer_size = calculate_serialize_size(header);

    /* Allocate buffer */
    uint8_t *buffer = (uint8_t *) nmo_arena_alloc(arena, buffer_size, 1);
    if (buffer == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate serialization buffer"));
    }

    size_t pos = 0;

    /* Serialize object descriptors */
    nmo_result_t result = serialize_objects(header, buffer, buffer_size, &pos);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Serialize plugin dependencies */
    result = serialize_plugin_deps(header, buffer, buffer_size, &pos);
    if (result.code != NMO_OK) {
        return result;
    }

    if (pos + 8 > buffer_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR,
                                          "Buffer too small for included metadata"));
    }
    nmo_write_u32_le(buffer + pos, header->included_file_count);
    pos += 4;

    size_t block_size = 0;
    for (uint32_t i = 0; i < header->included_file_count; i++) {
        uint32_t name_len = header->included_files[i].name
                              ? (uint32_t) strlen(header->included_files[i].name)
                              : 0;
        block_size += 4;
        block_size += name_len;
        block_size += 4;
    }
    nmo_write_u32_le(buffer + pos, (uint32_t) block_size);
    pos += 4;

    for (uint32_t i = 0; i < header->included_file_count; i++) {
        const char *name = header->included_files[i].name;
        uint32_t name_len = name ? (uint32_t) strlen(name) : 0;

        if (pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR,
                                              "Buffer too small for included name length"));
        }
        nmo_write_u32_le(buffer + pos, name_len);
        pos += 4;

        if (name_len > 0) {
            if (pos + name_len > buffer_size) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                  NMO_SEVERITY_ERROR,
                                                  "Buffer too small for included filename"));
            }
            memcpy(buffer + pos, name, name_len);
            pos += name_len;
        }

        if (pos + 4 > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR,
                                              "Buffer too small for included size"));
        }
        nmo_write_u32_le(buffer + pos, header->included_files[i].data_size);
        pos += 4;
    }

    *out_data = buffer;
    *out_size = pos;

    return nmo_result_ok();
}

/**
 * @brief Free Header1 resources
 */
void nmo_header1_free(nmo_header1_t *header) {
    if (header == NULL) {
        return;
    }

    /* When using arena allocation, this is typically a no-op */
    /* The arena will free all memory at once */

    /* Clear the structure */
    memset(header, 0, sizeof(nmo_header1_t));
}
