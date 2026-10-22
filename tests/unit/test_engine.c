#include "test_framework.h"

#include "nmo_engine.h"
#include "edit/nmo_script_edit.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_statesave_ids.h"
#include "runtime/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "type/nmo_operations.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <string.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_COUNTER NMO_GUID(0x998f000f, 0xf000f899)
#define GUID_BINARY_SWITCH NMO_GUID(0xeb506901, 0x984afccc)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_IDENTITY NMO_GUID(0x15151652, 0xaeefffd5)

/*
 * A document with one 3D entity whose script has a single input, and an open
 * script edit to add building blocks to it. Committing builds the engine.
 */
typedef struct engine_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_object_repository_t *repo;
    nmo_object_id_t owner_id;
    nmo_object_id_t script_id;
    nmo_object_id_t script_in;
    nmo_script_edit_tx_t *tx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_engine_t *engine;
} engine_fixture_t;

/* The helpers that return a value return 0 or NULL on failure; the caller's next check fails. */
static nmo_object_id_t fixture_create(engine_fixture_t *f, nmo_class_id_t class_id,
                                      const char *name)
{
    nmo_object_id_t id = 0;
    if (nmo_session_create_object(f->session, class_id, name, (nmo_guid_t){0, 0}, &id, NULL) !=
        NMO_OK) {
        return 0;
    }
    return id;
}

static void *fixture_state(engine_fixture_t *f, nmo_object_id_t id)
{
    nmo_object_t *object = nmo_object_repository_find_by_id(f->repo, id);
    return object != NULL ? nmo_object_get_state(object) : NULL;
}

static void fixture_begin(engine_fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->ctx = nmo_context_create(&(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(f->ctx);
    f->session = nmo_session_create(f->ctx);
    ASSERT_NOT_NULL(f->session);
    f->repo = nmo_session_get_repository(f->session);

    f->owner_id = fixture_create(f, NMO_CID_3DENTITY, "Owner");
    f->script_id = fixture_create(f, NMO_CID_BEHAVIOR, "Script");
    f->script_in = fixture_create(f, NMO_CID_BEHAVIORIO, "Start");

    nmo_beobject_state_t *owner = (nmo_beobject_state_t *)fixture_state(f, f->owner_id);
    nmo_behavior_state_t *script = (nmo_behavior_state_t *)fixture_state(f, f->script_id);
    nmo_behaviorio_state_t *io = (nmo_behaviorio_state_t *)fixture_state(f, f->script_in);
    ASSERT_NOT_NULL(owner);
    ASSERT_NOT_NULL(script);
    ASSERT_NOT_NULL(io);
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(&owner->scripts, f->script_id));
    ASSERT_EQ(NMO_OK, nmo_behavior_ref_array_append(&script->inputs, f->script_in, NULL));
    script->flags |= CKBEHAVIOR_SCRIPT;
    nmo_behavior_set_owner_id(script, f->owner_id);
    script->has_save_flags = true;
    script->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV | CK_STATESAVE_BEHAVIORINPUTS;
    io->old_flags = NMO_BEHAVIORIO_OLD_IN;
    io->has_flags = true;

    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(f->session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f->ctx, document, &workspace));
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(workspace, "engine fixture", &f->tx));
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
}

static nmo_object_id_t fixture_block(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    nmo_object_id_t id = 0;
    return nmo_script_edit_add_node(f->tx, f->script_id, guid, name, &id) == NMO_OK ? id : 0;
}

static nmo_object_id_t fixture_ref(engine_fixture_t *f, nmo_object_id_t behavior_id,
                                   size_t which, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, behavior_id);
    if (state == NULL) {
        return 0;
    }
    const nmo_array_t *arrays[] = {&state->inputs, &state->outputs, &state->in_parameters,
                                   &state->out_parameters};
    return pos < arrays[which]->count ? nmo_behavior_ref_array_get_id(arrays[which], pos) : 0;
}

#define BLOCK_IN(f, id, pos) fixture_ref((f), (id), 0, (pos))
#define BLOCK_OUT(f, id, pos) fixture_ref((f), (id), 1, (pos))
#define BLOCK_PIN(f, id, pos) fixture_ref((f), (id), 2, (pos))
#define BLOCK_POUT(f, id, pos) fixture_ref((f), (id), 3, (pos))

static void fixture_link(engine_fixture_t *f, nmo_object_id_t from, nmo_object_id_t to,
                         uint32_t delay)
{
    nmo_object_id_t link = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_behavior_link(f->tx, f->script_id, from, to, delay,
                                                        &link));
}

/* Give input parameter `pin` a source holding `size` bytes. */
static void fixture_input_value(engine_fixture_t *f, nmo_object_id_t pin, const void *data,
                                size_t size)
{
    nmo_object_id_t source = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f->tx, pin, &source));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_set_parameter_bytes(f->tx, source, (const uint8_t *)data, size));
}

static void fixture_commit(engine_fixture_t *f, const nmo_engine_options_t *options)
{
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(f->tx));
    f->tx = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(f->session, &f->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f->ctx, f->document, &f->workspace));
    ASSERT_EQ(NMO_OK, nmo_engine_create(f->workspace, options, &f->engine));
}

