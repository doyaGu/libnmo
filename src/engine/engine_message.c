/**
 * @file engine_message.c
 * @brief Messages: sending, waiting, and the delivery at the end of a frame.
 *
 * Follows CKMessageManager: SendMessage* queue a message for this frame,
 * RegisterWait/UnRegisterWait keep the waiting list, and PostProcess
 * delivers the frame's messages after the behaviors ran.
 */

#include "engine_internal.h"

#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_type_query.h"
#include "../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Message types
 * ============================================================================ */

nmo_status_t eng_load_message_names(nmo_engine_t *engine)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096u);
    if (arena == NULL) {
        return NMO_ERR_NOMEM;
    }
    const char **names = NULL;
    uint32_t count = 0;
    nmo_status_t status = NMO_OK;
    /* A file without a message manager declares no message types */
    if (nmo_workspace_internal_message_names(engine->workspace, arena, &names, &count) == NMO_OK) {
        for (uint32_t i = 0; status == NMO_OK && i < count; i++) {
            const char *name = names[i] != NULL ? names[i] : "";
            size_t length = strlen(name);
            char **slot = NULL;
            char *copy = (char *)malloc(length + 1u);
            if (copy == NULL || !ENG_VEC_PUSH(engine->message_names, slot)) {
                free(copy);
                status = NMO_ERR_NOMEM;
                break;
            }
            memcpy(copy, name, length + 1u);
            *slot = copy;
        }
    }
    nmo_arena_destroy(arena);
    return status;
}

void eng_free_messages(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->message_names.count; i++) {
        free(engine->message_names.data[i]);
    }
    free(engine->message_names.data);
    free(engine->messages.data);
    free(engine->waits.data);
    free(engine->received.data);
}

int32_t nmo_engine_message_type_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->message_names.count : 0;
}

const char *nmo_engine_message_type_name(const nmo_engine_t *engine, int32_t type)
{
    if (engine == NULL || type < 0 || (size_t)type >= engine->message_names.count) {
        return NULL;
    }
    return engine->message_names.data[type];
}

