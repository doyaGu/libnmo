/**
 * @file nmo_object_type_common.h
 * @brief Common helpers for CKObject-derived type vtables
 */

#ifndef NMO_OBJECT_TYPE_COMMON_H
#define NMO_OBJECT_TYPE_COMMON_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash.h"
#include "format/nmo_chunk.h"
#include "type/type_system.h"
#include "type/type_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default lifecycle / operations */
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

/* Generic deep-copy helpers */
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

/* Validation helpers */
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

/* Per-type equals/hash macro */
#define NMO_DEFINE_OBJECT_STATE_OPS(_name, _state_t) \
static bool _name##_equals(const void *a, const void *b) { \
    if (a == b) { \
        return true; \
    } \
    if (!a || !b) { \
        return false; \
    } \
    return memcmp(a, b, sizeof(_state_t)) == 0; \
} \
static uint32_t _name##_hash(const void *instance) { \
    if (!instance) { \
        return 0; \
    } \
    return (uint32_t)nmo_hash_fnv1a(instance, sizeof(_state_t)); \
}

#define NMO_OBJECT_VTABLE(_serialize, _deserialize, _copy, _validate, _equals, _hash) .create = nmo_object_default_create, .destroy = nmo_object_default_destroy, .copy = (_copy), .serialize = (_serialize), .deserialize = (_deserialize), .validate = (_validate), .equals = (_equals), .hash = (_hash), .to_string = nmo_object_default_to_string, .from_string = nmo_object_default_from_string

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
        .vtable = (_vtable), \
        .creator_plugin = NULL \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Declarations for schema headers */
#define NMO_DECLARE_OBJECT_SCHEMA(_vtable, _register_fn) \
    NMO_API extern nmo_type_vtable_t _vtable; \
    NMO_API nmo_status_t _register_fn(nmo_type_registry_t *registry);

/* Definition helper for schema source files */
#define NMO_DEFINE_OBJECT_SCHEMA(_prefix, _state_t, _serialize, _deserialize, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(_serialize, _deserialize, nmo_object_copy, nmo_object_validate, \
                          _prefix##_equals, _prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_TYPE_COMMON_H */
