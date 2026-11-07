/**
 * @file lights.c
 * @brief Ports of the Lights building blocks that keep state.
 *
 * Each function does what the block's function in Lights.dll does (the
 * reference is the Ballanced source of the blocks).
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_param_guids.h"

#include <string.h>

/* The target light, its change reported as an effect. */
static eng_light_t *target_light(blk_t *b)
{
    return eng_light(b->engine, nmo_engine_block_target(b));
}

static int light_done(blk_t *b, const eng_light_t *light)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, light->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Light Color: CKLight::SetColor */
static int bb_set_light_color(blk_t *b)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_block_get_input_value(b, 0, light->color, sizeof(light->color));
    return light_done(b, light);
}

/* Set Light Attenuation: the constant, linear and quadratic attenuations. */
static int bb_set_light_attenuation(blk_t *b)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    for (int i = 0; i < 3; i++) {
        light->attenuation[i] = get_in_float(b, i, light->attenuation[i]);
    }
    return light_done(b, light);
}

/* Set Linear Attenuation and Set Quadratic Attenuation (obsolete). */
static int set_one_attenuation(blk_t *b, int which)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_block_get_input_value(b, 0, &light->attenuation[which], sizeof(float));
    return light_done(b, light);
}

static int bb_set_linear_attenuation(blk_t *b) { return set_one_attenuation(b, 1); }

static int bb_set_quadratic_attenuation(blk_t *b) { return set_one_attenuation(b, 2); }

/* Set Light Range: CKLight::SetRange */
static int bb_set_light_range(blk_t *b)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_block_get_input_value(b, 0, &light->range, sizeof(light->range));
    return light_done(b, light);
}

/* Set Light Type: CKLight::SetType */
static int bb_set_light_type(blk_t *b)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_block_get_input_value(b, 0, &light->type, sizeof(light->type));
    return light_done(b, light);
}

/* Set Specular Flag: CKLight::SetSpecularFlag */
static int bb_set_specular_flag(blk_t *b)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    light->specular = get_in_int(b, 0, light->specular ? 1 : 0) != 0;
    return light_done(b, light);
}

/* Spot Light Properties: fall off, fall off shape and hot spot; its out
 * fires before the target is checked. */
static int bb_spot_light_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    light->fall_off = get_in_float(b, 0, 0.87f);
    light->fall_off_shape = get_in_float(b, 1, 1.0f);
    light->hot_spot = get_in_float(b, 2, 0.7f);
    return light_done(b, light);
}

/*
 * Set Light Target: CKLight::SetTarget with the input when its class is
 * 3D Entity itself (no derived class), no target otherwise. Only a target
 * light keeps one.
 */
