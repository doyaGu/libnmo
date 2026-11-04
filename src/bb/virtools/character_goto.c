/* Character Go To steers a controller with self-addressed Joy messages. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

#include <math.h>

static int goto_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD && message != NMO_ENGINE_MSG_RESET) return NMO_ENGINE_BR_OK;
    const char *names[] = {"Joy_Up","Joy_Down","Joy_Left","Joy_Right"};
    int32_t types[4];
    for (size_t i = 0; i < 4; i++) {
        types[i] = eng_message_type_add(b->engine,names[i]);
        if (types[i] < 0) return NMO_ENGINE_BR_GENERICERROR;
    }
    return nmo_engine_block_set_local_value(b,0,types,sizeof(types)) == NMO_OK ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
}

static int bb_character_goto(blk_t *b)
{
    IN_OFF(b,0); IN_OFF(b,1);
    nmo_object_id_t owner = nmo_engine_block_target(b);
    int32_t character = eng_entity_index(b->engine,owner);
    if (character < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER) return NMO_ENGINE_BR_OWNERERROR;
    int32_t root = eng_entity_index(b->engine,eng_character_root(b->engine,owner)), target = input_entity(b,0);
    if (root < 0 || target < 0) { OUT_ON(b,1); return NMO_ENGINE_BR_OK; }
    int32_t messages[4];
    if (nmo_engine_block_get_local_value(b,0,messages,sizeof(messages)) != NMO_OK) return NMO_ENGINE_BR_PARAMETERERROR;
    eng_vector_t here = eng_entity_position(b->engine,root,-1), there = eng_entity_position(b->engine,target,-1);
    eng_vector_t delta = {there.x-here.x,0,there.z-here.z};
    /* The native block narrows the squared horizontal distance before sqrt. */
    float distance = sqrtf(delta.x*delta.x+delta.z*delta.z);
    eng_vector_t side, up, forward;
    switch (get_in_int(b,2,1)) {
    case 1:
        eng_entity_orientation(b->engine,character,&side,&up,&forward,-1);
        side = (eng_vector_t){-side.x,-side.y,-side.z}; break;
    case 2:
        eng_entity_orientation(b->engine,character,&side,&up,&forward,-1);
        forward = (eng_vector_t){-forward.x,-forward.y,-forward.z}; break;
    case 3:
        eng_entity_orientation(b->engine,character,&up,&forward,&side,-1); break;
    case 4:
        eng_entity_orientation(b->engine,character,&up,&forward,&side,-1);
        forward = (eng_vector_t){-forward.x,-forward.y,-forward.z};
        side = (eng_vector_t){-side.x,-side.y,-side.z}; break;
    case 5:
        eng_entity_orientation(b->engine,character,&forward,&up,&side,-1); break;
    case 6:
        eng_entity_orientation(b->engine,character,&forward,&up,&side,-1);
        forward = (eng_vector_t){-forward.x,-forward.y,-forward.z};
        side = (eng_vector_t){-side.x,-side.y,-side.z}; break;
    default: return NMO_ENGINE_BR_PARAMETERERROR;
    }
    side.y = forward.y = 0;
    float reciprocal = 1/distance;
    delta.x *= reciprocal; delta.y *= reciprocal; delta.z *= reciprocal;
    bool reverse = get_in_int(b,4,0) != 0;
    if (reverse) forward = (eng_vector_t){-forward.x,-forward.y,-forward.z};
    float front = forward.x*delta.x+forward.y*delta.y+forward.z*delta.z;
    float lateral = side.x*delta.x+side.y*delta.y+side.z*delta.z;
    float limit = cosf(get_in_float(b,3,1.22f));
    if (limit >= .98f) limit = .98f;
    float minimum = get_in_float(b,1,12);
    if (distance > minimum && front > limit &&
        nmo_engine_block_send_message(b,messages[reverse ? 1 : 0],owner,owner) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    if (front <= .98f && nmo_engine_block_send_message(b,messages[lateral < 0 ? 2 : 3],owner,owner) != NMO_OK)
        return NMO_ENGINE_BR_GENERICERROR;
    OUT_ON(b,distance <= minimum ? 0 : 2);
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_character_goto(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x11223344,0xaabbccdd),"Character Go To",bb_character_goto,goto_callback,NULL};
    return nmo_engine_register_block(engine,&block);
}
