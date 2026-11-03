/* Keyed animation membership, root selection and runtime centering offsets. */
#include "../engine_internal.h"

#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_character_schemas.h"

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
    for (size_t i = 0; i < engine->animations.keyed.count; i++) {
        free(engine->animations.keyed.data[i].members.data);
        free(engine->animations.keyed.data[i].order.data);
    }
    free(engine->animations.keyed.data);
    free(engine->animations.keyed_map.slots);
    free(engine->animations.parents.slots);
    free(engine->animations.consumed.slots);
    free(engine->animations.clocks.data);
    free(engine->animations.clock_map.slots);
}

void eng_copy_animation(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                          const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count)
{
    int32_t clock = eng_map_get(&engine->animations.clock_map, source);
    if (clock >= 0) eng_animation_set_current_step(engine, target, engine->animations.clocks.data[clock].step);
    int32_t from = eng_map_get(&engine->animations.keyed_map, source);
    int32_t to = eng_map_get(&engine->animations.keyed_map, target);
    if (from < 0 || to < 0) return;
    eng_keyed_animation_t snapshot = engine->animations.keyed.data[from];
    eng_keyed_animation_t *copy = &engine->animations.keyed.data[to];
    copy->offset = snapshot.offset;
    copy->root_entity = snapshot.root_entity;
    for (size_t i = 0; i < count; i++) {
        if (originals[i] == copy->root_entity) { copy->root_entity = copies[i]; break; }
    }
    /* RCKKeyedAnimation::Copy invalidates the selected root channel. */
    copy->root_animation = 0;
    copy->sorted = false;
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

nmo_object_id_t eng_keyed_animation_root(nmo_engine_t *engine, nmo_object_id_t animation)
{
    eng_keyed_animation_t *runtime = keyed_runtime(engine, animation);
    const nmo_keyedanimation_state_t *state = keyed_state(engine, animation, NMO_CID_KEYEDANIMATION);
    if (runtime == NULL || state == NULL) return 0;
    nmo_object_id_t root = keyed_root(engine, runtime, state);
    if (root != 0 && !eng_map_put(&engine->animations.parents, root, (int32_t)(runtime-engine->animations.keyed.data))) return 0;
    return root;
}

bool eng_keyed_animation_center(nmo_engine_t *engine, nmo_object_id_t animation, float frame)
{
    if (!isfinite(frame)) return false;
    eng_keyed_animation_t *runtime = keyed_runtime(engine, animation);
    const nmo_keyedanimation_state_t *state = keyed_state(engine, animation, NMO_CID_KEYEDANIMATION);
    if (runtime == NULL || state == NULL) return false;
    nmo_object_id_t root = keyed_root(engine, runtime, state);
    if (root == 0) return false;
    nmo_vector_t position = {0};
    eng_animation_position(engine, root, frame, &position);
    if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z)) return false;
    if (!eng_map_put(&engine->animations.parents, root, (int32_t)(runtime - engine->animations.keyed.data))) return false;
    runtime->offset = (nmo_vector_t){-position.x,-position.y,-position.z};
    return true;
}

nmo_object_id_t eng_keyed_animation_part(nmo_engine_t *engine, nmo_object_id_t animation, nmo_object_id_t entity)
{
    eng_keyed_animation_t *runtime = keyed_runtime(engine,animation);
    const nmo_keyedanimation_state_t *state = keyed_state(engine,animation,NMO_CID_KEYEDANIMATION);
    if (runtime == NULL || state == NULL || entity == 0) return 0;
    if (entity == runtime->root_entity) return eng_keyed_animation_root(engine,animation);
    size_t count = runtime->sorted ? runtime->order.count : state->animation_count;
    for (size_t i = 0; i < count; i++) {
        nmo_object_id_t id = runtime->sorted ? runtime->order.data[i].id : eng_object_ref(engine,animation,&state->animation_ids[i]);
        if (eng_map_get(&engine->destroyed,id) < 0 && keyed_entity(engine,id) == entity) return id;
    }
    return 0;
}

float eng_character_animation_lod(const nmo_engine_t *engine, nmo_object_id_t character)
{
    int32_t index = eng_entity_index(engine, character);
    return index >= 0 && nmo_engine_object_class(engine, character) == NMO_CID_CHARACTER
        ? engine->entities.data[index].animation_lod : 0;
}

bool eng_character_set_animation_lod(nmo_engine_t *engine, nmo_object_id_t character, float lod)
{
    int32_t index = eng_entity_index(engine, character);
    if (index < 0 || nmo_engine_object_class(engine, character) != NMO_CID_CHARACTER || !isfinite(lod)) return false;
    engine->entities.data[index].animation_lod = lod;
    return true;
}

void eng_animations_end_frame(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->entities.count; i++) {
        eng_entity_t *entity = &engine->entities.data[i];
        if (!entity->destroyed && nmo_engine_object_class(engine, entity->id) == NMO_CID_CHARACTER) {
            entity->moveable_flags &= ~VX_MOVEABLE_CHARACTERRENDERED;
        }
    }
}

/* Keep the sorted list private. Membership, target rebinding and root/floor
 * changes invalidate it; loading never trusts the file's SUBANIMSSORTED bit. */
