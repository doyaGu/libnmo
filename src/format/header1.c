/**
 * @file header1.c
 * @brief NMO Header1 (object descriptors and plugin dependencies) implementation
 */

#include "format/nmo_header1.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* Helper macros for safe buffer reading */
#define CHECK_BUFFER_SIZE(pos, needed, size) \
    do { \
        if (!nmo_check_buffer_bounds((pos), (needed), (size))) { \
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

        /* Read file ID (Object) - may have sign bit set for reference-only */
        CHECK_BUFFER_SIZE(*pos, sizeof(uint32_t), size);
        obj->file_id = nmo_read_u32_le(data + *pos);
        *pos += sizeof(uint32_t);

        /* Extract reference-only flag from sign bit */
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
    (void)arena;

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
    CHECK_BUFFER_SIZE(*pos, payload_remaining, size);
    *pos += payload_remaining;

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

    nmo_header1_t staged;
    memset(&staged, 0, sizeof(staged));
    staged.object_count = header->object_count;

    const uint8_t *buffer = (const uint8_t *) data;
    size_t pos = 0;

    /* Parse object descriptors */
    nmo_status_t result = parse_objects(buffer, size, &pos, &staged, arena);
    NMO_RETURN_IF_ERROR(result);

    /* Parse plugin dependencies (if data remains) */
    if (pos < size) {
        result = parse_plugin_deps(buffer, size, &pos, &staged, arena);
        NMO_RETURN_IF_ERROR(result);
    }

    staged.included_file_count = 0;
    staged.included_files = NULL;

    if (pos < size) {
        result = parse_included_files(buffer, size, &pos, &staged, arena);
        NMO_RETURN_IF_ERROR(result);
    }

    *header = staged;
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
        size_t name_size = obj->name ? strlen(obj->name) : 0;
        if (name_size > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Object name length does not fit the file format");
        }
        uint32_t name_len = (uint32_t) name_size;

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
static nmo_status_t serialize_plugin_deps_planned(
    const nmo_header1_t *header,
    const nmo_header1_layout_t *layout,
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

    if (layout == NULL ||
        layout->plugin_categories == NULL ||
        layout->plugin_category_count == 0 ||
        layout->plugin_category_count > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid Header1 plugin layout");
    }

    /* Write category count */
    if (*pos + sizeof(uint32_t) > buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for category count");
    }
    nmo_write_u32_le(buffer + *pos, (uint32_t)layout->plugin_category_count);
    *pos += sizeof(uint32_t);

    /* Write each category in recorded order */
    for (size_t c = 0; c < layout->plugin_category_count; c++) {
        uint32_t cat = layout->plugin_categories[c];
        uint32_t cat_count = 0;
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            if (header->plugin_deps[i].category == cat) {
                cat_count++;
            }
        }

        /* Write category type */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for category type");
        }
        nmo_write_u32_le(buffer + *pos, cat);
        *pos += sizeof(uint32_t);

        /* Write GUID count */
        if (*pos + sizeof(uint32_t) > buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for GUID count");
        }
        nmo_write_u32_le(buffer + *pos, cat_count);
        *pos += sizeof(uint32_t);

        /* Write GUIDs for this category */
        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            if (header->plugin_deps[i].category == cat) {
                if (*pos + sizeof(nmo_guid_t) > buffer_size) {
                    NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for GUID");
                }
                nmo_write_u32_le(buffer + *pos, header->plugin_deps[i].guid.d1);
                *pos += sizeof(uint32_t);
                nmo_write_u32_le(buffer + *pos, header->plugin_deps[i].guid.d2);
                *pos += sizeof(uint32_t);
            }
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Validate Header1 serialization input
 */
static nmo_status_t validate_header1_for_write(const nmo_header1_t *header) {
    if (header == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL Header1");
    }
    if (header->object_count > 0 && header->objects == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Header1 object count is non-zero but object array is NULL");
    }
    if (header->plugin_dep_count > 0 && header->plugin_deps == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Header1 plugin dependency count is non-zero but dependency array is NULL");
    }
    NMO_RETURN_OK();
}

/**
 * @brief Plan required buffer size and plugin category order for serialization
 */
nmo_status_t nmo_header1_plan(
    const nmo_header1_t *header,
    nmo_arena_t *arena,
    nmo_header1_layout_t *out_layout) {
    if (out_layout != NULL) {
        memset(out_layout, 0, sizeof(*out_layout));
    }
    if (header == NULL || arena == NULL || out_layout == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid Header1 plan arguments");
    }
    NMO_RETURN_IF_ERROR(validate_header1_for_write(header));

    nmo_header1_layout_t staged = {0};

    /* NOTE: Object count is NOT in buffer - it's in file header */

    /* Object descriptors */
    for (uint32_t i = 0; i < header->object_count; i++) {
        size_t name_len = header->objects[i].name ? strlen(header->objects[i].name) : 0;
        if (name_len > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Object name length does not fit the file format");
        }
        if (!nmo_safe_add_size(staged.object_table_size,
                               sizeof(uint32_t) * 4u,
                               &staged.object_table_size)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (object fields)");
        }
        if (!nmo_safe_add_size(staged.object_table_size, name_len, &staged.object_table_size)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (object name)");
        }
    }

    /* Plugin dependencies */
    staged.plugin_dep_size = sizeof(uint32_t);

    if (header->plugin_dep_count > 0) {
        size_t ordering_bytes = 0;
        if (!nmo_safe_mul_size(header->plugin_dep_count, sizeof(uint32_t), &ordering_bytes)) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category ordering)");
        }

        staged.plugin_categories = (uint32_t *)nmo_arena_alloc(arena, ordering_bytes, _Alignof(uint32_t));
        if (staged.plugin_categories == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate plugin category order");
        }

        for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
            uint32_t cat = header->plugin_deps[i].category;
            int found = 0;
            for (size_t j = 0; j < staged.plugin_category_count; j++) {
                if (staged.plugin_categories[j] == cat) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                staged.plugin_categories[staged.plugin_category_count++] = cat;
            }
        }

        for (size_t c = 0; c < staged.plugin_category_count; c++) {
            uint32_t cat = staged.plugin_categories[c];
            uint32_t cat_count = 0;
            for (uint32_t i = 0; i < header->plugin_dep_count; i++) {
                if (header->plugin_deps[i].category == cat) {
                    cat_count++;
                }
            }
            if (!nmo_safe_add_size(staged.plugin_dep_size,
                                   sizeof(uint32_t) * 2u,
                                   &staged.plugin_dep_size)) {
                NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category header)");
            }
            size_t guid_bytes = 0;
            if (!nmo_safe_mul_size(cat_count, sizeof(nmo_guid_t), &guid_bytes) ||
                !nmo_safe_add_size(staged.plugin_dep_size, guid_bytes, &staged.plugin_dep_size)) {
                NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow (category GUIDs)");
            }
        }
    }

    staged.included_metadata_size = sizeof(uint32_t) * 2u;

    if (!nmo_safe_add_size(staged.object_table_size,
                           staged.plugin_dep_size,
                           &staged.total_size) ||
        !nmo_safe_add_size(staged.total_size,
                           staged.included_metadata_size,
                           &staged.total_size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Header1 size overflow");
    }

    *out_layout = staged;
    NMO_RETURN_OK();
}

