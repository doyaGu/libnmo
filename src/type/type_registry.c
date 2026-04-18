/**
 * @file type_registry.c
 * @brief Implementation of unified type registry with GUID-based O(1) lookup
 *
 * Implements the design validated against Virtools SDK:
 * - GUID-indexed hash table (CKParameterManager.cpp:175-181)
 * - Slot recycling (CKParameterManager.cpp:11-37)
 * - Lazy derivation mask updates (CKParameterManager.cpp:1265-1276)
 * - Plugin tracking (CKParameterManager.cpp:38-47)
 */

#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "type_value_internal.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "core/nmo_bit_array.h"
#include "core/nmo_debug.h"
#include "core/nmo_utils.h"
#include "type/nmo_type_guids.h"
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <ctype.h>

/* ============================================================================
 * Compatibility Mask Helpers (nmo_bit_array_t)
 * ============================================================================ */

static nmo_allocator_t type_allocator_from_registry(const nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_allocator_default();
    }
    return registry->type_allocator;
}

static size_t compat_mask_growth_bits(size_t required_bits) {
    if (required_bits == 0) {
        return (size_t)NMO_TYPE_COMPAT_MASK_SIZE;
    }
    size_t chunk = (size_t)NMO_TYPE_COMPAT_MASK_SIZE;
    if (chunk == 0) {
        chunk = 256;
    }
    return ((required_bits + chunk - 1u) / chunk) * chunk;
}

void nmo_type_assign_default_vtable(
    nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry)
{
    if (!type || type->vtable) {
        return;
    }

    if (type->category & NMO_TYPE_CATEGORY_ENUM) {
        type->vtable = &nmo_type_vtable_enum;
        return;
    }
    if (type->category & NMO_TYPE_CATEGORY_FLAGS) {
        type->vtable = &nmo_type_vtable_flags;
        return;
    }
    if (type->category & NMO_TYPE_CATEGORY_OBJECT_REF) {
        type->vtable = &nmo_type_vtable_object_ref;
        return;
    }
    if (type->category & (NMO_TYPE_CATEGORY_STRUCT | NMO_TYPE_CATEGORY_UNION)) {
        type->vtable = &nmo_type_vtable_reflected_struct;
        return;
    }

    if (registry && !nmo_guid_is_null(type->base_type)) {
        const nmo_type_descriptor_t *base =
            nmo_type_registry_find_by_guid(registry, type->base_type);
        if (base && base->vtable) {
            type->vtable = base->vtable;
        }
    }
}

static nmo_status_t ensure_compat_mask_capacity(nmo_type_registry_t *registry, nmo_type_descriptor_t *type) {
    if (!registry || !type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to ensure_compat_mask_capacity");
    }

    if (!type->ext) {
        type->ext = (nmo_type_descriptor_ext_t *)nmo_alloc(
            &registry->type_allocator,
            sizeof(nmo_type_descriptor_ext_t),
            _Alignof(nmo_type_descriptor_ext_t));
        if (!type->ext) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate type extension");
        }
        memset(type->ext, 0, sizeof(*type->ext));
    }

    const size_t required_bits = compat_mask_growth_bits(registry->types.count);
    if (type->ext->compat_mask.bits.alloc.alloc == NULL) {
        nmo_allocator_t alloc = type_allocator_from_registry(registry);
        nmo_status_t init = nmo_bit_array_init(&type->ext->compat_mask.bits, required_bits, &alloc);
        if (init != NMO_OK) {
            return init;
        }
        NMO_RETURN_OK();
    }

    return nmo_bit_array_reserve(&type->ext->compat_mask.bits, required_bits);
}

/* ============================================================================
 * Note: nmo_type_registry_t is already defined in type_system.h
 * We just need to implement the functions that operate on it.
 * ============================================================================ */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Hash function adapter for GUID keys
 */
static size_t guid_hash_func(const void *key, size_t key_size) {
    (void)key_size; // Always sizeof(nmo_guid_t)
    return (size_t)nmo_guid_hash(*(const nmo_guid_t *)key);
}

/**
 * @brief Compare function adapter for GUID keys
 */
static int guid_compare_func(const void *key1, const void *key2, size_t key_size) {
    (void)key_size; // Always sizeof(nmo_guid_t)
    return !nmo_guid_equals(*(const nmo_guid_t *)key1, *(const nmo_guid_t *)key2);
}

/**
 * @brief Hash function for string keys
 */
static size_t string_hash_func(const void *key, size_t key_size) {
    (void)key_size;
    const char *str = *(const char **)key;
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

/**
 * @brief Compare function for string keys
 */
static int string_compare_func(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    const char *str1 = *(const char **)key1;
    const char *str2 = *(const char **)key2;
    return strcmp(str1, str2);
}

static bool pop_free_slot(nmo_type_registry_t *registry, size_t *out_slot) {
    if (!registry || !out_slot) {
        return false;
    }

    if (registry->free_slots.count == 0) {
        return false;
    }

    nmo_type_id_t slot = NMO_TYPE_ID_INVALID;
    nmo_arena_array_pop(&registry->free_slots, &slot);
    if (slot < 0) {
        return false;
    }
    *out_slot = (size_t)slot;
    return true;
}

static void push_free_slot(nmo_type_registry_t *registry, nmo_type_id_t slot) {
    if (!registry || slot < 0) {
        return;
    }
    nmo_arena_array_append(&registry->free_slots, &slot);
}

static nmo_type_alias_list_t *get_alias_list(nmo_type_registry_t *registry, nmo_type_id_t type_id) {
    if (!registry || type_id < 0 || (size_t)type_id >= registry->alias_lists.count) {
        return NULL;
    }
    return (nmo_type_alias_list_t *)nmo_arena_array_get(&registry->alias_lists, (size_t)type_id);
}

static nmo_type_child_list_t *get_child_list(nmo_type_registry_t *registry, nmo_type_id_t type_id) {
    if (!registry || type_id < 0 || (size_t)type_id >= registry->child_lists.count) {
        return NULL;
    }
    return (nmo_type_child_list_t *)nmo_arena_array_get(&registry->child_lists, (size_t)type_id);
}

static nmo_status_t alias_list_append(
    nmo_type_registry_t *registry,
    nmo_type_alias_list_t *list,
    const char *alias)
{
    if (!registry || !list || !alias) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to alias_list_append");
    }

    if (list->arr.arena == NULL) {
        nmo_status_t init = nmo_arena_array_init(&list->arr, sizeof(const char *), 4, registry->arena);
        if (init != NMO_OK) return init;
    }

    return nmo_arena_array_append(&list->arr, &alias);
}

static void free_alias_list(nmo_type_registry_t *registry, nmo_type_id_t type_id) {
    nmo_type_alias_list_t *list = get_alias_list(registry, type_id);
    if (!list || list->arr.count == 0) {
        return;
    }

    NMO_OWNERSHIP_EXPECT(list->alias_string_ownership, NMO_OWNERSHIP_HEAP);

    for (size_t i = 0; i < list->arr.count; i++) {
        const char **alias = (const char **)nmo_arena_array_get(&list->arr, i);
        if (alias && *alias) {
            nmo_free(&registry->type_allocator, (void *)*alias);
        }
    }

    nmo_arena_array_clear(&list->arr);
}

static nmo_status_t child_list_append(
    nmo_type_registry_t *registry,
    nmo_type_child_list_t *list,
    nmo_type_id_t child_id)
{
    if (!registry || !list || child_id < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to child_list_append");
    }

    if (list->arr.arena == NULL) {
        nmo_status_t init = nmo_arena_array_init(&list->arr, sizeof(nmo_type_id_t), 4, registry->arena);
        if (init != NMO_OK) return init;
    }

    return nmo_arena_array_append(&list->arr, &child_id);
}

static void child_list_remove(
    nmo_type_registry_t *registry,
    nmo_type_id_t parent_id,
    nmo_type_id_t child_id)
{
    nmo_type_child_list_t *list = get_child_list(registry, parent_id);
    if (!list || list->arr.count == 0) {
        return;
    }

    nmo_type_id_t *data = (nmo_type_id_t *)list->arr.data;
    for (size_t i = 0; i < list->arr.count; i++) {
        if (data[i] == child_id) {
            data[i] = data[list->arr.count - 1];
            list->arr.count--;
            return;
        }
    }
}

