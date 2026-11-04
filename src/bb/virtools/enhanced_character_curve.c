/* Enhanced Character Curve Follow owns its animation and a bounded path interval. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

static void enhanced_orientation(blk_t *b, int32_t actor, eng_vector_t direction)
{
    eng_vector_t up = {0,1,0}, forward;
    direction.y = 0; eng_vector_normalize(&direction);
    switch (get_in_int(b,3,1)) {
    case 1: forward = eng_vector_cross(&direction,&up); break;
    case 2: forward = eng_vector_cross(&up,&direction); break;
    case 3:
        eng_entity_set_orientation(b->engine,actor,&up,&direction,NULL,-1,false); return;
    case 4:
        up.y = -1; eng_entity_set_orientation(b->engine,actor,&up,&direction,NULL,-1,false); return;
    case 5: forward = direction; break;
    case 6: forward = (eng_vector_t){-direction.x,-direction.y,-direction.z}; break;
    default: return; /* The native switch leaves orientation untouched. */
    }
    eng_entity_set_orientation(b->engine,actor,&forward,&up,NULL,-1,false);
}

static int bb_enhanced_character_curve(blk_t *b)
{
    nmo_object_id_t owner = nmo_engine_block_target(b);
    int32_t actor = eng_entity_index(b->engine,owner);
    if (actor < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER) return NMO_ENGINE_BR_OWNERERROR;
    float height = eng_entity_position(b->engine,actor,-1).y;
    nmo_object_id_t curve = nmo_engine_block_get_input_object(b,0);
    float length;
    if (!eng_curve_length(b->engine,curve,&length)) return NMO_ENGINE_BR_PARAMETERERROR;
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b,4);
    const nmo_animation_state_t *settings = eng_animation_playback_state(b->engine,animation);
    if (settings == NULL) animation = 0;
    else if (!eng_animation_set_playback(b->engine,animation,CKANIMATION_CANBEBREAK,CKANIMATION_CANBEBREAK,settings->frame_rate))
        return NMO_ENGINE_BR_GENERICERROR;
    const uint32_t mode = CK_TRANSITION_WARPBEST | CK_TRANSITION_LOOPIFEQUAL | CK_TRANSITION_USEVELOCITY;
    bool initial = IN_ACTIVE(b,0), forward;
    float current, step;
    if (initial) {
        IN_OFF(b,0);
        step = get_in_float(b,1,0); current = length*step;
        float end = get_in_float(b,2,1), total = length*end;
        int32_t ascending = step <= end;
        if (nmo_engine_block_set_local_value(b,2,&current,sizeof(current)) != NMO_OK ||
            nmo_engine_block_set_local_value(b,3,&total,sizeof(total)) != NMO_OK ||
            nmo_engine_block_set_local_value(b,4,&ascending,sizeof(ascending)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    } else if (IN_ACTIVE(b,1)) {
        IN_OFF(b,1);
        if (!eng_character_set_automatic(b->engine,owner,true)) return NMO_ENGINE_BR_GENERICERROR;
        eng_vector_t position = eng_entity_position(b->engine,actor,-1), previous;
        if (nmo_engine_block_get_local_value(b,1,&previous,sizeof(previous)) != NMO_OK) return NMO_ENGINE_BR_PARAMETERERROR;
        eng_vector_t displacement = {position.x-previous.x,0,position.z-previous.z};
        forward = get_local_int(b,4,0) != 0;
        current = get_local_float(b,2,0) + (forward ? 1 : -1)*eng_vector_magnitude(&displacement);
        if (nmo_engine_block_set_local_value(b,2,&current,sizeof(current)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
        float total = get_local_float(b,3,0);
        if (forward ? current >= total : current < total) {
            OUT_ON(b,0); set_out_float(b,0,forward ? 1 : 0);
            if (!get_in_int(b,5,0)) {
                /* SetNextActiveAnimation(NULL) stops interruptible playback;
                 * its native error return is intentionally ignored. */
                eng_character_set_next_animation_warp(b->engine,owner,0,mode,5);
            }
            return NMO_ENGINE_BR_OK;
        }
        step = current/length; set_out_float(b,0,step);
    } else return NMO_ENGINE_BR_OK;
    eng_vector_t position, ahead;
    if (!eng_curve_position(b->engine,curve,step,&position) || !eng_curve_position(b->engine,curve,step+.01f,&ahead))
        return NMO_ENGINE_BR_PARAMETERERROR;
    eng_vector_t direction = {ahead.x-position.x,ahead.y-position.y,ahead.z-position.z};
    if (nmo_engine_block_set_local_value(b,1,&position,sizeof(position)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    if (initial && !eng_character_align_root(b->engine,owner)) return NMO_ENGINE_BR_GENERICERROR;
    position.y = height; eng_entity_set_position(b->engine,actor,&position,-1,false);
    enhanced_orientation(b,actor,direction);
    eng_character_set_next_animation_warp(b->engine,owner,animation,mode,5);
    OUT_ON(b,1);
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_enhanced_character_curve(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x785e7760,0x46ea50b5),"Enhanced Character Curve Follow",bb_enhanced_character_curve,NULL,NULL};
    return nmo_engine_register_block(engine,&block);
}
