/**
 * @file render.c
 * @brief The render state the blocks change, and the frame a host draws.
 *
 * Objects, materials, 2D entities and lights start as the file has them and
 * keep the changes blocks make (CKObject::Show, CKMaterial::SetDiffuse,
 * CK2dEntity::SetMaterial, CKLight::SetColor, ...). nmo_engine_render_frame()
 * walks them as CKRenderedScene::Draw does (CK2_3D.dll, after Ballanced's
 * reimplementation): the background 2D entities, the 3D entities of the
 * level scene, the foreground 2D entities, each 2D tree in Z order with its
 * extents clipped to the render context (CK2dEntity::UpdateExtents).
 */

#include "../engine_block_util.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_image.h"
#include "format/nmo_stb_adapter.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* CK_OBJECT_FLAGS the render state keeps, and CK_OBJECT_SHOWOPTION */
#define OBJECT_VISIBLE 0x40
#define OBJECT_HIERARCHICAL_HIDE 0x200
#define SHOW_SHOW 1
/* VXCHANNEL_FLAGS */
#define VXCHANNEL_ACTIVE 0x00000001u
#define VXCHANNEL_SAMEUV 0x00800000u
#define VXCHANNEL_NOTLIT 0x01000000u
#define SHOW_HIERARCHICAL_HIDE 2

void eng_free_render(nmo_engine_t *engine)
{
    eng_render_t *render = &engine->render;
    free(render->shown.slots);
    free(render->materials.data);
    free(render->material_map.slots);
    free(render->entities_2d.data);
    free(render->entity_2d_map.slots);
    free(render->lights.data);
    free(render->light_map.slots);
    free(render->sprites_3d.data);
    free(render->sprite_3d_map.slots);
    for (size_t i = 0; i < render->images.count; i++) {
        free(render->images.data[i].rgba);
    }
    free(render->images.data);
    for (size_t i = 0; i < render->user_mipmaps.count; i++) {
        free(render->user_mipmaps.data[i].rgba);
    }
    free(render->user_mipmaps.data);
    for (size_t i = 0; i < render->meshes.count; i++) {
        eng_mesh_t *mesh = &render->meshes.data[i];
        free(mesh->vertices);
        free(mesh->colors);
        free(mesh->indices);
        free(mesh->faces);
        free(mesh->materials);
    }
    free(render->meshes.data);
    for (size_t i = 0; i < render->mesh_copies.count; i++) {
        eng_mesh_copy_t *copy = &render->mesh_copies.data[i];
        free(copy->vertices);
        free(copy->colors);
        for (size_t c = 0; c < copy->channels.count; c++) {
            free((void *)copy->channels.data[c].uvs);
        }
        free(copy->channels.data);
        free(copy->faces);
        free(copy->groups);
    }
    free(render->mesh_copies.data);
    free(render->camera_states.data);
    free(render->camera_targets.data);
    free(render->vertices.data);
    free(render->draws.data);
    free(render->frame_lights.data);
    free(render->env_maps.data);
    free(render->passes.data);
    free(render->pass_firsts.data);
    free(render->view_requests.data);
    free(render->views.data);
    free(render->view_firsts.data);
    free(render->entity_callbacks.data);
}

eng_mesh_t *eng_render_mesh(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; id != 0 && i < engine->render.meshes.count; i++) {
        if (engine->render.meshes.data[i].id == id) {
            return &engine->render.meshes.data[i];
        }
    }
    return NULL;
}

nmo_engine_draw_t *eng_render_push(nmo_engine_t *engine)
{
    nmo_engine_draw_t *draw = NULL;
    if (!ENG_VEC_PUSH(engine->render.draws, draw)) {
        return NULL;
    }
    draw->stencil = engine->render.stencil;
    return draw;
}

void eng_render_apply_scene(nmo_engine_t *engine, uint32_t background_color,
                            nmo_object_id_t background_texture, uint32_t ambient_light,
                            int32_t fog_mode, uint32_t fog_color, float fog_start,
                            float fog_end, float fog_density, nmo_object_id_t camera)
{
    eng_render_t *render = &engine->render;
    if (camera != 0) {
        render->camera = camera;
    }
    render->background_color = background_color;
    render->background_texture = background_texture;
    render->fog_color = fog_color;
    render->fog_mode = fog_mode;
    render->fog_start = fog_start;
    render->fog_end = fog_end;
    render->fog_density = fog_density;
    render->ambient_light = ambient_light;
}

eng_render_t *eng_render_settings(nmo_engine_t *engine)
{
    eng_render_t *render = &engine->render;
    if (!render->settings_set) {
        render->settings_set = true;
        render->render_options = CK_RENDER_DEFAULTSETTINGS;
        render->shading = 2; /* GouraudShading */
        render->use_textures = true;
        render->wireframe_layer = false;
    }
    return render;
}

nmo_object_id_t eng_object_ref(const nmo_engine_t *engine, nmo_object_id_t owner,
                               const nmo_ref_t *ref)
{
    nmo_object_id_t local = nmo_ref_runtime_id(ref);
    if (local == 0) {
        return 0;
    }
    nmo_object_id_t id = eng_module_id(eng_module_of(engine, owner, NULL), local);
    return eng_find_object(engine, id) != NULL && eng_map_get(&engine->destroyed, id) < 0 ? id : 0;
}

/* The state of the object's class or the one it derives from, as the file has it */
static const void *eng_typed_state(const nmo_engine_t *engine, nmo_object_id_t id, nmo_guid_t guid)
{
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    return object != NULL
               ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, guid)
               : NULL;
}

void eng_sprite3d_from_state(const nmo_engine_t *engine, nmo_object_id_t id,
    const nmo_sprite3d_state_t *state, eng_sprite3d_t *out)
{
    *out = (eng_sprite3d_t){.id=id, .material=state->has_data ? eng_object_ref(engine,id,&state->material) : 0,
        .mode=state->mode, .offset=state->offset, .uv_rect=state->uv_rect,
        .min={(state->offset.x-1)*state->half_width, (state->offset.y-1)*state->half_height},
        .max={(state->offset.x+1)*state->half_width, (state->offset.y+1)*state->half_height}};
}

eng_sprite3d_t *eng_sprite3d(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (eng_map_get(&engine->destroyed,id) >= 0) return NULL;
    const nmo_sprite3d_state_t *state = eng_typed_state(engine,id,CKPGUID_SPRITE3D);
    if (state == NULL) return NULL;
    int32_t index = eng_map_get(&engine->render.sprite_3d_map,id);
    if (index >= 0) return &engine->render.sprites_3d.data[index];
    eng_sprite3d_t *sprite = NULL;
    index = (int32_t)engine->render.sprites_3d.count;
    if (!ENG_VEC_PUSH(engine->render.sprites_3d,sprite)) return NULL;
    if (!eng_map_put(&engine->render.sprite_3d_map,id,index)) {
        engine->render.sprites_3d.count--;
        return NULL;
    }
    eng_sprite3d_from_state(engine,id,state,sprite);
    return sprite;
}

bool eng_sprite3d_set_size(nmo_engine_t *engine, nmo_object_id_t id, nmo_vector2_t size)
{
    eng_sprite3d_t *sprite = eng_sprite3d(engine,id);
    if (sprite == NULL) return false;
    float half_width = size.x*.5f, half_height = size.y*.5f;
    sprite->min = (nmo_vector2_t){(sprite->offset.x-1)*half_width, (sprite->offset.y-1)*half_height};
    sprite->max = (nmo_vector2_t){(sprite->offset.x+1)*half_width, (sprite->offset.y+1)*half_height};
    return true;
}

/* CKSprite3D::SetOffset: the box keeps its size around the new offset. */
bool eng_sprite3d_set_offset(nmo_engine_t *engine, nmo_object_id_t id, nmo_vector2_t offset)
{
    eng_sprite3d_t *sprite = eng_sprite3d(engine, id);
    if (sprite == NULL) {
        return false;
    }
    float half_width = (sprite->max.x - sprite->min.x) * 0.5f;
    float half_height = (sprite->max.y - sprite->min.y) * 0.5f;
    sprite->offset = offset;
    sprite->min = (nmo_vector2_t){(offset.x - 1.0f) * half_width, (offset.y - 1.0f) * half_height};
    sprite->max = (nmo_vector2_t){(offset.x + 1.0f) * half_width, (offset.y + 1.0f) * half_height};
    return true;
}

void eng_copy_sprite3d(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count)
{
    const eng_sprite3d_t *sprite = eng_sprite3d(engine,source);
    if (sprite == NULL) return;
    eng_sprite3d_t saved = *sprite;
    eng_sprite3d_t *copy = eng_sprite3d(engine,target);
    if (copy == NULL) return;
    *copy = saved; copy->id = target;
    for (size_t i = 0; i < count; i++) if (copy->material == originals[i]) { copy->material = copies[i]; break; }
}

static void eng_color_floats(uint32_t argb, float out[4])
{
    out[0] = (float)((argb >> 16) & 0xFFu) / 255.0f;
    out[1] = (float)((argb >> 8) & 0xFFu) / 255.0f;
    out[2] = (float)(argb & 0xFFu) / 255.0f;
    out[3] = (float)(argb >> 24) / 255.0f;
}

static uint32_t eng_color_argb(const float c[4])
{
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        uint32_t byte = (uint32_t)(v * 255.0f + 0.5f);
        argb |= byte << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/* ============================================================================
 * Showing and hiding
 * ============================================================================ */

/* CKObject's VISIBLE and HIERACHICALHIDE flags, as Show left them or the file has them */
static int32_t eng_object_flags(const nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t shown = eng_map_get(&engine->render.shown, id);
    if (shown >= 0) {
        return shown;
    }
    nmo_object_t *object = eng_find_object(engine, id);
    const nmo_object_state_t *state =
        object != NULL ? (const nmo_object_state_t *)nmo_object_get_state(object) : NULL;
    if (state == NULL) {
        return OBJECT_VISIBLE;
    }
    if (state->visibility_flags & NMO_CKOBJECT_HIERARCHICAL) {
        return OBJECT_HIERARCHICAL_HIDE;
    }
    return (state->visibility_flags & NMO_CKOBJECT_VISIBLE) ? OBJECT_VISIBLE : 0;
}

void eng_show(nmo_engine_t *engine, nmo_object_id_t id, int32_t show)
{
    if (id == 0) {
        return;
    }
    int32_t flags = show == SHOW_SHOW ? OBJECT_VISIBLE
                    : show == SHOW_HIERARCHICAL_HIDE ? OBJECT_HIERARCHICAL_HIDE
                                                     : 0;
    eng_map_put(&engine->render.shown, id, flags);
    nmo_object_t *object = eng_find_object(engine, id);
    if (object != NULL && nmo_object_get_class_id(object) == NMO_CID_GROUP) {
        int32_t count = nmo_engine_group_count(engine, id);
        for (int32_t i = 0; i < count; i++) {
            nmo_object_id_t member = nmo_engine_group_object(engine, id, i);
            if (member != id) {
                eng_show(engine, member, show);
            }
        }
    }
}

int32_t eng_object_visibility(const nmo_engine_t *engine, nmo_object_id_t id)
{
    return eng_object_flags(engine, id);
}

void eng_set_object_visibility(nmo_engine_t *engine, nmo_object_id_t id, int32_t flags)
{
    eng_map_put(&engine->render.shown, id, flags & (OBJECT_VISIBLE | OBJECT_HIERARCHICAL_HIDE));
}

bool nmo_engine_object_is_visible(const nmo_engine_t *engine, nmo_object_id_t id)
{
    return engine != NULL && (eng_object_flags(engine, id) & OBJECT_VISIBLE) != 0;
}

/* ============================================================================
 * Materials
 * ============================================================================ */

#define VXEFFECT_TEXGEN 1
#define VXEFFECT_TEXGENREF 2
/* CKStructSaver's subchunk: this identifier, the member count, then a CKParameter chunk each */
#define CK_STRUCT_IDENTIFIER 0x12348765u
#define CK_PARAMETER_IDENTIFIER 0x40u

/*
 * A member of a struct parameter's subchunk (CKParameter::Load): a buffer
 * (state 1) into `out_buffer`, or an object (state 2) into `out_object`, as
 * its file ID. False when it holds neither.
 */
static bool eng_struct_member(nmo_chunk_t *member, uint32_t *out_buffer, nmo_object_id_t *out_object)
{
    nmo_guid_t guid;
    uint32_t state = 0;
    if (nmo_chunk_start_read(member) != NMO_OK ||
        nmo_chunk_seek_identifier(member, CK_PARAMETER_IDENTIFIER) != NMO_OK ||
        nmo_chunk_read_guid(member, &guid) != NMO_OK || nmo_chunk_read_dword(member, &state) != NMO_OK) {
        return false;
    }
    if (state == 2) {
        return nmo_chunk_read_object_id(member, out_object) == NMO_OK;
    }
    void *data = NULL;
    size_t size = 0;
    if (state != 1 || nmo_chunk_read_buffer(member, &data, &size) != NMO_OK || data == NULL ||
        size < sizeof(*out_buffer)) {
        return false;
    }
    memcpy(out_buffer, data, sizeof(*out_buffer));
    return true;
}

/*
 * The texture coordinates generation a material's effect sets (ReadTexGenParameter,
 * ReadTexGenRefParameter): VXEFFECT_TEXGEN's parameter holds the VX_EFFECTTEXGEN,
 * VXEFFECT_TEXGENREF's is a struct of it and the referential.
 */
static void eng_material_texgen(const nmo_engine_t *engine, nmo_object_id_t id,
                                const nmo_material_state_t *state, nmo_engine_material_t *out)
{
    if ((state->effect != VXEFFECT_TEXGEN && state->effect != VXEFFECT_TEXGENREF) ||
        !state->has_effect_param) {
        return;
    }
    nmo_object_id_t param = eng_object_ref(engine, id, &state->effect_parameter);
    nmo_object_t *object = param != 0 ? eng_find_object(engine, param) : NULL;
    const nmo_parameter_state_t *value =
        object != NULL ? (const nmo_parameter_state_t *)nmo_object_get_state(object) : NULL;
    if (value == NULL) {
        return;
    }
    if (state->effect == VXEFFECT_TEXGEN) {
        if (value->buffer_data.data != NULL &&
            value->buffer_data.count * value->buffer_data.element_size >= sizeof(out->texgen)) {
            memcpy(&out->texgen, value->buffer_data.data, sizeof(out->texgen));
        }
        return;
    }
    if (value->mode != CKPARAM_MODE_SUBCHUNK || value->subchunk == NULL) {
        return;
    }
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096u);
    nmo_chunk_t *saved = arena != NULL ? nmo_chunk_clone(value->subchunk, arena) : NULL;
    int32_t count = 0;
    if (saved != NULL && nmo_chunk_start_read(saved) == NMO_OK &&
        nmo_chunk_seek_identifier(saved, CK_STRUCT_IDENTIFIER) == NMO_OK &&
        nmo_chunk_read_int(saved, &count) == NMO_OK) {
        for (int32_t i = 0; i < count && i < 2; i++) {
            nmo_chunk_t *member = NULL;
            if (nmo_chunk_read_sub_chunk(saved, &member) != NMO_OK || member == NULL) {
                break;
            }
            uint32_t texgen = 0;
            nmo_object_id_t referential = 0;
            if (eng_struct_member(member, &texgen, &referential)) {
                if (i == 0) {
                    out->texgen = (int32_t)texgen;
                } else {
                    const eng_module_t *module = eng_module_of(engine, id, NULL);
                    out->texgen_referential =
                        module != NULL && referential != 0 ? eng_module_id(module, referential) : 0;
                }
            }
            nmo_chunk_destroy(member);
        }
    }
    nmo_arena_destroy(arena);
}

/* RCKMaterial as it loads: colors, the texture, and the packed modes and flags */
void eng_material_from_state(const nmo_engine_t *engine, nmo_object_id_t id,
                                    const nmo_material_state_t *state, nmo_engine_material_t *out)
{
    memset(out, 0, sizeof(*out));
    eng_color_floats(state->diffuse_color, out->diffuse);
    eng_color_floats(state->ambient_color, out->ambient);
    eng_color_floats(state->specular_color, out->specular);
    eng_color_floats(state->emissive_color, out->emissive);
    out->power = state->specular_power;
    out->texture = eng_object_ref(engine, id, &state->textures[0]);
    uint32_t modes = state->packed_modes;
    out->texture_blend = (int32_t)(modes & 0xFu);
    out->min_filter = (int32_t)((modes >> 4) & 0xFu);
    out->mag_filter = (int32_t)((modes >> 8) & 0xFu);
    out->source_blend = (int32_t)((modes >> 12) & 0xFu);
    out->dest_blend = (int32_t)((modes >> 16) & 0xFu);
    out->shade_mode = (int32_t)((modes >> 20) & 0xFu);
    out->fill_mode = (int32_t)((modes >> 24) & 0xFu);
    out->address_mode = (int32_t)((modes >> 28) & 0xFu);
    out->border_color = state->texture_border_color;
    uint32_t flags = state->packed_flags;
    out->two_sided = (flags & 0x01u) != 0;
    out->z_write = (flags & 0x02u) != 0;
    out->alpha_blend = (flags & 0x08u) != 0;
    out->alpha_test = (flags & 0x10u) != 0;
    out->z_func = (int32_t)((flags >> 8) & 0xFu);
    out->alpha_func = (int32_t)((flags >> 16) & 0xFu);
    out->alpha_ref = (uint8_t)(flags >> 24);
    eng_material_texgen(engine, id, state, out);
}

eng_material_t *eng_material(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t at = eng_map_get(&engine->render.material_map, id);
    if (at >= 0) {
        return &engine->render.materials.data[at];
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_MATERIAL) {
        return NULL;
    }
    const nmo_material_state_t *state = (const nmo_material_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return NULL;
    }
    eng_material_t *material = NULL;
    if (!ENG_VEC_PUSH(engine->render.materials, material)) {
        return NULL;
    }
    material->id = id;
    eng_material_from_state(engine, id, state, &material->desc);
    if (!eng_map_put(&engine->render.material_map, id,
                     (int32_t)engine->render.materials.count - 1)) {
        engine->render.materials.count--;
        return NULL;
    }
    return material;
}

