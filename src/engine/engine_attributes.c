/**
 * @file engine_attributes.c
 * @brief Object attributes at run time, and the Logics/Attribute blocks.
 *
 * The engine copies the attributes of every behavioral object of a
 * document: the attribute type (an index into the engine's attribute types,
 * the same an Attribute parameter holds) and the value of its parameter.
 * Blocks then set and remove attributes on that copy, as CKBeObject's
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

typedef nmo_engine_block_t blk_t;

#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

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
    if (!ENG_VEC_PUSH(engine->attributes, attribute)) {
        return NMO_ERR_NOMEM;
    }
    attribute->object = object;
    attribute->type = type;
    attribute->has_value = !nmo_guid_is_null(value_type);
    if (attribute->has_value &&
        !attribute_value_init(engine, module, &attribute->value, value_type, state)) {
        engine->attributes.count--;
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

nmo_status_t eng_load_attributes(nmo_engine_t *engine, int32_t module)
{
    const eng_module_t *owner = &engine->modules.data[module];
    nmo_object_repository_t *repo = owner->repo;
    size_t count = 0;
    nmo_object_t **objects = repo != NULL ? nmo_object_repository_get_all(repo, &count) : NULL;
    for (size_t i = 0; objects != NULL && i < count; i++) {
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
    for (size_t i = 0; i < engine->attributes.count; i++) {
        free(engine->attributes.data[i].value.data);
    }
    free(engine->attributes.data);
    for (size_t i = 0; i < engine->attribute_types.count; i++) {
        free(engine->attribute_types.data[i].name);
    }
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

/* ============================================================================
 * Logics/Attribute
 * ============================================================================ */

static int32_t attribute_input(blk_t *b)
{
    int32_t type = -1;
    nmo_engine_block_get_input_value(b, 0, &type, sizeof(type));
    return type;
}

static eng_param_t *block_output(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine, node->first_pout, node->pout_count, pos);
}

/* Has Attribute: True with the attribute's value in the output, or False. */
static int bb_has_attribute(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_attribute_t *attribute =
        target != 0 ? attribute_find(b->engine, target, attribute_input(b)) : NULL;
    if (attribute == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 0);
    eng_param_t *out = block_output(b, 0);
    if (out != NULL && attribute->has_value) {
        eng_param_copy(b->engine, out, &attribute->value, false);
    }
    return NMO_ENGINE_BR_OK;
}

/* Set Attribute: the attribute, its value from the input after Attribute. */
static int bb_set_attribute(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t type = attribute_input(b);
    nmo_engine_object_set_attribute(b->engine, target, type);
    eng_attribute_t *attribute = attribute_find(b->engine, target, type);
    if (attribute == NULL || !attribute->has_value) {
        return NMO_ENGINE_BR_OK;
    }
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    eng_param_t *source = eng_param_real_source(
        b->engine, eng_node_param(b->engine, node->first_pin, node->pin_count, 1));
    if (source != NULL) {
        eng_param_copy(b->engine, &attribute->value, source, true);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_remove_attribute(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target != 0) {
        nmo_engine_object_remove_attribute(b->engine, target, attribute_input(b));
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_attribute_blocks[] = {
    {NMO_GUID(0x25b54079, 0x6ff90545), "Has Attribute", bb_has_attribute, NULL, NULL},
    {NMO_GUID(0x373040f2, 0x05e01b34), "Set Attribute", bb_set_attribute, NULL, NULL},
    {NMO_GUID(0x6b6340c4, 0x61e94a41), "Remove Attribute", bb_remove_attribute, NULL, NULL},
};

nmo_status_t eng_register_attributes(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_attribute_blocks) / sizeof(eng_attribute_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_attribute_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
