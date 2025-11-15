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

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_REGISTRY_H */
