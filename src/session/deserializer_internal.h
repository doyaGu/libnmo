/**
 * @file deserializer_internal.h
 * @brief Internal ID-remapping API for session-layer use only
 *
 * These functions are NOT part of the public API. They are used by
 * object_system.c and id_remap.c during the load pipeline.
 */

#ifndef NMO_DESERIALIZER_INTERNAL_H
#define NMO_DESERIALIZER_INTERNAL_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_deserializer nmo_deserializer_t;

/**
 * @brief Start an ID-remapping session (internal)
 *
 * Allocates a lightweight deserializer context for mapping file object
 * indices to runtime IDs during load.
 *
 * @param repo  Object repository
 * @param max_saved_id  Maximum saved ID from file header
 * @return Deserializer context, or NULL on error
 */
nmo_deserializer_t *nmo_deserializer_start(
    nmo_object_repository_t *repo,
    nmo_object_id_t max_saved_id);

/**
 * @brief Register a file_index -> runtime_id mapping
 */
int nmo_deserializer_register(nmo_deserializer_t *session,
                              nmo_object_t *obj,
                              nmo_object_id_t file_index);

/**
 * @brief End the remapping session (marks inactive)
 */
int nmo_deserializer_end(nmo_deserializer_t *session);

/**
 * @brief Look up a runtime ID by file index
 */
int nmo_deserializer_get_runtime_id(const nmo_deserializer_t *session,
                                    nmo_object_id_t file_index,
                                    nmo_object_id_t *out_runtime_id);

/**
 * @brief Get the object repository from the session
 */
nmo_object_repository_t *nmo_deserializer_get_repository(
    const nmo_deserializer_t *session);

/**
 * @brief Get the ID base (first available runtime ID)
 */
nmo_object_id_t nmo_deserializer_get_id_base(
    const nmo_deserializer_t *session);

/**
 * @brief Get the maximum saved ID from the file
 */
nmo_object_id_t nmo_deserializer_get_max_saved_id(
    const nmo_deserializer_t *session);

/**
 * @brief Destroy a legacy (ID-remapping only) deserializer
 */
void nmo_deserializer_destroy_legacy(nmo_deserializer_t *session);

/**
 * @brief Get all ID mappings (used by id_remap.c)
 */
int nmo_load_session_get_mappings(const nmo_deserializer_t *session,
                                  nmo_object_id_t **file_ids,
                                  nmo_object_id_t **runtime_ids,
                                  size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DESERIALIZER_INTERNAL_H */
