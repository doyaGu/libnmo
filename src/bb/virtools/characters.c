/* Characters.dll: direct pose control, exclusion, animation LOD and floor references. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

#include <string.h>

static int bb_add_animation(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t character = nmo_engine_block_target(b), animation = nmo_engine_block_get_input_object(b,0);
    if (character == 0 || eng_animation_state(b->engine,animation) == NULL) return NMO_ENGINE_BR_OK;
    if (eng_character_add_animation(b->engine,character,animation) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    if (nmo_engine_object_class(b->engine,animation) != NMO_CID_KEYEDANIMATION) return NMO_ENGINE_BR_OK;
    const nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(b->engine,animation));
    size_t parts = eng_character_bodypart_count(b->engine,character), attributed = 0;
    eng_map_t exact = {0};
    int result = NMO_ENGINE_BR_OK;
    for (int pass = 0; pass < 2; pass++) {
        if (pass && attributed == state->animation_count) break;
        for (uint32_t i = 0; i < state->animation_count; i++) {
            nmo_object_id_t channel = eng_object_ref(b->engine,animation,&state->animation_ids[i]);
            if (nmo_engine_object_class(b->engine,channel) != NMO_CID_OBJECTANIMATION ||
                (pass && eng_map_get(&exact,channel) >= 0)) continue;
            const char *name = nmo_engine_object_name(b->engine,channel);
            if (name == NULL) continue;
            size_t length = strlen(name);
            nmo_object_id_t previous = eng_object_animation_entity(b->engine,channel);
            for (size_t k = 0; k < parts; k++) {
                nmo_object_id_t part = eng_character_bodypart(b->engine,character,k);
                const char *part_name = nmo_engine_object_name(b->engine,part);
                if (part == 0 || part == previous || part_name == NULL) continue;
                size_t part_length = strlen(part_name);
                bool match = !pass ? part_length == length && strcmp(part_name,name) == 0 :
                    part_length >= 3 && part_length-3 == length && strncmp(part_name,name,length) == 0 &&
                    part_name[length] >= '0' && part_name[length] <= '9' &&
                    part_name[length+1] >= '0' && part_name[length+1] <= '9';
                /* The historical suffix test checks its first two characters;
                 * the third character is intentionally unconstrained. */
                if (!match) continue;
                if ((!pass && !eng_map_put(&exact,channel,1)) ||
                    !eng_object_animation_set_entity(b->engine,channel,part)) { result = NMO_ENGINE_BR_GENERICERROR; goto done; }
                if (!pass) attributed++;
            }
        }
    }
done:
    free(exact.slots);
    return result;
}

static int bb_set_animation_frame(blk_t *b)
{
    if (nmo_engine_block_target(b) == 0) return NMO_ENGINE_BR_OWNERERROR;
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b,1);
    if (eng_animation_state(b->engine,animation) != NULL) eng_animation_set_frame(b->engine,animation,get_in_float(b,0,0));
    return NMO_ENGINE_BR_OK;
}

static int bb_set_blended_animation_factor(blk_t *b)
{
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b,0);
    if (eng_animation_state(b->engine,animation) != NULL &&
        !eng_animation_set_merge_factor(b->engine,animation,get_in_float(b,1,.5f)))
        return NMO_ENGINE_BR_GENERICERROR;
    OUT_ON(b,0);
    return NMO_ENGINE_BR_OK;
}

static int bb_create_blended_animation(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t first = nmo_engine_block_get_input_object(b,0), second = nmo_engine_block_get_input_object(b,1);
    nmo_object_id_t result = (nmo_object_id_t)get_out_int(b,0), character = nmo_engine_block_target(b);
    if (eng_animation_state(b->engine,result) == NULL) result = 0;
    if (eng_animation_state(b->engine,first) != NULL && eng_animation_state(b->engine,second) != NULL)
        result = eng_keyed_animation_merge(b->engine,first,second,get_local_int(b,0,1) != 0);
    if (result != 0 && eng_entity_index(b->engine,character) >= 0 && get_local_int(b,1,1) != 0 &&
        eng_character_add_animation(b->engine,character,result) != NMO_OK) return NMO_ENGINE_BR_GENERICERROR;
    return nmo_engine_block_set_output_value(b,0,&result,sizeof(result)) == NMO_OK ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
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

static int bb_get_nearest_object(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    int32_t character = target_entity(b);
    if (character < 0) return NMO_ENGINE_BR_OWNERERROR;
    eng_vector_t origin = eng_entity_position(b->engine,character,-1);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b,0), nearest = 0;
    float minimum = 100000000.0f;
    int32_t count = eng_map_get(&b->engine->destroyed,group) < 0 ? nmo_engine_group_count(b->engine,group) : 0;
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t id = nmo_engine_group_object(b->engine,group,i);
        int32_t entity = eng_entity_index(b->engine,id);
        if (entity < 0) continue;
        eng_vector_t position = eng_entity_position(b->engine,entity,-1);
        eng_vector_t delta = {position.x-origin.x,position.y-origin.y,position.z-origin.z};
        float distance = eng_vector_magnitude(&delta);
        if (distance < minimum) { minimum = distance; nearest = id; }
    }
    return nmo_engine_block_set_output_value(b,0,&nearest,sizeof(nearest)) == NMO_OK ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
}

static const nmo_engine_block_desc_t character_blocks[] = {
    {NMO_GUID_INIT(0x6a794228,0x77197f5e),"Add Animation",bb_add_animation,NULL,NULL},
    {NMO_GUID_INIT(0x7afc20a3,0x6d367a99),"Set Blended Animation Factor",bb_set_blended_animation_factor,NULL,NULL},
    {NMO_GUID_INIT(0x56864d6d,0x159b5d1e),"Create Blended Animation 2",bb_create_blended_animation,NULL,NULL},
    {NMO_GUID_INIT(0x0e579636,0xa3b6e7f0),"Set Animation Frame",bb_set_animation_frame,NULL,NULL},
    {NMO_GUID_INIT(0x120127aa,0x960added),"Set Animation Step",bb_set_animation_step,NULL,NULL},
    {NMO_GUID_INIT(0x55f045c7,0x6cb3417b),"Set Bodypart Animation Frame",bb_set_bodypart_animation_frame,NULL,NULL},
    {NMO_GUID_INIT(0x4d3f515b,0x450c20c6),"Exclude From Animation",bb_exclude_from_animation,NULL,NULL},
    {NMO_GUID_INIT(0x200d6501,0x4cf61e52),"LOD Character Animation",bb_animation_lod,NULL,NULL},
    {NMO_GUID_INIT(0x0ad75ccb,0x0cd1369a),"Set Floor Reference Object",bb_set_floor_reference,NULL,NULL},
    {NMO_GUID_INIT(0x4321abcd,0x0eeaabdd),"Get Nearest Object",bb_get_nearest_object,NULL,NULL},
};

nmo_status_t eng_register_characters(nmo_engine_t *engine)
{
    nmo_status_t status = ENG_REGISTER_BLOCKS(engine,character_blocks);
    if (status == NMO_OK) status = eng_register_character_goto(engine);
    if (status == NMO_OK) status = eng_register_character_curve(engine);
    if (status == NMO_OK) status = eng_register_character_controller(engine);
    if (status == NMO_OK) status = eng_register_ik_position(engine);
    return status == NMO_OK ? eng_register_unlimited_controller(engine) : status;
}
