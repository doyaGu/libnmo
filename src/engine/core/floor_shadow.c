/**
 * @file floor_shadow.c
 * @brief A soft shadow texture projected on the floors under a 3D entity.
 *
 * Simple Shadow (Visuals.dll) and TT Simple Shadow (TT_Gravity_RT.dll) do
 * the same thing, A_GetFloors and the channel UVs: the floors (objects with
 * the Floor attribute, shown) whose world box is under the entity's get a
 * material channel multiplying them (ZERO, SRCCOLOR), with UVs from the
 * entity's position over each floor.
 */

#include "../engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

/* The floor's mesh copy, where the shadow's channel goes */
static eng_mesh_copy_t *shadow_floor_mesh(nmo_engine_t *engine, nmo_object_id_t floor)
{
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, eng_find_object(engine, floor), CKPGUID_3DENTITY);
    return state != NULL ? eng_mesh_copy(engine, eng_object_ref(engine, floor, &state->current_mesh))
                         : NULL;
}

static void shadow_off_floor(nmo_engine_t *engine, nmo_object_id_t material, nmo_object_id_t floor)
{
    eng_mesh_copy_t *mesh = shadow_floor_mesh(engine, floor);
    if (mesh != NULL) {
        eng_mesh_remove_channel(mesh, eng_mesh_channel(mesh, material));
        mesh->version = ++engine->render.mesh_version;
    }
}

nmo_object_id_t eng_floor_shadow_material(nmo_engine_t *engine, const char *name)
{
    nmo_object_id_t id = eng_create_object(engine, NMO_CID_MATERIAL, name);
    eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
    if (material == NULL && id != 0) {
        material = eng_material_new(engine, id);
    }
    if (material == NULL) {
        return 0;
    }
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f}, black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(material->desc.emissive, white, sizeof(white));
    memcpy(material->desc.diffuse, white, sizeof(white));
    memcpy(material->desc.specular, black, sizeof(black));
    memcpy(material->desc.ambient, black, sizeof(black));
    material->desc.address_mode = VXTEXTURE_ADDRESSCLAMP;
    material->desc.texture_blend = VXTEXTUREBLEND_COPY;
    return id;
}

void eng_floor_shadow_remove(nmo_engine_t *engine, eng_floor_shadow_t *shadow)
{
    for (int32_t f = 0; f < shadow->floor_count; f++) {
        shadow_off_floor(engine, shadow->material, shadow->floors[f]);
    }
}

void eng_floor_shadow_place(nmo_engine_t *engine, eng_floor_shadow_t *shadow, nmo_object_id_t target,
                            float zoom, float max_height)
{
    int32_t entity = eng_entity_index(engine, target);
    if (entity < 0) {
        return;
    }
    eng_vector_t min, max, wmin, wmax;
    eng_entity_local_box(engine, entity, &min, &max);
    eng_vector_t axis = {engine->entities.data[entity].world.m[0][0],
                         engine->entities.data[entity].world.m[0][1],
                         engine->entities.data[entity].world.m[0][2]};
    float width = (max.x - min.x) * eng_vector_magnitude(&axis);
    if (zoom == 0.0f || width == 0.0f) {
        return;
    }
    float inv_zoom = 1.0f / (zoom * width);
    eng_entity_world_box(engine, entity, &wmin, &wmax);
    float get_inv_zoom = 1.0f / (zoom * (wmax.x - wmin.x));
    nmo_object_id_t old[ENG_SHADOW_FLOORS];
    int32_t old_count = shadow->floor_count;
    memcpy(old, shadow->floors, sizeof(old));
    eng_vector_t positions[ENG_SHADOW_FLOORS], scales[ENG_SHADOW_FLOORS];
    int32_t count = 0;
    int32_t type = eng_floor_attribute(engine);
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        int32_t floor = attribute->type == type ? eng_entity_index(engine, attribute->object) : -1;
        if (floor < 0 || engine->entities.data[floor].destroyed ||
            !nmo_engine_object_is_visible(engine, attribute->object)) {
            continue;
        }
        eng_vector_t fmin, fmax;
        eng_entity_world_box(engine, floor, &fmin, &fmax);
        if (fmin.x > wmax.x || fmin.z > wmax.z || fmax.x < wmin.x || fmax.z < wmin.z ||
            fmin.y > wmax.y || wmin.y - fmax.y > max_height) {
            continue;
        }
        eng_vector_t pos = eng_entity_position(engine, entity, floor);
        eng_vector_t lmin, lmax;
        eng_entity_local_box(engine, floor, &lmin, &lmax);
        eng_vector_t scale = eng_entity_scale(engine, floor, true);
        float tx = get_inv_zoom * scale.x, tz = get_inv_zoom * scale.z;
        if ((lmax.x - pos.x) * tx >= -0.5f && (lmin.x - pos.x) * tx <= 0.5f &&
            (lmax.z - pos.z) * tz >= -0.5f && (lmin.z - pos.z) * tz <= 0.5f &&
            count < ENG_SHADOW_FLOORS) {
            positions[count] = pos;
            scales[count] = scale;
            shadow->floors[count++] = attribute->object;
        }
    }
    shadow->floor_count = count;
    /* Off the floors it left */
    for (int32_t a = 0; a < old_count; a++) {
        bool kept = false;
        for (int32_t f = 0; f < count && !kept; f++) {
            kept = old[a] == shadow->floors[f];
        }
        if (!kept) {
            shadow_off_floor(engine, shadow->material, old[a]);
        }
    }
    /* On the floors under it, its UVs from the object's place over each */
    for (int32_t f = 0; f < count; f++) {
        eng_mesh_copy_t *mesh = shadow_floor_mesh(engine, shadow->floors[f]);
        nmo_engine_mesh_t desc;
        if (mesh == NULL || !nmo_engine_mesh(engine, mesh->mesh, &desc)) {
            continue;
        }
        int32_t channel = eng_mesh_channel(mesh, shadow->material);
        if (channel < 0) {
            channel = eng_mesh_add_channel(mesh, shadow->material);
            if (channel < 0) {
                continue;
            }
            mesh->channels.data[channel].source_blend = VXBLEND_ZERO;
            mesh->channels.data[channel].dest_blend = VXBLEND_SRCCOLOR;
        }
        nmo_engine_channel_t *ch = &mesh->channels.data[channel];
        float *uvs = (float *)ch->uvs;
        if (uvs == NULL) {
            uvs = (float *)malloc((size_t)desc.vertex_count * 2u * sizeof(float));
            ch->uvs = uvs;
        }
        float tx = inv_zoom * scales[f].x, tz = inv_zoom * scales[f].z;
        float u0 = positions[f].x * tx + 0.5f, v0 = positions[f].z * tz + 0.5f;
        for (int32_t v = 0; uvs != NULL && v < desc.vertex_count; v++) {
            uvs[v * 2] = u0 - mesh->vertices[v].position.x * tx;
            uvs[v * 2 + 1] = v0 - mesh->vertices[v].position.z * tz;
        }
        mesh->version = ++engine->render.mesh_version;
    }
}
