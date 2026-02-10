/**
 * @file nmo_operation_system.h
 * @brief Parameter operation system (Phase 6.1)
 *
 * Implements CKParameter-compatible operation system with 4D lookup tree.
 * Supports 50+ builtin operations (arithmetic, logic, comparison, etc.)
 * and custom operations registered by plugins.
 *
 * Design:
 * - 4D tree structure: Operation GUID -> P1 Type -> P2 Type -> Result Type
 * - O(log N) lookup via binary search at each level
 * - Type inheritance matching (derived types accepted)
 * - Cache-friendly sorted arrays
 *
 * Reference: CKParameterManager::ProcessParameterCombinations
 */

#ifndef NMO_OPERATION_SYSTEM_H
#define NMO_OPERATION_SYSTEM_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "type/nmo_type_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_logger nmo_logger_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;

/* ============================================================================
 * Operation Function Signature
 * ============================================================================ */

/**
 * @brief Operation function signature
 *
 * Performs an operation on one or two parameters and writes the result.
 *
 * @param p1_data      Pointer to parameter 1 data (never NULL)
 * @param p1_type      Type descriptor of parameter 1
 * @param p2_data      Pointer to parameter 2 data (NULL for unary operations)
 * @param p2_type      Type descriptor of parameter 2 (NULL for unary operations)
 * @param result_data  Pointer to result buffer (pre-allocated by caller)
 * @param result_type  Expected result type descriptor
 * @param user_data    Optional user data (from operation descriptor)
 * @return NMO_OK on success, error code on failure
 *
 * @note
 * - Function must validate type compatibility if needed
 * - result_data is always pre-allocated to result_type->size bytes
 * - Function must not allocate memory (use provided buffer)
 * - Thread-safety: Function must be reentrant (no shared state)
 */
typedef nmo_status_t (*nmo_operation_fn)(
    const void *p1_data,
    const nmo_type_descriptor_t *p1_type,
    const void *p2_data,
    const nmo_type_descriptor_t *p2_type,
    void *result_data,
    const nmo_type_descriptor_t *result_type,
    void *user_data
);

/* ============================================================================
 * Operation Descriptor
 * ============================================================================ */

/**
 * @brief Operation flags
 */
typedef enum nmo_operation_flags {
    NMO_OP_NONE = 0,
    NMO_OP_COMMUTATIVE = 1 << 0,  /**< Operation is commutative (a op b == b op a) */
    NMO_OP_ASSOCIATIVE = 1 << 1,  /**< Operation is associative ((a op b) op c == a op (b op c)) */
    NMO_OP_UNARY = 1 << 2,        /**< Unary operation (only p1, p2 is NULL) */
    NMO_OP_BINARY = 1 << 3,       /**< Binary operation (both p1 and p2 required) */
    NMO_OP_HIDDEN = 1 << 4,       /**< Hidden from UI (internal use only) */
} nmo_operation_flags_t;

/**
 * @brief Operation descriptor
 *
 * Describes a single operation between two parameter types.
 * Stored in 4D tree leaf nodes.
 *
 * Size: 64 bytes (cache-line friendly)
 */
typedef struct nmo_operation_desc {
    nmo_guid_t operation_guid;           /**< Operation family GUID (e.g., Add, Multiply) */
    nmo_guid_t p1_type_guid;             /**< Parameter 1 type GUID */
    nmo_guid_t p2_type_guid;             /**< Parameter 2 type GUID (NULL_GUID for unary) */
    nmo_guid_t result_type_guid;         /**< Result type GUID */
    
    nmo_operation_fn function;           /**< Operation function pointer */
    void *user_data;                     /**< Optional user data for function */
    
    uint32_t flags;                      /**< Operation flags (nmo_operation_flags_t) */
    uint32_t priority;                   /**< Priority for ambiguous matches (higher = preferred) */
    
    const char *name;                    /**< Operation name (e.g., "Add", "Multiply") */
    const char *description;             /**< Human-readable description */
} nmo_operation_desc_t;

/* ============================================================================
 * 4D Tree Structures
 * ============================================================================ */