static bool child_list_contains(
    const nmo_type_child_list_t *list,
    nmo_type_id_t child_id)
{
    if (!list || list->arr.count == 0) {
        return false;
    }

    const nmo_type_id_t *data = (const nmo_type_id_t *)list->arr.data;
    for (size_t i = 0; i < list->arr.count; i++) {
        if (data[i] == child_id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t child_list_append_unique(
    nmo_type_registry_t *registry,
    nmo_type_id_t parent_id,
    nmo_type_id_t child_id)
{
    nmo_type_child_list_t *parent_list = get_child_list(registry, parent_id);
    if (!parent_list) {
        NMO_RETURN_OK();
    }
    if (child_list_contains(parent_list, child_id)) {
        NMO_RETURN_OK();
    }
    return child_list_append(registry, parent_list, child_id);
}

static nmo_status_t ensure_parent_child_link(
    nmo_type_registry_t *registry,
    nmo_type_descriptor_t *type)
{
    if (!registry || !type || !type->valid || nmo_guid_is_null(type->base_type)) {
        NMO_RETURN_OK();
    }

    nmo_type_id_t parent_id = type->base_type_id;
    if (parent_id == NMO_TYPE_ID_INVALID) {
        if (nmo_hash_table_get(registry->guid_map, &type->base_type, &parent_id) != NMO_OK) {
            NMO_RETURN_OK();
        }
        type->base_type_id = parent_id;
    }

    if (parent_id < 0 || (size_t)parent_id >= registry->types.count) {
        NMO_RETURN_OK();
    }

    return child_list_append_unique(registry, parent_id, type->id);
}

static void free_child_list(nmo_type_registry_t *registry, nmo_type_id_t type_id) {
    (void)registry;
    nmo_type_child_list_t *list = get_child_list(registry, type_id);
    if (!list) {
        return;
    }

    nmo_arena_array_clear(&list->arr);
}

static void free_type_storage(
    nmo_type_registry_t *registry,
    nmo_type_descriptor_t *type,
    nmo_type_id_t type_id)
{
    if (!registry || !type) {
        return;
    }

    free_alias_list(registry, type_id);
    free_child_list(registry, type_id);

    if (type->name) {
        nmo_free(&registry->type_allocator, (void *)type->name);
        type->name = NULL;
    }

    const bool is_incomplete_struct_state =
        (type->category == NMO_TYPE_CATEGORY_STRUCT) &&
        (type->valid == false) &&
        (type->fields == NULL) &&
        (type->field_count == 0);

    if (type->description) {
        if (!is_incomplete_struct_state) {
            nmo_free(&registry->type_allocator, (void *)type->description);
        }
        type->description = NULL;
    }

    if (type->fields && type->field_count > 0) {
        nmo_type_field_t *fields = (nmo_type_field_t *)type->fields;
        for (size_t i = 0; i < type->field_count; i++) {
            if (fields[i].name) {
                nmo_free(&registry->type_allocator, (void *)fields[i].name);
                fields[i].name = NULL;
            }
            if (fields[i].description) {
                nmo_free(&registry->type_allocator, (void *)fields[i].description);
                fields[i].description = NULL;
            }
            if (fields[i].default_value) {
                nmo_free(&registry->type_allocator, (void *)fields[i].default_value);
                fields[i].default_value = NULL;
            }
        }
        nmo_free(&registry->type_allocator, (void *)fields);
        type->fields = NULL;
        type->field_count = 0;
    }

    if (type->ext) {
        if (type->ext->hierarchy) {
            nmo_free(&registry->type_allocator, (void *)type->ext->hierarchy);
            type->ext->hierarchy = NULL;
        }
        if (type->ext->state_offsets) {
            nmo_free(&registry->type_allocator, (void *)type->ext->state_offsets);
            type->ext->state_offsets = NULL;
        }
        type->ext->hierarchy_depth = 0;
        type->ext->total_state_size = 0;

        nmo_bit_array_dispose(&type->ext->compat_mask.bits);
        nmo_free(&registry->type_allocator, type->ext);
        type->ext = NULL;
    }

    nmo_free(&registry->type_allocator, type);
}

static void reset_class_id_inherited_cache(nmo_type_registry_t *registry) {
    if (!registry) {
        return;
    }

    if (registry->class_id_inherited_map) {
        nmo_hash_table_destroy(registry->class_id_inherited_map);
    }

    registry->class_id_inherited_map = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t),
        sizeof(nmo_type_id_t),
        64,
        NULL,
        NULL);
    registry->class_id_inherited_version = registry->registry_version;
}

static nmo_type_id_t get_parent_type_id(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type)
{
    if (!registry || !type || nmo_guid_is_null(type->base_type)) {
        return NMO_TYPE_ID_INVALID;
    }

    if (type->base_type_id != NMO_TYPE_ID_INVALID) {
        return type->base_type_id;
    }

    nmo_type_id_t parent_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->guid_map, &type->base_type, &parent_id) != NMO_OK) {
        return NMO_TYPE_ID_INVALID;
    }

    if (parent_id < 0 || (size_t)parent_id >= registry->types.count) {
        return NMO_TYPE_ID_INVALID;
    }

    return parent_id;
}

static bool has_type_inheritance_cycle(
    const nmo_type_registry_t *registry,
    nmo_type_id_t start_id)
{
    if (!registry || start_id == NMO_TYPE_ID_INVALID) {
        return false;
    }

    nmo_type_id_t slow = start_id;
    nmo_type_id_t fast = start_id;

    while (true) {
        const nmo_type_descriptor_t *slow_type =
            *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, slow);
        slow = get_parent_type_id(registry, slow_type);
        if (slow == NMO_TYPE_ID_INVALID) {
            return false;
        }

        const nmo_type_descriptor_t *fast_type =
            *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, fast);
        fast = get_parent_type_id(registry, fast_type);
        if (fast == NMO_TYPE_ID_INVALID) {
            return false;
        }
        fast_type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, fast);
        fast = get_parent_type_id(registry, fast_type);
        if (fast == NMO_TYPE_ID_INVALID) {
            return false;
        }

        if (fast == slow) {
            return true;
        }
    }
}

static void ensure_class_id_inherited_cache(nmo_type_registry_t *registry) {
    if (!registry) {
        return;
    }

    if (!registry->class_id_inherited_map ||
        registry->class_id_inherited_version != registry->registry_version) {
        reset_class_id_inherited_cache(registry);
    }
}

static uint32_t class_parent_id_from_registry(
    const nmo_type_registry_t *registry,
    uint32_t class_id)
{
    if (!registry || class_id == 0) {
        return 0;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id(registry, class_id);
    if (!type || nmo_guid_is_null(type->base_type)) {
        return 0;
    }

    const nmo_type_descriptor_t *base_type =
        nmo_type_registry_find_by_guid(registry, type->base_type);
    if (!base_type) {
        return 0;
    }

    return base_type->class_id;
}

static nmo_status_t validate_type_descriptor(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *descriptor
) {
    (void)registry;
    if (!descriptor) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL type descriptor");
    }

    if (nmo_guid_is_null(descriptor->guid)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Type GUID must not be null");
    }

    if (descriptor->name && descriptor->name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Type name must not be empty");
    }

    if (!descriptor->valid) {
        const bool is_incomplete_struct =
            (descriptor->category == NMO_TYPE_CATEGORY_STRUCT) &&
            (descriptor->class_id == 0) &&
            (descriptor->fields == NULL) &&
            (descriptor->field_count == 0);
        if (is_incomplete_struct) {
            NMO_RETURN_OK();
        }
    }

    if (descriptor->size == 0) {
        const bool is_struct_like =
            (descriptor->category & (NMO_TYPE_CATEGORY_STRUCT |
                                     NMO_TYPE_CATEGORY_UNION |
                                     NMO_TYPE_CATEGORY_ENUM |
                                     NMO_TYPE_CATEGORY_FLAGS)) != 0u;
        const bool has_fields = (descriptor->fields != NULL) || (descriptor->field_count > 0);
        const bool allow_zero_size =
            nmo_guid_equals(descriptor->guid, CKPGUID_NONE) ||
            nmo_guid_equals(descriptor->guid, CKPGUID_VOIDBUF) ||
            (!is_struct_like && !has_fields && descriptor->class_id == 0);
        if (!allow_zero_size) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Type size must be non-zero");
        }
    }

    if (descriptor->alignment != 0 && !NMO_IS_POWER_OF_TWO(descriptor->alignment)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Type alignment must be a power of two");
    }

    if (nmo_guid_equals(descriptor->guid, descriptor->base_type)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Type cannot inherit from itself");
    }

    if ((descriptor->category & (NMO_TYPE_CATEGORY_ENUM | NMO_TYPE_CATEGORY_FLAGS)) != 0u) {
        if (descriptor->field_count > 0 || descriptor->fields != NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Enum/flags types must not define fields");
        }
    }

    if ((descriptor->fields == NULL && descriptor->field_count > 0) ||
        (descriptor->fields != NULL && descriptor->field_count == 0)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Field pointer/count mismatch");
    }

    if (descriptor->fields && descriptor->field_count > 0) {
        const bool is_union =
            (descriptor->category & NMO_TYPE_CATEGORY_UNION) != 0u;

        for (size_t i = 0; i < descriptor->field_count; i++) {
            const nmo_type_field_t *field = &descriptor->fields[i];
            if (!field->name || field->name[0] == '\0') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Field name must not be empty");
            }
            if (nmo_guid_is_null(field->type_guid)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Field type GUID must not be null");
            }
            if (field->size == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Field size must be non-zero");
            }
            if ((uint64_t)field->offset + (uint64_t)field->size > (uint64_t)descriptor->size) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Field '%s' exceeds type size", field->name);
            }
            if (nmo_field_uses_pointer_array_storage(field) &&
                (field->count_field_name == NULL || field->count_field_name[0] == '\0')) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Pointer array field '%s' must declare count_field_name",
                                 field->name);
            }
            if ((field->flags & NMO_FIELD_REPEATED) != 0u &&
                field->count_field_name != NULL &&
                field->count_field_name[0] != '\0') {
                if (field->count_multiplier == 0u) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                     "Array field '%s' must declare a non-zero count_multiplier",
                                     field->name);
                }
                bool found_count_field = false;
                for (size_t j = 0; j < descriptor->field_count; j++) {
                    const nmo_type_field_t *candidate = &descriptor->fields[j];
                    if (candidate->name != NULL &&
                        strcmp(candidate->name, field->count_field_name) == 0) {
                        found_count_field = true;
                        break;
                    }
                }
                if (!found_count_field) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                     "Array field '%s' references missing count field '%s'",
                                     field->name,
                                     field->count_field_name);
                }
            }

            /* Check for duplicate field names */
            for (size_t j = 0; j < i; j++) {
                if (strcmp(field->name, descriptor->fields[j].name) == 0) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                     "Duplicate field name '%s'", field->name);
                }
            }

            /* Check for field overlap (skip for unions where overlap is expected) */
            if (!is_union) {
                uint64_t a_start = field->offset;
                uint64_t a_end = (uint64_t)field->offset + (uint64_t)field->size;
                for (size_t j = 0; j < i; j++) {
                    uint64_t b_start = descriptor->fields[j].offset;
                    uint64_t b_end = (uint64_t)descriptor->fields[j].offset +
                                     (uint64_t)descriptor->fields[j].size;
                    if (a_start < b_end && b_start < a_end) {
                        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                         "Field '%s' overlaps with '%s'",
                                         field->name, descriptor->fields[j].name);
                    }
                }
            }
        }
    }

    if (descriptor->class_id != 0) {
        if (!descriptor->vtable ||
            !descriptor->vtable->serialize ||
            !descriptor->vtable->deserialize) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Object types must provide serialize/deserialize vtable callbacks");
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t ensure_registry_mutable(
    nmo_type_registry_t *registry,
    const char *action)
{
    if (registry && registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Type registry is finalized; cannot %s", action);
    }
    return NMO_OK;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

nmo_type_registry_t* nmo_type_registry_create(nmo_arena_t *arena) {
    return nmo_type_registry_create_ex(arena, nmo_allocator_default());
}

nmo_type_registry_t* nmo_type_registry_create_ex(nmo_arena_t *arena, nmo_allocator_t type_allocator) {
    if (!arena) return NULL;

    // Allocate registry struct
    nmo_type_registry_t *registry = nmo_arena_alloc(arena, 
        sizeof(nmo_type_registry_t), 
        _Alignof(nmo_type_registry_t));
    if (!registry) return NULL;

    // Initialize all fields to zero
    memset(registry, 0, sizeof(nmo_type_registry_t));
    
    registry->arena = arena;
    registry->type_allocator = nmo_allocator_debug_init(
        &registry->type_allocator_debug,
        type_allocator,
        "type_registry",
        "type_allocator");
    registry->derivation_masks_valid = false;
    registry->finalized = false;

    // Initialize type array
    if (nmo_arena_array_init(&registry->types,
                             sizeof(nmo_type_descriptor_t*),
                             32,
                             arena) != NMO_OK) {
        goto fail;
    }

    // Initialize alias list array (parallel to types)
    if (nmo_arena_array_init(&registry->alias_lists,
                             sizeof(nmo_type_alias_list_t),
                             32,
                             arena) != NMO_OK) {
        goto fail;
    }

    if (nmo_arena_array_init(&registry->child_lists,
                             sizeof(nmo_type_child_list_t),
                             32,
                             arena) != NMO_OK) {
        goto fail;
    }

    if (nmo_arena_array_init(&registry->free_slots,
                             sizeof(nmo_type_id_t),
                             32,
                             arena) != NMO_OK) {
        goto fail;
    }

    // Create GUID hash table
    registry->guid_map = nmo_hash_table_create(
        NULL, // Use default allocator
        sizeof(nmo_guid_t),
        sizeof(nmo_type_id_t),
        128,
        guid_hash_func,
        guid_compare_func
    );
    if (!registry->guid_map) goto fail;

    // Create name hash table
    registry->name_map = nmo_hash_table_create(
        NULL,
        sizeof(const char *),
        sizeof(nmo_type_id_t),
        128,
        string_hash_func,
        string_compare_func
    );
    if (!registry->name_map) goto fail;

    // Create class_id hash table (for Virtools object types)
    registry->class_id_map = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t),
        sizeof(nmo_type_id_t),
        64,  // Most Virtools files have < 64 object types
        NULL,  // Use default uint32 hash
        NULL   // Use default uint32 compare
    );
    if (!registry->class_id_map) goto fail;

    // Create inherited class_id cache map
    registry->class_id_inherited_map = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t),
        sizeof(nmo_type_id_t),
        64,
        NULL,
        NULL
    );
    if (!registry->class_id_inherited_map) goto fail;

    // Create plugin hash table
    registry->type_to_plugin = nmo_hash_table_create(
        NULL,
        sizeof(nmo_type_id_t),
        sizeof(nmo_guid_t),
        128,
        NULL, // Default hash for integers
        NULL  // Default compare for integers
    );
    if (!registry->type_to_plugin) goto fail;

    // Create metadata management structures
    registry->type_to_metadata = nmo_hash_table_create(
        NULL,
        sizeof(nmo_type_id_t),
        sizeof(size_t),  // metadata index
        64,
        NULL,  // Default hash for integers
        NULL   // Default compare for integers
    );
    if (!registry->type_to_metadata) goto fail;

    // Initialize metadata array (lazy init with 0 capacity)
    if (nmo_arena_array_init(&registry->metadata,
                             sizeof(nmo_specialized_metadata_t*),
                             0,
                             arena) != NMO_OK) {
        goto fail;
    }
    
    // Initialize saver managers array (lazy init with 0 capacity)
    if (nmo_arena_array_init(&registry->saver_managers,
                             sizeof(nmo_saver_manager_t*),
                             0,
                             arena) != NMO_OK) {
        goto fail;
    }

    return registry;

