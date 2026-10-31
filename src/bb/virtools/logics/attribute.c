/**
 * @file attribute.c
 * @brief Ports of the Logics/Attribute building blocks, on the attributes the
 *        engine keeps (core/attributes.c).
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

/* ============================================================================
 * Logics/Attribute
 * ============================================================================ */

static int32_t attribute_input(blk_t *b)
{
    int32_t type = -1;
    nmo_engine_block_get_input_value(b, 0, &type, sizeof(type));
    return type;
}

/* Has Attribute: True with the attribute's value in the output, or False. */
static int bb_has_attribute(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_attribute_t *attribute =
        target != 0 ? eng_attribute_find(b->engine, target, attribute_input(b)) : NULL;
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
    eng_attribute_t *attribute = eng_attribute_find(b->engine, target, type);
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

/*
 * The `index`th object with an attribute of `type` (CKAttributeManager's
 * list: in the order objects got it, those in the level scene); 0 past the end.
 */
static nmo_object_id_t attribute_holder(const nmo_engine_t *engine, int32_t type, int32_t index,
                                        eng_attribute_t **out_attribute)
{
    for (size_t i = 0; index >= 0 && i < engine->attributes.values.count; i++) {
        eng_attribute_t *attribute = &engine->attributes.values.data[i];
        if (attribute->type == type && eng_in_scene(engine, attribute->object) && index-- == 0) {
            *out_attribute = attribute;
            return attribute->object;
        }
    }
    return 0;
}

/*
 * Objects With Attribute Iterator: each object with the attribute, Loop Out
 * with it and its attribute's value, then Out.
 */
static int bb_objects_with_attribute(blk_t *b)
{
    int32_t type = attribute_input(b);
    int32_t index = 0;
    nmo_engine_block_get_local_value(b, 0, &index, sizeof(index));
    if (IN_ACTIVE(b, 0)) {
        index = 0;
        IN_OFF(b, 0);
    } else {
        IN_OFF(b, 1);
        index++;
    }
    eng_attribute_t *attribute = NULL;
    nmo_object_id_t object = attribute_holder(b->engine, type, index, &attribute);
    if (object == 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t id = (uint32_t)object;
    nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    nmo_engine_block_set_local_value(b, 0, &index, sizeof(index));
    eng_param_t *out = block_output(b, 1);
    if (out != NULL && attribute->has_value) {
        eng_param_copy(b->engine, out, &attribute->value, false);
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_attribute_blocks[] = {
    {NMO_GUID(0x25b54079, 0x6ff90545), "Has Attribute", bb_has_attribute, NULL, NULL},
    {NMO_GUID(0x373040f2, 0x05e01b34), "Set Attribute", bb_set_attribute, NULL, NULL},
    {NMO_GUID(0x6b6340c4, 0x61e94a41), "Remove Attribute", bb_remove_attribute, NULL, NULL},
    {NMO_GUID(0x6bc1494c, 0x0c816ad3), "Objects With Attribute Iterator", bb_objects_with_attribute,
     NULL, NULL},
};

nmo_status_t eng_register_logics_attribute(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_attribute_blocks);
}
