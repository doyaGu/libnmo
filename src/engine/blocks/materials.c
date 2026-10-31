/**
 * @file materials.c
 * @brief Ports of the Materials-Textures building blocks that keep state.
 *
 * Each function does what the block's function in Materials.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Movie Player moves through a movie's frames as it plays.
 */

#include "../engine_block_util.h"

#include "type/nmo_type_guids.h"

#include <stdlib.h>

/*
 * Change Texture Video Format: the texture's format in video memory. The
 * engine's render driver has every format, so Success for one of the 14.
 */
static int bb_change_texture_video_format(blk_t *b)
{
    if (!IN_ACTIVE(b, 0)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    int32_t format = get_in_int(b, 0, 0);
    if (format < 0 || format >= 14) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t texture = nmo_engine_block_target(b);
    if (texture == 0) {
        return NMO_ENGINE_BR_BEHAVIORERROR;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Movie Player (MoviePlayer): the target texture's slots from Starting Slot
 * to Ending Slot (all of them when both are 0) over Duration, Synchro Out at
 * each new slot, One Loop Played at the end of each pass.
 */
static int bb_movie_player(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *texture = target != 0 ? eng_texture(b->engine, target) : NULL;
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float duration = 5000.0f;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_INT)) {
        duration = (float)get_in_int(b, 0, 5000); /* back compatibility */
    } else {
        duration = get_in_float(b, 0, 5000.0f);
    }
    if (duration == 0.0f) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t capacity = texture->slot_count;
    if (capacity == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t start = get_in_int(b, 2, 0);
    start = start < 0 ? 0 : start > capacity - 1 ? capacity : start;
    int32_t end = get_in_int(b, 3, 0);
    end = end < 0 ? 0 : end > capacity - 1 ? capacity : end;
    int32_t slots = start == 0 && end == 0 ? capacity : abs(end - start);
    float rate = (float)slots / duration;

    float time = 0.0f;
    int32_t old_slot = -1;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    } else {
        nmo_engine_block_get_local_value(b, 0, &time, sizeof(time));
        old_slot = (int32_t)(time * rate);
        time += nmo_engine_block_delta_time(b);
    }
    if (time >= duration) {
        OUT_ON(b, 1);
        if (get_in_int(b, 1, 1) == 0) {
            return NMO_ENGINE_BR_OK;
        }
        do {
            time -= duration;
        } while (time >= duration);
    }
    nmo_engine_block_set_local_value(b, 0, &time, sizeof(time));
    int32_t new_slot = (int32_t)(time * rate);
    if (new_slot != old_slot) {
        int32_t slot = start <= end ? start + new_slot : start - 1 - new_slot;
        texture->current_slot = slot;
        nmo_engine_block_set_output_value(b, 0, &slot, sizeof(slot));
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Set Diffuse: the color, with the material's own alpha when Keep Alpha is TRUE */
static int bb_set_diffuse(blk_t *b)
{
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    if (get_in_int(b, 1, 0) != 0) {
        color[3] = material->desc.diffuse[3];
    } else if (nmo_engine_block_version(b) < 0x00020000) {
        /* Version 1 blends a translucent color */
        if (color[3] < 1.0f) {
            material->desc.alpha_blend = true;
            material->desc.source_blend = VXBLEND_SRCALPHA;
            material->desc.dest_blend = VXBLEND_INVSRCALPHA;
        }
        if (color[3] == 1.0f) {
            material->desc.alpha_blend = false;
        }
    }
    memcpy(material->desc.diffuse, color, sizeof(color));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_set_emissive(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    memcpy(material->desc.emissive, color, sizeof(color));
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Texture: the texture when its input has a source, and the address mode when given */
static int bb_set_texture(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (input_source(b, 0, false) != NULL) {
        material->desc.texture = nmo_engine_block_get_input_object(b, 0);
    }
    int32_t address = VXTEXTURE_ADDRESSWRAP;
    if (nmo_engine_block_get_input_value(b, 2, &address, sizeof(address)) == NMO_OK) {
        material->desc.address_mode = address;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_materials_blocks[] = {
    {NMO_GUID(0xf56d92ef, 0x41eb20c1), "Change Texture Video Format",
     bb_change_texture_video_format, NULL, NULL},
    {NMO_GUID(0x778d16d4, 0x1dd60060), "Movie Player", bb_movie_player, NULL, NULL},
    {NMO_GUID(0xe1e1e1e1, 0x1e1e1e1e), "Set Diffuse", bb_set_diffuse, NULL, NULL},
    {NMO_GUID(0xd1d1d1d1, 0x30303030), "Set Emissive", bb_set_emissive, NULL, NULL},
    {NMO_GUID(0xeb123eb5, 0x5be321be), "Set Texture", bb_set_texture, NULL, NULL},
};

nmo_status_t eng_register_materials(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_materials_blocks);
}
