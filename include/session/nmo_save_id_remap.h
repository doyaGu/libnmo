/**
 * @file nmo_save_id_remap.h
 * @brief Save-time runtime ID to file ID remap planning.
 */

#ifndef NMO_SESSION_SAVE_ID_REMAP_H
#define NMO_SESSION_SAVE_ID_REMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "format/nmo_id_remap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_save_id_remap_plan nmo_save_id_remap_plan_t;

/**
 * @brief Create a save-time remap plan.
 *
 * Builds a runtime ID -> file ID table for the objects being saved. Existing
 * nonzero file IDs are preserved, and gaps are filled for new objects.
 *
 * @ownership owned
 */
NMO_API nmo_save_id_remap_plan_t *nmo_save_id_remap_plan_create(
    nmo_object_repository_t *repo,
    nmo_object_t **objects_to_save,
    size_t object_count);

/**
 * @brief Get the runtime ID -> file ID table from a save remap plan.
 *
 * @ownership borrowed
 */
NMO_API nmo_id_remap_t *nmo_save_id_remap_plan_get_table(
    const nmo_save_id_remap_plan_t *plan);

/**
 * @brief Get the number of objects remapped by the plan.
 */
NMO_API size_t nmo_save_id_remap_plan_get_remapped_count(
    const nmo_save_id_remap_plan_t *plan);

/**
 * @brief Destroy a save remap plan.
 */
NMO_API void nmo_save_id_remap_plan_destroy(nmo_save_id_remap_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_SAVE_ID_REMAP_H */