/**
 * @brief 4D tree cell (leaf node)
 *
 * Represents one valid operation: Operation -> P1 -> P2 -> Result
 * Stored in sorted arrays for binary search.
 *
 * Size: 72 bytes (includes operation descriptor)
 */
typedef struct nmo_operation_tree_cell {
    nmo_operation_desc_t desc;           /**< Operation descriptor */
    
    /* Type descriptors (cached for fast access) */
    const nmo_type_descriptor_t *p1_type;     /**< P1 type descriptor (NULL for wildcard) */
    const nmo_type_descriptor_t *p2_type;     /**< P2 type descriptor (NULL for wildcard) */
    const nmo_type_descriptor_t *result_type; /**< Result type descriptor */
    
    /* Statistics (for profiling) */
    uint64_t call_count;                 /**< Number of times this operation was executed */
} nmo_operation_tree_cell_t;

/**
 * @brief P2 type layer (level 3)
 *
 * Array of cells with the same Operation + P1 type, sorted by P2 type GUID.
 */
typedef struct nmo_operation_p2_layer {
    nmo_guid_t p2_type_guid;             /**< P2 type GUID (for verification) */
    
    nmo_operation_tree_cell_t *cells;    /**< Sorted array of cells (by p2_type_guid) */
    uint32_t cell_count;                 /**< Number of cells */
    uint32_t cell_capacity;              /**< Allocated capacity */
} nmo_operation_p2_layer_t;

/**
 * @brief P1 type layer (level 2)
 *
 * Array of P2 layers with the same Operation, sorted by P1 type GUID.
 */
typedef struct nmo_operation_p1_layer {
    nmo_guid_t p1_type_guid;             /**< P1 type GUID (for verification) */
    
    nmo_operation_p2_layer_t *p2_layers; /**< Sorted array of P2 layers (by p2_type_guid) */
    uint32_t layer_count;                /**< Number of P2 layers */
    uint32_t layer_capacity;             /**< Allocated capacity */
} nmo_operation_p1_layer_t;

/**
 * @brief Operation family (level 1 - root)
 *
 * Top-level structure representing one operation (e.g., Add, Multiply).
 * Contains all type combinations for this operation.
 */
typedef struct nmo_operation_family {
    nmo_guid_t operation_guid;           /**< Operation family GUID */
    const char *name;                    /**< Operation name (e.g., "Add") */
    const char *description;             /**< Human-readable description */
    
    nmo_operation_p1_layer_t *p1_layers; /**< Sorted array of P1 layers */
    uint32_t layer_count;                /**< Number of P1 layers */
    uint32_t layer_capacity;             /**< Allocated capacity */
    
    /* Statistics */
    uint64_t total_operations;           /**< Total number of operations in family */
    uint64_t total_calls;                /**< Total number of calls across all operations */
} nmo_operation_family_t;

/* ============================================================================
 * Operation Registry
 * ============================================================================ */

/**
 * @brief Operation registry
 *
 * Central registry for all parameter operations.
 * Supports O(1) family lookup by GUID, O(log N) operation lookup.
 *
 * Thread-safety: Not thread-safe, caller must synchronize.
 * Rationale: Registry is typically populated at initialization time.
 */
typedef struct nmo_operation_registry {
    /* OWNERSHIP:
     * - owner: context/registry
     * - allocator: arena
     * - lifetime: registry
     * - free: nmo_operation_registry_destroy()
     * - thread: caller-synchronized
     */
    nmo_arena_t *arena;                  /**< Arena for all allocations */
    
    /* Operation families (root level) */
    nmo_operation_family_t **families;   /**< Array of operation family pointers */
    uint32_t family_count;               /**< Number of families */
    uint32_t family_capacity;            /**< Allocated capacity */
    
    /* GUID-based hash map for O(1) family lookup */
    nmo_hash_table_t *family_map;        /**< GUID -> family index */

    /* Lookup cache for resolved operations */
    nmo_hash_table_t *lookup_cache;      /**< cache key -> cell pointer */
    uint32_t registry_version;           /**< Incremented on operation changes */
    uint32_t cache_version;              /**< Version used by lookup_cache */
    uint32_t cached_type_registry_version; /**< Type registry version for cache */
    
    /* Statistics */
    uint64_t total_operations;           /**< Total operations across all families */
    uint64_t total_lookups;              /**< Total lookup operations */
    uint64_t cache_hits;                 /**< Cache hits (for future optimization) */
} nmo_operation_registry_t;

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================ */

