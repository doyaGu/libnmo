/* CKCharacter playback, generated primary transitions and secondary gates. */
#include "../engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_object.h"

#include <math.h>

static const nmo_character_state_t *character_state(const nmo_engine_t *engine, nmo_object_id_t id)
{
    if (eng_map_get(&engine->destroyed, id) >= 0) return NULL;
    nmo_object_t *object = eng_find_object(engine, id);
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_CHARACTER ? nmo_object_get_state(object) : NULL;
}

static eng_character_t *character_runtime(nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_character_state_t *state = character_state(engine, id);
    if (state == NULL) return NULL;
    int32_t index = eng_map_get(&engine->characters.map, id);
    if (index >= 0) return &engine->characters.entries.data[index];
    index = (int32_t)engine->characters.entries.count;
    eng_character_t *runtime = NULL;
    if (!ENG_VEC_PUSH(engine->characters.entries, runtime)) return NULL;
    runtime->id = id;
    runtime->root = eng_object_ref(engine, id, &state->root_body_part);
    int32_t entity = eng_entity_index(engine, id);
    if (runtime->root == 0 && entity >= 0) {
        int32_t child = engine->entities.data[entity].first_child;
        if (child >= 0) runtime->root = engine->entities.data[child].id;
    }
    runtime->active = eng_object_ref(engine, id, &state->active_animation);
    runtime->next = eng_object_ref(engine, id, &state->anim_dest);
    runtime->floor = eng_object_ref(engine, id, &state->floor_ref);
    runtime->automatic = (state->base.entity_flags & CK_3DENTITY_CHARACTERDOPROCESS) != 0;
    for (size_t i = 0; i < state->animations.count; i++) {
        nmo_object_id_t *slot = NULL;
        if (!ENG_VEC_PUSH(runtime->animations,slot)) return NULL;
        *slot = eng_object_ref(engine,id,&((const nmo_ref_t *)state->animations.data)[i]);
    }
    if (!eng_map_put(&engine->characters.map, id, index)) return NULL;
    return runtime;
}

nmo_object_id_t eng_character_root(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine,character);
    return runtime != NULL && eng_entity_index(engine,runtime->root) >= 0 ? runtime->root : 0;
}

size_t eng_character_animation_count(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime == NULL) return 0;
    size_t kept = 0;
    for (size_t i = 0; i < runtime->animations.count; i++) {
        nmo_object_id_t id = runtime->animations.data[i];
        if (id == 0 || eng_animation_state(engine,id) != NULL) runtime->animations.data[kept++] = id;
    }
    return runtime->animations.count = kept;
}

nmo_object_id_t eng_character_animation(nmo_engine_t *engine, nmo_object_id_t character, size_t index)
{
    if (index >= eng_character_animation_count(engine,character)) return 0;
    return character_runtime(engine,character)->animations.data[index];
}

size_t eng_character_bodypart_count(const nmo_engine_t *engine, nmo_object_id_t character)
{
    const nmo_character_state_t *state = character_state(engine,character);
    return state != NULL ? state->body_parts.count : 0;
}

nmo_object_id_t eng_character_bodypart(const nmo_engine_t *engine, nmo_object_id_t character, size_t index)
{
    const nmo_character_state_t *state = character_state(engine,character);
    if (state == NULL || index >= state->body_parts.count) return 0;
    nmo_object_id_t id = eng_object_ref(engine,character,&((const nmo_character_part_t *)state->body_parts.data)[index].ref);
    return eng_entity_index(engine,id) >= 0 && nmo_type_registry_is_class_derived_from(engine->types,
        nmo_engine_object_class(engine,id),NMO_CID_BODYPART) ? id : 0;
}

nmo_status_t eng_character_add_animation(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime == NULL || eng_animation_state(engine,animation) == NULL) return NMO_ERR_INVALID_ARGUMENT;
    bool keyed = nmo_engine_object_class(engine,animation) == NMO_CID_KEYEDANIMATION;
    nmo_object_id_t root = 0;
    if (keyed && !eng_keyed_animation_update_root(engine,animation,runtime->root,&root)) return NMO_ERR_NOMEM;
    bool present = false;
    for (size_t i = 0; i < runtime->animations.count; i++) if (runtime->animations.data[i] == animation) present = true;
    if (!present) {
        if (!eng_vec_reserve((void **)&runtime->animations.data,&runtime->animations.capacity,
            runtime->animations.count+1,sizeof(*runtime->animations.data))) return NMO_ERR_NOMEM;
        nmo_object_id_t old = eng_animation_character(engine,animation);
        if (!eng_animation_set_character(engine,animation,character)) return NMO_ERR_NOMEM;
        if (old != 0 && old != character) {
            eng_character_t *previous = character_runtime(engine,old);
            size_t kept = 0;
            if (previous != NULL) {
                for (size_t i = 0; i < previous->animations.count; i++)
                    if (previous->animations.data[i] != animation) previous->animations.data[kept++] = previous->animations.data[i];
                previous->animations.count = kept;
            }
        }
        /* Initializing the previous owner can move the character table. */
        runtime = character_runtime(engine,character);
        runtime->animations.data[runtime->animations.count++] = animation;
    }
    if (keyed) {
        if (eng_entity_index(engine,runtime->root) < 0) runtime->root = root;
        float frame = 0;
        if (eng_animation_frame(engine,animation,&frame)) eng_keyed_animation_center(engine,animation,frame);
    }
    return NMO_OK;
}

bool eng_character_automatic(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    return runtime != NULL && runtime->automatic;
}

void eng_character_stop_automatic(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime != NULL) runtime->automatic = false;
}

