#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"

void *nmo_object_get_ancestor_state(
    const nmo_object_t *object,
    const nmo_type_descriptor_t *type_desc,
    const nmo_type_descriptor_t *derived_type_desc)
{
    if (object == NULL || type_desc == NULL || derived_type_desc == NULL) {
        return NULL;
    }

    if (object->state == NULL) {
        return NULL;
    }

    uint32_t offset = nmo_type_get_state_offset(
        NULL, derived_type_desc, type_desc);
    return offset == (uint32_t)-1
        ? NULL
        : (uint8_t *)object->state + offset;
}
