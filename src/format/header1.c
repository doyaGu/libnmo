/**
 * @file header1.c
 * @brief NMO Header1 (object descriptors and plugin dependencies) implementation
 */

#include "format/nmo_header1.h"
#include "core/nmo_utils.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

static int nmo_safe_mul_size(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return 1;
    }
    if (a > (SIZE_MAX / b)) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int nmo_safe_add_size(size_t a, size_t b, size_t *out) {
    if (a > (SIZE_MAX - b)) {
        return 0;
    }
    *out = a + b;
    return 1;
}

/* Helper macros for safe buffer reading */
#define CHECK_BUFFER_SIZE(pos, needed, size) \
    do { \
        if (!nmo_check_buffer_bounds((pos), (needed), (size))) { \
            fprintf(stderr, "[ERROR] Buffer overrun: pos=%zu, needed=%zu, size=%zu, total=%zu\n", \
                    (size_t)(pos), (size_t)(needed), (size_t)(size), (size_t)((pos)+(needed))); \
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer overrun while reading Header1"); \
        } \
    } while (0)

/**
 * @brief Parse object descriptors from buffer
 */
static nmo_status_t parse_objects(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    /* NOTE: Object count is already set from file header, not read from buffer */
    /* In Virtools file version 8+, Header1 does not contain object count */

    if (header->object_count == 0) {
        header->objects = NULL;
        NMO_RETURN_OK();
    }

    if (header->object_count > (uint32_t)(size / (4u * sizeof(uint32_t)))) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Object descriptor count exceeds header size");
    }

    /* Allocate object array */
    size_t objects_size = 0;
    if (!nmo_safe_mul_size(header->object_count, sizeof(nmo_object_desc_t), &objects_size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Object descriptor allocation overflow");
    }
    header->objects = (nmo_object_desc_t *) nmo_arena_alloc(arena, objects_size, 8);
    if (header->objects == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object descriptor array");
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

        /* Read name string (if any) */
        if (name_len > 0) {
            CHECK_BUFFER_SIZE(*pos, name_len, size);
        }

        /* Allocate for name + null terminator */
        obj->name = (char *) nmo_arena_alloc(arena, (size_t)name_len + 1, 1);
        if (obj->name == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object name");
        }

        if (name_len > 0) {
            memcpy(obj->name, data + *pos, name_len);
            *pos += name_len;
        }

        /* Add null terminator */
        obj->name[name_len] = '\0';
    }

    NMO_RETURN_OK();
}

/**
 * @brief Parse plugin dependencies from buffer
 */
static nmo_status_t parse_plugin_deps(
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
        NMO_RETURN_OK();
    }

    /* Count total number of plugins across all categories */
    size_t total_plugins = 0;
    size_t saved_pos = *pos;

    for (uint32_t i = 0; i < category_count; i++) {
        /* Read category type */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        *pos += sizeof(uint32_t);

        /* Read GUID count for this category */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        uint32_t guid_count = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        if (total_plugins > SIZE_MAX - guid_count) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Plugin dependency count overflow");
        }
        total_plugins += guid_count;

        /* Skip GUIDs (sizeof(nmo_guid_t) each) */
        size_t guid_bytes = 0;
        if (!nmo_safe_mul_size(guid_count, sizeof(nmo_guid_t), &guid_bytes)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Plugin GUID size overflow");
        }
        CHECK_BUFFER_SIZE(*pos, guid_bytes, size);
        *pos += guid_bytes;
    }

    /* Reset position to start of categories */
    *pos = saved_pos;
    if (total_plugins > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Plugin dependency count overflow");
    }
    header->plugin_dep_count = (uint32_t)total_plugins;

    if (total_plugins == 0) {
        header->plugin_deps = NULL;
        NMO_RETURN_OK();
    }

    /* Allocate plugin dependency array */
    size_t deps_size = 0;
    if (!nmo_safe_mul_size(total_plugins, sizeof(nmo_plugin_dep_t), &deps_size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Plugin dependency allocation overflow");
    }
    header->plugin_deps = (nmo_plugin_dep_t *) nmo_arena_alloc(arena, deps_size, 8);
    if (header->plugin_deps == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate plugin dependency array");
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

    NMO_RETURN_OK();
}

/**
 * @brief Parse included files metadata from buffer
 */
static nmo_status_t parse_included_files(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    if (*pos + sizeof(uint32_t) > size) {
        NMO_RETURN_OK();
    }

    CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
    uint32_t payload_size = nmo_read_u32_le(data + *pos);
    *pos += sizeof(uint32_t);

    header->included_file_count = 0;
    header->included_files = NULL;

    if (payload_size == 0) {
        NMO_RETURN_OK();
    }

    if (payload_size < sizeof(uint32_t)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Invalid included files payload size");
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
        NMO_RETURN_OK();
    }

    if (file_count > (uint32_t)(payload_remaining / (2u * sizeof(uint32_t)))) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Included file count exceeds payload size");
    }

    size_t files_size = 0;
    if (!nmo_safe_mul_size(file_count, sizeof(nmo_included_file_desc_t), &files_size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Included files allocation overflow");
    }

    header->included_files = (nmo_included_file_desc_t *)nmo_arena_alloc(
        arena, files_size, 8);
    if (header->included_files == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate included files array");
    }

    for (uint32_t i = 0; i < file_count; i++) {
        nmo_included_file_desc_t *entry = &header->included_files[i];
        entry->name = NULL;
        entry->data_size = 0;

        if (*pos + sizeof(uint32_t) > payload_end) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Included files payload truncated (name length)");
        }

        uint32_t name_len = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        if (name_len > 0) {
            if (*pos + name_len > payload_end) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Included files payload truncated (name)");
            }
        }

        entry->name = (char *)nmo_arena_alloc(arena, (size_t)name_len + 1, 1);
        if (entry->name == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate included file name");
        }

        if (name_len > 0) {
            memcpy(entry->name, data + *pos, name_len);
            *pos += name_len;
        }
        entry->name[name_len] = '\0';

        if (*pos + sizeof(uint32_t) > payload_end) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Included files payload truncated (data size)");
        }

        entry->data_size = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);
    }

    if (*pos < payload_end) {
        *pos = payload_end;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Parse Header1 from buffer
 */
nmo_status_t nmo_header1_parse(
    const void *data,
    size_t size,
    nmo_header1_t *header,
    nmo_arena_t *arena) {
    if (data == NULL || header == NULL || arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL pointer passed to nmo_header1_parse");
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
    nmo_status_t result = parse_objects(buffer, size, &pos, header, arena);
    NMO_RETURN_IF_ERROR(result);

    /* Parse plugin dependencies (if data remains) */
    if (pos < size) {
        result = parse_plugin_deps(buffer, size, &pos, header, arena);
        NMO_RETURN_IF_ERROR(result);
    }

    header->included_file_count = 0;
    header->included_files = NULL;

    if (pos < size) {
        result = parse_included_files(buffer, size, &pos, header, arena);
        NMO_RETURN_IF_ERROR(result);
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize object descriptors to buffer
 */
static nmo_status_t serialize_objects(
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
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for file ID");
        }
        uint32_t file_id = obj->file_id;
        if (obj->flags & NMO_OBJECT_REFERENCE_FLAG) {
            file_id |= NMO_OBJECT_REFERENCE_FLAG;
        }
        nmo_write_u32_le(buffer + *pos, file_id);
        *pos += sizeof(uint32_t);

        /* Write class ID (ObjectCid) */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for class ID");
        }
        nmo_write_u32_le(buffer + *pos, obj->class_id);
        *pos += sizeof(uint32_t);

        /* Write file index (FileIndex) */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for file index");
        }
        nmo_write_u32_le(buffer + *pos, obj->file_index);
        *pos += sizeof(uint32_t);

        /* Calculate name length (does NOT include null terminator) */
        uint32_t name_len = obj->name ? (uint32_t) strlen(obj->name) : 0;

        /* Write name length */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for name length");
        }
        nmo_write_u32_le(buffer + *pos, name_len);
        *pos += sizeof(uint32_t);

        /* Write name string (without null terminator) */
        if (name_len > 0) {
            if (*pos + name_len > buffer_size) {
                NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for name string");
            }
            memcpy(buffer + *pos, obj->name, name_len);
            *pos += name_len;
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize plugin dependencies to buffer
 */
static nmo_status_t serialize_plugin_deps(
    const nmo_header1_t *header,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *pos) {
    if (header->plugin_dep_count == 0) {
        /* Write category count of 0 */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for category count");
        }
        nmo_write_u32_le(buffer + *pos, 0);
        *pos += sizeof(uint32_t);
        NMO_RETURN_OK();
    }

    /* Build unique category list in first-seen order (CK2 preserves array order). */
    nmo_last_error_clear();
    nmo_status_t result = NMO_OK;
    uint32_t unique_count = 0;
    size_t ordering_bytes = 0;
    if (!nmo_safe_mul_size(header->plugin_dep_count, sizeof(uint32_t), &ordering_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Plugin category allocation overflow");
    }
    
    nmo_allocator_t alloc = nmo_allocator_default();
    uint32_t *category_ordering = (uint32_t *) nmo_alloc(&alloc, ordering_bytes, _Alignof(uint32_t));
    if (category_ordering == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate plugin category order");
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
        NMO_SET_LAST_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for category count");
        result = NMO_ERR_BUFFER_OVERRUN;
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
            NMO_SET_LAST_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for category type");
            result = NMO_ERR_BUFFER_OVERRUN;
            goto cleanup;
        }
        nmo_write_u32_le(buffer + *pos, cat);
        *pos += sizeof(uint32_t);

        /* Write GUID count */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_SET_LAST_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for GUID count");
            result = NMO_ERR_BUFFER_OVERRUN;
            goto cleanup;
        }
        nmo_write_u32_le(buffer + *pos, cat_count);
        *pos += sizeof(uint32_t);

        /* Write GUIDs for this category */
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            if (header->plugin_deps[i].category == cat) {
                if (*pos + sizeof(nmo_guid_t) > buffer_size) {
                    NMO_SET_LAST_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for GUID");
                    result = NMO_ERR_BUFFER_OVERRUN;
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
    nmo_free(&alloc, category_ordering);
    return result;
}

/**
 * @brief Calculate required buffer size for serialization
 */
static nmo_status_t calculate_serialize_size(const nmo_header1_t *header, size_t *out_size) {
    size_t size = 0;

    if (header == NULL || out_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid header size arguments");
    }

    /* NOTE: Object count is NOT in buffer - it's in file header */

    /* Object descriptors */
    for (uint32_t i = 0; i < header->object_count; i++) {
        size_t name_len = header->objects[i].name ? strlen(header->objects[i].name) : 0;
        if (!nmo_safe_add_size(size, sizeof(uint32_t) * 4u, &size)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (object fields)");
        }
        if (!nmo_safe_add_size(size, name_len, &size)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (object name)");
        }
    }

    /* Plugin dependencies */
    if (!nmo_safe_add_size(size, sizeof(uint32_t), &size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category count)");
    }

    if (header->plugin_dep_count > 0) {
        uint32_t unique_count = 0;
        size_t ordering_bytes = 0;
        if (!nmo_safe_mul_size(header->plugin_dep_count, sizeof(uint32_t), &ordering_bytes)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category ordering)");
        }

        nmo_allocator_t alloc = nmo_allocator_default();
        uint32_t *category_ordering = (uint32_t *) nmo_alloc(&alloc, ordering_bytes, _Alignof(uint32_t));
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
                if (!nmo_safe_add_size(size, sizeof(uint32_t) * 2u, &size)) {
                    nmo_free(&alloc, category_ordering);
                    NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category header)");
                }
                size_t guid_bytes = 0;
                if (!nmo_safe_mul_size(cat_count, sizeof(nmo_guid_t), &guid_bytes) ||
                    !nmo_safe_add_size(size, guid_bytes, &size)) {
                    nmo_free(&alloc, category_ordering);
                    NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category GUIDs)");
                }
            }

            nmo_free(&alloc, category_ordering);
        } else {
            /* Fallback: assume each dep is its own category (over-allocate) */
            size_t per_dep = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(nmo_guid_t);
            size_t deps_size = 0;
            if (!nmo_safe_mul_size(header->plugin_dep_count, per_dep, &deps_size) ||
                !nmo_safe_add_size(size, deps_size, &size)) {
                NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (plugin deps)");
            }
        }
    }

    if (!nmo_safe_add_size(size, sizeof(uint32_t) * 2u, &size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (included metadata)");
    }

    for (uint32_t i = 0; i < header->included_file_count; i++) {
        size_t name_len = header->included_files && header->included_files[i].name
                           ? strlen(header->included_files[i].name)
                           : 0;
        if (!nmo_safe_add_size(size, sizeof(uint32_t) * 2u, &size) ||
            !nmo_safe_add_size(size, name_len, &size)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (included file entry)");
        }
    }

    *out_size = size;
    NMO_RETURN_OK();
}

