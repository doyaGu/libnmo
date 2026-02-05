/**
 * @file object_vtables.c
 * @brief CKObject-derived vtable definitions and copy/validate helpers
 */

#include "object/nmo_object_type_common.h"

nmo_status_t nmo_object_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    return nmo_object_default_copy(src, dst, type, arena);
}

nmo_status_t nmo_object_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

