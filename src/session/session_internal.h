#ifndef NMO_SESSION_INTERNAL_H
#define NMO_SESSION_INTERNAL_H

#include "session/nmo_session.h"
#include "format/nmo_object.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_runtime.h"

void nmo_session_internal_set_partial_load(nmo_session_t *session, int partial);

/**
 * @brief Find the effective registered type descriptor for an object.
 */
static inline const nmo_type_descriptor_t *runtime_find_type_for_object(
    const nmo_type_runtime_t *type_rt,
    const nmo_object_t *object)
{
    if (type_rt == NULL || type_rt->types == NULL || object == NULL) {
        return NULL;
    }
    return nmo_type_query_find_for_object(type_rt->types, object);
}

#endif /* NMO_SESSION_INTERNAL_H */
