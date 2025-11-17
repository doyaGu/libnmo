/**
 * @file nmo_param_type_table.h
 * @brief Parameter type table for GUID-based type lookup
 *
 * This module provides a mapping from Parameter GUIDs (CKPGUID) to schema types.
 * It enables efficient lookup of type information by GUID, supporting the Virtools
 * Parameter system's GUID-based type resolution.
 *
 * The table is built from a schema registry by extracting all types with param_meta.
 */

#ifndef NMO_PARAM_TYPE_TABLE_H
#define NMO_PARAM_TYPE_TABLE_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nmo_schema_type;
struct nmo_schema_registry;
struct nmo_arena;

/* =============================================================================
 * PARAMETER TYPE TABLE
 * ============================================================================= */

/**
 * @brief Parameter type table for GUID-based lookup
 *
 * Maps Parameter GUIDs to schema types. Built from a registry by extracting
 * all types that have param_meta attached.
 *
 * Typical workflow:
 *   1. Register types with param_meta using nmo_register_param_types()
 *   2. Build table with nmo_param_type_table_build()
 *   3. Query types with nmo_param_type_table_find()
 */
typedef struct nmo_param_type_table {
    nmo_hash_table_t *guid_to_type_map; /**< GUID (hash) → schema_type */
    struct nmo_arena *arena;            /**< Arena owning the table */
    size_t type_count;                  /**< Number of parameter types */
} nmo_param_type_table_t;

/* =============================================================================
 * TABLE CONSTRUCTION
 * ============================================================================= */

/**
 * @brief Build parameter type table from schema registry
 *
 * Iterates through all types in the registry and adds those with param_meta
 * to a GUID-indexed hash table. Uses nmo_guid_hash() for hashing.
 *
 * @param registry Source schema registry
 * @param arena Arena for table allocations
 * @param out_table Output table pointer (allocated in arena)
 * @return NMO_OK on success, error otherwise
 *
 * @note The table lifetime is tied to the arena. The registry must remain
 *       valid as long as the table is used (types are not copied).
 */
NMO_API nmo_result_t nmo_param_type_table_build(
    const struct nmo_schema_registry *registry,
    struct nmo_arena *arena,
    nmo_param_type_table_t **out_table);

/* =============================================================================
 * TABLE QUERIES
 * ============================================================================= */

/**
 * @brief Find schema type by Parameter GUID
 *
 * O(1) average case hash table lookup.
 *
 * @param table Parameter type table
 * @param guid Parameter GUID (CKPGUID)
 * @return Schema type pointer, or NULL if not found
 */
NMO_API const struct nmo_schema_type *nmo_param_type_table_find(
    const nmo_param_type_table_t *table,
    nmo_guid_t guid);

/**
 * @brief Get number of parameter types in table
 *
 * @param table Parameter type table
 * @return Number of types (0 if table is NULL)
 */
NMO_API size_t nmo_param_type_table_get_count(
    const nmo_param_type_table_t *table);

/**
 * @brief Check if GUID exists in table
 *
 * @param table Parameter type table
 * @param guid Parameter GUID
 * @return 1 if exists, 0 otherwise
 */
NMO_API int nmo_param_type_table_contains(
    const nmo_param_type_table_t *table,
    nmo_guid_t guid);

/* =============================================================================
 * ITERATION
 * ============================================================================= */

/**
 * @brief Iterator callback for parameter types
 *
 * @param guid Parameter GUID
 * @param type Schema type
 * @param userdata User-provided data
 * @return 1 to continue iteration, 0 to stop
 */
typedef int (*nmo_param_type_iterator_fn)(
    nmo_guid_t guid,
    const struct nmo_schema_type *type,
    void *userdata);

/**
 * @brief Iterate over all parameter types
 *
 * @param table Parameter type table
 * @param callback Iterator callback
 * @param userdata User data passed to callback
 */
NMO_API void nmo_param_type_table_iterate(
    const nmo_param_type_table_t *table,
    nmo_param_type_iterator_fn callback,
    void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_TYPE_TABLE_H */
