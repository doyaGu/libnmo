/**
 * @file scene.c
 * @brief Initial conditions of the level scene's objects (CKScene's object
 *        initial values), of what the engine models.
 */

#include "../engine_internal.h"

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
    if (array == NULL && !has_matrix) {
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
        nmo_engine_set_entity_world_matrix(engine, id, ic->world, true);
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
