/**
 * @file nmo_id_remap.h
 * @brief ID remapping for object ID translation
 */

#ifndef NMO_ID_REMAP_H
#define NMO_ID_REMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ID remapper
 */
typedef struct nmo_id_remap nmo_id_remap_t;

/**
 * Create ID remapper
 * @param initial_capacity Initial mapping capacity
 * @return Remapper or NULL on error
 */
NMO_API nmo_id_remap_t* nmo_id_remap_create(size_t initial_capacity);

/**
 * Destroy ID remapper
 * @param remapper Remapper to destroy
 */
NMO_API void nmo_id_remap_destroy(nmo_id_remap_t* remapper);

/**
 * Add ID mapping
 * @param remapper Remapper
 * @param old_id Old object ID
 * @param new_id New object ID
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_id_remap_add_mapping(nmo_id_remap_t* remapper, uint32_t old_id, uint32_t new_id);

/**
 * Get mapped ID
 * @param remapper Remapper
 * @param old_id Old object ID
 * @param out_new_id Output new object ID
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_id_remap_get_mapping(const nmo_id_remap_t* remapper, uint32_t old_id, uint32_t* out_new_id);

/**
 * Check if ID mapping exists
 * @param remapper Remapper
 * @param old_id Old object ID
 * @return 1 if mapping exists, 0 otherwise
 */
NMO_API int nmo_id_remap_has_mapping(const nmo_id_remap_t* remapper, uint32_t old_id);

/**
 * Remove ID mapping
 * @param remapper Remapper
 * @param old_id Old object ID
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_id_remap_remove_mapping(nmo_id_remap_t* remapper, uint32_t old_id);

/**
 * Get mapping count
 * @param remapper Remapper
 * @return Number of mappings
 */
NMO_API uint32_t nmo_id_remap_get_count(const nmo_id_remap_t* remapper);

/**
 * Get old ID at index
 * @param remapper Remapper
 * @param index Index
 * @return Old object ID or 0 if index out of bounds
 */
NMO_API uint32_t nmo_id_remap_get_old_id_at(const nmo_id_remap_t* remapper, uint32_t index);

/**
 * Clear all mappings
 * @param remapper Remapper
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_id_remap_clear(nmo_id_remap_t* remapper);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ID_REMAP_H */
