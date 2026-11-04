/* Blend controls belong to the running engine, not to loaded animation data. */
#include "../engine_internal.h"
#include "object/nmo_class_ids.h"

float eng_animation_merge_factor(const nmo_engine_t *engine, nmo_object_id_t animation)
{
    if (eng_map_get(&engine->destroyed,animation) >= 0) return 0;
    int32_t index = eng_map_get(&engine->animations.blend_map,animation);
    if (index >= 0) return engine->animations.blends.data[index].factor;
    nmo_object_t *object = eng_find_object(engine,animation);
    if (object == NULL) return 0;
    uint32_t type = nmo_object_get_class_id(object);
    if (type == NMO_CID_OBJECTANIMATION)
        return ((const nmo_objectanimation_state_t *)nmo_object_get_state(object))->merge_factor;
    if (type == NMO_CID_KEYEDANIMATION)
        return ((const nmo_keyedanimation_state_t *)nmo_object_get_state(object))->merge_factor;
    return 0;
}

static bool blend_factor(nmo_engine_t *engine, nmo_object_id_t animation, float factor)
{
    int32_t index = eng_map_get(&engine->animations.blend_map,animation);
    if (index < 0) {
        index = (int32_t)engine->animations.blends.count;
        eng_animation_blend_t *blend = NULL;
        if (!ENG_VEC_PUSH(engine->animations.blends,blend)) return false;
        blend->id = animation;
        if (!eng_map_put(&engine->animations.blend_map,animation,index)) return false;
    }
    engine->animations.blends.data[index].factor = factor;
    return true;
}

bool eng_animation_set_merge_factor(nmo_engine_t *engine, nmo_object_id_t animation, float factor)
{
    if (eng_map_get(&engine->destroyed,animation) >= 0) return false;
    uint32_t type = nmo_engine_object_class(engine,animation);
    if (type == NMO_CID_ANIMATION) return true; /* Base virtual method is empty. */
    if (type != NMO_CID_OBJECTANIMATION && type != NMO_CID_KEYEDANIMATION) return false;
    if (!blend_factor(engine,animation,factor)) return false;
    if (type == NMO_CID_KEYEDANIMATION) {
        const nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(engine,animation));
        for (uint32_t i = 0; state->animation_ids != NULL && i < state->animation_count; i++) {
            nmo_object_id_t part = eng_object_ref(engine,animation,&state->animation_ids[i]);
            if (nmo_engine_object_class(engine,part) == NMO_CID_OBJECTANIMATION && !blend_factor(engine,part,factor)) return false;
        }
    }
    return true;
}

void eng_copy_animation_blend(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target)
{
    int32_t index = eng_map_get(&engine->animations.blend_map,source);
    if (index >= 0) blend_factor(engine,target,engine->animations.blends.data[index].factor);
}
