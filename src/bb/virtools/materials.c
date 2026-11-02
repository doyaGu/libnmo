/**
 * @file materials.c
 * @brief Ports of the Materials-Textures building blocks that keep state.
 *
 * Each function does what the block's function in Materials.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Movie Player moves through a movie's frames as it plays.
 */

#include "virtools.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

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

/* Set MipMap Level: the target texture with its mipmaps or without (CKTexture::UseMipmap) */
static int bb_set_mipmap_level(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t texture = nmo_engine_block_target(b);
    if (texture == 0) {
        return NMO_ENGINE_BR_OK;
    }
    eng_texture_use_mipmap(b->engine, texture, get_in_int(b, 0, 1) != 0 ? 1 : 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Alpha Test: each input given changes the material's alpha test */
static int bb_set_alpha_test(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t value = 0;
    if (nmo_engine_block_get_input_value(b, 0, &value, sizeof(value)) == NMO_OK) {
        material->desc.alpha_test = value != 0;
    }
    if (nmo_engine_block_get_input_value(b, 1, &value, sizeof(value)) == NMO_OK) {
        material->desc.alpha_func = value;
    }
    if (nmo_engine_block_get_input_value(b, 2, &value, sizeof(value)) == NMO_OK) {
        material->desc.alpha_ref = (uint8_t)value;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Blend Modes: blending, its factors and the texture blend, each when given */
static int bb_set_blend_modes(blk_t *b)
{
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t value = 0;
    if (nmo_engine_block_get_input_value(b, 0, &value, sizeof(value)) == NMO_OK) {
        material->desc.alpha_blend = value != 0;
    }
    if (nmo_engine_block_get_input_value(b, 1, &value, sizeof(value)) == NMO_OK) {
        material->desc.source_blend = value;
    }
    if (nmo_engine_block_get_input_value(b, 2, &value, sizeof(value)) == NMO_OK) {
        material->desc.dest_blend = value;
    }
    if (nmo_engine_block_get_input_value(b, 3, &value, sizeof(value)) == NMO_OK) {
        material->desc.texture_blend = value;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Material Z Buffer: Z writes and the Z compare function */
static int bb_set_material_z_buffer(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    material->desc.z_write = get_in_int(b, 0, 1) != 0;
    material->desc.z_func = get_in_int(b, 1, VXCMP_LESSEQUAL);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Current Slot (ChangeTextureSlot): CKTexture::SetCurrentSlot, for a slot it has */
static int bb_set_current_slot(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t slot = get_in_int(b, 0, 0);
    if (slot >= 0 && slot < texture->slot_count) {
        texture->current_slot = slot;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture->id, 0);
    return NMO_ENGINE_BR_OK;
}

static uint32_t color_argb(const float c[4])
{
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        argb |= (uint32_t)(v * 255.0f + 0.5f) << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/*
 * Set Prelit Color: the target's current mesh becomes prelit, every vertex
 * the Filtering Color (the Additional Color is its specular, which the
 * engine does not keep).
 */
static int bb_set_prelit_color(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    nmo_object_t *object = target != 0 ? eng_find_object(engine, target) : NULL;
    const nmo_3dentity_state_t *state =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    if (state == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_copy_t *mesh = eng_mesh_copy(engine, eng_object_ref(engine, target, &state->current_mesh));
    nmo_engine_mesh_t desc;
    if (mesh == NULL || !nmo_engine_mesh(engine, mesh->mesh, &desc)) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {1.0f, 1.0f, 1.0f, 0.5f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    if (mesh->colors == NULL && desc.vertex_count > 0) {
        mesh->colors = (uint32_t *)malloc((size_t)desc.vertex_count * sizeof(uint32_t));
    }
    uint32_t argb = color_argb(color);
    for (int32_t v = 0; mesh->colors != NULL && v < desc.vertex_count; v++) {
        mesh->colors[v] = argb;
    }
    mesh->prelit = true;
    mesh->version = ++engine->render.mesh_version;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Texture Scroller: the target mesh's UVs (channel -1, the base ones) move by
 * the Scroll Vector, less the whole part of the first vertex's, so they stay
 * near the texture. The engine keeps the base UVs only.
 */
static int bb_texture_scroller(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_mesh_copy_t *mesh = eng_mesh_copy(b->engine, nmo_engine_block_target(b));
    nmo_engine_mesh_t desc;
    if (mesh == NULL || !nmo_engine_mesh(b->engine, mesh->mesh, &desc)) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float scroll[2] = {0.1f, 0.1f};
    nmo_engine_block_get_input_value(b, 0, scroll, sizeof(scroll));
    if (get_in_int(b, 1, -1) != -1 || desc.vertex_count == 0) {
        return NMO_ENGINE_BR_OK;
    }
    scroll[0] -= (float)(int)mesh->vertices[0].uv.x;
    scroll[1] -= (float)(int)mesh->vertices[0].uv.y;
    for (int32_t v = 0; v < desc.vertex_count; v++) {
        mesh->vertices[v].uv.x += scroll[0];
        mesh->vertices[v].uv.y += scroll[1];
    }
    mesh->version = ++b->engine->render.mesh_version;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, mesh->mesh, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_materials_blocks[] = {
    {NMO_GUID_INIT(0xf56d92ef, 0x41eb20c1), "Change Texture Video Format",
     bb_change_texture_video_format, NULL, NULL},
    {NMO_GUID_INIT(0x778d16d4, 0x1dd60060), "Movie Player", bb_movie_player, NULL, NULL},
    {NMO_GUID_INIT(0xe1e1e1e1, 0x1e1e1e1e), "Set Diffuse", bb_set_diffuse, NULL, NULL},
    {NMO_GUID_INIT(0xd1d1d1d1, 0x30303030), "Set Emissive", bb_set_emissive, NULL, NULL},
    {NMO_GUID_INIT(0xeb123eb5, 0x5be321be), "Set Texture", bb_set_texture, NULL, NULL},
    {NMO_GUID_INIT(0x3ebe40f2, 0x3fa41377), "Set MipMap Level", bb_set_mipmap_level, NULL, NULL},
    {NMO_GUID_INIT(0x1cb5661e, 0x5b5d3fda), "Set Alpha Test", bb_set_alpha_test, NULL, NULL},
    {NMO_GUID_INIT(0x2f572a66, 0x6a9a0088), "Set Blend Modes", bb_set_blend_modes, NULL, NULL},
    {NMO_GUID_INIT(0x1144022f, 0x68fd055c), "Set Material Z Buffer", bb_set_material_z_buffer, NULL,
     NULL},
    {NMO_GUID_INIT(0xaaaa213a, 0xeaa8d52a), "Set Current Slot", bb_set_current_slot, NULL, NULL},
    {NMO_GUID_INIT(0x60415d44, 0x0d0174ea), "Set Prelit Color", bb_set_prelit_color, NULL, NULL},
    {NMO_GUID_INIT(0xf11d010a, 0xfb1d010a), "Texture Scroller", bb_texture_scroller, NULL, NULL},
};

nmo_status_t eng_register_materials(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_materials_blocks);
}