fail:
    nmo_type_registry_destroy(registry);
    return NULL;
}

void nmo_type_registry_destroy(nmo_type_registry_t *registry) {
    if (!registry) return;

    if (registry->metadata.data && registry->metadata.count > 0) {
        for (size_t i = 0; i < registry->metadata.count; i++) {
            nmo_specialized_metadata_t *entry =
                *(nmo_specialized_metadata_t **)nmo_arena_array_get(&registry->metadata, i);
            if (entry) {
                nmo_type_registry_unregister_metadata(registry, entry->type_id);
            }
        }
    }

    if (registry->types.data && registry->types.count > 0) {
        for (size_t i = 0; i < registry->types.count; i++) {
            nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
            if (type) {
                free_type_storage(registry, type, (nmo_type_id_t)i);
                nmo_type_descriptor_t **slot_ptr =
                    (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
                *slot_ptr = NULL;
            }
        }
    }

    if (registry->guid_map) {
        nmo_hash_table_destroy(registry->guid_map);
    }
    if (registry->name_map) {
        nmo_hash_table_destroy(registry->name_map);
    }
    if (registry->class_id_map) {
        nmo_hash_table_destroy(registry->class_id_map);
    }
    if (registry->class_id_inherited_map) {
        nmo_hash_table_destroy(registry->class_id_inherited_map);
    }
    if (registry->type_to_plugin) {
        nmo_hash_table_destroy(registry->type_to_plugin);
    }
    if (registry->type_to_metadata) {
        nmo_hash_table_destroy(registry->type_to_metadata);
    }
    if (registry->manager_guid_map) {
        nmo_hash_table_destroy(registry->manager_guid_map);
    }
    if (registry->type_to_manager) {
        nmo_hash_table_destroy(registry->type_to_manager);
    }

    // Arena owns all memory, no need to free types/metadata/manager arrays
}

nmo_status_t nmo_type_registry_register(
    nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *descriptor) 
{
    if (!registry || !descriptor) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL registry or descriptor");
    }

    nmo_status_t mutable_res = ensure_registry_mutable(registry, "register type");
    if (mutable_res != NMO_OK) {
        return mutable_res;
    }

    nmo_status_t validate_res = validate_type_descriptor(registry, descriptor);
    if (validate_res != NMO_OK) {
        return validate_res;
    }

    // Check for GUID collision
    nmo_type_id_t existing_id;
    if (nmo_hash_table_get(registry->guid_map, &descriptor->guid, &existing_id) == NMO_OK) {
        char guid_str[32] = {0};
        (void)nmo_guid_format(descriptor->guid, guid_str, sizeof(guid_str));

        const nmo_type_descriptor_t *existing_type = nmo_type_registry_get_by_id(registry, existing_id);
        const char *existing_name = (existing_type && existing_type->name) ? existing_type->name : "<unnamed>";
        const char *new_name = descriptor->name ? descriptor->name : "<unnamed>";

        NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                         "Type GUID already registered: %s (new='%s', existing='%s', existing_id=%d)",
                         guid_str,
                         new_name,
                         existing_name,
                         (int)existing_id);
    }

    // Find slot (reuse freed slots before expanding)
    size_t slot = 0;
    bool reused_slot = pop_free_slot(registry, &slot);
    if (!reused_slot) {
        slot = registry->types.count;
    }

    // Allocate and copy descriptor
    nmo_type_descriptor_t *type = (nmo_type_descriptor_t *)nmo_alloc(
        &registry->type_allocator,
        sizeof(nmo_type_descriptor_t),
        _Alignof(nmo_type_descriptor_t));
    if (!type) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate type descriptor");
    }
    
    memcpy(type, descriptor, sizeof(nmo_type_descriptor_t));

    const nmo_type_field_t *source_fields = type->fields;
    size_t source_field_count = type->field_count;
    type->fields = NULL;
    type->field_count = 0;
    type->base_type_id = NMO_TYPE_ID_INVALID;
    type->ext = NULL;

    if (!type->ext) {
        type->ext = (nmo_type_descriptor_ext_t *)nmo_alloc(
            &registry->type_allocator,
            sizeof(nmo_type_descriptor_ext_t),
            _Alignof(nmo_type_descriptor_ext_t));
        if (!type->ext) {
            free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate type extension");
        }
        memset(type->ext, 0, sizeof(*type->ext));
    }

    // Registry owns specialized metadata index; default to invalid until set.
    type->specialized_index = NMO_SPECIALIZED_INDEX_INVALID;

    // Deep copy name/description to registry-owned arena.
    if (type->name) {
        char *name_copy = nmo_strdup(&registry->type_allocator, type->name);
        if (!name_copy) {
            free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
            return NMO_ERR_NOMEM;
        }
        type->name = name_copy;
    }
    if (type->description) {
        const bool is_incomplete_struct_state =
            (type->category == NMO_TYPE_CATEGORY_STRUCT) &&
            (type->valid == false) &&
            (source_fields == NULL) &&
            (source_field_count == 0);

        if (!is_incomplete_struct_state) {
            char *desc_copy = nmo_strdup(&registry->type_allocator, type->description);
            if (!desc_copy) {
                free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
                return NMO_ERR_NOMEM;
            }
            type->description = desc_copy;
        }
    }

    // Deep copy field descriptors and their strings/defaults.
    if (source_fields && source_field_count > 0) {
        nmo_type_field_t *fields_copy = (nmo_type_field_t *)nmo_alloc(
            &registry->type_allocator,
            sizeof(nmo_type_field_t) * source_field_count,
            _Alignof(nmo_type_field_t));
        if (!fields_copy) {
            free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
            return NMO_ERR_NOMEM;
        }
        memcpy(fields_copy, source_fields, sizeof(nmo_type_field_t) * source_field_count);
        for (size_t i = 0; i < source_field_count; i++) {
            if (fields_copy[i].name) {
                char *field_name = nmo_strdup(&registry->type_allocator, fields_copy[i].name);
                if (!field_name) {
                    free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
                    return NMO_ERR_NOMEM;
                }
                fields_copy[i].name = field_name;
            }
            if (fields_copy[i].description) {
                char *field_desc = nmo_strdup(&registry->type_allocator, fields_copy[i].description);
                if (!field_desc) {
                    free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
                    return NMO_ERR_NOMEM;
                }
                fields_copy[i].description = field_desc;
            }
            if (fields_copy[i].default_value && fields_copy[i].size > 0) {
                void *default_copy = nmo_alloc(
                    &registry->type_allocator,
                    fields_copy[i].size,
                    _Alignof(max_align_t));
                if (!default_copy) {
                    free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
                    return NMO_ERR_NOMEM;
                }
                memcpy(default_copy, fields_copy[i].default_value, fields_copy[i].size);
                fields_copy[i].default_value = default_copy;
            }
        }
        type->fields = fields_copy;
        type->field_count = source_field_count;
    }

    nmo_type_assign_default_vtable(type, registry);
    
    // Assign ID and store descriptor
    nmo_type_id_t type_id = (nmo_type_id_t)slot;
    type->id = type_id;
    type->valid = true;
    type->saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    if (slot < registry->types.count) {
        // Reuse slot
        nmo_type_descriptor_t **slot_ptr = (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
        *slot_ptr = type;
        nmo_type_alias_list_t *alias_list = get_alias_list(registry, (nmo_type_id_t)slot);
        if (alias_list) {
            nmo_arena_array_clear(&alias_list->arr);
            alias_list->alias_string_ownership = NMO_OWNERSHIP_HEAP;
        }
        nmo_type_child_list_t *child_list = get_child_list(registry, (nmo_type_id_t)slot);
        if (child_list) {
            nmo_arena_array_clear(&child_list->arr);
        }
    } else {
        // Append new slot
        nmo_status_t res = nmo_arena_array_append(&registry->types, &type);
        if (res != NMO_OK) {
            free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
            return res;
        }
        nmo_type_alias_list_t alias_list = {0};
        alias_list.alias_string_ownership = NMO_OWNERSHIP_HEAP;
        res = nmo_arena_array_append(&registry->alias_lists, &alias_list);
        if (res != NMO_OK) {
            nmo_arena_array_pop(&registry->types, NULL);
            free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
            return res;
        }
        nmo_type_child_list_t child_list = {0};
        res = nmo_arena_array_append(&registry->child_lists, &child_list);
        if (res != NMO_OK) {
            nmo_arena_array_pop(&registry->alias_lists, NULL);
            nmo_arena_array_pop(&registry->types, NULL);
            free_type_storage(registry, type, NMO_TYPE_ID_INVALID);
            return res;
        }
    }

    // Insert into hash tables
    nmo_status_t result = nmo_hash_table_insert(registry->guid_map, &type->guid, &type_id);
    if (result != NMO_OK) {
        nmo_type_descriptor_t **slot_ptr = (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
        *slot_ptr = NULL;
        push_free_slot(registry, type_id);
        free_type_storage(registry, type, type_id);
        return result;
    }

    if (type->name) {
        result = nmo_hash_table_insert(registry->name_map, &type->name, &type_id);
        if (result != NMO_OK) {
            nmo_hash_table_remove(registry->guid_map, &type->guid);
            nmo_type_descriptor_t **slot_ptr =
                (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
            *slot_ptr = NULL;
            push_free_slot(registry, type_id);
            free_type_storage(registry, type, type_id);
            return result;
        }
    }

    // Insert into class_id map if this is a Virtools object type
    if (type->class_id != 0) {
        result = nmo_hash_table_insert(registry->class_id_map, &type->class_id, &type_id);
        if (result != NMO_OK) {
            if (type->name) {
                nmo_hash_table_remove(registry->name_map, &type->name);
            }
            nmo_hash_table_remove(registry->guid_map, &type->guid);
            nmo_type_descriptor_t **slot_ptr =
                (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
            *slot_ptr = NULL;
            push_free_slot(registry, type_id);
            free_type_storage(registry, type, type_id);
            return result;
        }
    }

    // Track plugin ownership if provided
    if (!nmo_guid_is_null(type->creator_plugin_guid)) {
        /* Store plugin GUID in map */
        result = nmo_hash_table_insert(registry->type_to_plugin,
                                       &type_id,
                                       &type->creator_plugin_guid);
        if (result != NMO_OK) {
            if (type->class_id != 0) {
                nmo_hash_table_remove(registry->class_id_map, &type->class_id);
            }
            if (type->name) {
                nmo_hash_table_remove(registry->name_map, &type->name);
            }
            nmo_hash_table_remove(registry->guid_map, &type->guid);
            nmo_type_descriptor_t **slot_ptr =
                (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, slot);
            *slot_ptr = NULL;
            push_free_slot(registry, type_id);
            free_type_storage(registry, type, type_id);
            return result;
        }
        registry->plugin_count++;
    } else {
        registry->builtin_count++;
    }

    // Invalidate derivation masks (lazy update)
    registry->derivation_masks_valid = false;
    registry->registry_version++;
    registry->class_id_inherited_version = 0;

    if (!nmo_guid_is_null(type->base_type)) {
        nmo_status_t child_res = ensure_parent_child_link(registry, type);
        if (child_res != NMO_OK) {
            nmo_type_registry_unregister(registry, type->guid);
            return child_res;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_type_registry_unregister(
    nmo_type_registry_t *registry,
    nmo_guid_t guid) 
{
    if (!registry) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t mutable_res = ensure_registry_mutable(registry, "unregister type");
    if (mutable_res != NMO_OK) {
        return mutable_res;
    }

    // Find type by GUID
    nmo_type_id_t type_id;
    if (nmo_hash_table_get(registry->guid_map, &guid, &type_id) != NMO_OK) {
        return NMO_ERR_NOT_FOUND;
    }

    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
    if (!type) {
        return NMO_ERR_NOT_FOUND;
    }

    nmo_type_id_t parent_id = get_parent_type_id(registry, type);

    // Remove from hash tables
    nmo_hash_table_remove(registry->guid_map, &guid);
    if (type->name) {
        nmo_hash_table_remove(registry->name_map, &type->name);
    }
    {
        nmo_type_alias_list_t *alias_list = get_alias_list(registry, type_id);
        if (alias_list && alias_list->arr.count > 0) {
            for (size_t i = 0; i < alias_list->arr.count; i++) {
                const char *alias_key = *(const char **)nmo_arena_array_get(&alias_list->arr, i);
                if (alias_key) {
                    nmo_hash_table_remove(registry->name_map, &alias_key);
                }
            }
        }
    }
    if (type->class_id != 0 && registry->class_id_map) {
        nmo_hash_table_remove(registry->class_id_map, &type->class_id);
    }
    nmo_hash_table_remove(registry->type_to_plugin, &type_id);
    if (registry->type_to_metadata) {
        nmo_hash_table_remove(registry->type_to_metadata, &type_id);
    }
    if (registry->type_to_manager) {
        nmo_hash_table_remove(registry->type_to_manager, &type_id);
    }

    if (parent_id != NMO_TYPE_ID_INVALID) {
        child_list_remove(registry, parent_id, type_id);
    }

    /* Update stats */
    if (type->valid) {
        if (!nmo_guid_is_null(type->creator_plugin_guid)) {
            if (registry->plugin_count > 0) {
                registry->plugin_count--;
            }
        } else {
            if (registry->builtin_count > 0) {
                registry->builtin_count--;
            }
        }
    }

    nmo_type_registry_unregister_metadata(registry, type_id);

    /* Soft delete: mark invalid, keep slot for recycling */
    type->valid = false;
    type->specialized_index = NMO_SPECIALIZED_INDEX_INVALID;
    type->saver_manager = NMO_MANAGER_INDEX_INVALID;
    nmo_type_descriptor_t **slot_ptr = (nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_id);
    *slot_ptr = NULL;

    push_free_slot(registry, type_id);

    free_type_storage(registry, type, type_id);

    /* Invalidate derivation masks */
    registry->derivation_masks_valid = false;
    registry->registry_version++;
    registry->class_id_inherited_version = 0;

    return NMO_OK;
}

/* Note: nmo_type_registry_unregister_plugin_types() is implemented in plugin_support.c
 * with full cascade deletion support (Phase 5.6)
 */

const nmo_type_descriptor_t* nmo_type_registry_find_by_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) 
{
    if (!registry) return NULL;

    nmo_type_id_t type_id;
    if (nmo_hash_table_get(registry->guid_map, &guid, &type_id) != NMO_OK) {
        return NULL;
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    return (type && type->valid) ? type : NULL;
}

const nmo_type_descriptor_t* nmo_type_registry_find_by_name(
    const nmo_type_registry_t *registry,
    const char *name) 
{
    if (!registry || !name) return NULL;

    nmo_type_id_t type_id;
    if (nmo_hash_table_get(registry->name_map, &name, &type_id) == NMO_OK) {
        if (type_id < 0 || (size_t)type_id >= registry->types.count) return NULL;
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(
            (nmo_arena_array_t*)&registry->types, type_id);
        return (type && type->valid) ? type : NULL;
    }

    return NULL;
}

nmo_status_t nmo_type_registry_add_name_alias(
    nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    const char *alias)
{
    if (!registry || !alias || !registry->name_map) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    nmo_status_t mutable_res = ensure_registry_mutable(registry, "add name alias");
    if (mutable_res != NMO_OK) {
        return mutable_res;
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID");
    }

    if (alias[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Empty alias");
    }

    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, (size_t)type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid");
    }

    /* If alias is already mapped to a different valid type, reject. */
    nmo_type_id_t existing_id = NMO_TYPE_ID_INVALID;
    const char *alias_key = alias;
    if (nmo_hash_table_get(registry->name_map, &alias_key, &existing_id) == NMO_OK) {
        const nmo_type_descriptor_t *existing_type = nmo_type_registry_get_by_id(registry, existing_id);
        if (existing_type && existing_type->valid) {
            if (existing_id == type_id) {
                NMO_RETURN_OK();
            }
            NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                             "Alias already mapped to another type");
        }
        nmo_hash_table_remove(registry->name_map, &alias_key);
    }

    char *alias_copy = nmo_strdup(&registry->type_allocator, alias);
    if (!alias_copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Out of memory");
    }

    /* name_map key is (const char*), so we store pointer to the copied string. */
    nmo_status_t res = nmo_hash_table_insert(registry->name_map, &alias_copy, &type_id);
    if (res != NMO_OK) {
        nmo_free(&registry->type_allocator, alias_copy);
        return res;
    }

    nmo_type_alias_list_t *alias_list = get_alias_list(registry, type_id);
    if (!alias_list) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Alias list storage missing for type");
    }

    res = alias_list_append(registry, alias_list, alias_copy);
    if (res != NMO_OK) {
        const char *alias_key = alias_copy;
        nmo_hash_table_remove(registry->name_map, &alias_key);
        nmo_free(&registry->type_allocator, alias_copy);
        return res;
    }

    return res;
}

const nmo_type_descriptor_t* nmo_type_registry_find_by_class_id(
    const nmo_type_registry_t *registry,
    uint32_t class_id) 
{
    if (!registry || class_id == 0) return NULL;

    nmo_type_id_t type_id;
    if (nmo_hash_table_get(registry->class_id_map, &class_id, &type_id) != NMO_OK) {
        return NULL;
    }

    if (type_id < 0 || (size_t)type_id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    return (type && type->valid) ? type : NULL;
}

const nmo_type_descriptor_t* nmo_type_registry_find_by_class_id_inherited(
    const nmo_type_registry_t *registry,
    uint32_t class_id)
{
    if (!registry || class_id == 0) return NULL;

    nmo_type_registry_t *mutable_registry = (nmo_type_registry_t *)registry;

    ensure_class_id_inherited_cache(mutable_registry);

    nmo_type_id_t cached_id = NMO_TYPE_ID_INVALID;
    if (mutable_registry->class_id_inherited_map &&
        nmo_hash_table_get(mutable_registry->class_id_inherited_map, &class_id, &cached_id) == NMO_OK) {
        return nmo_type_registry_get_by_id(mutable_registry, cached_id);
    }

    // First try direct lookup
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(registry, class_id);
    if (type) {
        nmo_type_id_t type_id = type->id;
        if (mutable_registry->class_id_inherited_map) {
            nmo_hash_table_insert(mutable_registry->class_id_inherited_map, &class_id, &type_id);
        }
        return type;
    }

    // Detect cycles using tortoise/hare on class hierarchy
    uint32_t slow = class_id;
    uint32_t fast = class_id;
    while (true) {
        slow = class_parent_id_from_registry(mutable_registry, slow);
        if (slow == 0) {
            break;
        }

        fast = class_parent_id_from_registry(mutable_registry, fast);
        if (fast == 0) {
            break;
        }
        fast = class_parent_id_from_registry(mutable_registry, fast);
        if (fast == 0) {
            break;
        }

        if (fast == slow) {
            return NULL;
        }
    }

    // Walk up class hierarchy to find parent with schema
    uint32_t current_class_id = class_id;
    while (true) {
        uint32_t parent_id = class_parent_id_from_registry(mutable_registry, current_class_id);
        if (parent_id == 0) {
            break;
        }

        type = nmo_type_registry_find_by_class_id(registry, parent_id);
        if (type) {
            nmo_type_id_t type_id = type->id;
            if (mutable_registry->class_id_inherited_map) {
                nmo_hash_table_insert(mutable_registry->class_id_inherited_map, &class_id, &type_id);
            }
            return type;
        }

        current_class_id = parent_id;
    }

    return NULL;  // No schema found in hierarchy
}

bool nmo_type_registry_is_class_derived_from(
    const nmo_type_registry_t *registry,
    uint32_t class_id,
    uint32_t base_class_id)
{
    if (!registry || class_id == 0 || base_class_id == 0) {
        return false;
    }

    const nmo_type_descriptor_t *child =
        nmo_type_registry_find_by_class_id_inherited(registry, class_id);
    const nmo_type_descriptor_t *base =
        nmo_type_registry_find_by_class_id_inherited(registry, base_class_id);
    if (!child || !base) {
        return false;
    }

    return nmo_type_is_derived_from((nmo_type_registry_t *)registry, child->id, base->id);
}

uint32_t nmo_type_registry_get_class_parent(
    const nmo_type_registry_t *registry,
    uint32_t class_id)
{
    return class_parent_id_from_registry(registry, class_id);
}

int nmo_type_registry_get_class_ancestors(
    const nmo_type_registry_t *registry,
    uint32_t class_id,
    uint32_t *out_ancestors,
    int max_count)
{
    if (!registry || !out_ancestors || max_count <= 0 || class_id == 0) {
        return 0;
    }

    int count = 0;
    uint32_t current = class_parent_id_from_registry(registry, class_id);
    while (current != 0 && count < max_count) {
        out_ancestors[count++] = current;
        current = class_parent_id_from_registry(registry, current);
    }

    return count;
}

uint32_t nmo_type_registry_get_common_class_ancestor(
    const nmo_type_registry_t *registry,
    uint32_t class_id1,
    uint32_t class_id2)
{
    if (!registry || class_id1 == 0 || class_id2 == 0) {
        return 0;
    }

    uint32_t probe = class_id1;
    while (probe != 0) {
        if (nmo_type_registry_is_class_derived_from(registry, class_id2, probe)) {
            return probe;
        }
        probe = class_parent_id_from_registry(registry, probe);
    }

    return 0;
}

int32_t nmo_type_registry_get_class_derivation_level(
    const nmo_type_registry_t *registry,
    uint32_t class_id)
{
    if (!registry || class_id == 0) {
        return -1;
    }

    uint32_t slow = class_id;
    uint32_t fast = class_id;
    while (true) {
        slow = class_parent_id_from_registry(registry, slow);
        if (slow == 0) {
            break;
        }

        fast = class_parent_id_from_registry(registry, fast);
        if (fast == 0) {
            break;
        }
        fast = class_parent_id_from_registry(registry, fast);
        if (fast == 0) {
            break;
        }

        if (fast == slow) {
            return -1;
        }
    }

    int32_t level = 0;
    uint32_t current = class_id;
    while (current != 0) {
        uint32_t parent = class_parent_id_from_registry(registry, current);
        if (parent == 0) {
            return level;
        }
        level++;
        current = parent;
    }

    return -1;
}

const nmo_type_descriptor_t* nmo_type_registry_get_by_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t id) 
{
    if (!registry || id < 0 || (size_t)id >= registry->types.count) return NULL;
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, id);
    return (type && type->valid) ? type : NULL;
}

static bool compute_compat_mask_recursive(
    nmo_type_registry_t *registry,
    size_t type_index,
    uint8_t *state)
{
    if (!registry || !state) {
        return false;
    }

    if (state[type_index] == 2u) {
        return true;
    }
    if (state[type_index] == 1u) {
        return false;
    }

    state[type_index] = 1u;
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, type_index);
    if (!type || !type->valid) {
        state[type_index] = 2u;
        return true;
    }

    if (!nmo_guid_is_null(type->base_type)) {
        if (ensure_parent_child_link(registry, type) != NMO_OK) {
            return false;
        }
        nmo_type_id_t parent_id = type->base_type_id;

        if (parent_id >= 0 && (size_t)parent_id < registry->types.count) {
            if (!compute_compat_mask_recursive(registry, (size_t)parent_id, state)) {
                return false;
            }

            const nmo_type_descriptor_t *parent =
                *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, parent_id);
            if (parent && parent->valid && parent->ext && type->ext) {
                const nmo_bit_array_t *src = &parent->ext->compat_mask.bits;
                nmo_bit_array_t *dst = &type->ext->compat_mask.bits;
                const size_t words = dst->word_capacity;

                for (size_t word = 0; word < words; word++) {
                    uint32_t parent_bits = (src->words && word < src->word_capacity) ? src->words[word] : 0;
                    dst->words[word] |= parent_bits;
                }
            }
        }
    }

    state[type_index] = 2u;
    return true;
}

void nmo_type_registry_update_derivation_masks(nmo_type_registry_t *registry) {
    if (!registry || registry->derivation_masks_valid) return;

    if (registry->types.count == 0) {
        registry->derivation_masks_valid = true;
        return;
    }

    // Ensure all valid types have an initialized mask with enough capacity.
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
        if (type && type->valid) {
            if (ensure_compat_mask_capacity(registry, type) != NMO_OK) {
                return;
            }
            if (!nmo_guid_is_null(type->base_type)) {
                if (ensure_parent_child_link(registry, type) != NMO_OK) {
                    return;
                }
            }
        }
    }

    // Reset all masks
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
        if (type && type->valid) {
            nmo_compat_mask_clear(&type->ext->compat_mask);
            nmo_compat_mask_set(&type->ext->compat_mask, (nmo_type_id_t)i); // Self-compatible
        }
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    uint8_t *state = (uint8_t *)nmo_alloc(&alloc, registry->types.count, _Alignof(uint8_t));
    if (!state) {
        return;
    }
    memset(state, 0, registry->types.count);

    for (size_t i = 0; i < registry->types.count; i++) {
        if (state[i] == 0u) {
            if (!compute_compat_mask_recursive(registry, i, state)) {
                nmo_free(&alloc, state);
                return;
            }
        }
    }

    nmo_free(&alloc, state);
    registry->derivation_masks_valid = true;
}

