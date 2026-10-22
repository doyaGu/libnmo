#include "engine_fixture.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_WAIT_MESSAGE NMO_GUID(0x4587ffee, 0x4587ffdd)
#define GUID_SEND_MESSAGE NMO_GUID(0xa20e8d5b, 0xdf002150)
#define GUID_BROADCAST_MESSAGE NMO_GUID(0x3d6c4ae1, 0x72ae2cd6)
#define GUID_SWITCH_ON_MESSAGE NMO_GUID(0x1bb23f1d, 0x17ff14b9)
#define GUID_EXECUTE_SCRIPT NMO_GUID(0x706c5a40, 0x5bb31a0b)
#define GUID_DEACTIVATE_SCRIPT NMO_GUID(0x14367c05, 0x635b24f9)

/* The message types the fixture document declares; a type is its position. */
static const char *const message_names[] = {"OnClick", "Message A", "Message B"};
#define MSG_A 1
#define MSG_B 2

/* One script on the owner, with the message types declared. */
static void begin(engine_fixture_t *f)
{
    fixture_begin_document(f);
    fixture_install_messages(f, message_names, 3);
    f->script_id = fixture_add_script(f, "Script", &f->script_in);
    ASSERT_TRUE(f->script_id != 0);
    fixture_begin_edit(f);
}

typedef struct message_log {
    size_t sent;
    size_t received;
    int32_t last_type;
} message_log_t;

static void log_messages(const nmo_engine_event_t *event, void *user_data)
{
    message_log_t *log = (message_log_t *)user_data;
    if (event->kind == NMO_ENGINE_EVENT_MESSAGE_SENT) {
        log->sent++;
    } else if (event->kind == NMO_ENGINE_EVENT_MESSAGE_RECEIVED) {
        log->received++;
        log->last_type = event->result;
    }
}

/* A Wait Message on the owner for `type`; its output goes to a new Nop, returned in `out_after`. */
static nmo_object_id_t add_wait(engine_fixture_t *f, int32_t type, nmo_object_id_t *out_after)
{
    nmo_object_id_t wait = fixture_block(f, GUID_WAIT_MESSAGE, "Wait");
    nmo_object_id_t after = fixture_block(f, GUID_NOP, "After Wait");
    fixture_link(f, f->script_in, BLOCK_IN(f, wait, 0), 0);
    fixture_link(f, BLOCK_OUT(f, wait, 0), BLOCK_IN(f, after, 0), 0);
    fixture_input_int(f, BLOCK_PIN(f, wait, 0), type);
    if (fixture_target(f, wait) != 0) {
        fixture_input_int(f, fixture_target(f, wait), (int32_t)f->owner_id);
    }
    *out_after = after;
    return wait;
}

TEST(engine_messages, sent_message_ends_the_wait_in_the_next_frame)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t after = 0;
    nmo_object_id_t wait = add_wait(&f, MSG_A, &after);
    nmo_object_id_t send = fixture_block(&f, GUID_SEND_MESSAGE, "Send");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, send, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, send, 0), MSG_A);
    fixture_input_int(&f, BLOCK_PIN(&f, send, 1), (int32_t)f.owner_id);
    if (fixture_target(&f, send) != 0) {
        fixture_input_int(&f, fixture_target(&f, send), (int32_t)f.owner_id);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(3, nmo_engine_message_type_count(f.engine));
    ASSERT_EQ(MSG_A, nmo_engine_message_type(f.engine, "Message A"));
    ASSERT_STR_EQ("Message B", nmo_engine_message_type_name(f.engine, MSG_B));
    ASSERT_EQ(-1, nmo_engine_message_type(f.engine, "Missing"));
    message_log_t log = {0};
    nmo_engine_set_trace(f.engine, log_messages, &log);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* Delivered at the end of frame 0; the graph follows the output in frame 1 */
    ASSERT_EQ(1u, runs(&f, wait));
    ASSERT_EQ(1u, runs(&f, send));
    ASSERT_EQ(1u, log.sent);
    ASSERT_EQ(1u, log.received);
    ASSERT_EQ(MSG_A, log.last_type);
    ASSERT_EQ(0u, runs(&f, after));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, after));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    fixture_end(&f);
}

TEST(engine_messages, waiting_keeps_the_script_active_until_the_message_comes)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t after = 0;
    nmo_object_id_t wait = add_wait(&f, MSG_A, &after);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
        ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, f.script_id));
        ASSERT_TRUE((nmo_engine_behavior_flags(f.engine, wait) & CKBEHAVIOR_WAITSFORMESSAGE) != 0);
    }
    ASSERT_EQ(1u, runs(&f, wait));

    /* Another type, or another object, does not end the wait */
    ASSERT_EQ(NMO_OK, nmo_engine_send_message(f.engine, MSG_B, f.owner_id, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_send_message(f.engine, MSG_A, f.script_id, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_TRUE((nmo_engine_behavior_flags(f.engine, wait) & CKBEHAVIOR_WAITSFORMESSAGE) != 0);

    ASSERT_EQ(NMO_OK, nmo_engine_send_message(f.engine, MSG_A, f.owner_id, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, after));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, after));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_engine_send_message(f.engine, -1, f.owner_id, 0));
    fixture_end(&f);
}

