/**
 * @file nmo_object_hierarchy.h
 * @brief Ownership-oriented object hierarchy builder.
 */

#ifndef NMO_OBJECT_HIERARCHY_H
#define NMO_OBJECT_HIERARCHY_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_object_hierarchy {
    nmo_object_id_t *parent_of; /* parent_of[child_id] = parent_id, 0 means root */
    size_t map_size;
    size_t root_count;
    size_t object_count;
} nmo_object_hierarchy_t;

/**
 * @brief Build object ownership hierarchy by enumerating reference fields.
 */
NMO_API bool nmo_object_hierarchy_build(nmo_context_t *ctx,
                                        nmo_session_t *session,
                                        nmo_object_hierarchy_t *out_hierarchy);

NMO_API void nmo_object_hierarchy_free(nmo_object_hierarchy_t *hierarchy);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_HIERARCHY_H */
