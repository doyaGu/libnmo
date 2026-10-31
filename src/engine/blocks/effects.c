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

#include "../engine_block_util.h"

static int bb_effect(nmo_engine_block_t *block)
{
    nmo_engine_block_activate_input(block, 0, false);
    nmo_engine_block_activate_output(block, 0, true);
    eng_emit(block->engine, NMO_ENGINE_EVENT_EFFECT, block->node, nmo_engine_block_target(block),
             0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_effect_blocks[] = {
    {NMO_GUID(0x676776d0, 0x20d457bd), "Position On Curve", bb_effect, NULL, NULL}, /* 3D Transformations/Curve */
    {NMO_GUID(0x652316a2, 0x01aa09a9), "Set Clipping Planes", bb_effect, NULL, NULL}, /* Cameras/Basic */
    {NMO_GUID(0xf11d010a, 0xfb1d010a), "Texture Scroller", bb_effect, NULL, NULL}, /* Materials-Textures/Animation */
    {NMO_GUID(0x1cb5661e, 0x5b5d3fda), "Set Alpha Test", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x2f572a66, 0x6a9a0088), "Set Blend Modes", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0xaaaa213a, 0xeaa8d52a), "Set Current Slot", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x1144022f, 0x68fd055c), "Set Material Z Buffer", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x60415d44, 0x0d0174ea), "Set Prelit Color", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x3ebe40f2, 0x3fa41377), "Set MipMap Level", bb_effect, NULL, NULL}, /* Materials-Textures/Texture */
    {NMO_GUID(0x00283a35, 0x38ef6b48), "Play Sound Instance", bb_effect, NULL, NULL}, /* Sounds/Basic */
    {NMO_GUID(0x6a5f17af, 0x63480864), "Panning Control", bb_effect, NULL, NULL}, /* Sounds/Control */
    {NMO_GUID(0x7b7c731e, 0x175b27a3), "Pitch Control", bb_effect, NULL, NULL}, /* Sounds/Control */
    {NMO_GUID(0x33613f21, 0x776b185b), "Volume Control", bb_effect, NULL, NULL}, /* Sounds/Control */
    {NMO_GUID(0x734f73b4, 0x01fc79ac), "TT_FlushTextures", bb_effect, NULL, NULL}, /* TT Toolbox/Narratives */
};

nmo_status_t eng_register_effects(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_effect_blocks);
}
