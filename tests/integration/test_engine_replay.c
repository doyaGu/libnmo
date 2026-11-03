#include "../unit/engine_fixture.h"

#include "document/nmo_document_load.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_guids.h"

#include <stdarg.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_KEY_EVENT NMO_GUID(0x1af2274b, 0x6b8c1524)
#define GUID_WAIT_MESSAGE NMO_GUID(0x4587ffee, 0x4587ffdd)
#define GUID_SEND_MESSAGE NMO_GUID(0xa20e8d5b, 0xdf002150)
#define GUID_DELAYER NMO_GUID(0x15d472a5, 0x3bea409f)
#define GUID_SET_POSITION NMO_GUID(0xe456e78a, 0x456789aa)
#define GUID_RANDOM NMO_GUID(0x0c622386, 0x1c3054f7)
#define GUID_SET_CELL NMO_GUID(0x30ed1c6d, 0x4a3b7067)
#define KEY_SPACE 0x39u
#define FRAME_MS 10.0f
#define FRAME_COUNT 8

/* Save the inputs in the composition, including object and manager references.
 * The replay must exercise the values produced by loading the file. */
static void saved_input(engine_fixture_t *f, nmo_object_id_t input, const void *bytes,
                         size_t size, const char *text, const char *name)
{
    nmo_object_id_t source = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f->tx, input, &source));
    ASSERT_TRUE(source != 0);
    if (text != NULL) {
        ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(f->tx, source, text));
    } else {
        ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f->tx, source, bytes, size));
    }
    if (name != NULL) {
        ASSERT_EQ(NMO_OK, nmo_object_repository_rename(f->repo, source, name));
    }
}

static void saved_int(engine_fixture_t *f, nmo_object_id_t input, int32_t value, const char *name)
{
    saved_input(f, input, &value, sizeof(value), NULL, name);
}

static void saved_object(engine_fixture_t *f, nmo_object_id_t input, nmo_object_id_t object)
{
    char text[32];
    snprintf(text, sizeof(text), "%u", object);
    saved_input(f, input, NULL, 0, text, NULL);
}

static void set_priority(engine_fixture_t *f, nmo_object_id_t behavior, int32_t priority)
{
    nmo_behavior_state_t *state = fixture_state(f, behavior);
    ASSERT_NOT_NULL(state);
    state->priority = priority;
}

