/**
 * @file nmo_id_remap.h
 * @brief ID remapping compatibility layer for parser.c
 * 
 * This file provides a compatibility layer between the old session-level
 * ID remap API (used by parser.c) and the new format-level nmo_id_remap_t API.
 */

#ifndef NMO_SESSION_ID_REMAP_H
#define NMO_SESSION_ID_REMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "format/nmo_id_remap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_id_mapping nmo_id_mapping_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object nmo_object_t;

/**
 * @brief ID remap table (compatibility wrapper around nmo_id_remap_t)
 * 
 * Used during load operations to map file object indices (SaveFindObjectIndex)
 * to runtime IDs for chunk ID remapping.
 */
typedef nmo_id_remap_t nmo_id_remap_table_t;

/**
 * @brief ID remap plan for save operations
 * 
 * Creates a plan that maps runtime IDs to sequential file IDs.
 */
typedef struct nmo_id_remap_plan nmo_id_remap_plan_t;

/* ============================================================================
 * Load-time ID Remapping (file object index -> runtime ID)
 * ============================================================================ */

/**
 * @brief Build ID remap table from load session
 * 
 * Creates a remap table that maps file object indices (SaveFindObjectIndex, 0-based)
 * to runtime IDs based on the mappings recorded during the load session.
 * 
 * @param mapping ID mapping containing file_index → runtime_id entries
 * @return Remap table or NULL on error
 * @note Returned table is caller-owned; destroy via nmo_id_remap_table_destroy().
 * @ownership owned
 */
NMO_API nmo_id_remap_table_t *nmo_build_remap_table(nmo_id_mapping_t *mapping);

/**
 * @brief Lookup remapped ID
 * 
 * @param table Remap table
 * @param old_id Original ID (file object index during load, runtime ID during save)
 * @param new_id Output for remapped ID
 * @return NMO_OK on success, NMO_ERR_NOT_FOUND if not found
 */
NMO_API int nmo_id_remap_lookup(const nmo_id_remap_table_t *table,
                                nmo_object_id_t old_id,
                                nmo_object_id_t *new_id);

/**
 * @brief Get entry count in remap table
 * 
 * @param table Remap table
 * @return Number of entries
 */
NMO_API size_t nmo_id_remap_table_get_count(const nmo_id_remap_table_t *table);

/**
 * @brief Destroy remap table
 *
 * Destroys the table created by nmo_build_remap_table(), including the
 * internal arena used for its storage.
 *
 * @param table Remap table to destroy
 */
NMO_API void nmo_id_remap_table_destroy(nmo_id_remap_table_t *table);

/* ============================================================================
 * Save-time ID Remapping (runtime ID -> sequential file ID)
 * ============================================================================ */

/**
 * @brief Create ID remap plan for save operations
 * 
 * Creates a plan that maps runtime IDs to sequential file IDs (starting from 1)
 * for the objects being saved.
 *
 * File ID 0 is reserved to mean "no file ID".
 * 
 * @param repo Object repository
 * @param objects_to_save Array of objects to save
 * @param object_count Number of objects
 * @return Remap plan or NULL on error
 * @note Returned plan is caller-owned; destroy via nmo_id_remap_plan_destroy().
 * @ownership owned
 */
NMO_API nmo_id_remap_plan_t *nmo_id_remap_plan_create(nmo_object_repository_t *repo,
                                                    nmo_object_t **objects_to_save,
                                                    size_t object_count);

/**
 * @brief Get remap table from plan
 * 
 * @param plan Remap plan
 * @return Remap table (runtime ID -> file ID)
 * @note Returned pointer is plan-owned; valid until plan destruction.
 * @ownership borrowed
 */
NMO_API nmo_id_remap_table_t *nmo_id_remap_plan_get_table(const nmo_id_remap_plan_t *plan);

/**
 * @brief Get objects remapped count
 * 
 * @param plan Remap plan
 * @return Number of objects remapped
 */
NMO_API size_t nmo_id_remap_plan_get_remapped_count(const nmo_id_remap_plan_t *plan);

/**
 * @brief Destroy remap plan
 * 
 * @param plan Remap plan to destroy
 */
NMO_API void nmo_id_remap_plan_destroy(nmo_id_remap_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_ID_REMAP_H */
