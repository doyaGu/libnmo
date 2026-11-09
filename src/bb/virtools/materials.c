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
#include "object/nmo_class_ids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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
 * each new slot, One Loop Played at the end of each pass. Visuals' Sprite
 * Movie Player does the same with a sprite's.
 */
int virtools_movie_player(blk_t *b)
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
        eng_texture_set_slot(texture, slot);
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
    eng_texture_set_slot(texture, get_in_int(b, 0, 0));
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* RGBAFTOCOLOR(const VxColor *): ARGB, each channel times 255 truncated and kept in [0, 255] */
static uint32_t vxcolor_argb(const float c[4])
{
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] * 255.0f;
        uint32_t byte = !(v > -2147483648.0f && v < 2147483648.0f) ? 0u
                        : v >= 255.0f                               ? 255u
                        : v > 0.0f                                  ? (uint32_t)(int32_t)v
                                                                    : 0u;
        argb |= byte << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/* The current mesh of 3D entity `id`; 0 when it has none or is no 3D entity. */
static nmo_object_id_t entity_mesh(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    const nmo_3dentity_state_t *state =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    return state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
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
    eng_mesh_copy_t *mesh = eng_mesh_copy(engine, entity_mesh(engine, target));
    nmo_engine_mesh_t desc;
    if (mesh == NULL || !nmo_engine_mesh(engine, mesh->mesh, &desc)) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {1.0f, 1.0f, 1.0f, 0.5f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    if (mesh->colors == NULL && desc.vertex_count > 0) {
        mesh->colors = (uint32_t *)malloc((size_t)desc.vertex_count * sizeof(uint32_t));
    }
    uint32_t argb = rgbaf_to_color(color);
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


/* The target material; its change is reported as an effect by material_done. */
static eng_material_t *target_material(blk_t *b)
{
    return eng_material(b->engine, nmo_engine_block_target(b));
}

static int material_done(blk_t *b, const eng_material_t *material, bool io)
{
    if (io) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, material->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Ambient: CKMaterial::SetAmbient */
static int bb_set_ambient(blk_t *b)
{
    eng_material_t *material = target_material(b);
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    memcpy(material->desc.ambient, color, sizeof(color));
    return material_done(b, material, true);
}

/* Set Specular: the color, and the power when its input is given; out before the target check */
static int bb_set_specular(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = target_material(b);
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    memcpy(material->desc.specular, color, sizeof(color));
    float power = 1.0f;
    if (nmo_engine_block_get_input_value(b, 1, &power, sizeof(power)) == NMO_OK) {
        material->desc.power = power;
    }
    return material_done(b, material, false);
}

/* Set Specular Power (obsolete): CKMaterial::SetPower */
static int bb_set_specular_power(blk_t *b)
{
    eng_material_t *material = target_material(b);
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    material->desc.power = get_in_float(b, 0, 0.0f);
    return material_done(b, material, true);
}

/* Set Both Sided: CKMaterial::SetTwoSided; out before the target check */
static int bb_set_both_sided(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_material_t *material = target_material(b);
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    material->desc.two_sided = get_in_int(b, 0, 1) != 0;
    return material_done(b, material, false);
}

/* The material setters of one enum: which member, and the value without an input */
typedef enum {
    MATERIAL_FILL_MODE,
    MATERIAL_SHADE_MODE,
    MATERIAL_SOURCE_BLEND,
    MATERIAL_DEST_BLEND,
    MATERIAL_MAG_FILTER,
    MATERIAL_MIN_FILTER,
} material_member_t;

static int set_material_member(blk_t *b, material_member_t member, int32_t fallback)
{
    eng_material_t *material = target_material(b);
    if (material == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t value = get_in_int(b, 0, fallback);
    nmo_engine_material_t *desc = &material->desc;
    int32_t *members[] = {&desc->fill_mode,   &desc->shade_mode, &desc->source_blend,
                          &desc->dest_blend,  &desc->mag_filter, &desc->min_filter};
    *members[member] = value;
    return material_done(b, material, true);
}

/* Set Fill Mode: CKMaterial::SetFillMode (WireFrame without an input) */
static int bb_set_fill_mode(blk_t *b) { return set_material_member(b, MATERIAL_FILL_MODE, VXFILL_WIREFRAME); }

/* Set Shade Mode: CKMaterial::SetShadeMode (Flat without an input) */
static int bb_set_shade_mode(blk_t *b) { return set_material_member(b, MATERIAL_SHADE_MODE, VXSHADE_FLAT); }

/* Set Src Blend and Set Dest Blend (obsolete): CKMaterial::SetSourceBlend and SetDestBlend */
static int bb_set_src_blend(blk_t *b) { return set_material_member(b, MATERIAL_SOURCE_BLEND, VXBLEND_ONE); }

static int bb_set_dest_blend(blk_t *b) { return set_material_member(b, MATERIAL_DEST_BLEND, VXBLEND_ZERO); }

/* Set Texture Mag and Set Texture Min: the texture filters (Linear without an input) */
static int bb_set_texture_mag(blk_t *b)
{
    return set_material_member(b, MATERIAL_MAG_FILTER, VXTEXTUREFILTER_LINEAR);
}

static int bb_set_texture_min(blk_t *b)
{
    return set_material_member(b, MATERIAL_MIN_FILTER, VXTEXTUREFILTER_LINEAR);
}

/*
 * Set Color Key: CKBitmapData::SetTransparentColor with the color, then
 * SetTransparent; the image is keyed again.
 */
static int bb_set_color_key(blk_t *b)
{
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    texture->transparent_color = rgbaf_to_color(color);
    texture->transparent = get_in_int(b, 1, 1) != 0;
    eng_texture_changed(b->engine, texture->id);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture->id, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Material: a 3D sprite takes the material; another entity's current
 * mesh replaces Material to replace with it, or takes it on every face when
 * there is none to replace (CKMesh::ReplaceMaterial, ApplyGlobalMaterial).
 */
static int bb_set_material(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    if (eng_object_class(engine, target) == NMO_CID_SPRITE3D) {
        eng_sprite3d_t *sprite = eng_sprite3d(engine, target);
        if (sprite != NULL) {
            sprite->material = material;
        }
    } else {
        nmo_object_id_t mesh = entity_mesh(engine, target);
        if (mesh == 0) {
            return NMO_ENGINE_BR_OK;
        }
        nmo_object_id_t replaced = nmo_engine_block_get_input_object(b, 1);
        if (replaced != 0) {
            eng_mesh_replace_material(engine, mesh, replaced, material);
        } else {
            eng_mesh_apply_material(engine, mesh, material);
        }
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Material 3DSprite (obsolete): CKSprite3D::SetMaterial */
static int bb_set_material_3dsprite(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_sprite3d_t *sprite = target != 0 ? eng_sprite3d(b->engine, target) : NULL;
    if (sprite == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    sprite->material = nmo_engine_block_get_input_object(b, 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Lighting Mode: CKMesh::SetLitMode on the current mesh (Prelit without an input) */
static int bb_set_lighting_mode(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t mesh = entity_mesh(b->engine, target);
    if (mesh == 0 || !eng_mesh_set_lit_mode(b->engine, mesh, get_in_int(b, 0, 0))) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, mesh, 0);
    return NMO_ENGINE_BR_OK;
}

/* A mesh's flag set or cleared (CKMesh::SetWrapMode, SetTransparent) */
static void set_mesh_flag(nmo_engine_t *engine, eng_mesh_copy_t *copy, uint32_t flag, bool on)
{
    copy->flags = on ? copy->flags | flag : copy->flags & ~flag;
    copy->version = ++engine->render.mesh_version;
}

/*
 * Set Wrap Mode: CKMesh::SetWrapMode on the current mesh (None without an
 * input). Without a mesh CK has none to call; the block stops there.
 */
static int bb_set_wrap_mode(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_copy_t *copy = eng_mesh_copy(b->engine, entity_mesh(b->engine, target));
    if (copy == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t mode = get_in_int(b, 0, VXTEXTUREWRAP_NONE);
    set_mesh_flag(b->engine, copy, VXMESH_WRAPU, (mode & VXTEXTUREWRAP_U) != 0);
    set_mesh_flag(b->engine, copy, VXMESH_WRAPV, (mode & VXTEXTUREWRAP_V) != 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Transparent: the target mesh forced transparent by Set, not by UnSet */
static int bb_set_transparent(blk_t *b)
{
    eng_mesh_copy_t *copy = eng_mesh_copy(b->engine, nmo_engine_block_target(b));
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool set = IN_ACTIVE(b, 0);
    set_mesh_flag(b->engine, copy, VXMESH_FORCETRANSPARENCY, set);
    IN_OFF(b, set ? 0 : 1);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * The mesh the channel blocks act on: the target when it is a mesh, a 3D
 * entity's current mesh, none otherwise. Their out opens first.
 */
static eng_mesh_copy_t *channel_mesh(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    nmo_class_id_t cid = target != 0 ? eng_object_class(engine, target) : 0;
    nmo_object_id_t mesh = cid == NMO_CID_MESH || cid == NMO_CID_PATCHMESH ? target : entity_mesh(engine, target);
    return mesh != 0 ? eng_mesh_copy(engine, mesh) : NULL;
}

static int channel_done(blk_t *b, eng_mesh_copy_t *copy)
{
    copy->version = ++b->engine->render.mesh_version;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
    return NMO_ENGINE_BR_OK;
}

static nmo_engine_channel_t *mesh_channel_at(eng_mesh_copy_t *copy, int32_t channel)
{
    return channel >= 0 && (size_t)channel < copy->channels.count ? &copy->channels.data[channel] : NULL;
}

/* Activate Channel: CKMesh::ActivateChannel */
static int bb_activate_channel(blk_t *b)
{
    eng_mesh_copy_t *copy = channel_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_channel_t *channel = mesh_channel_at(copy, get_in_int(b, 0, 0));
    if (channel != NULL) {
        channel->active = get_in_int(b, 1, 0) != 0;
    }
    return channel_done(b, copy);
}

/*
 * Add Channel: CKMesh::AddChannel(material, TRUE): the material's channel,
 * or a new one on every face with a copy of the mesh's UVs (at most 16; -1
 * without a material or room).
 */
static int bb_add_channel(blk_t *b)
{
    eng_mesh_copy_t *copy = channel_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    int32_t channel = material != 0 ? eng_mesh_channel(copy, material) : -1;
    if (material != 0 && channel < 0 && copy->channels.count < 16) {
        nmo_engine_mesh_t desc;
        channel = nmo_engine_mesh(b->engine, copy->mesh, &desc) ? eng_mesh_add_channel(copy, material) : -1;
        float *uvs = channel >= 0 && desc.vertex_count > 0
                         ? (float *)malloc((size_t)desc.vertex_count * 2u * sizeof(float))
                         : NULL;
        for (int32_t v = 0; uvs != NULL && v < desc.vertex_count; v++) {
            uvs[v * 2] = copy->vertices[v].uv.x;
            uvs[v * 2 + 1] = copy->vertices[v].uv.y;
        }
        if (channel >= 0) {
            copy->channels.data[channel].uvs = uvs;
        }
    }
    nmo_engine_block_set_output_value(b, 0, &channel, sizeof(channel));
    return channel_done(b, copy);
}

/* Remove Channel: the channel at its index when not -1, else every channel of the material */
static int bb_remove_channel(blk_t *b)
{
    eng_mesh_copy_t *copy = channel_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t index = get_in_int(b, 0, -1);
    if (index > -1) {
        eng_mesh_remove_channel(copy, index);
        return channel_done(b, copy);
    }
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 1);
    for (size_t c = 0; material != 0 && c < copy->channels.count;) {
        if (copy->channels.data[c].material == material) {
            eng_mesh_remove_channel(copy, (int32_t)c);
        } else {
            c++;
        }
    }
    return channel_done(b, copy);
}

/* Set Channel Src Blend and Set Channel Dest Blend: a channel's blend factors */
static int set_channel_blend(blk_t *b, bool source)
{
    eng_mesh_copy_t *copy = channel_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t index = get_in_int(b, 0, 0);
    int32_t blend = get_in_int(b, 1, source ? VXBLEND_SRCALPHA : VXBLEND_INVSRCALPHA);
    nmo_engine_channel_t *channel = mesh_channel_at(copy, index);
    if (channel != NULL) {
        *(source ? &channel->source_blend : &channel->dest_blend) = blend;
    }
    return channel_done(b, copy);
}

static int bb_set_channel_src_blend(blk_t *b) { return set_channel_blend(b, true); }

static int bb_set_channel_dest_blend(blk_t *b) { return set_channel_blend(b, false); }

/* Set Channel Material: CKMesh::SetChannelMaterial */
static int bb_set_channel_material(blk_t *b)
{
    eng_mesh_copy_t *copy = channel_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_channel_t *channel = mesh_channel_at(copy, get_in_int(b, 0, 0));
    if (channel != NULL) {
        channel->material = nmo_engine_block_get_input_object(b, 1);
    }
    return channel_done(b, copy);
}

/*
 * The matrix the mapping blocks take the target's vertices through: into
 * the Mapping Referential's space (CK3dEntity::GetInverseWorldMatrix times
 * the target's world matrix), or into the world without one.
 */
static eng_matrix_t mapping_matrix(nmo_engine_t *engine, int32_t entity, int32_t referential)
{
    eng_matrix_t m = engine->entities.data[entity].world;
    if (referential >= 0) {
        eng_matrix_t inverse;
        eng_matrix_inverse(&inverse, &engine->entities.data[referential].world);
        eng_matrix_multiply(&m, &inverse, &m);
    }
    return m;
}

/* The target's current mesh copy and the UVs of the Channel input (-1 the mesh's own) */
typedef struct mapping {
    int32_t entity, referential;
    eng_mesh_copy_t *copy;
    float *uvs;
    size_t stride;
    int32_t count;
    int32_t channel;
} mapping_t;

static bool mapping_uvs(blk_t *b, nmo_object_id_t mesh, mapping_t *out)
{
    out->copy = mesh != 0 ? eng_mesh_copy(b->engine, mesh) : NULL;
    if (out->copy == NULL) {
        return false;
    }
    out->channel = get_in_int(b, 0, -1);
    out->uvs = eng_mesh_uvs(b->engine, out->copy, out->channel, &out->stride, &out->count);
    return out->uvs != NULL;
}

static float *uv_at(const mapping_t *m, int32_t v)
{
    return (float *)((uint8_t *)m->uvs + (size_t)v * m->stride);
}

static void mapping_done(blk_t *b, eng_mesh_copy_t *copy)
{
    copy->version = ++b->engine->render.mesh_version;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
}

static void normalize_vector(eng_vector_t *v)
{
    float length = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
    if (length > 0.0f) {
        v->x /= length, v->y /= length, v->z /= length;
    }
}

/* The angle about the Y axis the round mappings make U of, times `scale` */
static float around_y(const eng_vector_t *p, float scale)
{
    return (1.5707964f - atan2f(p->x, p->z)) * scale;
}

/*
 * Cylindrical Mapping: the target's vertices in the Mapping Referential's
 * space (needed) wrap the UVs about its Y axis: U the angle, V the height
 * (0.5 at the origin), both times UV Tile; Wrap Correction keeps U's
 * fraction.
 */
static int bb_cylindrical_mapping(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t entity = target != 0 ? eng_entity_index(engine, target) : -1;
    int32_t referential = input_entity(b, 1);
    mapping_t m;
    if (entity < 0 || referential < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t mesh = entity_mesh(engine, target);
    if (mesh == 0 || eng_mesh_copy(engine, mesh) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!mapping_uvs(b, mesh, &m)) {
        return NMO_ENGINE_BR_OK;
    }
    float tile[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 2, tile, sizeof(tile));
    bool wrap = get_in_int(b, 3, 1) != 0;
    eng_matrix_t matrix = mapping_matrix(engine, entity, referential);
    float u_scale = tile[0] * 0.15915494f, v_scale = tile[1] * 0.5f;
    for (int32_t v = 0; v < m.count; v++) {
        const nmo_vector_t *position = &m.copy->vertices[v].position;
        eng_vector_t p, local = {position->x, position->y, position->z};
        eng_vector_transform(&p, &matrix, &local);
        float *uv = uv_at(&m, v);
        uv[1] = 0.5f - v_scale * p.y;
        p.y = 0.0f;
        normalize_vector(&p);
        float u = around_y(&p, u_scale);
        uv[0] = wrap ? u - (float)(int32_t)u : u;
    }
    mapping_done(b, m.copy);
    return NMO_ENGINE_BR_OK;
}

/*
 * Spherical Mapping: the target's vertices, in the Mapping Referential's
 * space or the world's, wrap the UVs about a sphere: U the angle about Y,
 * V the angle from the top, both times UV Tile; Wrap Correction keeps U's
 * fraction.
 */
static int bb_spherical_mapping(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t entity = target != 0 ? eng_entity_index(engine, target) : -1;
    mapping_t m;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!mapping_uvs(b, entity_mesh(engine, target), &m)) {
        return NMO_ENGINE_BR_OK;
    }
    float tile[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 2, tile, sizeof(tile));
    bool wrap = get_in_int(b, 3, 1) != 0;
    eng_matrix_t matrix = mapping_matrix(engine, entity, input_entity(b, 1));
    float u_scale = tile[0] * 0.15915494f, v_scale = tile[1] * 0.31830987f;
    for (int32_t v = 0; v < m.count; v++) {
        const nmo_vector_t *position = &m.copy->vertices[v].position;
        eng_vector_t p, local = {position->x, position->y, position->z};
        eng_vector_transform(&p, &matrix, &local);
        normalize_vector(&p);
        p.y = !(p.y < 1.0f) ? 1.0f : p.y <= -1.0f ? -1.0f : p.y;
        float *uv = uv_at(&m, v);
        float u = around_y(&p, u_scale);
        uv[0] = wrap ? u - (float)(int32_t)u : u;
        uv[1] = (1.5707964f - asinf(p.y)) * v_scale;
    }
    mapping_done(b, m.copy);
    return NMO_ENGINE_BR_OK;
}

/*
 * Planar Mapping: the UVs are the target's vertices projected on the
 * Mapping Referential's XY plane (the world's without one), times half the
 * UV Tile about 0.5, V downward. With Exclude Faces, a channel whose
 * material clamps its texture keeps only the faces facing the projection
 * with a vertex inside it.
 */
static int bb_planar_mapping(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t entity = target != 0 ? eng_entity_index(engine, target) : -1;
    mapping_t m;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!mapping_uvs(b, entity_mesh(engine, target), &m)) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t referential = input_entity(b, 1);
    float tile[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 2, tile, sizeof(tile));
    eng_matrix_t matrix = mapping_matrix(engine, entity, referential);
    float su = tile[0] * 0.5f, sv = tile[1] * 0.5f;
    for (int r = 0; r < 3; r++) {
        matrix.m[r][0] *= su;
        matrix.m[r][1] = -(matrix.m[r][1] * sv);
    }
    matrix.m[3][0] = matrix.m[3][0] * su + 0.5f;
    matrix.m[3][1] = 0.5f - matrix.m[3][1] * sv;

    nmo_engine_material_t clamp;
    nmo_object_id_t channel_material =
        (size_t)m.channel < m.copy->channels.count ? m.copy->channels.data[m.channel].material : 0;
    bool exclude = m.channel >= 0 && get_local_int(b, 0, 0) != 0 && channel_material != 0 &&
                   nmo_engine_material(engine, channel_material, &clamp) &&
                   clamp.address_mode == VXTEXTURE_ADDRESSCLAMP;
    uint32_t *clips = exclude ? (uint32_t *)calloc((size_t)m.count, sizeof(uint32_t)) : NULL;
    for (int32_t v = 0; v < m.count; v++) {
        const nmo_vector_t *position = &m.copy->vertices[v].position;
        eng_vector_t p, local = {position->x, position->y, position->z};
        eng_vector_transform(&p, &matrix, &local);
        float *uv = uv_at(&m, v);
        uv[0] = p.x;
        uv[1] = p.y;
        if (clips != NULL) {
            clips[v] = (p.z < 0.0f ? 0x100u : 0u) | (p.x < 0.0f ? 0x10u : 0u) | (p.x > 1.0f ? 0x20u : 0u) |
                       (p.y < 0.0f ? 0x40u : 0u) | (p.y > 1.0f ? 0x80u : 0u);
        }
    }
    nmo_engine_mesh_t desc;
    nmo_face_t *faces = clips != NULL ? eng_mesh_copy_faces(engine, m.copy) : NULL;
    if (faces != NULL && m.channel < 16 && nmo_engine_mesh(engine, m.copy->mesh, &desc)) {
        /* The mapper's Z axis in the target's space (CK3dEntity::InverseTransformVector) */
        eng_vector_t z = {0.0f, 0.0f, 1.0f}, mapper;
        eng_entity_transform_vector(engine, entity, &mapper, &z, referential, true);
        uint16_t bit = (uint16_t)(1u << m.channel);
        for (int32_t f = 0; f < desc.face_count; f++) {
            const uint16_t *i = &desc.indices[3 * f];
            if (i[0] >= m.count || i[1] >= m.count || i[2] >= m.count) {
                continue;
            }
            const nmo_vector_t *p0 = &m.copy->vertices[i[0]].position, *p1 = &m.copy->vertices[i[1]].position,
                               *p2 = &m.copy->vertices[i[2]].position;
            eng_vector_t e1 = {p1->x - p0->x, p1->y - p0->y, p1->z - p0->z};
            eng_vector_t e2 = {p2->x - p0->x, p2->y - p0->y, p2->z - p0->z};
            eng_vector_t n = eng_vector_cross(&e1, &e2);
            normalize_vector(&n);
            bool outside = (clips[i[0]] & clips[i[1]] & clips[i[2]]) != 0;
            bool facing = n.x * mapper.x + n.y * mapper.y + n.z * mapper.z < 0.0f;
            faces[f].channel_mask = (uint16_t)(!outside && facing ? faces[f].channel_mask | bit
                                                                  : faces[f].channel_mask & ~bit);
        }
        m.copy->channels.data[m.channel].mask = bit;
    }
    free(clips);
    mapping_done(b, m.copy);
    return NMO_ENGINE_BR_OK;
}

/*
 * Copy Mapping: the Src Mesh's UVs of Src Channel, times UV Tile, become
 * the target mesh's of Dest Channel (as many as both meshes have vertices).
 */
static int bb_copy_mapping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, nmo_engine_block_target(b));
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t dest_channel = get_in_int(b, 0, -1);
    eng_mesh_copy_t *source = eng_mesh_copy(engine, nmo_engine_block_get_input_object(b, 1));
    if (source == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t source_channel = get_in_int(b, 2, -1);
    float tile[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 3, tile, sizeof(tile));
    size_t dest_stride = 0, source_stride = 0;
    int32_t dest_count = 0, source_count = 0;
    float *dest = eng_mesh_uvs(engine, copy, dest_channel, &dest_stride, &dest_count);
    const float *from = eng_mesh_uvs(engine, source, source_channel, &source_stride, &source_count);
    int32_t count = source_count < dest_count ? source_count : dest_count;
    for (int32_t v = 0; dest != NULL && from != NULL && v < count; v++) {
        float *to = (float *)((uint8_t *)dest + (size_t)v * dest_stride);
        const float *uv = (const float *)((const uint8_t *)from + (size_t)v * source_stride);
        to[0] = tile[0] * uv[0];
        to[1] = tile[1] * uv[1];
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    mapping_done(b, copy);
    return NMO_ENGINE_BR_OK;
}

/*
 * Texture Sine: each frame the owner mesh's UVs of Channel sway about the
 * UVs saved when the block was attached (its first local), each vertex a
 * step further along the sine; the second local is the time in seconds,
 * kept below one period of Velocity. A block without saved UVs saves the
 * mesh's own the first time it runs, as attaching it does.
 */
static int bb_texture_sine(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    float amplitude[2] = {get_in_float(b, 0, 0.1f), get_in_float(b, 1, 0.1f)};
    float velocity = get_in_float(b, 2, 1.0f);
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, nmo_engine_block_owner(b));
    int32_t channel = get_in_int(b, 3, -1);
    if (copy == NULL || channel < -1 || channel >= (int32_t)copy->channels.count) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    size_t stride = 0;
    int32_t count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &count);
    eng_param_t *saved = block_local(b, 0);
    if (saved != NULL && saved->size < (uint32_t)count * 2u * sizeof(float) && count > 0) {
        size_t own_stride = 0;
        int32_t own_count = 0;
        const float *own = eng_mesh_uvs(engine, copy, -1, &own_stride, &own_count);
        float *capture = (float *)malloc((size_t)own_count * 2u * sizeof(float));
        for (int32_t v = 0; capture != NULL && v < own_count; v++) {
            const float *uv = (const float *)((const uint8_t *)own + (size_t)v * own_stride);
            capture[v * 2] = uv[0];
            capture[v * 2 + 1] = uv[1];
        }
        if (capture != NULL) {
            nmo_engine_block_set_local_value(b, 0, capture, (size_t)own_count * 2u * sizeof(float));
        }
        free(capture);
        saved = block_local(b, 0);
    }
    const float *original = saved != NULL && saved->size >= (uint32_t)count * 2u * sizeof(float)
                                ? (const float *)saved->data
                                : NULL;
    float time = get_local_float(b, 1, 0.0f);
    for (int32_t v = 0; uvs != NULL && original != NULL && v < count; v++) {
        float angle = ((float)v / (float)count - 0.5f) * 4.0f + time * velocity;
        float *uv = (float *)((uint8_t *)uvs + (size_t)v * stride);
        uv[0] = cosf(angle) * (0.5f - original[v * 2]) * amplitude[0] + original[v * 2];
        uv[1] = sinf(angle) * (0.5f - original[v * 2 + 1]) * amplitude[1] + original[v * 2 + 1];
    }
    copy->version = ++engine->render.mesh_version;
    time += nmo_engine_block_delta_time(b) * 0.001f;
    if (time * velocity > 6.2831855f) {
        time -= 6.2831855f / velocity;
    }
    nmo_engine_block_set_local_value(b, 1, &time, sizeof(time));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
    return NMO_ENGINE_BR_OK;
}

static uint32_t rgba_to_argb(const uint8_t *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

static void argb_to_rgba(uint32_t argb, uint8_t *p)
{
    p[0] = (uint8_t)(argb >> 16), p[1] = (uint8_t)(argb >> 8), p[2] = (uint8_t)argb, p[3] = (uint8_t)(argb >> 24);
}

/* Write In Texture's Write Mode on one ARGB pixel: copy, average, add clamped, add */
static uint32_t write_pixel(int32_t mode, uint32_t dst, uint32_t src)
{
    uint32_t b[4], a[4];
    for (int i = 0; i < 4; i++) {
        b[i] = (dst >> (8 * i)) & 0xFFu;
        a[i] = (src >> (8 * i)) & 0xFFu;
    }
    switch (mode) {
    case 1:
        return ((a[0] + b[0]) >> 1) | (((a[1] + b[1]) >> 1) << 8) | (((a[2] + b[2]) >> 1) << 16) |
               (((a[3] + b[3]) >> 1) << 24);
    case 2: {
        uint32_t out = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t sum = a[i] + b[i];
            out |= (sum > 0xFFu ? 0xFFu : sum) << (8 * i);
        }
        return out;
    }
    case 3: /* the sums overflow into the next byte */
        return (a[0] + b[0]) | (((a[1] + b[1]) | (((a[2] + b[2]) | ((a[3] + b[3]) << 8)) << 8)) << 8);
    default:
        return src;
    }
}

static void write_at(eng_surface_t *surface, int32_t x, int32_t y, int32_t mode, uint32_t argb)
{
    uint8_t *p = &surface->rgba[((size_t)y * (size_t)surface->width + (size_t)x) * 4u];
    argb_to_rgba(write_pixel(mode, rgba_to_argb(p), argb), p);
}

/* A coordinate brought into [0, 1) by whole turns */
static float wrap_unit(float value)
{
    while (value < 0.0f) {
        value += 1.0f;
    }
    while (!(value < 1.0f)) {
        value -= 1.0f;
    }
    return value;
}

/*
 * Write In Texture: at UV of the target texture's current slot, the Pen
 * Texture centered there, or else a square of Color reaching Size pixels
 * each way (one pixel for 1), each pixel written by Write Mode (copy,
 * average, add clamped, add). Only 32-bit images; the engine's are.
 */
static int bb_write_in_texture(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    eng_texture_t *texture = eng_texture(engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float uv[2] = {0.0f, 0.0f}, color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, uv, sizeof(uv));
    nmo_engine_block_get_input_value(b, 1, color, sizeof(color));
    int32_t size = get_in_int(b, 2, 1);
    nmo_object_id_t pen_id = nmo_engine_block_get_input_object(b, 3);
    int32_t mode = get_in_int(b, 4, 0);
    mode = mode >= 0 && mode <= 3 ? mode : 0;
    uv[0] = wrap_unit(uv[0]);
    uv[1] = wrap_unit(uv[1]);
    texture = eng_texture(engine, nmo_engine_block_target(b));
    eng_surface_t *surface = eng_texture_surface(engine, texture, -1);
    if (surface == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t texture_id = texture->id;
    int32_t w = surface->width, h = surface->height;
    int32_t x = (int32_t)((float)w * uv[0]), y = (int32_t)((float)h * uv[1]);
    uint32_t argb = vxcolor_argb(color);
    if (pen_id != 0) {
        eng_texture_t *pen = eng_texture(engine, pen_id);
        const eng_surface_t *pen_surface = pen != NULL ? eng_texture_surface(engine, pen, -1) : NULL;
        /* the pen's state may have moved the target's */
        texture = eng_texture(engine, texture_id);
        surface = texture != NULL ? eng_texture_surface(engine, texture, -1) : NULL;
        if (pen_surface != NULL && surface != NULL) {
            int32_t pw = pen_surface->width, ph = pen_surface->height;
            int32_t x0 = x - pw / 2, y0 = y - ph / 2, x1 = pw / 2 + x - 1, y1 = ph / 2 + y - 1;
            int32_t skip_x = 0, skip_y = 0;
            if (x0 < 0) {
                skip_x = pw / 2 - x;
                x0 = 0;
            }
            if (y0 < 0) {
                skip_y = -y0;
                y0 = 0;
            }
            x1 = x1 >= w ? w - 1 : x1;
            y1 = y1 >= h ? h - 1 : y1;
            for (int32_t row = y0; row <= y1; row++) {
                const uint8_t *line =
                    &pen_surface->rgba[((size_t)(row - y0 + skip_y) * (size_t)pw + (size_t)skip_x) * 4u];
                for (int32_t col = 0; col < x1 - x0 + 1; col++) {
                    write_at(surface, x0 + col, row, mode, rgba_to_argb(&line[(size_t)col * 4u]));
                }
            }
        }
    } else if (size == 1) {
        if (x < w && y < h) {
            write_at(surface, x, y, mode, argb);
        }
    } else {
        int32_t x0 = x - size < 0 ? 0 : x - size, y0 = y - size < 0 ? 0 : y - size;
        int32_t x1 = x + size >= w ? w - 1 : x + size, y1 = y + size >= h ? h - 1 : y + size;
        for (int32_t row = y0; row <= y1; row++) {
            for (int32_t col = x0; col <= x1; col++) {
                write_at(surface, col, row, mode, argb);
            }
        }
    }
    eng_texture_changed(engine, texture_id);
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture_id, 0);
    return NMO_ENGINE_BR_OK;
}

/* The smallest power of two from n's lowest set bit up to n (0 for 0) */
static int32_t power_of_two_above(int32_t n)
{
    int32_t p = n & -n;
    while (p < n) {
        p *= 2;
    }
    return p;
}

/*
 * Change Texture Size: the target texture made Size (each side rounded up
 * to a power of two) by CKTexture::Create, an opaque black image, when
 * Clear is TRUE, else by scaling its images (CKBitmapData::ResizeImages).
 */
static int bb_change_texture_size(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float size[2] = {64.0f, 64.0f};
    nmo_engine_block_get_input_value(b, 0, size, sizeof(size));
    bool clear = get_in_int(b, 1, 1) != 0;
    if (size[0] < 0.0f || size[1] < 0.0f) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t width = power_of_two_above((int32_t)size[0]), height = power_of_two_above((int32_t)size[1]);
    if (clear) {
        eng_texture_create(b->engine, texture, width, height, 0);
    } else {
        eng_texture_resize(b->engine, texture, width, height);
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture->id, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set MipMap Texture: the Full Size Texture takes user mipmap levels
 * (CKTexture::SetUserMipMapMode), each following input's image copied into
 * one, from the first; each must be half the size of the one before, or
 * the block stops there with an error.
 */
static int bb_set_mipmap_texture(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t texture_id = nmo_engine_block_get_input_object(b, 0);
    eng_texture_t *texture = texture_id != 0 ? eng_texture(engine, texture_id) : NULL;
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_texture_use_user_levels(engine, texture);
    eng_surface_t *full = eng_texture_surface(engine, texture, -1);
    int32_t w = full != NULL ? full->width : 0, h = full != NULL ? full->height : 0;
    int32_t count = nmo_engine_block_input_parameter_count(b);
    for (int32_t i = 1; i < count; i++) {
        nmo_object_id_t level_id = nmo_engine_block_get_input_object(b, i);
        eng_texture_t *source = level_id != 0 ? eng_texture(engine, level_id) : NULL;
        if (source == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        const eng_surface_t *image = eng_texture_surface(engine, source, -1);
        w >>= 1;
        h >>= 1;
        if (image == NULL || image->width != w || image->height != h) {
            return NMO_ENGINE_BR_OWNERERROR; /* Wrong texture size */
        }
        texture = eng_texture(engine, texture_id);
        eng_surface_t *level = (size_t)(i - 1) < texture->levels.count ? &texture->levels.data[i - 1] : NULL;
        if (level != NULL && level->rgba != NULL) {
            memcpy(level->rgba, image->rgba, (size_t)level->width * (size_t)level->height * 4u);
        }
    }
    eng_texture_changed(engine, texture_id);
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture_id, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Environment Mapping: the target's next render maps its current mesh's UVs
 * of Channel from its normals as seen from the Projection Referential (the
 * camera without one), by a temporary pre-render callback the block adds
 * each time it runs.
 */
static int bb_environment_mapping(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_env_map_t map = {
        .entity = target,
        .behavior = b->node,
        .channel = get_in_int(b, 0, -1),
        .options = (uint32_t)get_in_int(b, 1, 0),
        .referential = nmo_engine_block_get_input_object(b, 2),
        .old_version = nmo_engine_block_version(b) < 0x00010005u,
    };
    eng_add_env_map(b->engine, &map);
    return NMO_ENGINE_BR_OK;
}

/*
 * Screen Mapping's render callback with Disable Perspective Correction
 * (the entity is drawn by it alone): the current mesh's vertices
 * transformed to the screen (CKRenderContext::TransformVertices), each
 * white, its UV where it falls in Screen UVs over the viewport (turned by
 * Rotation about the middle, the turned V from Screen UVs' bottom as the
 * DLL has it), with an rhw of 1, so that the texture is not perspective
 * corrected, drawn as triangles with the mesh's first material.
 */
static void screen_mapping_render(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)transparent;
    blk_t block = {engine, node};
    int32_t index = eng_entity_index(engine, entity);
    nmo_object_id_t mesh_id = entity_mesh(engine, entity);
    nmo_engine_mesh_t mesh;
    if (index < 0 || mesh_id == 0 || !nmo_engine_mesh(engine, mesh_id, &mesh) || mesh.vertices == NULL ||
        mesh.indices == NULL) {
        return;
    }
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(&block, 1, rect, sizeof(rect));
    float rotation = 0.0f;
    nmo_engine_block_get_input_value(&block, 2, &rotation, sizeof(rotation));
    int32_t viewport[4];
    eng_render_viewport(engine, viewport);
    /* VxRect::TransformFromHomogeneous: Screen UVs over the view rect */
    float left = (float)viewport[0] + rect[0] * (float)viewport[2];
    float top = (float)viewport[1] + rect[1] * (float)viewport[3];
    float right = (float)viewport[0] + rect[2] * (float)viewport[2];
    float bottom = (float)viewport[1] + rect[3] * (float)viewport[3];
    float inv_w = 1.0f / (right - left), inv_h = 1.0f / (bottom - top);
    bool turned = !(fabsf(rotation) <= 1.1920929e-07f);
    float c = cosf(rotation), sn = sinf(rotation);
    const eng_matrix_t *world = &engine->entities.data[index].world;
    eng_vector_t *screen = (eng_vector_t *)malloc((size_t)mesh.vertex_count * sizeof(*screen));
    if (screen == NULL) {
        return;
    }
    for (int32_t v = 0; v < mesh.vertex_count; v++) {
        const nmo_vector_t *p = &mesh.vertices[v].position;
        eng_vector_t local = {p->x, p->y, p->z}, at;
        eng_vector_transform(&at, world, &local);
        eng_render_project(engine, &at, &screen[v]);
    }
    int32_t first = (int32_t)engine->render.vertices.count;
    for (int32_t i = 0; i < mesh.face_count * 3; i++) {
        uint16_t at = mesh.indices[i];
        nmo_engine_vertex_t *vertex = NULL;
        if (at >= mesh.vertex_count || !ENG_VEC_PUSH(engine->render.vertices, vertex)) {
            continue;
        }
        const eng_vector_t *p = &screen[at];
        float u, w;
        if (turned) {
            float u0 = (p->x - left) * inv_w - 0.5f, w0 = (p->y - bottom) * inv_h - 0.5f;
            u = sn * w0 + u0 * c + 0.5f;
            w = w0 * c - u0 * sn + 0.5f;
        } else {
            u = (p->x - left) * inv_w;
            w = (p->y - top) * inv_h;
        }
        *vertex = (nmo_engine_vertex_t){p->x, p->y, p->z, u, w, 0xFFFFFFFFu, 0.0f, 0.0f, 0.0f};
    }
    free(screen);
    int32_t count = (int32_t)engine->render.vertices.count - first;
    nmo_engine_draw_t *draw = count > 0 ? eng_render_push(engine) : NULL;
    if (draw == NULL) {
        return;
    }
    draw->kind = NMO_ENGINE_DRAW_PRIMITIVE;
    draw->object = entity;
    draw->first_vertex = first;
    draw->vertex_count = count;
    draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
    draw->material = nmo_engine_mesh_material(engine, mesh_id, 0);
    draw->screen = true;
}

/* The render callback, when the mesh has one material ("Is Forcing" set); false and a
   console line otherwise */
static bool screen_mapping_force(blk_t *b, nmo_object_id_t target)
{
    nmo_engine_mesh_t mesh;
    nmo_object_id_t mesh_id = entity_mesh(b->engine, target);
    if (mesh_id == 0 || !nmo_engine_mesh(b->engine, mesh_id, &mesh)) {
        return true; /* nothing to do */
    }
    if (mesh.material_count != 1) {
        eng_console(b->engine, "There must be only one material for disabling perspective correction", true);
        return false;
    }
    eng_entity_callback_t callback = {target, ENG_ENTITY_RENDER, screen_mapping_render, b->node, false};
    eng_add_entity_callback(b->engine, &callback);
    set_local_int(b, 1, 1);
    return true;
}

static void screen_mapping_unforce(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (get_local_int(b, 1, 0) != 0 && target != 0) {
        eng_remove_entity_callback(b->engine, target, ENG_ENTITY_RENDER, screen_mapping_render, b->node);
        set_local_int(b, 1, 0);
    }
}

/* The callback: the load (and a settings edit) forces the render with Disable Perspective
   Correction, or stops it without; Pause, Reset, the script's deactivation and a new scene
   the script is not active in stop it */
static int cb_screen_mapping(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD || message == 3u || message == 13u) { /* attach, settings edited */
        if (get_local_int(b, 0, 0) == 0) {
            screen_mapping_unforce(b);
            return NMO_ENGINE_BR_OK;
        }
        if (get_local_int(b, 1, 0) != 0) {
            return NMO_ENGINE_BR_OK;
        }
        nmo_object_id_t target = nmo_engine_block_target(b);
        if (target == 0) {
            return NMO_ENGINE_BR_OWNERERROR;
        }
        return screen_mapping_force(b, target) ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
    }
    if (message == 4u || message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_RESET ||
        message == NMO_ENGINE_MSG_DEACTIVATESCRIPT ||
        (message == NMO_ENGINE_MSG_NEWSCENE && !nmo_engine_parent_script_is_active(b->engine, block_node(b)->id))) {
        screen_mapping_unforce(b);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Screen Mapping: the target's current mesh's UVs of Channel are its
 * vertices as the render context's camera projects them (each divided by
 * its depth through a perspective camera), placed in Screen UVs and turned
 * by Rotation, centered on 0. With Disable Perspective Correction it forces
 * its render callback instead, once.
 */
static int bb_screen_mapping(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t entity = target != 0 ? eng_entity_index(engine, target) : -1;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (entity_mesh(engine, target) == 0 || get_local_int(b, 1, 0) != 0) {
        return NMO_ENGINE_BR_OK;
    }
    if (get_local_int(b, 0, 0) != 0) {
        return screen_mapping_force(b, target) ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
    }
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, entity_mesh(engine, target));
    nmo_object_id_t camera = engine->render.camera;
    eng_matrix_t view_projection;
    if (copy == NULL || camera == 0 ||
        !eng_camera_view_projection(engine, camera, &view_projection, NULL)) {
        return NMO_ENGINE_BR_OK;
    }
    eng_matrix_t matrix;
    eng_matrix_multiply4(&matrix, &view_projection, &engine->entities.data[entity].world);
    uint32_t projection = 0;
    eng_camera_projection_type(engine, camera, &projection);
    bool perspective = projection != 2u; /* CK_ORTHOGRAPHICPROJECTION */
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 1, rect, sizeof(rect));
    float rotation = get_in_float(b, 2, 0.0f);
    float half_w = (rect[2] - rect[0]) * 0.5f, half_h = (rect[3] - rect[1]) * 0.5f;
    size_t stride = 0;
    int32_t count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, get_in_int(b, 0, -1), &stride, &count);
    float c = cosf(rotation), sn = sinf(rotation);
    bool turned = !(fabsf(rotation) <= 1.1920929e-07f);
    for (int32_t v = 0; uvs != NULL && v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        float h[3];
        for (int i = 0; i < 3; i++) {
            h[i] = p->x * matrix.m[0][i] + p->y * matrix.m[1][i] + p->z * matrix.m[2][i] + matrix.m[3][i];
        }
        float k = perspective ? fabsf(1.0f / h[2]) : 1.0f;
        float *uv = (float *)((uint8_t *)uvs + (size_t)v * stride);
        float u = k * h[0] * half_w + rect[0], w = rect[1] - k * h[1] * half_h;
        if (turned) {
            uv[0] = sn * w + u * c - 0.5f;
            uv[1] = w * c - u * sn - 0.5f;
        } else {
            uv[0] = u - 0.5f;
            uv[1] = w - 0.5f;
        }
    }
    copy->version = ++engine->render.mesh_version;
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_materials_blocks[] = {
    {NMO_GUID_INIT(0xf56d92ef, 0x41eb20c1), "Change Texture Video Format",
     bb_change_texture_video_format, NULL, NULL},
    {NMO_GUID_INIT(0x778d16d4, 0x1dd60060), "Movie Player", virtools_movie_player, NULL, NULL},
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
    {NMO_GUID_INIT(0xaaaaaaaa, 0xaeae1287), "Set Ambient", bb_set_ambient, NULL, NULL},
    {NMO_GUID_INIT(0x20202020, 0x30303030), "Set Specular", bb_set_specular, NULL, NULL},
    {NMO_GUID_INIT(0x45454545, 0x15151515), "Set Specular Power", bb_set_specular_power, NULL, NULL},
    {NMO_GUID_INIT(0x76319845, 0x48593399), "Set Both Sided", bb_set_both_sided, NULL, NULL},
    {NMO_GUID_INIT(0xc5c4c3c2, 0x8c8c8c8c), "Set Fill Mode", bb_set_fill_mode, NULL, NULL},
    {NMO_GUID_INIT(0x95195195, 0x98795198), "Set Shade Mode", bb_set_shade_mode, NULL, NULL},
    {NMO_GUID_INIT(0x40234ddc, 0x012da700), "Set Src Blend", bb_set_src_blend, NULL, NULL},
    {NMO_GUID_INIT(0x036d1a87, 0xdf0a670c), "Set Dest Blend", bb_set_dest_blend, NULL, NULL},
    {NMO_GUID_INIT(0xda495631, 0xde155488), "Set Texture Mag", bb_set_texture_mag, NULL, NULL},
    {NMO_GUID_INIT(0x12589321, 0x32587123), "Set Texture Min", bb_set_texture_min, NULL, NULL},
    {NMO_GUID_INIT(0x41bb5af1, 0x01eb26e9), "Set Color Key", bb_set_color_key, NULL, NULL},
    {NMO_GUID_INIT(0x37865d50, 0x2e285fab), "Set Material", bb_set_material, NULL, NULL},
    {NMO_GUID_INIT(0x3ddd6d33, 0xa54d5c48), "Set Material 3DSprite", bb_set_material_3dsprite, NULL, NULL},
    {NMO_GUID_INIT(0x45899665, 0x45211254), "Set Lighting Mode", bb_set_lighting_mode, NULL, NULL},
    {NMO_GUID_INIT(0x12a5498d, 0x65657512), "Set Wrap Mode", bb_set_wrap_mode, NULL, NULL},
    {NMO_GUID_INIT(0x2fc37564, 0x299a2c9d), "Set Transparent", bb_set_transparent, NULL, NULL},
    {NMO_GUID_INIT(0x55566666, 0x66666444), "Activate Channel", bb_activate_channel, NULL, NULL},
    {NMO_GUID_INIT(0x89658965, 0x36654789), "Add Channel", bb_add_channel, NULL, NULL},
    {NMO_GUID_INIT(0x4a462e58, 0x022e369b), "Remove Channel", bb_remove_channel, NULL, NULL},
    {NMO_GUID_INIT(0x3b9b0e7f, 0xb1316800), "Set Channel Src Blend", bb_set_channel_src_blend, NULL, NULL},
    {NMO_GUID_INIT(0x2f5a0eb9, 0x27067e93), "Set Channel Dest Blend", bb_set_channel_dest_blend, NULL, NULL},
    {NMO_GUID_INIT(0x2e0a6b92, 0x366a3f9b), "Set Channel Material", bb_set_channel_material, NULL, NULL},
    {NMO_GUID_INIT(0x770a5fc9, 0x7bbe2aed), "Cylindrical Mapping", bb_cylindrical_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x5e40778e, 0x44f36cf3), "Spherical Mapping", bb_spherical_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x2a6a3fa3, 0x3280407d), "Planar Mapping", bb_planar_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x086a487c, 0x3cd44acb), "Copy Mapping", bb_copy_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x4fe0646e, 0x7e0635c9), "Texture Sine", bb_texture_sine, NULL, NULL},
    {NMO_GUID_INIT(0x1d57024d, 0x7c064238), "Write In Texture", bb_write_in_texture, NULL, NULL},
    {NMO_GUID_INIT(0x7a7c4d7b, 0x468d0f33), "Change Texture Size", bb_change_texture_size, NULL, NULL},
    {NMO_GUID_INIT(0x6e407fcf, 0x0eff06a5), "Set MipMap Texture", bb_set_mipmap_texture, NULL, NULL},
    {NMO_GUID_INIT(0x10d1c997, 0x99dabef4), "Environment Mapping", bb_environment_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x58ef12ca, 0x26805940), "Screen Mapping", bb_screen_mapping, cb_screen_mapping, NULL},
};

nmo_status_t eng_register_materials(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_materials_blocks);
}