static void fixture_end(engine_fixture_t *f)
{
    if (f->tx != NULL) {
        nmo_script_edit_rollback(f->tx);
    }
    nmo_engine_destroy(f->engine);
    if (f->workspace != NULL) {
        nmo_workspace_destroy(f->workspace);
    }
    if (f->document != NULL) {
        nmo_document_destroy(f->document);
    }
    nmo_session_destroy(f->session);
    nmo_context_release(f->ctx);
}

static uint64_t runs(engine_fixture_t *f, nmo_object_id_t id)
{
    return nmo_engine_behavior_execution_count(f->engine, id);
}

static void read_value(engine_fixture_t *f, nmo_object_id_t param, void *out, size_t size)
{
    const void *data = NULL;
    size_t data_size = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f->engine, param, &data, &data_size, NULL));
    ASSERT_TRUE(data_size >= size);
    memcpy(out, data, size);
}

typedef struct event_log {
    size_t infinite_loops;
    size_t unknown_blocks;
    size_t operations_run;
} event_log_t;

static void log_event(const nmo_engine_event_t *event, void *user_data)
{
    event_log_t *log = (event_log_t *)user_data;
    log->infinite_loops += event->kind == NMO_ENGINE_EVENT_INFINITE_LOOP;
    log->unknown_blocks += event->kind == NMO_ENGINE_EVENT_UNKNOWN_BLOCK;
    log->operations_run += event->kind == NMO_ENGINE_EVENT_OPERATION && event->result == NMO_OK;
}

/* ========================================================================== */

TEST(engine, script_start_runs_first_block_and_finishes)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = fixture_block(&f, GUID_NOP, "A");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, a, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, a));
    ASSERT_EQ(1u, runs(&f, f.script_id));
    /* Nothing left to run: the script deactivates and is not executed again */
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, f.script_id));
    ASSERT_EQ(2u, nmo_engine_frame(f.engine));
    fixture_end(&f);
}

TEST(engine, delayed_link_activates_its_target_after_the_delay)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = fixture_block(&f, GUID_NOP, "A");
    nmo_object_id_t b = fixture_block(&f, GUID_NOP, "B");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, a, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, a, 0), BLOCK_IN(&f, b, 0), 2);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, a));
    ASSERT_EQ(0u, runs(&f, b));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, b));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, b));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    fixture_end(&f);
}

TEST(engine, immediate_loop_runs_within_one_frame)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t counter = fixture_block(&f, GUID_COUNTER, "Counter");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, counter, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, counter, 1), BLOCK_IN(&f, counter, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, counter, 0), BLOCK_IN(&f, done, 0), 0);
    int32_t count = 5;
    fixture_input_value(&f, BLOCK_PIN(&f, counter, 0), &count, sizeof(count));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* CounterV2 with Start Index and Step unset (1): five loops, then Out */
    ASSERT_EQ(6u, runs(&f, counter));
    ASSERT_EQ(1u, runs(&f, done));
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, counter, 0), &value, sizeof(value));
    ASSERT_EQ(6, value);
    fixture_end(&f);
}

TEST(engine, binary_switch_follows_its_condition)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t sw = fixture_block(&f, GUID_BINARY_SWITCH, "Switch");
    nmo_object_id_t yes = fixture_block(&f, GUID_NOP, "Yes");
    nmo_object_id_t no = fixture_block(&f, GUID_NOP, "No");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sw, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sw, 0), BLOCK_IN(&f, yes, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sw, 1), BLOCK_IN(&f, no, 0), 0);
    int32_t condition = 0;
    fixture_input_value(&f, BLOCK_PIN(&f, sw, 0), &condition, sizeof(condition));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, yes));
    ASSERT_EQ(1u, runs(&f, no));
    fixture_end(&f);
}

TEST(engine, delayer_waits_for_its_time)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t delayer = fixture_block(&f, GUID_DELAYER, "Delayer");
    nmo_object_id_t after = fixture_block(&f, GUID_NOP, "After");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, delayer, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, delayer, 0), BLOCK_IN(&f, after, 0), 0);
    float wait = 100.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, delayer, 0), &wait, sizeof(wait));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
        ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, delayer));
    }
    ASSERT_EQ(0u, runs(&f, after));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_EQ(4u, runs(&f, delayer));
    ASSERT_EQ(1u, runs(&f, after));
    float elapsed = 0.0f;
    read_value(&f, BLOCK_POUT(&f, delayer, 0), &elapsed, sizeof(elapsed));
    ASSERT_TRUE(elapsed >= 100.0f);
    fixture_end(&f);
}