static void write_replay_file(const char *path)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    const char *const messages[] = {"OnClick", "Advance", "Complete"};
    fixture_install_messages(&f, messages, 3);
    nmo_3dentity_state_t *entity = fixture_state(&f, f.owner_id);
    ASSERT_NOT_NULL(entity);
    memset(entity->world_matrix, 0, sizeof(entity->world_matrix));
    entity->world_matrix[0] = entity->world_matrix[5] = entity->world_matrix[10] =
        entity->world_matrix[15] = 1.0f;

    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "Table");
    nmo_dataarray_state_t *table = fixture_state(&f, array);
    ASSERT_NOT_NULL(table);
    nmo_arena_t *arena = nmo_session_get_arena(f.session);
    table->column_formats = nmo_arena_alloc(arena, sizeof(*table->column_formats),
                                            _Alignof(nmo_dataarray_column_format_t));
    table->rows = nmo_arena_alloc(arena, sizeof(*table->rows), _Alignof(nmo_dataarray_row_t));
    ASSERT_NOT_NULL(table->column_formats);
    ASSERT_NOT_NULL(table->rows);
    *table->column_formats = (nmo_dataarray_column_format_t){.name = "Value", .type = CKARRAYTYPE_INT};
    table->rows->cells = nmo_arena_alloc(arena, sizeof(*table->rows->cells),
                                         _Alignof(nmo_dataarray_cell_t));
    ASSERT_NOT_NULL(table->rows->cells);
    memset(table->rows->cells, 0, sizeof(*table->rows->cells));
    table->column_count = table->row_count = table->rows->column_count = 1;
    table->key_column = -1;

    /* CKBeObject::ExecuteBehaviors follows its saved script list. Unlike a
       graph's children, that list is not sorted again when the file loads. */
    nmo_object_id_t worker_in = 0, clock_in = 0, observer_in = 0;
    nmo_object_id_t worker = fixture_add_script(&f, "Worker", &worker_in);
    nmo_object_id_t clock = fixture_add_script(&f, "Clock", &clock_in);
    f.script_id = fixture_add_script(&f, "Input", &f.script_in);
    nmo_object_id_t observer = fixture_add_script(&f, "Observer", &observer_in);
    ASSERT_TRUE(worker != 0 && clock != 0 && f.script_id != 0 && observer != 0);
    set_priority(&f, observer, 40);
    set_priority(&f, f.script_id, 30);
    set_priority(&f, worker, 20);
    set_priority(&f, clock, 10);
    fixture_begin_edit(&f);

    nmo_object_id_t key = fixture_block(&f, GUID_KEY_EVENT, "Key");
    nmo_object_id_t advance = fixture_block(&f, GUID_SEND_MESSAGE, "SendAdvance");
    nmo_object_id_t released = fixture_block(&f, GUID_NOP, "Released");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, key, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, key, 0), BLOCK_IN(&f, advance, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, key, 1), BLOCK_IN(&f, released, 0), 0);
    saved_int(&f, BLOCK_PIN(&f, key, 0), KEY_SPACE, NULL);
    saved_input(&f, BLOCK_PIN(&f, advance, 0), NULL, 0, "Advance", NULL);
    saved_object(&f, BLOCK_PIN(&f, advance, 1), f.owner_id);
    saved_object(&f, fixture_target(&f, advance), f.owner_id);

    nmo_object_id_t wait = fixture_block_in(&f, worker, GUID_WAIT_MESSAGE, "WaitAdvance");
    nmo_object_id_t pause = fixture_block_in(&f, worker, GUID_DELAYER, "Pause");
    nmo_object_id_t move = fixture_block_in(&f, worker, GUID_SET_POSITION, "Move");
    nmo_object_id_t random = fixture_block_in(&f, worker, GUID_RANDOM, "Random");
    nmo_object_id_t store = fixture_block_in(&f, worker, GUID_SET_CELL, "Store");
    nmo_object_id_t complete = fixture_block_in(&f, worker, GUID_SEND_MESSAGE, "SendComplete");
    nmo_object_id_t done = fixture_block_in(&f, worker, GUID_NOP, "Done");
    nmo_object_id_t chain[] = {wait, pause, move, random, store, complete, done};
    fixture_link_in(&f, worker, worker_in, BLOCK_IN(&f, wait, 0), 0);
    for (size_t i = 1; i < sizeof(chain) / sizeof(chain[0]); i++) {
        ASSERT_TRUE(chain[i] != 0);
        fixture_link_in(&f, worker, BLOCK_OUT(&f, chain[i - 1], 0), BLOCK_IN(&f, chain[i], 0), 0);
    }
    saved_input(&f, BLOCK_PIN(&f, wait, 0), NULL, 0, "Advance", NULL);
    saved_object(&f, fixture_target(&f, wait), f.owner_id);
    const float delay = 20.0f, position[3] = {10.0f, 20.0f, 30.0f}, maximum = 6.5f;
    saved_input(&f, BLOCK_PIN(&f, pause, 0), &delay, sizeof(delay), NULL, NULL);
    saved_input(&f, BLOCK_PIN(&f, move, 0), position, sizeof(position), NULL, "MovePosition");
    saved_object(&f, fixture_target(&f, move), f.owner_id);
    saved_input(&f, BLOCK_PIN(&f, random, 1), &maximum, sizeof(maximum), NULL, NULL);
    saved_object(&f, fixture_target(&f, store), array);
    saved_int(&f, BLOCK_PIN(&f, store, 0), 0, NULL);
    saved_int(&f, BLOCK_PIN(&f, store, 1), 0, NULL);
    saved_int(&f, BLOCK_PIN(&f, store, 2), 7, "StoreValue");
    saved_input(&f, BLOCK_PIN(&f, complete, 0), NULL, 0, "Complete", NULL);
    saved_object(&f, BLOCK_PIN(&f, complete, 1), f.owner_id);
    saved_object(&f, fixture_target(&f, complete), f.owner_id);
    ASSERT_EQ(NMO_OK, nmo_object_repository_rename(f.repo, BLOCK_POUT(&f, random, 0), "Sample"));

    nmo_object_id_t a = 0, b = 0, sum = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, worker, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_FLOAT, "MinA", &a));
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, worker, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_FLOAT, "MinB", &b));
    const float va = 2.5f, vb = 3.0f;
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, a, (const uint8_t *)&va, sizeof(va)));
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, b, (const uint8_t *)&vb, sizeof(vb)));
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_operation(f.tx, worker, NMO_OP_GUID_ADD, a, b, 0, &sum));
    const nmo_parameteroperation_state_t *op = fixture_state(&f, sum);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(f.tx, nmo_parameteroperation_out_id(op),
                                                        BLOCK_PIN(&f, random, 0)));
    ASSERT_EQ(NMO_OK, nmo_object_repository_rename(f.repo, sum, "Sum"));

    nmo_object_id_t wait_complete = fixture_block_in(&f, observer, GUID_WAIT_MESSAGE, "WaitComplete");
    nmo_object_id_t observed = fixture_block_in(&f, observer, GUID_NOP, "Observed");
    fixture_link_in(&f, observer, observer_in, BLOCK_IN(&f, wait_complete, 0), 0);
    fixture_link_in(&f, observer, BLOCK_OUT(&f, wait_complete, 0), BLOCK_IN(&f, observed, 0), 0);
    saved_input(&f, BLOCK_PIN(&f, wait_complete, 0), NULL, 0, "Complete", NULL);
    saved_object(&f, fixture_target(&f, wait_complete), f.owner_id);

    nmo_object_id_t low = fixture_block_in(&f, clock, GUID_NOP, "Low");
    nmo_object_id_t high = fixture_block_in(&f, clock, GUID_NOP, "High");
    nmo_object_id_t late = fixture_block_in(&f, clock, GUID_NOP, "Late");
    set_priority(&f, low, -5);
    set_priority(&f, high, 5);
    fixture_link_in(&f, clock, clock_in, BLOCK_IN(&f, low, 0), 0);
    fixture_link_in(&f, clock, clock_in, BLOCK_IN(&f, high, 0), 0);
    fixture_link_in(&f, clock, BLOCK_OUT(&f, high, 0), BLOCK_IN(&f, late, 0), 2);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_session_save_file(f.session, path, NULL, NULL));
    fixture_end(&f);
}