bool eng_character_set_automatic(nmo_engine_t *engine, nmo_object_id_t character, bool automatic)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime == NULL) return false;
    int32_t index = eng_map_get(&engine->object_map,character);
    if (automatic && index < 0) {
        eng_object_t *entry = NULL;
        index = (int32_t)engine->objects.count;
        if (!ENG_VEC_PUSH(engine->objects,entry) || !eng_map_put(&engine->object_map,character,index)) return false;
        entry->id = character;
        entry->priority = ((const nmo_beobject_state_t *)character_state(engine,character))->priority;
        entry->order = (uint32_t)index;
        entry->active = engine->started && eng_in_scene(engine,character);
    }
    runtime->automatic = automatic;
    if (automatic && engine->started && index >= 0 && engine->objects.data[index].active &&
        !engine->objects.data[index].listed && !engine->objects.data[index].next_frame_asked)
        eng_object_next_frame(engine,index,1);
    return true;
}

nmo_object_id_t eng_character_floor_reference(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine,character);
    return runtime != NULL && eng_entity_index(engine,runtime->floor) >= 0 ? runtime->floor : 0;
}

bool eng_character_set_floor_reference(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t floor)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime == NULL || (floor != 0 && eng_entity_index(engine,floor) < 0)) return false;
    runtime->floor = floor;
    return true;
}

nmo_status_t eng_load_characters(nmo_engine_t *engine, const eng_range_t *range)
{
    const eng_module_t *module = &engine->modules.data[range->module];
    size_t count = module->repo != NULL ? nmo_object_repository_get_count(module->repo) : 0;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(module->repo, i);
        if (object == NULL) continue;
        nmo_object_id_t id = nmo_object_get_id(object);
        if (id < range->first || id > range->last || nmo_object_get_class_id(object) != NMO_CID_CHARACTER) continue;
        nmo_object_id_t runtime_id = eng_module_id(module, id);
        eng_character_t *runtime = character_runtime(engine, runtime_id);
        if (runtime == NULL) return NMO_ERR_NOMEM;
        /* Automatic characters remain behavior-manager objects even without
         * scripts. Existing script owners retain their place and priority. */
        if (runtime->automatic && eng_map_get(&engine->object_map, runtime_id) < 0) {
            const nmo_character_state_t *state = nmo_object_get_state(object);
            eng_object_t *entry = NULL;
            int32_t index = (int32_t)engine->objects.count;
            if (!ENG_VEC_PUSH(engine->objects, entry) || !eng_map_put(&engine->object_map, runtime_id, index)) return NMO_ERR_NOMEM;
            entry->id = runtime_id;
            entry->priority = ((const nmo_beobject_state_t *)state)->priority;
            entry->order = (uint32_t)index;
        }
    }
    return NMO_OK;
}

void eng_free_characters(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->characters.entries.count; i++) {
        eng_character_t *runtime = &engine->characters.entries.data[i];
        for (size_t j = 0; j < runtime->secondary.count; j++) {
            free(runtime->secondary.data[j].gate.bodies.data);
            free(runtime->secondary.data[j].warp_gate.bodies.data);
        }
        free(runtime->secondary.data);
        free(runtime->secondary_warpers.data);
        free(runtime->animations.data);
    }
    free(engine->characters.entries.data);
    free(engine->characters.map.slots);
}

static nmo_object_id_t character_copy_ref(nmo_object_id_t id, const nmo_object_id_t *originals,
                                          const nmo_object_id_t *copies, size_t count)
{
    for (size_t i = 0; i < count; i++) if (originals[i] == id) return copies[i];
    return id;
}

void eng_copy_character(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                          const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count, bool copy_next)
{
    int32_t from = eng_map_get(&engine->characters.map, source), to = eng_map_get(&engine->characters.map, target);
    if (from < 0 || to < 0) return;
    eng_character_t snapshot = engine->characters.entries.data[from];
    eng_character_t *copy = &engine->characters.entries.data[to];
    copy->root = character_copy_ref(snapshot.root, originals, copies, count);
    copy->floor = character_copy_ref(snapshot.floor, originals, copies, count);
    copy->active = character_copy_ref(snapshot.active, originals, copies, count);
    copy->next = copy_next ? character_copy_ref(snapshot.next, originals, copies, count) : 0;
    copy->frame_dest = snapshot.frame_dest;
    copy->automatic = snapshot.automatic;
    if (eng_vec_reserve((void **)&copy->animations.data,&copy->animations.capacity,
        snapshot.animations.count,sizeof(*copy->animations.data))) {
        copy->animations.count = snapshot.animations.count;
        for (size_t i = 0; i < snapshot.animations.count; i++)
            copy->animations.data[i] = character_copy_ref(snapshot.animations.data[i],originals,copies,count);
    }
}

nmo_object_id_t eng_character_active_animation(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    return runtime != NULL && eng_animation_playback_state(engine, runtime->active) != NULL ? runtime->active : 0;
}

nmo_object_id_t eng_character_next_animation(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    return runtime != NULL && eng_animation_playback_state(engine, runtime->next) != NULL ? runtime->next : 0;
}

static bool character_keyed(const nmo_engine_t *engine, nmo_object_id_t animation)
{
    return nmo_engine_object_class(engine, animation) == NMO_CID_KEYEDANIMATION;
}

static void character_root_position(nmo_engine_t *engine, const eng_character_t *runtime)
{
    int32_t root = eng_entity_index(engine, runtime->root), actor = eng_entity_index(engine, runtime->id);
    if (root < 0 || actor < 0) return;
    const eng_matrix_t *world = &engine->entities.data[root].world;
    eng_vector_t position = {world->m[3][0],world->m[3][1],world->m[3][2]};
    eng_entity_set_position(engine, actor, &position, -1, true);
}

static void character_align(nmo_engine_t *engine, eng_character_t *runtime)
{
    if (runtime->active == 0) { character_root_position(engine, runtime); return; }
    if (!character_keyed(engine, runtime->active)) return;
    float frame = 0;
    eng_animation_frame(engine, runtime->active, &frame);
    int32_t actor = eng_entity_index(engine, runtime->id), root = eng_entity_index(engine, runtime->root);
    const nmo_animation_state_t *state = eng_animation_playback_state(engine, runtime->active);
    if (actor >= 0 && root >= 0 && state != NULL && (state->flags & CKANIMATION_ALIGNORIENTATION)) {
        eng_matrix_t turn, world;
        eng_animation_yaw(engine, eng_keyed_animation_root(engine, runtime->active), frame, &turn);
        eng_matrix_multiply(&world, &turn, &engine->entities.data[actor].world);
        for (int c = 0; c < 3; c++) world.m[3][c] = engine->entities.data[root].world.m[3][c];
        eng_entity_set_world_matrix(engine, actor, &world, true);
    } else character_root_position(engine, runtime);
    eng_keyed_animation_center(engine, runtime->active, frame);
}

