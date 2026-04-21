/**
 * @file nmo_reflection.h
 * @brief Compile-time reflection macros and runtime reflection API
 *
 * Provides:
 * - Compile-time macros for defining field descriptors
 * - Runtime API for querying type fields
 * - Field access helpers for reading/writing field values
 *
 * Architecture: Type Layer (no dependencies on Object/Session layers)
 */

#ifndef NMO_REFLECTION_H
#define NMO_REFLECTION_H

#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "core/nmo_error.h"
#include <stddef.h>
#include <stdbool.h>

#define NMO_REFLECTION_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER
#define NMO_REFLECTION_FIELD_QUERY_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_REFLECTION_AUTHORING_API_TIER NMO_API_TIER_PUBLIC_PROTOCOL

/*
 * Reflection stays public for schema authors and advanced inspectors. Future
 * bindings should avoid depending on these raw field/query structures as their
 * default metadata story unless they explicitly need advanced reflection.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Compile-Time Field Definition Macros
 * ============================================================================ */

/**
 * @brief Define a simple field (no flags, no semantic)
 * Note: _type_guid must be a bare GUID macro token (no parentheses).
 * This macro uses the corresponding *_INIT form to keep file-scope initializers
 * pedantic-clean.
 */

#define NMO_FIELD(_struct, _field, _type_guid) \
    { \
        .name = #_field, \
        .description = NULL, \
        .type_guid = _type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_field), \
        .flags = 0, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_NONE, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define an optional field
 */
#define NMO_FIELD_OPT(_struct, _field, _type_guid) \
    { \
        .name = #_field, \
        .description = NULL, \
        .type_guid = _type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_field), \
        .flags = NMO_FIELD_OPTIONAL, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_NONE, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define a repeated field without companion count metadata.
 *
 * Use this for inline/fixed-size repeated storage only. A repeated field whose
 * storage is a raw pointer must use NMO_FIELD_ARRAY_COUNTED,
 * NMO_FIELD_REF_ARRAY_COUNTED, or NMO_FIELD_PTR_ARRAY so exporters/importers
 * can resolve its runtime element count without naming heuristics.
 */
#define NMO_FIELD_ARRAY(_struct, _ptr_field, _elem_type_guid) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = _elem_type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = NMO_FIELD_REPEATED, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_NONE, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define a raw pointer array with explicit count metadata.
 *
 * The runtime element count is `_count_field * _count_multiplier`.
 * Use multiplier 1 for ordinary pointer arrays, 3 for triangle index arrays,
 * byte-size fields for byte buffers, etc. This macro intentionally keeps only
 * NMO_FIELD_REPEATED: the field stores a pointer to contiguous elements, not a
 * pointer-valued scalar.
 */
#define NMO_FIELD_ARRAY_COUNTED(_struct, _ptr_field, _count_field, _count_multiplier, _elem_type_guid) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = _elem_type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = NMO_FIELD_REPEATED, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_NONE, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL, \
        .count_field_name = #_count_field, \
        .count_multiplier = (uint32_t)(_count_multiplier) \
    }

#define NMO_FIELD_ARRAY_COUNTED_FLAGS(_struct, _ptr_field, _count_field, _count_multiplier, _elem_type_guid, _flags, _semantic) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = _elem_type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = (_flags) | NMO_FIELD_REPEATED, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = (_semantic), \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL, \
        .count_field_name = #_count_field, \
        .count_multiplier = (uint32_t)(_count_multiplier) \
    }

/**
 * @brief Define an array field with explicit name (no struct lookup for offset)
 */