eng_material_t *eng_material_new(nmo_engine_t *engine, nmo_object_id_t id)
{
    eng_material_t *material = NULL;
    if (id == 0 || !ENG_VEC_PUSH(engine->render.materials, material)) {
        return NULL;
    }
    material->id = id;
    nmo_engine_material_t *d = &material->desc;
    const float diffuse[4] = {0.7f, 0.7f, 0.7f, 1.0f}, ambient[4] = {0.3f, 0.3f, 0.3f, 1.0f};
    const float specular[4] = {0.5f, 0.5f, 0.5f, 1.0f}, emissive[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(d->diffuse, diffuse, sizeof(diffuse));
    memcpy(d->ambient, ambient, sizeof(ambient));
    memcpy(d->specular, specular, sizeof(specular));
    memcpy(d->emissive, emissive, sizeof(emissive));
    d->texture_blend = VXTEXTUREBLEND_MODULATEALPHA;
    d->min_filter = d->mag_filter = VXTEXTUREFILTER_LINEAR;
    d->address_mode = VXTEXTURE_ADDRESSWRAP;
    d->source_blend = VXBLEND_ONE;
    d->dest_blend = VXBLEND_ZERO;
    d->shade_mode = VXSHADE_GOURAUD;
    d->fill_mode = VXFILL_SOLID;
    d->z_write = true;
    d->z_func = VXCMP_LESSEQUAL;
    d->alpha_func = VXCMP_ALWAYS;
    if (!eng_map_put(&engine->render.material_map, id, (int32_t)engine->render.materials.count - 1)) {
        engine->render.materials.count--;
        return NULL;
    }
    return material;
}

bool eng_texture_keyed(const nmo_engine_t *engine, nmo_object_id_t texture)
{
    for (size_t i = 0; texture != 0 && i < engine->media.textures.count; i++) {
        if (engine->media.textures.data[i].id == texture) {
            return engine->media.textures.data[i].transparent;
        }
    }
    nmo_object_t *object = texture != 0 ? eng_find_object(engine, texture) : NULL;
    const nmo_texture_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_TEXTURE
            ? (const nmo_texture_state_t *)nmo_object_get_state(object)
            : NULL;
    return state != NULL && state->is_transparent;
}

/* RCKMaterial::SetAsCurrent leaves its own alpha test unset when the texture tests alpha */
static void eng_texture_alpha_test(const nmo_engine_t *engine, nmo_engine_material_t *desc)
{
    if (eng_texture_keyed(engine, desc->texture)) {
        desc->alpha_test = true;
        desc->alpha_func = VXCMP_GREATER;
        desc->alpha_ref = 0;
    }
}

bool nmo_engine_material(const nmo_engine_t *engine, nmo_object_id_t material,
                         nmo_engine_material_t *out)
{
    if (engine == NULL || out == NULL) {
        return false;
    }
    int32_t at = eng_map_get(&engine->render.material_map, material);
    if (at >= 0) {
        *out = engine->render.materials.data[at].desc;
        eng_texture_alpha_test(engine, out);
        return true;
    }
    nmo_object_t *object = material != 0 ? eng_find_object(engine, material) : NULL;
    const nmo_material_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_MATERIAL
            ? (const nmo_material_state_t *)nmo_object_get_state(object)
            : NULL;
    if (state == NULL) {
        return false;
    }
    eng_material_from_state(engine, material, state, out);
    eng_texture_alpha_test(engine, out);
    return true;
}

/* ============================================================================
 * Meshes
 * ============================================================================ */

static const nmo_mesh_state_t *eng_mesh_state(const nmo_engine_t *engine, nmo_object_id_t mesh)
{
    return (const nmo_mesh_state_t *)eng_typed_state(engine, mesh, CKPGUID_MESH);
}

eng_mesh_copy_t *eng_mesh_copy_find(const nmo_engine_t *engine, nmo_object_id_t mesh)
{
    for (size_t i = 0; i < engine->render.mesh_copies.count; i++) {
        if (engine->render.mesh_copies.data[i].mesh == mesh) {
            return &engine->render.mesh_copies.data[i];
        }
    }
    return NULL;
}

/* The copy of the mesh's vertices: with the normals CK builds from the faces when the file saved none */
static eng_mesh_copy_t *eng_make_mesh_copy(nmo_engine_t *engine, nmo_object_id_t mesh,
                                           const nmo_mesh_state_t *state)
{
    size_t count = state->vertex_count;
    nmo_vertex_t *vertices = (nmo_vertex_t *)malloc((count > 0 ? count : 1) * sizeof(nmo_vertex_t));
    eng_mesh_copy_t *copy = NULL;
    if (vertices == NULL || !ENG_VEC_PUSH(engine->render.mesh_copies, copy)) {
        free(vertices);
        return NULL;
    }
    if (count > 0) {
        memcpy(vertices, state->vertices, count * sizeof(nmo_vertex_t));
    }
    nmo_vector_t *normals = nmo_mesh_normals_are_derived(state)
                                ? (nmo_vector_t *)malloc(count * sizeof(nmo_vector_t))
                                : NULL;
    if (normals != NULL && nmo_mesh_build_vertex_normals(state, normals) == NMO_OK) {
        for (size_t v = 0; v < count; v++) {
            vertices[v].normal = normals[v];
        }
    }
    free(normals);
    copy->mesh = mesh;
    copy->vertices = vertices;
    copy->prelit = (state->flags & VXMESH_PRELITMODE) != 0;
    copy->flags = state->flags;
    copy->version = ++engine->render.mesh_version;
    /* The file's channels: a channel without UVs of its own uses the mesh's (VXCHANNEL_SAMEUV) */
    for (uint32_t c = 0; c < state->material_channel_count; c++) {
        const nmo_material_channel_t *file = &state->material_channels[c];
        nmo_engine_channel_t *channel = NULL;
        if (!ENG_VEC_PUSH(copy->channels, channel)) {
            break;
        }
        channel->material = eng_object_ref(engine, mesh, &file->material);
        channel->source_blend = (int32_t)file->source_blend;
        channel->dest_blend = (int32_t)file->dest_blend;
        channel->active = (file->flags & VXCHANNEL_ACTIVE) != 0;
        channel->lit = (file->flags & VXCHANNEL_NOTLIT) == 0;
        channel->mask = c < 16 ? (uint16_t)(1u << c) : 0;
        if (!(file->flags & VXCHANNEL_SAMEUV) && file->uv_coords != NULL && file->uv_count >= count) {
            float *uvs = (float *)malloc(count * 2u * sizeof(float));
            for (size_t v = 0; uvs != NULL && v < count; v++) {
                uvs[v * 2] = file->uv_coords[v].x;
                uvs[v * 2 + 1] = file->uv_coords[v].y;
            }
            channel->uvs = uvs;
        }
    }
    return copy;
}

int32_t eng_mesh_channel(const eng_mesh_copy_t *copy, nmo_object_id_t material)
{
    for (size_t c = 0; copy != NULL && c < copy->channels.count; c++) {
        if (copy->channels.data[c].material == material) {
            return (int32_t)c;
        }
    }
    return -1;
}

int32_t eng_mesh_add_channel(eng_mesh_copy_t *copy, nmo_object_id_t material)
{
    nmo_engine_channel_t *channel = NULL;
    if (!ENG_VEC_PUSH(copy->channels, channel)) {
        return -1;
    }
    channel->material = material;
    channel->source_blend = VXBLEND_DESTCOLOR;
    channel->dest_blend = VXBLEND_ZERO;
    channel->active = true;
    channel->lit = true;
    return (int32_t)copy->channels.count - 1;
}

void eng_mesh_remove_channel(eng_mesh_copy_t *copy, int32_t channel)
{
    if (channel < 0 || (size_t)channel >= copy->channels.count) {
        return;
    }
    free((void *)copy->channels.data[channel].uvs);
    memmove(&copy->channels.data[channel], &copy->channels.data[channel + 1],
            (copy->channels.count - (size_t)channel - 1) * sizeof(nmo_engine_channel_t));
    copy->channels.count--;
}

eng_mesh_copy_t *eng_mesh_copy(nmo_engine_t *engine, nmo_object_id_t mesh)
{
    eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh);
    const nmo_mesh_state_t *state = copy == NULL ? eng_mesh_state(engine, mesh) : NULL;
    return copy != NULL ? copy : state != NULL ? eng_make_mesh_copy(engine, mesh, state) : NULL;
}

bool nmo_engine_mesh(const nmo_engine_t *engine, nmo_object_id_t mesh, nmo_engine_mesh_t *out)
{
    const eng_mesh_t *made = engine != NULL ? eng_render_mesh((nmo_engine_t *)engine, mesh) : NULL;
    if (made != NULL && out != NULL) {
        memset(out, 0, sizeof(*out));
        out->vertex_count = made->vertex_count;
        out->vertices = made->vertices;
        out->colors = made->colors;
        out->face_count = made->face_count;
        out->indices = made->indices;
        out->faces = made->faces;
        out->material_count = made->material_count;
        out->prelit = made->prelit;
        return true;
    }
    const nmo_mesh_state_t *state = engine != NULL ? eng_mesh_state(engine, mesh) : NULL;
    if (state == NULL || out == NULL) {
        return false;
    }
    eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh);
    if (copy == NULL && (nmo_mesh_normals_are_derived(state) || state->material_channel_count > 0)) {
        copy = eng_make_mesh_copy((nmo_engine_t *)engine, mesh, state);
    }
    memset(out, 0, sizeof(*out));
    out->vertex_count = (int32_t)state->vertex_count;
    out->vertices = copy != NULL ? copy->vertices : state->vertices;
    out->colors = copy != NULL && copy->colors != NULL ? copy->colors : state->vertex_colors;
    out->version = copy != NULL ? copy->version : 0;
    out->channel_count = copy != NULL ? (int32_t)copy->channels.count : 0;
    out->channels = copy != NULL ? copy->channels.data : NULL;
    out->face_count = (int32_t)state->face_count;
    out->indices = state->face_vertex_indices;
    out->faces = copy != NULL && copy->faces != NULL ? copy->faces : state->faces;
    out->material_count =
        copy != NULL && copy->groups != NULL ? copy->group_count : (int32_t)state->material_group_count;
    out->prelit = copy != NULL ? copy->prelit : (state->flags & VXMESH_PRELITMODE) != 0;
    uint32_t flags = copy != NULL ? copy->flags : state->flags;
    out->wrap_u = (flags & VXMESH_WRAPU) != 0;
    out->wrap_v = (flags & VXMESH_WRAPV) != 0;
    return true;
}

nmo_object_id_t nmo_engine_mesh_material(const nmo_engine_t *engine, nmo_object_id_t mesh,
                                         int32_t group)
{
    const eng_mesh_t *made = engine != NULL ? eng_render_mesh((nmo_engine_t *)engine, mesh) : NULL;
    if (made != NULL) {
        return group >= 0 && group < made->material_count ? made->materials[group] : 0;
    }
    const eng_mesh_copy_t *copy = engine != NULL ? eng_mesh_copy_find(engine, mesh) : NULL;
    if (copy != NULL && copy->groups != NULL) {
        return group >= 0 && group < copy->group_count ? copy->groups[group] : 0;
    }
    const nmo_mesh_state_t *state = engine != NULL ? eng_mesh_state(engine, mesh) : NULL;
    if (state == NULL || group < 0 || (uint32_t)group >= state->material_group_count) {
        return 0;
    }
    return eng_object_ref(engine, mesh, &state->material_groups[group].material);
}

nmo_face_t *eng_mesh_copy_faces(nmo_engine_t *engine, eng_mesh_copy_t *copy)
{
    const nmo_mesh_state_t *state = eng_mesh_state(engine, copy->mesh);
    if (copy->faces == NULL && state != NULL && state->face_count > 0) {
        copy->faces = (nmo_face_t *)malloc(state->face_count * sizeof(nmo_face_t));
        if (copy->faces != NULL) {
            memcpy(copy->faces, state->faces, state->face_count * sizeof(nmo_face_t));
        }
    }
    return copy->faces;
}

void eng_mesh_vertices_moved(nmo_engine_t *engine, eng_mesh_copy_t *copy, bool rebuild_normals)
{
    const nmo_mesh_state_t *state = eng_mesh_state(engine, copy->mesh);
    uint32_t count = state != NULL ? state->vertex_count : 0;
    if (count == 0) {
        return;
    }
    if (rebuild_normals) {
        nmo_mesh_state_t moved = *state;
        moved.vertices = copy->vertices;
        nmo_vector_t *normals = (nmo_vector_t *)malloc(count * sizeof(nmo_vector_t));
        if (normals != NULL && nmo_mesh_build_vertex_normals(&moved, normals) == NMO_OK) {
            for (uint32_t v = 0; v < count; v++) {
                copy->vertices[v].normal = normals[v];
            }
        }
        free(normals);
        nmo_face_t *faces = state->face_vertex_indices != NULL ? eng_mesh_copy_faces(engine, copy) : NULL;
        for (uint32_t f = 0; faces != NULL && f < state->face_count; f++) {
            const uint16_t *k = &state->face_vertex_indices[3u * f];
            if (k[0] >= count || k[1] >= count || k[2] >= count) {
                continue;
            }
            const nmo_vector_t *p0 = &copy->vertices[k[0]].position, *p1 = &copy->vertices[k[1]].position;
            const nmo_vector_t *p2 = &copy->vertices[k[2]].position;
            double ax = (double)p1->x - p0->x, ay = (double)p1->y - p0->y, az = (double)p1->z - p0->z;
            double bx = (double)p2->x - p0->x, by = (double)p2->y - p0->y, bz = (double)p2->z - p0->z;
            double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
            double length = sqrt(nx * nx + ny * ny + nz * nz);
            if (length > 0.0) {
                faces[f].normal = (nmo_vector_t){(float)(nx / length), (float)(ny / length), (float)(nz / length)};
            }
        }
    }
    double sum[3] = {0.0, 0.0, 0.0};
    for (uint32_t v = 0; v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        sum[0] += p->x, sum[1] += p->y, sum[2] += p->z;
    }
    copy->bary_center = (nmo_vector_t){(float)(sum[0] / count), (float)(sum[1] / count), (float)(sum[2] / count)};
    double squared = 0.0;
    for (uint32_t v = 0; v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        double x = (double)p->x - copy->bary_center.x, y = (double)p->y - copy->bary_center.y;
        double z = (double)p->z - copy->bary_center.z;
        squared = fmax(squared, x * x + y * y + z * z);
    }
    copy->radius = (float)sqrt(squared);
    copy->geometry_changed = true;
    copy->version = ++engine->render.mesh_version;
}

float *eng_mesh_uvs(nmo_engine_t *engine, eng_mesh_copy_t *copy, int32_t channel, size_t *stride,
                    int32_t *count)
{
    const nmo_mesh_state_t *state = eng_mesh_state(engine, copy->mesh);
    *count = state != NULL ? (int32_t)state->vertex_count : 0;
    *stride = sizeof(nmo_vertex_t);
    const nmo_engine_channel_t *own =
        channel >= 0 && (size_t)channel < copy->channels.count ? &copy->channels.data[channel] : NULL;
    if ((channel != -1 && own == NULL) || *count == 0) {
        return NULL;
    }
    if (own != NULL && own->uvs != NULL) {
        *stride = 2u * sizeof(float);
        return (float *)own->uvs;
    }
    return &copy->vertices[0].uv.x; /* the mesh's, which a channel without its own shares */
}

/*
 * A mesh's faces and group materials as blocks change them: a made mesh's
 * own, or a file mesh copy's, copied from the file the first time.
 */
typedef struct eng_mesh_groups {
    nmo_face_t *faces;
    int32_t face_count;
    nmo_object_id_t **materials;
    int32_t *count;
    uint32_t *version; /* NULL for a made mesh */
} eng_mesh_groups_t;

static bool eng_mesh_groups(nmo_engine_t *engine, nmo_object_id_t mesh, eng_mesh_groups_t *out)
{
    eng_mesh_t *made = eng_render_mesh(engine, mesh);
    if (made != NULL) {
        *out = (eng_mesh_groups_t){made->faces, made->face_count, &made->materials, &made->material_count,
                                   NULL};
        return true;
    }
    const nmo_mesh_state_t *state = eng_mesh_state(engine, mesh);
    eng_mesh_copy_t *copy = state != NULL ? eng_mesh_copy(engine, mesh) : NULL;
    if (copy == NULL) {
        return false;
    }
    if (state->face_count > 0 && eng_mesh_copy_faces(engine, copy) == NULL) {
        return false;
    }
    if (copy->groups == NULL) {
        uint32_t count = state->material_group_count;
        copy->groups = (nmo_object_id_t *)calloc(count > 0 ? count : 1, sizeof(nmo_object_id_t));
        if (copy->groups == NULL) {
            return false;
        }
        for (uint32_t g = 0; g < count; g++) {
            copy->groups[g] = eng_object_ref(engine, mesh, &state->material_groups[g].material);
        }
        copy->group_count = (int32_t)count;
    }
    *out = (eng_mesh_groups_t){copy->faces, (int32_t)state->face_count, &copy->groups, &copy->group_count,
                               &copy->version};
    return true;
}

/* CKMesh::GetMaterialGroupIndex: the first group of `material`, made when `create`; -1 for none */
static int32_t eng_mesh_group_of(eng_mesh_groups_t *groups, nmo_object_id_t material, bool create)
{
    for (int32_t g = 0; g < *groups->count; g++) {
        if ((*groups->materials)[g] == material) {
            return g;
        }
    }
    if (!create) {
        return -1;
    }
    nmo_object_id_t *grown =
        (nmo_object_id_t *)realloc(*groups->materials, ((size_t)*groups->count + 1u) * sizeof(nmo_object_id_t));
    if (grown == NULL) {
        return -1;
    }
    grown[*groups->count] = material;
    *groups->materials = grown;
    return (*groups->count)++;
}

