/* Curve Follow advances only through its In and Loop In triggers. */
#include "virtools.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <limits.h>
#include <string.h>

static int32_t curve_integer(float value)
{
    return value >= -2147483648.0f && value < 2147483648.0f ? (int32_t)value : INT32_MIN;
}

/* Preserve the original 32-bit cursor arithmetic without signed overflow. */
static int32_t curve_add(int32_t a, int32_t c)
{
    uint32_t word = (uint32_t)a+(uint32_t)c;
    int32_t result; memcpy(&result,&word,sizeof(result)); return result;
}

static bool curve_exists(blk_t *b, nmo_object_id_t path)
{
    return eng_entity_index(b->engine,path) >= 0 &&
        nmo_type_registry_is_class_derived_from(b->engine->types,
            (uint32_t)nmo_engine_object_class(b->engine,path),NMO_CID_CURVE);
}

static bool curve_position(blk_t *b, int32_t entity, nmo_object_id_t path, float value)
{
    eng_vector_t position;
    if (!eng_curve_position(b->engine,path,value,&position)) return false;
    eng_entity_set_position(b->engine,entity,&position,-1,keep_children(b,3));
    return true;
}

static int curve_time(blk_t *b, int32_t entity, bool legacy)
{
    nmo_object_id_t path = nmo_engine_block_get_input_object(b,0);
    if (!curve_exists(b,path)) return NMO_ENGINE_BR_PARAMETERERROR;
    int32_t integer_duration = legacy ? get_in_int(b,1,1000) : 0;
    float duration = legacy ? (float)integer_duration : get_in_float(b,1,1000);
    size_t size = 0; const void *curve = nmo_engine_block_input_data(b,2,&size);
    if (curve == NULL) return NMO_ENGINE_BR_PARAMETERERROR;
    if (IN_ACTIVE(b,0)) { IN_OFF(b,0); set_local_int(b,0,0); }
    else IN_OFF(b,1);
    int32_t integer_elapsed = 0;
    float elapsed;
    if (legacy) {
        integer_elapsed = curve_add(get_local_int(b,0,0),curve_integer(nmo_engine_block_delta_time(b)));
        elapsed = (float)integer_elapsed;
    } else elapsed = get_local_float(b,0,0)+nmo_engine_block_delta_time(b);
    float progression = elapsed/duration;
    if (progression > 1) progression = 1;
    float value = nmo_engine_curve_y(curve,size,progression);
    set_out_float(b,0,progression); set_out_float(b,1,value);
    if (!curve_position(b,entity,path,value)) return NMO_ENGINE_BR_PARAMETERERROR;
    if (legacy ? integer_elapsed > integer_duration : elapsed > duration) {
        OUT_ON(b,0);
        return NMO_ENGINE_BR_OK; /* Outputs advance on completion; the elapsed cursor does not. */
    }
    OUT_ON(b,1);
    if (legacy) set_local_int(b,0,integer_elapsed);
    else nmo_engine_block_set_local_value(b,0,&elapsed,sizeof(elapsed));
    return NMO_ENGINE_BR_OK;
}

static int curve_frames(blk_t *b, int32_t entity)
{
    if (!IN_ACTIVE(b,0) && !IN_ACTIVE(b,1)) return NMO_ENGINE_BR_OK;
    bool initial = IN_ACTIVE(b,0);
    IN_OFF(b,initial ? 0 : 1);
    float frames = initial ? get_in_float(b,1,10) : 0;
    nmo_object_id_t path = nmo_engine_block_get_input_object(b,0);
    if (!curve_exists(b,path)) return NMO_ENGINE_BR_PARAMETERERROR;
    if (initial) {
        if (!(frames > 0)) return NMO_ENGINE_BR_PARAMETERERROR;
        int32_t remaining = curve_add(curve_integer(frames),-1);
        if (remaining <= 0) {
            size_t size = 0; const void *curve = nmo_engine_block_input_data(b,2,&size);
            float value = curve != NULL ? nmo_engine_curve_y(curve,size,1) : 1;
            if (!curve_position(b,entity,path,value)) return NMO_ENGINE_BR_PARAMETERERROR;
            set_out_float(b,0,1); set_out_float(b,1,value); OUT_ON(b,0);
            return NMO_ENGINE_BR_OK;
        }
        float zero = 0, delta = 1.0f/(float)remaining;
        set_local_int(b,0,remaining);
        nmo_engine_block_set_local_value(b,1,&zero,sizeof(zero));
        nmo_engine_block_set_local_value(b,2,&delta,sizeof(delta));
        /* The first frame uses the path's start directly and leaves outputs untouched. */
        if (!curve_position(b,entity,path,0)) return NMO_ENGINE_BR_PARAMETERERROR;
        OUT_ON(b,1);
        return NMO_ENGINE_BR_OK;
    }
    size_t size = 0; const void *curve = nmo_engine_block_input_data(b,2,&size);
    if (curve == NULL) return NMO_ENGINE_BR_PARAMETERERROR;
    int32_t remaining = curve_add(get_local_int(b,0,0),-1);
    float progression = get_local_float(b,1,0)+get_local_float(b,2,0);
    float value = nmo_engine_curve_y(curve,size,progression);
    set_out_float(b,0,progression); set_out_float(b,1,value);
    if (!curve_position(b,entity,path,value)) return NMO_ENGINE_BR_PARAMETERERROR;
    set_local_int(b,0,remaining);
    nmo_engine_block_set_local_value(b,1,&progression,sizeof(progression));
    OUT_ON(b,remaining > 0 ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_curve_follow(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) return NMO_ENGINE_BR_OWNERERROR;
    if (!get_local_int(b,3,0)) return curve_frames(b,entity);
    bool legacy = nmo_engine_block_version(b) < 0x10005 &&
        !nmo_guid_equals(nmo_engine_block_input_parameter_type(b,1),CKPGUID_TIME);
    return curve_time(b,entity,legacy);
}

static int curve_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    eng_node_t *node = &b->engine->nodes.data[b->node];
    eng_param_t *duration = eng_node_param(b->engine,node->first_pin,node->pin_count,1);
    if (duration == NULL) return NMO_ENGINE_BR_PARAMETERERROR;
    bool time = get_local_int(b,3,0) != 0;
    bool old_integer = node->version < 0x10005 && !nmo_guid_equals(duration->type,CKPGUID_TIME);
    duration->type = time ? (old_integer ? CKPGUID_INT : CKPGUID_TIME) : CKPGUID_FLOAT;
    duration->type_desc = nmo_type_registry_find_by_guid(b->engine->types,duration->type);
    if (time && !old_integer && node->version < 0x10005) node->version = 0x10005;
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_curve_follow(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x00ed010b,0x00fd010b),"Curve Follow",bb_curve_follow,curve_callback,NULL};
    return nmo_engine_register_block(engine,&block);
}