/**
 * @brief Serialize Header1 using a planned layout
 */
nmo_status_t nmo_header1_write_planned(
    const nmo_header1_t *header,
    const nmo_header1_layout_t *layout,
    nmo_arena_t *arena,
    uint8_t **out_buffer,
    size_t *out_size) {
    if (out_buffer != NULL) {
        *out_buffer = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0;
    }
    if (header == NULL || layout == NULL || arena == NULL || out_buffer == NULL || out_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid Header1 planned write arguments");
    }
    NMO_RETURN_IF_ERROR(validate_header1_for_write(header));

    size_t expected_total = 0;
    if (!nmo_safe_add_size(layout->object_table_size, layout->plugin_dep_size, &expected_total) ||
        !nmo_safe_add_size(expected_total, layout->included_metadata_size, &expected_total) ||
        expected_total != layout->total_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid Header1 layout sizes");
    }

    /* Allocate buffer */
    uint8_t *buffer = (uint8_t *) nmo_arena_alloc(arena, layout->total_size, 1);
    if (buffer == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate serialization buffer");
    }

    size_t pos = 0;

    /* Serialize object descriptors */
    nmo_status_t result = serialize_objects(header, buffer, layout->total_size, &pos);
    NMO_RETURN_IF_ERROR(result);
    if (pos != layout->object_table_size) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Header1 object layout size mismatch");
    }

    /* Serialize plugin dependencies */
    result = serialize_plugin_deps_planned(header, layout, buffer, layout->total_size, &pos);
    NMO_RETURN_IF_ERROR(result);
    if (pos != layout->object_table_size + layout->plugin_dep_size) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Header1 plugin layout size mismatch");
    }

    if (pos + (2 * sizeof(uint32_t)) > layout->total_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for included metadata");
    }

    uint32_t payload_size = sizeof(uint32_t);
    nmo_write_u32_le(buffer + pos, payload_size);
    pos += sizeof(uint32_t);
    nmo_write_u32_le(buffer + pos, header->included_file_count);
    pos += sizeof(uint32_t);

    if (pos != layout->total_size) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Header1 total layout size mismatch");
    }

    *out_buffer = buffer;
    *out_size = pos;

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
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0;
    }
    if (header == NULL || out_data == NULL || out_size == NULL || arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL pointer passed to nmo_header1_serialize");
    }

    nmo_header1_layout_t layout = {0};
    NMO_RETURN_IF_ERROR(nmo_header1_plan(header, arena, &layout));

    uint8_t *buffer = NULL;
    NMO_RETURN_IF_ERROR(nmo_header1_write_planned(header, &layout, arena, &buffer, out_size));
    *out_data = buffer;

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