static void eng_mesh_groups_changed(nmo_engine_t *engine, const eng_mesh_groups_t *groups)
{
    if (groups->version != NULL) {
        *groups->version = ++engine->render.mesh_version;
    }
}

bool eng_mesh_apply_material(nmo_engine_t *engine, nmo_object_id_t mesh, nmo_object_id_t material)
{
    eng_mesh_groups_t groups;
    if (!eng_mesh_groups(engine, mesh, &groups)) {
        return false;
    }
    int32_t group = eng_mesh_group_of(&groups, material, true);
    for (int32_t f = 0; group >= 0 && f < groups.face_count; f++) {
        groups.faces[f].material_group_idx = (uint32_t)group;
    }
    eng_mesh_groups_changed(engine, &groups);
    return true;
}

bool eng_mesh_set_face_material(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t face,
                                nmo_object_id_t material)
{
    eng_mesh_groups_t groups;
    if (!eng_mesh_groups(engine, mesh, &groups) || face < 0 || face >= groups.face_count) {
        return false;
    }
    int32_t group = eng_mesh_group_of(&groups, material, true);
    if (group >= 0) {
        groups.faces[face].material_group_idx = (uint32_t)group;
    }
    eng_mesh_groups_changed(engine, &groups);
    return true;
}

bool eng_mesh_replace_material(nmo_engine_t *engine, nmo_object_id_t mesh, nmo_object_id_t old_material,
                               nmo_object_id_t new_material)
{
    eng_mesh_groups_t groups;
    if (!eng_mesh_groups(engine, mesh, &groups)) {
        return false;
    }
    int32_t old_group = old_material != new_material ? eng_mesh_group_of(&groups, old_material, false) : -1;
    if (old_group == 0) {
        int32_t group = eng_mesh_group_of(&groups, new_material, true);
        for (int32_t f = 0; group >= 0 && f < groups.face_count; f++) {
            if (groups.faces[f].material_group_idx == 0) {
                groups.faces[f].material_group_idx = (uint32_t)group;
            }
        }
    } else if (old_group > 0 && new_material != 0) {
        (*groups.materials)[old_group] = new_material;
    } else if (old_group > 0) {
        for (int32_t f = 0; f < groups.face_count; f++) {
            uint32_t *index = &groups.faces[f].material_group_idx;
            if (*index == (uint32_t)old_group) {
                *index = 0;
            } else if (*index > (uint32_t)old_group) {
                (*index)--;
            }
        }
        memmove(&(*groups.materials)[old_group], &(*groups.materials)[old_group + 1],
                (size_t)(*groups.count - old_group - 1) * sizeof(nmo_object_id_t));
        (*groups.count)--;
    }
    if (old_group >= 0) {
        eng_mesh_groups_changed(engine, &groups);
    }
    return true;
}

bool eng_mesh_set_lit_mode(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t mode)
{
    eng_mesh_t *made = eng_render_mesh(engine, mesh);
    if (made != NULL) {
        made->prelit = mode == 0;
        return true;
    }
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, mesh);
    if (copy == NULL) {
        return false;
    }
    copy->prelit = mode == 0;
    copy->version = ++engine->render.mesh_version;
    return true;
}

/* CKMesh::IsTransparent: forced, or a material group's material blends */
static bool eng_mesh_transparent(nmo_engine_t *engine, nmo_object_id_t mesh,
                                 const nmo_mesh_state_t *state)
{
    const eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh);
    if ((copy != NULL ? copy->flags : state->flags) & VXMESH_FORCETRANSPARENCY) {
        return true;
    }
    int32_t count = copy != NULL && copy->groups != NULL ? copy->group_count : (int32_t)state->material_group_count;
    for (int32_t g = 0; g < count; g++) {
        eng_material_t *material = eng_material(engine, nmo_engine_mesh_material(engine, mesh, g));
        if (material != NULL && material->desc.alpha_blend) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * 3D sprites
 * ============================================================================ */

static void eng_normalize3(float v[3])
{
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0.0f) {
        v[0] /= length, v[1] /= length, v[2] /= length;
    }
}

/*
 * RCKSprite3D: UpdateOrientation (a billboard takes the camera's axes, an X or
 * Y rotating one turns about that axis toward it), then FillBatch: the quad
 * along the world X and Y axes times the sprite's size, from its offset, in
 * its UV rectangle, with its material's diffuse color (CallSprite3DBatches).
 */
static bool eng_draw_sprite3d(nmo_engine_t *engine, const eng_entity_t *entity, const float camera[16])
{
    const eng_sprite3d_t *state = eng_sprite3d(engine,entity->id);
    if (state == NULL) return false;
    nmo_object_id_t material_id = state->material;
    eng_material_t *material = material_id != 0 && eng_map_get(&engine->destroyed,material_id) < 0
        ? eng_material(engine, material_id) : NULL;
    if (material == NULL) {
        return true; /* only a sprite with a material joins a batch */
    }
    float x[3] = {entity->world.m[0][0], entity->world.m[0][1], entity->world.m[0][2]};
    float y[3] = {entity->world.m[1][0], entity->world.m[1][1], entity->world.m[1][2]};
    if (camera != NULL && state->mode == VXSPRITE3D_BILLBOARD) {
        memcpy(x, &camera[0], sizeof(x));
        memcpy(y, &camera[4], sizeof(y));
    } else if (camera != NULL && state->mode == VXSPRITE3D_XROTATE) {
        float z[3] = {0.0f, -camera[6], camera[5]};
        eng_normalize3(z);
        x[0] = 1.0f, x[1] = 0.0f, x[2] = 0.0f;
        y[0] = 0.0f, y[1] = z[2], y[2] = -z[1];
        eng_normalize3(y);
    } else if (camera != NULL && state->mode == VXSPRITE3D_YROTATE) {
        x[0] = camera[10], x[1] = 0.0f, x[2] = -camera[8];
        eng_normalize3(x);
        y[0] = 0.0f, y[1] = 1.0f, y[2] = 0.0f;
    }
    float width = state->max.x-state->min.x, height = state->max.y-state->min.y;
    float ox = (state->offset.x - 1.0f) * 0.5f, oy = (state->offset.y - 1.0f) * 0.5f;
    float base[3], corners[4][3];
    for (int c = 0; c < 3; c++) {
        x[c] *= width;
        y[c] *= height;
        base[c] = entity->world.m[3][c] + x[c] * ox + y[c] * oy;
        corners[0][c] = base[c];
        corners[1][c] = base[c] + y[c];
        corners[2][c] = base[c] + y[c] + x[c];
        corners[3][c] = base[c] + x[c];
    }
    const nmo_rect_t *uv = &state->uv_rect;
    const float uvs[4][2] = {{uv->left, uv->bottom}, {uv->left, uv->top}, {uv->right, uv->top},
                             {uv->right, uv->bottom}};
    uint32_t color = eng_color_argb(material->desc.diffuse);
    int32_t first = (int32_t)engine->render.vertices.count;
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        nmo_engine_vertex_t *vertex = NULL;
        if (!ENG_VEC_PUSH(engine->render.vertices, vertex)) {
            return false;
        }
        const float *p = corners[order[k]];
        *vertex = (nmo_engine_vertex_t){p[0], p[1], p[2], uvs[order[k]][0], uvs[order[k]][1], color, 0.0f, 0.0f, 0.0f};
    }
    nmo_engine_draw_t *draw = eng_render_push(engine);
    if (draw == NULL) {
        return false;
    }
    memset(draw, 0, sizeof(*draw));
    draw->kind = NMO_ENGINE_DRAW_SPRITE3D;
    draw->object = entity->id;
    draw->material = material_id;
    draw->texture = material->desc.texture;
    draw->first_vertex = first;
    draw->vertex_count = 6;
    draw->transparent = material->desc.alpha_blend;
    draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
    memcpy(&draw->world[12], entity->world.m[3], 3u * sizeof(float));
    return true;
}

/* ============================================================================
 * Lights
 * ============================================================================ */

static const nmo_targetcamera_state_t *eng_target_camera(nmo_engine_t *engine, nmo_object_id_t id)
{
    return eng_entity_index(engine,id) >= 0 ? eng_typed_state(engine,id,CKPGUID_TARGETCAMERA) : NULL;
}

static eng_camera_target_t *eng_camera_target_override(nmo_engine_t *engine, nmo_object_id_t id, bool create)
{
    for (size_t i = 0; i < engine->render.camera_targets.count; i++)
        if (engine->render.camera_targets.data[i].id == id) return &engine->render.camera_targets.data[i];
    eng_camera_target_t *entry = NULL;
    if (!create || !ENG_VEC_PUSH(engine->render.camera_targets,entry)) return NULL;
    entry->id = id;
    return entry;
}

nmo_object_id_t eng_camera_target(nmo_engine_t *engine, nmo_object_id_t camera)
{
    const nmo_targetcamera_state_t *source = eng_target_camera(engine,camera);
    if (source == NULL) return 0;
    const eng_camera_target_t *entry = eng_camera_target_override(engine,camera,false);
    nmo_object_id_t target = entry != NULL ? entry->target : source->has_target ? eng_object_ref(engine,camera,&source->target) : 0;
    return eng_entity_index(engine,target) >= 0 ? target : 0;
}

bool eng_restore_camera_target(nmo_engine_t *engine, nmo_object_id_t camera, nmo_object_id_t target)
{
    if (eng_target_camera(engine,camera) == NULL) return false;
    eng_camera_target_t *entry = eng_camera_target_override(engine,camera,true);
    if (entry == NULL) return false;
    entry->target = target;
    return true;
}

static void eng_camera_target_flags(nmo_engine_t *engine, nmo_object_id_t target, bool attached)
{
    int32_t index = eng_entity_index(engine,target);
    if (index < 0) return;
    uint32_t *flags = &engine->entities.data[index].entity_flags;
    if (attached) *flags = (*flags | CK_3DENTITY_TARGETCAMERA) & ~(uint32_t)CK_3DENTITY_FRAME;
    else *flags = (*flags & ~(uint32_t)CK_3DENTITY_TARGETCAMERA) | CK_3DENTITY_FRAME;
}

bool eng_set_camera_target(nmo_engine_t *engine, nmo_object_id_t camera, nmo_object_id_t target)
{
    if (eng_target_camera(engine,camera) == NULL) return false;
    if (target == camera) return true; /* SetTarget ignores self attachments. */
    if (target != 0 && eng_entity_index(engine,target) < 0) return false;
    nmo_object_id_t old = eng_camera_target(engine,camera);
    if (!eng_restore_camera_target(engine,camera,target)) return false;
    if (old != target) {
        eng_camera_target_flags(engine,old,false);
        eng_camera_target_flags(engine,target,true);
    }
    return true;
}

void eng_copy_camera_target(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count)
{
    if (eng_target_camera(engine,source) == NULL) return;
    nmo_object_id_t attached = eng_camera_target(engine,source);
    if (attached == 0 && eng_camera_target_override(engine,source,false) == NULL) return;
    /* Copy starts from no attachment; remapping then calls SetTarget again. */
    if (!eng_restore_camera_target(engine,target,0) || !eng_set_camera_target(engine,target,attached)) return;
    for (size_t i = 0; i < count; i++) if (originals[i] == attached) {
        eng_set_camera_target(engine,target,copies[i]);
        break;
    }
}

void eng_camera_forget_object(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (eng_entity_index(engine,id) < 0) return;
    for (size_t i = 0; i < engine->entities.count; i++) {
        nmo_object_id_t camera = engine->entities.data[i].id;
        if (camera == id || eng_camera_target(engine,camera) == id) eng_set_camera_target(engine,camera,0);
    }
}

static eng_camera_state_t *eng_camera_override(nmo_engine_t *engine, nmo_object_id_t id, bool create)
{
    const nmo_camera_state_t *source = NULL;
    if (create) {
        source = eng_entity_index(engine,id) >= 0 ? eng_typed_state(engine,id,CKPGUID_CAMERA) : NULL;
        if (source == NULL) return NULL;
    }
    for (size_t i = 0; i < engine->render.camera_states.count; i++)
        if (engine->render.camera_states.data[i].id == id) return &engine->render.camera_states.data[i];
    if (!create) return NULL;
    eng_camera_state_t *state = NULL;
    if (source == NULL || !ENG_VEC_PUSH(engine->render.camera_states,state)) return NULL;
    *state = (eng_camera_state_t){id,source->fov,source->orthographic_zoom,source->near_plane,source->far_plane,source->projection_type};
    return state;
}

bool eng_camera_projection_type(nmo_engine_t *engine, nmo_object_id_t id, uint32_t *projection)
{
    const nmo_camera_state_t *source = eng_entity_index(engine,id) >= 0 ? eng_typed_state(engine,id,CKPGUID_CAMERA) : NULL;
    if (source == NULL) return false;
    const eng_camera_state_t *state = eng_camera_override(engine,id,false);
    *projection = state != NULL ? state->projection_type : source->projection_type;
    return true;
}

bool eng_set_camera_projection_type(nmo_engine_t *engine, nmo_object_id_t id, uint32_t projection)
{
    eng_camera_state_t *state = eng_camera_override(engine,id,true);
    if (state == NULL) return false;
    state->projection_type = projection;
    return true;
}

bool eng_camera_fov(nmo_engine_t *engine, nmo_object_id_t id, float *fov)
{
    const nmo_camera_state_t *source = eng_entity_index(engine,id) >= 0 ? eng_typed_state(engine,id,CKPGUID_CAMERA) : NULL;
    if (source == NULL) return false;
    const eng_camera_state_t *state = eng_camera_override(engine,id,false);
    *fov = state != NULL ? state->fov : source->fov;
    return true;
}

bool eng_set_camera_fov(nmo_engine_t *engine, nmo_object_id_t id, float fov)
{
    eng_camera_state_t *state = eng_camera_override(engine,id,true);
    if (state == NULL) return false;
    state->fov = fov;
    return true;
}

bool eng_camera_orthographic_zoom(nmo_engine_t *engine, nmo_object_id_t id, float *zoom)
{
    const nmo_camera_state_t *source = eng_entity_index(engine,id) >= 0 ? eng_typed_state(engine,id,CKPGUID_CAMERA) : NULL;
    if (source == NULL) return false;
    const eng_camera_state_t *state = eng_camera_override(engine,id,false);
    *zoom = state != NULL ? state->orthographic_zoom : source->orthographic_zoom;
    return true;
}

bool eng_set_camera_orthographic_zoom(nmo_engine_t *engine, nmo_object_id_t id, float zoom)
{
    eng_camera_state_t *state = eng_camera_override(engine,id,true);
    if (state == NULL) return false;
    state->orthographic_zoom = zoom;
    return true;
}

void eng_copy_camera_state(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target)
{
    const eng_camera_state_t *original = eng_camera_override(engine,source,false);
    if (original == NULL) return;
    /* Creating the target can reallocate the table containing the source. */
    eng_camera_state_t saved = *original;
    eng_camera_state_t *copy = eng_camera_override(engine,target,true);
    if (copy != NULL) { *copy = saved; copy->id = target; }
}

bool eng_camera_planes(nmo_engine_t *engine, nmo_object_id_t id, float *near_plane,
                       float *far_plane)
{
    const nmo_camera_state_t *source = eng_entity_index(engine,id) >= 0 ? eng_typed_state(engine,id,CKPGUID_CAMERA) : NULL;
    if (source == NULL) return false;
    const eng_camera_state_t *state = eng_camera_override(engine,id,false);
    *near_plane = state != NULL ? state->near_plane : source->near_plane;
    *far_plane = state != NULL ? state->far_plane : source->far_plane;
    return true;
}

void eng_set_camera_planes(nmo_engine_t *engine, nmo_object_id_t id, float near_plane,
                           float far_plane)
{
    eng_camera_state_t *state = eng_camera_override(engine,id,true);
    if (state != NULL) { state->near_plane = near_plane; state->far_plane = far_plane; }
}

eng_light_t *eng_light(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t at = eng_map_get(&engine->render.light_map, id);
    if (at >= 0) {
        return &engine->render.lights.data[at];
    }
    const nmo_light_state_t *state =
        (const nmo_light_state_t *)eng_typed_state(engine, id, CKPGUID_LIGHT);
    if (state == NULL) {
        return NULL;
    }
    eng_light_t *light = NULL;
    if (!ENG_VEC_PUSH(engine->render.lights, light)) {
        return NULL;
    }
    memset(light, 0, sizeof(*light));
    light->id = id;
    light->color[0] = state->light_data.diffuse.r;
    light->color[1] = state->light_data.diffuse.g;
    light->color[2] = state->light_data.diffuse.b;
    light->color[3] = state->light_data.diffuse.a;
    light->active = (state->flags & NMO_LIGHT_FLAG_ACTIVE) != 0;
    light->type = (int32_t)state->light_data.type;
    light->range = state->light_data.range;
    light->attenuation[0] = state->light_data.attenuation0;
    light->attenuation[1] = state->light_data.attenuation1;
    light->attenuation[2] = state->light_data.attenuation2;
    light->hot_spot = state->light_data.inner_spot_cone;
    light->fall_off = state->light_data.outer_spot_cone;
    light->fall_off_shape = state->light_data.falloff;
    light->specular = (state->flags & NMO_LIGHT_FLAG_SPECULAR) != 0;
    const nmo_targetlight_state_t *target =
        (const nmo_targetlight_state_t *)eng_typed_state(engine, id, CKPGUID_TARGETLIGHT);
    if (target != NULL && target->has_target) {
        light->target = eng_object_ref(engine, id, &target->target);
    }
    if (!eng_map_put(&engine->render.light_map, id, (int32_t)engine->render.lights.count - 1)) {
        engine->render.lights.count--;
        return NULL;
    }
    return light;
}