/* Names make this baseline independent of object IDs assigned while loading.
 * These expectations describe the scenario, rather than recording a test run:
 * messages arrive after behaviors, delayed links count frames, and Pause counts ms. */
static const char *const expected_trace[FRAME_COUNT] = {
    "begin\nrun WaitAdvance 0\nrun High 0\ndelay Clock 2\nrun Low 0\n"
    "run Key 1\nrun WaitComplete 0\nend\n",
    "begin\narrive Clock\nrun Key 1\nsend SendAdvance Advance\nrun SendAdvance 0\n"
    "receive WaitAdvance Advance\nend\n",
    "begin\nrun Pause 1\nrun Late 0\nrun Key 1\nend\n",
    "begin\nrun Pause 0\nrun Move 0\n"
    "op Sum 0\nrun Random 0\nrun Store 0\nsend SendComplete Complete\n"
    "run SendComplete 0\nrun Done 0\nrun Key 1\nrun Released 0\n"
    "receive WaitComplete Complete\nend\n",
    "begin\nrun WaitAdvance 0\nrun Key 1\nsend SendAdvance Advance\nrun SendAdvance 0\n"
    "run Observed 0\nreceive WaitAdvance Advance\nend\n",
    "begin\nrun Pause 1\nrun Key 1\nrun Released 0\nrun WaitComplete 0\nend\n",
    "begin\nrun Pause 0\nrun Move 0\nop Sum 0\nrun Random 0\n"
    "run Store 0\nsend SendComplete Complete\nrun SendComplete 0\nrun Done 0\n"
    "run Key 1\nreceive WaitComplete Complete\nend\n",
    "begin\nrun Key 1\nrun Observed 0\nend\n",
};

typedef struct replay_trace {
    nmo_engine_t *engine;
    uint32_t frame;
    nmo_object_id_t owner;
    char text[2048];
    size_t length;
    bool invalid;
} replay_trace_t;

static void append_trace(replay_trace_t *trace, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int size = vsnprintf(trace->text + trace->length, sizeof(trace->text) - trace->length, format, args);
    va_end(args);
    if (size < 0 || (size_t)size >= sizeof(trace->text) - trace->length) {
        trace->invalid = true;
        return;
    }
    trace->length += (size_t)size;
}

