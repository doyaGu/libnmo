/**
 * @file nmo_behavior_edit.h
 * @brief Behavior graph mutation APIs
 */
#ifndef NMO_BEHAVIOR_EDIT_H
#define NMO_BEHAVIOR_EDIT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;

/**
 * @brief Find a parameter in a behavior by name.
 *
 * Searches in_parameters, out_parameters, and local_parameters arrays.
 *
 * @param repo      Object repository
 * @param behavior  Behavior object
 * @param name      Parameter name to find
 * @return Parameter object or NULL if not found
 */
NMO_API nmo_object_t *nmo_behavior_find_parameter(
    nmo_object_repository_t *repo,
    nmo_object_t *behavior,
    const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_EDIT_H */