bool eng_set_light_target(nmo_engine_t *engine, nmo_object_id_t id, nmo_object_id_t target)
{
    if (eng_typed_state(engine, id, CKPGUID_TARGETLIGHT) == NULL) {
        return false; /* CKLight::SetTarget does nothing */
    }
    eng_light_t *light = eng_light(engine, id);
    if (light == NULL || target == id) {
        return light != NULL;
    }
    int32_t old = light->target != 0 ? eng_entity_index(engine, light->target) : -1;
    if (old >= 0) {
        uint32_t *flags = &engine->entities.data[old].entity_flags;
        *flags = (*flags & ~(uint32_t)CK_3DENTITY_TARGETLIGHT) | CK_3DENTITY_FRAME;
    }
    int32_t index = target != 0 ? eng_entity_index(engine, target) : -1;
    if (index >= 0) {
        uint32_t *flags = &engine->entities.data[index].entity_flags;
        *flags = (*flags | CK_3DENTITY_TARGETLIGHT) & ~(uint32_t)CK_3DENTITY_FRAME;
        light->target = target;
    } else {
        light->target = 0;
    }
    return true;
}

/* ============================================================================
 * 2D entities
 * ============================================================================ */

static void eng_load_2d_class(nmo_engine_t *engine, uint32_t class_id);

/* CK2dEntity::Load: the rects, Z order, parent, material and flags */
eng_2d_t *eng_2d_entity(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t at = eng_map_get(&engine->render.entity_2d_map, id);
    if (at >= 0) {
        return &engine->render.entities_2d.data[at];
    }
    const nmo_2dentity_state_t *state =
        (const nmo_2dentity_state_t *)eng_typed_state(engine, id, CKPGUID_2DENTITY);
    if (state == NULL) {
        return NULL;
    }
    eng_2d_t *entity = NULL;
    if (!ENG_VEC_PUSH(engine->render.entities_2d, entity)) {
        return NULL;
    }
    entity->id = id;
    entity->flags = state->flags;
    entity->rect = state->rect;
    entity->homogeneous = state->has_homogeneous_rect ? state->homogeneous_rect : state->rect;
    if (state->has_source_rect) {
        entity->source = state->source_rect;
    } else {
        entity->source.right = entity->source.bottom = 1.0f;
    }
    entity->z_order = state->has_z_order ? state->z_order : 0;
    entity->material = state->has_material ? eng_object_ref(engine, id, &state->material) : 0;
    entity->parent = state->has_parent ? eng_object_ref(engine, id, &state->parent) : 0;
    entity->destroyed = eng_map_get(&engine->destroyed, id) >= 0;
    if (!eng_map_put(&engine->render.entity_2d_map, id,
                     (int32_t)engine->render.entities_2d.count - 1)) {
        engine->render.entities_2d.count--;
        return NULL;
    }
    return entity;
}

void eng_2d_destroy(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t at = eng_map_get(&engine->render.entity_2d_map, id);
    if (at >= 0) {
        engine->render.entities_2d.data[at].destroyed = true;
    }
}

static void eng_load_2d_class(nmo_engine_t *engine, uint32_t class_id)
{
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(engine, class_id, &ids);
    for (size_t i = 0; i < count; i++) {
        eng_2d_entity(engine, ids[i]);
    }
    free(ids);
}

/* The 2D entities of every file loaded so far */
void eng_load_2d_entities(nmo_engine_t *engine)
{
    if (engine->render.entities_2d_modules != engine->modules.count) {
        engine->render.entities_2d_modules = engine->modules.count;
        eng_load_2d_class(engine, NMO_CID_2DENTITY);
    }
}

/* RCK2dEntity::GetHomogeneousRelativeRect: the parent's rect, or the render context's */
static nmo_rect_t eng_2d_homogeneous_relative(nmo_engine_t *engine, nmo_object_id_t parent)
{
    int32_t at = parent != 0 ? eng_map_get(&engine->render.entity_2d_map, parent) : -1;
    if (at >= 0) {
        return engine->render.entities_2d.data[at].rect;
    }
    const eng_screen_mode_t *mode = &eng_screen_modes[engine->render.screen_mode];
    nmo_rect_t rect = {0.0f, 0.0f, (float)mode->width, (float)mode->height};
    return rect;
}

/* One axis of a child's rect when its parent's goes from old to new (the STICK flags) */
static void eng_2d_stick(float child_lo, float child_hi, float old_lo, float old_hi, float new_lo,
                         float new_hi, bool stick_lo, bool stick_hi, float *lo, float *hi)
{
    float size = child_hi - child_lo;
    if (stick_lo) {
        *lo = child_lo - old_lo + new_lo;
        *hi = stick_hi ? new_hi - (old_hi - child_hi) : *lo + size;
    } else if (stick_hi) {
        *hi = new_hi - (old_hi - child_hi);
        *lo = *hi - size;
    } else {
        float parent = old_hi - old_lo;
        float ratio = parent != 0.0f ? ((child_lo + child_hi) * 0.5f - old_lo) / parent : 0.5f;
        float center = (new_hi - new_lo) * ratio + new_lo;
        *lo = center - size * 0.5f;
        *hi = *lo + size;
    }
}

static void eng_2d_set_rect_at(nmo_engine_t *engine, int32_t index, nmo_rect_t rect, bool keep_children)
{
    eng_2d_t *entity = &engine->render.entities_2d.data[index];
    nmo_rect_t old = entity->rect;
    if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        nmo_rect_t relative = eng_2d_homogeneous_relative(engine, entity->parent);
        float inv_w = 1.0f / (relative.right - relative.left);
        float inv_h = 1.0f / (relative.bottom - relative.top);
        nmo_rect_t h;
        h.left = (rect.left - relative.left) * inv_w;
        h.top = (rect.top - relative.top) * inv_h;
        h.right = h.left + (rect.right - rect.left) * inv_w;
        h.bottom = h.top + (rect.bottom - rect.top) * inv_h;
        entity->homogeneous = h;
    }
    entity->rect = rect;
    if (keep_children) {
        return;
    }
    nmo_object_id_t id = entity->id;
    for (size_t i = 0; i < engine->render.entities_2d.count; i++) {
        const eng_2d_t *child = &engine->render.entities_2d.data[i];
        if (child->parent != id || child->destroyed) {
            continue;
        }
        nmo_rect_t next;
        if (child->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
            float w = rect.right - rect.left, h = rect.bottom - rect.top;
            next.left = rect.left + child->homogeneous.left * w;
            next.top = rect.top + child->homogeneous.top * h;
            next.right = rect.left + child->homogeneous.right * w;
            next.bottom = rect.top + child->homogeneous.bottom * h;
        } else {
            eng_2d_stick(child->rect.left, child->rect.right, old.left, old.right, rect.left,
                         rect.right, (child->flags & CK_2DENTITY_STICKLEFT) != 0,
                         (child->flags & CK_2DENTITY_STICKRIGHT) != 0, &next.left, &next.right);
            eng_2d_stick(child->rect.top, child->rect.bottom, old.top, old.bottom, rect.top,
                         rect.bottom, (child->flags & CK_2DENTITY_STICKTOP) != 0,
                         (child->flags & CK_2DENTITY_STICKBOTTOM) != 0, &next.top, &next.bottom);
        }
        eng_2d_set_rect_at(engine, (int32_t)i, next, false);
    }
}

static int32_t eng_2d_index(nmo_engine_t *engine, nmo_object_id_t id)
{
    eng_load_2d_entities(engine);
    return eng_2d_entity(engine, id) != NULL ? eng_map_get(&engine->render.entity_2d_map, id) : -1;
}

bool eng_2d_set_rect(nmo_engine_t *engine, nmo_object_id_t id, nmo_rect_t rect, bool keep_children)
{
    int32_t at = eng_2d_index(engine, id);
    if (at < 0) {
        return false;
    }
    eng_2d_set_rect_at(engine, at, rect, keep_children);
    return true;
}

/* RCK2dEntity::SetHomogeneousRect, through SetRect */
static void eng_2d_set_homogeneous_at(nmo_engine_t *engine, int32_t index, nmo_rect_t h, bool keep_children)
{
    eng_2d_t *entity = &engine->render.entities_2d.data[index];
    entity->homogeneous = h;
    nmo_rect_t relative = eng_2d_homogeneous_relative(engine, entity->parent);
    float w = relative.right - relative.left, ht = relative.bottom - relative.top;
    nmo_rect_t rect = {relative.left + h.left * w, relative.top + h.top * ht,
                       relative.left + h.right * w, relative.top + h.bottom * ht};
    eng_2d_set_rect_at(engine, index, rect, keep_children);
}

bool eng_2d_set_position(nmo_engine_t *engine, nmo_object_id_t id, float x, float y, bool homogeneous,
                         bool keep_children, nmo_object_id_t ref)
{
    int32_t at = eng_2d_index(engine, id);
    if (at < 0) {
        return false;
    }
    eng_2d_t *entity = &engine->render.entities_2d.data[at];
    if (homogeneous) {
        if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
            nmo_rect_t h = entity->homogeneous;
            h.right += x - h.left;
            h.bottom += y - h.top;
            h.left = x;
            h.top = y;
            eng_2d_set_homogeneous_at(engine, at, h, keep_children);
        }
        return true;
    }
    if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        entity->flags |= CK_2DENTITY_UPDATEHOMOGENEOUSCOORD;
    }
    int32_t ref_at = ref != 0 ? eng_2d_index(engine, ref) : -1;
    if (ref_at >= 0) {
        x += engine->render.entities_2d.data[ref_at].rect.left;
        y += engine->render.entities_2d.data[ref_at].rect.top;
    }
    nmo_rect_t rect = engine->render.entities_2d.data[at].rect;
    rect.right += x - rect.left;
    rect.bottom += y - rect.top;
    rect.left = x;
    rect.top = y;
    eng_2d_set_rect_at(engine, at, rect, keep_children);
    return true;
}

bool eng_2d_set_size(nmo_engine_t *engine, nmo_object_id_t id, float width, float height, bool homogeneous,
                     bool keep_children)
{
    int32_t at = eng_2d_index(engine, id);
    if (at < 0) {
        return false;
    }
    eng_2d_t *entity = &engine->render.entities_2d.data[at];
    if (homogeneous) {
        if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
            nmo_rect_t h = entity->homogeneous;
            h.right = h.left + width;
            h.bottom = h.top + height;
            eng_2d_set_homogeneous_at(engine, at, h, keep_children);
        }
        return true;
    }
    if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        entity->flags |= CK_2DENTITY_UPDATEHOMOGENEOUSCOORD;
    }
    nmo_rect_t rect = entity->rect;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    eng_2d_set_rect_at(engine, at, rect, keep_children);
    return true;
}

/* RCK2dEntity::HierarchySetBackground: the entity and every child under it */
static void eng_2d_hierarchy_background(nmo_engine_t *engine, int32_t index, bool back, int depth)
{
    eng_2d_t *entity = &engine->render.entities_2d.data[index];
    if (back) {
        entity->flags |= CK_2DENTITY_BACKGROUND;
    } else {
        entity->flags &= ~(uint32_t)CK_2DENTITY_BACKGROUND;
    }
    nmo_object_id_t id = entity->id;
    for (size_t i = 0; depth < 1024 && i < engine->render.entities_2d.count; i++) {
        if (engine->render.entities_2d.data[i].parent == id) {
            eng_2d_hierarchy_background(engine, (int32_t)i, back, depth + 1);
        }
    }
}

bool eng_2d_set_background(nmo_engine_t *engine, nmo_object_id_t id, bool back)
{
    int32_t at = eng_2d_index(engine, id);
    if (at < 0) {
        return false;
    }
    const eng_2d_t *entity = &engine->render.entities_2d.data[at];
    if (entity->parent == 0 && ((entity->flags & CK_2DENTITY_BACKGROUND) != 0) != back) {
        eng_2d_hierarchy_background(engine, at, back, 0);
    }
    return true;
}

bool eng_2d_set_parent(nmo_engine_t *engine, nmo_object_id_t id, nmo_object_id_t parent)
{
    int32_t at = eng_2d_index(engine, id);
    int32_t parent_at = parent != 0 ? eng_2d_index(engine, parent) : -1;
    if (at < 0) {
        return false;
    }
    /* No cycle: the entity is none of the new parent's ancestors */
    for (int32_t p = parent_at, depth = 0; p >= 0 && depth < 1024; depth++) {
        const eng_2d_t *ancestor = &engine->render.entities_2d.data[p];
        if (ancestor->id == id) {
            return false;
        }
        p = ancestor->parent != 0 ? eng_map_get(&engine->render.entity_2d_map, ancestor->parent) : -1;
    }
    eng_2d_t *entity = &engine->render.entities_2d.data[at];
    if (parent_at < 0) {
        entity->parent = 0; /* a child of the background or foreground root, as it is */
        return true;
    }
    entity->parent = parent;
    if (engine->render.entities_2d.data[parent_at].flags & CK_2DENTITY_BACKGROUND) {
        entity->flags |= CK_2DENTITY_BACKGROUND;
    } else {
        entity->flags &= ~(uint32_t)CK_2DENTITY_BACKGROUND;
    }
    return true;
}

/* The rect homogeneous coordinates are relative to: the parent's, or the render context's */
static nmo_rect_t eng_2d_relative_rect(nmo_engine_t *engine, const eng_2d_t *entity,
                                       float width, float height)
{
    if (entity->parent != 0) {
        eng_2d_t *parent = eng_2d_entity(engine, entity->parent);
        if (parent != NULL) {
            return parent->rect;
        }
    }
    nmo_rect_t rect = {0.0f, 0.0f, width, height};
    return rect;
}

/* RCK2dEntity::UpdateExtents with the viewport the whole render context; false when clipped away */
static bool eng_2d_update_extents(nmo_engine_t *engine, eng_2d_t *entity, float width,
                                  float height, const int32_t *sprite_size)
{
    nmo_rect_t src = entity->source;
    if (!(entity->flags & CK_2DENTITY_USESRCRECT)) {
        /* a sprite's whole bitmap in pixels, another entity's in [0, 1] */
        src.left = src.top = 0.0f;
        src.right = sprite_size != NULL ? (float)sprite_size[0] : 1.0f;
        src.bottom = sprite_size != NULL ? (float)sprite_size[1] : 1.0f;
    }
    memset(&entity->extents, 0, sizeof(entity->extents));
    memset(&entity->source_extents, 0, sizeof(entity->source_extents));
    nmo_rect_t clip = {0.0f, 0.0f, width, height};
    if ((entity->flags & CK_2DENTITY_CLIPTOPARENT) && entity->parent != 0) {
        eng_2d_t *parent = eng_2d_entity(engine, entity->parent);
        if (parent != NULL) {
            clip.left = fmaxf(clip.left, parent->extents.left);
            clip.top = fmaxf(clip.top, parent->extents.top);
            clip.right = fminf(clip.right, parent->extents.right);
            clip.bottom = fminf(clip.bottom, parent->extents.bottom);
        }
    }
    float src_width = src.right - src.left;
    float src_height = src.bottom - src.top;
    nmo_rect_t rect = entity->rect;
    if (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        nmo_rect_t relative = eng_2d_relative_rect(engine, entity, width, height);
        float rw = relative.right - relative.left;
        float rh = relative.bottom - relative.top;
        rect.left = relative.left + entity->homogeneous.left * rw;
        rect.right = relative.left + entity->homogeneous.right * rw;
        rect.top = relative.top + entity->homogeneous.top * rh;
        rect.bottom = relative.top + entity->homogeneous.bottom * rh;
        entity->rect = rect;
    }
    float rect_width = rect.right - rect.left;
    float rect_height = rect.bottom - rect.top;
    if (rect.right < rect.left) {
        rect.right = rect.left;
    }
    if (rect.bottom < rect.top) {
        rect.bottom = rect.top;
    }
    float inv_width = rect_width > 0.0f ? 1.0f / rect_width : 0.0f;
    float inv_height = rect_height > 0.0f ? 1.0f / rect_height : 0.0f;
    if (rect.right < clip.left || rect.left > clip.right || rect.bottom < clip.top ||
        rect.top > clip.bottom) {
        return false;
    }
    float src_left = src.left;
    float src_top = src.top;
    if (rect.left < clip.left) {
        src_left += (clip.left - rect.left) * src_width * inv_width;
        rect.left = clip.left;
    }
    if (rect.top < clip.top) {
        src_top += (clip.top - rect.top) * src_height * inv_height;
        rect.top = clip.top;
    }
    if (rect.right > clip.right) {
        src.right = src.left + (rect_width - (rect.right - clip.right)) * src_width * inv_width;
        rect.right = clip.right;
    }
    if (rect.bottom > clip.bottom) {
        src.bottom = src.top + (rect_height - (rect.bottom - clip.bottom)) * src_height * inv_height;
        rect.bottom = clip.bottom;
    }
    entity->extents = rect;
    entity->source_extents.left = src_left;
    entity->source_extents.top = src_top;
    entity->source_extents.right = src.right;
    entity->source_extents.bottom = src.bottom;
    return true;
}

static int eng_2d_compare(const nmo_engine_t *engine, const int32_t *a, const int32_t *b)
{
    const eng_2d_t *ea = &engine->render.entities_2d.data[*a];
    const eng_2d_t *eb = &engine->render.entities_2d.data[*b];
    if (ea->z_order != eb->z_order) {
        return ea->z_order < eb->z_order ? -1 : 1;
    }
    return ea->id < eb->id ? -1 : ea->id > eb->id;
}

/* The children of a 2D entity (or of a root, for 0), by Z order, in a list the caller frees */
static int32_t *eng_2d_children(nmo_engine_t *engine, nmo_object_id_t parent, bool background,
                                size_t *out_count)
{
    *out_count = 0;
    size_t count = engine->render.entities_2d.count;
    int32_t *children = count > 0 ? (int32_t *)malloc(count * sizeof(int32_t)) : NULL;
    for (size_t i = 0; children != NULL && i < count; i++) {
        const eng_2d_t *entity = &engine->render.entities_2d.data[i];
        if (entity->destroyed || entity->parent != parent ||
            (parent == 0 && ((entity->flags & CK_2DENTITY_BACKGROUND) != 0) != background)) {
            continue;
        }
        children[(*out_count)++] = (int32_t)i;
    }
    /* insertion sort: stable, and the lists are short */
    for (size_t i = 1; i < *out_count; i++) {
        int32_t item = children[i];
        size_t j = i;
        while (j > 0 && eng_2d_compare(engine, &children[j - 1], &item) > 0) {
            children[j] = children[j - 1];
            j--;
        }
        children[j] = item;
    }
    return children;
}