TEST(engine, operation_output_is_computed_when_read)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t identity = fixture_block(&f, GUID_IDENTITY, "Identity");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, identity, 0), 0);

    nmo_object_id_t a = 0, b = 0, op = 0;
    float va = 2.5f, vb = 3.0f;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, f.script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_FLOAT, "A", &a));
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, f.script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_FLOAT, "B", &b));
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, a, (const uint8_t *)&va, 4));
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, b, (const uint8_t *)&vb, 4));
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_operation(f.tx, f.script_id, NMO_OP_GUID_ADD, a, b, 0,
                                                    &op));
    const nmo_parameteroperation_state_t *op_state =
        (const nmo_parameteroperation_state_t *)fixture_state(&f, op);
    ASSERT_NOT_NULL(op_state);
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(
                          f.tx, nmo_parameteroperation_out_id(op_state), BLOCK_PIN(&f, identity, 0)));
    fixture_commit(&f, NULL);

    event_log_t log = {0};
    nmo_engine_set_trace(f.engine, log_event, &log);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, identity));
    ASSERT_EQ(1u, log.operations_run);
    float result = 0.0f;
    read_value(&f, BLOCK_POUT(&f, identity, 0), &result, sizeof(result));
    ASSERT_TRUE(result == 5.5f);

    /* The engine's values change; the document's do not */
    float changed = 10.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, a, &changed, sizeof(changed)));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    read_value(&f, BLOCK_POUT(&f, identity, 0), &result, sizeof(result));
    ASSERT_TRUE(result == 13.0f);
    fixture_end(&f);
}

TEST(engine, unknown_block_stops_or_passes_the_flow)
{
    nmo_engine_options_t options = nmo_engine_options_default();
    options.no_builtin_blocks = true;
    for (int pass = 0; pass < 2; pass++) {
        engine_fixture_t f;
        fixture_begin(&f);
        nmo_object_id_t a = fixture_block(&f, GUID_NOP, "A");
        nmo_object_id_t b = fixture_block(&f, GUID_NOP, "B");
        fixture_link(&f, f.script_in, BLOCK_IN(&f, a, 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, a, 0), BLOCK_IN(&f, b, 0), 0);
        options.unknown_block = pass ? NMO_ENGINE_UNKNOWN_BLOCK_PASS : NMO_ENGINE_UNKNOWN_BLOCK_STOP;
        fixture_commit(&f, &options);

        event_log_t log = {0};
        nmo_engine_set_trace(f.engine, log_event, &log);
        ASSERT_FALSE(nmo_engine_has_block(f.engine, GUID_NOP));
        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
        ASSERT_EQ(1u, runs(&f, a));
        ASSERT_EQ(pass ? 1u : 0u, runs(&f, b));
        ASSERT_EQ(pass ? 2u : 1u, log.unknown_blocks);
        fixture_end(&f);
    }
}

static int custom_nop_calls;

static int custom_nop(nmo_engine_block_t *block)
{
    custom_nop_calls++;
    nmo_engine_block_activate_input(block, 0, false);
    nmo_engine_block_activate_output(block, 0, true);
    return NMO_ENGINE_BR_OK;
}

TEST(engine, registered_block_replaces_the_builtin)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = fixture_block(&f, GUID_NOP, "A");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, a, 0), 0);
    fixture_commit(&f, NULL);

    custom_nop_calls = 0;
    nmo_engine_block_desc_t desc = {.guid = GUID_NOP, .name = "Nop", .function = custom_nop};
    ASSERT_EQ(NMO_OK, nmo_engine_register_block(f.engine, &desc));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_engine_register_block(f.engine, &desc));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1, custom_nop_calls);
    fixture_end(&f);
}

TEST(engine, iteration_limit_stops_an_infinite_loop)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = fixture_block(&f, GUID_NOP, "A");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, a, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, a, 0), BLOCK_IN(&f, a, 0), 0);
    nmo_engine_options_t options = nmo_engine_options_default();
    options.max_iterations = 20;
    fixture_commit(&f, &options);

    event_log_t log = {0};
    nmo_engine_set_trace(f.engine, log_event, &log);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, log.infinite_loops);
    ASSERT_EQ(21u, runs(&f, a));
    fixture_end(&f);
}

TEST(engine, scripts_start_only_when_asked)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = fixture_block(&f, GUID_NOP, "A");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, a, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_engine_start(f.engine, NMO_ENGINE_START_NONE));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, a));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_engine_activate_script(f.engine, a, true, true));
    ASSERT_EQ(NMO_OK, nmo_engine_activate_script(f.engine, f.script_id, true, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, a));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine, script_start_runs_first_block_and_finishes);
REGISTER_TEST(engine, delayed_link_activates_its_target_after_the_delay);
REGISTER_TEST(engine, immediate_loop_runs_within_one_frame);
REGISTER_TEST(engine, binary_switch_follows_its_condition);
REGISTER_TEST(engine, delayer_waits_for_its_time);
REGISTER_TEST(engine, operation_output_is_computed_when_read);
REGISTER_TEST(engine, unknown_block_stops_or_passes_the_flow);
REGISTER_TEST(engine, registered_block_replaces_the_builtin);
REGISTER_TEST(engine, iteration_limit_stops_an_infinite_loop);
REGISTER_TEST(engine, scripts_start_only_when_asked);
TEST_MAIN_END()
