/**
 * @file ckattributemanager_schemas.c
 * @brief CKAttributeManager schema implementation
 *
 * Implements schema-driven deserialization for CKAttributeManager (attribute type registry).
 * This is a manager class that handles attribute type definitions and categories.
 * 
 * Based on official Virtools SDK (reference/src/CKAttributeManager.cpp:726-890).
 */

#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(attributemanager, nmo_attributemanager_state_t)

/* =============================================================================
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKAttributeManager.cpp */
#define CK_STATESAVE_ATTRIBUTEMANAGER 0x52

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_attributemanager_fields[] = {
    NMO_FIELD(nmo_attributemanager_state_t, category_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_attributemanager_state_t, categories, category_count, 1, NMO_GUID_STRUCT_CKATTRIBUTECATEGORY),
    NMO_FIELD(nmo_attributemanager_state_t, attribute_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_attributemanager_state_t, attributes, attribute_count, 1, NMO_GUID_STRUCT_CKATTRIBUTEDESCRIPTOR)
};

/* =============================================================================
 * CKAttributeManager DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKAttributeManager state from chunk
 * 
 * Implements the symmetric read operation for CKAttributeManager::LoadData.
 * Reads attribute categories and attribute type definitions.
 * 
 * Reference: reference/src/CKAttributeManager.cpp:726-790
 * 
 * @param chunk Chunk containing CKAttributeManager data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static bool nmo_attributemanager_size_mul_overflows(
    size_t count,
    size_t element_size)
{
    return count != 0 && element_size > SIZE_MAX / count;
}

static nmo_status_t nmo_attributemanager_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_attributemanager_state_t *out_state = (nmo_attributemanager_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_attributemanager_deserialize");
    }

    /* Seek identifier */
    size_t section_dwords = 0;
    nmo_status_t result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_ATTRIBUTEMANAGER, &section_dwords);
    if (result == NMO_ERR_NOT_FOUND) {
        /* No data to load - this is valid */
        NMO_RETURN_OK();
    }
    if (result != NMO_OK) return result;
    const size_t section_end =
        nmo_chunk_get_position(chunk) + section_dwords;

    /* Read counts */
    int32_t category_count, attribute_count;
    result = nmo_chunk_read_int(chunk, &category_count);
    if (result != NMO_OK) return result;

    result = nmo_chunk_read_int(chunk, &attribute_count);
    if (result != NMO_OK) return result;
    if (nmo_chunk_get_position(chunk) > section_end) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    if (category_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid category count");
    }

    if (attribute_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid attribute count");
    }
    const size_t minimum_entry_dwords =
        (size_t)category_count + (size_t)attribute_count;
    if (minimum_entry_dwords >
        section_end - nmo_chunk_get_position(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Attribute manager counts exceed remaining DWORDs");
    }
    if (minimum_entry_dwords > 0 && arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Attribute manager deserialization requires an arena");
    }
    if (nmo_attributemanager_size_mul_overflows(
            (size_t)category_count, sizeof(nmo_attribute_category_t)) ||
        nmo_attributemanager_size_mul_overflows(
            (size_t)attribute_count, sizeof(nmo_attribute_descriptor_t))) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Attribute manager allocation size overflows");
    }

    nmo_attribute_category_t *categories = NULL;
    nmo_attribute_descriptor_t *attributes = NULL;

    /* Allocate categories */
    if (category_count > 0) {
        categories = (nmo_attribute_category_t *)nmo_arena_alloc(
            arena, category_count * sizeof(nmo_attribute_category_t),
            _Alignof(nmo_attribute_category_t));
        if (!categories) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate categories");
        }
        memset(categories, 0, category_count * sizeof(nmo_attribute_category_t));

        /* Read each category */
        for (int32_t i = 0; i < category_count; i++) {
            int32_t present;
            result = nmo_chunk_read_int(chunk, &present);
            if (result != NMO_OK) return result;

            nmo_attribute_category_t *cat = &categories[i];
            cat->present = (present != 0);

            if (cat->present) {
                char *name = NULL;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &name, NULL));
                if (name == NULL) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                     NMO_SEVERITY_ERROR,
                                     "Attribute category name is missing");
                }
                cat->name = name;

                result = nmo_chunk_read_dword(chunk, &cat->flags);
                if (result != NMO_OK) return result;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
        }
    }

    /* Allocate attributes */
    if (attribute_count > 0) {
        attributes = (nmo_attribute_descriptor_t *)nmo_arena_alloc(
            arena, attribute_count * sizeof(nmo_attribute_descriptor_t),
            _Alignof(nmo_attribute_descriptor_t));
        if (!attributes) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate attributes");
        }
        memset(attributes, 0, attribute_count * sizeof(nmo_attribute_descriptor_t));

        /* Read each attribute */
        for (int32_t i = 0; i < attribute_count; i++) {
            int32_t present;
            result = nmo_chunk_read_int(chunk, &present);
            if (result != NMO_OK) return result;

            nmo_attribute_descriptor_t *attr = &attributes[i];
            attr->present = (present != 0);

            if (attr->present) {
                char *name = NULL;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_string_checked(chunk, &name, NULL));
                if (name == NULL) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                     NMO_SEVERITY_ERROR,
                                     "Attribute name is missing");
                }
                attr->name = name;

                result = nmo_chunk_read_guid(chunk, &attr->parameter_type_guid);
                if (result != NMO_OK) return result;

                result = nmo_chunk_read_int(chunk, &attr->category_index);
                if (result != NMO_OK) return result;

                result = nmo_chunk_read_int(chunk, &attr->compatible_class_id);
                if (result != NMO_OK) return result;

                result = nmo_chunk_read_dword(chunk, &attr->flags);
                if (result != NMO_OK) return result;
            }
            if (nmo_chunk_get_position(chunk) > section_end) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
        }
    }

    if (nmo_chunk_get_position(chunk) != section_end) {
        return NMO_ERR_INVALID_FORMAT;
    }

    out_state->category_count = (uint32_t)category_count;
    out_state->categories = categories;
    out_state->attribute_count = (uint32_t)attribute_count;
    out_state->attributes = attributes;

    NMO_RETURN_OK();
}