int32_t nmo_engine_message_type(const nmo_engine_t *engine, const char *name)
{
    if (engine == NULL || name == NULL) {
        return -1;
    }
    for (size_t i = 0; i < engine->message_names.count; i++) {
        if (strcmp(engine->message_names.data[i], name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ============================================================================
 * Sending and waiting
 * ============================================================================ */

nmo_status_t eng_send(nmo_engine_t *engine, const eng_message_t *message)
{
    if (message->type < 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_message_t *slot = NULL;
    if (!ENG_VEC_PUSH(engine->messages, slot)) {
        return NMO_ERR_NOMEM;
    }
    *slot = *message;
    eng_emit(engine, NMO_ENGINE_EVENT_MESSAGE_SENT, message->sent_by,
             message->sending == ENG_MESSAGE_BROADCAST ? 0 : message->recipient, message->type);
    return NMO_OK;
}

nmo_status_t nmo_engine_send_message(nmo_engine_t *engine, int32_t type,
                                     nmo_object_id_t recipient, nmo_object_id_t sender)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_message_t message = {.type = type, .sending = ENG_MESSAGE_SINGLE, .sender = sender,
                             .recipient = recipient, .sent_by = -1};
    return eng_send(engine, &message);
}

nmo_status_t nmo_engine_broadcast_message(nmo_engine_t *engine, int32_t type,
                                          nmo_class_id_t class_id, nmo_object_id_t sender)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_message_t message = {.type = type, .sending = ENG_MESSAGE_BROADCAST, .sender = sender,
                             .broadcast_cid = class_id, .sent_by = -1};
    return eng_send(engine, &message);
}

nmo_status_t eng_register_wait(nmo_engine_t *engine, int32_t type, int32_t node, int output_pos,
                               nmo_object_id_t object)
{
    if (type < 0) {
        return NMO_ERR_INVALID_ARGUMENT; /* CKERR_INVALIDMESSAGE */
    }
    eng_node_t *n = &engine->nodes.data[node];
    eng_io_t *io = eng_node_io(engine, n, true, output_pos);
    int32_t output = io != NULL ? (int32_t)(io - engine->ios.data) : -1;
    n->flags |= CKBEHAVIOR_WAITSFORMESSAGE;
    for (size_t i = 0; i < engine->waits.count; i++) {
        const eng_wait_t *w = &engine->waits.data[i];
        if (w->type == type && w->node == node && w->output == output) {
            return NMO_ERR_ALREADY_EXISTS;
        }
    }
    eng_wait_t *slot = NULL;
    if (!ENG_VEC_PUSH(engine->waits, slot)) {
        return NMO_ERR_NOMEM;
    }
    *slot = (eng_wait_t){.type = type, .node = node, .output = output, .object = object};
    return NMO_OK;
}

static void eng_remove_wait(nmo_engine_t *engine, size_t index)
{
    memmove(&engine->waits.data[index], &engine->waits.data[index + 1u],
            (engine->waits.count - index - 1u) * sizeof(eng_wait_t));
    engine->waits.count--;
}

void eng_unregister_wait(nmo_engine_t *engine, int32_t type, int32_t node, int output_pos)
{
    eng_node_t *n = &engine->nodes.data[node];
    eng_io_t *io = output_pos != -1 ? eng_node_io(engine, n, true, output_pos) : NULL;
    int32_t output = io != NULL ? (int32_t)(io - engine->ios.data) : -1;
    bool removed = false;
    for (size_t i = 0; i < engine->waits.count;) {
        const eng_wait_t *w = &engine->waits.data[i];
        if (w->type == type && w->node == node && (output < 0 || w->output == output)) {
            eng_remove_wait(engine, i);
            removed = true;
            if (output >= 0) {
                break;
            }
        } else {
            i++;
        }
    }
    if (!removed) {
        return;
    }
    for (size_t i = 0; i < engine->waits.count; i++) {
        if (engine->waits.data[i].node == node) {
            return;
        }
    }
    n->flags &= ~(uint32_t)CKBEHAVIOR_WAITSFORMESSAGE;
}

void eng_reset_messages(nmo_engine_t *engine)
{
    engine->messages.count = 0;
    engine->waits.count = 0;
    engine->received.count = 0;
}

/* ============================================================================
 * Delivery
 * ============================================================================ */

static nmo_class_id_t eng_object_class(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    nmo_object_t *object = repo != NULL && id != 0 ? nmo_object_repository_find_by_id(repo, id)
                                                   : NULL;
    return object != NULL ? nmo_object_get_class_id(object) : 0;
}

static bool eng_is_child_class(nmo_engine_t *engine, nmo_class_id_t class_id, nmo_class_id_t base)
{
    return class_id != 0 &&
           (class_id == base || nmo_type_query_class_is_derived_from(engine->types, class_id, base));
}

static void eng_add_received(nmo_engine_t *engine, nmo_object_id_t object,
                             const eng_message_t *message, uint32_t position)
{
    for (size_t i = 0; i < engine->received.count; i++) {
        const eng_received_t *r = &engine->received.data[i];
        if (r->object == object && r->message == position) {
            return;
        }
    }
    eng_received_t *slot = NULL;
    if (ENG_VEC_PUSH(engine->received, slot)) {
        *slot = (eng_received_t){.object = object, .type = message->type,
                                 .sender = message->sender, .message = position};
    }
}

static bool eng_object_waits_for_messages(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t object = eng_map_get(&engine->object_map, id);
    return object >= 0 && engine->objects.data[object].waiting > 0;
}

/* The members of group `id`; NULL when it is not a group. */
static const nmo_array_t *eng_group_members(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (eng_object_class(engine, id) != NMO_CID_GROUP) {
        return NULL;
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    const nmo_group_state_t *group =
        (const nmo_group_state_t *)nmo_object_get_state(nmo_object_repository_find_by_id(repo, id));
    return group != NULL ? &group->object_ids : NULL;
}

/* CKMessageManager::AddMessageToObject */
static void eng_deliver_to(nmo_engine_t *engine, nmo_object_id_t id, const eng_message_t *message,
                           uint32_t position, bool recurse, int depth)
{
    if (id == 0 || depth > 32) {
        return;
    }
    if (eng_object_waits_for_messages(engine, id)) {
        eng_add_received(engine, id, message, position);
    }
    if (!recurse || message->sending != ENG_MESSAGE_GROUP) {
        return;
    }
    const nmo_array_t *members = eng_group_members(engine, id);
    for (size_t i = 0; members != NULL && i < members->count; i++) {
        nmo_object_id_t member = nmo_ref_runtime_id(&((const nmo_ref_t *)members->data)[i]);
        eng_deliver_to(engine, member, message, position, true, depth + 1);
    }
}

static bool eng_group_has(nmo_engine_t *engine, nmo_object_id_t group, nmo_object_id_t id)
{
    const nmo_array_t *members = eng_group_members(engine, group);
    for (size_t i = 0; members != NULL && i < members->count; i++) {
        if (nmo_ref_runtime_id(&((const nmo_ref_t *)members->data)[i]) == id) {
            return true;
        }
    }
    return false;
}

static bool eng_wait_matches(nmo_engine_t *engine, const eng_wait_t *wait,
                             const eng_message_t *message)
{
    switch (message->sending) {
    case ENG_MESSAGE_BROADCAST:
        return eng_is_child_class(engine, eng_object_class(engine, wait->object),
                                  message->broadcast_cid);
    case ENG_MESSAGE_SINGLE:
        return wait->object == message->recipient;
    case ENG_MESSAGE_GROUP:
        return wait->object == message->recipient ||
               eng_group_has(engine, message->recipient, wait->object);
    default:
        return false;
    }
}

void eng_dispatch_messages(nmo_engine_t *engine)
{
    /* Last frame's messages are gone (RemoveAllLastFrameMessage) */
    engine->received.count = 0;

    for (size_t m = 0; m < engine->messages.count; m++) {
        eng_message_t message = engine->messages.data[m];
        uint32_t position = (uint32_t)m;
        if (message.sending == ENG_MESSAGE_BROADCAST) {
            for (size_t o = 0; o < engine->objects.count; o++) {
                const eng_object_t *object = &engine->objects.data[o];
                if (object->waiting > 0 &&
                    eng_is_child_class(engine, eng_object_class(engine, object->id),
                                       message.broadcast_cid)) {
                    eng_add_received(engine, object->id, &message, position);
                }
            }
        } else {
            eng_deliver_to(engine, message.recipient, &message, position, true, 0);
        }

        for (size_t i = 0; i < engine->waits.count;) {
            eng_wait_t wait = engine->waits.data[i];
            if (wait.type != message.type || wait.object == 0 ||
                !eng_wait_matches(engine, &wait, &message)) {
                i++;
                continue;
            }
            eng_add_received(engine, wait.object, &message, position);
            eng_node_t *node = &engine->nodes.data[wait.node];
            if (wait.output >= 0 && (node->flags & CKBEHAVIOR_WAITSFORMESSAGE) != 0) {
                engine->ios.data[wait.output].active = true;
            }
            node->flags &= ~(uint32_t)CKBEHAVIOR_WAITSFORMESSAGE;
            eng_remove_wait(engine, i);
            eng_emit(engine, NMO_ENGINE_EVENT_MESSAGE_RECEIVED, wait.node, wait.object,
                     message.type);
        }
    }
    engine->messages.count = 0;
}
