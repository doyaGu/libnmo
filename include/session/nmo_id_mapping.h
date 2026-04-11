/**
 * @file nmo_id_mapping.h
 * @brief File-index to runtime-ID mapping for load pipelines
 *
 * Tracks the mapping from file object table indices (0-based) to
 * runtime IDs assigned during object creation. Used by the deserializer
 * and the ID remap table builder.
 */

#ifndef NMO_ID_MAPPING_H
#define NMO_ID_MAPPING_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_id_mapping nmo_id_mapping_t;

/**
 * @brief Create an ID mapping session
 *
 * @param repo Object repository (for determining ID base)
 * @param max_saved_id Maximum object ID from the file being loaded
 * @return ID mapping context, or NULL on error
 * @ownership owned
 */
NMO_API nmo_id_mapping_t *nmo_id_mapping_create(
    nmo_object_repository_t *repo,
    nmo_object_id_t max_saved_id);

/**
 * @brief Register a file_index → runtime_id mapping
 */
NMO_API int nmo_id_mapping_register(nmo_id_mapping_t *mapping,
                                    nmo_object_t *obj,
                                    nmo_object_id_t file_index);

/**
 * @brief Mark the mapping session as complete
 */
NMO_API int nmo_id_mapping_end(nmo_id_mapping_t *mapping);

/**
 * @brief Look up runtime_id by file_index
 */
NMO_API int nmo_id_mapping_get_runtime_id(const nmo_id_mapping_t *mapping,
                                          nmo_object_id_t file_index,
                                          nmo_object_id_t *out_runtime_id);

/**
 * @brief Get the ID base (first allocated runtime ID)
 */
NMO_API nmo_object_id_t nmo_id_mapping_get_id_base(
    const nmo_id_mapping_t *mapping);

/**
 * @brief Get the maximum saved ID from the file
 */
NMO_API nmo_object_id_t nmo_id_mapping_get_max_saved_id(
    const nmo_id_mapping_t *mapping);

/**
 * @brief Get all mappings as parallel arrays
 *
 * @param mapping ID mapping context
 * @param file_ids Output array of file indices (arena-allocated)
 * @param runtime_ids Output array of runtime IDs (arena-allocated)
 * @param count Output number of mappings
 * @return NMO_OK on success
 */
NMO_API int nmo_id_mapping_get_all(const nmo_id_mapping_t *mapping,
                                   nmo_object_id_t **file_ids,
                                   nmo_object_id_t **runtime_ids,
                                   size_t *count);

/**
 * @brief Destroy the ID mapping
 */
NMO_API void nmo_id_mapping_destroy(nmo_id_mapping_t *mapping);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ID_MAPPING_H */