nmo_status_t eng_character_set_active_animation(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation)
{
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime == NULL || (animation != 0 && eng_animation_playback_state(engine, animation) == NULL)) return NMO_ERR_INVALID_ARGUMENT;
    runtime->active = animation;
    return NMO_OK;
}

nmo_object_id_t eng_character_warper(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime == NULL) return 0;
    if (runtime->warper != 0) return eng_animation_playback_state(engine,runtime->warper) != NULL ? runtime->warper : 0;
    nmo_object_id_t id = eng_create_related_object(engine,character,NMO_CID_KEYEDANIMATION);
    if (id == 0) return 0;
    nmo_object_t *object = eng_find_object(engine,id);
    nmo_keyedanimation_state_t *state = nmo_object_get_state(object);
    state->base.has_character = state->base.has_root_entity = state->base.has_data = 1;
    state->base.flags |= CKANIMATION_ALLOWTURN;
    if (!eng_animation_local_ref(engine,id,character,&state->base.character)) {
        nmo_engine_destroy_object(engine,id);
        return 0;
    }
    if (!eng_animation_local_ref(engine,id,runtime->root,&state->base.root_entity)) state->base.root_entity = nmo_ref_from_id(0);
    object->flags |= CK_OBJECT_NOTTOBELISTEDANDSAVED;
    eng_keyed_animation_refresh(engine,id);
    eng_keyed_animation_set_root_entity(engine,id,runtime->root);
    runtime->warper = id;
    return id;
}

bool eng_character_warper_parameters(nmo_engine_t *engine, nmo_object_id_t character,
    uint32_t *mode, nmo_object_id_t *source, float *from, nmo_object_id_t *target, float *to)
{
    eng_character_t *runtime = character_runtime(engine,character);
    if (runtime == NULL) return false;
    if (mode != NULL) *mode = runtime->transition_mode;
    if (source != NULL) *source = eng_animation_playback_state(engine,runtime->source) != NULL ? runtime->source : 0;
    if (from != NULL) *from = runtime->frame_src;
    if (target != NULL) *target = eng_character_next_animation(engine,character);
    if (to != NULL) *to = runtime->frame_dest;
    return true;
}

static nmo_status_t character_start_warp(nmo_engine_t *engine, eng_character_t *runtime,
    nmo_object_id_t animation, uint32_t mode, float length)
{
    if (runtime->next == animation) return NMO_OK;
    bool keyed = character_keyed(engine,animation);
    if (keyed && runtime->active == animation) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *pending = eng_animation_playback_state(engine,runtime->next);
    if (runtime->active == runtime->warper && pending != NULL && !(pending->flags & CKANIMATION_CANBEBREAK)) return NMO_OK;
    const nmo_animation_state_t *source = eng_animation_playback_state(engine,runtime->active);
    float frame, duration = eng_animation_length(source);
    if (!eng_animation_frame(engine,runtime->active,&frame) || !isfinite(frame) || !isfinite(duration) || duration < 0 || !isfinite(source->frame_rate)) return NMO_ERR_VALIDATION_FAILED;
    bool ready = (source->flags & CKANIMATION_CANBEBREAK) != 0;
    if (!ready) {
        if (source->flags & CKANIMATION_LINKTOFRAMERATE) ready = source->frame_rate < 0 ? frame == 0 : frame >= duration-.3f;
        else ready = frame == duration;
    }
    if (!ready) return NMO_OK;
    if (!keyed || !character_keyed(engine,runtime->active)) {
        runtime->active = animation;
        if (keyed) {
            eng_keyed_animation_center(engine,animation,0);
            eng_animation_set_step(engine,animation,0);
        }
        runtime->next = 0; runtime->frame_dest = 0;
        return NMO_OK;
    }
    if (eng_animation_character(engine,runtime->active) != runtime->id) return NMO_ERR_INVALID_ARGUMENT;
    nmo_object_id_t warper = eng_character_warper(engine,runtime->id);
    if (warper == 0) return runtime->warper != 0 ? NMO_ERR_INVALID_ARGUMENT : NMO_ERR_NOMEM;
    /* Capture the old alignment so a failed builder leaves playback intact. */
    eng_keyed_animation_root(engine,runtime->active);
    int32_t index = eng_map_get(&engine->animations.keyed_map,runtime->active);
    nmo_vector_t offset = index >= 0 ? engine->animations.keyed.data[index].offset : (nmo_vector_t){0};
    int32_t actor = eng_entity_index(engine,runtime->id);
    eng_matrix_t world;
    if (actor >= 0) world = engine->entities.data[actor].world;
    character_align(engine,runtime);
    float entry = 0;
    nmo_status_t status = eng_keyed_animation_transition(engine,warper,runtime->active,animation,mode,length,0,&entry);
    if (status != NMO_OK) {
        if (actor >= 0) eng_entity_set_world_matrix(engine,actor,&world,true);
        if (index >= 0) engine->animations.keyed.data[index].offset = offset;
        return status;
    }
    eng_keyed_animation_center(engine,warper,0);
    eng_animation_set_step(engine,warper,0);
    runtime->source = runtime->active; runtime->frame_src = frame; runtime->transition_mode = mode;
    runtime->active = warper; runtime->next = animation; runtime->frame_dest = entry;
    return NMO_OK;
}

nmo_status_t eng_character_set_next_animation(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation, uint32_t mode)
{
    return eng_character_set_next_animation_warp(engine,character,animation,mode,0);
}

