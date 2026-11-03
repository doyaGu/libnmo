/* Characters.dll: direct pose control, exclusion, animation LOD and floor references. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

static int bb_set_animation_frame(blk_t *b)
{
    if (nmo_engine_block_target(b) == 0) return NMO_ENGINE_BR_OWNERERROR;
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b,1);
    if (eng_animation_state(b->engine,animation) != NULL) eng_animation_set_frame(b->engine,animation,get_in_float(b,0,0));
    return NMO_ENGINE_BR_OK;
}

static int bb_set_animation_step(blk_t *b)
{
    if (nmo_engine_block_target(b) == 0) return NMO_ENGINE_BR_OWNERERROR;
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b,1);
    if (eng_animation_state(b->engine,animation) != NULL) eng_animation_set_step(b->engine,animation,get_in_float(b,0,0));
    return NMO_ENGINE_BR_OK;
}

static int bb_set_bodypart_animation_frame(blk_t *b)
{
    nmo_object_id_t body = nmo_engine_block_target(b);
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b,1);
    if (eng_animation_state(b->engine,animation) == NULL ||
        nmo_engine_object_class(b->engine,animation) != NMO_CID_KEYEDANIMATION) return NMO_ENGINE_BR_PARAMETERERROR;
    nmo_object_id_t part = eng_keyed_animation_part(b->engine,animation,body);
    if (part != 0) eng_object_animation_set_frame(b->engine,part,get_in_float(b,0,0),true);
    return NMO_ENGINE_BR_OK;
}

static int bb_exclude_from_animation(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) return NMO_ENGINE_BR_OWNERERROR;
    IN_OFF(b,0); OUT_ON(b,0);
    eng_entity_ignore_animations(b->engine,entity,get_in_int(b,0,1) != 0,get_in_int(b,1,1) != 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_animation_lod(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t character = nmo_engine_block_target(b);
    int32_t entity = eng_entity_index(b->engine,character);
    if (entity < 0) return NMO_ENGINE_BR_OWNERERROR;
    float minimum_distance = get_in_float(b,0,100), maximum_distance = get_in_float(b,1,1000);
    float minimum = get_in_float(b,2,1);
    int32_t camera = eng_entity_index(b->engine,b->engine->render.camera);
    eng_vector_t position = eng_entity_position(b->engine,entity,camera);
    float lod = 1-(eng_vector_magnitude(&position)-minimum_distance)/(maximum_distance-minimum_distance);
    if (lod < minimum) lod = minimum;
    else if (lod > 1) lod = 1;
    eng_character_set_animation_lod(b->engine,character,lod);
    return NMO_ENGINE_BR_OK;
}

static int bb_set_floor_reference(blk_t *b)
{
    nmo_object_id_t character = nmo_engine_block_target(b);
    if (character == 0) return NMO_ENGINE_BR_OWNERERROR;
    nmo_object_id_t floor = nmo_engine_block_get_input_object(b,0);
    if (floor != 0) eng_character_set_floor_reference(b->engine,character,floor);
    IN_OFF(b,0); OUT_ON(b,0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t character_blocks[] = {
    {NMO_GUID_INIT(0x0e579636,0xa3b6e7f0),"Set Animation Frame",bb_set_animation_frame,NULL,NULL},
    {NMO_GUID_INIT(0x120127aa,0x960added),"Set Animation Step",bb_set_animation_step,NULL,NULL},
    {NMO_GUID_INIT(0x55f045c7,0x6cb3417b),"Set Bodypart Animation Frame",bb_set_bodypart_animation_frame,NULL,NULL},
    {NMO_GUID_INIT(0x4d3f515b,0x450c20c6),"Exclude From Animation",bb_exclude_from_animation,NULL,NULL},
    {NMO_GUID_INIT(0x200d6501,0x4cf61e52),"LOD Character Animation",bb_animation_lod,NULL,NULL},
    {NMO_GUID_INIT(0x0ad75ccb,0x0cd1369a),"Set Floor Reference Object",bb_set_floor_reference,NULL,NULL},
};

nmo_status_t eng_register_characters(nmo_engine_t *engine)
{
    nmo_status_t status = ENG_REGISTER_BLOCKS(engine,character_blocks);
    return status == NMO_OK ? eng_register_unlimited_controller(engine) : status;
}
