#ifndef NMO_OBJECT_SCAN_INTERNAL_H
#define NMO_OBJECT_SCAN_INTERNAL_H

#include "object/nmo_object_repository.h"

typedef nmo_status_t (*nmo_object_scan_fn)(
    size_t object_index,
    nmo_object_t *object,
    void *user_data);

static inline nmo_status_t nmo_object_scan_repository(
    const nmo_object_repository_t *repository,
    nmo_object_scan_fn visitor,
    void *user_data,
    size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (repository == NULL || visitor == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t count = nmo_object_repository_get_count(repository);
    if (out_count != NULL) {
        *out_count = count;
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
        if (object == NULL) {
            continue;
        }
        nmo_status_t status = visitor(i, object, user_data);
        if (status != NMO_OK) {
            return status;
        }
    }

    return NMO_OK;
}

#endif /* NMO_OBJECT_SCAN_INTERNAL_H */