nmo_status_t eng_character_set_next_animation_warp(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation, uint32_t mode, float length)
{
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime == NULL || !isfinite(length)) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *source = eng_animation_playback_state(engine, runtime->active);
    if (source == NULL) runtime->active = 0;
    if (animation == 0) {
        runtime->next = 0;
        if (source != NULL && (source->flags & CKANIMATION_CANBEBREAK)) runtime->active = 0;
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_animation_state_t *target = eng_animation_playback_state(engine, animation);
    if (target == NULL || eng_animation_character(engine,animation) != character) return NMO_ERR_INVALID_ARGUMENT;
    if (mode & CK_TRANSITION_FROMANIMATION) {
        uint32_t preset = (target->flags & CKANIMATION_TRANSITION_ALL) >> 8;
        if (preset != 0) mode = preset;
    }
    if (runtime->active == 0) {
        eng_animation_set_step(engine, animation, 0);
        if (character_keyed(engine, animation)) eng_keyed_animation_center(engine, animation, 0);
        eng_animation_set_step(engine, animation, 0);
        runtime->next = animation;
    } else {
        float step = 0;
        eng_animation_current_step(engine, runtime->active, &step);
        if ((mode & CK_TRANSITION_LOOPIFEQUAL) && animation == runtime->active) {
            if (step > 0.3f) { runtime->next = animation; runtime->frame_dest = 0; }
            return NMO_OK;
        }
        if (((mode & CK_TRANSITION_FROMNOW) && (source->flags & CKANIMATION_CANBEBREAK)) || step == 0) {
            if (animation == runtime->active) runtime->next = animation;
            else {
                character_align(engine, runtime);
                runtime->active = animation;
                if (character_keyed(engine, animation)) eng_keyed_animation_center(engine, animation, 0);
                eng_animation_set_step(engine, animation, 0);
                runtime->next = 0;
            }
        } else if (mode & CK_TRANSITION_TOSTART) runtime->next = animation;
        else if (mode & CK_TRANSITION_WARPMASK) return character_start_warp(engine,runtime,animation,mode,length);
        else runtime->next = animation;
    }
    runtime->frame_dest = 0;
    return NMO_OK;
}

/* Finish root translation in the source's old world basis, then transfer yaw.
 * The source cursor remains logical state even after displaying its endpoint. */
static void character_terminal(nmo_engine_t *engine, eng_character_t *runtime, float current, float endpoint)
{
    if (!character_keyed(engine, runtime->active)) { character_align(engine, runtime); return; }
    float step = 0;
    eng_animation_current_step(engine, runtime->active, &step);
    nmo_object_id_t channel = eng_keyed_animation_root(engine, runtime->active);
    int32_t root = eng_entity_index(engine, runtime->root), actor = eng_entity_index(engine, runtime->id);
    if (channel != 0 && root >= 0 && actor >= 0) {
        eng_vector_t position = {engine->entities.data[root].world.m[3][0],engine->entities.data[root].world.m[3][1],engine->entities.data[root].world.m[3][2]};
        nmo_vector_t from = {0}, to = {0};
        eng_animation_position(engine, channel, current, &from);
        eng_animation_position(engine, channel, endpoint, &to);
        float length = eng_animation_length(eng_animation_playback_state(engine, runtime->active));
        eng_object_animation_apply_step(engine, channel, endpoint == 0 || length <= 0 ? 0 : 1, runtime->active);
        eng_entity_set_position(engine, root, &position, -1, false);
        eng_vector_t delta = {to.x-from.x,to.y-from.y,to.z-from.z};
        eng_entity_translate(engine, root, &delta, actor, false);
        character_root_position(engine, runtime);
    }
    eng_animation_set_current_step(engine, runtime->active, endpoint == 0 ? 0 : 1);
    character_align(engine, runtime);
    eng_animation_set_current_step(engine, runtime->active, step);
}

static float character_remainder(const nmo_animation_state_t *source, const nmo_animation_state_t *target,
                                  float remaining, float advance, float delta)
{
    bool a = (source->flags & CKANIMATION_LINKTOFRAMERATE) != 0, b = (target->flags & CKANIMATION_LINKTOFRAMERATE) != 0;
    if (a && b && source->frame_rate != 0) remaining *= target->frame_rate/source->frame_rate;
    else if (a != b && remaining != 0 && advance != 0) {
        remaining /= advance;
        if (b) remaining *= delta*target->frame_rate*0.001f;
    }
    return remaining;
}

static bool character_secondary_bodies(nmo_engine_t *engine, eng_secondary_gate_t *gate)
{
    if (!character_keyed(engine, gate->animation)) return true;
    const nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(engine, gate->animation));
    for (uint32_t i = 0; state->animation_ids != NULL && i < state->animation_count; i++) {
        nmo_object_id_t channel = eng_object_ref(engine, gate->animation, &state->animation_ids[i]);
        nmo_object_t *object = eng_find_object(engine, channel);
        if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_OBJECTANIMATION) continue;
        nmo_object_id_t body = eng_object_animation_entity(engine,channel);
        if (!nmo_type_registry_is_class_derived_from(engine->types, nmo_engine_object_class(engine, body), NMO_CID_BODYPART)) continue;
        nmo_object_id_t *slot = NULL;
        if (!ENG_VEC_PUSH(gate->bodies, slot)) return false;
        *slot = body;
    }
    return true;
}

/* Retain the claimed targets so deleting a channel cannot leave a stale gate.
 * Releasing an older secondary must preserve a later secondary's ownership. */
static void character_secondary_gate(nmo_engine_t *engine, const eng_secondary_gate_t *gate, bool claim)
{
    for (size_t i = 0; i < gate->bodies.count; i++) {
        int32_t body = eng_entity_index(engine, gate->bodies.data[i]);
        if (body < 0) continue;
        if (claim || engine->entities.data[body].exclusive_animation == gate->animation) {
            engine->entities.data[body].exclusive_animation = claim ? gate->animation : 0;
        }
    }
}

