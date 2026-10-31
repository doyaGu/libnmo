/**
 * @file attributes.c
 * @brief The attribute manager: object attributes at run time.
 *
 * The engine copies the attributes of every behavioral object of a document:
 * the attribute type (an index into the engine's attribute types, the same an
 * Attribute parameter holds) and the value of its parameter. Blocks then set
 * and remove attributes on that copy, as CKBeObject's SetAttribute and
 * RemoveAttribute do; the document does not change.
 */

#include "../engine_internal.h"

#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

/* A parameter holding a value of `type`: zeroes, or the saved state's bytes (of `module`) when given. */
static bool attribute_value_init(nmo_engine_t *engine, const eng_module_t *module,
                                 eng_param_t *value, nmo_guid_t type,
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
        return eng_parameter_state_bytes(module, state,
                                         value->type_desc != NULL ? value->type_desc->size : 0u,
                                         &value->data, &value->size);
    }
    size_t size = eng_type_size(engine, type);
    value->data = size > 0 ? (uint8_t *)calloc(1, size) : NULL;
    value->size = value->data != NULL ? (uint32_t)size : 0u;
    return size == 0 || value->data != NULL;
}

static nmo_status_t attribute_add(nmo_engine_t *engine, const eng_module_t *module,
                                  nmo_object_id_t object, int32_t type, nmo_guid_t value_type,
                                  const nmo_parameter_state_t *state)
{
    eng_attribute_t *attribute = NULL;
    if (!ENG_VEC_PUSH(engine->attributes.values, attribute)) {
        return NMO_ERR_NOMEM;
    }
    attribute->object = object;
    attribute->type = type;
    attribute->has_value = !nmo_guid_is_null(value_type);
    if (attribute->has_value &&
        !attribute_value_init(engine, module, &attribute->value, value_type, state)) {
        engine->attributes.values.count--;
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

nmo_status_t eng_load_attributes(nmo_engine_t *engine, const eng_range_t *range)
{
    const eng_module_t *owner = &engine->modules.data[range->module];
    nmo_object_repository_t *repo = owner->repo;
    size_t count = 0;
    nmo_object_t **objects = repo != NULL ? nmo_object_repository_get_all(repo, &count) : NULL;
    for (size_t i = 0; objects != NULL && i < count; i++) {
        if (nmo_object_get_id(objects[i]) < range->first ||
            nmo_object_get_id(objects[i]) > range->last) {
            continue;
        }
        const nmo_beobject_state_t *be =
            (const nmo_beobject_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                engine->types, objects[i], CKPGUID_BEOBJECT);
        if (be == NULL) {
            continue;
        }
        nmo_object_id_t id = eng_module_id(owner, nmo_object_get_id(objects[i]));
        for (size_t a = 0; a < be->attributes.count; a++) {
            const nmo_beobject_attribute_t *saved =
                &((const nmo_beobject_attribute_t *)be->attributes.data)[a];
            nmo_object_t *param = nmo_object_repository_find_by_id(
                repo, nmo_ref_runtime_id(&saved->parameter));
            const nmo_parameter_state_t *state =
                param != NULL ? (const nmo_parameter_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                    engine->types, param, CKPGUID_PARAMETER)
                              : NULL;
            nmo_status_t status = attribute_add(
                engine, owner, id, eng_module_attribute_type(owner, (int32_t)saved->type_id),
                state != NULL ? state->type_guid : NMO_GUID(0u, 0u), state);
            if (status != NMO_OK) {
                return status;
            }
        }
    }
    return NMO_OK;
}

void eng_free_attributes(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        free(engine->attributes.values.data[i].value.data);
    }
    free(engine->attributes.values.data);
    for (size_t i = 0; i < engine->attributes.types.count; i++) {
        free(engine->attributes.types.data[i].name);
    }
    free(engine->attributes.types.data);
}

eng_attribute_t *eng_attribute_find(const nmo_engine_t *engine, nmo_object_id_t object,
                                    int32_t type)
{
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        eng_attribute_t *attribute = &engine->attributes.values.data[i];
        if (attribute->object == object && attribute->type == type) {
            return attribute;
        }
    }
    return NULL;
}

/*
 * CKBeObject::Copy at run time: `to` has `from`'s attributes, with the values
 * the engine has for them now, in place of the ones it was loaded with.
 */
void eng_copy_attributes(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to)
{
    if (from == to) {
        return;
    }
    for (size_t i = 0; i < engine->attributes.values.count;) {
        if (engine->attributes.values.data[i].object == to) {
            nmo_engine_object_remove_attribute(engine, to, engine->attributes.values.data[i].type);
        } else {
            i++;
        }
    }
    size_t count = engine->attributes.values.count;
    for (size_t i = 0; i < count; i++) {
        const eng_attribute_t *original = &engine->attributes.values.data[i];
        if (original->object != from) {
            continue;
        }
        int32_t type = original->type;
        nmo_guid_t value_type = original->has_value ? original->value.type : NMO_GUID(0u, 0u);
        if (attribute_add(engine, NULL, to, type, value_type, NULL) != NMO_OK) {
            return;
        }
        eng_attribute_t *copy = &engine->attributes.values.data[engine->attributes.values.count - 1u];
        original = &engine->attributes.values.data[i];
        if (original->has_value && copy->has_value) {
            eng_param_copy(engine, &copy->value, (eng_param_t *)&original->value, false);
        }
    }
}

bool nmo_engine_object_has_attribute(const nmo_engine_t *engine, nmo_object_id_t object,
                                     int32_t type)
{
    return engine != NULL && eng_attribute_find(engine, object, type) != NULL;
}

nmo_status_t nmo_engine_object_attribute_value(const nmo_engine_t *engine, nmo_object_id_t object,
                                               int32_t type, const void **out_data,
                                               size_t *out_size, nmo_guid_t *out_type)
{
    const eng_attribute_t *attribute = engine != NULL ? eng_attribute_find(engine, object, type) : NULL;
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
    if (engine == NULL || type < 0 || eng_attribute_find(engine, object, type) != NULL) {
        return false;
    }
    if ((size_t)type >= engine->attributes.types.count ||
        !engine->attributes.types.data[type].present) {
        return false;
    }
    const eng_attribute_type_t *desc = &engine->attributes.types.data[type];
    nmo_object_t *target = eng_find_object(engine, object);
    if (target == NULL) {
        return false;
    }
    if (desc->compatible_class_id > 0 &&
        !nmo_type_registry_is_class_derived_from(engine->types,
                                                 (uint32_t)nmo_object_get_class_id(target),
                                                 (uint32_t)desc->compatible_class_id)) {
        return false;
    }
    return attribute_add(engine, NULL, object, type, desc->parameter_type, NULL) == NMO_OK;
}

bool nmo_engine_object_remove_attribute(nmo_engine_t *engine, nmo_object_id_t object,
                                        int32_t type)
{
    eng_attribute_t *attribute = engine != NULL ? eng_attribute_find(engine, object, type) : NULL;
    if (attribute == NULL) {
        return false;
    }
    free(attribute->value.data);
    size_t index = (size_t)(attribute - engine->attributes.values.data);
    memmove(attribute, attribute + 1,
            (engine->attributes.values.count - index - 1u) * sizeof(eng_attribute_t));
    engine->attributes.values.count--;
    return true;
}