static int bb_set_light_target(blk_t *b)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_get_input_object(b, 0);
    bool entity = target != 0 && eng_object_class(b->engine, target) == NMO_CID_3DENTITY;
    eng_set_light_target(b->engine, light->id, entity ? target : 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, light->id, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Light Color Progression: the light's RGB goes from Start Color to End
 * Color along the Progression Curve over Duration, keeping Start Color's
 * alpha (FlashColorTimeBased, its old integer time version, and FlashColor
 * in steps, which scales Start Color by the curve). The elapsed time is the
 * first local; the curve is read at elapsed / duration before it advances
 * (no curve reads 0).
 */
typedef enum { FLASH_TIME, FLASH_TIME_INT, FLASH_STEPS } flash_mode_t;

static int light_color_progression(blk_t *b, flash_mode_t mode)
{
    eng_light_t *light = target_light(b);
    if (light == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    float elapsed = 0.0f;
    int32_t steps = 0;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        nmo_engine_block_set_local_value(b, 0, &elapsed, sizeof(elapsed));
    } else {
        if (mode == FLASH_STEPS) {
            steps = get_local_int(b, 0, 0);
        } else {
            elapsed = get_local_float(b, 0, 0.0f);
        }
        IN_OFF(b, 1);
    }
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 1, &curve_size);
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 2, color, sizeof(color));
    float duration;
    bool more;
    if (mode == FLASH_STEPS) {
        int32_t count = get_in_int(b, 0, 0);
        float y = nmo_engine_curve_y(curve, curve_size, (float)steps / (float)count);
        for (int i = 0; i < 3; i++) {
            color[i] *= y;
        }
        memcpy(light->color, color, sizeof(color));
        steps++;
        more = steps < count;
        if (more) {
            set_local_int(b, 0, steps);
        }
    } else {
        duration = mode == FLASH_TIME ? get_in_float(b, 0, 0.0f) : (float)get_in_int(b, 0, 0);
        float end[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 3, end, sizeof(end));
        float y = nmo_engine_curve_y(curve, curve_size, elapsed / duration);
        for (int i = 0; i < 3; i++) {
            color[i] += (end[i] - color[i]) * y;
        }
        memcpy(light->color, color, sizeof(color));
        float delta = nmo_engine_block_delta_time(b);
        elapsed += mode == FLASH_TIME ? delta : (float)(int64_t)delta;
        more = elapsed < duration;
        if (more) {
            nmo_engine_block_set_local_value(b, 0, &elapsed, sizeof(elapsed));
        }
    }
    OUT_ON(b, more ? 1 : 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, light->id, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_light_color_progression(blk_t *b) { return light_color_progression(b, FLASH_TIME); }

static int bb_light_color_progression_int(blk_t *b) { return light_color_progression(b, FLASH_TIME_INT); }

static int bb_light_color_progression_steps(blk_t *b) { return light_color_progression(b, FLASH_STEPS); }

static int cb_light_color_progression(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) {
        return NMO_ENGINE_BR_OK;
    }
    if (get_local_int(b, 1, 1) == 0) {
        nmo_engine_block_set_function(b, bb_light_color_progression_steps);
    } else if (nmo_engine_block_version(b) < 0x00010005u &&
               !nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_TIME)) {
        nmo_engine_block_set_function(b, bb_light_color_progression_int);
    } else {
        nmo_engine_block_set_function(b, bb_light_color_progression);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Light Sprite (DisplayOmniLight, the DLL's version): a 3D sprite of the
 * owner light, made with its material when the block loads (blended
 * additively, no Z writes) and hidden. On sets the sprite's texture, its
 * color the light's (opaque) and its size Size Scale times the light's
 * range; then each frame the sprite shows at the light, until Off hides it.
 * The first local holds the sprite, the material and whether it is on.
 */
typedef struct light_sprite {
    uint32_t sprite, material, on;
} light_sprite_t;

static light_sprite_t *light_sprite_local(blk_t *b)
{
    eng_param_t *local = block_local(b, 0);
    return local != NULL && local->size >= sizeof(light_sprite_t) ? (light_sprite_t *)local->data : NULL;
}

/* A color channel as RGBAFTOCOLOR(const VxColor *) keeps it: times 255, truncated, in [0, 255] */
static float color_channel(float value)
{
    float v = value * 255.0f;
    uint32_t byte = !(v > -2147483648.0f && v < 2147483648.0f) ? 0u
                    : v >= 255.0f                               ? 255u
                    : v > 0.0f                                  ? (uint32_t)(int32_t)v
                                                                : 0u;
    return (float)((double)byte * 0.0039215689);
}

static int bb_light_sprite(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    light_sprite_t *local = light_sprite_local(b);
    eng_light_t *light = eng_light(engine, owner);
    int32_t sprite = local != NULL ? eng_entity_index(engine, local->sprite) : -1;
    if (light == NULL || sprite < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (local->on == 0) {
        local->on = 1;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_material_t *material = eng_material(engine, local->material);
        if (material != NULL) {
            material->desc.texture = nmo_engine_block_get_input_object(b, 0);
            light = eng_light(engine, owner);
            for (int i = 0; i < 3; i++) {
                material->desc.diffuse[i] = color_channel(light->color[i]);
            }
            material->desc.diffuse[3] = color_channel(1.0f);
        }
        light = eng_light(engine, owner);
        float side = light->range * get_in_float(b, 1, 0.1f);
        eng_sprite3d_set_size(engine, local->sprite, (nmo_vector2_t){side, side});
    }
    int32_t at = eng_entity_index(engine, owner);
    if (at >= 0) {
        const float *row = engine->entities.data[at].world.m[3];
        eng_vector_t position = {row[0], row[1], row[2]};
        eng_entity_set_position(engine, eng_entity_index(engine, local->sprite), &position, -1, false);
    }
    eng_show(engine, local->sprite, 1);
    if (!IN_ACTIVE(b, 1)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    IN_OFF(b, 1);
    OUT_ON(b, 0);
    eng_show(engine, local->sprite, 0);
    local->on = 0;
    return NMO_ENGINE_BR_OK;
}

static int cb_light_sprite(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    if (message == NMO_ENGINE_MSG_LOAD) {
        light_sprite_t made = {(uint32_t)eng_create_object(engine, NMO_CID_SPRITE3D, "DOL Sprite3D"),
                               (uint32_t)eng_create_object(engine, NMO_CID_MATERIAL, "DOL Material"), 0};
        nmo_engine_block_set_local_value(b, 0, &made, sizeof(made));
        eng_material_t *material = eng_material(engine, made.material);
        if (material != NULL) {
            material->desc.alpha_blend = true;
            material->desc.z_write = false;
            material->desc.source_blend = VXBLEND_ONE;
            material->desc.dest_blend = VXBLEND_ONE;
            const float diffuse[4] = {0.0f, 0.0f, 0.0f, 0.9f}, specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            memcpy(material->desc.diffuse, diffuse, sizeof(diffuse));
            memcpy(material->desc.specular, specular, sizeof(specular));
        }
        eng_sprite3d_t *sprite = eng_sprite3d(engine, made.sprite);
        if (sprite != NULL) {
            sprite->material = made.material;
        }
        eng_show(engine, made.sprite, 0);
    } else if (message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_RESET ||
               message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        const light_sprite_t *local = light_sprite_local(b);
        if (local != NULL) {
            eng_show(engine, local->sprite, 0);
        }
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_lights_blocks[] = {
    {NMO_GUID_INIT(0x32115590, 0x78951230), "Set Light Color", bb_set_light_color, NULL, NULL},
    {NMO_GUID_INIT(0xd63d004d, 0xd54d8100), "Set Light Attenuation", bb_set_light_attenuation, NULL, NULL},
    {NMO_GUID_INIT(0x45895551, 0x59486677), "Set Linear Attenuation", bb_set_linear_attenuation, NULL, NULL},
    {NMO_GUID_INIT(0x0a0e0f18, 0x7a01ef09), "Set Quadratic Attenuation", bb_set_quadratic_attenuation, NULL, NULL},
    {NMO_GUID_INIT(0x22252552, 0x00200020), "Set Light Range", bb_set_light_range, NULL, NULL},
    {NMO_GUID_INIT(0x32165a2f, 0x70000000), "Set Light Type", bb_set_light_type, NULL, NULL},
    {NMO_GUID_INIT(0x1ae11f1a, 0x1a1e1f11), "Set Specular Flag", bb_set_specular_flag, NULL, NULL},
    {NMO_GUID_INIT(0x01ed010b, 0x01fd010b), "Spot Light Properties", bb_spot_light_properties, NULL, NULL},
    {NMO_GUID_INIT(0x02580258, 0x02580258), "Set Light Target", bb_set_light_target, NULL, NULL},
    {NMO_GUID_INIT(0x20171e01, 0x234a1b62), "Light Color Progression", bb_light_color_progression,
     cb_light_color_progression, NULL},
    {NMO_GUID_INIT(0x4def3319, 0x075b5cc8), "Light Sprite", bb_light_sprite, cb_light_sprite, NULL},
};

nmo_status_t eng_register_lights(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_lights_blocks);
}
