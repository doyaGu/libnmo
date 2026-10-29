#include "engine_fixture.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_WAIT_MESSAGE NMO_GUID(0x4587ffee, 0x4587ffdd)
#define GUID_SEND_MESSAGE NMO_GUID(0xa20e8d5b, 0xdf002150)
#define GUID_BROADCAST_MESSAGE NMO_GUID(0x3d6c4ae1, 0x72ae2cd6)
#define GUID_SWITCH_ON_MESSAGE NMO_GUID(0x1bb23f1d, 0x17ff14b9)
#define GUID_EXECUTE_SCRIPT NMO_GUID(0x706c5a40, 0x5bb31a0b)
#define GUID_DEACTIVATE_SCRIPT NMO_GUID(0x14367c05, 0x635b24f9)
#define GUID_ACTIVATE_OBJECT NMO_GUID(0x5f003fba, 0x5e574f60)
#define GUID_DEACTIVATE_OBJECT NMO_GUID(0x160f4b7d, 0x67de224e)
#define GUID_CALL_SCRIPT NMO_GUID(0x1a7d5b17, 0x70630fed)
#define GUID_ACTIVATE_SCRIPT NMO_GUID(0x4c7e7bc3, 0x0b693155)
#define GUID_OBJECT_DELETE NMO_GUID(0x74120ded, 0x76524673)

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

/* A script on another object; its blocks go in with fixture_block_in(). */
static nmo_object_id_t add_script_on(engine_fixture_t *f, nmo_object_id_t object, const char *name,
                                     nmo_object_id_t *out_input)
{
    nmo_object_id_t owner = f->owner_id;
    f->owner_id = object;
    nmo_object_id_t script = fixture_add_script(f, name, out_input);
    f->owner_id = owner;
    return script;
}

/* A script whose Switch On Message stays active, so it runs every frame. */
static void make_looper(engine_fixture_t *f, nmo_object_id_t script, nmo_object_id_t input)
{
    nmo_object_id_t sw = fixture_block_in(f, script, GUID_SWITCH_ON_MESSAGE, "Switch");
    fixture_link_in(f, script, input, BLOCK_IN(f, sw, 0), 0);
}

TEST(engine_messages, deactivated_object_skips_its_scripts)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t main_in = 0, loop_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t looper = add_script_on(&f, other, "Looper", &loop_in);
    ASSERT_TRUE(f.script_id != 0 && looper != 0);
    fixture_begin_edit(&f);
    make_looper(&f, looper, loop_in);
    nmo_object_id_t off = fixture_block(&f, GUID_DEACTIVATE_OBJECT, "Off");
    nmo_object_id_t on = fixture_block(&f, GUID_ACTIVATE_OBJECT, "On");
    ASSERT_TRUE(off != 0 && on != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, off, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, off, 0), BLOCK_IN(&f, on, 0), 3);
    fixture_input_int(&f, BLOCK_PIN(&f, off, 0), (int32_t)other);
    fixture_input_int(&f, BLOCK_PIN(&f, on, 0), (int32_t)other);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* Off in frame 0 takes effect from frame 1; On in frame 3 from frame 4 */
    const uint64_t loops[5] = {1u, 1u, 1u, 1u, 2u};
    for (int frame = 0; frame < 5; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
        ASSERT_EQ(loops[frame], runs(&f, looper));
        ASSERT_EQ(frame < 3 ? false : true, nmo_engine_object_is_active(f.engine, other));
    }
    /* The script kept being active through it */
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, looper));
    fixture_end(&f);
}

TEST(engine_messages, activate_object_can_start_all_its_scripts)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t main_in = 0, loop_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t looper = add_script_on(&f, other, "Looper", &loop_in);
    ASSERT_TRUE(f.script_id != 0 && looper != 0);
    fixture_begin_edit(&f);
    make_looper(&f, looper, loop_in);
    nmo_object_id_t on = fixture_block(&f, GUID_ACTIVATE_OBJECT, "On");
    ASSERT_TRUE(on != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, on, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, on, 0), (int32_t)other);
    fixture_input_int(&f, BLOCK_PIN(&f, on, 2), 1); /* Activate All Scripts */
    fixture_input_int(&f, BLOCK_PIN(&f, on, 3), 1); /* Reset Scripts */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, looper));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, looper));
    fixture_end(&f);
}

