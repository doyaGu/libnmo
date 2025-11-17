/**
 * @file nmo_schema_registry.h
 * @brief Schema registry for managing type descriptors
 *
 * The registry maintains the "single source of truth" for all schema types.
 * It provides:
 * - Registration of built-in and custom types
 * - Lookup by name, class ID, or manager GUID
 * - Consistency validation across the type graph
 * - Support for schema evolution and versioning
 */

#ifndef NMO_SCHEMA_REGISTRY_H
#define NMO_SCHEMA_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_class_hierarchy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Schema registry
 *
 * Opaque structure holding all registered types and their indices.
 */
typedef struct nmo_schema_registry nmo_schema_registry_t;

/* =============================================================================
 * LIFECYCLE
 * ============================================================================= */

/**
 * @brief Create schema registry
 *
 * Creates an empty registry. Call nmo_schema_registry_add_builtin() to
 * register standard types (u32, f32, Vec3, Transform, etc.).
 *
 * @param arena Arena for allocations (required)
 * @return New registry or NULL on allocation failure
 */
NMO_API nmo_schema_registry_t *nmo_schema_registry_create(nmo_arena_t *arena);

/**
 * @brief Destroy schema registry
 *
 * Releases internal hash/index structures. The caller remains responsible
 * for destroying the arena that was passed to create().
 *
 * @param registry Registry to destroy (can be NULL)
 */
NMO_API void nmo_schema_registry_destroy(nmo_schema_registry_t *registry);

/* =============================================================================
 * REGISTRATION
 * ============================================================================= */

/**
 * @brief Register a type
 *
 * Adds a type descriptor to the registry. The descriptor must remain valid
 * for the lifetime of the registry (typically static data).
 *
 * @param registry Registry
 * @param type Type descriptor (required, must be valid)
 * @return Result with error if type is invalid or already registered
 */
NMO_API nmo_result_t nmo_schema_registry_add(
    nmo_schema_registry_t *registry,
    const nmo_schema_type_t *type);

/**
 * @brief Register all built-in types
 *
 * Registers standard types:
 * - Scalars: u8-u64, i8-i64, f32, f64, bool, string
 * - Math: Vec2, Vec3, Vec4, Quat, Mat4, BoundingBox
 * - Virtools: GUID, ObjectID, Transform, etc.
 *
 * Should be called once after creating a new registry.
 *
 * @param registry Registry
 * @return Result with error on failure
 */
NMO_API nmo_result_t nmo_schema_registry_add_builtin(
    nmo_schema_registry_t *registry);

/* =============================================================================
 * LOOKUP
 * ============================================================================= */

/**
 * @brief Check if a type is compatible with a specific file version
 *
 * A type is compatible if:
 * - file_version >= type->since_version (or since_version == 0)
 * - file_version < type->removed_version (or removed_version == 0)
 *
 * @param type Type descriptor
 * @param file_version File version to check
 * @return 1 if compatible, 0 otherwise
 */
NMO_API int nmo_schema_is_compatible(
    const nmo_schema_type_t *type,
    uint32_t file_version);

/**
 * @brief Find type by name for a specific file version
 *
 * Searches for a type that is compatible with the given file version.
 * If multiple versions exist, returns the most appropriate one.
 *
 * @param registry Registry
 * @param name Type name
 * @param file_version Target file version
 * @return Type descriptor or NULL if not found or incompatible
 */
NMO_API const nmo_schema_type_t *nmo_schema_registry_find_for_version(
    const nmo_schema_registry_t *registry,
    const char *name,
    uint32_t file_version);

/**
 * @brief Find all version variants of a type
 *
 * Retrieves all registered versions of a type with the given base name.
 * Useful for version migration analysis.
 *
 * @param registry Registry
 * @param base_name Base type name (without version suffix)
 * @param arena Arena for output array allocation
 * @param out_schemas Output array of type pointers (allocated in arena)
 * @param out_count Output count of found variants
 * @return Result with error on failure
 */
NMO_API nmo_result_t nmo_schema_registry_find_all_variants(
    const nmo_schema_registry_t *registry,
    const char *base_name,
    nmo_arena_t *arena,
    const nmo_schema_type_t ***out_schemas,
    size_t *out_count);

/**
 * @brief Find type by name
 *
 * @param registry Registry
 * @param name Type name (e.g., "Vec3", "Transform")
 * @return Type descriptor or NULL if not found
 */
NMO_API const nmo_schema_type_t *nmo_schema_registry_find_by_name(
    const nmo_schema_registry_t *registry,
    const char *name);

