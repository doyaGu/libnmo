/**
 * @file type_query.c
 * @brief Type query convenience functions
 */

#include "type/nmo_type_query.h"
#include "core/nmo_guid.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

const char *nmo_type_query_class_name_from_id(
    const nmo_type_registry_t *registry, nmo_class_id_t class_id)
{
    if (!registry) return NULL;
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(registry, (uint32_t)class_id);
    return type ? type->name : NULL;
}

nmo_class_id_t nmo_type_query_class_id_from_name(
    const nmo_type_registry_t *registry, const char *name)
{
    if (!registry || !name || !name[0]) return 0;
    nmo_type_id_t type_id = nmo_type_registry_name_to_type_id(registry, name);
    if (type_id == NMO_TYPE_ID_INVALID) return 0;
    uint32_t class_id_u32 = 0;
    nmo_status_t rc = nmo_type_registry_type_id_to_class_id(registry, type_id, &class_id_u32);
    return (rc == NMO_OK && class_id_u32 != 0) ? (nmo_class_id_t)class_id_u32 : 0;
}

nmo_class_id_t nmo_type_query_class_get_parent(
    const nmo_type_registry_t *registry, nmo_class_id_t class_id)
{
    if (!registry) return 0;
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id(registry, (uint32_t)class_id);
    if (!type || nmo_guid_is_null(type->base_type)) return 0;
    const nmo_type_descriptor_t *base =
        nmo_type_registry_find_by_guid(registry, type->base_type);
    return base ? (nmo_class_id_t)base->class_id : 0;
}

bool nmo_type_query_class_is_derived_from(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id, nmo_class_id_t base_id)
{
    if (!registry) return false;
    const nmo_type_descriptor_t *child =
        nmo_type_registry_find_by_class_id_inherited(registry, (uint32_t)class_id);
    const nmo_type_descriptor_t *base =
        nmo_type_registry_find_by_class_id_inherited(registry, (uint32_t)base_id);
    if (!child || !base) return false;
    return nmo_type_is_derived_from((nmo_type_registry_t *)registry, child->id, base->id);
}

const nmo_type_descriptor_t *nmo_type_query_find_by_guid(
    const nmo_type_registry_t *registry, nmo_guid_t guid)
{
    return registry ? nmo_type_registry_find_by_guid(registry, guid) : NULL;
}

const nmo_type_descriptor_t *nmo_type_query_find_by_class_id(
    const nmo_type_registry_t *registry, nmo_class_id_t class_id)
{
    return registry ? nmo_type_registry_find_by_class_id(registry, class_id) : NULL;
}

const nmo_type_descriptor_t *nmo_type_query_find_for_object(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj)
{
    if (!registry || !obj) return NULL;

    nmo_guid_t explicit_guid = nmo_object_get_type_guid(obj);
    if (!nmo_guid_is_null(explicit_guid)) {
        const nmo_type_descriptor_t *type =
            nmo_type_registry_find_by_guid(registry, explicit_guid);
        if (type != NULL) return type;
    }

    return nmo_type_registry_find_by_class_id_inherited(
        registry, (uint32_t)nmo_object_get_class_id(obj));
}

bool nmo_type_query_object_is_derived_from_guid(
    const nmo_type_registry_t *registry,
    const nmo_object_t *obj, nmo_guid_t base_guid)
{
    if (!registry || !obj) return false;
    const nmo_type_descriptor_t *base = nmo_type_query_find_by_guid(registry, base_guid);
    const nmo_type_descriptor_t *derived =
        nmo_type_query_find_for_object(registry, obj);
    if (!base || !derived) return false;
    return nmo_type_is_derived_from((nmo_type_registry_t *)registry, derived->id, base->id);
}

void *nmo_type_query_object_get_ancestor_state_by_guid(
    const nmo_type_registry_t *registry,
    nmo_object_t *obj, nmo_guid_t base_guid)
{
    if (!registry || !obj) return NULL;
    const nmo_type_descriptor_t *base = nmo_type_query_find_by_guid(registry, base_guid);
    const nmo_type_descriptor_t *derived =
        nmo_type_query_find_for_object(registry, obj);
    if (!base || !derived) return NULL;
    return nmo_object_get_ancestor_state(obj, base, derived);
}