TEST(engine_messages, call_script_runs_the_script_now)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t main_in = 0, wait_in = 0, once_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t waited = fixture_add_script(&f, "Waited", &wait_in);
    nmo_object_id_t once = fixture_add_script(&f, "Once", &once_in);
    ASSERT_TRUE(f.script_id != 0 && waited != 0 && once != 0);
    fixture_begin_edit(&f);
    /* Both scripts wait 50 ms with a Delayer, then reach "Done" */
    nmo_object_id_t done[2];
    nmo_object_id_t scripts[2] = {waited, once}, inputs[2] = {wait_in, once_in};
    for (int i = 0; i < 2; i++) {
        nmo_object_id_t delayer = fixture_block_in(&f, scripts[i], GUID_DELAYER, "Delayer");
        done[i] = fixture_block_in(&f, scripts[i], GUID_NOP, "Done");
        ASSERT_TRUE(delayer != 0 && done[i] != 0);
        fixture_link_in(&f, scripts[i], inputs[i], BLOCK_IN(&f, delayer, 0), 0);
        fixture_link_in(&f, scripts[i], BLOCK_OUT(&f, delayer, 0), BLOCK_IN(&f, done[i], 0), 0);
        const float wait = 50.0f;
        fixture_input_value(&f, BLOCK_PIN(&f, delayer, 0), &wait, sizeof(wait));
    }
    nmo_object_id_t call_waiting = fixture_block(&f, GUID_CALL_SCRIPT, "Call Waiting");
    nmo_object_id_t call_once = fixture_block(&f, GUID_CALL_SCRIPT, "Call Once");
    nmo_object_id_t after = fixture_block(&f, GUID_NOP, "After");
    ASSERT_TRUE(call_waiting != 0 && call_once != 0 && after != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, call_waiting, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, call_waiting, 0), BLOCK_IN(&f, call_once, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, call_once, 0), BLOCK_IN(&f, after, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, call_waiting, 0), 1); /* Reset */
    fixture_input_int(&f, BLOCK_PIN(&f, call_waiting, 1), (int32_t)waited);
    fixture_input_int(&f, BLOCK_PIN(&f, call_waiting, 2), 1); /* Wait For Completion */
    fixture_input_int(&f, BLOCK_PIN(&f, call_once, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, call_once, 1), (int32_t)once);
    fixture_input_int(&f, BLOCK_PIN(&f, call_once, 2), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    /* Waiting: run until done within the frame, each run a frame's time */
    ASSERT_EQ(1u, runs(&f, done[0]));
    ASSERT_TRUE(runs(&f, waited) > 1u);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, waited));
    /* Not waiting: one run, then the script is stopped */
    ASSERT_EQ(1u, runs(&f, once));
    ASSERT_EQ(0u, runs(&f, done[1]));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, once));
    ASSERT_EQ(1u, runs(&f, after));
    fixture_end(&f);
}

/* An Activate Script of `script`, its Awake Object setting to be set to `awake` */
static nmo_object_id_t activate_script_block(engine_fixture_t *f, const char *name,
                                             nmo_object_id_t script, nmo_object_id_t *out_awake)
{
    nmo_object_id_t block = fixture_block(f, GUID_ACTIVATE_SCRIPT, name);
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    if (state == NULL || state->local_parameters.count == 0) {
        return 0;
    }
    *out_awake = nmo_behavior_ref_array_get_id(&state->local_parameters, 0);
    fixture_input_int(f, BLOCK_PIN(f, block, 0), 1);
    fixture_input_int(f, BLOCK_PIN(f, block, 1), (int32_t)script);
    return block;
}

TEST(engine_messages, activate_script_can_awake_an_inactive_owner)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t woken = fixture_create(&f, NMO_CID_3DENTITY, "Woken");
    nmo_object_id_t asleep = fixture_create(&f, NMO_CID_3DENTITY, "Asleep");
    nmo_object_id_t main_in = 0, woken_in = 0, asleep_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t woken_looper = add_script_on(&f, woken, "Woken Looper", &woken_in);
    nmo_object_id_t asleep_looper = add_script_on(&f, asleep, "Asleep Looper", &asleep_in);
    ASSERT_TRUE(f.script_id != 0 && woken_looper != 0 && asleep_looper != 0);
    fixture_begin_edit(&f);
    make_looper(&f, woken_looper, woken_in);
    make_looper(&f, asleep_looper, asleep_in);
    nmo_object_id_t off_woken = fixture_block(&f, GUID_DEACTIVATE_OBJECT, "Off Woken");
    nmo_object_id_t off_asleep = fixture_block(&f, GUID_DEACTIVATE_OBJECT, "Off Asleep");
    nmo_object_id_t awake = 0, not_awake = 0;
    nmo_object_id_t wake = activate_script_block(&f, "Wake", woken_looper, &awake);
    nmo_object_id_t start = activate_script_block(&f, "Start", asleep_looper, &not_awake);
    ASSERT_TRUE(off_woken != 0 && off_asleep != 0 && wake != 0 && start != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, off_woken, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, off_woken, 0), BLOCK_IN(&f, off_asleep, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, off_asleep, 0), BLOCK_IN(&f, wake, 0), 2);
    fixture_link(&f, BLOCK_OUT(&f, wake, 0), BLOCK_IN(&f, start, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, off_woken, 0), (int32_t)woken);
    fixture_input_int(&f, BLOCK_PIN(&f, off_asleep, 0), (int32_t)asleep);
    fixture_commit(&f, NULL);
    const int32_t yes = 1, no = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, awake, &yes, sizeof(yes)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, not_awake, &no, sizeof(no)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    for (int frame = 0; frame < 5; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    }
    /* Awake Object activates the owner as CKScene::Activate does; without it the script waits */
    ASSERT_TRUE(nmo_engine_object_is_active(f.engine, woken));
    ASSERT_TRUE(runs(&f, woken_looper) > 0u);
    ASSERT_FALSE(nmo_engine_object_is_active(f.engine, asleep));
    ASSERT_EQ(0u, runs(&f, asleep_looper));
    fixture_end(&f);
}