nmo_status_t nmo_type_registry_begin_update(nmo_type_registry_t *registry) {
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry pointer");
    }

    if (!registry->finalized) {
        NMO_RETURN_OK();
    }

    registry->finalized = false;
    registry->derivation_masks_valid = false;
    registry->class_id_inherited_version = 0u;

    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_finalize(nmo_type_registry_t *registry) {
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid registry pointer");
    }

    nmo_type_registry_update_derivation_masks(registry);
    ensure_class_id_inherited_cache(registry);
    registry->finalized = true;

    NMO_RETURN_OK();
}

/* ============================================================================
 * State Hierarchy Layout Computation
 * 
 * Computes inheritance hierarchy and state offsets for each type.
 * Called lazily after type registration when state layout is needed.
 * ============================================================================ */

/**
 * @brief Count inheritance depth (number of ancestors including self)
 */
static uint16_t count_hierarchy_depth(
    nmo_type_registry_t *registry,
    nmo_type_descriptor_t *type)
{
    uint16_t depth = 0;
    nmo_type_descriptor_t *current = type;
    while (current && current->valid) {
        depth++;
        if (nmo_guid_is_null(current->base_type)) {
            break;
        }
        nmo_type_id_t parent_id = current->base_type_id;
        if (parent_id == NMO_TYPE_ID_INVALID) {
            if (nmo_hash_table_get(registry->guid_map, &current->base_type, &parent_id) == NMO_OK) {
                current->base_type_id = parent_id;
            } else {
                break;
            }
        }
        current = (nmo_type_descriptor_t *)nmo_type_registry_get_by_id(registry, parent_id);
    }
    return depth;
}

