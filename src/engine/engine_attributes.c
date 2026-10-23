/**
 * @file engine_attributes.c
 * @brief Object attributes at run time.
 *
 * The engine copies the attributes of every behavioral object from the
 * document: the attribute type (an index into the file's attribute types,
 * the same an Attribute parameter holds) and the value of its parameter.
 * Attributes are set and removed on that copy, as CKBeObject's
 * SetAttribute and RemoveAttribute do; the document does not change.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

/* A parameter holding a value of `type`: zeroes, or the saved state's bytes when given. */
static bool attribute_value_init(nmo_engine_t *engine, eng_param_t *value, nmo_guid_t type,
                                 const nmo_parameter_state_t *state)
{
    memset(value, 0, sizeof(*value));
    value->kind = ENG_PARAM_PLAIN;
    value->type = type;
    value->type_desc = nmo_type_registry_find_by_guid(engine->types, type);
    value->source = -1;
    value->operation = -1;
    if (nmo_guid_equals(type, CKPGUID_2DCURVE)) {
        return eng_curve_value(state, &value->data, &value->size);
    }
    if (state != NULL) {
        return eng_parameter_state_bytes(state, value->type_desc != NULL ? value->type_desc->size : 0u,
                                         &value->data, &value->size);
    }
    size_t size = eng_type_size(engine, type);
    value->data = size > 0 ? (uint8_t *)calloc(1, size) : NULL;
    value->size = value->data != NULL ? (uint32_t)size : 0u;
    return size == 0 || value->data != NULL;
}

static nmo_status_t attribute_add(nmo_engine_t *engine, nmo_object_id_t object, int32_t type,
                                  nmo_guid_t value_type, const nmo_parameter_state_t *state)
{
    eng_attribute_t *attribute = NULL;
    if (!ENG_VEC_PUSH(engine->attributes, attribute)) {
        return NMO_ERR_NOMEM;
    }
    attribute->object = object;
    attribute->type = type;
    attribute->has_value = !nmo_guid_is_null(value_type);
    if (attribute->has_value && !attribute_value_init(engine, &attribute->value, value_type, state)) {
        engine->attributes.count--;
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

nmo_status_t eng_load_attributes(nmo_engine_t *engine)
{
    nmo_attributemanager_state_t types;
    if (nmo_workspace_internal_attribute_types(engine->workspace, &types) == NMO_OK) {
        for (uint32_t i = 0; i < types.attribute_count; i++) {
            eng_attribute_type_t *slot = NULL;
            if (!ENG_VEC_PUSH(engine->attribute_types, slot)) {
                return NMO_ERR_NOMEM;
            }
            const nmo_attribute_descriptor_t *desc = &types.attributes[i];
            slot->present = desc->present;
            slot->parameter_type = desc->parameter_type_guid;
            slot->compatible_class_id = desc->compatible_class_id;
        }
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    size_t count = 0;
    nmo_object_t **objects = repo != NULL ? nmo_object_repository_get_all(repo, &count) : NULL;
    for (size_t i = 0; objects != NULL && i < count; i++) {
        const nmo_beobject_state_t *be =
            (const nmo_beobject_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                engine->types, objects[i], CKPGUID_BEOBJECT);
        if (be == NULL) {
            continue;
        }
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        for (size_t a = 0; a < be->attributes.count; a++) {
            const nmo_beobject_attribute_t *saved =
                &((const nmo_beobject_attribute_t *)be->attributes.data)[a];
            nmo_object_t *param = nmo_object_repository_find_by_id(
                repo, nmo_ref_runtime_id(&saved->parameter));
            const nmo_parameter_state_t *state =
                param != NULL ? (const nmo_parameter_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                    engine->types, param, CKPGUID_PARAMETER)
                              : NULL;
            nmo_status_t status = attribute_add(engine, id, (int32_t)saved->type_id,
                                                state != NULL ? state->type_guid : NMO_GUID(0u, 0u),
                                                state);
            if (status != NMO_OK) {
                return status;
            }
        }
    }
    return NMO_OK;
}

void eng_free_attributes(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->attributes.count; i++) {
        free(engine->attributes.data[i].value.data);
    }
    free(engine->attributes.data);
    free(engine->attribute_types.data);
}

static eng_attribute_t *attribute_find(const nmo_engine_t *engine, nmo_object_id_t object,
                                       int32_t type)
{
    for (size_t i = 0; i < engine->attributes.count; i++) {
        eng_attribute_t *attribute = &engine->attributes.data[i];
        if (attribute->object == object && attribute->type == type) {
            return attribute;
        }
    }
    return NULL;
}

bool nmo_engine_object_has_attribute(const nmo_engine_t *engine, nmo_object_id_t object,
                                     int32_t type)
{
    return engine != NULL && attribute_find(engine, object, type) != NULL;
}

nmo_status_t nmo_engine_object_attribute_value(const nmo_engine_t *engine, nmo_object_id_t object,
                                               int32_t type, const void **out_data,
                                               size_t *out_size, nmo_guid_t *out_type)
{
    const eng_attribute_t *attribute = engine != NULL ? attribute_find(engine, object, type) : NULL;
    if (attribute == NULL || !attribute->has_value) {
        return NMO_ERR_NOT_FOUND;
    }
    if (out_data != NULL) {
        *out_data = attribute->value.data;
    }
    if (out_size != NULL) {
        *out_size = attribute->value.size;
    }
    if (out_type != NULL) {
        *out_type = attribute->value.type;
    }
    return NMO_OK;
}

/*
 * CKBeObject::SetAttribute: a new attribute of a type the object's class may
 * have, with a parameter of the attribute's type when it has one.
 */
bool nmo_engine_object_set_attribute(nmo_engine_t *engine, nmo_object_id_t object, int32_t type)
{
    if (engine == NULL || type < 0 || attribute_find(engine, object, type) != NULL) {
        return false;
    }
    if ((size_t)type >= engine->attribute_types.count ||
        !engine->attribute_types.data[type].present) {
        return false;
    }
    const eng_attribute_type_t *desc = &engine->attribute_types.data[type];
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    nmo_object_t *target = repo != NULL ? nmo_object_repository_find_by_id(repo, object) : NULL;
    if (target == NULL) {
        return false;
    }
    if (desc->compatible_class_id > 0 &&
        !nmo_type_registry_is_class_derived_from(engine->types,
                                                 (uint32_t)nmo_object_get_class_id(target),
                                                 (uint32_t)desc->compatible_class_id)) {
        return false;
    }
    return attribute_add(engine, object, type, desc->parameter_type, NULL) == NMO_OK;
}

bool nmo_engine_object_remove_attribute(nmo_engine_t *engine, nmo_object_id_t object,
                                        int32_t type)
{
    eng_attribute_t *attribute = engine != NULL ? attribute_find(engine, object, type) : NULL;
    if (attribute == NULL) {
        return false;
    }
    free(attribute->value.data);
    size_t index = (size_t)(attribute - engine->attributes.data);
    memmove(attribute, attribute + 1,
            (engine->attributes.count - index - 1u) * sizeof(eng_attribute_t));
    engine->attributes.count--;
    return true;
}