TEST(engine_messages, object_deactivated_in_the_frame_it_awakes_still_runs)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t main_in = 0, loop_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t looper = add_script_on(&f, other, "Looper", &loop_in);
    ASSERT_TRUE(f.script_id != 0 && looper != 0);
    fixture_begin_edit(&f);
    make_looper(&f, looper, loop_in);
    nmo_object_id_t off = fixture_block(&f, GUID_DEACTIVATE_OBJECT, "Off");
    nmo_object_id_t awake = 0;
    nmo_object_id_t wake = activate_script_block(&f, "Wake", looper, &awake);
    nmo_object_id_t off_again = fixture_block(&f, GUID_DEACTIVATE_OBJECT, "Off Again");
    ASSERT_TRUE(off != 0 && wake != 0 && off_again != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, off, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, off, 0), BLOCK_IN(&f, wake, 0), 2);
    fixture_link(&f, BLOCK_OUT(&f, wake, 0), BLOCK_IN(&f, off_again, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, off, 0), (int32_t)other);
    fixture_input_int(&f, BLOCK_PIN(&f, off_again, 0), (int32_t)other);
    fixture_commit(&f, NULL);
    const int32_t yes = 1;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, awake, &yes, sizeof(yes)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    }
    ASSERT_EQ(0u, runs(&f, looper));
    /*
     * Awoken and deactivated in frame 2: ManageObjectsActivity removes only
     * an object asked to leave more than to come, so it enters the list.
     */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, looper));
    fixture_end(&f);
}

TEST(engine_messages, object_delete_stops_the_object_for_good)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    fixture_install_messages(&f, message_names, 3);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t main_in = 0, loop_in = 0;
    f.script_id = fixture_add_script(&f, "Main", &main_in);
    f.script_in = main_in;
    nmo_object_id_t looper = add_script_on(&f, other, "Looper", &loop_in);
    ASSERT_TRUE(f.script_id != 0 && looper != 0);
    fixture_begin_edit(&f);
    make_looper(&f, looper, loop_in);
    nmo_object_id_t delete = fixture_block(&f, GUID_OBJECT_DELETE, "Delete");
    nmo_object_id_t on = fixture_block(&f, GUID_ACTIVATE_OBJECT, "On");
    ASSERT_TRUE(delete != 0 && on != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, delete, 0), 1);
    fixture_link(&f, BLOCK_OUT(&f, delete, 0), BLOCK_IN(&f, on, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, delete, 0), (int32_t)other);
    fixture_input_int(&f, BLOCK_PIN(&f, on, 0), (int32_t)other);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    }
    /* Deleted in frame 1, activating it again changes nothing */
    uint64_t before = runs(&f, looper);
    ASSERT_TRUE(before <= 2u);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(before, runs(&f, looper));
    ASSERT_FALSE(nmo_engine_object_is_active(f.engine, other));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_messages, sent_message_ends_the_wait_in_the_next_frame);
REGISTER_TEST(engine_messages, waiting_keeps_the_script_active_until_the_message_comes);
REGISTER_TEST(engine_messages, broadcast_reaches_waiters_of_the_class);
REGISTER_TEST(engine_messages, switch_on_message_sees_last_frame_messages);
REGISTER_TEST(engine_messages, execute_script_waits_for_the_script_to_finish);
REGISTER_TEST(engine_messages, deactivate_script_stops_another_script);
REGISTER_TEST(engine_messages, deactivated_object_skips_its_scripts);
REGISTER_TEST(engine_messages, activate_object_can_start_all_its_scripts);
REGISTER_TEST(engine_messages, call_script_runs_the_script_now);
REGISTER_TEST(engine_messages, activate_script_can_awake_an_inactive_owner);
REGISTER_TEST(engine_messages, object_deactivated_in_the_frame_it_awakes_still_runs);
REGISTER_TEST(engine_messages, object_delete_stops_the_object_for_good);
TEST_MAIN_END()
