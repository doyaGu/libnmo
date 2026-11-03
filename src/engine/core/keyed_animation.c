/* Keyed animation membership, root selection and runtime centering offsets. */
#include "../engine_internal.h"

#include "object/nmo_class_ids.h"

#include <math.h>

static const void *keyed_state(const nmo_engine_t *engine, nmo_object_id_t id, uint32_t class_id)
{
    nmo_object_t *object = eng_find_object(engine, id);
    return object != NULL && nmo_object_get_class_id(object) == class_id ? nmo_object_get_state(object) : NULL;
}

static const nmo_objectanimation_state_t *keyed_part(const nmo_engine_t *engine, nmo_object_id_t id)
{
    return keyed_state(engine, id, NMO_CID_OBJECTANIMATION);
}

static eng_keyed_animation_t *keyed_runtime(nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_keyedanimation_state_t *state = keyed_state(engine, id, NMO_CID_KEYEDANIMATION);
    if (state == NULL || (state->animation_count != 0 && state->animation_ids == NULL)) return NULL;
    int32_t index = eng_map_get(&engine->animations.keyed_map, id);
    eng_keyed_animation_t *runtime = NULL;
    if (index < 0) {
        index = (int32_t)engine->animations.keyed.count;
        if (!ENG_VEC_PUSH(engine->animations.keyed, runtime)) return NULL;
        runtime->id = id;
        runtime->root_entity = eng_object_ref(engine, id, &state->base.root_entity);
        if (!eng_map_put(&engine->animations.keyed_map, id, index)) return NULL;
    } else runtime = &engine->animations.keyed.data[index];
    if (runtime->ready) return runtime;
    /* Native Load transfers each child's pending nonzero vector, in list order,
     * once. The transfer and parent links belong to this engine, not the file. */
    for (uint32_t i = 0; i < state->animation_count; i++) {
        nmo_object_id_t part = eng_object_ref(engine, id, &state->animation_ids[i]);
        const nmo_objectanimation_state_t *child = keyed_part(engine, part);
        if (child == NULL) continue;
        if (!eng_map_put(&engine->animations.parents, part, index)) return NULL;
        if (eng_map_get(&engine->animations.consumed, part) >= 0) continue;
        if (!eng_map_put(&engine->animations.consumed, part, 0)) return NULL;
        nmo_vector_t vector = child->root_pos;
        if (child->has_root_pos && (vector.x != 0 || vector.y != 0 || vector.z != 0) &&
            isfinite(vector.x) && isfinite(vector.y) && isfinite(vector.z)) runtime->offset = vector;
    }
    runtime->ready = true;
    return runtime;
}

nmo_status_t eng_load_animations(nmo_engine_t *engine, const eng_range_t *range)
{
    const eng_module_t *module = &engine->modules.data[range->module];
    size_t count = 0;
    nmo_object_t **objects = module->repo != NULL ? nmo_object_repository_get_all(module->repo, &count) : NULL;
    for (size_t i = 0; objects != NULL && i < count; i++) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id < range->first || id > range->last || nmo_object_get_class_id(objects[i]) != NMO_CID_KEYEDANIMATION) continue;
        if (keyed_runtime(engine, eng_module_id(module, id)) == NULL) return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

void eng_free_animations(nmo_engine_t *engine)
{
    free(engine->animations.keyed.data);
    free(engine->animations.keyed_map.slots);
    free(engine->animations.parents.slots);
    free(engine->animations.consumed.slots);
}

static nmo_object_id_t keyed_entity(const nmo_engine_t *engine, nmo_object_id_t part)
{
    const nmo_objectanimation_state_t *state = keyed_part(engine, part);
    return state != NULL ? eng_object_ref(engine, part, &state->entity) : 0;
}

/* UpdateRootEntity walks from the first bound channel to the character, or
 * to a standalone hierarchy's top when no character is attached. */
static nmo_object_id_t keyed_hierarchy_root(nmo_engine_t *engine, nmo_object_id_t id,
                                            const nmo_keyedanimation_state_t *state)
{
    int32_t entity = -1;
    for (uint32_t i = 0; entity < 0 && i < state->animation_count; i++) {
        nmo_object_id_t part = eng_object_ref(engine, id, &state->animation_ids[i]);
        entity = eng_entity_index(engine, keyed_entity(engine, part));
    }
    nmo_object_id_t character = eng_object_ref(engine, id, &state->base.character);
    for (size_t visited = 0; entity >= 0 && visited < engine->entities.count; visited++) {
        const eng_entity_t *current = &engine->entities.data[entity];
        int32_t parent = current->parent;
        nmo_object_id_t parent_id = parent >= 0 ? engine->entities.data[parent].id : 0;
        if (parent_id == character || (parent_id != 0 && nmo_engine_object_class(engine, parent_id) == NMO_CID_CHARACTER)) return current->id;
        entity = parent;
    }
    return 0;
}

static bool keyed_contains(const nmo_engine_t *engine, const nmo_keyedanimation_state_t *state,
                            nmo_object_id_t group, nmo_object_id_t part)
{
    for (uint32_t i = 0; i < state->animation_count; i++) {
        if (eng_object_ref(engine, group, &state->animation_ids[i]) == part) return true;
    }
    return false;
}

static nmo_object_id_t keyed_root(nmo_engine_t *engine, eng_keyed_animation_t *runtime,
                                   const nmo_keyedanimation_state_t *state)
{
    if (eng_entity_index(engine, runtime->root_entity) < 0) {
        runtime->root_entity = keyed_hierarchy_root(engine, runtime->id, state);
        runtime->root_animation = 0;
    }
    nmo_object_id_t root = runtime->root_entity;
    if (root == 0) return 0;
    if (runtime->root_animation != 0 &&
        keyed_contains(engine, state, runtime->id, runtime->root_animation) &&
        keyed_entity(engine, runtime->root_animation) == root) return runtime->root_animation;
    runtime->root_animation = 0;
    for (uint32_t i = 0; i < state->animation_count; i++) {
        nmo_object_id_t part = eng_object_ref(engine, runtime->id, &state->animation_ids[i]);
        if (keyed_entity(engine, part) == root) {
            runtime->root_animation = part;
            break;
        }
    }
    return runtime->root_animation;
}

bool eng_animation_root_offset(const nmo_engine_t *engine, nmo_object_id_t animation, nmo_vector_t *out)
{
    int32_t index = eng_map_get(&engine->animations.parents, animation);
    if (index < 0) return false;
    const eng_keyed_animation_t *runtime = &engine->animations.keyed.data[index];
    if (runtime->root_animation != animation || eng_find_object(engine, runtime->id) == NULL) return false;
    *out = runtime->offset;
    return true;
}

bool eng_keyed_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    eng_keyed_animation_t *runtime = keyed_runtime(engine, animation);
    const nmo_keyedanimation_state_t *state = keyed_state(engine, animation, NMO_CID_KEYEDANIMATION);
    if (runtime == NULL || state == NULL) return false;
    nmo_object_id_t root = keyed_root(engine, runtime, state);
    if (root != 0 && !eng_map_put(&engine->animations.parents, root, (int32_t)(runtime - engine->animations.keyed.data))) return false;
    step = step > 1 ? 1 : step < 0 ? 0 : step;
    for (uint32_t i = 0; i < state->animation_count; i++) {
        eng_object_animation_set_step(engine, eng_object_ref(engine, animation, &state->animation_ids[i]), step);
    }
    return true;
}
