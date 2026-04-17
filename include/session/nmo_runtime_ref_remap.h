#ifndef NMO_SESSION_RUNTIME_REF_REMAP_H
#define NMO_SESSION_RUNTIME_REF_REMAP_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_runtime nmo_type_runtime_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_id_remap nmo_id_remap_t;
typedef struct nmo_object_repository nmo_object_repository_t;

/**
 * @brief Remap object ID references in a single object's state.
 *
 * Walks the type hierarchy of @p type, visiting every ref-typed field
 * in @p instance and its base layers. Each ID that appears in @p remap
 * is replaced with the mapped value. Used after copy to redirect
 * cloned objects' references to their cloned counterparts.
 *
 * @param type_rt  Type runtime (registry aggregate)
 * @param type     Type descriptor for the object
 * @param instance Mutable object state pointer
 * @param remap    ID remap table (old -> new)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_runtime_remap_copy_refs(
    const nmo_type_runtime_t *type_rt,
    const nmo_type_descriptor_t *type,
    void *instance,
    const nmo_id_remap_t *remap);

/**
 * @brief Remap dependencies on all objects in the repository.
 *
 * Iterates every object in @p repo and invokes its
 * vtable->remap_dependencies hook. Used after delete with
 * NMO_RUNTIME_REQUEST_SAFE_DETACH to clean up dangling references.
 *
 * @param repo          Object repository
 * @param type_rt       Type runtime
 * @param request_flags Runtime request flags (STRICT controls error behavior)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_runtime_remap_all_refs(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    uint32_t request_flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_RUNTIME_REF_REMAP_H */
