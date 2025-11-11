/**
 * @file nmo_id_remap.h
 * @brief ID remapping for object ID translation during load and save
 */

#ifndef NMO_ID_REMAP_H
#define NMO_ID_REMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_load_session nmo_load_session;
typedef struct nmo_object_repository nmo_object_repository;
typedef struct nmo_object nmo_object;

/**
 * @brief ID remapping entry
 */
typedef struct {
    nmo_object_id old_id;
    nmo_object_id new_id;
} nmo_id_remap_entry;

/**
 * @brief ID remap table for load operations
 *
 * Maps file IDs to runtime IDs. Used during deserialization to translate
 * object references from file format to runtime format.
 */
typedef struct nmo_id_remap_table nmo_id_remap_table;

/**
 * @brief Build ID remap table from load session
 *
 * Creates a remap table that maps file IDs to runtime IDs based on
 * the mappings recorded during load session.
 *
 * @param session Load session containing ID mappings
 * @return Remap table or NULL on error
 */
NMO_API nmo_id_remap_table* nmo_build_remap_table(nmo_load_session* session);

/**
 * @brief Lookup remapped ID
 *
 * @param table Remap table
 * @param old_id Original object ID
 * @param new_id Output for remapped ID
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if not found
 */
NMO_API int nmo_id_remap_lookup(const nmo_id_remap_table* table,
                                 nmo_object_id old_id,
                                 nmo_object_id* new_id);

/**
 * @brief Get entry count
 *
 * @param table Remap table
 * @return Number of entries
 */
NMO_API size_t nmo_id_remap_table_get_count(const nmo_id_remap_table* table);

/**
 * @brief Get entry at index
 *
 * @param table Remap table
 * @param index Entry index
 * @return Entry or NULL if out of bounds
 */
NMO_API const nmo_id_remap_entry* nmo_id_remap_table_get_entry(
    const nmo_id_remap_table* table, size_t index);

/**
 * @brief Destroy remap table
 *
 * @param table Remap table to destroy
 */
NMO_API void nmo_id_remap_table_destroy(nmo_id_remap_table* table);

/**
 * @brief ID remap plan for save operations
 *
 * Creates a plan for remapping runtime IDs to sequential file IDs (0-based).
 * Handles object selection and conflict resolution.
 */
typedef struct nmo_id_remap_plan nmo_id_remap_plan;

/**
 * @brief Create ID remap plan for save
 *
 * Creates a plan that maps runtime IDs to sequential file IDs for the
 * objects being saved. File IDs start from 0 and increment sequentially.
 *
 * @param repo Object repository
 * @param objects_to_save Array of objects to save
 * @param object_count Number of objects
 * @return Remap plan or NULL on error
 */
NMO_API nmo_id_remap_plan* nmo_id_remap_plan_create(nmo_object_repository* repo,
                                                      nmo_object** objects_to_save,
                                                      size_t object_count);

/**
 * @brief Lookup remapped ID from plan
 *
 * @param plan Remap plan
 * @param runtime_id Runtime object ID
 * @param file_id Output for file ID
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if not found
 */
NMO_API int nmo_id_remap_plan_lookup(const nmo_id_remap_plan* plan,
                                      nmo_object_id runtime_id,
                                      nmo_object_id* file_id);

/**
 * @brief Get remap table from plan
 *
 * @param plan Remap plan
 * @return Remap table
 */
NMO_API nmo_id_remap_table* nmo_id_remap_plan_get_table(const nmo_id_remap_plan* plan);

/**
 * @brief Get objects remapped count
 *
 * @param plan Remap plan
 * @return Number of objects remapped
 */
NMO_API size_t nmo_id_remap_plan_get_remapped_count(const nmo_id_remap_plan* plan);

/**
 * @brief Get conflicts resolved count
 *
 * @param plan Remap plan
 * @return Number of conflicts resolved
 */
NMO_API size_t nmo_id_remap_plan_get_conflicts_resolved(const nmo_id_remap_plan* plan);

/**
 * @brief Destroy remap plan
 *
 * @param plan Remap plan to destroy
 */
NMO_API void nmo_id_remap_plan_destroy(nmo_id_remap_plan* plan);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ID_REMAP_H */
