#ifndef NMO_RUNTIME_KERNEL_INTERNAL_H
#define NMO_RUNTIME_KERNEL_INTERNAL_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_runtime nmo_type_runtime_t;
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Preview which objects would be deleted without actually deleting.
 *
 * Collects the expanded delete set (including cascade if flags contain
 * NMO_RUNTIME_REQUEST_CASCADE) and returns the IDs via out parameters.
 * No objects are modified or removed.
 *
 * @param repo       Object repository
 * @param type_rt    Type runtime (may be NULL if no cascade needed)
 * @param arena      Arena for temporary allocations
 * @param object_ids Array of object IDs to delete
 * @param object_count Number of IDs
 * @param flags      Runtime request flags (e.g. NMO_RUNTIME_REQUEST_CASCADE)
 * @param out_ids    Output: arena-allocated array of expanded IDs
 * @param out_count  Output: number of expanded IDs
 * @return NMO_OK on success
 */
int runtime_kernel_preview_delete(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_object_id_t **out_ids,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_RUNTIME_KERNEL_INTERNAL_H */