static bool eng_push_draw(nmo_engine_t *engine, nmo_engine_draw_t **out)
{
    if (!ENG_VEC_PUSH(engine->render.draws, *out)) {
        return false;
    }
    (*out)->stencil = engine->render.stencil;
    return true;
}

int32_t eng_render_first_free_stencil_bit(nmo_engine_t *engine)
{
    for (int32_t bit = 0; bit < 8; bit++) {
        if (!(engine->render.stencil_used & (1u << bit))) {
            return bit;
        }
    }
    engine->render.stencil_used = 0;
    return 0;
}

void eng_render_use_stencil_bits(nmo_engine_t *engine, uint32_t bits)
{
    engine->render.stencil_used |= bits;
}

/* RCK2dEntity::Render: itself if shown and in the scene, then its children */
static void eng_render_2d(nmo_engine_t *engine, int32_t index, float width, float height)
{
    eng_2d_t *entity = &engine->render.entities_2d.data[index];
    nmo_object_id_t id = entity->id;
    int32_t flags = eng_object_flags(engine, entity->id);
    if (flags & OBJECT_HIERARCHICAL_HIDE) {
        return;
    }
    bool visible = (flags & OBJECT_VISIBLE) != 0 && eng_in_scene(engine, entity->id);
    size_t child_count = 0;
    int32_t *children = eng_2d_children(engine, entity->id, false, &child_count);
    if (!visible && child_count == 0) {
        free(children);
        return;
    }
    /* A CKSprite draws its own bitmap (RCKSprite::Draw), its material unused */
    eng_texture_t *sprite = eng_object_class(engine, id) == NMO_CID_SPRITE ? eng_texture(engine, id) : NULL;
    int32_t sprite_size[2] = {0, 0};
    bool has_bitmap = sprite != NULL && eng_bitmap_size(engine, sprite, &sprite_size[0], &sprite_size[1]);
    entity = &engine->render.entities_2d.data[index];
    bool clipped = !eng_2d_update_extents(engine, entity, width, height, sprite != NULL ? sprite_size : NULL);
    entity = &engine->render.entities_2d.data[index];
    nmo_engine_draw_t *draw = NULL;
    if (sprite != NULL) {
        if (!clipped && visible && has_bitmap && eng_push_draw(engine, &draw)) {
            draw->kind = NMO_ENGINE_DRAW_QUAD;
            draw->object = id;
            draw->rect[0] = floorf(entity->extents.left + 0.5f);
            draw->rect[1] = floorf(entity->extents.top + 0.5f);
            draw->rect[2] = floorf(entity->extents.right + 0.5f);
            draw->rect[3] = floorf(entity->extents.bottom + 0.5f);
            /* the source rect is in bitmap pixels */
            draw->uv[0] = entity->source_extents.left / (float)sprite_size[0];
            draw->uv[1] = entity->source_extents.top / (float)sprite_size[1];
            draw->uv[2] = entity->source_extents.right / (float)sprite_size[0];
            draw->uv[3] = entity->source_extents.bottom / (float)sprite_size[1];
            draw->texture = id;
            draw->color = draw->color2 = 0xFFFFFFFFu;
            draw->nearest = true;
            draw->background = (entity->flags & CK_2DENTITY_BACKGROUND) != 0;
            /* transparent: blended and alpha tested; opaque otherwise */
            draw->blend = true;
            draw->source_blend = sprite->transparent ? VXBLEND_SRCALPHA : VXBLEND_ONE;
            draw->dest_blend = sprite->transparent ? VXBLEND_INVSRCALPHA : VXBLEND_ZERO;
            draw->alpha_test = sprite->transparent;
        }
    } else if (!clipped && visible && entity->material != 0 && eng_push_draw(engine, &draw)) {
        eng_material_t *material = eng_material(engine, entity->material);
        draw->kind = NMO_ENGINE_DRAW_QUAD;
        draw->object = entity->id;
        draw->rect[0] = floorf(entity->extents.left + 0.5f);
        draw->rect[1] = floorf(entity->extents.top + 0.5f);
        draw->rect[2] = floorf(entity->extents.right + 0.5f);
        draw->rect[3] = floorf(entity->extents.bottom + 0.5f);
        draw->uv[0] = entity->source_extents.left;
        draw->uv[1] = entity->source_extents.top;
        draw->uv[2] = entity->source_extents.right;
        draw->uv[3] = entity->source_extents.bottom;
        draw->material = entity->material;
        draw->texture = material != NULL ? material->desc.texture : 0;
        draw->color = material != NULL ? eng_color_argb(material->desc.diffuse) : 0xFFFFFFFFu;
        draw->color2 = draw->color;
        draw->background = (entity->flags & CK_2DENTITY_BACKGROUND) != 0;
    }
    for (size_t i = 0; i < child_count; i++) {
        const eng_2d_t *child = &engine->render.entities_2d.data[children[i]];
        if (!clipped || !(child->flags & CK_2DENTITY_CLIPTOPARENT)) {
            eng_render_2d(engine, children[i], width, height);
        }
    }
    free(children);
    if (visible) {
        /* the post render callbacks */
        eng_render_args_t args = {NULL, width, height, id, false};
        eng_managers_render(engine, ENG_RENDER_ENTITY_2D, &args);
    }
}

void eng_render_2d_entity(nmo_engine_t *engine, nmo_object_id_t id, float width, float height)
{
    int32_t at = eng_2d_index(engine, id);
    if (at >= 0 && !engine->render.entities_2d.data[at].destroyed) {
        eng_render_2d(engine, at, width, height);
    }
}

static void eng_render_2d_root(nmo_engine_t *engine, bool background, float width, float height)
{
    size_t count = 0;
    int32_t *roots = eng_2d_children(engine, 0, background, &count);
    for (size_t i = 0; i < count; i++) {
        eng_render_2d(engine, roots[i], width, height);
    }
    free(roots);
}

/* ============================================================================
 * The frame
 * ============================================================================ */

/* An entity's scene graph priority: 10000 rendered first, else its Z order */
static int32_t eng_render_priority(const eng_entity_t *entity)
{
    return (entity->moveable_flags & VX_MOVEABLE_RENDERFIRST) ? 10000 : entity->render_priority;
}

typedef struct eng_priority_item {
    int32_t priority;
    size_t index;
} eng_priority_item_t;

static int eng_priority_compare(const void *a, const void *b)
{
    const eng_priority_item_t *x = (const eng_priority_item_t *)a, *y = (const eng_priority_item_t *)b;
    if (x->priority != y->priority) {
        return x->priority > y->priority ? -1 : 1;
    }
    return x->index < y->index ? -1 : x->index > y->index;
}

/* The scene graph's order, approximated: the entities' draws by priority, highest first,
   the others kept in order (the scene graph's hierarchy is not modelled) */
static void eng_sort_by_priority(nmo_engine_t *engine, size_t first, size_t end)
{
    size_t count = end - first;
    eng_priority_item_t *items = count > 1 ? (eng_priority_item_t *)malloc(count * sizeof(*items)) : NULL;
    nmo_engine_draw_t *copy = items != NULL ? (nmo_engine_draw_t *)malloc(count * sizeof(*copy)) : NULL;
    if (copy == NULL) {
        free(items);
        return;
    }
    nmo_engine_draw_t *draws = &engine->render.draws.data[first];
    for (size_t i = 0; i < count; i++) {
        int32_t at = eng_entity_index(engine, draws[i].object);
        items[i].priority = at >= 0 ? eng_render_priority(&engine->entities.data[at]) : 0;
        items[i].index = i;
    }
    qsort(items, count, sizeof(*items), eng_priority_compare);
    memcpy(copy, draws, count * sizeof(*copy));
    for (size_t i = 0; i < count; i++) {
        draws[i] = copy[items[i].index];
    }
    free(copy);
    free(items);
}

bool eng_add_entity_callback(nmo_engine_t *engine, const eng_entity_callback_t *callback)
{
    for (size_t i = 0; i < engine->render.entity_callbacks.count; i++) {
        eng_entity_callback_t *known = &engine->render.entity_callbacks.data[i];
        if (known->entity == callback->entity && known->kind == callback->kind &&
            (callback->kind == ENG_ENTITY_RENDER || (known->fn == callback->fn && known->node == callback->node))) {
            *known = *callback;
            return true;
        }
    }
    eng_entity_callback_t *added = NULL;
    if (!ENG_VEC_PUSH(engine->render.entity_callbacks, added)) {
        return false;
    }
    *added = *callback;
    return true;
}

void eng_remove_entity_callback(nmo_engine_t *engine, nmo_object_id_t entity, eng_entity_callback_kind_t kind,
                                eng_entity_callback_fn fn, int32_t node)
{
    size_t kept = 0;
    for (size_t i = 0; i < engine->render.entity_callbacks.count; i++) {
        const eng_entity_callback_t *callback = &engine->render.entity_callbacks.data[i];
        if (!(callback->entity == entity && callback->kind == kind && callback->fn == fn && callback->node == node)) {
            engine->render.entity_callbacks.data[kept++] = *callback;
        }
    }
    engine->render.entity_callbacks.count = kept;
}

nmo_engine_draw_t *eng_render_entity_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh,
                                          const float world[16], bool transparent)
{
    int32_t index = eng_entity_index(engine, entity);
    nmo_engine_draw_t *draw = NULL;
    if (index < 0 || !eng_push_draw(engine, &draw)) {
        return NULL;
    }
    uint32_t moveable = engine->entities.data[index].moveable_flags;
    draw->kind = NMO_ENGINE_DRAW_MESH;
    draw->object = entity;
    draw->mesh = mesh;
    memcpy(draw->world, world, sizeof(draw->world));
    /* and the moveable flags RCKMesh::Render applies */
    draw->transparent = transparent;
    draw->no_z_test = (moveable & VX_MOVEABLE_NOZBUFFERTEST) != 0;
    draw->no_z_write = (moveable & VX_MOVEABLE_NOZBUFFERWRITE) != 0;
    draw->z_only = (moveable & VX_MOVEABLE_ZBUFONLY) != 0;
    draw->stencil_only = (moveable & VX_MOVEABLE_STENCILONLY) != 0;
    draw->mirrored = (moveable & VX_MOVEABLE_INDIRECTMATRIX) != 0;
    return draw;
}

/* The entity's render callbacks of `kind`, in the order they were added; whether it has any */
static bool eng_run_entity_callbacks(nmo_engine_t *engine, nmo_object_id_t entity,
                                     eng_entity_callback_kind_t kind, bool transparent, bool run)
{
    bool found = false;
    for (size_t i = 0; i < engine->render.entity_callbacks.count; i++) {
        eng_entity_callback_t callback = engine->render.entity_callbacks.data[i];
        if (callback.entity == entity && (callback.kind == kind || (int)kind < 0)) {
            found = true;
            if (run && callback.fn != NULL) {
                callback.fn(engine, entity, callback.node, transparent);
            }
        }
    }
    return found;
}

/* The temporary callbacks removed after the frame */
static void eng_drop_temporary_callbacks(nmo_engine_t *engine)
{
    size_t kept = 0;
    for (size_t i = 0; i < engine->render.entity_callbacks.count; i++) {
        if (!engine->render.entity_callbacks.data[i].temporary) {
            engine->render.entity_callbacks.data[kept++] = engine->render.entity_callbacks.data[i];
        }
    }
    engine->render.entity_callbacks.count = kept;
}

/* Whether a 3D entity or one of its parents is hidden with its children */
static bool eng_hidden_by_parent(const nmo_engine_t *engine, const eng_entity_t *entity)
{
    while (entity->parent >= 0) {
        entity = &engine->entities.data[entity->parent];
        if (eng_object_flags(engine, entity->id) & OBJECT_HIERARCHICAL_HIDE) {
            return true;
        }
    }
    return false;
}

bool eng_render_hidden_by_parent(const nmo_engine_t *engine, int32_t index)
{
    return index >= 0 && eng_hidden_by_parent(engine, &engine->entities.data[index]);
}

/* CKMesh::GetBaryCenter and GetRadius, in the mesh's space; false for no mesh */
nmo_object_id_t eng_create_mesh(nmo_engine_t *engine, const char *name, int32_t vertex_count,
                                int32_t face_count, nmo_mesh_state_t **out_state)
{
    nmo_object_id_t id = vertex_count >= 0 && face_count >= 0 ? eng_create_object(engine, NMO_CID_MESH, name) : 0;
    const eng_module_t *module = id != 0 ? eng_module_of(engine, id, NULL) : NULL;
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    nmo_mesh_state_t *state = object != NULL ? (nmo_mesh_state_t *)nmo_object_get_state(object) : NULL;
    if (module == NULL || module->session == NULL || state == NULL) {
        return 0;
    }
    nmo_arena_t *arena = nmo_session_get_arena(module->session);
    size_t vertices = (size_t)(vertex_count > 0 ? vertex_count : 1);
    size_t faces = (size_t)(face_count > 0 ? face_count : 1);
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, vertices * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->vertex_colors = (uint32_t *)nmo_arena_alloc(arena, vertices * sizeof(uint32_t), _Alignof(uint32_t));
    state->vertex_specular = (uint32_t *)nmo_arena_alloc(arena, vertices * sizeof(uint32_t), _Alignof(uint32_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, faces * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices =
        (uint16_t *)nmo_arena_alloc(arena, faces * 3u * sizeof(uint16_t), _Alignof(uint16_t));
    if (state->vertices == NULL || state->vertex_colors == NULL || state->vertex_specular == NULL ||
        state->faces == NULL || state->face_vertex_indices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, vertices * sizeof(nmo_vertex_t));
    for (size_t v = 0; v < vertices; v++) {
        state->vertex_colors[v] = 0xFFFFFFFFu;
    }
    memset(state->vertex_specular, 0, vertices * sizeof(uint32_t));
    memset(state->faces, 0, faces * sizeof(nmo_face_t));
    memset(state->face_vertex_indices, 0, faces * 3u * sizeof(uint16_t));
    state->vertex_count = (uint32_t)vertex_count;
    state->face_count = (uint32_t)face_count;
    *out_state = state;
    return id;
}

bool eng_entity_set_current_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh)
{
    nmo_object_id_t local = 0;
    const eng_module_t *module = eng_module_of(engine, entity, NULL);
    nmo_object_t *object = entity != 0 ? eng_find_object(engine, entity) : NULL;
    nmo_3dentity_state_t *state = object != NULL
        ? (nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                   CKPGUID_3DENTITY)
        : NULL;
    if (state == NULL || (mesh != 0 && eng_module_of(engine, mesh, &local) != module)) {
        return false;
    }
    state->current_mesh = nmo_ref_from_id(local);
    return true;
}

bool eng_mesh_sphere(nmo_engine_t *engine, nmo_object_id_t mesh_id, eng_vector_t *center, float *radius)
{
    const nmo_mesh_state_t *mesh = mesh_id != 0 ? eng_mesh_state(engine, mesh_id) : NULL;
    if (mesh == NULL) {
        return false;
    }
    nmo_vector_t c = mesh->bary_center;
    *radius = mesh->radius;
    const eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh_id);
    if (copy != NULL && copy->geometry_changed) {
        c = copy->bary_center;
        *radius = copy->radius;
    }
    *center = (eng_vector_t){c.x, c.y, c.z};
    return true;
}

bool eng_render_entity_drawn(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine, id);
    if (index < 0) {
        return false;
    }
    const eng_entity_t *entity = &engine->entities.data[index];
    return !entity->destroyed && eng_in_scene(engine, id) && !eng_hidden_by_parent(engine, entity) &&
           (eng_object_flags(engine, id) & OBJECT_VISIBLE) != 0;
}

/* CKRenderedScene::PrepareCameras: a target camera and a target light turn to their target */
static void eng_prepare_cameras(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        nmo_object_t *object = entity->destroyed ? NULL : eng_find_object(engine, entity->id);
        uint32_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0;
        const nmo_ref_t *target_ref = NULL;
        nmo_object_id_t target = 0;
        if (nmo_type_registry_is_class_derived_from(engine->types,class_id,NMO_CID_TARGETCAMERA)) {
            target = eng_camera_target(engine,entity->id);
        } else if (class_id == NMO_CID_TARGETLIGHT) {
            const eng_light_t *light = eng_light(engine, entity->id);
            entity = &engine->entities.data[i];
            target = light != NULL && light->type != VX_LIGHTPOINT ? light->target : 0;
        }
        if (target_ref != NULL) target = eng_object_ref(engine,entity->id,target_ref);
        int32_t target_index = target != 0 ? eng_entity_index(engine, target) : -1;
        if (target_index >= 0) {
            eng_vector_t origin = {0.0f, 0.0f, 0.0f};
            eng_entity_look_at(engine, (int32_t)i, &origin, target_index, false);
        }
    }
}

bool eng_add_env_map(nmo_engine_t *engine, const eng_env_map_t *map)
{
    for (size_t m = 0; m < engine->render.env_maps.count; m++) {
        eng_env_map_t *known = &engine->render.env_maps.data[m];
        if (known->entity == map->entity && known->behavior == map->behavior) {
            *known = *map;
            return true;
        }
    }
    eng_env_map_t *added = NULL;
    if (!ENG_VEC_PUSH(engine->render.env_maps, added)) {
        return false;
    }
    *added = *map;
    return true;
}

static void eng_normalize_vector(eng_vector_t *v)
{
    float length = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
    if (length > 0.0f) {
        v->x /= length, v->y /= length, v->z /= length;
    }
}

static float eng_dot(const eng_vector_t *a, const eng_vector_t *b)
{
    return a->z * b->z + a->y * b->y + a->x * b->x;
}

/*
 * Environment Mapping's callback (Materials.dll): the UVs of the entity's
 * mesh from its normals (or, reflecting, from the directions to the
 * referential mirrored on them) along the referential's right and up axes,
 * seen from the entity or as the referential is oriented.
 */