nmo_status_t nmo_attributemanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_attributemanager_state_t *out_state =
        (nmo_attributemanager_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_attributemanager_state_t decoded = {0};
    nmo_status_t result = nmo_attributemanager_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKAttributeManager SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKAttributeManager state to chunk
 * 
 * Implements the symmetric write operation for CKAttributeManager::SaveData.
 * Writes attribute categories and attribute type definitions.
 * 
 * Reference: reference/src/CKAttributeManager.cpp:795-890
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_status_t nmo_attributemanager_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_attributemanager_state_t *in_state =
        (const nmo_attributemanager_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_attributemanager_serialize");
    }
    if (in_state->category_count > INT32_MAX ||
        in_state->attribute_count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Attribute manager counts exceed format limits");
    }
    if (in_state->category_count > 0 && in_state->categories == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Attribute categories missing");
    }
    if (in_state->attribute_count > 0 && in_state->attributes == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Attribute descriptors missing");
    }

    nmo_status_t result;

    /* Write identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ATTRIBUTEMANAGER);
    if (result != NMO_OK) return result;

    /* Write counts */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->category_count);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->attribute_count);
    if (result != NMO_OK) return result;

    /* Write categories */
    for (uint32_t i = 0; i < in_state->category_count; i++) {
        const nmo_attribute_category_t *cat = &in_state->categories[i];

        result = nmo_chunk_write_int(out_chunk, cat->present ? 1 : 0);
        if (result != NMO_OK) return result;

        if (cat->present) {
            if (cat->name == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                 NMO_SEVERITY_ERROR,
                                 "Attribute category name is missing");
            }
            result = nmo_chunk_write_string(out_chunk, cat->name);
            if (result != NMO_OK) return result;

            result = nmo_chunk_write_dword(out_chunk, cat->flags);
            if (result != NMO_OK) return result;
        }
    }

    /* Write attributes */
    for (uint32_t i = 0; i < in_state->attribute_count; i++) {
        const nmo_attribute_descriptor_t *attr = &in_state->attributes[i];

        result = nmo_chunk_write_int(out_chunk, attr->present ? 1 : 0);
        if (result != NMO_OK) return result;

        if (attr->present) {
            if (attr->name == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED,
                                 NMO_SEVERITY_ERROR,
                                 "Attribute name is missing");
            }
            result = nmo_chunk_write_string(out_chunk, attr->name);
            if (result != NMO_OK) return result;

            result = nmo_chunk_write_guid(out_chunk, attr->parameter_type_guid);
            if (result != NMO_OK) return result;

            result = nmo_chunk_write_int(out_chunk, attr->category_index);
            if (result != NMO_OK) return result;

            result = nmo_chunk_write_int(out_chunk, attr->compatible_class_id);
            if (result != NMO_OK) return result;

            result = nmo_chunk_write_dword(out_chunk, attr->flags);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_attributemanager_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_attributemanager_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * Vtable + registration
 * ============================================================================= */

static nmo_status_t nmo_attributemanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_attributemanager_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_attributemanager_validate(instance, type, context);
}