/**
 * @brief Compute state layout for a single type
 * 
 * Builds hierarchy array (root first, self last) and computes state offsets.
 * Only computed for types with vtable (i.e., object types with state).
 */
static nmo_status_t compute_type_state_layout(
    nmo_type_registry_t *registry,
    nmo_type_descriptor_t *type)
{
    if (!type || !type->valid) {
        NMO_RETURN_OK();
    }
    
    /* Skip if already computed */
    if (type->ext && type->ext->hierarchy != NULL) {
        NMO_RETURN_OK();
    }
    
    /* Skip types without vtable (non-object types) */
    if (type->vtable == NULL) {
        if (type->ext) {
            type->ext->hierarchy_depth = 0;
            type->ext->total_state_size = 0;
        }
        NMO_RETURN_OK();
    }
    
    /* Count hierarchy depth */
    uint16_t depth = count_hierarchy_depth(registry, type);
    if (depth == 0) {
        NMO_RETURN_OK();
    }
    
    /* Allocate hierarchy array and offsets */
    const nmo_type_descriptor_t **hierarchy = (const nmo_type_descriptor_t **)nmo_alloc(
        &registry->type_allocator,
        sizeof(nmo_type_descriptor_t *) * depth,
        _Alignof(nmo_type_descriptor_t *));
    if (!hierarchy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
            "Failed to allocate type hierarchy array");
    }
    
    uint32_t *offsets = (uint32_t *)nmo_alloc(
        &registry->type_allocator,
        sizeof(uint32_t) * depth,
        _Alignof(uint32_t));
    if (!offsets) {
        nmo_free(&registry->type_allocator, (void *)hierarchy);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
            "Failed to allocate state offsets array");
    }
    
    /* Build hierarchy array (root first, self last) by traversing to root */
    size_t idx = depth;
    nmo_type_descriptor_t *current = type;
    while (current && current->valid && idx > 0) {
        idx--;
        hierarchy[idx] = current;
        if (nmo_guid_is_null(current->base_type)) {
            break;
        }
        nmo_type_id_t parent_id = current->base_type_id;
        if (parent_id == NMO_TYPE_ID_INVALID) {
            if (nmo_hash_table_get(registry->guid_map, &current->base_type, &parent_id) == NMO_OK) {
                current->base_type_id = parent_id;
            } else {
                break;
            }
        }
        current = (nmo_type_descriptor_t *)nmo_type_registry_get_by_id(registry, parent_id);
    }
    
    /* Compute state offsets for nested 'base' pattern
     * 
     * In C, when using nested structures like:
     *   struct Derived { struct Base base; int derived_field; };
     * The 'base' member is always at offset 0.
     * 
     * So for our inheritance chain, ALL ancestor states start at offset 0.
     * This allows safe casting between derived and base state pointers.
     */
    for (uint16_t i = 0; i < depth; i++) {
        offsets[i] = 0;  /* All ancestors at offset 0 due to nested base pattern */
    }
    
    /* Total size is the size of the most derived type (self)
     * Since we use nested base pattern, the derived type already includes all base sizes */
    uint32_t total_size = type->size;
    
    /* Store in type descriptor */
    if (!type->ext) {
        type->ext = (nmo_type_descriptor_ext_t *)nmo_alloc(
            &registry->type_allocator,
            sizeof(nmo_type_descriptor_ext_t),
            _Alignof(nmo_type_descriptor_ext_t));
        if (!type->ext) {
            nmo_free(&registry->type_allocator, (void *)hierarchy);
            nmo_free(&registry->type_allocator, offsets);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate type extension");
        }
        memset(type->ext, 0, sizeof(*type->ext));
    }

    type->ext->hierarchy = hierarchy;
    type->ext->state_offsets = offsets;
    type->ext->hierarchy_depth = depth;
    type->ext->total_state_size = total_size;
    
    NMO_RETURN_OK();
}