#define NMO_FIELD_ARRAY_NAMED(_name, _offset, _size, _elem_type_guid, _flags, _semantic) \
    { \
        .name = (_name), \
        .description = NULL, \
        .type_guid = _elem_type_guid##_INIT, \
        .offset = (uint32_t)(_offset), \
        .size = (uint32_t)(_size), \
        .flags = (_flags) | NMO_FIELD_REPEATED, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = (_semantic), \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define an object reference field
 */
#define NMO_FIELD_REF(_struct, _field) \
    { \
        .name = #_field, \
        .description = NULL, \
        .type_guid = CKPGUID_ID_INIT, \
        .offset = (uint32_t)offsetof(_struct, _field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_field), \
        .flags = NMO_FIELD_REFERENCE, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_OBJECT_REF, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define an array of object references
 */
#define NMO_FIELD_REF_ARRAY(_struct, _ptr_field) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = CKPGUID_ID_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = NMO_FIELD_REPEATED | NMO_FIELD_REFERENCE, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_OBJECT_REF, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define a raw pointer array of object references with count metadata.
 */
#define NMO_FIELD_REF_ARRAY_COUNTED(_struct, _ptr_field, _count_field) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = CKPGUID_ID_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = NMO_FIELD_REPEATED | NMO_FIELD_REFERENCE, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_OBJECT_REF, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL, \
        .count_field_name = #_count_field, \
        .count_multiplier = 1u \
    }

/**
 * @brief Define a pointer-to-struct field (dereference before formatting)
 */
#define NMO_FIELD_PTR(_struct, _ptr_field, _pointee_type_guid) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = _pointee_type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = NMO_FIELD_POINTER | NMO_FIELD_OPTIONAL, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_NONE, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define a raw pointer+count array field with explicit count field name.
 *
 * @param _struct         Struct type containing the field
 * @param _ptr_field      Pointer field name (e.g. items)
 * @param _count_field    Count field name (e.g. item_count)
 * @param _elem_type_guid Element type GUID (without _INIT suffix)
 */
#define NMO_FIELD_PTR_ARRAY(_struct, _ptr_field, _count_field, _elem_type_guid) \
    { \
        .name = #_ptr_field, \
        .description = NULL, \
        .type_guid = _elem_type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _ptr_field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_ptr_field), \
        .flags = NMO_FIELD_POINTER | NMO_FIELD_REPEATED, \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = NMO_SEMANTIC_NONE, \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL, \
        .count_field_name = #_count_field, \
        .count_multiplier = 1u \
    }

/**
 * @brief Define a field with full specification
 */
#define NMO_FIELD_FULL(_struct, _field, _type_guid, _flags, _semantic) \
    { \
        .name = #_field, \
        .description = NULL, \
        .type_guid = _type_guid##_INIT, \
        .offset = (uint32_t)offsetof(_struct, _field), \
        .size = (uint32_t)sizeof(((_struct*)0)->_field), \
        .flags = (_flags), \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = (_semantic), \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Define a field with custom name and explicit offset/size
 */
#define NMO_FIELD_NAMED(_name, _offset, _size, _type_guid, _flags, _semantic) \
    { \
        .name = (_name), \
        .description = NULL, \
        .type_guid = _type_guid##_INIT, \
        .offset = (uint32_t)(_offset), \
        .size = (uint32_t)(_size), \
        .flags = (_flags), \
        .added_version = 0, \
        .removed_version = 0, \
        .semantic = (_semantic), \
        .units = NMO_UNITS_NONE, \
        .default_value = NULL \
    }

/**
 * @brief Count fields in a static array
 */
#define NMO_FIELD_COUNT(_fields) (sizeof(_fields) / sizeof((_fields)[0]))

/* ============================================================================
 * Runtime Reflection API
 * ============================================================================ */

/**
 * @brief Get field by name
 *
 * @param type Type descriptor
 * @param name Field name to find
 * @return Field descriptor, or NULL if not found
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_type_field_t* nmo_type_get_field_by_name(
    const nmo_type_descriptor_t *type,
    const char *name);

/**
 * @brief Get field by index
 *
 * @param type Type descriptor
 * @param index Field index (0-based)
 * @return Field descriptor, or NULL if out of range
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_type_field_t* nmo_type_get_field_by_index(
    const nmo_type_descriptor_t *type,
    size_t index);

/**
 * @brief Get field count for a type
 *
 * @param type Type descriptor
 * @return Number of fields
 */
NMO_API size_t nmo_type_get_field_count(const nmo_type_descriptor_t *type);

/**
 * @brief Check if type has reflection data
 *
 * @param type Type descriptor
 * @return true if fields are defined
 */
NMO_API bool nmo_type_has_reflection(const nmo_type_descriptor_t *type);

/**
 * @brief Get pointer to field value in instance
 *
 * @param instance Object instance pointer
 * @param field Field descriptor
 * @return Pointer to field value
 */
static inline void* nmo_field_get_ptr(void *instance, const nmo_type_field_t *field) {
    if (!instance || !field) return NULL;
    return (char*)instance + field->offset;
}

/**
 * @brief Get const pointer to field value in instance
 */
static inline const void* nmo_field_get_ptr_const(const void *instance, const nmo_type_field_t *field) {
    if (!instance || !field) return NULL;
    return (const char*)instance + field->offset;
}

/**
 * @brief Read field as int32
 */
static inline int32_t nmo_field_get_int32(const void *instance, const nmo_type_field_t *field) {
    const void *ptr = nmo_field_get_ptr_const(instance, field);
    if (!ptr) return 0;
    return *(const int32_t*)ptr;
}

/**
 * @brief Read field as uint32
 */
static inline uint32_t nmo_field_get_uint32(const void *instance, const nmo_type_field_t *field) {
    const void *ptr = nmo_field_get_ptr_const(instance, field);
    if (!ptr) return 0;
    return *(const uint32_t*)ptr;
}

/**
 * @brief Read field as float
 */
static inline float nmo_field_get_float(const void *instance, const nmo_type_field_t *field) {
    const void *ptr = nmo_field_get_ptr_const(instance, field);
    if (!ptr) return 0.0f;
    return *(const float*)ptr;
}

/**
 * @brief Read field as object ID
 */
static inline nmo_object_id_t nmo_field_get_object_id(const void *instance, const nmo_type_field_t *field) {
    const void *ptr = nmo_field_get_ptr_const(instance, field);
    if (!ptr) return 0;
    return *(const nmo_object_id_t*)ptr;
}

/**
 * @brief Read field as string pointer
 */
static inline const char* nmo_field_get_string(const void *instance, const nmo_type_field_t *field) {
    const void *ptr = nmo_field_get_ptr_const(instance, field);
    if (!ptr) return NULL;
    return *(const char*const*)ptr;
}

/**
 * @brief Check if field is a reference type
 */
static inline bool nmo_field_is_ref(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_REFERENCE);
}

