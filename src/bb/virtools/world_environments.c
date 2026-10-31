/**
 * @file world_environments.c
 * @brief Ports of the World Environments building blocks that keep state.
 *
 * Each function does what the block's function in WorldEnvironments.dll does
 * (the reference is the Ballanced source of the blocks): it changes the
 * render context's background or fog.
 */

#include "virtools.h"

static uint32_t color_argb(const float color[4], float alpha)
{
    uint32_t argb = 0;
    const float c[4] = {color[0], color[1], color[2], alpha};
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        argb |= (uint32_t)(v * 255.0f + 0.5f) << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/* Set Background Color: the diffuse of the render context's background material */
static int bb_set_background_color(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    b->engine->render.background_color = color_argb(color, color[3]);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Fog: mode, start, end, density and color. Version 2 takes the
 * VXFOG_MODE; an older one (no locals) turns linear fog on or off and
 * scales the distances by 2.8.
 */
static int bb_set_fog(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    bool old_mode = nmo_engine_block_version(b) != 0x00020000 &&
                    nmo_engine_block_local_parameter_count(b) == 0;
    int32_t mode = get_in_int(b, 0, 0);
    float start = get_in_float(b, 1, 0.0f);
    float end = get_in_float(b, 2, 10.0f);
    if (old_mode) {
        mode = mode != 0 ? 3 : 0; /* VXFOG_LINEAR */
        start *= 2.8f;
        end *= 2.8f;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 4, color, sizeof(color));
    eng_render_t *render = &b->engine->render;
    render->fog_mode = mode;
    render->fog_start = start;
    render->fog_end = end;
    render->fog_density = get_in_float(b, 3, 1.0f);
    render->fog_color = color_argb(color, 1.0f);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_world_environments_blocks[] = {
    {NMO_GUID(0xf5faaaaa, 0xfdd5bd00), "Set Background Color", bb_set_background_color, NULL, NULL},
    {NMO_GUID(0x151aaaaa, 0xffd5bdbc), "Set Fog", bb_set_fog, NULL, NULL},
};

nmo_status_t eng_register_world_environments(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_world_environments_blocks);
}