static void character_remove_secondary(nmo_engine_t *engine, eng_character_t *runtime, size_t index)
{
    character_secondary_gate(engine, &runtime->secondary.data[index].gate, false);
    character_secondary_gate(engine, &runtime->secondary.data[index].warp_gate, false);
    free(runtime->secondary.data[index].gate.bodies.data);
    free(runtime->secondary.data[index].warp_gate.bodies.data);
    runtime->secondary.count--;
    memmove(&runtime->secondary.data[index], &runtime->secondary.data[index+1],
            (runtime->secondary.count-index)*sizeof(*runtime->secondary.data));
}

nmo_status_t eng_character_play_secondary(nmo_engine_t *engine, nmo_object_id_t character,
                                          nmo_object_id_t animation, float frame, uint32_t flags, uint32_t loops)
{
    return eng_character_play_secondary_warp(engine,character,animation,frame,flags,0,loops);
}

static bool character_owned_keyed(nmo_engine_t *engine, eng_character_t *runtime, nmo_object_id_t animation)
{
    const nmo_animation_state_t *state = eng_animation_playback_state(engine,animation);
    return state != NULL && character_keyed(engine,animation) && eng_animation_character(engine,animation) == runtime->id;
}

static nmo_object_id_t character_secondary_warper(nmo_engine_t *engine, eng_character_t *runtime)
{
    for (size_t i = 0; i < runtime->secondary_warpers.count; i++) {
        nmo_object_id_t id = runtime->secondary_warpers.data[i];
        if (eng_animation_playback_state(engine,id) == NULL) continue;
        bool used = false;
        for (size_t j = 0; j < runtime->secondary.count; j++) if (runtime->secondary.data[j].warper == id) used = true;
        if (!used) return id;
    }
    if (!eng_vec_reserve((void **)&runtime->secondary_warpers.data,&runtime->secondary_warpers.capacity,
                         runtime->secondary_warpers.count+1,sizeof(*runtime->secondary_warpers.data))) return 0;
    nmo_object_id_t id = eng_create_related_object(engine,runtime->id,NMO_CID_KEYEDANIMATION);
    if (id == 0) return 0;
    nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(engine,id));
    state->base.flags |= CKANIMATION_SECONDARYWARPER;
    runtime->secondary_warpers.data[runtime->secondary_warpers.count++] = id;
    return id;
}

nmo_status_t eng_character_play_secondary_warp(nmo_engine_t *engine, nmo_object_id_t character,
    nmo_object_id_t animation, float frame, uint32_t flags, float warp_length, uint32_t loops)
{
    eng_character_t *runtime = character_runtime(engine, character);
    const nmo_animation_state_t *state = eng_animation_playback_state(engine, animation);
    if (runtime == NULL || state == NULL || eng_animation_character(engine,animation) != character ||
        !isfinite(frame) || !isfinite(warp_length)) return NMO_ERR_INVALID_ARGUMENT;
    if (flags & CKSECONDARYANIMATION_FROMANIMATION) {
        uint32_t preset = (state->flags & CKANIMATION_SECONDARY_ALL) >> 16;
        if (preset != 0) flags = preset;
    }
    for (size_t i = 0; i < runtime->secondary.count; i++) if (runtime->secondary.data[i].id == animation) return NMO_OK;
    float length = eng_animation_length(state);
    if (!isfinite(length) || length < 0) return NMO_ERR_VALIDATION_FAILED;
    eng_secondary_animation_t entry = {.id=animation,.flags=flags,.loops=loops,.starting_frame=frame,.gate={.animation=animation}};
    if (!character_secondary_bodies(engine, &entry.gate) ||
        !eng_vec_reserve((void **)&runtime->secondary.data, &runtime->secondary.capacity,
                         runtime->secondary.count+1, sizeof(*runtime->secondary.data))) {
        free(entry.gate.bodies.data);
        return NMO_ERR_NOMEM;
    }
    nmo_status_t status = NMO_OK;
    bool warp = (flags & CKSECONDARYANIMATION_DOWARP) && character_keyed(engine,animation) &&
        runtime->active != animation && character_owned_keyed(engine,runtime,runtime->active);
    if (warp) {
        entry.warper = character_secondary_warper(engine,runtime);
        float target_frame;
        status = entry.warper != 0 ? eng_keyed_animation_transition(engine,entry.warper,runtime->active,animation,0,warp_length,frame,&target_frame) : NMO_ERR_NOMEM;
        entry.warp_gate.animation = entry.warper;
        if (status == NMO_OK && !character_secondary_bodies(engine,&entry.warp_gate)) status = NMO_ERR_NOMEM;
        if (status == NMO_OK && !eng_animation_set_frame(engine,entry.warper,0)) status = NMO_ERR_VALIDATION_FAILED;
        if (status == NMO_OK) {
            entry.phase = ENG_SECONDARY_ENTERING;
            entry.warp_length = warp_length;
            character_secondary_gate(engine,&entry.warp_gate,true);
        }
    } else {
        /* The entry pose is sampled before claiming exclusive bone ownership. */
        if (!eng_animation_set_frame(engine,animation,frame)) status = NMO_ERR_VALIDATION_FAILED;
        else character_secondary_gate(engine,&entry.gate,true);
    }
    if (status != NMO_OK) { free(entry.gate.bodies.data); free(entry.warp_gate.bodies.data); return status; }
    runtime->secondary.data[runtime->secondary.count++] = entry;
    return NMO_OK;
}

static nmo_status_t character_secondary_refresh_gate(nmo_engine_t *engine, eng_secondary_animation_t *entry, bool claim)
{
    eng_secondary_gate_t gate = {.animation=entry->warper};
    if (!character_secondary_bodies(engine,&gate)) { free(gate.bodies.data); return NMO_ERR_NOMEM; }
    character_secondary_gate(engine,&entry->warp_gate,false);
    free(entry->warp_gate.bodies.data);
    entry->warp_gate = gate;
    if (claim) character_secondary_gate(engine,&entry->warp_gate,true);
    return NMO_OK;
}

