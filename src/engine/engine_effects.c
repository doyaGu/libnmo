/**
 * @file engine_effects.c
 * @brief Building blocks whose only effect is on what the engine does not simulate.
 *
 * Each block here takes its input, changes the scene, a material, a sound,
 * a light or a camera, and activates its output in the same call; it has no
 * output parameter and never stays active (the Ballanced source of each was
 * checked for that). The engine has no scene to change, so the function
 * reports an effect event naming the block's target and continues the
 * flow, as the block would.
 */

#include "engine_internal.h"

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
    {NMO_GUID(0x368f0ab1, 0x2d8957e4), "Set As Active Camera", bb_effect, NULL, NULL}, /* Cameras/Montage */
    {NMO_GUID(0x32115590, 0x78951230), "Set Light Color", bb_effect, NULL, NULL}, /* Lights/Basic */
    {NMO_GUID(0xf11d010a, 0xfb1d010a), "Texture Scroller", bb_effect, NULL, NULL}, /* Materials-Textures/Animation */
    {NMO_GUID(0x1cb5661e, 0x5b5d3fda), "Set Alpha Test", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x2f572a66, 0x6a9a0088), "Set Blend Modes", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0xaaaa213a, 0xeaa8d52a), "Set Current Slot", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0xe1e1e1e1, 0x1e1e1e1e), "Set Diffuse", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0xd1d1d1d1, 0x30303030), "Set Emissive", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x1144022f, 0x68fd055c), "Set Material Z Buffer", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x60415d44, 0x0d0174ea), "Set Prelit Color", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0xeb123eb5, 0x5be321be), "Set Texture", bb_effect, NULL, NULL}, /* Materials-Textures/Basic */
    {NMO_GUID(0x3ebe40f2, 0x3fa41377), "Set MipMap Level", bb_effect, NULL, NULL}, /* Materials-Textures/Texture */
    {NMO_GUID(0x766e4e44, 0x4fac6d52), "Restore IC", bb_effect, NULL, NULL}, /* Narratives/State */
    {NMO_GUID(0x00283a35, 0x38ef6b48), "Play Sound Instance", bb_effect, NULL, NULL}, /* Sounds/Basic */
    {NMO_GUID(0x6a5f17af, 0x63480864), "Panning Control", bb_effect, NULL, NULL}, /* Sounds/Control */
    {NMO_GUID(0x7b7c731e, 0x175b27a3), "Pitch Control", bb_effect, NULL, NULL}, /* Sounds/Control */
    {NMO_GUID(0x33613f21, 0x776b185b), "Volume Control", bb_effect, NULL, NULL}, /* Sounds/Control */
    {NMO_GUID(0x7e0f2f58, 0x232d3109), "TT Restore IC", bb_effect, NULL, NULL}, /* TT Toolbox/Object */
    {NMO_GUID(0x30362f34, 0x1935316f), "TT Save IC", bb_effect, NULL, NULL}, /* TT Toolbox/Narratives */
    {NMO_GUID(0x3f7e5bff, 0x02326a71), "Set 2D Material", bb_effect, NULL, NULL}, /* Visuals/2D */
    {NMO_GUID(0x31d97d82, 0x78d54d98), "Hide", bb_effect, NULL, NULL}, /* Visuals/Show-Hide */
    {NMO_GUID(0xa85a213a, 0xef78d52a), "Show", bb_effect, NULL, NULL}, /* Visuals/Show-Hide */
    {NMO_GUID(0xf5faaaaa, 0xfdd5bd00), "Set Background Color", bb_effect, NULL, NULL}, /* World Environments/Background */
    {NMO_GUID(0x151aaaaa, 0xffd5bdbc), "Set Fog", bb_effect, NULL, NULL}, /* World Environments/Global */
};

nmo_status_t eng_register_effects(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_effect_blocks) / sizeof(eng_effect_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_effect_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
