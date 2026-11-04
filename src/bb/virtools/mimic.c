/* Mimic samples each source component after applying the preceding ones. */
#include "virtools.h"

static float mimic_speed(blk_t *b, int input, float previous, bool time_based)
{
    nmo_engine_block_get_input_value(b,input,&previous,sizeof(previous));
    if (time_based) {
        previous *= nmo_engine_block_delta_time(b)*.07f;
        if (previous > 1) previous = 1;
    }
    return previous;
}

static eng_vector_t mimic_vector(eng_vector_t a, eng_vector_t c, float amount)
{
    return (eng_vector_t){a.x+(c.x-a.x)*amount,a.y+(c.y-a.y)*amount,a.z+(c.z-a.z)*amount};
}

static int bb_mimic(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    int32_t target = target_entity(b), source = input_entity(b,0);
    if (target < 0) return NMO_ENGINE_BR_OWNERERROR;
    if (source < 0) return NMO_ENGINE_BR_OK;
    bool keep = keep_children(b,7), time = get_local_int(b,0,0) != 0;
    float speed = 0;
    if (get_in_int(b,1,1)) {
        speed = mimic_speed(b,2,speed,time);
        eng_vector_t a = eng_entity_position(b->engine,target,-1), c = eng_entity_position(b->engine,source,-1);
        eng_vector_t position = mimic_vector(a,c,speed);
        eng_entity_set_position(b->engine,target,&position,-1,keep);
    }
    if (get_in_int(b,3,1)) {
        speed = mimic_speed(b,4,speed,time);
        nmo_quaternion_t a, c;
        if (!eng_entity_quaternion(b->engine,target,&a,-1) || !eng_entity_quaternion(b->engine,source,&c,-1))
            return NMO_ENGINE_BR_GENERICERROR;
        nmo_quaternion_t orientation = eng_quaternion_slerp(speed,a,c);
        if (!eng_entity_set_quaternion(b->engine,target,&orientation,-1,keep,true)) return NMO_ENGINE_BR_GENERICERROR;
    }
    if (get_in_int(b,5,1)) {
        speed = mimic_speed(b,6,speed,time);
        eng_vector_t a = eng_entity_scale(b->engine,target,true), c = eng_entity_scale(b->engine,source,true);
        eng_vector_t scale = mimic_vector(a,c,speed);
        eng_entity_set_scale(b->engine,target,&scale,keep,true);
    }
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_mimic(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x12db7b06,0x74c514c3),"Mimic",bb_mimic,NULL,NULL};
    return nmo_engine_register_block(engine,&block);
}