/* OWNERSHIP (Operation Registry API):
 * - registry returned by create(): caller-owned, destroy with nmo_operation_registry_destroy()
 * - lookup results are registry-owned; do not free
 * - thread: caller-synchronized
 */

/**
 * @brief Create operation registry
 *
 * @param arena Arena for allocations (must outlive registry)
 * @return Registry or NULL on error
 * @note Returned registry is caller-owned.
 */
NMO_API nmo_operation_registry_t *nmo_operation_registry_create(nmo_arena_t *arena);

/**
 * @brief Destroy operation registry
 *
 * Frees the registry structure (arena contents remain).
 *
 * @param registry Registry to destroy
 */
NMO_API void nmo_operation_registry_destroy(nmo_operation_registry_t *registry);

/**
 * @brief Finalize operation registry caches
 *
 * Clears and primes caches after bulk registration.
 *
 * @param registry Operation registry
 * @param type_registry Type registry (used for cache versioning)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_operation_registry_finalize(
    nmo_operation_registry_t *registry,
    const nmo_type_registry_t *type_registry);

/* ============================================================================
 * Operation Registration
 * ============================================================================ */

/**
 * @brief Register operation
 *
 * Registers a new operation in the 4D tree. If operation already exists,
 * it is replaced if priority is higher.
 *
 * @param registry       Operation registry
 * @param desc           Operation descriptor (copied into registry)
 * @param type_registry  Type registry for resolving type GUIDs
 * @return NMO_OK on success, error code on failure
 *
 * @note
 * - desc is copied, caller retains ownership
 * - Type GUIDs must exist in type_registry
 * - Function pointer must be valid for the lifetime of the registry
 */
NMO_API nmo_status_t nmo_operation_registry_register(
    nmo_operation_registry_t *registry,
    const nmo_operation_desc_t *desc,
    const nmo_type_registry_t *type_registry
);

/**
 * @brief Register multiple operations (bulk)
 *
 * Efficiently registers multiple operations at once.
 * Sorts arrays once at the end instead of per-operation.
 *
 * @param registry       Operation registry
 * @param descs          Array of operation descriptors
 * @param count          Number of descriptors
 * @param type_registry  Type registry for resolving type GUIDs
 * @param logger         Optional logger for error reporting (can be NULL)
 * @return NMO_OK on success, error code on failure
 *
 * @note
 * - Continues registration on errors, logging failures if logger provided
 * - Returns error only if no operations successfully registered
 */
NMO_API nmo_status_t nmo_operation_registry_register_bulk(
    nmo_operation_registry_t *registry,
    const nmo_operation_desc_t *descs,
    uint32_t count,
    const nmo_type_registry_t *type_registry,
    nmo_logger_t *logger
);

/* ============================================================================
 * Operation Lookup
 * ============================================================================ */

/**
 * @brief Find operation
 *
 * Finds an operation in the 4D tree. Supports type inheritance matching:
 * if exact match not found, searches for compatible types up the inheritance chain.
 *
 * @param registry       Operation registry
 * @param operation_guid Operation family GUID
 * @param p1_type        Parameter 1 type descriptor
 * @param p2_type        Parameter 2 type descriptor (NULL for unary)
 * @param type_registry  Type registry for inheritance matching (optional, can be NULL)
 * @param out_cell       Output cell pointer (set to NULL if not found)
 * @return NMO_OK if found, NMO_ERROR_NOT_FOUND if no match
 * @note Returned cell is registry-owned; do not free.
 *
 * @note
 * - Searches exact match first, then compatible types (if type_registry provided)
 * - Returns highest priority match if multiple matches exist
 * - Increments call_count in matched cell
 */
NMO_API nmo_status_t nmo_operation_registry_find(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const nmo_type_descriptor_t *p1_type,
    const nmo_type_descriptor_t *p2_type,
    const nmo_type_registry_t *type_registry,
    const nmo_operation_tree_cell_t **out_cell
);