static void character_secondary_clock(nmo_engine_t *engine, nmo_object_id_t warper, bool linked, float rate)
{
    nmo_animation_state_t *state = (nmo_animation_state_t *)eng_animation_playback_state(engine,warper);
    state->flags = (state->flags & ~CKANIMATION_LINKTOFRAMERATE) | (linked ? CKANIMATION_LINKTOFRAMERATE : 0);
    state->frame_rate = rate;
}

typedef struct character_return_target {
    nmo_object_id_t id, track;
    float frame, rate;
    bool linked;
} character_return_target_t;

static nmo_status_t character_secondary_target(nmo_engine_t *engine, eng_character_t *runtime,
    float warp_length, character_return_target_t *target)
{
    const nmo_animation_state_t *active = eng_animation_playback_state(engine,runtime->active);
    float frame, length = eng_animation_length(active), duration = fmaxf(warp_length,1);
    if (!eng_animation_frame(engine,runtime->active,&frame) || !isfinite(frame) ||
        !isfinite(length) || length < 0 || !isfinite(active->frame_rate)) return NMO_ERR_VALIDATION_FAILED;
    *target = (character_return_target_t){.id=runtime->active,.frame=frame+warp_length,
        .linked=(active->flags & CKANIMATION_LINKTOFRAMERATE) != 0,.rate=active->frame_rate};
    if (!isfinite(target->frame)) return NMO_ERR_VALIDATION_FAILED;
    if (length > 0) {
        if (target->linked && target->rate == 0) target->frame = frame;
        else if (target->linked && target->rate < 0) {
            target->frame = frame-duration;
            if (target->frame < 0) {
                if (runtime->next == runtime->active) target->frame += length;
                if (target->frame < 0) target->frame = 0;
            }
        } else if (target->frame > length) {
            target->frame = fmodf(target->frame,length);
            if (target->frame == 0) target->frame = length;
        }
    } else target->frame = 0;
    const nmo_animation_state_t *queued = eng_animation_playback_state(engine,runtime->next);
    bool distinct = runtime->next != runtime->active && character_owned_keyed(engine,runtime,runtime->next);
    bool queued_linked = distinct && (queued->flags & CKANIMATION_LINKTOFRAMERATE);
    if (length > 0 && target->linked && target->rate != 0 && queued_linked) {
        float future = frame+(target->rate < 0 ? -duration : duration);
        bool crossing = target->rate < 0 ? future < 0 : future >= length &&
            (frame != length || (active->flags & CKANIMATION_CANBEBREAK));
        if (crossing) {
            float queued_length = eng_animation_length(queued);
            if (!isfinite(queued_length) || queued_length < 0 || !isfinite(queued->frame_rate)) return NMO_ERR_VALIDATION_FAILED;
            float remaining = target->rate < 0 ? future : future-length;
            float entry = runtime->frame_dest+remaining*(queued->frame_rate/target->rate);
            if (!isfinite(entry)) return NMO_ERR_VALIDATION_FAILED;
            target->id = runtime->next;
            target->frame = fminf(fmaxf(entry,0),queued_length);
        }
    }
    bool held = frame == length && !(active->flags & CKANIMATION_CANBEBREAK) && !(target->linked && target->rate < 0);
    if (held || (distinct && target->linked != queued_linked)) {
        target->track = runtime->active;
        target->frame = frame;
    }
    return NMO_OK;
}

static nmo_status_t character_secondary_return(nmo_engine_t *engine, eng_character_t *runtime,
    eng_secondary_animation_t *entry, float length, bool *warped)
{
    *warped = entry->phase == ENG_SECONDARY_LEAVING;
    if (*warped || !character_owned_keyed(engine,runtime,runtime->active)) return NMO_OK;
    nmo_object_id_t from = entry->phase == ENG_SECONDARY_PLAYING ? entry->id : entry->warper;
    if (from == runtime->active || !character_owned_keyed(engine,runtime,from)) return NMO_OK;
    character_return_target_t target;
    nmo_status_t status = character_secondary_target(engine,runtime,length,&target);
    if (status != NMO_OK) return status;
    if (eng_animation_playback_state(engine,entry->warper) == NULL) entry->warper = character_secondary_warper(engine,runtime);
    if (entry->warper == 0) return NMO_ERR_NOMEM;
    float ignored;
    if (from == target.id) {
        float frame = 0;
        eng_animation_frame(engine,runtime->active,&frame);
        status = eng_keyed_animation_transition(engine,entry->warper,from,runtime->active,0,length,frame,&ignored);
        if (status == NMO_OK) {
            eng_animation_set_current_step(engine,entry->warper,0);
            from = entry->warper;
        }
    }
    if (status == NMO_OK) status = eng_keyed_animation_transition(engine,entry->warper,from,target.id,0,length,target.frame,&ignored);
    if (status == NMO_OK) status = character_secondary_refresh_gate(engine,entry,false);
    if (status != NMO_OK) return status;
    if (target.id != runtime->active) character_secondary_clock(engine,entry->warper,target.linked,fabsf(target.rate));
    if (!eng_animation_set_frame(engine,entry->warper,0)) return NMO_ERR_VALIDATION_FAILED;
    character_secondary_gate(engine,&entry->warp_gate,true);
    entry->flags |= CKSECONDARYANIMATION_DOWARP;
    entry->warp_length = length;
    entry->track = target.track;
    entry->phase = ENG_SECONDARY_LEAVING;
    *warped = true;
    return NMO_OK;
}

nmo_status_t eng_character_stop_secondary(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation)
{
    return eng_character_stop_secondary_warp(engine,character,animation,false,0);
}

nmo_status_t eng_character_stop_secondary_from_animation(nmo_engine_t *engine, nmo_object_id_t character,
    nmo_object_id_t animation, float length)
{
    const nmo_animation_state_t *state = eng_animation_playback_state(engine,animation);
    uint32_t flags = state != NULL ? (state->flags & CKANIMATION_SECONDARY_ALL)>>16 : 0;
    return eng_character_stop_secondary_warp(engine,character,animation,(flags & CKSECONDARYANIMATION_DOWARP) != 0,length);
}