void nmo_type_registry_compute_state_layouts(nmo_type_registry_t *registry) {
    if (!registry) return;
    
    /* Ensure derivation masks are valid first (establishes base_type links) */
    nmo_type_registry_finalize(registry);
    
    /* Compute state layout for each type */
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(&registry->types, i);
        if (type && type->valid) {
            nmo_status_t status = compute_type_state_layout(registry, type);
            if (status != NMO_OK) {
                /* Log error but continue with other types */
            }
        }
    }
}

uint32_t nmo_type_get_state_offset(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *derived_type,
    const nmo_type_descriptor_t *ancestor_type)
{
    (void)registry; /* Currently unused, but may be needed for lookup */
    
    if (!derived_type || !ancestor_type || !derived_type->ext || !derived_type->ext->hierarchy) {
        return (uint32_t)-1;
    }
    
    /* Search hierarchy for the ancestor type */
    for (uint16_t i = 0; i < derived_type->ext->hierarchy_depth; i++) {
        if (derived_type->ext->hierarchy[i] == ancestor_type ||
            nmo_guid_equals(derived_type->ext->hierarchy[i]->guid, ancestor_type->guid)) {
            return derived_type->ext->state_offsets[i];
        }
    }
    
    return (uint32_t)-1;
}

void nmo_type_registry_get_stats(
    const nmo_type_registry_t *registry,
    size_t *total_types,
    size_t *builtin_types,
    size_t *plugin_types) {
    
    if (!registry) {
        if (total_types) *total_types = 0;
        if (builtin_types) *builtin_types = 0;
        if (plugin_types) *plugin_types = 0;
        return;
    }
    
    // Count valid types
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type != NULL && type->valid) {
            count++;
        }
    }
    
    if (total_types) *total_types = count;
    if (builtin_types) *builtin_types = registry->builtin_count;
    if (plugin_types) *plugin_types = registry->plugin_count;
}

/* ============================================================================
 * Field Annotation API Implementation
 * ============================================================================ */

const char* nmo_field_semantic_name(nmo_field_semantic_t semantic) {
    switch (semantic) {
        case NMO_SEMANTIC_NONE:       return "none";
        case NMO_SEMANTIC_POSITION:   return "position";
        case NMO_SEMANTIC_ROTATION:   return "rotation";
        case NMO_SEMANTIC_SCALE:      return "scale";
        case NMO_SEMANTIC_DIRECTION:  return "direction";
        case NMO_SEMANTIC_NORMAL:     return "normal";
        case NMO_SEMANTIC_COLOR:      return "color";
        case NMO_SEMANTIC_ALPHA:      return "alpha";
        case NMO_SEMANTIC_UV:         return "uv";
        case NMO_SEMANTIC_ID:         return "id";
        case NMO_SEMANTIC_OBJECT_REF: return "object_ref";
        case NMO_SEMANTIC_MANAGER_REF:return "manager_ref";
        case NMO_SEMANTIC_TIME:       return "time";
        case NMO_SEMANTIC_DURATION:   return "duration";
        case NMO_SEMANTIC_NAME:       return "name";
        case NMO_SEMANTIC_PATH:       return "path";
        case NMO_SEMANTIC_USER_DATA:  return "user_data";
        default:                      return "unknown";
    }
}

const char* nmo_field_units_name(nmo_field_units_t units) {
    switch (units) {
        case NMO_UNITS_NONE:          return "none";
        case NMO_UNITS_DEGREES:       return "degrees";
        case NMO_UNITS_RADIANS:       return "radians";
        case NMO_UNITS_METERS:        return "meters";
        case NMO_UNITS_CENTIMETERS:   return "centimeters";
        case NMO_UNITS_UNITS:         return "units";
        case NMO_UNITS_SECONDS:       return "seconds";
        case NMO_UNITS_MILLISECONDS:  return "milliseconds";
        case NMO_UNITS_FRAMES:        return "frames";
        default:                      return "unknown";
    }
}

/* ============================================================================
 * Phase 6.3: Type Compatibility & Conversion API Implementation
 * 
 * Reference: SCHEMA_V2_IMPLEMENTATION_PLAN.md Phase 6.3
 * ============================================================================ */

/* --- 6.3.1: Inheritance Checking API --- */