/**
 * @brief Find type by Virtools class ID
 *
 * Used to map chunk class_id to schema type for object deserialization.
 *
 * @param registry Registry
 * @param class_id Virtools class ID
 * @return Type descriptor or NULL if not found
 */
NMO_API const nmo_schema_type_t *nmo_schema_registry_find_by_class_id(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Find type by class ID with inheritance support
 *
 * Like find_by_class_id, but searches up the inheritance chain if no
 * exact match is found. Useful for finding generic deserializers for
 * derived classes.
 *
 * Example: If CKSprite(28) has no registered schema but CK2dEntity(27)
 * does, this will return the CK2dEntity schema.
 *
 * @param registry Registry
 * @param class_id Virtools class ID
 * @return Type descriptor for class_id or its nearest ancestor, or NULL
 */
NMO_API const nmo_schema_type_t *nmo_schema_registry_find_by_class_id_inherited(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Find type by manager GUID
 *
 * Used to lookup manager-specific data structures.
 *
 * @param registry Registry
 * @param guid Manager GUID
 * @return Type descriptor or NULL if not found
 */
NMO_API const nmo_schema_type_t *nmo_schema_registry_find_by_guid(
    const nmo_schema_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Get number of registered types
 *
 * @param registry Registry
 * @return Type count
 */
NMO_API size_t nmo_schema_registry_get_count(
    const nmo_schema_registry_t *registry);

/* =============================================================================
 * ITERATION AND VALIDATION
 * ============================================================================= */

/**
 * @brief Iterator callback for type enumeration
 *
 * @param type Current type
 * @param user_data User-provided context
 * @return 0 to stop iteration, non-zero to continue
 */
typedef int (*nmo_schema_iterator_fn)(
    const nmo_schema_type_t *type,
    void *user_data);

/**
 * @brief Iterate over all registered types
 *
 * @param registry Registry
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
NMO_API void nmo_schema_registry_iterate(
    const nmo_schema_registry_t *registry,
    nmo_schema_iterator_fn callback,
    void *user_data);

/**
 * @brief Verify registry consistency
 *
 * Checks:
 * - All type references are valid (no dangling pointers)
 * - No circular dependencies in struct types
 * - Field offsets are within struct bounds
 * - Array element types are valid
 *
 * Should be called after registering custom types or before production use.
 *
 * @param registry Registry
 * @param arena Arena for error message allocations
 * @return Result with error if inconsistencies found
 */
NMO_API nmo_result_t nmo_schema_registry_verify(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * EXTENDED METADATA (for tooling)
 * ============================================================================= */

/**
 * @brief Register class ID mapping
 *
 * Associates a Virtools class ID with a schema type for automatic lookup
 * during deserialization.
 *
 * @param registry Registry
 * @param class_id Virtools class ID
 * @param type Type descriptor
 * @return Result with error if already mapped to different type
 */
NMO_API nmo_result_t nmo_schema_registry_map_class_id(
    nmo_schema_registry_t *registry,
    nmo_class_id_t class_id,
    const nmo_schema_type_t *type);

/**
 * @brief Register manager GUID mapping
 *
 * Associates a manager GUID with a schema type for manager data parsing.
 *
 * @param registry Registry
 * @param guid Manager GUID
 * @param type Type descriptor
 * @return Result with error if already mapped to different type
 */
NMO_API nmo_result_t nmo_schema_registry_map_guid(
    nmo_schema_registry_t *registry,
    nmo_guid_t guid,
    const nmo_schema_type_t *type);

/* =============================================================================
 * CLASS HIERARCHY INTEGRATION
 * ============================================================================= */

/**
 * @brief Check if class should use CKBeObject deserialization
 *
 * Convenience wrapper around nmo_class_uses_beobject_deserializer().
 * Replaces hardcoded checks like "class_id >= 0x0A" or "class_id >= 10".
 *
 * @param registry Registry
 * @param class_id Class ID to check
 * @return 1 if uses CKBeObject deserializer, 0 otherwise
 */
NMO_API int nmo_schema_registry_uses_beobject_deserializer(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check if one class is derived from another
 *
 * Convenience wrapper around nmo_class_is_derived_from().
 *
 * @param registry Registry
 * @param child_id Class ID to check
 * @param parent_id Potential ancestor class ID
 * @return 1 if child is derived from parent, 0 otherwise
 */
NMO_API int nmo_schema_registry_is_derived_from(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t child_id,
    nmo_class_id_t parent_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_REGISTRY_H */
