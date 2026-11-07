/**
 * @file world_environments.c
 * @brief Ports of the World Environments building blocks that keep state.
 *
 * Each function does what the block's function in WorldEnvironments.dll does
 * (the reference is the Ballanced source of the blocks): it changes the
 * render context's background or fog.
 */

#include "virtools.h"

#include <math.h>

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

/* RGBAFTOCOLOR(r, g, b, a): each channel scaled to its place and rounded (half to even), masked */
static uint32_t rgba_to_color(const float c[4])
{
    uint32_t r = (uint32_t)(int64_t)rint((double)c[0] * 16711680.0);
    uint32_t g = (uint32_t)(int64_t)rint((double)c[1] * 65280.0);
    uint32_t bl = (uint32_t)(int64_t)rint((double)c[2] * 255.0);
    uint32_t a = (uint32_t)(int64_t)rint((double)c[3] * 255.0);
    return (g & 0xFF00u) | (r & 0xFF0000u) | (bl & 0xFFu) | (a << 24);
}

/* Set Background Image: the texture of the render context's background material */
static int bb_set_background_image(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    b->engine->render.background_texture = nmo_engine_block_get_input_object(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Background Material: the background material takes every member of
 * the material; what a background shows of it is its diffuse color and
 * texture.
 */
static int bb_set_background_material(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_material_t desc;
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    if (material != 0 && nmo_engine_material(b->engine, material, &desc)) {
        b->engine->render.background_color = color_argb(desc.diffuse, desc.diffuse[3]);
        b->engine->render.background_texture = desc.texture;
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Set Ambient Light Color: CKRenderContext::SetAmbientLight with the color's red, green and blue */
static int bb_set_ambient_light_color(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    color[3] = 1.0f;
    b->engine->render.ambient_light = rgba_to_color(color);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_world_environments_blocks[] = {
    {NMO_GUID_INIT(0xf5faaaaa, 0xfdd5bd00), "Set Background Color", bb_set_background_color, NULL, NULL},
    {NMO_GUID_INIT(0x151aaaaa, 0xffd5bdbc), "Set Fog", bb_set_fog, NULL, NULL},
    {NMO_GUID_INIT(0x1254d1ce, 0x001d0a58), "Set Background Image", bb_set_background_image, NULL, NULL},
    {NMO_GUID_INIT(0xddccefec, 0xd010c102), "Set Background Material", bb_set_background_material, NULL,
     NULL},
    {NMO_GUID_INIT(0x136b3f22, 0x0dc12786), "Set Ambient Light Color", bb_set_ambient_light_color, NULL,
     NULL},
};

nmo_status_t eng_register_world_environments(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_world_environments_blocks);
}