bool nmo_type_is_derived_from(
    nmo_type_registry_t *registry,
    nmo_type_id_t child_id,
    nmo_type_id_t parent_id) {
    
    if (!registry) return false;
    
    // Invalid IDs
    if (child_id < 0 || (size_t)child_id >= registry->types.count ||
        parent_id < 0 || (size_t)parent_id >= registry->types.count) {
        return false;
    }
    
    // NULL or invalid types
    const nmo_type_descriptor_t *child = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, child_id);
    const nmo_type_descriptor_t *parent = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, parent_id);
    if (!child || !child->valid || !parent || !parent->valid) {
        return false;
    }
    
    // Same type (trivial case)
    if (child_id == parent_id) {
        return true;
    }
    
    // Lazy update of derivation masks if needed
    if (!registry->derivation_masks_valid) {
        nmo_type_registry_update_derivation_masks(registry);
    }

    if (!registry->derivation_masks_valid) {
        return false;
    }
    
    if (!child->ext) {
        return false;
    }
    bool result = nmo_compat_mask_is_set(&child->ext->compat_mask, parent_id);
    return result;
}

nmo_status_t nmo_type_get_inheritance_chain(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_type_id_t **out_chain,
    size_t *out_count,
    nmo_arena_t *arena) {
    
    if (!registry || !out_chain || !out_count || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter");
    }
    
    // Validate type ID
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID");
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid or unregistered type");
    }

    if (has_type_inheritance_cycle(registry, type_id)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Circular inheritance detected");
    }
    
    // Count chain length (walk up base_type chain)
    size_t chain_length = 0;
    nmo_type_id_t current_id = type_id;
    
    while (current_id != NMO_TYPE_ID_INVALID) {
        chain_length++;
        const nmo_type_descriptor_t *current = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, current_id);

        current_id = get_parent_type_id(registry, current);
    }
    
    // Allocate output array
    nmo_type_id_t *chain = nmo_arena_alloc(arena, 
        chain_length * sizeof(nmo_type_id_t), 
        _Alignof(nmo_type_id_t));
    if (!chain) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate chain array");
    }
    
    // Fill array (most-derived to least-derived)
    size_t index = 0;
    current_id = type_id;
    
    while (current_id != NMO_TYPE_ID_INVALID && index < chain_length) {
        chain[index++] = current_id;
        const nmo_type_descriptor_t *current = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, current_id);
        
        current_id = get_parent_type_id(registry, current);
    }
    
    *out_chain = chain;
    *out_count = index;
    NMO_RETURN_OK();
}

bool nmo_type_is_compatible(
    nmo_type_registry_t *registry,
    nmo_type_id_t type1,
    nmo_type_id_t type2) {
    
    // Check if type1 is derived from type2 OR type2 is derived from type1 (symmetric)
    return nmo_type_is_derived_from(registry, type1, type2) ||
           nmo_type_is_derived_from(registry, type2, type1);
}

int32_t nmo_type_get_derivation_depth(
    nmo_type_registry_t *registry,
    nmo_type_id_t child_id,
    nmo_type_id_t parent_id) {
    
    if (!registry) return -1;
    
    // Not derived
    if (!nmo_type_is_derived_from(registry, child_id, parent_id)) {
        return -1;
    }
    
    // Same type (depth 0)
    if (child_id == parent_id) {
        return 0;
    }
    
    // Walk up chain counting steps
    int32_t depth = 0;
    nmo_type_id_t current_id = child_id;

    if (has_type_inheritance_cycle(registry, child_id)) {
        return -1;
    }
    
    while (current_id != NMO_TYPE_ID_INVALID) {
        const nmo_type_descriptor_t *current = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, current_id);

        nmo_type_id_t next_id = get_parent_type_id(registry, current);
        if (next_id == NMO_TYPE_ID_INVALID) {
            break; // Reached root without finding parent_id
        }

        depth++;
        current_id = next_id;
        
        // Found parent
        if (current_id == parent_id) {
            return depth;
        }
        
    }
    
    return -1; // Should not reach here if is_derived_from returned true
}

/* --- 6.3.3: Type Conversion API --- */

nmo_type_id_t nmo_type_registry_guid_to_type_id(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) {
    
    if (!registry || !registry->guid_map) {
        return NMO_TYPE_ID_INVALID;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->guid_map, &guid, &type_id) == NMO_OK) {
        if (type_id < 0 || (size_t)type_id >= registry->types.count) {
            return NMO_TYPE_ID_INVALID;
        }
        const nmo_type_descriptor_t *type =
            *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
        if (!type || !type->valid) {
            return NMO_TYPE_ID_INVALID;
        }
        return type_id;
    }
    return NMO_TYPE_ID_INVALID;
}

nmo_status_t nmo_type_registry_type_id_to_guid(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_guid_t *out_guid) {
    
    if (!registry || !out_guid) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter");
    }
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID");
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid");
    }
    
    *out_guid = type->guid;
    NMO_RETURN_OK();
}

const char* nmo_type_registry_guid_to_name(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) {
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, guid);
    if (type_id == NMO_TYPE_ID_INVALID) {
        return NULL;
    }

    const nmo_type_descriptor_t *type = nmo_type_registry_get_by_id(registry, type_id);
    return type ? type->name : NULL;
}

nmo_status_t nmo_type_registry_name_to_guid(
    const nmo_type_registry_t *registry,
    const char *name,
    nmo_guid_t *out_guid) {
    
    if (!registry || !name || !out_guid) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter");
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->name_map, &name, &type_id) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type name not found");
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type invalid");
    }
    
    *out_guid = type->guid;
    NMO_RETURN_OK();
}

const char* nmo_type_registry_type_id_to_name(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id) {
    
    if (!registry) return NULL;
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        return NULL;
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return NULL;
    }
    
    return type->name;
}

nmo_type_id_t nmo_type_registry_name_to_type_id(
    const nmo_type_registry_t *registry,
    const char *name) {
    
    if (!registry || !name || !registry->name_map) {
        return NMO_TYPE_ID_INVALID;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->name_map, &name, &type_id) == NMO_OK) {
        if (type_id < 0 || (size_t)type_id >= registry->types.count) {
            return NMO_TYPE_ID_INVALID;
        }
        const nmo_type_descriptor_t *type =
            *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
        if (!type || !type->valid) {
            return NMO_TYPE_ID_INVALID;
        }
        return type_id;
    }
    return NMO_TYPE_ID_INVALID;
}

nmo_status_t nmo_type_registry_class_id_to_guid(
    const nmo_type_registry_t *registry,
    uint32_t class_id,
    nmo_guid_t *out_guid) {
    
    if (!registry || !out_guid) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter");
    }
    
    if (!registry->class_id_map) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "ClassID map not initialized");
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->class_id_map, &class_id, &type_id) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "ClassID not found");
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type invalid");
    }
    
    *out_guid = type->guid;
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_guid_to_class_id(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid,
    uint32_t *out_class_id) {
    
    if (!registry || !out_class_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter");
    }
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, guid);
    if (type_id == NMO_TYPE_ID_INVALID) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found");
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type->class_id == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type has no ClassID");
    }
    
    *out_class_id = type->class_id;
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_type_id_to_class_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    uint32_t *out_class_id) {
    
    if (!registry || !out_class_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL parameter");
    }
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID");
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid");
    }
    
    if (type->class_id == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type has no ClassID");
    }
    
    *out_class_id = type->class_id;
    NMO_RETURN_OK();
}

nmo_type_id_t nmo_type_registry_class_id_to_type_id(
    const nmo_type_registry_t *registry,
    uint32_t class_id) {
    
    if (!registry || !registry->class_id_map) {
        return NMO_TYPE_ID_INVALID;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->class_id_map, &class_id, &type_id) == NMO_OK) {
        if (type_id < 0 || (size_t)type_id >= registry->types.count) {
            return NMO_TYPE_ID_INVALID;
        }

        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get(
            (nmo_arena_array_t*)&registry->types, type_id);
        if (type == NULL || !type->valid) {
            return NMO_TYPE_ID_INVALID;
        }

        return type_id;
    }
    return NMO_TYPE_ID_INVALID;
}

/* ============================================================================
 * Phase 6.5: Type Statistics & Visibility Control Implementation
 * ============================================================================ */

size_t nmo_type_registry_get_type_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type != NULL && type->valid) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_builtin_count(const nmo_type_registry_t *registry) {
    return registry ? registry->builtin_count : 0;
}

size_t nmo_type_registry_get_plugin_type_count(const nmo_type_registry_t *registry) {
    return registry ? registry->plugin_count : 0;
}

size_t nmo_type_registry_get_flags_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid && (type->category & NMO_TYPE_CATEGORY_FLAGS)) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_enum_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid && (type->category & NMO_TYPE_CATEGORY_ENUM)) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_struct_count(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < registry->types.count; i++) {
        const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid && (type->category & NMO_TYPE_CATEGORY_STRUCT)) {
            count++;
        }
    }
    return count;
}

size_t nmo_type_registry_get_memory_usage(const nmo_type_registry_t *registry) {
    if (!registry) {
        return 0;
    }
    
    size_t total = 0;
    
    // Registry structure itself
    total += sizeof(nmo_type_registry_t);
    
    // Type descriptor array
    total += registry->types.capacity * sizeof(nmo_type_descriptor_t*);
    
    // Type descriptors (estimate based on valid types)
    for (size_t i = 0; i < registry->types.count; i++) {
        nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
        if (type && type->valid) {
            total += sizeof(nmo_type_descriptor_t);
            
            // Field descriptors
            if (type->fields) {
                total += type->field_count * sizeof(nmo_type_field_t);
            }
        }
    }
    
    // Metadata array
    total += registry->metadata.capacity * sizeof(nmo_specialized_metadata_t*);
    total += registry->metadata.count * sizeof(nmo_specialized_metadata_t);
    
    // Hash tables (estimate: 2x key-value pairs + overhead)
    // guid_map, name_map, class_id_map, type_to_metadata, type_to_plugin
    size_t hash_table_overhead = 5 * 256;  // Rough estimate per table
    total += hash_table_overhead;
    
    // Arena overhead (estimate: 10% of total)
    total += total / 10;
    
    return total;
}

bool nmo_type_registry_is_ui_visible(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid) {
    
    if (!registry) {
        return false;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->guid_map, &guid, &type_id) != NMO_OK) {
        return false;
    }
    
    return nmo_type_registry_is_ui_visible_by_id(registry, type_id);
}