nmo_status_t nmo_attributemanager_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_attributemanager_remap_dependencies");
    }

    nmo_attributemanager_state_t *state = (nmo_attributemanager_state_t *)instance;

    return nmo_attributemanager_validate(state, NULL, NULL);
}

static nmo_status_t nmo_attributemanager_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_attributemanager_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_attributemanager_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_attributemanager_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    if (src == NULL || dst == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_attributemanager_validate(src, type, NULL));

    const nmo_attributemanager_state_t *source = src;
    nmo_attributemanager_state_t copied = {
        .category_count = source->category_count,
        .attribute_count = source->attribute_count,
    };
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&copied.categories, source->categories,
        sizeof(*copied.categories), copied.category_count));
    for (uint32_t i = 0; i < copied.category_count; ++i) {
        copied.categories[i].name = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_string(
            arena, (char **)&copied.categories[i].name,
            source->categories[i].name));
    }

    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&copied.attributes, source->attributes,
        sizeof(*copied.attributes), copied.attribute_count));
    for (uint32_t i = 0; i < copied.attribute_count; ++i) {
        copied.attributes[i].name = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_string(
            arena, (char **)&copied.attributes[i].name,
            source->attributes[i].name));
    }

    *(nmo_attributemanager_state_t *)dst = copied;
    return NMO_OK;
}

