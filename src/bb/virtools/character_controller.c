/* The original four- and seven-animation Joy message receivers. */
#include "virtools.h"
#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

typedef struct controller_state { bool listening; } controller_state_t;

static int controller_callback_impl(blk_t *b, uint32_t message, bool enhanced)
{
    if (message != NMO_ENGINE_MSG_LOAD && message != NMO_ENGINE_MSG_ACTIVATESCRIPT) return NMO_ENGINE_BR_OK;
    if (message == NMO_ENGINE_MSG_LOAD) {
        const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"};
        int32_t messages[11] = {0}; size_t count = enhanced ? 10 : 6;
        for (size_t i = 0; i < count; i++) {
            messages[i] = eng_message_type_add(b->engine,names[i]);
            if (messages[i] < 0) return NMO_ENGINE_BR_GENERICERROR;
        }
        if (nmo_engine_block_set_local_value(b,0,messages,(count+1)*sizeof(messages[0])) != NMO_OK)
            return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (eng_entity_index(b->engine,owner) < 0 || nmo_engine_object_class(b->engine,owner) != NMO_CID_CHARACTER)
        return NMO_ENGINE_BR_OWNERERROR;
    /* Copies are activated without LOAD; activation supplies the native ATTACH registration. */
    controller_state_t *state = nmo_engine_block_state(b,sizeof(*state));
    if (state == NULL) return NMO_ENGINE_BR_GENERICERROR;
    if (!state->listening) {
        nmo_engine_block_set_waiting_for_messages(b,owner,true);
        state->listening = true;
    }
    return NMO_ENGINE_BR_OK;
}

static int controller_execute_impl(blk_t *b, bool enhanced)
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
    nmo_object_id_t animations[7]; int animation_count = enhanced ? 7 : 4;
    for (int i = 0; i < animation_count; i++) {
        nmo_object_id_t id = nmo_engine_block_get_input_object(b,i);
        animations[i] = eng_animation_state(b->engine,id) != NULL ? id : 0;
    }
    for (int i = enhanced ? 5 : 3; i < animation_count; i++) {
        const nmo_animation_state_t *protected = eng_animation_playback_state(b->engine,animations[i]);
        if (protected != NULL && !eng_animation_set_playback(b->engine,animations[i],CKANIMATION_CANBEBREAK,0,protected->frame_rate))
            return NMO_ENGINE_BR_GENERICERROR;
    }
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    const eng_param_t *buffer = eng_node_param(b->engine,node->first_local,node->local_count,0);
    int32_t messages[11]; int message_count = enhanced ? 10 : 6;
    size_t bytes = (size_t)(message_count+1)*sizeof(messages[0]);
    if (buffer == NULL || buffer->size < bytes) return NMO_ENGINE_BR_PARAMETERERROR;
    memcpy(messages,buffer->data,bytes);
    bool received[10] = {false};
    int count = nmo_engine_block_last_frame_message_count(b,owner);
    for (int i = 0; i < count; i++) {
        int32_t type = nmo_engine_block_last_frame_message_type(b,owner,i);
        for (int j = 0; j < message_count; j++) if (type == messages[j]) { received[j] = true; break; }
    }
    int selected;
    if (enhanced) selected = received[4] ? 3 : received[5] ? 4 : received[6] ? 5 : received[2] ? 1 : received[3] ? 2 : received[7] ? 6 : 0;
    else selected = received[4] ? 3 : received[2] ? 1 : received[3] ? 2 : 0;
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

static int controller_callback(blk_t *b, uint32_t message) { return controller_callback_impl(b,message,false); }
static int enhanced_callback(blk_t *b, uint32_t message) { return controller_callback_impl(b,message,true); }
static int controller_execute(blk_t *b) { return controller_execute_impl(b,false); }
static int enhanced_execute(blk_t *b) { return controller_execute_impl(b,true); }

nmo_status_t eng_register_character_controller(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t blocks[] = {
        {NMO_GUID_INIT(0x12874356,0x12874356),"Character Controller",controller_execute,controller_callback,NULL},
        {NMO_GUID_INIT(0x44433224,0x12874356),"Enhanced Character Controller",enhanced_execute,enhanced_callback,NULL},
    };
    return ENG_REGISTER_BLOCKS(engine,blocks);
}
