/**
 * @file nmo_object_type_common.h
 * @brief Common helpers for CKObject-derived type vtables
 */

#ifndef NMO_OBJECT_TYPE_COMMON_H
#define NMO_OBJECT_TYPE_COMMON_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_hash.h"

#include "object/nmo_deserialize_context.h"

#include "format/nmo_chunk.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Default Lifecycle / Operations
 * ============================================================================ */
NMO_API nmo_status_t nmo_object_default_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API void nmo_object_default_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_object_default_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena);

NMO_API void nmo_object_dispose_array_fields(
    void *instance,
    const nmo_type_descriptor_t *type);

/* Shared deep-copy / validate implementations used by object vtables. */
NMO_API nmo_status_t nmo_object_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena);

NMO_API nmo_status_t nmo_object_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_object_default_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_object_default_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);

NMO_API nmo_status_t nmo_object_default_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

/* ============================================================================
 * Generic Deep-Copy Helpers
 * ============================================================================ */
NMO_API nmo_status_t nmo_object_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size);

NMO_API nmo_status_t nmo_object_copy_array(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t elem_size,
    uint32_t count);

NMO_API nmo_status_t nmo_object_copy_string(
    nmo_arena_t *arena,
    char **dst,
    const char *src);

NMO_API nmo_status_t nmo_object_copy_string_array(
    nmo_arena_t *arena,
    char ***dst,
    char *const *src,
    uint32_t count);

NMO_API nmo_status_t nmo_object_copy_chunk(
    nmo_arena_t *arena,
    nmo_chunk_t **dst,
    nmo_chunk_t *src);

NMO_API nmo_status_t nmo_object_copy_chunk_array(
    nmo_arena_t *arena,
    nmo_chunk_t ***dst,
    nmo_chunk_t *const *src,
    uint32_t count);

/* ============================================================================
 * Chunk Array Helpers (nmo_array_t)
 * ============================================================================ */
NMO_API void nmo_object_array_set_chunk_lifecycle(nmo_array_t *array);

NMO_API void nmo_object_array_set_string_lifecycle(nmo_array_t *array);

NMO_API nmo_status_t nmo_object_clone_chunk_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src);

NMO_API nmo_status_t nmo_object_clone_string_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src);

/* ============================================================================
 * Validation Helpers
 * ============================================================================ */
#define NMO_VALIDATE_COUNT(ptr, count, label) \
    do { \
        if ((count) > 0 && !(ptr)) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
                                    "Missing %s array for count %u", (label), (count)); \
        } \
    } while (0)

#define NMO_VALIDATE_BYTES(ptr, size, label) \
    do { \
        if ((size) > 0 && !(ptr)) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
                                    "Missing %s buffer for size %zu", (label), (size_t)(size)); \
        } \
    } while (0)

/* ============================================================================
 * Per-Type Lifecycle Helpers
 * ============================================================================ */
#define NMO_DEFINE_OBJECT_LIFECYCLE(_prefix, _state_t, _init_block, _destroy_block) \
    static nmo_status_t nmo_##_prefix##_create( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        (void)type; \
        (void)context; \
        if (instance == NULL) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
                             "Invalid arguments to nmo_" #_prefix "_create"); \
        } \
        _state_t *state = (_state_t *)instance; \
        memset(state, 0, sizeof(*state)); \
        _init_block; \
        NMO_RETURN_OK(); \
    } \
    static void nmo_##_prefix##_destroy( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        (void)type; \
        (void)context; \
        if (instance == NULL) { \
            return; \
        } \
        _state_t *state = (_state_t *)instance; \
        _destroy_block; \
        nmo_object_dispose_array_fields(state, type); \
        memset(state, 0, sizeof(*state)); \
    }

#define NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(_prefix, _state_t) \
    NMO_DEFINE_OBJECT_LIFECYCLE(_prefix, _state_t, ((void)0), ((void)0))

/* ============================================================================
 * Per-Type State Ops (equals/hash/copy/validate)
 * ============================================================================ */