static nmo_status_t nmo_attributemanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;

    const nmo_attributemanager_state_t *state = instance;
    if (state->category_count > INT32_MAX ||
        state->attribute_count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (state->category_count > 0 && state->categories == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (state->attribute_count > 0 && state->attributes == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < state->category_count; ++i) {
        if (state->categories[i].present &&
            state->categories[i].name == NULL) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    for (uint32_t i = 0; i < state->attribute_count; ++i) {
        if (state->attributes[i].present &&
            state->attributes[i].name == NULL) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    return NMO_OK;
}

static bool nmo_attributemanager_string_equals(
    const char *lhs,
    const char *rhs)
{
    if (lhs == rhs) return true;
    return lhs != NULL && rhs != NULL && strcmp(lhs, rhs) == 0;
}

static bool nmo_attributemanager_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;

    const nmo_attributemanager_state_t *lhs = a;
    const nmo_attributemanager_state_t *rhs = b;
    if (lhs->category_count != rhs->category_count ||
        lhs->attribute_count != rhs->attribute_count) {
        return false;
    }
    if ((lhs->category_count > 0 &&
         (lhs->categories == NULL || rhs->categories == NULL)) ||
        (lhs->attribute_count > 0 &&
         (lhs->attributes == NULL || rhs->attributes == NULL))) {
        return false;
    }

    for (uint32_t i = 0; i < lhs->category_count; ++i) {
        const nmo_attribute_category_t *lhs_category = &lhs->categories[i];
        const nmo_attribute_category_t *rhs_category = &rhs->categories[i];
        if (!nmo_attributemanager_string_equals(
                lhs_category->name, rhs_category->name) ||
            lhs_category->flags != rhs_category->flags ||
            lhs_category->present != rhs_category->present) {
            return false;
        }
    }
    for (uint32_t i = 0; i < lhs->attribute_count; ++i) {
        const nmo_attribute_descriptor_t *lhs_attribute = &lhs->attributes[i];
        const nmo_attribute_descriptor_t *rhs_attribute = &rhs->attributes[i];
        if (!nmo_attributemanager_string_equals(
                lhs_attribute->name, rhs_attribute->name) ||
            !nmo_guid_equals(lhs_attribute->parameter_type_guid,
                             rhs_attribute->parameter_type_guid) ||
            lhs_attribute->category_index != rhs_attribute->category_index ||
            lhs_attribute->compatible_class_id !=
                rhs_attribute->compatible_class_id ||
            lhs_attribute->flags != rhs_attribute->flags ||
            lhs_attribute->present != rhs_attribute->present) {
            return false;
        }
    }
    return true;
}

static uint32_t nmo_attributemanager_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_attributemanager_hash_string(
    uint32_t hash,
    const char *string)
{
    const uint8_t present = string != NULL;
    hash = nmo_attributemanager_hash_bytes(
        hash, &present, sizeof(present));
    return present
        ? nmo_attributemanager_hash_bytes(
            hash, string, strlen(string) + 1u)
        : hash;
}

static uint32_t nmo_attributemanager_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_attributemanager_state_t *state = instance;
    uint32_t hash = 2166136261u;
#define NMO_ATTRIBUTEMANAGER_HASH_FIELD(value) \
    hash = nmo_attributemanager_hash_bytes( \
        hash, &(value), sizeof(value))
    NMO_ATTRIBUTEMANAGER_HASH_FIELD(state->category_count);
    if (state->category_count > 0 && state->categories == NULL) return hash;
    for (uint32_t i = 0; i < state->category_count; ++i) {
        const nmo_attribute_category_t *category = &state->categories[i];
        hash = nmo_attributemanager_hash_string(hash, category->name);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(category->flags);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(category->present);
    }
    NMO_ATTRIBUTEMANAGER_HASH_FIELD(state->attribute_count);
    if (state->attribute_count > 0 && state->attributes == NULL) return hash;
    for (uint32_t i = 0; i < state->attribute_count; ++i) {
        const nmo_attribute_descriptor_t *attribute = &state->attributes[i];
        hash = nmo_attributemanager_hash_string(hash, attribute->name);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(attribute->parameter_type_guid.d1);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(attribute->parameter_type_guid.d2);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(attribute->category_index);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(attribute->compatible_class_id);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(attribute->flags);
        NMO_ATTRIBUTEMANAGER_HASH_FIELD(attribute->present);
    }
#undef NMO_ATTRIBUTEMANAGER_HASH_FIELD
    return hash;
}

nmo_type_vtable_t nmo_attributemanager_vtable = {
    .prepare_dependencies = nmo_attributemanager_prepare_dependencies,
    .remap_dependencies = nmo_attributemanager_remap_dependencies,
    .pre_delete = nmo_attributemanager_pre_delete,
    .post_delete = nmo_attributemanager_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_attributemanager_create,
        nmo_attributemanager_destroy,
        nmo_attributemanager_serialize,
        nmo_attributemanager_deserialize,
        nmo_attributemanager_copy,
        nmo_attributemanager_validate,
        nmo_attributemanager_equals,
        nmo_attributemanager_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_attributemanager_type,
    NMO_MANAGER_GUID_ATTRIBUTE,
    "CKAttributeManager",
    0,
    NMO_GUID_NULL,
    nmo_attributemanager_state_t,
    &nmo_attributemanager_vtable,
    nmo_attributemanager_fields)





