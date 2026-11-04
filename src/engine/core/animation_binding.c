/* Live animation ownership and channel targets use engine IDs across modules. */
#include "../engine_internal.h"
#include "object/nmo_class_ids.h"

static const nmo_objectanimation_state_t *binding_part(const nmo_engine_t *engine, nmo_object_id_t id)
{
    if (eng_map_get(&engine->destroyed,id) >= 0) return NULL;
    nmo_object_t *object = eng_find_object(engine,id);
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_OBJECTANIMATION ? nmo_object_get_state(object) : NULL;
}

static eng_animation_binding_t *binding_runtime(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_map_get(&engine->animations.binding_map,id);
    if (index >= 0) return &engine->animations.bindings.data[index];
    index = (int32_t)engine->animations.bindings.count;
    eng_animation_binding_t *binding = NULL;
    if (!ENG_VEC_PUSH(engine->animations.bindings,binding)) return NULL;
    binding->id = id;
    if (!eng_map_put(&engine->animations.binding_map,id,index)) return NULL;
    return binding;
}

nmo_object_id_t eng_animation_character(const nmo_engine_t *engine, nmo_object_id_t animation)
{
    const nmo_animation_state_t *state = eng_animation_state(engine,animation);
    if (state == NULL) return 0;
    int32_t index = eng_map_get(&engine->animations.binding_map,animation);
    const eng_animation_binding_t *binding = index >= 0 ? &engine->animations.bindings.data[index] : NULL;
    nmo_object_id_t character = binding != NULL && binding->has_character ? binding->character : eng_object_ref(engine,animation,&state->character);
    return eng_entity_index(engine,character) >= 0 && nmo_engine_object_class(engine,character) == NMO_CID_CHARACTER ? character : 0;
}

bool eng_animation_set_character(nmo_engine_t *engine, nmo_object_id_t animation, nmo_object_id_t character)
{
    if (eng_animation_state(engine,animation) == NULL || (character != 0 &&
        (eng_entity_index(engine,character) < 0 || nmo_engine_object_class(engine,character) != NMO_CID_CHARACTER))) return false;
    eng_animation_binding_t *binding = binding_runtime(engine,animation);
    if (binding == NULL) return false;
    binding->character = character; binding->has_character = true;
    return true;
}

nmo_object_id_t eng_object_animation_entity(const nmo_engine_t *engine, nmo_object_id_t animation)
{
    const nmo_objectanimation_state_t *state = binding_part(engine,animation);
    if (state == NULL) return 0;
    int32_t index = eng_map_get(&engine->animations.binding_map,animation);
    const eng_animation_binding_t *binding = index >= 0 ? &engine->animations.bindings.data[index] : NULL;
    nmo_object_id_t entity = binding != NULL && binding->has_entity ? binding->entity : eng_object_ref(engine,animation,&state->entity);
    return eng_entity_index(engine,entity) >= 0 ? entity : 0;
}

bool eng_object_animation_set_entity(nmo_engine_t *engine, nmo_object_id_t animation, nmo_object_id_t entity)
{
    if (binding_part(engine,animation) == NULL || (entity != 0 && eng_entity_index(engine,entity) < 0)) return false;
    eng_animation_binding_t *binding = binding_runtime(engine,animation);
    if (binding == NULL) return false;
    binding->entity = entity; binding->has_entity = true;
    return true;
}

static nmo_object_id_t binding_copy_ref(nmo_object_id_t id, const nmo_object_id_t *originals,
    const nmo_object_id_t *copies, size_t count)
{
    for (size_t i = 0; i < count; i++) if (originals[i] == id) return copies[i];
    return id;
}

void eng_copy_animation_bindings(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count)
{
    int32_t index = eng_map_get(&engine->animations.binding_map,source);
    if (index < 0) return;
    eng_animation_binding_t snapshot = engine->animations.bindings.data[index];
    if (snapshot.has_character) eng_animation_set_character(engine,target,
        binding_copy_ref(eng_animation_character(engine,source),originals,copies,count));
    if (snapshot.has_entity) eng_object_animation_set_entity(engine,target,
        binding_copy_ref(eng_object_animation_entity(engine,source),originals,copies,count));
}