static void eng_run_env_map(nmo_engine_t *engine, const eng_env_map_t *map, int32_t entity, nmo_object_id_t mesh)
{
    nmo_object_id_t referential_id = map->referential;
    eng_vector_t up = {0.0f, 1.0f, 0.0f}, right;
    if (referential_id == 0) {
        referential_id = engine->render.camera;
        int32_t camera = referential_id != 0 ? eng_entity_index(engine, referential_id) : -1;
        if (camera >= 0) {
            const float *y = engine->entities.data[camera].world.m[1];
            up = (eng_vector_t){y[0], y[1], y[2]};
        }
    }
    int32_t referential = referential_id != 0 ? eng_entity_index(engine, referential_id) : -1;
    if (referential < 0) {
        return;
    }
    const eng_matrix_t *ref_world = &engine->entities.data[referential].world;
    if ((map->options & 4u) || map->old_version) {
        right = (eng_vector_t){ref_world->m[0][0], ref_world->m[0][1], ref_world->m[0][2]};
        up = (eng_vector_t){ref_world->m[1][0], ref_world->m[1][1], ref_world->m[1][2]};
        eng_normalize_vector(&right);
        eng_normalize_vector(&up);
    } else {
        const float *at = engine->entities.data[entity].world.m[3], *from = ref_world->m[3];
        eng_vector_t dir = {at[0] - from[0], at[1] - from[1], at[2] - from[2]};
        eng_normalize_vector(&dir);
        float along = eng_dot(&up, &dir);
        up = (eng_vector_t){up.x - dir.x * along, up.y - dir.y * along, up.z - dir.z * along};
        eng_normalize_vector(&up);
        right = eng_vector_cross(&up, &dir);
    }
    if (map->options & 1u) {
        right = (eng_vector_t){-right.x, -right.y, -right.z};
    }
    if (!(map->options & 2u)) {
        up = (eng_vector_t){-up.x, -up.y, -up.z};
    }
    eng_vector_t u_axis, v_axis;
    eng_entity_transform_vector(engine, entity, &u_axis, &right, -1, true);
    eng_entity_transform_vector(engine, entity, &v_axis, &up, -1, true);
    eng_normalize_vector(&u_axis);
    eng_normalize_vector(&v_axis);
    u_axis = (eng_vector_t){u_axis.x * 0.45f, u_axis.y * 0.45f, u_axis.z * 0.45f};
    v_axis = (eng_vector_t){v_axis.x * 0.45f, v_axis.y * 0.45f, v_axis.z * 0.45f};
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, mesh);
    size_t stride = 0;
    int32_t count = 0;
    float *uvs = copy != NULL ? eng_mesh_uvs(engine, copy, map->channel, &stride, &count) : NULL;
    if (uvs == NULL) {
        return;
    }
    eng_vector_t eye = {0.0f, 0.0f, 0.0f};
    if (map->options & 8u) {
        const eng_vector_t world = {ref_world->m[3][0], ref_world->m[3][1], ref_world->m[3][2]};
        eng_entity_inverse_transform(engine, entity, &eye, &world, -1);
    }
    for (int32_t v = 0; v < count; v++) {
        const nmo_vertex_t *vertex = &copy->vertices[v];
        eng_vector_t n = {vertex->normal.x, vertex->normal.y, vertex->normal.z};
        if (map->options & 8u) {
            eng_vector_t d = {eye.x - vertex->position.x, eye.y - vertex->position.y, eye.z - vertex->position.z};
            float twice = eng_dot(&d, &n) * 2.0f;
            n = (eng_vector_t){twice * n.x - d.x, twice * n.y - d.y, twice * n.z - d.z};
            eng_normalize_vector(&n);
        }
        float *uv = (float *)((uint8_t *)uvs + (size_t)v * stride);
        uv[0] = eng_dot(&u_axis, &n) + 0.5f;
        uv[1] = eng_dot(&v_axis, &n) + 0.5f;
    }
    copy->version = ++engine->render.mesh_version;
}

/* The current mesh of a 3D entity, when it and the mesh are shown */
static nmo_object_id_t eng_entity_current_mesh(const nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)eng_typed_state(engine, id, CKPGUID_3DENTITY);
    nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
    return mesh != 0 && (eng_object_flags(engine, mesh) & OBJECT_VISIBLE) ? mesh : 0;
}

static void eng_frame_lights(nmo_engine_t *engine)
{
    engine->render.frame_lights.count = 0;
    for (size_t i = 0; i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        if (entity->destroyed || !eng_in_scene(engine, entity->id)) {
            continue;
        }
        const nmo_light_state_t *state =
            (const nmo_light_state_t *)eng_typed_state(engine, entity->id, CKPGUID_LIGHT);
        eng_light_t *light = state != NULL ? eng_light(engine, entity->id) : NULL;
        nmo_engine_light_t *out = NULL;
        if (light == NULL || !light->active || !ENG_VEC_PUSH(engine->render.frame_lights, out)) {
            continue;
        }
        entity = &engine->entities.data[i];
        out->id = entity->id;
        out->type = light->type;
        float power = state->has_light_power_chunk ? state->light_power : 1.0f;
        for (int c = 0; c < 3; c++) {
            out->color[c] = light->color[c] * power;
        }
        out->color[3] = light->color[3];
        for (int c = 0; c < 3; c++) {
            out->position[c] = entity->world.m[3][c];
            out->direction[c] = entity->world.m[2][c];
        }
        out->range = light->range;
        memcpy(out->attenuation, light->attenuation, sizeof(out->attenuation));
        out->inner_cone = light->hot_spot;
        out->outer_cone = light->fall_off;
        out->falloff = light->fall_off_shape;
        out->specular = light->specular;
    }
}

/* The nearest Place that sets a nonempty, non-full viewport wins, as in
 * CKSceneGraphNode's traversal. A full inner Place keeps an outer clip. */
static void eng_draw_portal_clips(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->render.draws.count; i++) {
        nmo_engine_draw_t *draw = &engine->render.draws.data[i];
        int32_t entity = eng_entity_index(engine,draw->object);
        for (size_t depth = 0; entity >= 0 && depth < engine->entities.count; depth++) {
            nmo_rect_t clip = eng_place_viewport_clip(engine,engine->entities.data[entity].id);
            if (clip.left != clip.right && clip.top != clip.bottom && (clip.left != 0 || clip.top != 0 || clip.right != 1 || clip.bottom != 1)) {
                draw->clipped = true; draw->clip_rect[0] = clip.left; draw->clip_rect[1] = clip.top; draw->clip_rect[2] = clip.right; draw->clip_rect[3] = clip.bottom; break;
            }
            entity = engine->entities.data[entity].parent;
        }
    }
}

/* The camera's projection in the frame (its eye, perspective or orthographic, fov, zoom and
   planes); the defaults for none */
static void eng_frame_camera(nmo_engine_t *engine, nmo_object_id_t id, nmo_engine_frame_t *out)
{
    out->camera = id;
    out->fov = 0.5f;
    out->zoom = 1.0f;
    out->near_plane = 1.0f;
    out->far_plane = 4000.0f;
    memset(out->view, 0, sizeof(out->view));
    out->view[0] = out->view[5] = out->view[10] = out->view[15] = 1.0f;
    out->orthographic = false;
    int32_t camera = id != 0 ? eng_entity_index(engine, id) : -1;
    const nmo_camera_state_t *camera_state =
        camera >= 0 ? (const nmo_camera_state_t *)eng_typed_state(engine, id, CKPGUID_CAMERA) : NULL;
    if (camera_state != NULL) {
        memcpy(out->view, engine->entities.data[camera].world.m, sizeof(out->view));
        uint32_t projection = camera_state->projection_type;
        eng_camera_projection_type(engine, id, &projection);
        out->orthographic = (projection & 1u) == 0;
        eng_camera_fov(engine, id, &out->fov);
        eng_camera_orthographic_zoom(engine, id, &out->zoom);
        eng_camera_planes(engine, id, &out->near_plane, &out->far_plane);
    } else {
        out->camera = 0;
    }
}

/* CKRenderedScene::Draw: what the render context draws of the scene, from the draws' end, as
   `frame` sets it (its eye and render options); a pass leaves the temporary callbacks */
static nmo_status_t eng_draw_scene(nmo_engine_t *engine, const nmo_engine_frame_t *frame, bool pass)
{
    eng_render_t *render = &engine->render;
    float width = (float)frame->width, height = (float)frame->height;
    eng_render_args_t args = {frame->view, width, height, 0, pass};
    if (frame->render_options & CK_RENDER_BACKGROUNDSPRITES) {
        eng_render_2d_root(engine, true, width, height);
    }
    eng_managers_render(engine, ENG_RENDER_PRE, &args);
    eng_managers_render(engine, ENG_RENDER_BACKGROUND, &args);
    size_t first_entity_draw = render->draws.count;
    bool prioritized = false;
    for (size_t i = 0; i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        if (entity->destroyed || !eng_in_scene(engine, entity->id) ||
            eng_hidden_by_parent(engine, entity)) {
            continue;
        }
        uint32_t flags = eng_object_flags(engine, entity->id);
        /* Scene traversal sees a character even without a mesh of its own.
         * A plain Hide can leave its children traversable; hierarchy hide cannot. */
        if (!(flags & OBJECT_HIERARCHICAL_HIDE) && nmo_engine_object_class(engine, entity->id) == NMO_CID_CHARACTER) {
            engine->entities.data[i].moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
        }
        if (!(flags & OBJECT_VISIBLE)) continue;
        const nmo_sprite3d_state_t *sprite =
            (const nmo_sprite3d_state_t *)eng_typed_state(engine, entity->id, CKPGUID_SPRITE3D);
        if (sprite != NULL) {
            if (!eng_draw_sprite3d(engine, entity, frame->camera != 0 ? frame->view : NULL)) {
                return NMO_ERR_NOMEM;
            }
            continue;
        }
        nmo_object_id_t mesh = eng_entity_current_mesh(engine, entity->id);
        const nmo_mesh_state_t *mesh_state = mesh != 0 ? eng_mesh_state(engine, mesh) : NULL;
        bool has_mesh = mesh_state != NULL && mesh_state->face_count != 0;
        nmo_object_id_t id = entity->id;
        /* RCK3dEntity::Render: a mesh or callbacks */
        bool has_callbacks = eng_run_entity_callbacks(engine, id, (eng_entity_callback_kind_t)-1, false, false);
        if (!has_mesh && !has_callbacks) {
            continue;
        }
        for (size_t m = 0; has_mesh && m < render->env_maps.count; m++) {
            if (render->env_maps.data[m].entity == entity->id) {
                eng_run_env_map(engine, &render->env_maps.data[m], (int32_t)i, mesh);
            }
        }
        /* RCK3dEntity::IsToBeRenderedLast */
        uint32_t moveable = engine->entities.data[i].moveable_flags;
        bool last = (moveable & VX_MOVEABLE_RENDERFIRST) ? false
                    : (moveable & VX_MOVEABLE_RENDERLAST) ? true
                    : has_mesh && eng_mesh_transparent(engine, mesh, mesh_state);
        prioritized = prioritized || eng_render_priority(&engine->entities.data[i]) != 0;
        eng_run_entity_callbacks(engine, id, ENG_ENTITY_PRE_RENDER, last, true);
        if (eng_run_entity_callbacks(engine, id, ENG_ENTITY_RENDER, last, true) || !has_mesh) {
            eng_run_entity_callbacks(engine, id, ENG_ENTITY_POST_RENDER, last, true);
            continue;
        }
        if (eng_render_entity_mesh(engine, id, mesh, engine->entities.data[i].world.m[0], last) == NULL) {
            return NMO_ERR_NOMEM;
        }
        eng_run_entity_callbacks(engine, id, ENG_ENTITY_POST_RENDER, last, true);
    }
    if (prioritized) {
        eng_sort_by_priority(engine, first_entity_draw, render->draws.count);
    }
    if (!pass) {
        render->env_maps.count = 0; /* temporary callbacks */
        eng_drop_temporary_callbacks(engine);
    }
    eng_managers_render(engine, ENG_RENDER_SCENE, &args);
    if (!pass) {
        eng_draw_portal_clips(engine);
    }
    if (frame->render_options & CK_RENDER_FOREGROUNDSPRITES) {
        eng_render_2d_root(engine, false, width, height);
    }
    eng_managers_render(engine, ENG_RENDER_OVERLAY, &args); /* the post render callbacks */
    eng_managers_render(engine, ENG_RENDER_POST, &args);    /* the post sprite render callbacks */
    return NMO_OK;
}

nmo_engine_frame_t *eng_render_begin_pass(nmo_engine_t *engine, nmo_object_id_t texture, int32_t slot)
{
    eng_render_t *render = &engine->render;
    eng_texture_t *target = eng_texture(engine, texture);
    int32_t width = 0, height = 0;
    if (render->open_pass >= 0 || target == NULL || slot < 0 || slot > 5 ||
        !eng_bitmap_size(engine, target, &width, &height) || width <= 0 || height <= 0) {
        return NULL;
    }
    nmo_engine_pass_t *pass = NULL;
    size_t *first = NULL;
    if (!ENG_VEC_PUSH(render->passes, pass)) {
        return NULL;
    }
    if (!ENG_VEC_PUSH(render->pass_firsts, first)) {
        render->passes.count--;
        return NULL;
    }
    *first = render->draws.count;
    pass->texture = texture;
    pass->slot = slot;
    pass->width = width;
    pass->height = height;
    pass->viewport[2] = width;
    pass->viewport[3] = height;
    nmo_engine_frame_t *frame = &pass->frame;
    frame->width = width;
    frame->height = height;
    frame->viewport[2] = width; /* the pass's viewport is the frame */
    frame->viewport[3] = height;
    eng_frame_camera(engine, 0, frame);
    frame->background_color = render->background_color;
    frame->fog_mode = render->fog_mode;
    frame->fog_color = render->fog_color;
    frame->fog_start = render->fog_start;
    frame->fog_end = render->fog_end;
    frame->fog_density = render->fog_density;
    frame->ambient_light = render->ambient_light;
    frame->render_options = render->render_options;
    frame->shading = render->shading;
    frame->use_textures = render->use_textures;
    frame->wireframe_layer = render->wireframe_layer;
    frame->antialias = render->antialias;
    render->open_pass = (int32_t)render->passes.count - 1;
    return frame;
}

void eng_render_end_pass(nmo_engine_t *engine)
{
    engine->render.open_pass = -1;
}

nmo_status_t eng_render_scene_pass(nmo_engine_t *engine, nmo_object_id_t texture, int32_t slot,
                                   const eng_scene_view_t *view)
{
    eng_render_t *render = &engine->render;
    nmo_engine_frame_t *frame = eng_render_begin_pass(engine, texture, slot);
    if (frame == NULL) {
        return NMO_OK;
    }
    nmo_engine_pass_t *pass = &render->passes.data[render->open_pass];
    eng_frame_camera(engine, view->camera, frame);
    memcpy(frame->view, view->eye, sizeof(frame->view));
    if (view->has_fov) {
        frame->fov = view->fov;
    }
    frame->background_color = view->background_color;
    frame->background_texture = view->background_texture;
    frame->render_options = view->render_options;
    /* CKRenderedScene::UpdateViewportSize: the camera's ratio, centered */
    if (view->render_options & CK_RENDER_USECAMERARATIO) {
        const nmo_camera_state_t *camera = view->camera != 0
            ? (const nmo_camera_state_t *)eng_typed_state(engine, view->camera, CKPGUID_CAMERA) : NULL;
        int32_t aw = camera != NULL && camera->aspect_width > 0 ? camera->aspect_width : 4;
        int32_t ah = camera != NULL && camera->aspect_height > 0 ? camera->aspect_height : 3;
        if (view->has_fov) {
            aw = ah = 1; /* a cube face's 1:1 */
        }
        int32_t right = pass->width, bottom = pass->height;
        int32_t w = right, h = bottom;
        if ((double)right / bottom >= (double)aw / ah) {
            w = aw * bottom / ah;
        } else {
            h = ah * right / aw;
        }
        pass->viewport[0] = (int32_t)((double)(right - w) * 0.5);
        pass->viewport[1] = (int32_t)((double)(bottom - h) * 0.5);
        pass->viewport[2] = w;
        pass->viewport[3] = h;
        frame->width = w > 0 ? w : 1;
        frame->height = h > 0 ? h : 1;
        frame->viewport[2] = frame->width;
        frame->viewport[3] = frame->height;
    }
    nmo_engine_frame_t copy = *frame; /* the passes may move as callbacks draw */
    nmo_object_id_t attached = render->camera; /* AttachViewpointToCamera for the render */
    if (view->camera != 0) {
        render->camera = view->camera;
    }
    render->pass_eye_set = true;
    memcpy(render->pass_eye, view->eye, sizeof(render->pass_eye));
    nmo_status_t status = eng_draw_scene(engine, &copy, true);
    render->pass_eye_set = false;
    render->camera = attached;
    eng_render_end_pass(engine);
    return status;
}

/* CKRenderedScene::UpdateViewportSize: the attached camera's ratio (4:3 without one), centered
   in the render context */
static void eng_render_camera_ratio(nmo_engine_t *engine)
{
    eng_render_t *render = &engine->render;
    const eng_screen_mode_t *mode = &eng_screen_modes[render->screen_mode];
    const nmo_camera_state_t *camera = render->camera != 0
        ? (const nmo_camera_state_t *)eng_typed_state(engine, render->camera, CKPGUID_CAMERA) : NULL;
    /* a camera without its ratio (made, not loaded) has CKCamera's 4:3 */
    int32_t aw = camera != NULL && camera->aspect_width > 0 ? camera->aspect_width : 4;
    int32_t ah = camera != NULL && camera->aspect_height > 0 ? camera->aspect_height : 3;
    int32_t right = mode->width > 0 ? mode->width : 1, bottom = mode->height > 0 ? mode->height : 1;
    int32_t w = right, h = bottom;
    if ((double)right / (double)bottom >= (double)aw / (double)ah) {
        w = aw * bottom / ah;
    } else {
        h = ah * right / aw;
    }
    render->viewport[0] = (int32_t)((double)(right - w) * 0.5);
    render->viewport[1] = (int32_t)((double)(bottom - h) * 0.5);
    render->viewport[2] = w;
    render->viewport[3] = h;
    render->viewport_set = true;
}