TEST(engine_messages, broadcast_reaches_waiters_of_the_class)
{
    for (int matching = 0; matching < 2; matching++) {
        engine_fixture_t f;
        begin(&f);
        nmo_object_id_t after = 0;
        nmo_object_id_t wait = add_wait(&f, MSG_A, &after);
        nmo_object_id_t broadcast = fixture_block(&f, GUID_BROADCAST_MESSAGE, "Broadcast");
        ASSERT_TRUE(broadcast != 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, broadcast, 0), 0);
        fixture_input_int(&f, BLOCK_PIN(&f, broadcast, 0), MSG_A);
        /*
         * "Type" keeps its default, "Behavioral Object", which the owner (a 3D
         * entity) derives from; a broadcast to cameras misses it.
         */
        if (!matching) {
            fixture_input_int(&f, BLOCK_PIN(&f, broadcast, 1), NMO_CID_CAMERA);
        }
        if (fixture_target(&f, broadcast) != 0) {
            fixture_input_int(&f, fixture_target(&f, broadcast), (int32_t)f.owner_id);
        }
        fixture_commit(&f, NULL);

        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
        ASSERT_EQ(1u, runs(&f, broadcast));
        ASSERT_EQ(matching ? 0 : 1,
                  (nmo_engine_behavior_flags(f.engine, wait) & CKBEHAVIOR_WAITSFORMESSAGE) != 0);
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
        ASSERT_EQ(matching ? 1u : 0u, runs(&f, after));
        fixture_end(&f);
    }
}

TEST(engine_messages, switch_on_message_sees_last_frame_messages)
{
    engine_fixture_t f;
    begin(&f);
    nmo_object_id_t sw = fixture_block(&f, GUID_SWITCH_ON_MESSAGE, "Switch");
    nmo_object_id_t on_a = fixture_block(&f, GUID_NOP, "On A");
    nmo_object_id_t on_b = fixture_block(&f, GUID_NOP, "On B");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sw, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sw, 0), BLOCK_IN(&f, on_a, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sw, 1), BLOCK_IN(&f, on_b, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, sw, 0), MSG_A);
    fixture_input_int(&f, BLOCK_PIN(&f, sw, 1), MSG_B);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_send_message(f.engine, MSG_B, f.owner_id, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, on_b)); /* delivered at the end of frame 0 */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, on_a));
    ASSERT_EQ(1u, runs(&f, on_b));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, on_b)); /* only the frame after delivery sees it */
    ASSERT_EQ(3u, runs(&f, sw));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, sw));
    fixture_end(&f);
}

TEST(engine_messages, execute_script_waits_for_the_script_to_finish)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t main_in = 0, worker_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t worker = fixture_add_script(&f, "Worker", &worker_in);
    ASSERT_TRUE(f.script_id != 0 && worker != 0);
    fixture_begin_edit(&f);

    nmo_object_id_t exec = fixture_block(&f, GUID_EXECUTE_SCRIPT, "Execute");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, exec, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, exec, 0), BLOCK_IN(&f, done, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, exec, 1), (int32_t)worker);

    nmo_object_id_t delayer = fixture_block_in(&f, worker, GUID_DELAYER, "Delayer");
    nmo_object_id_t work_done = fixture_block_in(&f, worker, GUID_NOP, "Work Done");
    fixture_link_in(&f, worker, worker_in, BLOCK_IN(&f, delayer, 0), 0);
    fixture_link_in(&f, worker, BLOCK_OUT(&f, delayer, 0), BLOCK_IN(&f, work_done, 0), 0);
    float wait = 50.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, delayer, 0), &wait, sizeof(wait));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f)); /* frame 0: Execute starts Worker */
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, worker));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f)); /* frame 1: Delayer at 25 ms */
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, worker));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f)); /* frame 2: Delayer done, Worker ends */
    ASSERT_EQ(1u, runs(&f, work_done));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, worker));
    ASSERT_EQ(0u, runs(&f, done));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f)); /* frame 3: Execute sees it */
    ASSERT_EQ(1u, runs(&f, done));
    ASSERT_EQ(4u, runs(&f, exec));
    fixture_end(&f);
}

TEST(engine_messages, deactivate_script_stops_another_script)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t main_in = 0, other_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t other = fixture_add_script(&f, "Other", &other_in);
    ASSERT_TRUE(f.script_id != 0 && other != 0);
    fixture_begin_edit(&f);

    nmo_object_id_t deactivate = fixture_block(&f, GUID_DEACTIVATE_SCRIPT, "Deactivate");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, deactivate, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, deactivate, 0), (int32_t)other);
    /* Switch On Message stays active every frame */
    nmo_object_id_t sw = fixture_block_in(&f, other, GUID_SWITCH_ON_MESSAGE, "Switch");
    fixture_link_in(&f, other, other_in, BLOCK_IN(&f, sw, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, other));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, other));
    ASSERT_EQ(1u, runs(&f, other));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_messages, sent_message_ends_the_wait_in_the_next_frame);
REGISTER_TEST(engine_messages, waiting_keeps_the_script_active_until_the_message_comes);
REGISTER_TEST(engine_messages, broadcast_reaches_waiters_of_the_class);
REGISTER_TEST(engine_messages, switch_on_message_sees_last_frame_messages);
REGISTER_TEST(engine_messages, execute_script_waits_for_the_script_to_finish);
REGISTER_TEST(engine_messages, deactivate_script_stops_another_script);
TEST_MAIN_END()