/**
 * @brief Check if field is an array
 */
static inline bool nmo_field_is_array(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_REPEATED);
}

/**
 * @brief Check whether a repeated field is stored as a raw pointer.
 */
static inline bool nmo_field_uses_pointer_array_storage(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_REPEATED) && field->size == sizeof(void *);
}

/**
 * @brief Check whether a raw pointer array has explicit count metadata.
 */
static inline bool nmo_field_is_counted_pointer_array(const nmo_type_field_t *field) {
    return nmo_field_uses_pointer_array_storage(field) &&
           field->count_field_name != NULL &&
           field->count_field_name[0] != '\0';
}

/**
 * @brief Check if field is optional
 */
static inline bool nmo_field_is_optional(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_OPTIONAL);
}

/**
 * @brief Get the count field for a pointer-array field.
 *
 * Uses explicit count_field_name metadata only.
 * Returns NULL if no metadata is set or the named field is missing.
 */
static inline const nmo_type_field_t *nmo_field_get_count_field(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *field)
{
    if (type == NULL || field == NULL || field->count_field_name == NULL) {
        return NULL;
    }
    return nmo_type_get_field_by_name(type, field->count_field_name);
}

static inline uint32_t nmo_field_get_count_multiplier(
    const nmo_type_field_t *field)
{
    return field && field->count_multiplier != 0u ? field->count_multiplier : 1u;
}

/**
 * @brief Resolve the companion count field for an array field.
 *
 * Uses explicit count_field_name metadata only. If count_multiplier is set,
 * the returned value is the count field multiplied by that factor.
 *
 * @param type Type descriptor that owns array_field
 * @param array_field Array field descriptor
 * @return Count field descriptor, or NULL when not found
 */
NMO_API const nmo_type_field_t *nmo_field_resolve_count_field(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *array_field);

/**
 * @brief Resolve the runtime element count for a pointer-array field.
 *
 * Uses explicit count_field_name metadata only.
 *
 * @param type Type descriptor that owns array_field
 * @param array_field Pointer-array field descriptor
 * @param instance Object or struct instance containing both fields
 * @param out_count Output element count
 * @return NMO_OK when a count field was found, NMO_ERR_NOT_FOUND otherwise
 */
NMO_API nmo_status_t nmo_field_resolve_count(
    const nmo_type_descriptor_t *type,
    const nmo_type_field_t *array_field,
    const void *instance,
    uint32_t *out_count);

/* ============================================================================
 * Field Iteration
 * ============================================================================ */

/**
 * @brief Field visitor callback
 *
 * @param user_data User context
 * @param field Field descriptor
 * @param field_ptr Pointer to field value in instance
 * @return true to continue, false to stop
 */
