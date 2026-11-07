/**
 * @file engine_message.h
 * @brief The message manager: messages sent in a frame, the behaviors
 *        waiting for them, and what objects received.
 */

#ifndef NMO_ENGINE_MESSAGE_H
#define NMO_ENGINE_MESSAGE_H

#include "engine_base.h"

struct eng_param;

/* CK_MESSAGE_SENDINGTYPE */
#define ENG_MESSAGE_BROADCAST 1
#define ENG_MESSAGE_SINGLE 2
#define ENG_MESSAGE_GROUP 3

typedef struct eng_message {
    int32_t type;
    int32_t sending;              /* ENG_MESSAGE_* */
    nmo_object_id_t sender;
    nmo_object_id_t recipient;    /* single and group messages */
    nmo_class_id_t broadcast_cid; /* broadcast messages */
    int32_t sent_by;              /* the node that sent it, -1 for the host */
    /* The parameters it carries (CKMessage::AddParameter): engine parameter indexes in
       messages.params, the sender's sources, read when the message is read */
    uint32_t first_param, param_count;
} eng_message_t;

/* A behavior waiting for a message (CKWaitingObject). */
typedef struct eng_wait {
    int32_t type;
    int32_t node;
    int32_t output;            /* io activated on arrival; -1 for none */
    nmo_object_id_t object;    /* the object the message has to reach */
} eng_wait_t;

/* A message an object received in the last dispatch (CKBeObject last frame messages). */
typedef struct eng_received {
    nmo_object_id_t object;
    int32_t type;
    nmo_object_id_t sender;
    uint32_t message; /* position among the messages delivered, to add a message once */
    uint32_t first_param, param_count; /* its parameters, in messages.received_params */
} eng_received_t;

/* CKMessageManager */
typedef struct eng_messages {
    ENG_VEC(eng_message_t) sent; /* this frame */
    ENG_VEC(eng_wait_t) waits;
    ENG_VEC(eng_received_t) received;
    ENG_VEC(char *) names;       /* the message types, by type */
    ENG_VEC(int32_t) params;     /* the sent messages' parameters */
    ENG_VEC(int32_t) received_params; /* the received messages' parameters */
} eng_messages_t;

void eng_free_messages(nmo_engine_t *engine);
nmo_status_t eng_send(nmo_engine_t *engine, const eng_message_t *message);
/* CKMessage::AddParameter on the message sent last: it carries `param` (an engine parameter) */
nmo_status_t eng_message_add_param(nmo_engine_t *engine, const struct eng_param *param);
/* CKBeObject::GetLastFrameMessage: the `pos`th message `object` received; NULL when none */
const eng_received_t *eng_last_frame_message(const nmo_engine_t *engine, nmo_object_id_t object,
                                             int pos);
/* CKMessage::GetParameter of a received message; NULL past its parameters */
struct eng_param *eng_received_param(nmo_engine_t *engine, const eng_received_t *message,
                                    uint32_t pos);
nmo_status_t eng_register_wait(nmo_engine_t *engine, int32_t type, int32_t node, int output_pos,
                               nmo_object_id_t object);
void eng_unregister_wait(nmo_engine_t *engine, int32_t type, int32_t node, int output_pos);
/* CKMessageManager::PostProcess: deliver this frame's messages. */
void eng_dispatch_messages(nmo_engine_t *engine);
/* CKMessageManager::OnCKReset: no waits and no messages. */
void eng_reset_messages(nmo_engine_t *engine);
/* The engine message type named `name`, made when there is none (AddMessageType). */
int32_t eng_message_type_add(nmo_engine_t *engine, const char *name);
/* The class of object `id` in the document; 0 when there is none. */
nmo_class_id_t eng_object_class(nmo_engine_t *engine, nmo_object_id_t id);
/* CKIsChildClassOf for class ids. */
bool eng_is_child_class(nmo_engine_t *engine, nmo_class_id_t class_id, nmo_class_id_t base);

#endif /* NMO_ENGINE_MESSAGE_H */