void eng_render_viewport(nmo_engine_t *engine, int32_t out[4])
{
    eng_render_t *render = &engine->render;
    /* the ratio holds while the option does, of the camera attached now */
    if (!render->making_views && (eng_render_settings(engine)->render_options & CK_RENDER_USECAMERARATIO)) {
        eng_render_camera_ratio(engine);
    }
    if (render->viewport_set) {
        memcpy(out, render->viewport, sizeof(render->viewport));
        return;
    }
    const eng_screen_mode_t *mode = &eng_screen_modes[render->screen_mode];
    out[0] = out[1] = 0;
    out[2] = mode->width;
    out[3] = mode->height;
}

void eng_render_set_view_rect(nmo_engine_t *engine, const nmo_rect_t *rect)
{
    eng_render_t *render = &engine->render;
    render->viewport[0] = (int32_t)rect->left;
    render->viewport[1] = (int32_t)rect->top;
    render->viewport[2] = (int32_t)(rect->right - rect->left);
    render->viewport[3] = (int32_t)(rect->bottom - rect->top);
    render->viewport_set = true;
}

void eng_render_request_view(nmo_engine_t *engine, const eng_view_request_t *request)
{
    if (engine->render.making_views) {
        return;
    }
    for (size_t i = 0; i < engine->render.view_requests.count; i++) {
        if (engine->render.view_requests.data[i].node == request->node) {
            engine->render.view_requests.data[i] = *request;
            return;
        }
    }
    eng_view_request_t *slot = NULL;
    if (ENG_VEC_PUSH(engine->render.view_requests, slot)) {
        *slot = *request;
    }
}

/* The views asked for this frame, each drawn as a pass draws, into the screen: the draws
   from the end; the requests are gone after */
static nmo_status_t eng_make_views(nmo_engine_t *engine, const nmo_engine_frame_t *main)
{
    eng_render_t *render = &engine->render;
    render->views.count = 0;
    render->view_firsts.count = 0;
    render->making_views = true;
    nmo_status_t status = NMO_OK;
    for (size_t i = 0; i < render->view_requests.count && status == NMO_OK; i++) {
        eng_view_request_t request = render->view_requests.data[i];
        nmo_engine_view_t *view = NULL;
        size_t *first = NULL;
        if (!ENG_VEC_PUSH(render->views, view) || !ENG_VEC_PUSH(render->view_firsts, first)) {
            status = NMO_ERR_NOMEM;
            break;
        }
        *first = render->draws.count;
        view->over_2d = request.over_2d;
        nmo_engine_frame_t *frame = &view->frame;
        *frame = *main;
        frame->passes = NULL;
        frame->pass_count = 0;
        frame->views = NULL;
        frame->view_count = 0;
        /* AttachViewpointToCamera, SetViewRect, Render(options | CLEARVIEWPORT |
           DONOTUPDATEEXTENTS), then the camera and the view rect back */
        nmo_object_id_t attached = render->camera;
        bool had_viewport = render->viewport_set;
        int32_t viewport[4];
        memcpy(viewport, render->viewport, sizeof(viewport));
        render->camera = request.camera;
        eng_frame_camera(engine, request.camera, frame);
        eng_render_set_view_rect(engine, &request.rect);
        frame->render_options = request.options | CK_RENDER_CLEARVIEWPORT | CK_RENDER_DONOTUPDATEEXTENTS;
        memcpy(frame->viewport, render->viewport, sizeof(frame->viewport));
        nmo_engine_frame_t copy = *frame; /* the views may move as callbacks draw */
        status = eng_draw_scene(engine, &copy, true);
        render->camera = attached;
        render->viewport_set = had_viewport;
        memcpy(render->viewport, viewport, sizeof(viewport));
    }
    render->making_views = false;
    render->view_requests.count = 0;
    return status;
}

nmo_status_t nmo_engine_render_frame(nmo_engine_t *engine, nmo_engine_frame_t *out)
{
    if (engine == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_render_t *render = &engine->render;
    /* The 2D entities of every file loaded so far */
    eng_load_2d_entities(engine);
    memset(out, 0, sizeof(*out));
    const eng_screen_mode_t *mode = &eng_screen_modes[render->screen_mode];
    out->width = mode->width;
    out->height = mode->height;

    eng_prepare_cameras(engine);
    eng_render_args_t args = {out->view, (float)mode->width, (float)mode->height, 0, false};
    eng_managers_render(engine, ENG_RENDER_PREPARE, &args);
    eng_frame_camera(engine, render->camera, out);
    out->background_color = render->background_color;
    out->background_texture = render->background_texture;
    out->fog_mode = render->fog_mode;
    out->fog_color = render->fog_color;
    out->fog_start = render->fog_start;
    out->fog_end = render->fog_end;
    out->fog_density = render->fog_density;
    out->ambient_light = render->ambient_light;
    eng_frame_lights(engine);

    render->draws.count = 0;
    render->vertices.count = 0;
    eng_render_settings(engine);
    memset(&render->stencil, 0, sizeof(render->stencil));
    if (render->render_options & CK_RENDER_CLEARSTENCIL) {
        render->stencil_used = 0;
    }
    out->render_options = render->render_options;
    eng_render_viewport(engine, out->viewport);
    out->shading = render->shading;
    out->use_textures = render->use_textures;
    out->wireframe_layer = render->wireframe_layer;
    out->antialias = render->antialias;
    /* The passes the blocks asked for, drawn before the frame */
    render->passes.count = 0;
    render->pass_firsts.count = 0;
    render->open_pass = -1;
    eng_managers_render(engine, ENG_RENDER_PASSES, &args);
    render->open_pass = -1;
    memset(&render->stencil, 0, sizeof(render->stencil));
    size_t first = render->draws.count;
    nmo_status_t status = eng_draw_scene(engine, out, false);
    size_t main_end = render->draws.count;
    if (status == NMO_OK) {
        status = eng_make_views(engine, out);
    }
    if (status != NMO_OK) {
        return status;
    }

    out->lights = render->frame_lights.data;
    out->light_count = (int32_t)render->frame_lights.count;
    out->draws = render->draws.data + first;
    out->draw_count = (int32_t)(main_end - first);
    for (size_t v = 0; v < render->views.count; v++) {
        nmo_engine_frame_t *frame = &render->views.data[v].frame;
        size_t end = v + 1 < render->views.count ? render->view_firsts.data[v + 1] : render->draws.count;
        frame->lights = render->frame_lights.data;
        frame->light_count = (int32_t)render->frame_lights.count;
        frame->draws = render->draws.data + render->view_firsts.data[v];
        frame->draw_count = (int32_t)(end - render->view_firsts.data[v]);
        frame->vertices = render->vertices.data;
    }
    out->views = render->views.data;
    out->view_count = (int32_t)render->views.count;
    out->vertices = render->vertices.data;
    for (size_t p = 0; p < render->passes.count; p++) {
        nmo_engine_frame_t *frame = &render->passes.data[p].frame;
        size_t end = p + 1 < render->passes.count ? render->pass_firsts.data[p + 1] : first;
        frame->lights = out->lights;
        frame->light_count = out->light_count;
        frame->draws = render->draws.data + render->pass_firsts.data[p];
        frame->draw_count = (int32_t)(end - render->pass_firsts.data[p]);
        frame->vertices = out->vertices;
    }
    out->passes = render->passes.data;
    out->pass_count = (int32_t)render->passes.count;
    return NMO_OK;
}

/* ============================================================================
 * Texture images
 * ============================================================================ */

/* An RGBA copy of a decoded image of `channels` channels, malloc'ed */
static uint8_t *eng_rgba_copy(const uint8_t *pixels, int width, int height, int channels)
{
    size_t count = (size_t)width * (size_t)height;
    uint8_t *rgba = pixels != NULL && count > 0 ? (uint8_t *)malloc(count * 4u) : NULL;
    for (size_t i = 0; rgba != NULL && i < count; i++) {
        for (int c = 0; c < 4; c++) {
            rgba[i * 4 + (size_t)c] = c < channels ? pixels[i * (size_t)channels + (size_t)c] : 0xFF;
        }
        if (channels == 1 || channels == 2) {
            rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4];
            rgba[i * 4 + 3] = channels == 2 ? pixels[i * 2 + 1] : 0xFF;
        }
    }
    return rgba;
}

/* The pixels of a texture's or sprite's slot, as its reader, raw planes or file give them */
static uint8_t *eng_decode_slot(nmo_engine_t *engine, const nmo_bitmap_slots_t *state,
                                const eng_texture_t *texture, int32_t slot, nmo_arena_t *arena,
                                int *w, int *h, int *ch)
{
    uint32_t index = (uint32_t)slot;
    const char *loaded = (size_t)slot < texture->files.count ? texture->files.data[slot] : NULL;
    switch (loaded != NULL ? CKTEXTURE_BITMAP_NONE : state->kind) {
    case CKTEXTURE_BITMAP_READER:
        if (state->reader_slots != NULL && index < state->slot_count &&
            state->reader_slots[index].data != NULL) {
            const nmo_texture_reader_slot_t *rs = &state->reader_slots[index];
            uint8_t *pixels =
                nmo_stbi_load_from_memory(arena, rs->data, (int)rs->data_size, w, h, ch, 4);
            if (pixels != NULL) {
                *ch = 4;
                size_t count = (size_t)(*w) * (size_t)(*h);
                if (rs->alpha_plane != NULL && rs->alpha_plane_size > 0) {
                    size_t usable = rs->alpha_plane_size < count ? rs->alpha_plane_size : count;
                    for (size_t i = 0; i < usable; i++) {
                        pixels[i * 4 + 3] = rs->alpha_plane[i];
                    }
                } else if (rs->format_type == 2 && rs->alpha_count == 1) {
                    for (size_t i = 0; i < count; i++) {
                        pixels[i * 4 + 3] = (uint8_t)rs->alpha_value;
                    }
                }
            }
            return pixels;
        }
        break;
    case CKTEXTURE_BITMAP_RAW:
        if (state->raw_slots != NULL && index < state->slot_count) {
            const nmo_texture_raw_slot_t *rs = &state->raw_slots[index];
            uint8_t *pixels = NULL;
            if ((rs->compression & 0xFu) == 0 && rs->width > 0 && rs->height > 0 &&
                nmo_image_reconstruct_pixels(rs->red_data, rs->green_data, rs->blue_data,
                                             rs->alpha_data, rs->red_size, rs->green_size,
                                             rs->blue_size, rs->alpha_size, rs->width,
                                             rs->height, rs->bits_per_pixel, arena, &pixels,
                                             ch) == NMO_OK) {
                *w = rs->width;
                *h = rs->height;
                return pixels;
            }
        }
        break;
    case CKTEXTURE_BITMAP_BITMAP2:
        if (state->bitmap2_slots != NULL && index < state->slot_count &&
            state->bitmap2_slots[index].buffer != NULL) {
            const uint8_t *data = NULL;
            size_t size = 0;
            nmo_texture_bitmap2_image(&state->bitmap2_slots[index], &data, &size, NULL);
            uint8_t *pixels = nmo_stbi_load_from_memory(arena, data, (int)size, w, h, ch, 4);
            if (pixels != NULL) {
                *ch = 4;
            }
            return pixels;
        }
        break;
    default:
        break;
    }
    /* An external file: the slot's file name, read where the host finds it */
    const char *name = loaded != NULL ? loaded
                       : state->slot_filenames != NULL && index < state->slot_count
                           ? state->slot_filenames[index]
                           : NULL;
    size_t size = 0;
    unsigned char *file = name != NULL && name[0] != '\0' ? eng_read_path_file(engine, name, 0, &size) : NULL;
    uint8_t *pixels =
        file != NULL ? nmo_stbi_load_from_memory(arena, file, (int)size, w, h, ch, 4) : NULL;
    free(file);
    if (pixels != NULL) {
        *ch = 4;
    }
    return pixels;
}

/* CKBitmapData's transparent color (black unless set): those pixels are clear */
static void eng_key_image(const eng_texture_t *texture, uint8_t *rgba, int w, int h)
{
    if (!texture->transparent || rgba == NULL) {
        return;
    }
    uint32_t key = texture->transparent_color;
    uint8_t r = (uint8_t)(key >> 16), g = (uint8_t)(key >> 8), b = (uint8_t)key;
    size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; i++) {
        uint8_t *p = &rgba[i * 4];
        if (p[0] == r && p[1] == g && p[2] == b) {
            p[3] = 0;
        }
    }
}

static eng_image_t *eng_find_image(nmo_engine_t *engine, nmo_object_id_t texture)
{
    for (size_t i = 0; i < engine->render.images.count; i++) {
        if (engine->render.images.data[i].texture == texture) {
            return &engine->render.images.data[i];
        }
    }
    return NULL;
}

void eng_texture_changed(nmo_engine_t *engine, nmo_object_id_t texture)
{
    eng_image_t *image = eng_find_image(engine, texture);
    if (image != NULL) {
        image->slot = -1;
    }
    /* Its user mipmap levels are read again, keyed as the image now is */
    for (size_t i = 0; i < engine->render.user_mipmaps.count; i++) {
        eng_image_t *level = &engine->render.user_mipmaps.data[i];
        if (level->texture == texture) {
            free(level->rgba);
            level->rgba = NULL;
            level->texture = 0;
        }
    }
}

bool nmo_engine_texture_image(nmo_engine_t *engine, nmo_object_id_t texture,
                              nmo_engine_image_t *out)
{
    if (engine == NULL || out == NULL) {
        return false;
    }
    eng_texture_t *slots = eng_texture(engine, texture);
    nmo_bitmap_slots_t bitmap;
    if (slots == NULL || !eng_bitmap_view(engine, texture, &bitmap)) {
        return false;
    }
    const nmo_bitmap_slots_t *state = &bitmap;
    int32_t slot = slots->current_slot;
    eng_image_t *image = eng_find_image(engine, texture);
    if (image == NULL) {
        if (!ENG_VEC_PUSH(engine->render.images, image)) {
            return false;
        }
        image->texture = texture;
        image->slot = -1;
    }
    if (image->slot != slot) {
        free(image->rgba);
        image->rgba = NULL;
        image->width = image->height = 0;
        image->slot = slot;
        image->version = ++engine->render.image_version;
        const uint8_t *frame = NULL;
        int32_t fw = 0, fh = 0;
        if (slots->movie && slots->movie_reader != NULL &&
            eng_movie_frame(slots->movie_reader, slot, &frame, &fw, &fh)) {
            image->rgba = eng_rgba_copy(frame, fw, fh, 4);
            image->width = image->rgba != NULL ? fw : 0;
            image->height = image->rgba != NULL ? fh : 0;
            out->width = image->width;
            out->height = image->height;
            out->rgba = image->rgba;
            out->version = image->version;
            out->mipmapped = slots->mipmap_level != 0;
            return image->rgba != NULL;
        }
        const eng_surface_t *surface =
            (size_t)slot < slots->surfaces.count && slots->surfaces.data[slot].rgba != NULL
                ? &slots->surfaces.data[slot]
                : NULL;
        int w = 0, h = 0, ch = 0;
        if (surface != NULL) {
            w = surface->width;
            h = surface->height;
            image->rgba = eng_rgba_copy(surface->rgba, w, h, 4);
        } else {
            nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 16);
            uint8_t *pixels = arena != NULL ? eng_decode_slot(engine, state, slots, slot, arena, &w, &h, &ch)
                                            : NULL;
            image->rgba = eng_rgba_copy(pixels, w, h, ch);
            if (arena != NULL) {
                nmo_arena_destroy(arena);
            }
        }
        if (image->rgba != NULL) {
            image->width = w;
            image->height = h;
            eng_key_image(slots, image->rgba, w, h);
        }
    }
    out->width = image->width;
    out->height = image->height;
    out->rgba = image->rgba;
    out->version = image->version;
    out->mipmapped = slots->mipmap_level != 0;
    return image->rgba != NULL;
}

static const nmo_texture_state_t *eng_texture_state(const nmo_engine_t *engine,
                                                   nmo_object_id_t texture);
static uint8_t *eng_decode_user_level(const nmo_texture_state_t *state, int32_t level, int32_t *w,
                                      int32_t *h);

/* The surface entry of `slot`, the list grown to it; NULL without memory */
static eng_surface_t *eng_surface_entry(eng_texture_t *texture, int32_t slot)
{
    while (texture->surfaces.count <= (size_t)slot) {
        eng_surface_t *item = NULL;
        if (!ENG_VEC_PUSH(texture->surfaces, item)) {
            return NULL;
        }
    }
    return &texture->surfaces.data[slot];
}

eng_surface_t *eng_texture_surface(nmo_engine_t *engine, eng_texture_t *texture, int32_t slot)
{
    slot = slot < 0 ? texture->current_slot : slot;
    if (slot < 0 || slot >= texture->slot_count || texture->movie) {
        return NULL;
    }
    eng_surface_t *surface = eng_surface_entry(texture, slot);
    if (surface == NULL || surface->rgba != NULL) {
        return surface != NULL && surface->rgba != NULL ? surface : NULL;
    }
    nmo_bitmap_slots_t bitmap;
    nmo_arena_t *arena = eng_bitmap_view(engine, texture->id, &bitmap) ? nmo_arena_create(NULL, 1u << 16) : NULL;
    int w = 0, h = 0, ch = 0;
    uint8_t *pixels = arena != NULL ? eng_decode_slot(engine, &bitmap, texture, slot, arena, &w, &h, &ch) : NULL;
    uint8_t *rgba = eng_rgba_copy(pixels, w, h, ch);
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    surface = eng_surface_entry(texture, slot); /* decoding may have grown the list */
    if (rgba == NULL || surface == NULL) {
        free(rgba);
        return NULL;
    }
    *surface = (eng_surface_t){w, h, rgba};
    return surface;
}

