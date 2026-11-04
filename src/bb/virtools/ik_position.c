/* IK Position moves the hand's requested goal a fraction on each trigger. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

static int ik_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    nmo_object_id_t chain = eng_create_related_object(b->engine,nmo_engine_block_id(b),NMO_CID_KINEMATICCHAIN);
    if (chain == 0) return NMO_ENGINE_BR_GENERICERROR;
    if (nmo_object_set_name(eng_find_object(b->engine,chain),"IKPosChain") != NMO_OK) {
        nmo_engine_destroy_object(b->engine,chain);
        return NMO_ENGINE_BR_GENERICERROR;
    }
    if (nmo_engine_block_set_local_value(b,0,&chain,sizeof(chain)) != NMO_OK) {
        nmo_engine_destroy_object(b->engine,chain);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_ik_position(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t chain = (nmo_object_id_t)get_local_int(b,0,0);
    if (!eng_kinematic_effectors(b->engine,chain,NULL,NULL)) return NMO_ENGINE_BR_PARAMETERERROR;
    nmo_object_id_t start = nmo_engine_block_get_input_object(b,0), end = nmo_engine_block_get_input_object(b,1);
    if (eng_entity_index(b->engine,start) < 0 || nmo_engine_object_class(b->engine,start) != NMO_CID_BODYPART)
        return NMO_ENGINE_BR_PARAMETERERROR;
    if (!eng_kinematic_set_effectors(b->engine,chain,start,end)) return NMO_ENGINE_BR_GENERICERROR;
    int32_t last = eng_entity_index(b->engine,end), target = input_entity(b,2);
    if (last < 0 || nmo_engine_object_class(b->engine,end) != NMO_CID_BODYPART || target < 0)
        return NMO_ENGINE_BR_PARAMETERERROR;
    int32_t latency = get_in_int(b,3,5);
    if (latency == 0) latency = 1;
    eng_vector_t goal = eng_entity_position(b->engine,target,-1), here = eng_entity_position(b->engine,last,-1);
    goal = (eng_vector_t){here.x+(goal.x-here.x)/(float)latency,
        here.y+(goal.y-here.y)/(float)latency,here.z+(goal.z-here.z)/(float)latency};
    /* Native forwards neither the chain's failure nor a continuous activation. */
    eng_kinematic_set_position(b->engine,start,end,&goal);
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_ik_position(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0xbefc4a32,0x893ecad4),"IK Position",bb_ik_position,ik_callback,NULL};
    return nmo_engine_register_block(engine,&block);
}