/**
 * @brief Serialize Header1 to buffer
 */
nmo_status_t nmo_header1_serialize(
    const nmo_header1_t *header,
    void **out_data,
    size_t *out_size,
    nmo_arena_t *arena) {
    if (header == NULL || out_data == NULL || out_size == NULL || arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL pointer passed to nmo_header1_serialize");
    }

    if (header->included_file_count > 0 && header->included_files == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Included file count set without descriptors");
    }

    /* Calculate required buffer size */
    size_t buffer_size = 0;
    nmo_status_t size_result = calculate_serialize_size(header, &buffer_size);
    NMO_RETURN_IF_ERROR(size_result);

    /* Allocate buffer */
    uint8_t *buffer = (uint8_t *) nmo_arena_alloc(arena, buffer_size, 1);
    if (buffer == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate serialization buffer");
    }

    size_t pos = 0;

    /* Serialize object descriptors */
    nmo_status_t result = serialize_objects(header, buffer, buffer_size, &pos);
    NMO_RETURN_IF_ERROR(result);

    /* Serialize plugin dependencies */
    result = serialize_plugin_deps(header, buffer, buffer_size, &pos);
    NMO_RETURN_IF_ERROR(result);

    if (pos + (2 * sizeof(uint32_t)) > buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for included metadata");
    }

    size_t payload_start = pos + sizeof(uint32_t);
    size_t payload_pos = payload_start + sizeof(uint32_t);
    size_t payload_end = payload_pos;

    if (header->included_file_count > 0 && header->included_files != NULL) {
        for (uint32_t i = 0; i < header->included_file_count; i++) {
            const nmo_included_file_desc_t *entry = &header->included_files[i];
            uint32_t name_len = entry->name ? (uint32_t) strlen(entry->name) : 0;

            if (payload_end + sizeof(uint32_t) + name_len + sizeof(uint32_t) > buffer_size) {
                NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for included file entry");
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

    NMO_RETURN_OK();
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
