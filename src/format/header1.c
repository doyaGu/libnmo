/**
 * @file header1.c
 * @brief NMO Header1 (object descriptors and plugin dependencies) implementation
 */

#include "format/nmo_header1.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        obj->file_id = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        /* Extract reference-only flag from bit 23 */
        obj->flags = (obj->file_id & NMO_OBJECT_REFERENCE_FLAG) ? NMO_OBJECT_REFERENCE_FLAG : 0;
        obj->file_id &= ~NMO_OBJECT_REFERENCE_FLAG; /* Clear flag bit from ID */

        /* Read class ID (ObjectCid) */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        obj->class_id = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        /* Read file index (FileIndex) */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        obj->file_index = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        /* Read name length (does NOT include null terminator in buffer) */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        uint32_t name_len = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

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
    CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
    uint32_t category_count = nmo_read_u32_le(data + *pos);
    *pos += sizeof(uint32_t);

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
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        *pos += sizeof(uint32_t);

        /* Read GUID count for this category */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        uint32_t guid_count = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        total_plugins += guid_count;

        /* Skip GUIDs (sizeof(nmo_guid_t) each) */
        CHECK_BUFFER_SIZE(*pos, guid_count * sizeof(nmo_guid_t), size);
        *pos += guid_count * sizeof(nmo_guid_t);
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
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        uint32_t category = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        /* Read GUID count */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        uint32_t guid_count = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        /* Read each GUID */
        for (uint32_t j = 0; j < guid_count; j++) {
            CHECK_BUFFER_SIZE(*pos, sizeof(nmo_guid_t), size);

            nmo_plugin_dep_t *dep = &header->plugin_deps[plugin_index++];
            dep->category = category;
            dep->guid.d1 = nmo_read_u32_le(data + *pos);
            *pos += sizeof(uint32_t);
            dep->guid.d2 = nmo_read_u32_le(data + *pos);
            *pos += sizeof(uint32_t);
            dep->version = 0; /* Version not stored in format */
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Parse included files metadata from buffer
 */
static nmo_result_t parse_included_files(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    if (*pos + sizeof(uint32_t) > size) {
        return nmo_result_ok();
    }

    CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
    uint32_t payload_size = nmo_read_u32_le(data + *pos);
    *pos += sizeof(uint32_t);

    header->included_file_count = 0;
    header->included_files = NULL;

    if (payload_size == 0) {
        return nmo_result_ok();
    }

    if (payload_size < sizeof(uint32_t)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid included files payload size"));
    }

    CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
    uint32_t file_count = nmo_read_u32_le(data + *pos);
    *pos += sizeof(uint32_t);

    header->included_file_count = file_count;

    size_t payload_remaining = (size_t) payload_size - sizeof(uint32_t);
    size_t payload_end = *pos + payload_remaining;
    CHECK_BUFFER_SIZE(*pos, payload_remaining, size);

    if (file_count == 0) {
        *pos = payload_end;
        return nmo_result_ok();
    }

    header->included_files = (nmo_included_file_desc_t *)nmo_arena_alloc(
        arena, file_count * sizeof(nmo_included_file_desc_t), 8);
    if (header->included_files == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate included files array"));
    }

    for (uint32_t i = 0; i < file_count; i++) {
        nmo_included_file_desc_t *entry = &header->included_files[i];
        entry->name = NULL;
        entry->data_size = 0;

        if (*pos + sizeof(uint32_t) > payload_end) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR,
                                              "Included files payload truncated (name length)"));
        }

        uint32_t name_len = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        if (name_len > 0) {
            if (*pos + name_len > payload_end) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Included files payload truncated (name)"));
            }

            entry->name = (char *)nmo_arena_alloc(arena, name_len + 1, 1);
            if (entry->name == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate included file name"));
            }

            memcpy(entry->name, data + *pos, name_len);
            entry->name[name_len] = '\0';
            *pos += name_len;
        }

        if (*pos + sizeof(uint32_t) > payload_end) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                              NMO_SEVERITY_ERROR,
                                              "Included files payload truncated (data size)"));
        }

        entry->data_size = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);
    }

    if (*pos < payload_end) {
        *pos = payload_end;
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

    if (pos < size) {
        result = parse_included_files(buffer, size, &pos, header, arena);
        if (result.code != NMO_OK) {
            return result;
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
        if (*pos + sizeof(uint32_t) > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for file ID"));
        }
        uint32_t file_id = obj->file_id;
        if (obj->flags & NMO_OBJECT_REFERENCE_FLAG) {
            file_id |= NMO_OBJECT_REFERENCE_FLAG;
        }
        nmo_write_u32_le(buffer + *pos, file_id);
        *pos += sizeof(uint32_t);

        /* Write class ID (ObjectCid) */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for class ID"));
        }
        nmo_write_u32_le(buffer + *pos, obj->class_id);
        *pos += sizeof(uint32_t);

        /* Write file index (FileIndex) */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for file index"));
        }
        nmo_write_u32_le(buffer + *pos, obj->file_index);
        *pos += sizeof(uint32_t);

        /* Calculate name length (does NOT include null terminator) */
        uint32_t name_len = obj->name ? (uint32_t) strlen(obj->name) : 0;

        /* Write name length */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for name length"));
        }
        nmo_write_u32_le(buffer + *pos, name_len);
        *pos += sizeof(uint32_t);

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
        if (*pos + sizeof(uint32_t) > buffer_size) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                              NMO_SEVERITY_ERROR, "Buffer too small for category count"));
        }
        nmo_write_u32_le(buffer + *pos, 0);
        *pos += sizeof(uint32_t);
        return nmo_result_ok();
    }

    /* Build unique category list in first-seen order (CK2 preserves array order). */
    nmo_result_t result = nmo_result_ok();
    uint32_t unique_count = 0;
    uint32_t *category_ordering = (uint32_t *) malloc(header->plugin_dep_count * sizeof(uint32_t));
    if (category_ordering == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate plugin category order"));
    }

    for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
        uint32_t cat = header->plugin_deps[i].category;
        int found = 0;
        for (uint32_t j = 0; j < unique_count; j++) {
            if (category_ordering[j] == cat) {
                found = 1;
                break;
            }
        }
        if (!found) {
            category_ordering[unique_count++] = cat;
        }
    }

    /* Write category count */
    if (*pos + sizeof(uint32_t) > buffer_size) {
        result = nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                            NMO_SEVERITY_ERROR, "Buffer too small for category count"));
        goto cleanup;
    }
    nmo_write_u32_le(buffer + *pos, unique_count);
    *pos += sizeof(uint32_t);

    /* Write each category in recorded order */
    for (uint32_t c = 0; c < unique_count; c++) {
        uint32_t cat = category_ordering[c];
        uint32_t cat_count = 0;
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            if (header->plugin_deps[i].category == cat) {
                cat_count++;
            }
        }

        /* Write category type */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            result = nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                NMO_SEVERITY_ERROR, "Buffer too small for category type"));
            goto cleanup;
        }
        nmo_write_u32_le(buffer + *pos, cat);
        *pos += sizeof(uint32_t);

        /* Write GUID count */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            result = nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                NMO_SEVERITY_ERROR, "Buffer too small for GUID count"));
            goto cleanup;
        }
        nmo_write_u32_le(buffer + *pos, cat_count);
        *pos += sizeof(uint32_t);

        /* Write GUIDs for this category */
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            if (header->plugin_deps[i].category == cat) {
                if (*pos + sizeof(nmo_guid_t) > buffer_size) {
                    result = nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                        NMO_SEVERITY_ERROR, "Buffer too small for GUID"));
                    goto cleanup;
                }
                nmo_write_u32_le(buffer + *pos, header->plugin_deps[i].guid.d1);
                *pos += sizeof(uint32_t);
                nmo_write_u32_le(buffer + *pos, header->plugin_deps[i].guid.d2);
                *pos += sizeof(uint32_t);
            }
        }
    }
