/**
 * @file effects.c
 * @brief Building blocks whose only effect is on what the engine does not simulate.
 *
 * Each block here takes its input, changes the scene, a material, a sound,
 * a light or a camera, and activates its output in the same call; it has no
 * output parameter and never stays active (the Ballanced source of each was
 * checked for that). The engine has no scene to change, so the function
 * reports an effect event naming the block's target and continues the
 * flow, as the block would.
 */

#include "virtools.h"

static int bb_effect(nmo_engine_block_t *block)
{
    nmo_engine_block_activate_input(block, 0, false);
    nmo_engine_block_activate_output(block, 0, true);
    eng_emit(block->engine, NMO_ENGINE_EVENT_EFFECT, block->node, nmo_engine_block_target(block),
             0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_effect_blocks[] = {
    {NMO_GUID(0x3ebe40f2, 0x3fa41377), "Set MipMap Level", bb_effect, NULL, NULL}, /* Materials-Textures/Texture */
    {NMO_GUID(0x734f73b4, 0x01fc79ac), "TT_FlushTextures", bb_effect, NULL, NULL}, /* TT Toolbox/Narratives */
};

nmo_status_t eng_register_effects(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_effect_blocks);
}