/**
 * @brief Find operation with required result type
 *
 * Like nmo_operation_registry_find(), but requires the selected cell to match
 * the requested result type GUID. This is the correct primitive to use when
 * the caller already knows the expected result type (e.g. execution).
 *
 * @param registry       Operation registry
 * @param operation_guid Operation family GUID
 * @param p1_type        Parameter 1 type descriptor
 * @param p2_type        Parameter 2 type descriptor (NULL for unary)
 * @param result_type    Required result type descriptor (must not be NULL)
 * @param type_registry  Type registry for inheritance matching (optional, can be NULL)
 * @param out_cell       Output cell pointer (set to NULL if not found)
 * @return NMO_OK if found, NMO_ERR_NOT_FOUND if no match
 * @note Returned cell is registry-owned; do not free.
 */
NMO_API nmo_status_t nmo_operation_registry_find_typed(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const nmo_type_descriptor_t *p1_type,
    const nmo_type_descriptor_t *p2_type,
    const nmo_type_descriptor_t *result_type,
    const nmo_type_registry_t *type_registry,
    const nmo_operation_tree_cell_t **out_cell
);

/**
 * @brief Execute operation
 *
 * Convenience function: finds operation and executes it in one call.
 *
 * @param registry       Operation registry
 * @param operation_guid Operation family GUID
 * @param p1_data        Parameter 1 data
 * @param p1_type        Parameter 1 type descriptor
 * @param p2_data        Parameter 2 data (NULL for unary)
 * @param p2_type        Parameter 2 type descriptor (NULL for unary)
 * @param result_data    Result buffer (pre-allocated)
 * @param result_type    Expected result type descriptor
 * @param type_registry  Type registry for inheritance matching (optional, can be NULL)
 * @return NMO_OK on success, error code on failure
 * @note Does not allocate; caller provides result buffer.
 */
NMO_API nmo_status_t nmo_operation_registry_execute(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const void *p1_data,
    const nmo_type_descriptor_t *p1_type,
    const void *p2_data,
    const nmo_type_descriptor_t *p2_type,
    void *result_data,
    const nmo_type_descriptor_t *result_type,
    const nmo_type_registry_t *type_registry
);

/* ============================================================================
 * Query and Enumeration
 * ============================================================================ */

/**
 * @brief Get operation family by GUID
 *
 * @param registry       Operation registry
 * @param operation_guid Operation family GUID
 * @return Family pointer or NULL if not found
 * @note Returned pointer is registry-owned; do not free.
 */
NMO_API const nmo_operation_family_t *nmo_operation_registry_get_family(
    const nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid
);

/**
 * @brief Enumerate all operations in family
 *
 * @param family    Operation family
 * @param callback  Callback for each operation
 * @param user_data User data for callback
 * @return NMO_OK on success, error code on failure
 */
typedef nmo_status_t (*nmo_operation_enum_fn)(
    const nmo_operation_tree_cell_t *cell,
    void *user_data
);

NMO_API nmo_status_t nmo_operation_family_enumerate(
    const nmo_operation_family_t *family,
    nmo_operation_enum_fn callback,
    void *user_data
);

/* ============================================================================
 * Statistics and Debugging
 * ============================================================================ */

/**
 * @brief Get registry statistics
 *
 * @param registry Operation registry
 * @param out_total_operations  Total number of registered operations
 * @param out_total_lookups     Total lookup count
 * @param out_cache_hits        Cache hit count
 */
NMO_API void nmo_operation_registry_get_stats(
    const nmo_operation_registry_t *registry,
    uint64_t *out_total_operations,
    uint64_t *out_total_lookups,
    uint64_t *out_cache_hits
);

/**
 * @brief Print registry structure (debug)
 *
 * Prints the entire 4D tree structure to logger.
 *
 * @param registry Operation registry
 * @param logger   Logger for output
 */
NMO_API void nmo_operation_registry_debug_print(
    const nmo_operation_registry_t *registry,
    nmo_logger_t *logger
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OPERATION_SYSTEM_H */
