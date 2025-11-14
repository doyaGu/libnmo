/**
 * @file nmo_id_remap.h
 * @brief ID remapping for NeMo chunks
 * 
 * This module provides object ID remapping functionality for chunks,
 * allowing transformation of object IDs when loading files.
 */

#ifndef NMO_ID_REMAP_H
#define NMO_ID_REMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ID remapping entry
 * 
 * Maps an old (file) ID to a new (runtime) ID.
 */
typedef struct nmo_id_remap_entry {
    nmo_object_id_t old_id; /**< Original ID from file */
    nmo_object_id_t new_id; /**< New ID in runtime */
} nmo_id_remap_entry_t;

/**
 * @brief ID remapping table
 * 
 * Provides a mapping from old object IDs to new object IDs.
 * Used when loading files to translate file-local IDs to runtime IDs.
 */
typedef struct nmo_id_remap {
    nmo_id_remap_entry_t *entries; /**< Array of remap entries */
    size_t count;                  /**< Number of entries */
    size_t capacity;               /**< Allocated capacity */
    nmo_arena_t *arena;            /**< Memory arena for allocations */
} nmo_id_remap_t;

/**
 * @brief Create an ID remap table
 * 
 * @param arena Memory arena for allocations
 * @return Pointer to new remap table, or NULL on failure
 */
nmo_id_remap_t *nmo_id_remap_create(nmo_arena_t *arena);

/**
 * @brief Destroy an ID remap table
 * 
 * @param remap Remap table to destroy
 */
void nmo_id_remap_destroy(nmo_id_remap_t *remap);

/**
 * @brief Add an ID mapping
 * 
 * @param remap Remap table
 * @param old_id Original ID
 * @param new_id New ID
 * @return NMO_OK on success, error code on failure
 */
nmo_result_t nmo_id_remap_add(nmo_id_remap_t *remap, nmo_object_id_t old_id, nmo_object_id_t new_id);

/**
 * @brief Look up a new ID for an old ID
 * 
 * @param remap Remap table
 * @param old_id Original ID to look up
 * @param out_new_id Output for new ID (only set if found)
 * @return NMO_OK if mapping found, NMO_ERR_NOT_FOUND if not found
 */
nmo_result_t nmo_id_remap_lookup_id(const nmo_id_remap_t *remap, nmo_object_id_t old_id, nmo_object_id_t *out_new_id);

/**
 * @brief Get the number of mappings in the table
 * 
 * @param remap Remap table
 * @return Number of mappings
 */
size_t nmo_id_remap_get_count(const nmo_id_remap_t *remap);

/**
 * @brief Clear all mappings
 * 
 * @param remap Remap table to clear
 */
void nmo_id_remap_clear(nmo_id_remap_t *remap);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ID_REMAP_H */