nmo_status_t eng_character_stop_secondary_warp(nmo_engine_t *engine, nmo_object_id_t character,
    nmo_object_id_t animation, bool warp, float length)
{
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime == NULL || animation == 0 || !isfinite(length)) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *state = eng_animation_playback_state(engine, animation);
    if (state != NULL && eng_animation_character(engine,animation) != character) return NMO_ERR_INVALID_ARGUMENT;
    for (size_t i = 0; i < runtime->secondary.count; i++) {
        if (runtime->secondary.data[i].id == animation) {
            bool warped = false;
            if (warp && state != NULL) {
                nmo_status_t status = character_secondary_return(engine,runtime,&runtime->secondary.data[i],length,&warped);
                if (status != NMO_OK) return status;
            }
            if (!warped) character_remove_secondary(engine,runtime,i);
            return NMO_OK;
        }
    }
    return state != NULL ? NMO_OK : NMO_ERR_INVALID_ARGUMENT;
}

void eng_character_flush_secondary(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime != NULL) while (runtime->secondary.count != 0) character_remove_secondary(engine, runtime, runtime->secondary.count-1);
}

size_t eng_character_secondary_count(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    return runtime != NULL ? runtime->secondary.count : 0;
}

nmo_object_id_t eng_character_secondary_animation(nmo_engine_t *engine, nmo_object_id_t character, size_t index)
{
    eng_character_t *runtime = character_runtime(engine, character);
    nmo_object_id_t id = runtime != NULL && index < runtime->secondary.count ? runtime->secondary.data[index].id : 0;
    return eng_animation_playback_state(engine,id) != NULL ? id : 0;
}

nmo_object_id_t eng_character_secondary_warper(nmo_engine_t *engine, nmo_object_id_t character, size_t index)
{
    eng_character_t *runtime = character_runtime(engine, character);
    nmo_object_id_t id = runtime != NULL && index < runtime->secondary.count ? runtime->secondary.data[index].warper : 0;
    return eng_animation_playback_state(engine,id) != NULL ? id : 0;
}

static nmo_status_t character_process_secondary(nmo_engine_t *engine, eng_character_t *runtime, float delta)
{
    for (size_t i = 0; i < runtime->secondary.count;) {
        eng_secondary_animation_t *entry = &runtime->secondary.data[i];
        if (entry->phase == ENG_SECONDARY_LEAVING) {
            const nmo_animation_state_t *warp = eng_animation_playback_state(engine,entry->warper);
            float next;
            if (warp == NULL) { character_remove_secondary(engine,runtime,i); continue; }
            float duration = eng_animation_length(warp);
            if (!eng_animation_next_frame(engine,entry->warper,delta,&next)) return NMO_ERR_VALIDATION_FAILED;
            bool live = false;
            if (entry->track != 0) {
                bool stopped = eng_animation_playback_state(engine,runtime->active) == NULL;
                nmo_object_id_t target = stopped ? entry->track : runtime->active;
                if (character_owned_keyed(engine,runtime,target) && target != entry->warper) {
                    const nmo_animation_state_t *state = eng_animation_playback_state(engine,target);
                    bool linked = (warp->flags & CKANIMATION_LINKTOFRAMERATE) != 0;
                    float rate = warp->frame_rate, frame = 0, step = 0;
                    eng_animation_frame(engine,target,&frame);
                    if (stopped) frame = (state->flags & CKANIMATION_LINKTOFRAMERATE) && state->frame_rate < 0 ? 0 : eng_animation_length(state);
                    eng_animation_current_step(engine,entry->warper,&step);
                    eng_animation_set_current_step(engine,entry->warper,0);
                    float ignored;
                    nmo_status_t status = eng_keyed_animation_transition(engine,entry->warper,entry->warper,target,0,duration,frame,&ignored);
                    if (status != NMO_OK) { eng_animation_set_current_step(engine,entry->warper,step); return status; }
                    character_secondary_clock(engine,entry->warper,linked,rate);
                    status = character_secondary_refresh_gate(engine,entry,true);
                    if (status != NMO_OK) return status;
                    if (!eng_animation_set_frame(engine,entry->warper,fminf(next,duration))) return NMO_ERR_VALIDATION_FAILED;
                    entry->track = target;
                    live = true;
                }
            }
            if (next <= duration) {
                if (!live && !eng_animation_set_frame(engine,entry->warper,next)) return NMO_ERR_VALIDATION_FAILED;
                i++;
            } else character_remove_secondary(engine,runtime,i);
            continue;
        }
        if (entry->phase == ENG_SECONDARY_ENTERING) {
            const nmo_animation_state_t *warp = eng_animation_playback_state(engine,entry->warper);
            float current, next;
            if (warp == NULL) { character_remove_secondary(engine,runtime,i); continue; }
            if (!eng_animation_frame(engine,entry->warper,&current) || !eng_animation_next_frame(engine,entry->warper,delta,&next)) return NMO_ERR_VALIDATION_FAILED;
            float remaining = next-eng_animation_length(warp);
            if (remaining <= 0) {
                if (!eng_animation_set_frame(engine,entry->warper,next)) return NMO_ERR_VALIDATION_FAILED;
            } else {
                const nmo_animation_state_t *source = eng_animation_playback_state(engine,entry->id);
                if (source == NULL) { character_remove_secondary(engine,runtime,i); continue; }
                remaining = character_remainder(warp,source,remaining,next-current,delta);
                character_secondary_gate(engine,&entry->gate,true);
                if (!eng_animation_set_frame(engine,entry->id,entry->starting_frame+remaining)) return NMO_ERR_VALIDATION_FAILED;
                entry->phase = ENG_SECONDARY_PLAYING;
            }
            i++;
            continue;
        }
        const nmo_animation_state_t *state = eng_animation_playback_state(engine, entry->id);
        if (state == NULL) { character_remove_secondary(engine, runtime, i); continue; }
        float length = eng_animation_length(state), next;
        if (!isfinite(length) || length < 0 || !eng_animation_next_frame(engine, entry->id, delta, &next)) return NMO_ERR_VALIDATION_FAILED;
        bool reverse_end = next < 0;
        if (!reverse_end && next <= length) {
            if (!eng_animation_set_frame(engine, entry->id, next)) return NMO_ERR_VALIDATION_FAILED;
            i++;
            continue;
        }
        float terminal = reverse_end ? 0 : length;
        float loop_frame = reverse_end ? next : next-length;
        double completed = 1;
        if (length > 0 && (entry->flags & (CKSECONDARYANIMATION_LOOP | CKSECONDARYANIMATION_LOOPNTIMES))) {
            loop_frame = fmodf(next, length);
            if (reverse_end) {
                completed = ceil(-(double)next/length);
                if (loop_frame < 0) loop_frame += length;
            } else {
                completed = floor((double)next/length);
                if (loop_frame == 0) { loop_frame = length; completed -= 1; }
            }
        }
        bool remove = false;
        float pose = terminal;
        if (entry->flags & CKSECONDARYANIMATION_LOOP) pose = loop_frame;
        else if (entry->flags & CKSECONDARYANIMATION_LOOPNTIMES) {
            if (completed < entry->loops) { entry->loops -= (uint32_t)completed; pose = loop_frame; }
            else {
                entry->loops = 0;
                if (entry->flags & CKSECONDARYANIMATION_LASTFRAME) entry->flags &= ~CKSECONDARYANIMATION_LOOPNTIMES;
                else remove = true;
            }
        } else if (!(entry->flags & CKSECONDARYANIMATION_LASTFRAME)) remove = true;
        /* Forward one-shots keep the last applied pose on expiry; reverse
         * playback samples its first pose before releasing its bones. */
        if ((!remove || reverse_end) && !eng_animation_set_frame(engine, entry->id, remove ? 0 : pose)) return NMO_ERR_VALIDATION_FAILED;
        if (remove && (entry->flags & CKSECONDARYANIMATION_DOWARP)) {
            bool warped;
            nmo_status_t status = character_secondary_return(engine,runtime,entry,entry->warp_length,&warped);
            if (status != NMO_OK) return status;
            if (warped) { i++; continue; }
        }
        if (remove) character_remove_secondary(engine, runtime, i);
        else i++;
    }
    return NMO_OK;
}