cleanup:
    free(category_ordering);
    return result;
}

/**
 * @brief Calculate required buffer size for serialization
 */
static size_t calculate_serialize_size(const nmo_header1_t *header) {
    size_t size = 0;

    /* NOTE: Object count is NOT in buffer - it's in file header */

    /* Object descriptors */
    for (uint32_t i = 0; i < header->object_count; i++) {
        size += sizeof(uint32_t); /* file_id */
        size += sizeof(uint32_t); /* class_id */
        size += sizeof(uint32_t); /* file_index */
        size += sizeof(uint32_t); /* name_len */
        uint32_t name_len = header->objects[i].name ? (uint32_t) strlen(header->objects[i].name) : 0;
        size += name_len; /* name string (without null) */
    }

    /* Plugin dependencies */
    size += sizeof(uint32_t); /* category count */
    if (header->plugin_dep_count > 0) {
        uint32_t unique_count = 0;
        uint32_t *category_ordering = (uint32_t *) malloc(header->plugin_dep_count * sizeof(uint32_t));
        if (category_ordering != NULL) {
            for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
                uint32_t cat = header->plugin_deps[i].category;
                int found = 0;
                for (uint32_t j = 0; j < unique_count; j++) {
                    if (category_ordering[j] == cat) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    category_ordering[unique_count++] = cat;
                }
            }

            for (uint32_t c = 0; c < unique_count; c++) {
                uint32_t cat = category_ordering[c];
                uint32_t cat_count = 0;
                for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
                    if (header->plugin_deps[i].category == cat) {
                        cat_count++;
                    }
                }
                size += sizeof(uint32_t);                /* category type */
                size += sizeof(uint32_t);                /* GUID count */
                size += cat_count * sizeof(nmo_guid_t);  /* GUIDs */
            }

            free(category_ordering);
        } else {
            /* Fallback: assume each dep is its own category (over-allocate) */
            size += header->plugin_dep_count * (sizeof(uint32_t) + sizeof(uint32_t) + sizeof(nmo_guid_t));
        }
    }

    size += sizeof(uint32_t); /* payload size */
    size += sizeof(uint32_t); /* count */

    for (uint32_t i = 0; i < header->included_file_count; i++) {
        uint32_t name_len = header->included_files && header->included_files[i].name
                           ? (uint32_t) strlen(header->included_files[i].name)
                           : 0;
        size += sizeof(uint32_t); /* name length */
        size += name_len;         /* name bytes */
        size += sizeof(uint32_t); /* data size */
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

    if (header->included_file_count > 0 && header->included_files == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Included file count set without descriptors"));
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

    if (pos + (2 * sizeof(uint32_t)) > buffer_size) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                          NMO_SEVERITY_ERROR,
                                          "Buffer too small for included metadata"));
    }

    size_t payload_start = pos + sizeof(uint32_t);
    size_t payload_pos = payload_start + sizeof(uint32_t);
    size_t payload_end = payload_pos;

    if (header->included_file_count > 0 && header->included_files != NULL) {
        for (uint32_t i = 0; i < header->included_file_count; i++) {
            const nmo_included_file_desc_t *entry = &header->included_files[i];
            uint32_t name_len = entry->name ? (uint32_t) strlen(entry->name) : 0;

            if (payload_end + sizeof(uint32_t) + name_len + sizeof(uint32_t) > buffer_size) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_BUFFER_OVERRUN,
                                                  NMO_SEVERITY_ERROR,
                                                  "Buffer too small for included file entry"));
            }

            nmo_write_u32_le(buffer + payload_end, name_len);
            payload_end += sizeof(uint32_t);

            if (name_len > 0) {
                memcpy(buffer + payload_end, entry->name, name_len);
                payload_end += name_len;
            }

            nmo_write_u32_le(buffer + payload_end, entry->data_size);
            payload_end += sizeof(uint32_t);
        }
    }

    uint32_t payload_size = (uint32_t) (payload_end - payload_start);
    nmo_write_u32_le(buffer + pos, payload_size);
    pos += sizeof(uint32_t);
    nmo_write_u32_le(buffer + pos, header->included_file_count);
    pos += sizeof(uint32_t);

    if (payload_end > pos) {
        memmove(buffer + pos, buffer + payload_pos, payload_end - payload_pos);
        pos += payload_end - payload_pos;
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
