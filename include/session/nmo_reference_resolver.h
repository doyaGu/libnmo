/**
 * @file nmo_reference_resolver.h
 * @brief Object reference resolution system for loading Virtools files
 *
 * This module provides functionality to resolve object references during file loading.
 * References are objects that are saved with minimal metadata (ID, name, class) rather
 * than full chunk data, and must be resolved to existing objects in the repository.
 *
 * Based on: reference/src/CKFile.cpp ResolveReference() and related functions
 */

#ifndef NMO_REFERENCE_RESOLVER_H
#define NMO_REFERENCE_RESOLVER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_reference_resolver nmo_reference_resolver_t;

/**
 * @brief Object reference descriptor
 *
 * Minimal metadata stored for referenced objects, corresponding to
 * Virtools' CKFileObject with CK_FO_DONTLOADOBJECT option.
 */
typedef struct nmo_object_ref {
    nmo_object_id_t id;           /**< Original object ID from file */
    nmo_class_id_t class_id;      /**< Object class identifier */
    char *name;                   /**< Object name for matching */
    nmo_guid_t type_guid;         /**< Type GUID (for typed objects like parameters) */
    uint32_t flags;               /**< Additional flags */
    
    /* Internal fields */
    nmo_object_t *resolved_object; /**< Resolved object pointer (NULL if unresolved) */
    int32_t file_index;           /**< Position in file object list */
} nmo_object_ref_t;

/**
 * @brief Reference resolution strategy
 *
 * Custom resolution function for specific object types.
 * 
 * @param context User context passed during registration
 * @param ref Reference descriptor to resolve
 * @param repo Object repository to search in
 * @return Resolved object or NULL if not found
 */
typedef nmo_object_t *(*nmo_resolve_strategy_fn)(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
);

/**
 * @brief Resolution statistics
 */
typedef struct nmo_reference_stats {
    uint32_t total_count;       /**< Total references encountered */
    uint32_t resolved_count;    /**< Successfully resolved */
    uint32_t unresolved_count;  /**< Failed to resolve */
    uint32_t ambiguous_count;   /**< Multiple matches found */
} nmo_reference_stats_t;

/**
 * @brief Create reference resolver
 *
 * Creates a new resolver for handling object references during file load.
 * The resolver maintains strategies for different object types and tracks
 * resolution statistics.
 *
 * @param repo Object repository to resolve against (required)
 * @param arena Arena for allocations (required)
 * @return New resolver or NULL on allocation failure
 */
NMO_API nmo_reference_resolver_t *nmo_reference_resolver_create(
    nmo_object_repository_t *repo,
    nmo_arena_t *arena
);

/**
 * @brief Register custom resolution strategy
 *
 * Registers a custom resolution function for a specific object class.
 * When resolving references of this class, the custom function will be
 * called first. If it returns NULL, the default strategy is used.
 *
 * @param resolver Resolver instance (required)
 * @param class_id Object class ID (use 0 for default strategy)
 * @param strategy Resolution function (required)
 * @param context User context passed to strategy function
 * @return NMO_OK on success, error code on failure
 */
NMO_API int nmo_reference_resolver_register_strategy(
    nmo_reference_resolver_t *resolver,
    nmo_class_id_t class_id,
    nmo_resolve_strategy_fn strategy,
    void *context
);

/**
 * @brief Register a reference for resolution
 *
 * Adds a reference to the resolver's pending list. The reference will
 * be resolved later when nmo_reference_resolver_resolve_all() is called.
 *
 * @param resolver Resolver instance (required)
 * @param ref Reference descriptor (required, copied internally)
 * @return Pointer to registered reference copy, or NULL on failure
 */
NMO_API nmo_object_ref_t *nmo_reference_resolver_register_reference(
    nmo_reference_resolver_t *resolver,
    const nmo_object_ref_t *ref
);

/**
 * @brief Resolve a single reference immediately
 *
 * Attempts to resolve one reference to an existing object in the repository.
 * Uses registered strategies and falls back to default resolution.
 *
 * Resolution process:
 * 1. Try custom strategy for object's class (if registered)
 * 2. Try default strategy (name + type matching)
 * 3. Try fuzzy matching (name only)
 *
 * @param resolver Resolver instance (required)
 * @param ref Reference descriptor (required)
 * @return Resolved object or NULL if not found
 */
NMO_API nmo_object_t *nmo_reference_resolver_resolve(
    nmo_reference_resolver_t *resolver,
    const nmo_object_ref_t *ref
);

