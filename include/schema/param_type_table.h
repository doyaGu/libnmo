/**
 * @file param_type_table.h
 * @brief Parameter type table for GUID-based type lookup
 *
 * This module provides a GUID -> schema type mapping table for Parameter system.
 * Enables fast lookup of parameter types by their CKPGUID values.
 */

#ifndef NMO_PARAM_TYPE_TABLE_H
#define NMO_PARAM_TYPE_TABLE_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_hash_table.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nmo_schema_registry;
struct nmo_schema_type;
struct nmo_arena;

/* =============================================================================
 * PARAMETER TYPE TABLE
 * ============================================================================= */

/**
 * @brief Parameter type table structure
 *
 * Maps CKPGUID values to schema types for fast Parameter type lookup.
 * Built from schema registry containing param_meta information.
 */
typedef struct nmo_param_type_table {
    nmo_hash_table_t *guid_to_type_map;  /**< GUID -> nmo_schema_type_t* */
    size_t type_count;                    /**< Total number of parameter types */
} nmo_param_type_table_t;

/**
 * @brief Parameter type table statistics
 */
typedef struct nmo_param_type_table_stats {
    size_t total_types;      /**< Total parameter types */
    size_t scalar_count;     /**< Scalar parameter types */
    size_t struct_count;     /**< Struct parameter types */
    size_t enum_count;       /**< Enum parameter types */
    size_t object_ref_count; /**< Object reference types */
} nmo_param_type_table_stats_t;

/* =============================================================================
 * TABLE CONSTRUCTION
 * ============================================================================= */

/**
 * @brief Build parameter type table from schema registry
 *
 * Iterates all types in registry and collects those with param_meta.
 * Creates GUID -> type mapping for fast lookup.
 *
 * @param registry Schema registry containing types
 * @param arena Arena for allocations
 * @param out_table Output parameter for table pointer
 * @return NMO_OK on success, error otherwise
 */
NMO_API nmo_result_t nmo_param_type_table_build(
    const struct nmo_schema_registry *registry,
    struct nmo_arena *arena,
    nmo_param_type_table_t **out_table);

/* =============================================================================
 * TABLE LOOKUP
 * ============================================================================= */

/**
 * @brief Find parameter type by GUID
 *
 * @param table Type table
 * @param guid Parameter GUID (CKPGUID)
 * @return Type pointer or NULL if not found
 */
NMO_API const struct nmo_schema_type *nmo_param_type_table_find_by_guid(
    const nmo_param_type_table_t *table,
    nmo_guid_t guid);

/* =============================================================================
 * TABLE STATISTICS
 * ============================================================================= */

/**
 * @brief Get parameter type table statistics
 *
 * @param table Type table
 * @param out_stats Output statistics structure
 */
NMO_API void nmo_param_type_table_get_stats(
    const nmo_param_type_table_t *table,
    nmo_param_type_table_stats_t *out_stats);

/* =============================================================================
 * Phase 5 Improvements: Type Compatibility and Validation
 * ============================================================================= */

/**
 * @brief Check if one parameter type is compatible with another
 * 
 * A type is compatible if:
 * - GUIDs match exactly, OR
 * - type is derived from expected (directly or through inheritance chain)
 * 
 * @param table Parameter type table
 * @param type_guid GUID of the type to check
 * @param expected_guid GUID of the expected type
 * @return true if compatible, false otherwise
 */
NMO_API bool nmo_param_type_is_compatible(
    const nmo_param_type_table_t *table,
    nmo_guid_t type_guid,
    nmo_guid_t expected_guid);

/**
 * @brief Get the inheritance depth of a parameter type
 * 
 * Returns 0 for base types (no derived_from), 1 for direct derivatives,
 * 2 for second-level derivatives, etc.
 * 
 * @param table Parameter type table
 * @param type_guid GUID of the type
 * @return Inheritance depth, or -1 if type not found or circular dependency detected
 */
NMO_API int nmo_param_type_get_depth(
    const nmo_param_type_table_t *table,
    nmo_guid_t type_guid);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_TYPE_TABLE_H */