#define NMO_DEFINE_OBJECT_STATE_OPS(_name, _state_t) \
static bool nmo_##_name##_equals(const void *a, const void *b) { \
    if (a == b) { \
        return true; \
    } \
    if (!a || !b) { \
        return false; \
    } \
    return memcmp(a, b, sizeof(_state_t)) == 0; \
} \
static uint32_t nmo_##_name##_hash(const void *instance) { \
    if (!instance) { \
        return 0; \
    } \
    return (uint32_t)nmo_hash_fnv1a(instance, sizeof(_state_t)); \
} \
static nmo_status_t nmo_##_name##_copy(const void *src, void *dst, \
                                 const nmo_type_descriptor_t *type, nmo_arena_t *arena) { \
    return nmo_object_default_copy(src, dst, type, arena); \
} \
static nmo_status_t nmo_##_name##_validate(const void *instance, \
                                     const nmo_type_descriptor_t *type, void *context) { \
    return nmo_object_default_validate(instance, type, context); \
}

/* Per-type equals/hash macro without default copy/validate (for custom ops) */
#define NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(_name, _state_t) \
static bool nmo_##_name##_equals(const void *a, const void *b) { \
    if (a == b) { \
        return true; \
    } \
    if (!a || !b) { \
        return false; \
    } \
    return memcmp(a, b, sizeof(_state_t)) == 0; \
} \
static uint32_t nmo_##_name##_hash(const void *instance) { \
    if (!instance) { \
        return 0; \
    } \
    return (uint32_t)nmo_hash_fnv1a(instance, sizeof(_state_t)); \
}

/* ============================================================================
 * Vtable Helpers
 * ============================================================================ */

#define NMO_OBJECT_VTABLE(_create, _destroy, _serialize, _deserialize, _copy, _validate, _equals, _hash) \
    .create = (_create), \
    .destroy = (_destroy), \
    .copy = (_copy), \
    .serialize = (_serialize), \
    .deserialize = (_deserialize), \
    .validate = (_validate), \
    .equals = (_equals), \
    .hash = (_hash), \
    .to_string = nmo_object_default_to_string, \
    .from_string = nmo_object_default_from_string, \
    .enumerate_refs = NULL

/**
 * @brief Extended vtable macro with enumerate_refs support
 * 
 * Use this when a schema provides its own reference enumerator.
 */
#define NMO_OBJECT_VTABLE_EX(_create, _destroy, _serialize, _deserialize, _copy, _validate, _equals, _hash, _enumerate_refs) \
    .create = (_create), \
    .destroy = (_destroy), \
    .copy = (_copy), \
    .serialize = (_serialize), \
    .deserialize = (_deserialize), \
    .validate = (_validate), \
    .equals = (_equals), \
    .hash = (_hash), \
    .to_string = nmo_object_default_to_string, \
    .from_string = nmo_object_default_from_string, \
    .enumerate_refs = (_enumerate_refs)

/* ============================================================================
 * Registration Helpers
 * ============================================================================ */