nmo_status_t eng_character_process(nmo_engine_t *engine, nmo_object_id_t character, float delta)
{
    if (!isfinite(delta) || delta < 0) return NMO_ERR_INVALID_ARGUMENT;
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime == NULL) return NMO_ERR_INVALID_ARGUMENT;
    runtime->active = eng_character_active_animation(engine, character);
    runtime->next = eng_character_next_animation(engine, character);
    const nmo_animation_state_t *source = eng_animation_playback_state(engine, runtime->active);
    if (source == NULL) {
        runtime->active = runtime->next;
        character_align(engine, runtime);
        runtime->next = 0;
    } else {
        float current, next;
        float length = eng_animation_length(source);
        bool reverse = (source->flags & CKANIMATION_LINKTOFRAMERATE) && source->frame_rate < 0;
        bool interruptible = (source->flags & CKANIMATION_CANBEBREAK) != 0;
        if (!isfinite(length) || length < 0 || !eng_animation_frame(engine, runtime->active, &current) || !isfinite(current)) return NMO_ERR_VALIDATION_FAILED;
        if (current == length && !interruptible && !reverse) {
            character_align(engine, runtime);
            runtime->active = runtime->next;
            runtime->next = 0;
            if (runtime->active != 0) {
                if (character_keyed(engine, runtime->active)) eng_keyed_animation_center(engine, runtime->active, runtime->frame_dest);
                eng_animation_set_frame(engine, runtime->active, (length != 0 ? fmodf(current,length) : current)+runtime->frame_dest);
            }
        } else {
            if (!eng_animation_next_frame(engine, runtime->active, delta, &next)) return NMO_ERR_VALIDATION_FAILED;
            bool ended = reverse ? next < 0 : next >= length;
            if (ended && (reverse || interruptible || runtime->next != 0)) {
                nmo_object_id_t previous = runtime->active;
                character_terminal(engine, runtime, current, reverse ? 0 : length);
                runtime->active = runtime->next;
                runtime->next = 0;
                const nmo_animation_state_t *target = eng_animation_playback_state(engine, runtime->active);
                if (target != NULL) {
                    float remaining = !reverse && length > 0 ? next-length : next;
                    remaining = character_remainder(source, target, remaining, next-current, delta);
                    float entry = runtime->frame_dest;
                    if (reverse && runtime->active == previous && entry == 0 &&
                        (target->flags & CKANIMATION_LINKTOFRAMERATE) && target->frame_rate < 0) entry = eng_animation_length(target);
                    if (reverse) {
                        float target_length = eng_animation_length(target);
                        eng_animation_set_current_step(engine, runtime->active, target_length != 0 ? entry/target_length : 0);
                    }
                    if (character_keyed(engine, runtime->active)) eng_keyed_animation_center(engine, runtime->active, entry);
                    eng_animation_set_frame(engine, runtime->active, entry+remaining);
                    if (character_keyed(engine, runtime->active)) {
                        character_root_position(engine, runtime);
                        float center = reverse ? 0 : entry+remaining;
                        if (reverse) eng_animation_frame(engine, runtime->active, &center);
                        eng_keyed_animation_center(engine, runtime->active, center);
                    }
                }
            } else {
                if (ended) next = length;
                eng_animation_set_frame(engine, runtime->active, next);
                if (character_keyed(engine, runtime->active)) {
                    character_root_position(engine, runtime);
                    eng_keyed_animation_center(engine, runtime->active, next);
                }
            }
        }
    }
    nmo_status_t status = character_process_secondary(engine, runtime, delta);
    if (status != NMO_OK) return status;
    int32_t actor = eng_entity_index(engine, character);
    if (actor >= 0) engine->entities.data[actor].moveable_flags &= ~VX_MOVEABLE_CHARACTERRENDERED;
    return NMO_OK;
}
