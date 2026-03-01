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

    if (!derived_type_desc->ext ||
        !derived_type_desc->ext->hierarchy ||
        !derived_type_desc->ext->state_offsets ||
        derived_type_desc->ext->hierarchy_depth == 0) {
        return NULL;
    }

    for (uint16_t i = 0; i < derived_type_desc->ext->hierarchy_depth; i++) {
        if (derived_type_desc->ext->hierarchy[i] == type_desc ||
            nmo_guid_equals(derived_type_desc->ext->hierarchy[i]->guid, type_desc->guid)) {
            uint32_t offset = derived_type_desc->ext->state_offsets[i];
            return (uint8_t *)object->state + offset;
        }
    }

    return NULL;
}