/* Registration helper for per-schema files */
#define NMO_DEFINE_OBJECT_REGISTRATION(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable) \
NMO_API nmo_status_t _func(nmo_type_registry_t *registry) { \
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
               "NULL type registry"); \
    nmo_type_descriptor_t type_desc = { \
        .guid = (_guid), \
        .name = (_name), \
        .size = (uint32_t)sizeof(_state_t), \
        .alignment = (uint32_t)alignof(_state_t), \
        .class_id = (_class_id), \
        .base_type = (_base_guid), \
        .category = NMO_TYPE_CATEGORY_OBJECT_REF, \
        .flags = NMO_TYPE_FLAG_SERIALIZABLE, \
        .id = NMO_TYPE_ID_INVALID, \
        .description = NULL, \
        .fields = NULL, \
        .field_count = 0, \
        .vtable = (_vtable) \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Registration helper with reflection fields */
#define NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields) \
NMO_API nmo_status_t _func(nmo_type_registry_t *registry) { \
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
               "NULL type registry"); \
    nmo_type_descriptor_t type_desc = { \
        .guid = (_guid), \
        .name = (_name), \
        .size = (uint32_t)sizeof(_state_t), \
        .alignment = (uint32_t)alignof(_state_t), \
        .class_id = (_class_id), \
        .base_type = (_base_guid), \
        .category = NMO_TYPE_CATEGORY_OBJECT_REF, \
        .flags = NMO_TYPE_FLAG_SERIALIZABLE, \
        .id = NMO_TYPE_ID_INVALID, \
        .description = NULL, \
        .fields = (_fields), \
        .field_count = sizeof(_fields) / sizeof((_fields)[0]), \
        .vtable = (_vtable) \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Registration helper with reflection fields and explicit field count */
#define NMO_DEFINE_OBJECT_REGISTRATION_FIELDS_COUNT(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields, _field_count) \
NMO_API nmo_status_t _func(nmo_type_registry_t *registry) { \
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
               "NULL type registry"); \
    nmo_type_descriptor_t type_desc = { \
        .guid = (_guid), \
        .name = (_name), \
        .size = (uint32_t)sizeof(_state_t), \
        .alignment = (uint32_t)alignof(_state_t), \
        .class_id = (_class_id), \
        .base_type = (_base_guid), \
        .category = NMO_TYPE_CATEGORY_OBJECT_REF, \
        .flags = NMO_TYPE_FLAG_SERIALIZABLE, \
        .id = NMO_TYPE_ID_INVALID, \
        .description = NULL, \
        .fields = (_fields), \
        .field_count = (_field_count), \
        .vtable = (_vtable) \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Runtime registration helper aliases used by migrated schema files. */
#define NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable) \
    NMO_DEFINE_OBJECT_REGISTRATION(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable)

#define NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields) \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields)

/* ============================================================================
 * Schema Declarations
 * ============================================================================ */

/* Declarations for schema headers */
#define NMO_DECLARE_OBJECT_SCHEMA(_vtable, _register_fn) \
    NMO_API extern nmo_type_vtable_t _vtable; \
    NMO_API nmo_status_t _register_fn(nmo_type_registry_t *registry);

/* ============================================================================
 * Schema Definition Macros
 * ============================================================================ */

/* Definition helper for schema source files */
#define NMO_DEFINE_OBJECT_SCHEMA(_prefix, _state_t, _serialize, _deserialize, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

/* Definition helper with reflection fields */
#define NMO_DEFINE_OBJECT_SCHEMA_FIELDS(_prefix, _state_t, _serialize, _deserialize, _fields, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                          _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields)

/* Definition helper with reflection fields and explicit field count */
#define NMO_DEFINE_OBJECT_SCHEMA_FIELDS_COUNT(_prefix, _state_t, _serialize, _deserialize, _fields, _field_count, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS_COUNT(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                                 _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields, _field_count)

/* Schema definition with enumerate_refs for reflection support */
#define NMO_DEFINE_OBJECT_SCHEMA_REFS(_prefix, _state_t, _serialize, _deserialize, _enumerate_refs, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE_EX(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash, _enumerate_refs) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

/* Schema definition with both enumerate_refs and reflection fields */
#define NMO_DEFINE_OBJECT_SCHEMA_REFS_FIELDS(_prefix, _state_t, _serialize, _deserialize, _enumerate_refs, _fields, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE_EX(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash, _enumerate_refs) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                          _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields)

/* ============================================================================
 * Custom Schema Macros (expect _prefix##_copy/_prefix##_validate to be defined)
 * ============================================================================ */

/* Custom schema macros (expect _prefix##_copy/_prefix##_validate to be defined by caller) */
#define NMO_DEFINE_OBJECT_SCHEMA_CUSTOM(_prefix, _state_t, _serialize, _deserialize, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

#define NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(_prefix, _state_t, _serialize, _deserialize, _fields, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                          _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields)

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_TYPE_COMMON_H */
