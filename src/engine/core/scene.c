/**
 * @file scene.c
 * @brief Initial conditions of the level scene's objects (CKScene's object
 *        initial values), of what the engine models.
 */

#include "../engine_internal.h"

#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Initial conditions (CKScene::SetObjectInitialValue, GetObjectInitialValue)
 * ============================================================================ */

/* CKSaveObjectState into the level scene's initial value: what the engine models of the object */
void eng_save_ic(nmo_engine_t *engine, nmo_object_id_t id)
{
    const eng_array_t *array = eng_find_array(engine, id);
    float world[16];
    bool has_matrix = nmo_engine_entity_world_matrix(engine, id, world);
    float near_plane = 0.0f, far_plane = 0.0f;
    bool has_planes = eng_camera_planes(engine, id, &near_plane, &far_plane);
    nmo_object_t *object = eng_find_object(engine, id);
    uint32_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0u;
    const eng_material_t *material = class_id == NMO_CID_MATERIAL ? eng_material(engine, id) : NULL;
    const eng_light_t *light = has_matrix ? eng_light(engine, id) : NULL;
    if (array == NULL && !has_matrix && material == NULL) {
        return;
    }
    int32_t index = eng_map_get(&engine->ics.map, id);
    eng_ic_t *ic = NULL;
    if (index >= 0) {
        ic = &engine->ics.list.data[index];
        if (ic->has_array) {
            eng_array_release(&ic->array);
        }
    } else if (ENG_VEC_PUSH(engine->ics.list, ic)) {
        if (!eng_map_put(&engine->ics.map, id, (int32_t)(engine->ics.list.count - 1u))) {
            engine->ics.list.count--;
            return;
        }
    } else {
        return;
    }
    memset(ic, 0, sizeof(*ic));
    ic->id = id;
    ic->has_array = array != NULL && eng_array_clone(&ic->array, array);
    ic->has_matrix = has_matrix;
    if (has_matrix) {
        memcpy(ic->world, world, sizeof(world));
        int32_t entity = eng_entity_index(engine, id);
        int32_t parent = entity >= 0 ? engine->entities.data[entity].parent : -1;
        ic->parent = parent >= 0 ? engine->entities.data[parent].id : 0;
    }
    ic->has_planes = has_planes;
    ic->near_plane = near_plane;
    ic->far_plane = far_plane;
    if (material != NULL) {
        ic->has_material = true;
        ic->material = material->desc;
    }
    if (light != NULL) {
        ic->has_light = true;
        memcpy(ic->light_color, light->color, sizeof(ic->light_color));
        ic->light_active = light->active;
    }
    ic->visibility = eng_object_visibility(engine, id);
}

/* CKReadObjectState of the object's initial value in the level scene, when it is in it and has one */
void eng_restore_ic(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_in_scene(engine, id) ? eng_map_get(&engine->ics.map, id) : -1;
    if (index < 0) {
        return;
    }
    const eng_ic_t *ic = &engine->ics.list.data[index];
    eng_array_t *array = ic->has_array ? eng_find_array(engine, id) : NULL;
    eng_array_t copy;
    memset(&copy, 0, sizeof(copy));
    if (array != NULL && eng_array_clone(&copy, &ic->array)) {
        eng_array_release(array);
        copy.id = array->id;
        *array = copy;
    }
    eng_set_object_visibility(engine, id, ic->visibility);
    if (ic->has_matrix) {
        /* CK3dEntity::Load: the parent (keeping where it is), then the world matrix */
        int32_t entity = eng_entity_index(engine, id);
        if (entity >= 0) {
            eng_entity_set_parent(engine, entity,
                                  ic->parent != 0 ? eng_entity_index(engine, ic->parent) : -1, true);
        }
        nmo_engine_set_entity_world_matrix(engine, id, ic->world, true);
    }
    /* CKCamera, CKMaterial and CKLight's own members */
    if (ic->has_planes) {
        eng_set_camera_planes(engine, id, ic->near_plane, ic->far_plane);
    }
    eng_material_t *material = ic->has_material ? eng_material(engine, id) : NULL;
    if (material != NULL) {
        material->desc = ic->material;
    }
    eng_light_t *light = ic->has_light ? eng_light(engine, id) : NULL;
    if (light != NULL) {
        memcpy(light->color, ic->light_color, sizeof(light->color));
        light->active = ic->light_active;
    }
}

void eng_free_ics(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->ics.list.count; i++) {
        if (engine->ics.list.data[i].has_array) {
            eng_array_release(&engine->ics.list.data[i].array);
        }
    }
    free(engine->ics.list.data);
    free(engine->ics.map.slots);
}
