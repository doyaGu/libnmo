/**
 * @file runtime_internal.h
 * @brief Shared helpers for session-layer runtime modules.
 *
 * Internal header — not part of the public API.
 */

#ifndef NMO_SESSION_RUNTIME_INTERNAL_H
#define NMO_SESSION_RUNTIME_INTERNAL_H

#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "format/nmo_object.h"

/**
 * @brief Find type descriptor for an object by its class ID (inherited lookup).
 */
static inline const nmo_type_descriptor_t *runtime_find_type_for_object(
    const nmo_type_runtime_t *type_rt,
    const nmo_object_t *object)
{
    if (type_rt == NULL || type_rt->types == NULL || object == NULL) {
        return NULL;
    }
    return nmo_type_registry_find_by_class_id_inherited(type_rt->types, object->class_id);
}

#endif /* NMO_SESSION_RUNTIME_INTERNAL_H */
