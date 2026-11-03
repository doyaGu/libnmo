/* CKCharacter's non-warp primary playback and root-motion handoff. */
#include "../engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_class_ids.h"

#include <math.h>

static const nmo_character_state_t *character_state(const nmo_engine_t *engine, nmo_object_id_t id)
{
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
    runtime->automatic = (state->base.entity_flags & CK_3DENTITY_CHARACTERDOPROCESS) != 0;
    if (!eng_map_put(&engine->characters.map, id, index)) return NULL;
    return runtime;
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
        if (character_runtime(engine, eng_module_id(module, id)) == NULL) return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

void eng_free_characters(nmo_engine_t *engine)
{
    free(engine->characters.entries.data);
    free(engine->characters.map.slots);
}

nmo_object_id_t eng_character_active_animation(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    return runtime != NULL && eng_animation_state(engine, runtime->active) != NULL ? runtime->active : 0;
}

nmo_object_id_t eng_character_next_animation(nmo_engine_t *engine, nmo_object_id_t character)
{
    eng_character_t *runtime = character_runtime(engine, character);
    return runtime != NULL && eng_animation_state(engine, runtime->next) != NULL ? runtime->next : 0;
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
    const nmo_animation_state_t *state = eng_animation_state(engine, runtime->active);
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
    if (runtime == NULL || (animation != 0 && eng_animation_state(engine, animation) == NULL)) return NMO_ERR_INVALID_ARGUMENT;
    runtime->active = animation;
    return NMO_OK;
}

nmo_status_t eng_character_set_next_animation(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation, uint32_t mode)
{
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *source = eng_animation_state(engine, runtime->active);
    if (source == NULL) runtime->active = 0;
    if (animation == 0) {
        runtime->next = 0;
        if (source != NULL && (source->flags & CKANIMATION_CANBEBREAK)) runtime->active = 0;
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_animation_state_t *target = eng_animation_state(engine, animation);
    if (target == NULL || eng_object_ref(engine, animation, &target->character) != character) return NMO_ERR_INVALID_ARGUMENT;
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
        else if (mode & CK_TRANSITION_WARPMASK) return NMO_ERR_NOT_SUPPORTED;
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
        float length = eng_animation_length(eng_animation_state(engine, runtime->active));
        eng_object_animation_set_step(engine, channel, endpoint == 0 || length <= 0 ? 0 : 1);
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

nmo_status_t eng_character_process(nmo_engine_t *engine, nmo_object_id_t character, float delta)
{
    if (!isfinite(delta) || delta < 0) return NMO_ERR_INVALID_ARGUMENT;
    eng_character_t *runtime = character_runtime(engine, character);
    if (runtime == NULL) return NMO_ERR_INVALID_ARGUMENT;
    runtime->active = eng_character_active_animation(engine, character);
    runtime->next = eng_character_next_animation(engine, character);
    const nmo_animation_state_t *source = eng_animation_state(engine, runtime->active);
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
                const nmo_animation_state_t *target = eng_animation_state(engine, runtime->active);
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
    int32_t actor = eng_entity_index(engine, character);
    if (actor >= 0) engine->entities.data[actor].moveable_flags &= ~VX_MOVEABLE_CHARACTERRENDERED;
    return NMO_OK;
}
