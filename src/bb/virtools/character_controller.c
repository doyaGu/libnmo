/* Character Controller: the original four-animation Joy message receiver. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

static int controller_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_LOAD) return NMO_ENGINE_BR_OK;
    const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2"};
    int32_t messages[7] = {0};
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        messages[i] = eng_message_type_add(b->engine,names[i]);
        if (messages[i] < 0) return NMO_ENGINE_BR_GENERICERROR;
    }
    if (nmo_engine_block_set_local_value(b,0,messages,sizeof(messages)) != NMO_OK)
        return NMO_ENGINE_BR_PARAMETERERROR;
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (eng_entity_index(b->engine,owner) < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER)
        return NMO_ENGINE_BR_OWNERERROR;
    nmo_engine_block_set_waiting_for_messages(b,owner,true);
    return NMO_ENGINE_BR_OK;
}

static int controller_execute(blk_t *b)
{
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t entity = eng_entity_index(b->engine,owner);
    if (entity < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER)
        return NMO_ENGINE_BR_OWNERERROR;
    if (IN_ACTIVE(b,1)) {
        IN_OFF(b,1);
        eng_character_stop_automatic(b->engine,owner);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b,0);
    if (!eng_character_set_automatic(b->engine,owner,true)) return NMO_ENGINE_BR_GENERICERROR;
    nmo_object_id_t animations[4];
    for (int i = 0; i < 4; i++) {
        nmo_object_id_t id = nmo_engine_block_get_input_object(b,i);
        animations[i] = eng_animation_state(b->engine,id) != NULL ? id : 0;
    }
    const nmo_animation_state_t *run = eng_animation_playback_state(b->engine,animations[3]);
    if (run != NULL && !eng_animation_set_playback(b->engine,animations[3],CKANIMATION_CANBEBREAK,0,run->frame_rate))
        return NMO_ENGINE_BR_GENERICERROR;
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    const eng_param_t *buffer = eng_node_param(b->engine,node->first_local,node->local_count,0);
    int32_t messages[7];
    if (buffer == NULL || buffer->size < sizeof(messages)) return NMO_ENGINE_BR_PARAMETERERROR;
    memcpy(messages,buffer->data,sizeof(messages));
    bool received[6] = {false};
    int count = nmo_engine_block_last_frame_message_count(b,owner);
    for (int i = 0; i < count; i++) {
        int32_t type = nmo_engine_block_last_frame_message_type(b,owner,i);
        for (int j = 0; j < 6; j++) if (type == messages[j]) { received[j] = true; break; }
    }
    int selected = received[4] ? 3 : received[2] ? 1 : received[3] ? 2 : 0;
    /* A null animation is also a native stop request; its error is ignored. */
    eng_character_set_next_animation_warp(b->engine,owner,animations[selected],
        CK_TRANSITION_WARPBEST | CK_TRANSITION_LOOPIFEQUAL | CK_TRANSITION_USEVELOCITY,5);
    const nmo_animation_state_t *active = eng_animation_playback_state(b->engine,eng_character_active_animation(b->engine,owner));
    float angle = .06f*(nmo_engine_block_delta_time(b)*(active != NULL ? active->frame_rate : 30))*.001f;
    const eng_vector_t up = {0,1,0};
    if (isfinite(angle)) {
        if (received[1]) eng_entity_rotate(b->engine,entity,&up,-angle,-1,false);
        else if (received[0]) eng_entity_rotate(b->engine,entity,&up,angle,-1,false);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

nmo_status_t eng_register_character_controller(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {
        NMO_GUID_INIT(0x12874356,0x12874356),"Character Controller",controller_execute,controller_callback,NULL
    };
    return nmo_engine_register_block(engine,&block);
}