static void trace_replay(const nmo_engine_event_t *event, void *user_data)
{
    replay_trace_t *trace = user_data;
    if (event->frame != trace->frame || event->time != (double)trace->frame * FRAME_MS) {
        trace->invalid = true;
    }
    const char *name = nmo_engine_object_name(trace->engine, event->behavior_id);
    name = name != NULL ? name : "?";
    switch (event->kind) {
    case NMO_ENGINE_EVENT_FRAME_BEGIN: append_trace(trace, "begin\n"); break;
    case NMO_ENGINE_EVENT_FRAME_END: append_trace(trace, "end\n"); break;
    case NMO_ENGINE_EVENT_EXECUTE: {
        const nmo_script_node_t *node = nmo_engine_script_node(trace->engine, event->behavior_id);
        if (node != NULL && node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK) {
            append_trace(trace, "run %s %d\n", name, event->result);
        }
        break;
    }
    case NMO_ENGINE_EVENT_MESSAGE_SENT:
    case NMO_ENGINE_EVENT_MESSAGE_RECEIVED: {
        const char *message = nmo_engine_message_type_name(trace->engine, event->result);
        trace->invalid |= message == NULL || event->object_id != trace->owner;
        append_trace(trace, "%s %s %s\n", event->kind == NMO_ENGINE_EVENT_MESSAGE_SENT ? "send" : "receive",
                     name, message != NULL ? message : "?");
        break;
    }
    case NMO_ENGINE_EVENT_LINK_DELAYED: append_trace(trace, "delay %s %d\n", name, event->result); break;
    case NMO_ENGINE_EVENT_LINK_ARRIVED: append_trace(trace, "arrive %s\n", name); break;
    case NMO_ENGINE_EVENT_OPERATION: {
        const char *operation = nmo_engine_object_name(trace->engine, event->object_id);
        append_trace(trace, "op %s %d\n", operation != NULL ? operation : "?", event->result);
        break;
    }
    default: break;
    }
}

static float read_float(nmo_engine_t *engine, nmo_object_id_t parameter)
{
    const void *bytes = NULL;
    size_t size = 0;
    float value = 0.0f;
    if (nmo_engine_param_value(engine, parameter, &bytes, &size, NULL) == NMO_OK && size == sizeof(value)) {
        memcpy(&value, bytes, sizeof(value));
    }
    return value;
}

typedef struct replay_result {
    float samples[2];
    bool complete;
} replay_result_t;

