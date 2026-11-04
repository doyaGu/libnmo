/* Character Curve Follow advances from root motion and asks a controller to walk. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

static int curve_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD && message != NMO_ENGINE_MSG_RESET) return NMO_ENGINE_BR_OK;
    int32_t type = eng_message_type_add(b->engine,"Joy_Up");
    return type >= 0 && nmo_engine_block_set_local_value(b,0,&type,sizeof(type)) == NMO_OK ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
}

static void curve_orientation(blk_t *b, int32_t actor, eng_vector_t direction, const eng_vector_t *axis)
{
    const eng_vector_t up = {0,1,0};
    direction.y = 0; eng_vector_normalize(&direction);
    /* The original tests these independently, including compound vectors. */
    if (axis->z == 1) eng_entity_set_orientation(b->engine,actor,&direction,&up,NULL,-1,false);
    if (axis->z == -1) {
        direction = (eng_vector_t){-direction.x,-direction.y,-direction.z};
        eng_entity_set_orientation(b->engine,actor,&direction,&up,NULL,-1,false);
    }
    if (axis->x == 1) {
        eng_vector_t right = eng_vector_cross(&direction,&up);
        eng_entity_set_orientation(b->engine,actor,&right,&up,NULL,-1,false);
    }
    if (axis->x == -1) {
        eng_vector_t right = eng_vector_cross(&up,&direction);
        eng_entity_set_orientation(b->engine,actor,&right,&up,NULL,-1,false);
    }
}

static int bb_character_curve(blk_t *b)
{
    nmo_object_id_t owner = nmo_engine_block_target(b);
    int32_t actor = eng_entity_index(b->engine,owner);
    if (actor < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER) return NMO_ENGINE_BR_OWNERERROR;
    bool initial = IN_ACTIVE(b,0);
    if (!initial && !IN_ACTIVE(b,1)) return NMO_ENGINE_BR_OK;
    IN_OFF(b,initial ? 0 : 1);
    nmo_object_id_t curve = nmo_engine_block_get_input_object(b,0);
    if (eng_entity_index(b->engine,curve) < 0 || !eng_is_child_class(b->engine,eng_object_class(b->engine,curve),NMO_CID_CURVE)) {
        if (initial) OUT_ON(b,0);
        return NMO_ENGINE_BR_OK;
    }
    float length;
    if (!eng_curve_length(b->engine,curve,&length)) return NMO_ENGINE_BR_PARAMETERERROR;
    eng_vector_t axis = get_in_vector(b,2,1,0,0), previous, position, ahead;
    float travelled, step;
    if (initial) {
        step = get_in_float(b,1,0); travelled = length*step;
        if (nmo_engine_block_set_local_value(b,2,&travelled,sizeof(travelled)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
        set_out_float(b,0,step);
    } else {
        if (nmo_engine_block_get_local_value(b,1,&previous,sizeof(previous)) != NMO_OK) return NMO_ENGINE_BR_PARAMETERERROR;
        travelled = get_local_float(b,2,0);
    }
    if (!eng_character_align_root(b->engine,owner)) return NMO_ENGINE_BR_GENERICERROR;
    position = eng_entity_position(b->engine,actor,-1);
    float height = position.y;
    if (!initial) {
        eng_vector_t displacement = {position.x-previous.x,0,position.z-previous.z};
        travelled += eng_vector_magnitude(&displacement); step = travelled/length;
        if (nmo_engine_block_set_local_value(b,2,&travelled,sizeof(travelled)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
        set_out_float(b,0,step);
    }
    if (!eng_curve_position(b->engine,curve,step,&position) || !eng_curve_position(b->engine,curve,step+.01f,&ahead))
        return NMO_ENGINE_BR_PARAMETERERROR;
    eng_vector_t direction = {ahead.x-position.x,ahead.y-position.y,ahead.z-position.z};
    /* Initial Pos keeps character height; Loop In stores the sampled curve Y. */
    if (initial) position.y = height;
    if (nmo_engine_block_set_local_value(b,1,&position,sizeof(position)) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    if (!initial && !eng_character_align_root(b->engine,owner)) return NMO_ENGINE_BR_GENERICERROR;
    position.y = height;
    if (initial || travelled < length) {
        eng_entity_set_position(b->engine,actor,&position,-1,false);
        curve_orientation(b,actor,direction,&axis);
    }
    if (nmo_engine_block_send_message(b,get_local_int(b,0,0),owner,owner) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    if (!initial && travelled >= length) { OUT_ON(b,0); set_out_float(b,0,1); }
    else OUT_ON(b,1);
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_character_curve(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x4212cabd,0xeafbcda6),"Character Curve Follow",bb_character_curve,curve_callback,NULL};
    nmo_status_t status = nmo_engine_register_block(engine,&block);
    return status == NMO_OK ? eng_register_enhanced_character_curve(engine) : status;
}
