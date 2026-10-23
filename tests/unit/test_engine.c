#include "engine_fixture.h"

#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_COUNTER NMO_GUID(0x998f000f, 0xf000f899)
#define GUID_BINARY_SWITCH NMO_GUID(0xeb506901, 0x984afccc)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_IDENTITY NMO_GUID(0x15151652, 0xaeefffd5)
#define GUID_SHOW NMO_GUID(0xa85a213a, 0xef78d52a)
#define GUID_PER_SECOND NMO_GUID(0x448e54ce, 0x75a655c5)
#define GUID_THRESHOLD NMO_GUID(0x655e6af4, 0x08c0596f)
#define GUID_INTERPOLATOR NMO_GUID(0x35503950, 0x2dde7a65)
#define GUID_SET_COMPONENT NMO_GUID(0x6e800755, 0x57b64acb)

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

typedef struct event_log {
    size_t infinite_loops;
    size_t unknown_blocks;
    size_t operations_run;
    size_t effects;
    nmo_object_id_t effect_target;
} event_log_t;

static void log_event(const nmo_engine_event_t *event, void *user_data)
{
    event_log_t *log = (event_log_t *)user_data;
    log->infinite_loops += event->kind == NMO_ENGINE_EVENT_INFINITE_LOOP;
    log->unknown_blocks += event->kind == NMO_ENGINE_EVENT_UNKNOWN_BLOCK;
    log->operations_run += event->kind == NMO_ENGINE_EVENT_OPERATION && event->result == NMO_OK;
    if (event->kind == NMO_ENGINE_EVENT_EFFECT) {
        log->effects++;
        log->effect_target = event->object_id;
    }
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

TEST(engine, effect_block_reports_its_target_and_continues)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t show = fixture_block(&f, GUID_SHOW, "Show");
    nmo_object_id_t after = fixture_block(&f, GUID_NOP, "After");
    ASSERT_TRUE(show != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, show, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, show, 0), BLOCK_IN(&f, after, 0), 0);
    if (fixture_target(&f, show) != 0) {
        fixture_input_int(&f, fixture_target(&f, show), (int32_t)f.owner_id);
    }
    fixture_commit(&f, NULL);

    event_log_t log = {0};
    nmo_engine_set_trace(f.engine, log_event, &log);
    ASSERT_TRUE(nmo_engine_has_block(f.engine, GUID_SHOW));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, log.effects);
    ASSERT_EQ(f.owner_id, log.effect_target);
    ASSERT_EQ(0u, log.unknown_blocks);
    ASSERT_EQ(1u, runs(&f, after));
    fixture_end(&f);
}

TEST(engine, calculator_blocks_compute_their_outputs)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t per_second = fixture_block(&f, GUID_PER_SECOND, "Per Second");
    nmo_object_id_t interpolator = fixture_block(&f, GUID_INTERPOLATOR, "Interpolator");
    nmo_object_id_t set_component = fixture_block(&f, GUID_SET_COMPONENT, "Set Component");
    ASSERT_TRUE(per_second != 0 && interpolator != 0 && set_component != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, per_second, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, interpolator, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, set_component, 0), 0);
    input_float(&f, BLOCK_PIN(&f, per_second, 0), 10.0f);
    input_float(&f, BLOCK_PIN(&f, interpolator, 0), 2.0f);
    input_float(&f, BLOCK_PIN(&f, interpolator, 1), 4.0f);
    input_float(&f, BLOCK_PIN(&f, interpolator, 2), 0.25f);
    for (int i = 0; i < 3; i++) {
        input_float(&f, BLOCK_PIN(&f, set_component, (size_t)i), (float)(i + 1));
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 500.0f));
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, per_second, 0), &value, sizeof(value));
    ASSERT_TRUE(value == 5.0f); /* 10 per second over half a second */
    read_value(&f, BLOCK_POUT(&f, interpolator, 0), &value, sizeof(value));
    ASSERT_TRUE(value == 2.5f);
    float vector[3] = {0};
    read_value(&f, BLOCK_POUT(&f, set_component, 0), vector, sizeof(vector));
    ASSERT_TRUE(vector[0] == 1.0f && vector[1] == 2.0f && vector[2] == 3.0f);
    fixture_end(&f);
}

TEST(engine, threshold_picks_the_output_and_clamps)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t above = fixture_block(&f, GUID_THRESHOLD, "Above");
    nmo_object_id_t inside = fixture_block(&f, GUID_THRESHOLD, "Inside");
    nmo_object_id_t was_above = fixture_block(&f, GUID_NOP, "Was Above");
    nmo_object_id_t was_inside = fixture_block(&f, GUID_NOP, "Was Inside");
    ASSERT_TRUE(above != 0 && inside != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, above, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, inside, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, above, 1), BLOCK_IN(&f, was_above, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, inside, 2), BLOCK_IN(&f, was_inside, 0), 0);
    const float xs[2] = {12.0f, 5.0f};
    const nmo_object_id_t blocks[2] = {above, inside};
    for (int i = 0; i < 2; i++) {
        input_float(&f, BLOCK_PIN(&f, blocks[i], 0), xs[i]);
        input_float(&f, BLOCK_PIN(&f, blocks[i], 1), 0.0f);
        input_float(&f, BLOCK_PIN(&f, blocks[i], 2), 10.0f);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, was_above));
    ASSERT_EQ(1u, runs(&f, was_inside));
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, above, 0), &value, sizeof(value));
    ASSERT_TRUE(value == 10.0f);
    read_value(&f, BLOCK_POUT(&f, inside, 0), &value, sizeof(value));
    ASSERT_TRUE(value == 5.0f);
    fixture_end(&f);
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
REGISTER_TEST(engine, effect_block_reports_its_target_and_continues);
REGISTER_TEST(engine, calculator_blocks_compute_their_outputs);
REGISTER_TEST(engine, threshold_picks_the_output_and_clamps);
REGISTER_TEST(engine, registered_block_replaces_the_builtin);
REGISTER_TEST(engine, iteration_limit_stops_an_infinite_loop);
REGISTER_TEST(engine, scripts_start_only_when_asked);
TEST_MAIN_END()
