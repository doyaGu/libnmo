/* Move To keeps its native time, legacy integer-time and frame cursors. */
#include "virtools.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <limits.h>
#include <string.h>

static int move_offset(blk_t *b)
{
    return nmo_guid_equals(nmo_engine_block_input_parameter_type(b,1),CKPGUID_3DENTITY) ? 1 : 0;
}

static int32_t move_add(int32_t a, int32_t c)
{
    uint32_t word = (uint32_t)a+(uint32_t)c;
    int32_t result; memcpy(&result,&word,sizeof(result)); return result;
}

static int move_time(blk_t *b, int32_t entity, int offset, const void *curve, size_t size, bool legacy)
{
    int32_t reference = offset ? input_entity(b,1) : -1;
    eng_vector_t destination = get_in_vector(b,0,0,0,0);
    if (reference >= 0) eng_entity_transform(b->engine,reference,&destination,&destination,-1);
    int32_t integer_duration = legacy ? get_in_int(b,1+offset,1000) : 0;
    float duration = legacy ? (float)integer_duration : get_in_float(b,1+offset,1000);
    bool initial = IN_ACTIVE(b,0);
    if (initial) {
        IN_OFF(b,0); set_local_int(b,0,0);
        eng_vector_t origin = eng_entity_position(b->engine,entity,-1);
        nmo_engine_block_set_local_value(b,3,&origin,sizeof(origin));
    } else IN_OFF(b,1);
    float elapsed;
    int32_t integer_elapsed = 0;
    if (legacy) {
        float delta = nmo_engine_block_delta_time(b);
        int32_t step = delta >= -2147483648.0f && delta < 2147483648.0f ? (int32_t)delta : INT32_MIN;
        integer_elapsed = move_add(get_local_int(b,0,0),step);
        elapsed = (float)integer_elapsed;
    } else elapsed = get_local_float(b,0,0)+nmo_engine_block_delta_time(b);
    float progression = elapsed/duration;
    if (progression > 1) progression = 1;
    float value = nmo_engine_curve_y(curve,size,progression);
    eng_vector_t origin = {0}; nmo_engine_block_get_local_value(b,3,&origin,sizeof(origin));
    destination = (eng_vector_t){origin.x+(destination.x-origin.x)*value,
        origin.y+(destination.y-origin.y)*value,origin.z+(destination.z-origin.z)*value};
    eng_entity_set_position(b->engine,entity,&destination,-1,keep_children(b,3+offset));
    if (legacy ? integer_elapsed > integer_duration : elapsed > duration) {
        OUT_ON(b,0);
        return NMO_ENGINE_BR_OK; /* Final movement does not overwrite the cursor or outputs. */
    }
    OUT_ON(b,1);
    if (legacy) set_local_int(b,0,integer_elapsed);
    else nmo_engine_block_set_local_value(b,0,&elapsed,sizeof(elapsed));
    set_out_float(b,0,progression); set_out_float(b,1,value);
    return NMO_ENGINE_BR_OK;
}

static int move_frames(blk_t *b, int32_t entity, int offset, const void *curve, size_t size)
{
    int32_t reference = offset ? input_entity(b,1) : -1;
    int32_t remaining = get_local_int(b,0,0);
    if (IN_ACTIVE(b,0)) {
        eng_vector_t destination = get_in_vector(b,0,0,0,0), here = eng_entity_position(b->engine,entity,reference);
        eng_vector_t travel = {destination.x-here.x,destination.y-here.y,destination.z-here.z};
        remaining = get_in_int(b,1+offset,0);
        if (remaining <= 0) {
            eng_entity_translate(b->engine,entity,&travel,reference,keep_children(b,3+offset));
            set_out_float(b,0,1); set_out_float(b,1,nmo_engine_curve_y(curve,size,1));
            IN_OFF(b,0); OUT_ON(b,0);
            return NMO_ENGINE_BR_OK;
        }
        float zero = 0, increment = 1.0f/(float)remaining;
        set_out_float(b,0,0); set_out_float(b,1,nmo_engine_curve_y(curve,size,0));
        set_local_int(b,0,remaining);
        nmo_engine_block_set_local_value(b,1,&zero,sizeof(zero));
        nmo_engine_block_set_local_value(b,2,&increment,sizeof(increment));
        nmo_engine_block_set_local_value(b,3,&travel,sizeof(travel));
        IN_OFF(b,0); OUT_ON(b,1);
        return NMO_ENGINE_BR_OK;
    }
    float previous = get_local_float(b,1,0), progression = previous+get_local_float(b,2,0);
    float value = nmo_engine_curve_y(curve,size,progression), delta = value-nmo_engine_curve_y(curve,size,previous);
    set_out_float(b,0,progression); set_out_float(b,1,value);
    eng_vector_t travel = {0}; nmo_engine_block_get_local_value(b,3,&travel,sizeof(travel));
    travel = (eng_vector_t){travel.x*delta,travel.y*delta,travel.z*delta};
    eng_entity_translate(b->engine,entity,&travel,reference,keep_children(b,3+offset));
    remaining = move_add(remaining,-1); set_local_int(b,0,remaining);
    nmo_engine_block_set_local_value(b,1,&progression,sizeof(progression));
    IN_OFF(b,1); OUT_ON(b,remaining != 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_move_to(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) return NMO_ENGINE_BR_OWNERERROR;
    int offset = move_offset(b);
    size_t size = 0;
    const void *curve = nmo_engine_block_input_data(b,2+offset,&size);
    if (curve == NULL) return NMO_ENGINE_BR_PARAMETERERROR;
    if (!get_local_int(b,4,0)) return move_frames(b,entity,offset,curve,size);
    bool legacy = nmo_engine_block_version(b) < 0x10005 &&
        !nmo_guid_equals(nmo_engine_block_input_parameter_type(b,1+offset),CKPGUID_TIME);
    return move_time(b,entity,offset,curve,size,legacy);
}

static int move_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    eng_node_t *node = &b->engine->nodes.data[b->node];
    eng_param_t *duration = eng_node_param(b->engine,node->first_pin,node->pin_count,1+move_offset(b));
    if (duration == NULL) return NMO_ENGINE_BR_PARAMETERERROR;
    bool time = get_local_int(b,4,0) != 0;
    bool old_integer = node->version < 0x10005 && !nmo_guid_equals(duration->type,CKPGUID_TIME);
    duration->type = time && !old_integer ? CKPGUID_TIME : CKPGUID_INT;
    duration->type_desc = nmo_type_registry_find_by_guid(b->engine->types,duration->type);
    if (time && !old_integer && node->version < 0x10005) node->version = 0x10005;
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_move_to(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x010d010b,0x011d010b),"Move To",bb_move_to,move_callback,NULL};
    return nmo_engine_register_block(engine,&block);
}
