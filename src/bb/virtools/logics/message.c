/**
 * @file message.c
 * @brief Ports of the Logics/Message building blocks, through the engine's
 *        message manager.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

#include "object/nmo_class_ids.h"

/* ---- Logics/Message ---- */

/*
 * CKMessage::AddParameter of each input from `first` on, when the message
 * went: the parameter each reads (GetRealSource), its value read when the
 * message is.
 */
static void attach_inputs(blk_t *b, nmo_status_t sent, int first)
{
    if (sent != NMO_OK) {
        return;
    }
    for (int i = first; i < nmo_engine_block_input_parameter_count(b); i++) {
        eng_message_add_param(b->engine, input_source(b, i, false));
    }
}

static int bb_wait_message(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t type = get_in_int(b, 0, -1);
    if (type < 0) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        nmo_engine_block_register_wait(b, type, 0, target);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        nmo_engine_block_unregister_wait(b, type, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_send_message(blk_t *b)
{
    nmo_object_id_t sender = nmo_engine_block_target(b);
    if (sender == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t type = get_in_int(b, 0, -1);
    nmo_status_t sent =
        nmo_engine_block_send_message(b, type, nmo_engine_block_get_input_object(b, 1), sender);
    attach_inputs(b, sent, 2);
    return NMO_ENGINE_BR_OK;
}

static int bb_broadcast_message(blk_t *b)
{
    nmo_object_id_t sender = nmo_engine_block_target(b);
    if (sender == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t type = get_in_int(b, 0, -1);
    int32_t class_id = NMO_CID_BEOBJECT;
    int first = 1;
    if (nmo_engine_block_version(b) > 0x00010000u) {
        class_id = get_in_int(b, 1, NMO_CID_BEOBJECT);
        first = 2;
    }
    nmo_status_t sent =
        nmo_engine_block_broadcast_message(b, type, (nmo_class_id_t)class_id, sender);
    attach_inputs(b, sent, first);
    return NMO_ENGINE_BR_OK;
}

/*
 * Send Message To Group: Message to the group and every object in it
 * (CKMessageManager::SendMessageGroup), from the target. A parameter
 * error without a group, after Out. The inputs from the third on (from the
 * second, the group itself, before version 2) go with the message.
 */
static int bb_send_message_to_group(blk_t *b)
{
    nmo_object_id_t sender = nmo_engine_block_target(b);
    if (sender == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 1);
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_message_t message = {.type = get_in_int(b, 0, -1), .sending = ENG_MESSAGE_GROUP,
                             .sender = sender, .recipient = group, .sent_by = b->node};
    attach_inputs(b, eng_send(b->engine, &message),
                  nmo_engine_block_version(b) < 0x00020000u ? 1 : 2);
    return NMO_ENGINE_BR_OK;
}

static int bb_switch_on_message(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int count = nmo_engine_block_last_frame_message_count(b, owner);
    for (int i = 0; count > 0 && i < nmo_engine_block_input_parameter_count(b); i++) {
        int32_t type = get_in_int(b, i, -1);
        for (int a = 0; a < count; a++) {
            if (nmo_engine_block_last_frame_message_type(b, owner, a) == type) {
                OUT_ON(b, i);
            }
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_switch_on_message(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_waiting_for_messages(b, nmo_engine_block_owner(b), true);
    }
    return NMO_ENGINE_BR_OK;
}

/* Check For Message: how many of the target's messages of the last frame are of the type */
static int bb_check_for_message(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    IN_OFF(b, 0);
    int32_t type = get_in_int(b, 0, -1);
    int count = nmo_engine_block_last_frame_message_count(b, target);
    int32_t received = 0;
    for (int i = 0; i < count; i++) {
        received += nmo_engine_block_last_frame_message_type(b, target, i) == type;
    }
    if (received != 0) {
        OUT_ON(b, 1);
        set_out_int(b, 0, received);
    } else {
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* CheckForMessageCB: the target keeps its messages of the last frame */
static int cb_check_for_message(blk_t *b, uint32_t message)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_waiting_for_messages(b, target, true);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Get Message Data: the sender of the target's message of the type, from the
 * message the local index names on, and its parameters into the outputs past
 * the first when there are as many. The sender is written for each message
 * of the type it passes. The index goes one past where it started, not past
 * the message found, as in Logics.dll. With "Can get several Message per
 * Frame" (local 0) a message found activates "Data Received" (output 1),
 * entered again by "Next Data" (input 1).
 */
static int bb_get_message_data(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 1, 0);
    } else {
        IN_OFF(b, 1);
    }
    int32_t type = get_in_int(b, 0, -1);
    int data_count = nmo_engine_block_output_parameter_count(b) - 1;
    int count = nmo_engine_block_last_frame_message_count(b, target);
    int output = get_local_int(b, 0, 0) != 0 ? 1 : 0;
    int32_t start = get_local_int(b, 1, 0);
    for (int32_t i = start; i < count; i++) {
        const eng_received_t *message = eng_last_frame_message(b->engine, target, i);
        if (message == NULL || message->type != type) {
            continue;
        }
        uint32_t sender = (uint32_t)message->sender;
        nmo_engine_block_set_output_value(b, 0, &sender, sizeof(sender));
        if ((uint32_t)data_count != message->param_count) {
            continue;
        }
        for (int j = 0; j < data_count; j++) {
            eng_param_copy(b->engine, block_output(b, j + 1),
                           eng_received_param(b->engine, message, (uint32_t)j), true);
        }
        set_local_int(b, 1, start + 1);
        OUT_ON(b, output);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_message_blocks[] = {
    {NMO_GUID_INIT(0x61226639, 0x6d390cb7), "Check For Message", bb_check_for_message,
     cb_check_for_message, NULL},
    {NMO_GUID_INIT(0x45875fee, 0x45875fdd), "Get Message Data", bb_get_message_data, NULL, NULL},
    {NMO_GUID_INIT(0x4587ffee, 0x4587ffdd), "Wait Message", bb_wait_message, NULL, NULL},
    {NMO_GUID_INIT(0xa20e8d5b, 0xdf002150), "Send Message", bb_send_message, NULL, NULL},
    {NMO_GUID_INIT(0x3d6c4ae1, 0x72ae2cd6), "Broadcast Message", bb_broadcast_message, NULL, NULL},
    {NMO_GUID_INIT(0x5f906952, 0x6df11649), "Send Message To Group", bb_send_message_to_group, NULL,
     NULL},
    {NMO_GUID_INIT(0x1bb23f1d, 0x17ff14b9), "Switch On Message", bb_switch_on_message,
     cb_switch_on_message, NULL},
};

nmo_status_t eng_register_logics_message(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_message_blocks);
}