typedef bool (*nmo_field_visitor_fn)(
    void *user_data,
    const nmo_type_field_t *field,
    const void *field_ptr);

/**
 * @brief Iterate over all fields in a type
 *
 * @param type Type descriptor
 * @param instance Object instance (may be NULL if only inspecting metadata)
 * @param visitor Callback for each field
 * @param user_data User context
 * @return NMO_OK on success
 * @note Does not allocate; visitor must not free field pointers.
 */
NMO_API nmo_status_t nmo_type_foreach_field(
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_field_visitor_fn visitor,
    void *user_data);

/**
 * @brief Iterate over reference fields only
 *
 * @param type Type descriptor
 * @param instance Object instance
 * @param visitor Callback for each reference field
 * @param user_data User context
 * @return NMO_OK on success
 * @note Does not allocate; visitor must not free field pointers.
 * @note Only iterates top-level fields with NMO_FIELD_REFERENCE flag.
 *       Nested struct fields containing object references are NOT traversed.
 *       For deep traversal, call this function recursively on nested types.
 */
NMO_API nmo_status_t nmo_type_foreach_ref_field(
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_field_visitor_fn visitor,
    void *user_data);

/* ============================================================================
 * Type Name Helpers
 * ============================================================================ */

/**
 * @brief Get human-readable name for a field type GUID
 *
 * @param registry Type registry (for GUID -> name resolution)
 * @param type_guid Field type GUID
 * @return Type name string (e.g., "int32", "float", "object_id")
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const char* nmo_field_type_name(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid);

/* ============================================================================
 * Specialized Metadata Reflection (Struct/Union/Enum/Flags)
 * ============================================================================ */

/**
 * @brief Get specialized metadata for a type
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_specialized_metadata_t* nmo_type_get_specialized_metadata(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type);

/**
 * @brief Struct/Union field visitor callback
 */
typedef bool (*nmo_struct_field_visitor_fn)(
    void *user_data,
    const nmo_struct_descriptor_t *field,
    const void *field_ptr);

/**
 * @brief Enum value visitor callback
 */
typedef bool (*nmo_enum_value_visitor_fn)(
    void *user_data,
    const nmo_enum_descriptor_t *value);

/**
 * @brief Flags bit visitor callback
 */
typedef bool (*nmo_flags_bit_visitor_fn)(
    void *user_data,
    const nmo_flags_descriptor_t *bit);

/**
 * @brief Get struct/union field by name
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_struct_descriptor_t* nmo_type_get_struct_field_by_name(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const char *name);

/**
 * @brief Get struct/union field by index
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_struct_descriptor_t* nmo_type_get_struct_field_by_index(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    size_t index);

/**
 * @brief Iterate over struct/union fields
 * @note Does not allocate; visitor must not free field pointers.
 */
NMO_API nmo_status_t nmo_type_foreach_struct_field(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const void *instance,
    nmo_struct_field_visitor_fn visitor,
    void *user_data);

/**
 * @brief Get enum value by name
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_enum_descriptor_t* nmo_type_get_enum_value_by_name(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const char *name);

/**
 * @brief Get enum value by index
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_enum_descriptor_t* nmo_type_get_enum_value_by_index(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    size_t index);

/**
 * @brief Iterate over enum values
 * @note Does not allocate; visitor must not free value pointers.
 */
NMO_API nmo_status_t nmo_type_foreach_enum_value(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    nmo_enum_value_visitor_fn visitor,
    void *user_data);

/**
 * @brief Get flags bit by name
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_flags_descriptor_t* nmo_type_get_flags_bit_by_name(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    const char *name);

/**
 * @brief Get flags bit by index
 * @note Returned pointer is registry-owned; do not free.
 * @ownership borrowed
 */
NMO_API const nmo_flags_descriptor_t* nmo_type_get_flags_bit_by_index(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    size_t index);

/**
 * @brief Iterate over flags bits
 * @note Does not allocate; visitor must not free bit pointers.
 */
NMO_API nmo_status_t nmo_type_foreach_flags_bit(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    nmo_flags_bit_visitor_fn visitor,
    void *user_data);

/**
 * @brief Get human-readable name for field flags
 *
 * @param flags Field flags
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of characters written
 * @note buffer is caller-owned; function does not allocate.
 */
NMO_API size_t nmo_field_flags_to_string(uint32_t flags, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REFLECTION_H */
