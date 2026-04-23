#ifndef NMO_OBJECT_HIERARCHY_OWNER_H
#define NMO_OBJECT_HIERARCHY_OWNER_H

#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>

#define NMO_OBJECT_HIERARCHY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_HIERARCHY_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

typedef struct nmo_object_hierarchy {
    nmo_object_id_t *parent_of;
    size_t map_size;
    size_t root_count;
    size_t object_count;
} nmo_object_hierarchy_t;

NMO_API bool nmo_object_hierarchy_build(nmo_context_t *ctx,
                                        nmo_session_t *session,
                                        nmo_object_hierarchy_t *out_hierarchy);

NMO_API void nmo_object_hierarchy_free(nmo_object_hierarchy_t *hierarchy);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_HIERARCHY_OWNER_H */
