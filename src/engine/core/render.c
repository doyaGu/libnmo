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
    }
    free(render->mesh_copies.data);
    free(render->camera_planes.data);
    free(render->vertices.data);
    free(render->draws.data);
    free(render->frame_lights.data);
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
    return ENG_VEC_PUSH(engine->render.draws, draw) ? draw : NULL;
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
    out->faces = state->faces;
    out->material_count = (int32_t)state->material_group_count;
    out->prelit = copy != NULL ? copy->prelit : (state->flags & VXMESH_PRELITMODE) != 0;
    out->wrap_u = (state->flags & VXMESH_WRAPU) != 0;
    out->wrap_v = (state->flags & VXMESH_WRAPV) != 0;
    return true;
}

nmo_object_id_t nmo_engine_mesh_material(const nmo_engine_t *engine, nmo_object_id_t mesh,
                                         int32_t group)
{
    const eng_mesh_t *made = engine != NULL ? eng_render_mesh((nmo_engine_t *)engine, mesh) : NULL;
    if (made != NULL) {
        return group >= 0 && group < made->material_count ? made->materials[group] : 0;
    }
    const nmo_mesh_state_t *state = engine != NULL ? eng_mesh_state(engine, mesh) : NULL;
    if (state == NULL || group < 0 || (uint32_t)group >= state->material_group_count) {
        return 0;
    }
    return eng_object_ref(engine, mesh, &state->material_groups[group].material);
}