/**
 * @brief Resolve all pending references
 *
 * Processes all references that were registered via
 * nmo_reference_resolver_register_reference(). Updates each
 * reference's resolved_object field.
 *
 * @param resolver Resolver instance (required)
 * @return NMO_OK if all resolved successfully, NMO_WARN if some failed
 */
NMO_API int nmo_reference_resolver_resolve_all(
    nmo_reference_resolver_t *resolver
);

/**
 * @brief Get resolution statistics
 *
 * @param resolver Resolver instance (required)
 * @param out_stats Output statistics structure (required)
 * @return NMO_OK on success
 */
NMO_API int nmo_reference_resolver_get_stats(
    const nmo_reference_resolver_t *resolver,
    nmo_reference_stats_t *out_stats
);

/**
 * @brief Check if resolver has unresolved references
 *
 * @param resolver Resolver instance (required)
 * @return 1 if there are unresolved references, 0 otherwise
 */
NMO_API int nmo_reference_resolver_has_unresolved(
    const nmo_reference_resolver_t *resolver
);

/**
 * @brief Get list of unresolved references
 *
 * Returns pointers to all references that failed to resolve.
 * Useful for error reporting and diagnostics.
 *
 * @param resolver Resolver instance (required)
 * @param out_refs Output array of reference pointers (allocated from arena)
 * @param out_count Number of unresolved references
 * @return NMO_OK on success
 */
NMO_API int nmo_reference_resolver_get_unresolved(
    const nmo_reference_resolver_t *resolver,
    nmo_object_ref_t ***out_refs,
    size_t *out_count
);

/**
 * @brief Destroy reference resolver
 *
 * Since the resolver uses arena allocation, this is mostly a no-op.
 * The arena cleanup will free all memory.
 *
 * @param resolver Resolver to destroy
 */
NMO_API void nmo_reference_resolver_destroy(
    nmo_reference_resolver_t *resolver
);

/* ========================================================================
 * Built-in Resolution Strategies
 * ======================================================================== */

/**
 * @brief Default resolution strategy (name + class matching)
 *
 * Matches objects by exact name and class ID.
 * This is the fallback strategy used when no custom strategy is registered.
 *
 * @param context Unused
 * @param ref Reference to resolve
 * @param repo Repository to search
 * @return Matched object or NULL
 */
NMO_API nmo_object_t *nmo_resolve_strategy_default(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
);

/**
 * @brief Parameter resolution strategy (name + type + class matching)
 *
 * Special strategy for CKCID_PARAMETER objects that additionally matches
 * the parameter type GUID. Based on reference/src/CKFile.cpp:1553-1583.
 *
 * @param context Unused
 * @param ref Reference to resolve (must have valid type_guid)
 * @param repo Repository to search
 * @return Matched parameter or NULL
 */
NMO_API nmo_object_t *nmo_resolve_strategy_parameter(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
);

/**
 * @brief GUID-based resolution strategy
 *
 * Matches objects by GUID rather than name. Useful for objects that
 * have unique GUIDs (like plugins, managers, etc.).
 *
 * @param context Unused
 * @param ref Reference to resolve (must have valid type_guid)
 * @param repo Repository to search
 * @return Matched object or NULL
 */
NMO_API nmo_object_t *nmo_resolve_strategy_guid(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
);

/**
 * @brief Fuzzy resolution strategy (name only, ignore case)
 *
 * Fallback strategy that matches by name only, case-insensitive.
 * Use this as last resort when strict matching fails.
 *
 * @param context Unused
 * @param ref Reference to resolve
 * @param repo Repository to search
 * @return First matched object or NULL
 */
NMO_API nmo_object_t *nmo_resolve_strategy_fuzzy(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
);

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

/**
 * @brief Check if object ID has reference flag
 *
 * In Virtools files, referenced objects have their ID's high bit (0x800000)
 * set. This function checks for that flag.
 *
 * @param id Object ID from file
 * @return 1 if ID has reference flag, 0 otherwise
 */
static inline int nmo_object_id_is_reference(nmo_object_id_t id) {
    return (id & 0x00800000) != 0;
}

/**
 * @brief Clear reference flag from object ID
 *
 * @param id Object ID with possible reference flag
 * @return ID with reference flag cleared
 */
static inline nmo_object_id_t nmo_object_id_clear_reference_flag(nmo_object_id_t id) {
    return id & ~0x00800000;
}

/**
 * @brief Set reference flag on object ID
 *
 * @param id Object ID
 * @return ID with reference flag set
 */
static inline nmo_object_id_t nmo_object_id_set_reference_flag(nmo_object_id_t id) {
    return id | 0x00800000;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_REFERENCE_RESOLVER_H */