/* The texture's size: its first image's (CKBitmapData keeps one for every slot) */
bool eng_bitmap_size(nmo_engine_t *engine, eng_texture_t *texture, int32_t *w, int32_t *h)
{
    for (int32_t slot = 0; slot < texture->slot_count; slot++) {
        const eng_surface_t *surface = eng_texture_surface(engine, texture, slot);
        if (surface != NULL) {
            *w = surface->width;
            *h = surface->height;
            return true;
        }
    }
    return false;
}

bool eng_texture_create(nmo_engine_t *engine, eng_texture_t *texture, int32_t width, int32_t height,
                        int32_t slot)
{
    int32_t w = 0, h = 0;
    if (slot < 0 || width <= 0 || height <= 0 ||
        (!(texture->slot_count <= 1 && slot == 0) && eng_bitmap_size(engine, texture, &w, &h) &&
         (w != width || h != height))) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    uint8_t *rgba = (uint8_t *)calloc(count, 4u);
    eng_surface_t *surface = rgba != NULL ? eng_surface_entry(texture, slot) : NULL;
    if (surface == NULL) {
        free(rgba);
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        rgba[i * 4 + 3] = 0xFF; /* opaque black */
    }
    free(surface->rgba);
    *surface = (eng_surface_t){width, height, rgba};
    if ((size_t)slot < texture->files.count) {
        free(texture->files.data[slot]); /* no file any more */
        texture->files.data[slot] = NULL;
    }
    if (slot >= texture->slot_count) {
        texture->slot_count = slot + 1;
    }
    if (texture->current_slot < 0 || texture->current_slot >= texture->slot_count) {
        texture->current_slot = slot;
    }
    /* Another size drops the user mipmap levels (RCKTexture::Create) */
    if (w != width || h != height) {
        for (size_t l = 0; l < texture->levels.count; l++) {
            free(texture->levels.data[l].rgba);
        }
        texture->levels.count = 0;
        texture->user_levels = false;
    }
    eng_texture_changed(engine, texture->id);
    return true;
}

static bool eng_power_of_two(int32_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

bool eng_texture_use_user_levels(nmo_engine_t *engine, eng_texture_t *texture)
{
    if (texture->user_levels) {
        return true;
    }
    const nmo_texture_state_t *state = eng_texture_state(engine, texture->id);
    int32_t file_levels = state != NULL && state->has_user_mipmaps && state->user_mipmaps != NULL
                              ? (int32_t)state->user_mipmap_count
                              : 0;
    int32_t w = 0, h = 0;
    if (file_levels == 0 && (texture->slot_count != 1 || !eng_bitmap_size(engine, texture, &w, &h) ||
                             !eng_power_of_two(w) || !eng_power_of_two(h))) {
        return false;
    }
    int32_t count = file_levels;
    for (int32_t side = w > h ? w : h; file_levels == 0 && side > 1; side >>= 1) {
        count++;
    }
    for (int32_t l = 0; l < count; l++) {
        eng_surface_t *level = NULL;
        if (!ENG_VEC_PUSH(texture->levels, level)) {
            return false;
        }
        if (file_levels > 0) {
            level->rgba = eng_decode_user_level(state, l + 1, &level->width, &level->height);
        } else {
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
            *level = (eng_surface_t){w, h, (uint8_t *)calloc((size_t)w * (size_t)h, 4u)};
        }
    }
    texture->user_levels = true;
    eng_texture_changed(engine, texture->id);
    return true;
}

/* ============================================================================
 * VxResizeImage32 (VxMath's VxBlitEngine::ResizeImage for 32-bit images)
 * ============================================================================ */

/* Its two pixels mixed, `t` of the second (16.16 fraction), each byte on its own */
static uint32_t eng_lerp_pixel(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t s = 0x10000u - t;
    uint32_t g = (t * ((b >> 8) & 0xFFu) + s * ((a >> 8) & 0xFFu)) & 0xFFFF00FFu;
    return ((s * ((a >> 16) & 0xFFu) + t * ((b >> 16) & 0xFFu)) & 0xFFFF0000u) |
           ((s * ((a >> 16) & 0xFF00u) + t * ((b >> 16) & 0xFF00u)) & 0xFF000000u) |
           ((((t * (b & 0xFFu) + s * (a & 0xFFu)) >> 8) | g) >> 8);
}

/* Sums of the bytes of pixels (each byte times its 16.16 weight) back to a pixel, times `scale` */
static uint32_t eng_pack_sums(uint32_t scale, const uint32_t c[4])
{
    return ((((scale * (c[1] >> 16)) & 0xFFFF00FFu) | ((scale * (c[0] >> 16)) >> 8)) >> 8) |
           ((scale * (c[2] >> 16)) & 0xFFFF0000u) | ((scale * ((c[3] >> 8) & 0xFFFF00u)) & 0xFF000000u);
}

static void eng_add_pixel(uint32_t c[4], uint32_t pixel)
{
    c[0] += (pixel & 0xFFu) << 16;
    c[1] += (pixel & 0xFF00u) << 8;
    c[2] += pixel & 0xFF0000u;
    c[3] += (pixel >> 8) & 0xFF0000u;
}

static void eng_add_weighted(uint32_t c[4], uint32_t pixel, uint32_t weight)
{
    c[0] += weight * (pixel & 0xFFu);
    c[1] += weight * ((pixel >> 8) & 0xFFu);
    c[2] += weight * ((pixel >> 16) & 0xFFu);
    c[3] += weight * (pixel >> 24);
}

typedef struct eng_resize {
    int32_t w1, w2, h1;
    uint32_t wr1, wr2, hr2;
} eng_resize_t;

/* A line of w1 pixels to w2: each the average of the pixels it covers (ResizeHLine_EqualY_ShrinkX) */
static void eng_shrink_line(const eng_resize_t *r, const uint32_t *src, uint32_t *dst)
{
    uint32_t pos = 0;
    int32_t x = 0;
    for (int32_t out = 0; out < r->w2; out++) {
        uint32_t c[4] = {0, 0, 0, 0};
        eng_add_weighted(c, src[x], 0x10000u - (pos & 0xFFFFu));
        uint32_t next = pos + r->wr1;
        int32_t end = (int32_t)(next >> 16);
        for (int32_t i = x + 1; i < end; i++) {
            eng_add_pixel(c, src[i]);
        }
        int32_t at = end > x + 1 ? end : x + 1;
        if (end < r->w1 && next != 0) {
            eng_add_weighted(c, src[at], next & 0xFFFFu);
        }
        x = end;
        pos = next;
        dst[out] = eng_pack_sums(r->wr2, c);
    }
}

/* A line of w1 pixels to w2, mixing neighbours, then the last pixel (ResizeHLine_EqualY_GrowX) */
static void eng_grow_line(const eng_resize_t *r, const uint32_t *src, uint32_t *dst)
{
    int32_t out = 0;
    int32_t limit = (r->w1 << 16) - 0x10000;
    for (uint32_t pos = 0; (int32_t)pos < limit; pos += r->wr1) {
        uint32_t i = pos >> 16;
        dst[out++] = eng_lerp_pixel(src[i], src[i + 1], pos & 0xFFFFu);
    }
    for (; out < r->w2; out++) {
        dst[out] = src[r->w1 - 1];
    }
}

static void eng_resize_line(const eng_resize_t *r, const uint32_t *src, uint32_t *dst)
{
    if (r->w1 == r->w2) {
        memcpy(dst, src, (size_t)r->w2 * sizeof(uint32_t));
    } else if (r->w1 > r->w2) {
        eng_shrink_line(r, src, dst);
    } else {
        eng_grow_line(r, src, dst);
    }
}

void eng_resize_image32(const uint32_t *src, int32_t w1, int32_t h1, uint32_t *dst, int32_t w2, int32_t h2)
{
    if (src == NULL || dst == NULL || w1 <= 0 || h1 <= 0 || w2 <= 0 || h2 <= 0) {
        return;
    }
    eng_resize_t r = {w1, w2, h1, (uint32_t)((w1 << 16) / w2), (uint32_t)((w2 << 16) / w1),
                      (uint32_t)((h2 << 16) / h1)};
    uint32_t hr1 = (uint32_t)((h1 << 16) / h2);
    uint32_t *line = (uint32_t *)malloc(((size_t)w1 + 1u) * sizeof(uint32_t));
    if (line == NULL) {
        return;
    }
    if (h1 == h2) {
        for (int32_t y = 0; y < h1; y++) {
            eng_resize_line(&r, &src[(size_t)y * (size_t)w1], &dst[(size_t)y * (size_t)w2]);
        }
    } else if (h1 > h2) {
        /* Each row the average of the rows it covers (ResizeHLine_ShrinkY_*) */
        uint32_t start = 0;
        for (int32_t y = 0; y < h2; y++) {
            uint32_t end = start + hr1;
            const uint32_t *row = &src[(size_t)(start >> 16) * (size_t)w1];
            int32_t end_row = (int16_t)(end >> 16), full = end_row - (int16_t)(start >> 16) - 1;
            uint32_t *sums = w1 == w2 ? &dst[(size_t)y * (size_t)w2] : line;
            for (int32_t x = 0; x < w1; x++) {
                uint32_t c[4] = {0, 0, 0, 0};
                eng_add_weighted(c, row[x], 0x10000u - (start & 0xFFFFu));
                const uint32_t *below = &row[x];
                for (int32_t k = 0; k < full; k++) {
                    below += w1;
                    eng_add_pixel(c, *below);
                }
                if (end_row < h1) {
                    eng_add_weighted(c, below[w1], end & 0xFFFFu);
                }
                sums[x] = eng_pack_sums(r.hr2, c);
            }
            if (w1 != w2) {
                eng_resize_line(&r, line, &dst[(size_t)y * (size_t)w2]);
            }
            start = end;
        }
    } else {
        /* Each row mixes the two rows about it (ResizeHLine_GrowY_*), then the last row repeats */
        int32_t limit = (h1 << 16) - 0x10000, y = 0;
        for (uint32_t pos = 0; (int32_t)pos < limit; pos += hr1, y++) {
            const uint32_t *row = &src[(size_t)(pos >> 16) * (size_t)w1];
            uint32_t *mixed = w1 == w2 ? &dst[(size_t)y * (size_t)w2] : line;
            for (int32_t x = 0; x < w1; x++) {
                mixed[x] = eng_lerp_pixel(row[x], row[x + w1], pos & 0xFFFFu);
            }
            if (w1 != w2) {
                eng_resize_line(&r, line, &dst[(size_t)y * (size_t)w2]);
            }
        }
        for (; y < h2; y++) {
            eng_resize_line(&r, &src[(size_t)(h1 - 1) * (size_t)w1], &dst[(size_t)y * (size_t)w2]);
        }
    }
    free(line);
}

bool eng_texture_resize(nmo_engine_t *engine, eng_texture_t *texture, int32_t width, int32_t height)
{
    int32_t w = 0, h = 0;
    if (texture->movie || texture->slot_count == 0 || !eng_bitmap_size(engine, texture, &w, &h) ||
        width <= 0 || height <= 0) {
        return false;
    }
    if (w == width && h == height) {
        return true;
    }
    size_t count = (size_t)width * (size_t)height;
    for (int32_t slot = 0; slot < texture->slot_count; slot++) {
        eng_surface_t *surface = eng_texture_surface(engine, texture, slot);
        uint8_t *rgba = (uint8_t *)malloc(count * 4u);
        if (rgba == NULL) {
            return false;
        }
        if (surface != NULL) {
            eng_resize_image32((const uint32_t *)surface->rgba, surface->width, surface->height,
                               (uint32_t *)rgba, width, height);
            free(surface->rgba);
        } else {
            for (size_t i = 0; i < count; i++) {
                rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0;
                rgba[i * 4 + 3] = 0xFF; /* a slot without an image: opaque black */
            }
            surface = eng_surface_entry(texture, slot);
            if (surface == NULL) {
                free(rgba);
                return false;
            }
        }
        *surface = (eng_surface_t){width, height, rgba};
    }
    eng_texture_changed(engine, texture->id);
    return true;
}

static const nmo_texture_state_t *eng_texture_state(const nmo_engine_t *engine,
                                                   nmo_object_id_t texture)
{
    nmo_object_t *object = engine != NULL && texture != 0 ? eng_find_object(engine, texture) : NULL;
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_TEXTURE
               ? (const nmo_texture_state_t *)nmo_object_get_state(object)
               : NULL;
}

/* The engine's state of a texture when it made one */
static const eng_texture_t *eng_texture_known(const nmo_engine_t *engine, nmo_object_id_t texture)
{
    for (size_t i = 0; engine != NULL && i < engine->media.textures.count; i++) {
        if (engine->media.textures.data[i].id == texture) {
            return &engine->media.textures.data[i];
        }
    }
    return NULL;
}

/* User mipmap level `level` (from 1) the file saved, RGBA unkeyed (malloc'ed); NULL when unreadable */
static uint8_t *eng_decode_user_level(const nmo_texture_state_t *state, int32_t level, int32_t *w, int32_t *h)
{
    const nmo_texture_raw_slot_t *rs = &state->user_mipmaps[level - 1];
    nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 16);
    uint8_t *pixels = NULL;
    int channels = 0;
    if (arena == NULL || (rs->compression & 0xFu) != 0 || rs->width <= 0 || rs->height <= 0 ||
        nmo_image_reconstruct_pixels(rs->red_data, rs->green_data, rs->blue_data, rs->alpha_data,
                                     rs->red_size, rs->green_size, rs->blue_size, rs->alpha_size,
                                     rs->width, rs->height, rs->bits_per_pixel, arena, &pixels,
                                     &channels) != NMO_OK) {
        nmo_arena_destroy(arena);
        return NULL;
    }
    uint8_t *rgba = eng_rgba_copy(pixels, rs->width, rs->height, channels);
    nmo_arena_destroy(arena);
    *w = rs->width;
    *h = rs->height;
    return rgba;
}

int32_t nmo_engine_texture_user_mipmap_count(const nmo_engine_t *engine, nmo_object_id_t texture)
{
    const eng_texture_t *known = eng_texture_known(engine, texture);
    if (known != NULL && known->user_levels) {
        return (int32_t)known->levels.count;
    }
    const nmo_texture_state_t *state = eng_texture_state(engine, texture);
    return state != NULL && state->has_user_mipmaps && state->user_mipmaps != NULL
               ? (int32_t)state->user_mipmap_count
               : 0;
}

bool nmo_engine_texture_user_mipmap(nmo_engine_t *engine, nmo_object_id_t texture, int32_t level,
                                    nmo_engine_image_t *out)
{
    if (out == NULL || level < 1 || level > nmo_engine_texture_user_mipmap_count(engine, texture)) {
        return false;
    }
    eng_image_t *image = NULL;
    for (size_t i = 0; i < engine->render.user_mipmaps.count && image == NULL; i++) {
        eng_image_t *cached = &engine->render.user_mipmaps.data[i];
        if (cached->texture == texture && cached->slot == level) {
            image = cached;
        }
    }
    if (image == NULL) {
        const eng_texture_t *known = eng_texture_known(engine, texture);
        int32_t w = 0, h = 0;
        uint8_t *rgba = NULL;
        if (known != NULL && known->user_levels) {
            const eng_surface_t *source = &known->levels.data[level - 1];
            size_t size = (size_t)source->width * (size_t)source->height * 4u;
            rgba = source->rgba != NULL && size > 0 ? (uint8_t *)malloc(size) : NULL;
            if (rgba != NULL) {
                memcpy(rgba, source->rgba, size);
                w = source->width;
                h = source->height;
            }
        } else {
            rgba = eng_decode_user_level(eng_texture_state(engine, texture), level, &w, &h);
        }
        if (rgba == NULL || !ENG_VEC_PUSH(engine->render.user_mipmaps, image)) {
            free(rgba);
            return false;
        }
        image->texture = texture;
        image->slot = level;
        image->width = w;
        image->height = h;
        image->rgba = rgba;
        image->version = ++engine->render.image_version;
        /* RCKTexture::Restore keys the levels as it keys the image */
        const eng_texture_t *keys = eng_texture(engine, texture);
        if (keys != NULL) {
            eng_key_image(keys, rgba, w, h);
        }
    }
    out->width = image->width;
    out->height = image->height;
    out->rgba = image->rgba;
    out->version = image->version;
    out->mipmapped = true;
    return true;
}

/* ============================================================================
 * Meshes, for a host (a physics provider's shapes)
 * ============================================================================ */

static const nmo_mesh_state_t *public_mesh(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = engine != NULL && id != 0 ? eng_find_object(engine, id) : NULL;
    return object != NULL ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                engine->types, object, CKPGUID_MESH)
                          : NULL;
}

int32_t nmo_engine_mesh_vertex_count(const nmo_engine_t *engine, nmo_object_id_t mesh)
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    return state != NULL ? (int32_t)state->vertex_count : -1;
}

bool nmo_engine_mesh_vertex(const nmo_engine_t *engine, nmo_object_id_t mesh, int32_t index,
                            float out_position[3])
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    if (state == NULL || index < 0 || (uint32_t)index >= state->vertex_count) {
        return false;
    }
    const eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh);
    const nmo_vertex_t *vertices = copy != NULL ? copy->vertices : state->vertices;
    out_position[0] = vertices[index].position.x;
    out_position[1] = vertices[index].position.y;
    out_position[2] = vertices[index].position.z;
    return true;
}

int32_t nmo_engine_mesh_face_count(const nmo_engine_t *engine, nmo_object_id_t mesh)
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    return state != NULL ? (int32_t)state->face_count : -1;
}

bool nmo_engine_mesh_face(const nmo_engine_t *engine, nmo_object_id_t mesh, int32_t index,
                          int32_t out_vertices[3])
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    if (state == NULL || index < 0 || (uint32_t)index >= state->face_count) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        out_vertices[k] = state->face_vertex_indices[3u * (uint32_t)index + (uint32_t)k];
    }
    return true;
}