static void run_replay(nmo_workspace_t *workspace, uint32_t seed, bool render, replay_result_t *result)
{
    nmo_engine_options_t options = nmo_engine_options_default();
    options.random_seed = seed;
    options.fail_on = NMO_ENGINE_FAIL_ALL;
    nmo_engine_t *engine = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_create(workspace, &options, &engine));
    ASSERT_EQ(NMO_OK, nmo_bb_virtools_register(engine));
    nmo_object_id_t owner = nmo_engine_object_named(engine, "Owner", NMO_CID_3DENTITY);
    nmo_object_id_t table = nmo_engine_object_named(engine, "Table", NMO_CID_DATAARRAY);
    nmo_object_id_t worker = nmo_engine_object_named(engine, "Worker", NMO_CID_BEHAVIOR);
    nmo_object_id_t observer = nmo_engine_object_named(engine, "Observer", NMO_CID_BEHAVIOR);
    nmo_object_id_t position = nmo_engine_object_named(engine, "MovePosition", NMO_CID_PARAMETER);
    nmo_object_id_t value = nmo_engine_object_named(engine, "StoreValue", NMO_CID_PARAMETER);
    nmo_object_id_t sample = nmo_engine_object_named(engine, "Sample", NMO_CID_PARAMETEROUT);
    ASSERT_TRUE(owner != 0 && table != 0 && worker != 0 && observer != 0);
    ASSERT_TRUE(position != 0 && value != 0 && sample != 0);
    ASSERT_EQ(NMO_OK, nmo_engine_start(engine, NMO_ENGINE_START_ALL));
    replay_trace_t trace = {.engine = engine, .owner = owner};
    nmo_engine_set_trace(engine, trace_replay, &trace);
    static const bool keys[FRAME_COUNT] = {false, true, true, false, true, false, false, false};
    static const uint32_t cells[FRAME_COUNT] = {0, 0, 0, 7, 7, 7, 11, 11};
    static const bool worker_active[FRAME_COUNT] = {true, true, true, false, true, true, false, false};
    static const bool observer_active[FRAME_COUNT] = {true, true, true, true, false, true, true, false};
    for (uint32_t frame = 0; frame < FRAME_COUNT; frame++) {
        trace.frame = frame;
        trace.length = 0;
        trace.text[0] = '\0';
        nmo_engine_set_key(engine, KEY_SPACE, keys[frame]);
        if (frame == 4) {
            ASSERT_EQ(NMO_OK, nmo_engine_activate_script(engine, worker, true, true));
        }
        if (frame == 5) {
            ASSERT_EQ(NMO_OK, nmo_engine_activate_script(engine, observer, true, true));
        }
        if (frame == 6) {
            const float next[3] = {-4.0f, 5.0f, 6.0f};
            const int32_t eleven = 11;
            ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(engine, position, next, sizeof(next)));
            ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(engine, value, &eleven, sizeof(eleven)));
        }
        nmo_status_t status = nmo_engine_step(engine, FRAME_MS);
        if (status != NMO_OK) {
            nmo_engine_event_t failure;
            if (nmo_engine_failure(engine, &failure)) {
                nmo_object_id_t id = failure.behavior_id != 0 ? failure.behavior_id : failure.object_id;
                fprintf(stderr, "Replay frame %u: %s at %s, result %d\n", frame,
                         nmo_engine_event_kind_name(failure.kind), nmo_engine_object_name(engine, id),
                         failure.result);
            }
        }
        ASSERT_EQ(NMO_OK, status);
        if (render) {
            nmo_engine_frame_t view;
            ASSERT_EQ(NMO_OK, nmo_engine_render_frame(engine, &view));
            ASSERT_EQ(NMO_OK, nmo_engine_render_frame(engine, &view));
        }
        ASSERT_FALSE(trace.invalid);
        if (strcmp(expected_trace[frame], trace.text) != 0) {
            fprintf(stderr, "Replay frame %u (seed %u, render %d):\n%s", frame, seed, render, trace.text);
        }
        ASSERT_STR_EQ(expected_trace[frame], trace.text);
        ASSERT_FALSE(nmo_engine_failure(engine, NULL));
        ASSERT_EQ(frame + 1u, nmo_engine_frame(engine));
        ASSERT_TRUE(nmo_engine_time(engine) == (double)(frame + 1u) * FRAME_MS);
        ASSERT_EQ(keys[frame], nmo_engine_is_key_down(engine, KEY_SPACE));
        ASSERT_EQ(worker_active[frame], nmo_engine_behavior_is_active(engine, worker));
        ASSERT_EQ(observer_active[frame], nmo_engine_behavior_is_active(engine, observer));
        nmo_engine_cell_t cell;
        ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(engine, table, 0, 0, &cell));
        ASSERT_EQ(cells[frame], cell.value);
        float world[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(engine, owner, world));
        const float *expected = frame < 3 ? (const float[3]){0, 0, 0}
                                         : frame < 6 ? (const float[3]){10, 20, 30}
                                                     : (const float[3]){-4, 5, 6};
        for (int axis = 0; axis < 3; axis++) {
            ASSERT_FLOAT_EQ(expected[axis], world[12 + axis], 0.000001f);
        }
        if (frame == 3 || frame == 6) {
            float random = read_float(engine, sample);
            ASSERT_TRUE(random >= 5.5f && random < 6.5f);
            result->samples[frame == 3 ? 0 : 1] = random;
        }
    }
    nmo_engine_destroy(engine);
    result->complete = true;
}

TEST(engine_replay, saved_graph_replays_with_fixed_inputs_and_isolated_engine_state)
{
    const char *path = NMO_TEST_SCRATCH_FILE("engine_replay.cmo");
    write_replay_file(path);
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, path, NULL, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    replay_result_t first = {0}, second = {0}, other_seed = {0};
    run_replay(workspace, 7, false, &first);
    if (!first.complete) goto cleanup;
    run_replay(workspace, 7, true, &second);
    if (!second.complete) goto cleanup;
    run_replay(workspace, 8, false, &other_seed);
    if (!other_seed.complete) goto cleanup;
    ASSERT_TRUE(first.samples[0] == second.samples[0] && first.samples[1] == second.samples[1]);
    ASSERT_TRUE(first.samples[0] != other_seed.samples[0] || first.samples[1] != other_seed.samples[1]);
    ASSERT_TRUE(first.samples[0] != first.samples[1]);
cleanup:
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_replay, saved_graph_replays_with_fixed_inputs_and_isolated_engine_state);
TEST_MAIN_END()
