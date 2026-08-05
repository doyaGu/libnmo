#include "type/nmo_type_view.h"

#include "format/nmo_object.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_system.h"

#include <string.h>

static void nmo_type_view_clear(nmo_type_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->type_id = NMO_TYPE_ID_INVALID;
}

static nmo_status_t nmo_type_view_fill(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *type,
    nmo_type_view_t *out_view)
{
    if (registry == NULL || type == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    out_view->guid = type->guid;
    out_view->base_guid = type->base_type;
    out_view->type_id = type->id;
    out_view->class_id = (nmo_class_id_t)type->class_id;
    out_view->category = type->category;
    out_view->flags = type->flags;
    out_view->size = type->size;
    out_view->alignment = type->alignment;
    out_view->field_count = type->field_count;
    out_view->name = type->name;
    out_view->description = type->description;
    out_view->has_reflection = nmo_type_has_reflection(type);
    out_view->ui_visible = nmo_type_registry_is_ui_visible_by_id(registry, type->id);
    return NMO_OK;
}

nmo_status_t nmo_type_view_from_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid,
    nmo_type_view_t *out_view)
{
    if (registry == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_type_view_clear(out_view);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, guid);
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    return nmo_type_view_fill(registry, type, out_view);
}

nmo_status_t nmo_type_view_from_class_id(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_type_view_t *out_view)
{
    if (registry == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_type_view_clear(out_view);

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(registry, (uint32_t)class_id);
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    return nmo_type_view_fill(registry, type, out_view);
}

nmo_status_t nmo_type_view_from_type_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_type_view_t *out_view)
{
    if (registry == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_type_view_clear(out_view);

    const nmo_type_descriptor_t *type =
        nmo_type_registry_get_by_id(registry, type_id);
    if (type == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    return nmo_type_view_fill(registry, type, out_view);
}

nmo_status_t nmo_type_view_from_object(
    const nmo_type_registry_t *registry,
    const nmo_object_t *object,
    nmo_type_view_t *out_view)
{
    if (registry == NULL || object == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_type_descriptor_t *type =
        nmo_type_query_find_for_object(registry, object);
    if (type == NULL) {
        nmo_type_view_clear(out_view);
        return NMO_ERR_NOT_FOUND;
    }
    return nmo_type_view_fill(registry, type, out_view);
}