/* CKMesh::IsTransparent: forced, or a material group's material blends */
static bool eng_mesh_transparent(nmo_engine_t *engine, nmo_object_id_t mesh,
                                 const nmo_mesh_state_t *state)
{
    if (state->flags & VXMESH_FORCETRANSPARENCY) {
        return true;
    }
    for (uint32_t g = 0; g < state->material_group_count; g++) {
        eng_material_t *material = eng_material(
            engine, eng_object_ref(engine, mesh, &state->material_groups[g].material));
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
        *vertex = (nmo_engine_vertex_t){p[0], p[1], p[2], uvs[order[k]][0], uvs[order[k]][1], color};
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

bool eng_camera_planes(nmo_engine_t *engine, nmo_object_id_t id, float *near_plane,
                       float *far_plane)
{
    const nmo_camera_state_t *state =
        (const nmo_camera_state_t *)eng_typed_state(engine, id, CKPGUID_CAMERA);
    if (state == NULL) {
        return false;
    }
    *near_plane = state->near_plane;
    *far_plane = state->far_plane;
    for (size_t i = 0; i < engine->render.camera_planes.count; i++) {
        if (engine->render.camera_planes.data[i].id == id) {
            *near_plane = engine->render.camera_planes.data[i].near_plane;
            *far_plane = engine->render.camera_planes.data[i].far_plane;
        }
    }
    return true;
}

void eng_set_camera_planes(nmo_engine_t *engine, nmo_object_id_t id, float near_plane,
                           float far_plane)
{
    eng_camera_planes_t *planes = NULL;
    for (size_t i = 0; i < engine->render.camera_planes.count && planes == NULL; i++) {
        if (engine->render.camera_planes.data[i].id == id) {
            planes = &engine->render.camera_planes.data[i];
        }
    }
    if (planes == NULL && !ENG_VEC_PUSH(engine->render.camera_planes, planes)) {
        return;
    }
    planes->id = id;
    planes->near_plane = near_plane;
    planes->far_plane = far_plane;
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
    light->id = id;
    light->color[0] = state->light_data.diffuse.r;
    light->color[1] = state->light_data.diffuse.g;
    light->color[2] = state->light_data.diffuse.b;
    light->color[3] = state->light_data.diffuse.a;
    light->active = (state->flags & NMO_LIGHT_FLAG_ACTIVE) != 0;
    if (!eng_map_put(&engine->render.light_map, id, (int32_t)engine->render.lights.count - 1)) {
        engine->render.lights.count--;
        return NULL;
    }
    return light;
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
                                  float height)
{
    nmo_rect_t src = entity->source;
    if (!(entity->flags & CK_2DENTITY_USESRCRECT)) {
        src.left = src.top = 0.0f;
        src.right = src.bottom = 1.0f;
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
    return ENG_VEC_PUSH(engine->render.draws, *out);
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
    bool clipped = !eng_2d_update_extents(engine, entity, width, height);
    entity = &engine->render.entities_2d.data[index];
    nmo_engine_draw_t *draw = NULL;
    if (!clipped && visible && entity->material != 0 && eng_push_draw(engine, &draw)) {
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
        eng_render_args_t args = {NULL, width, height, id};
        eng_managers_render(engine, ENG_RENDER_ENTITY_2D, &args);
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

/* CKRenderedScene::PrepareCameras: a target camera and a target light turn to their target */
static void eng_prepare_cameras(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        nmo_object_t *object = entity->destroyed ? NULL : eng_find_object(engine, entity->id);
        uint32_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0;
        const nmo_ref_t *target_ref = NULL;
        if (class_id == NMO_CID_TARGETCAMERA) {
            const nmo_targetcamera_state_t *state =
                (const nmo_targetcamera_state_t *)nmo_object_get_state(object);
            target_ref = state != NULL && state->has_target ? &state->target : NULL;
        } else if (class_id == NMO_CID_TARGETLIGHT) {
            const nmo_targetlight_state_t *state =
                (const nmo_targetlight_state_t *)nmo_object_get_state(object);
            target_ref = state != NULL && state->has_target &&
                                 state->base.light_data.type != VX_LIGHTPOINT
                             ? &state->target
                             : NULL;
        }
        nmo_object_id_t target = target_ref != NULL ? eng_object_ref(engine, entity->id, target_ref) : 0;
        int32_t target_index = target != 0 ? eng_entity_index(engine, target) : -1;
        if (target_index >= 0) {
            eng_vector_t origin = {0.0f, 0.0f, 0.0f};
            eng_entity_look_at(engine, (int32_t)i, &origin, target_index, false);
        }
    }
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
        out->type = (int32_t)state->light_data.type;
        float power = state->has_light_power_chunk ? state->light_power : 1.0f;
        for (int c = 0; c < 3; c++) {
            out->color[c] = light->color[c] * power;
        }
        out->color[3] = light->color[3];
        for (int c = 0; c < 3; c++) {
            out->position[c] = entity->world.m[3][c];
            out->direction[c] = entity->world.m[2][c];
        }
        out->range = state->light_data.range;
        out->attenuation[0] = state->light_data.attenuation0;
        out->attenuation[1] = state->light_data.attenuation1;
        out->attenuation[2] = state->light_data.attenuation2;
        out->inner_cone = state->light_data.inner_spot_cone;
        out->outer_cone = state->light_data.outer_spot_cone;
        out->falloff = state->light_data.falloff;
        out->specular = (state->flags & NMO_LIGHT_FLAG_SPECULAR) != 0;
    }
}

nmo_status_t nmo_engine_render_frame(nmo_engine_t *engine, nmo_engine_frame_t *out)
{
    if (engine == NULL || out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_render_t *render = &engine->render;
    /* The 2D entities of every file loaded so far */
    if (render->entities_2d_modules != engine->modules.count) {
        render->entities_2d_modules = engine->modules.count;
        eng_load_2d_class(engine, NMO_CID_2DENTITY);
    }
    memset(out, 0, sizeof(*out));
    const eng_screen_mode_t *mode = &eng_screen_modes[render->screen_mode];
    out->width = mode->width;
    out->height = mode->height;
    float width = (float)mode->width;
    float height = (float)mode->height;

    eng_prepare_cameras(engine);
    eng_render_args_t args = {out->view, width, height, 0};
    eng_managers_render(engine, ENG_RENDER_PREPARE, &args);
    out->camera = render->camera;
    out->fov = 0.5f;
    out->zoom = 1.0f;
    out->near_plane = 1.0f;
    out->far_plane = 4000.0f;
    out->view[0] = out->view[5] = out->view[10] = out->view[15] = 1.0f;
    int32_t camera = render->camera != 0 ? eng_entity_index(engine, render->camera) : -1;
    const nmo_camera_state_t *camera_state =
        camera >= 0 ? (const nmo_camera_state_t *)eng_typed_state(engine, render->camera, CKPGUID_CAMERA)
                    : NULL;
    if (camera_state != NULL) {
        memcpy(out->view, engine->entities.data[camera].world.m, sizeof(out->view));
        out->orthographic = (camera_state->projection_type & 1u) == 0;
        out->fov = camera_state->fov;
        out->zoom = camera_state->orthographic_zoom;
        out->near_plane = camera_state->near_plane;
        out->far_plane = camera_state->far_plane;
        for (size_t i = 0; i < render->camera_planes.count; i++) {
            if (render->camera_planes.data[i].id == render->camera) {
                out->near_plane = render->camera_planes.data[i].near_plane;
                out->far_plane = render->camera_planes.data[i].far_plane;
            }
        }
    } else {
        out->camera = 0;
    }
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
    eng_render_2d_root(engine, true, width, height);
    eng_managers_render(engine, ENG_RENDER_BACKGROUND, &args);
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
            if (!eng_draw_sprite3d(engine, entity, camera >= 0 ? out->view : NULL)) {
                return NMO_ERR_NOMEM;
            }
            continue;
        }
        nmo_object_id_t mesh = eng_entity_current_mesh(engine, entity->id);
        const nmo_mesh_state_t *mesh_state = mesh != 0 ? eng_mesh_state(engine, mesh) : NULL;
        nmo_engine_draw_t *draw = NULL;
        if (mesh_state == NULL || mesh_state->face_count == 0) {
            continue;
        }
        bool transparent = eng_mesh_transparent(engine, mesh, mesh_state);
        if (!eng_push_draw(engine, &draw)) {
            return NMO_ERR_NOMEM;
        }
        entity = &engine->entities.data[i];
        draw->kind = NMO_ENGINE_DRAW_MESH;
        draw->object = entity->id;
        draw->mesh = mesh;
        memcpy(draw->world, entity->world.m, sizeof(draw->world));
        draw->transparent = transparent;
    }
    eng_managers_render(engine, ENG_RENDER_SCENE, &args);
    eng_render_2d_root(engine, false, width, height);
    eng_managers_render(engine, ENG_RENDER_OVERLAY, &args); /* the post render callbacks */
    eng_managers_render(engine, ENG_RENDER_POST, &args);    /* the post sprite render callbacks */

    out->lights = render->frame_lights.data;
    out->light_count = (int32_t)render->frame_lights.count;
    out->draws = render->draws.data;
    out->draw_count = (int32_t)render->draws.count;
    out->vertices = render->vertices.data;
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

/* The pixels of a texture's slot, as its reader, raw planes or file give them */
static uint8_t *eng_decode_slot(nmo_engine_t *engine, const nmo_texture_state_t *state,
                                const eng_texture_t *texture, int32_t slot, nmo_arena_t *arena,
                                int *w, int *h, int *ch)
{
    uint32_t index = (uint32_t)slot;
    const char *loaded = (size_t)slot < texture->files.count ? texture->files.data[slot] : NULL;
    switch (loaded != NULL ? CKTEXTURE_BITMAP_NONE : state->bitmap_kind) {
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

/* CKBitmapData's transparent color (black unless saved): those pixels are clear */
static void eng_key_image(const nmo_texture_state_t *state, uint8_t *rgba, int w, int h)
{
    if (!state->is_transparent || rgba == NULL) {
        return;
    }
    uint32_t key = state->has_transparent_color ? state->transparent_color : 0u;
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
}

bool nmo_engine_texture_image(nmo_engine_t *engine, nmo_object_id_t texture,
                              nmo_engine_image_t *out)
{
    if (engine == NULL || out == NULL) {
        return false;
    }
    eng_texture_t *slots = eng_texture(engine, texture);
    nmo_object_t *object = slots != NULL ? eng_find_object(engine, texture) : NULL;
    const nmo_texture_state_t *state =
        object != NULL ? (const nmo_texture_state_t *)nmo_object_get_state(object) : NULL;
    if (state == NULL) {
        return false;
    }
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
        nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 16);
        int w = 0, h = 0, ch = 0;
        uint8_t *pixels = arena != NULL ? eng_decode_slot(engine, state, slots, slot, arena, &w, &h, &ch)
                                        : NULL;
        image->rgba = eng_rgba_copy(pixels, w, h, ch);
        if (arena != NULL) {
            nmo_arena_destroy(arena);
        }
        if (image->rgba != NULL) {
            image->width = w;
            image->height = h;
            eng_key_image(state, image->rgba, w, h);
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
                                                   nmo_object_id_t texture)
{
    nmo_object_t *object = engine != NULL && texture != 0 ? eng_find_object(engine, texture) : NULL;
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_TEXTURE
               ? (const nmo_texture_state_t *)nmo_object_get_state(object)
               : NULL;
}

int32_t nmo_engine_texture_user_mipmap_count(const nmo_engine_t *engine, nmo_object_id_t texture)
{
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
        const nmo_texture_state_t *state = eng_texture_state(engine, texture);
        const nmo_texture_raw_slot_t *rs = &state->user_mipmaps[level - 1];
        nmo_arena_t *arena = nmo_arena_create(NULL, 1u << 16);
        uint8_t *pixels = NULL;
        int channels = 0;
        if (arena == NULL || (rs->compression & 0xFu) != 0 || rs->width <= 0 || rs->height <= 0 ||
            nmo_image_reconstruct_pixels(rs->red_data, rs->green_data, rs->blue_data,
                                         rs->alpha_data, rs->red_size, rs->green_size,
                                         rs->blue_size, rs->alpha_size, rs->width, rs->height,
                                         rs->bits_per_pixel, arena, &pixels, &channels) != NMO_OK) {
            nmo_arena_destroy(arena);
            return false;
        }
        uint8_t *rgba = eng_rgba_copy(pixels, rs->width, rs->height, channels);
        nmo_arena_destroy(arena);
        if (rgba == NULL || !ENG_VEC_PUSH(engine->render.user_mipmaps, image)) {
            free(rgba);
            return false;
        }
        image->texture = texture;
        image->slot = level;
        image->width = rs->width;
        image->height = rs->height;
        image->rgba = rgba;
        /* RCKTexture::Restore keys the levels as it keys the image */
        eng_key_image(state, rgba, rs->width, rs->height);
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