bool nmo_type_registry_is_ui_visible_by_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id) {
    
    if (!registry || type_id < 0 || (size_t)type_id >= registry->types.count) {
        return false;
    }
    
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        return false;
    }
    
    // Type is hidden if NMO_TYPE_CATEGORY_HIDDEN flag is set
    return !(type->category & NMO_TYPE_CATEGORY_HIDDEN);
}

nmo_status_t nmo_type_registry_set_ui_visibility(
    nmo_type_registry_t *registry,
    nmo_guid_t guid,
    bool visible) {
    
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry");
    }

    nmo_status_t mutable_res = ensure_registry_mutable(registry, "set UI visibility");
    if (mutable_res != NMO_OK) {
        return mutable_res;
    }
    
    nmo_type_id_t type_id = NMO_TYPE_ID_INVALID;
    if (nmo_hash_table_get(registry->guid_map, &guid, &type_id) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found");
    }
    
    if (type_id < 0 || (size_t)type_id >= registry->types.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid type ID");
    }
    
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (!type || !type->valid) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found or invalid");
    }
    
    // Toggle NMO_TYPE_CATEGORY_HIDDEN flag
    if (visible) {
        type->category &= ~NMO_TYPE_CATEGORY_HIDDEN;  // Clear hidden flag
    } else {
        type->category |= NMO_TYPE_CATEGORY_HIDDEN;   // Set hidden flag
    }
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Phase 6.6: Custom Manager Registration Implementation
 * ============================================================================ */

nmo_status_t nmo_type_registry_register_saver_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t manager_guid,
    const char *name,
    nmo_manager_serialize_fn serialize,
    nmo_manager_deserialize_fn deserialize,
    void *manager_context) {
    
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry");
    }
    
    if (!serialize || !deserialize) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL serialize/deserialize callbacks");
    }
    
    // Check if manager already registered
    if (registry->manager_guid_map) {
        nmo_manager_index_t existing_index = NMO_MANAGER_INDEX_INVALID;
        if (nmo_hash_table_get(registry->manager_guid_map, &manager_guid, &existing_index) == NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR, "Manager already registered");
        }
    }
    
    // Lazy initialize hash tables
    if (!registry->manager_guid_map) {
        registry->manager_guid_map = nmo_hash_table_create(
            NULL, sizeof(nmo_guid_t), sizeof(nmo_manager_index_t),
            16, guid_hash_func, guid_compare_func);
        
        registry->type_to_manager = nmo_hash_table_create(
            NULL, sizeof(nmo_type_id_t), sizeof(nmo_manager_index_t),
            16, NULL, NULL);
        
        if (!registry->manager_guid_map || !registry->type_to_manager) {
            if (registry->manager_guid_map) {
                nmo_hash_table_destroy(registry->manager_guid_map);
                registry->manager_guid_map = NULL;
            }
            if (registry->type_to_manager) {
                nmo_hash_table_destroy(registry->type_to_manager);
                registry->type_to_manager = NULL;
            }
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to create manager hash tables");
        }
    }
    
    // Allocate manager descriptor
    nmo_saver_manager_t *manager = (nmo_saver_manager_t *)nmo_arena_alloc(
        registry->arena, sizeof(nmo_saver_manager_t), _Alignof(nmo_saver_manager_t));
    
    if (!manager) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate manager descriptor");
    }
    
    // Initialize manager
    manager->guid = manager_guid;
    if (name) {
        manager->name = nmo_arena_strdup(registry->arena, name);
        if (!manager->name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy manager name");
        }
    } else {
        manager->name = NULL;
    }
    manager->serialize = serialize;
    manager->deserialize = deserialize;
    manager->context = manager_context;
    
    // Add to array and hash table
    nmo_manager_index_t manager_index = (nmo_manager_index_t)registry->saver_managers.count;
    nmo_status_t res = nmo_arena_array_append(&registry->saver_managers, &manager);
    if (res != NMO_OK) return res;
    
    nmo_status_t map_result = nmo_hash_table_insert(registry->manager_guid_map, &manager_guid, &manager_index);
    if (map_result != NMO_OK) {
        nmo_arena_array_pop(&registry->saver_managers, NULL);
        return map_result;
    }
    
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_registry_unregister_saver_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t manager_guid) {
    
    if (!registry || !registry->manager_guid_map) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry or no managers");
    }

    nmo_status_t mutable_res = ensure_registry_mutable(registry, "unregister saver manager");
    if (mutable_res != NMO_OK) {
        return mutable_res;
    }
    
    // Find manager
    nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
    if (nmo_hash_table_get(registry->manager_guid_map, &manager_guid, &manager_index) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Manager not found");
    }
    
    if (manager_index < 0 || (size_t)manager_index >= registry->saver_managers.count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid manager index");
    }
    
    // Clear all type associations with this manager
    if (registry->type_to_manager) {
        // Find all types using this manager and clear their associations
        for (size_t i = 0; i < registry->types.count; i++) {
            nmo_manager_index_t type_manager_idx = NMO_MANAGER_INDEX_INVALID;
            nmo_type_id_t tid = (nmo_type_id_t)i;
            if (nmo_hash_table_get(registry->type_to_manager, &tid, &type_manager_idx) == NMO_OK) {
                if (type_manager_idx == manager_index) {
                    nmo_hash_table_remove(registry->type_to_manager, &tid);
                    
                    // Also clear saver_manager field in type descriptor
                    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, i);
                    if (type) {
                        type->saver_manager = NMO_MANAGER_INDEX_INVALID;
                    }
                }
            }
        }
    }

    // Remove manager from array
    nmo_saver_manager_t **manager_ptr = (nmo_saver_manager_t **)nmo_arena_array_get(&registry->saver_managers, manager_index);
    if (manager_ptr && *manager_ptr) {
        // Arena memory cannot be freed individually, just clear the pointer
        *manager_ptr = NULL;
    }

    // Remove from GUID map
    nmo_hash_table_remove(registry->manager_guid_map, &manager_guid);
    
    NMO_RETURN_OK();
}

const nmo_saver_manager_t* nmo_type_registry_get_saver_manager(
    const nmo_type_registry_t *registry,
    nmo_guid_t manager_guid) {
    
    if (!registry || !registry->manager_guid_map) return NULL;
    
    nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
    if (nmo_hash_table_get(registry->manager_guid_map, &manager_guid, &manager_index) != NMO_OK) {
        return NULL;
    }
    
    if (manager_index < 0 || (size_t)manager_index >= registry->saver_managers.count) {
        return NULL;
    }
    
    nmo_saver_manager_t *manager = *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, manager_index);
    return manager;
}

nmo_status_t nmo_type_registry_set_type_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid,
    nmo_guid_t manager_guid) {
    
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry");
    }

    nmo_status_t mutable_res = ensure_registry_mutable(registry, "set type manager");
    if (mutable_res != NMO_OK) {
        return mutable_res;
    }
    
    // Find type
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, type_guid);
    if (type_id == NMO_TYPE_ID_INVALID) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Type not found");
    }
    
    // Find manager
    if (!registry->manager_guid_map) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "No managers registered");
    }
    
    nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
    if (nmo_hash_table_get(registry->manager_guid_map, &manager_guid, &manager_index) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Manager not found");
    }
    
    // Update mapping
    if (!registry->type_to_manager) {
        registry->type_to_manager = nmo_hash_table_create(
            NULL, sizeof(nmo_type_id_t), sizeof(nmo_manager_index_t),
            16, NULL, NULL);
        if (!registry->type_to_manager) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to create type manager map");
        }
    }
    
    nmo_status_t map_result = nmo_hash_table_insert(registry->type_to_manager, &type_id, &manager_index);
    if (map_result != NMO_OK) {
        return map_result;
    }

    // Update type descriptor after mapping succeeds
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type) {
        type->saver_manager = manager_index;
    }
    
    NMO_RETURN_OK();
}

const nmo_saver_manager_t* nmo_type_registry_get_type_manager(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid) {
    
    if (!registry) return NULL;
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, type_guid);
    if (type_id == NMO_TYPE_ID_INVALID) return NULL;
    
    // Check type descriptor first (faster)
    const nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type && type->saver_manager != NMO_MANAGER_INDEX_INVALID) {
        if (type->saver_manager >= 0 && (size_t)type->saver_manager < registry->saver_managers.count) {
            return *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, type->saver_manager);
        }
    }
    
    // Fallback to hash table (should match)
    if (registry->type_to_manager) {
        nmo_manager_index_t manager_index = NMO_MANAGER_INDEX_INVALID;
        if (nmo_hash_table_get(registry->type_to_manager, &type_id, &manager_index) == NMO_OK) {
            if (manager_index >= 0 && (size_t)manager_index < registry->saver_managers.count) {
                return *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, manager_index);
            }
        }
    }
    
    return NULL;
}

nmo_status_t nmo_type_registry_clear_type_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid) {
    
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL registry");
    }
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, type_guid);
    if (type_id == NMO_TYPE_ID_INVALID) NMO_RETURN_OK();
    
    // Update type descriptor
    nmo_type_descriptor_t *type = *(nmo_type_descriptor_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->types, type_id);
    if (type) {
        type->saver_manager = NMO_MANAGER_INDEX_INVALID;
    }
    
    // Update mapping
    if (registry->type_to_manager) {
        nmo_hash_table_remove(registry->type_to_manager, &type_id);
    }
    
    NMO_RETURN_OK();
}

size_t nmo_type_registry_get_manager_count(const nmo_type_registry_t *registry) {
    if (!registry) return 0;
    
    size_t count = 0;
    for (size_t i = 0; i < registry->saver_managers.count; i++) {
        nmo_saver_manager_t *manager = *(nmo_saver_manager_t **)nmo_arena_array_get((nmo_arena_array_t*)&registry->saver_managers, i);
        if (manager != NULL) {
            count++;
        }
    }
    return count;
}