static bool keyed_order(nmo_engine_t *engine, eng_keyed_animation_t *runtime,
                         const nmo_keyedanimation_state_t *state, nmo_object_id_t character, nmo_object_id_t floor)
{
    bool changed = !runtime->sorted || runtime->members.count != state->animation_count ||
        runtime->sort_character != character || runtime->sort_root != runtime->root_entity || runtime->sort_floor != floor;
    for (uint32_t i = 0; !changed && i < state->animation_count; i++) {
        nmo_object_id_t id = eng_object_ref(engine, runtime->id, &state->animation_ids[i]);
        if (runtime->members.data[i].id != id || runtime->members.data[i].entity != keyed_entity(engine, id)) changed = true;
    }
    if (!changed) return true;
    if (!eng_vec_reserve((void **)&runtime->members.data, &runtime->members.capacity, state->animation_count, sizeof(*runtime->members.data)) ||
        !eng_vec_reserve((void **)&runtime->order.data, &runtime->order.capacity, state->animation_count, sizeof(*runtime->order.data))) return false;
    runtime->members.count = state->animation_count;
    runtime->order.count = 0;
    for (uint32_t i = 0; i < state->animation_count; i++) {
        nmo_object_id_t id = eng_object_ref(engine, runtime->id, &state->animation_ids[i]);
        nmo_object_id_t entity = keyed_entity(engine, id);
        runtime->members.data[i] = (eng_animation_member_t){id,entity};
        /* CheckPreDeletion removes dead object references from the runtime
         * list. Keep explicit empty snapshot slots, without counting a dead
         * channel toward the LOD budget or rewriting the saved membership. */
        if (id == 0 && nmo_ref_runtime_id(&state->animation_ids[i]) != 0) continue;
        float contribution = -1e30f;
        if (character != 0 && keyed_part(engine, id) != NULL) {
            contribution = -1e10f;
            if (entity != 0 && eng_find_object(engine, entity) != NULL && eng_entity_index(engine, entity) >= 0) {
                contribution = entity == runtime->root_entity || entity == floor ? 1e30f : eng_animation_contribution(engine, id);
            }
        }
        runtime->order.data[runtime->order.count++] = (eng_animation_channel_t){id,contribution};
    }
    if (character != 0) {
        for (size_t i = 1; i < runtime->order.count; i++) {
            eng_animation_channel_t value = runtime->order.data[i];
            size_t j = i;
            while (j > 0 && runtime->order.data[j-1].contribution < value.contribution) {
                runtime->order.data[j] = runtime->order.data[j-1];
                j--;
            }
            runtime->order.data[j] = value;
        }
    }
    runtime->sort_character = character;
    runtime->sort_root = runtime->root_entity;
    runtime->sort_floor = floor;
    runtime->sorted = true;
    return true;
}

void eng_keyed_animation_refresh(nmo_engine_t *engine, nmo_object_id_t animation)
{
    int32_t index = eng_map_get(&engine->animations.keyed_map,animation);
    const nmo_keyedanimation_state_t *state = keyed_state(engine,animation,NMO_CID_KEYEDANIMATION);
    if (index < 0 || state == NULL) return;
    eng_keyed_animation_t *runtime = &engine->animations.keyed.data[index];
    runtime->root_entity = eng_object_ref(engine,animation,&state->base.root_entity);
    runtime->root_animation = 0;
    runtime->sorted = runtime->ready = false;
}

bool eng_keyed_animation_channels(nmo_engine_t *engine, nmo_object_id_t animation,
    const eng_animation_channel_t **out, size_t *count)
{
    eng_keyed_animation_t *runtime = keyed_runtime(engine, animation);
    const nmo_keyedanimation_state_t *state = keyed_state(engine, animation, NMO_CID_KEYEDANIMATION);
    if (runtime == NULL || state == NULL) return false;
    nmo_object_id_t root = keyed_root(engine, runtime, state);
    if (root != 0 && !eng_map_put(&engine->animations.parents, root, (int32_t)(runtime - engine->animations.keyed.data))) return false;
    nmo_object_id_t character = eng_object_ref(engine, animation, &state->base.character);
    const nmo_character_state_t *actor = keyed_state(engine, character, NMO_CID_CHARACTER);
    if (actor == NULL) character = 0;
    nmo_object_id_t floor = actor != NULL ? eng_object_ref(engine, character, &actor->floor_ref) : 0;
    if (eng_entity_index(engine, floor) < 0) floor = 0;
    if (!keyed_order(engine, runtime, state, character, floor)) return false;
    *out = runtime->order.data;
    *count = runtime->order.count;
    return true;
}

bool eng_keyed_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    const eng_animation_channel_t *channels;
    size_t count;
    if (!eng_keyed_animation_channels(engine,animation,&channels,&count)) return false;
    const nmo_keyedanimation_state_t *state = keyed_state(engine,animation,NMO_CID_KEYEDANIMATION);
    nmo_object_id_t character = eng_object_ref(engine,animation,&state->base.character);
    const nmo_character_state_t *actor = keyed_state(engine,character,NMO_CID_CHARACTER);
    nmo_object_id_t floor = actor != NULL ? eng_object_ref(engine,character,&actor->floor_ref) : 0;
    if (actor == NULL) character = 0;
    if (eng_entity_index(engine,floor) < 0) floor = 0;
    int32_t character_index = eng_entity_index(engine, character);
    if (character_index >= 0 && count != 0) {
        size_t required = floor != 0 ? 2 : 1;
        float lod = (engine->entities.data[character_index].moveable_flags & VX_MOVEABLE_CHARACTERRENDERED)
            ? eng_character_animation_lod(engine, character) : 0;
        if (count > required && lod < 1) {
            /* Preserve the native wide product before truncating. Negative
             * finite LODs keep the required prefix instead of underflowing. */
            double additional = (double)(count-required) * fmaxf(lod, 0);
            count = (size_t)additional + required;
        }
    }
    step = step > 1 ? 1 : step < 0 ? 0 : step;
    for (size_t i = 0; i < count; i++) {
        eng_object_animation_apply_step(engine, channels[i].id, step, animation);
    }
    return eng_animation_set_current_step(engine, animation, step);
}
