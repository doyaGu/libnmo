/**
 * @file lights.c
 * @brief Ports of the Lights building blocks that keep state.
 *
 * Each function does what the block's function in Lights.dll does (the
 * reference is the Ballanced source of the blocks).
 */

#include "virtools.h"

/* Set Light Color: CKLight::SetColor */
static int bb_set_light_color(blk_t *b)
{
    eng_light_t *light = eng_light(b->engine, nmo_engine_block_target(b));
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_block_get_input_value(b, 0, light->color, sizeof(light->color));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, light->id, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_lights_blocks[] = {
    {NMO_GUID_INIT(0x32115590, 0x78951230), "Set Light Color", bb_set_light_color, NULL, NULL},
};

nmo_status_t eng_register_lights(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_lights_blocks);
}
