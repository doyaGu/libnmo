/**
 * @file message.c
 * @brief Ports of the Logics/Message building blocks, through the engine's
 *        message manager.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../../engine_block_util.h"

#include "object/nmo_class_ids.h"

/* ---- Logics/Message ---- */

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
    nmo_engine_block_send_message(b, type, nmo_engine_block_get_input_object(b, 1), sender);
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
    if (nmo_engine_block_version(b) > 0x00010000u) {
        class_id = get_in_int(b, 1, NMO_CID_BEOBJECT);
    }
    nmo_engine_block_broadcast_message(b, type, (nmo_class_id_t)class_id, sender);
    return NMO_ENGINE_BR_OK;
}

/*
 * Send Message To Group: Message to the group and every object in it
 * (CKMessageManager::SendMessageGroup), from the target. A parameter
 * error without a group, after Out. The extra inputs the original attaches
 * as message parameters are not kept: the engine's messages carry none.
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
    eng_send(b->engine, &message);
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

static const nmo_engine_block_desc_t eng_logics_message_blocks[] = {
    {NMO_GUID(0x4587ffee, 0x4587ffdd), "Wait Message", bb_wait_message, NULL, NULL},
    {NMO_GUID(0xa20e8d5b, 0xdf002150), "Send Message", bb_send_message, NULL, NULL},
    {NMO_GUID(0x3d6c4ae1, 0x72ae2cd6), "Broadcast Message", bb_broadcast_message, NULL, NULL},
    {NMO_GUID(0x5f906952, 0x6df11649), "Send Message To Group", bb_send_message_to_group, NULL,
     NULL},
    {NMO_GUID(0x1bb23f1d, 0x17ff14b9), "Switch On Message", bb_switch_on_message,
     cb_switch_on_message, NULL},
};

nmo_status_t eng_register_logics_message(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_message_blocks);
}
