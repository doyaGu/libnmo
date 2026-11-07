/**
 * @file test_engine_logics.c
 * @brief Logics.dll blocks of the streaming, loop, interpolator and message
 *        families, run in graphs: which outputs open (each feeds a Nop whose
 *        runs are counted) and what the outputs hold.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "core/nmo_array.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_ref.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_synchro_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <math.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_WHILE NMO_GUID(0x010015a6, 0xef597665)
#define GUID_BOOL_EVENT NMO_GUID(0xfefefefe, 0xfefefefe)
#define GUID_SPECIFIC_BOOL_EVENT NMO_GUID(0xe5f22514, 0x12dfffee)
#define GUID_TRIGGER_EVENT NMO_GUID(0x3c3f7044, 0x0e917d1a)
#define GUID_PRIORITY NMO_GUID(0xffb40d68, 0x45455444)
#define GUID_FIFO NMO_GUID(0x7242794c, 0x98c2b340)
#define GUID_LIFO NMO_GUID(0x7d037385, 0x12d62259)
#define GUID_STOP_AND_GO NMO_GUID(0x78a667f0, 0x5a3c0323)
#define GUID_CHRONO NMO_GUID(0x5e2f1788, 0x4f965a44)
#define GUID_HIERARCHY_PARSER NMO_GUID(0x5f4a214b, 0x6817452c)
#define GUID_INTERPOLATOR_MATRIX NMO_GUID(0x03184d55, 0x7d7a0509)
#define GUID_INTERPOLATOR_ORIENTATION NMO_GUID(0x17171704, 0x17171704)
#define GUID_INTERPOLATOR_INT NMO_GUID(0x17171701, 0x17171701)
#define GUID_WAIT_MESSAGE NMO_GUID(0x4587ffee, 0x4587ffdd)
#define GUID_SEND_MESSAGE NMO_GUID(0xa20e8d5b, 0xdf002150)
#define GUID_GET_MESSAGE_DATA NMO_GUID(0x45875fee, 0x45875fdd)
#define GUID_CHECK_FOR_MESSAGE NMO_GUID(0x61226639, 0x6d390cb7)
#define GUID_ADD_OBJECT_TO_GROUP NMO_GUID(0x69aa320b, 0x023829c7)
#define GUID_REMOVE_OBJECT_FROM_GROUP NMO_GUID(0x3c94747a, 0x50882825)
#define GUID_REMOVE_FROM_GROUP NMO_GUID(0xd0147412, 0x0258520d)
#define GUID_GROUP_CLEAR NMO_GUID(0x1d12662e, 0x42687a3c)
#define GUID_GROUP_OPERATOR NMO_GUID(0x7b377625, 0x16da3bda)
#define GUID_RENDEZVOUS NMO_GUID(0x3a1a16c7, 0x20095c45)
#define GUID_ENTER_CRITICAL_SECTION NMO_GUID(0x3c442002, 0x5988257c)
#define GUID_LEAVE_CRITICAL_SECTION NMO_GUID(0x45db3138, 0x4db80f47)

#define STEP_MS 10.0f

static nmo_object_id_t block(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    return fixture_block(f, guid, name); /* 0 fails the caller's next check */
}

/* A Nop fed by output `pos` of `from` (an IO when `from` is one) after `delay` frames */
static nmo_object_id_t nop_after(engine_fixture_t *f, nmo_object_id_t from_io, uint32_t delay)
{
    nmo_object_id_t nop = block(f, GUID_NOP, "Nop");
    fixture_link(f, from_io, BLOCK_IN(f, nop, 0), delay);
    return nop;
}

/* A targetable block acting on the fixture's owner */
static void target_owner(engine_fixture_t *f, nmo_object_id_t behavior)
{
    nmo_object_id_t target = fixture_target(f, behavior);
    ASSERT_TRUE(target != 0);
    fixture_input_int(f, target, (int32_t)f->owner_id);
}

static void start(engine_fixture_t *f)
{
    fixture_commit(f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f->engine, NMO_ENGINE_START_ALL));
}

static void step(engine_fixture_t *f, int frames)
{
    for (int i = 0; i < frames; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, STEP_MS));
    }
}

TEST(engine_logics, while_and_bool_events_follow_their_conditions)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t loop = block(&f, GUID_WHILE, "While");
    fixture_input_int(&f, BLOCK_PIN(&f, loop, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, loop, 0), 0);
    nmo_object_id_t done = nop_after(&f, BLOCK_OUT(&f, loop, 0), 0);
    nmo_object_id_t looped = nop_after(&f, BLOCK_OUT(&f, loop, 1), 0);
    /* Bool Event and Specific Bool Event see TRUE after their FALSE start */
    nmo_object_id_t event = block(&f, GUID_BOOL_EVENT, "Bool Event");
    fixture_input_int(&f, BLOCK_PIN(&f, event, 0), 1);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, event, 0), 0);
    nmo_object_id_t changed = nop_after(&f, BLOCK_OUT(&f, event, 0), 0);
    nmo_object_id_t same = nop_after(&f, BLOCK_OUT(&f, event, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, event, 0), BLOCK_IN(&f, event, 0), 1);
    nmo_object_id_t specific = block(&f, GUID_SPECIFIC_BOOL_EVENT, "Specific Bool Event");
    fixture_input_int(&f, BLOCK_PIN(&f, specific, 0), 1);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, specific, 0), 0);
    nmo_object_id_t rose = nop_after(&f, BLOCK_OUT(&f, specific, 1), 0);
    nmo_object_id_t still_true = nop_after(&f, BLOCK_OUT(&f, specific, 2), 0);
    fixture_link(&f, BLOCK_OUT(&f, specific, 1), BLOCK_IN(&f, specific, 0), 1);
    start(&f);

    step(&f, 3);
    ASSERT_EQ(1u, runs(&f, done));
    ASSERT_EQ(0u, runs(&f, looped));
    /* The first In changes FALSE to TRUE; the second sees TRUE again */
    ASSERT_EQ(1u, runs(&f, changed));
    ASSERT_EQ(1u, runs(&f, same));
    ASSERT_EQ(1u, runs(&f, rose));
    ASSERT_EQ(1u, runs(&f, still_true));
    fixture_end(&f);
}

TEST(engine_logics, fifo_and_lifo_release_their_inputs_in_order)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t queues[2] = {block(&f, GUID_FIFO, "FIFO"), block(&f, GUID_LIFO, "LIFO")};
    nmo_object_id_t first[2], second[2], unstacked[2];
    /* In 1 and In 2 on the first frame, Unstack on the third and the fifth */
    nmo_object_id_t later = nop_after(&f, f.script_in, 2);
    nmo_object_id_t later2 = nop_after(&f, BLOCK_OUT(&f, later, 0), 2);
    for (int q = 0; q < 2; q++) {
        fixture_link(&f, f.script_in, BLOCK_IN(&f, queues[q], 1), 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, queues[q], 2), 0);
        fixture_link(&f, BLOCK_OUT(&f, later, 0), BLOCK_IN(&f, queues[q], 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, later2, 0), BLOCK_IN(&f, queues[q], 0), 0);
        unstacked[q] = nop_after(&f, BLOCK_OUT(&f, queues[q], 0), 0);
        first[q] = nop_after(&f, BLOCK_OUT(&f, queues[q], 1), 0);
        second[q] = nop_after(&f, BLOCK_OUT(&f, queues[q], 2), 0);
    }
    start(&f);

    step(&f, 1);
    for (int q = 0; q < 2; q++) {
        ASSERT_EQ(0u, runs(&f, first[q]) + runs(&f, second[q]));
    }
    step(&f, 2);
    ASSERT_EQ(1u, runs(&f, unstacked[0]));
    ASSERT_EQ(1u, runs(&f, first[0]));  /* FIFO: In 1 came first */
    ASSERT_EQ(0u, runs(&f, second[0]));
    ASSERT_EQ(0u, runs(&f, first[1]));
    ASSERT_EQ(1u, runs(&f, second[1])); /* LIFO: In 2 came last */
    step(&f, 3);
    for (int q = 0; q < 2; q++) {
        ASSERT_EQ(2u, runs(&f, unstacked[q]));
        ASSERT_EQ(1u, runs(&f, first[q]));
        ASSERT_EQ(1u, runs(&f, second[q]));
    }
    fixture_end(&f);
}

TEST(engine_logics, priority_locks_lower_inputs_until_reset)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t priority = block(&f, GUID_PRIORITY, "Priority");
    /* In 2 on frame 1, In 1 on frame 2 (locked out), Reset on frame 3, In 1 on frame 4 */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, priority, 2), 0);
    nmo_object_id_t f2 = nop_after(&f, f.script_in, 1);
    nmo_object_id_t f3 = nop_after(&f, BLOCK_OUT(&f, f2, 0), 1);
    nmo_object_id_t f4 = nop_after(&f, BLOCK_OUT(&f, f3, 0), 1);
    fixture_link(&f, BLOCK_OUT(&f, f2, 0), BLOCK_IN(&f, priority, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, f3, 0), BLOCK_IN(&f, priority, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, f4, 0), BLOCK_IN(&f, priority, 1), 0);
    nmo_object_id_t reset = nop_after(&f, BLOCK_OUT(&f, priority, 0), 0);
    nmo_object_id_t out1 = nop_after(&f, BLOCK_OUT(&f, priority, 1), 0);
    nmo_object_id_t out2 = nop_after(&f, BLOCK_OUT(&f, priority, 2), 0);
    start(&f);

    step(&f, 2);
    ASSERT_EQ(1u, runs(&f, out2));
    ASSERT_EQ(0u, runs(&f, out1));
    step(&f, 3);
    ASSERT_EQ(1u, runs(&f, reset));
    ASSERT_EQ(1u, runs(&f, out1));
    ASSERT_EQ(1u, runs(&f, out2));
    fixture_end(&f);
}

TEST(engine_logics, trigger_event_stop_and_go_and_chrono_follow_activity)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t trigger = block(&f, GUID_TRIGGER_EVENT, "Trigger Event");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, trigger, 0), 0);
    nmo_object_id_t activated = nop_after(&f, BLOCK_OUT(&f, trigger, 0), 0);
    nmo_object_id_t deactivated = nop_after(&f, BLOCK_OUT(&f, trigger, 1), 0);
    nmo_object_id_t stop_go = block(&f, GUID_STOP_AND_GO, "Stop & Go");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, stop_go, 0), 0);
    nmo_object_id_t went = nop_after(&f, BLOCK_OUT(&f, stop_go, 0), 0);
    nmo_object_id_t chrono = block(&f, GUID_CHRONO, "Chrono");
    fixture_link(&f, f.script_in, BLOCK_IN(&f, chrono, 0), 0);
    nmo_object_id_t off = nop_after(&f, f.script_in, 3);
    fixture_link(&f, BLOCK_OUT(&f, off, 0), BLOCK_IN(&f, chrono, 1), 0);
    nmo_object_id_t stopped = nop_after(&f, BLOCK_OUT(&f, chrono, 1), 0);
    start(&f);

    step(&f, 1);
    ASSERT_EQ(1u, runs(&f, activated));
    ASSERT_EQ(0u, runs(&f, deactivated));
    ASSERT_EQ(0u, runs(&f, went));
    step(&f, 1);
    ASSERT_EQ(1u, runs(&f, activated));
    ASSERT_EQ(1u, runs(&f, deactivated));
    ASSERT_EQ(1u, runs(&f, went));
    step(&f, 4);
    ASSERT_EQ(1u, runs(&f, stopped));
    /* Each frame Chrono ran added the frame time, the first from 0 */
    float elapsed = 0.0f;
    read_value(&f, BLOCK_POUT(&f, chrono, 0), &elapsed, sizeof(elapsed));
    ASSERT_FLOAT_EQ(STEP_MS * (float)runs(&f, chrono), elapsed, 0.0f);
    ASSERT_TRUE(runs(&f, chrono) >= 3u);
    fixture_end(&f);
}

static nmo_object_id_t child_entity(engine_fixture_t *f, const char *name, nmo_object_id_t parent)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DENTITY, name);
    nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    state->parent = nmo_ref_from_id(parent);
    state->has_entityndata_chunk = 1;
    return id;
}

TEST(engine_logics, hierarchy_parser_walks_the_target_depth_first)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = child_entity(&f, "A", f.owner_id);
    nmo_object_id_t b = child_entity(&f, "B", f.owner_id);
    nmo_object_id_t c = child_entity(&f, "C", a);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t parser = block(&f, GUID_HIERARCHY_PARSER, "Hierarchy Parser");
    target_owner(&f, parser);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, parser, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, parser, 1), BLOCK_IN(&f, parser, 1), 1);
    nmo_object_id_t done = nop_after(&f, BLOCK_OUT(&f, parser, 0), 0);
    start(&f);

    const nmo_object_id_t order[] = {f.owner_id, a, c, b};
    const int32_t depths[] = {0, 1, 2, 1};
    for (int i = 0; i < 4; i++) {
        step(&f, 1);
        uint32_t current = 0;
        int32_t depth = -1;
        read_value(&f, BLOCK_POUT(&f, parser, 0), &current, sizeof(current));
        read_value(&f, BLOCK_POUT(&f, parser, 1), &depth, sizeof(depth));
        ASSERT_EQ((uint32_t)order[i], current);
        ASSERT_EQ(depths[i], depth);
        ASSERT_EQ(0u, runs(&f, done));
    }
    step(&f, 1);
    ASSERT_EQ(1u, runs(&f, done));
    fixture_end(&f);
}

/* A row vector rotation of `angle` around Y, then a translation */
static void rotation_y(float m[4][4], float angle, float scale, float tx)
{
    memset(m, 0, 16 * sizeof(float));
    m[0][0] = cosf(angle) * scale;
    m[0][2] = -sinf(angle) * scale;
    m[1][1] = scale;
    m[2][0] = sinf(angle) * scale;
    m[2][2] = cosf(angle) * scale;
    m[3][0] = tx;
    m[3][3] = 1.0f;
}

TEST(engine_logics, interpolators_blend_matrices_orientations_and_integers)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t from = fixture_create(&f, NMO_CID_3DENTITY, "From");
    nmo_object_id_t to = fixture_create(&f, NMO_CID_3DENTITY, "To");
    float m[4][4];
    rotation_y(m, 0.0f, 1.0f, 0.0f);
    nmo_3dentity_state_t *from_state = (nmo_3dentity_state_t *)fixture_state(&f, from);
    memcpy(from_state->world_matrix, m, sizeof(m));
    from_state->has_entityndata_chunk = 1;
    rotation_y(m, 1.5707963f, 3.0f, 7.0f);
    nmo_3dentity_state_t *to_state = (nmo_3dentity_state_t *)fixture_state(&f, to);
    memcpy(to_state->world_matrix, m, sizeof(m));
    to_state->has_entityndata_chunk = 1;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);

    nmo_object_id_t matrix = block(&f, GUID_INTERPOLATOR_MATRIX, "Interpolator Matrix");
    float a[4][4], c[4][4];
    rotation_y(a, 0.0f, 1.0f, 0.0f);
    rotation_y(c, 1.5707963f, 2.0f, 10.0f);
    const float half = 0.5f;
    nmo_object_id_t a_param = 0, c_param = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, BLOCK_PIN(&f, matrix, 0), &a_param));
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f.tx, BLOCK_PIN(&f, matrix, 1), &c_param));
    fixture_input_value(&f, BLOCK_PIN(&f, matrix, 2), &half, sizeof(half));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, matrix, 0), 0);

    nmo_object_id_t orientation = block(&f, GUID_INTERPOLATOR_ORIENTATION, "Interpolator Object Orientation");
    fixture_input_int(&f, BLOCK_PIN(&f, orientation, 0), (int32_t)from);
    fixture_input_int(&f, BLOCK_PIN(&f, orientation, 1), (int32_t)to);
    fixture_input_value(&f, BLOCK_PIN(&f, orientation, 2), &half, sizeof(half));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, orientation, 0), 0);

    nmo_object_id_t integer = block(&f, GUID_INTERPOLATOR_INT, "Interpolator Int");
    fixture_input_int(&f, BLOCK_PIN(&f, integer, 0), 10);
    fixture_input_int(&f, BLOCK_PIN(&f, integer, 1), 21);
    fixture_input_value(&f, BLOCK_PIN(&f, integer, 2), &half, sizeof(half));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, integer, 0), 0);
    start(&f);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, a_param, a, sizeof(a)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, c_param, c, sizeof(c)));

    step(&f, 1);
    float r[4][4];
    read_value(&f, BLOCK_POUT(&f, matrix, 0), r, sizeof(r));
    /* Half way: 45 degrees, scale 1.5, at x 5 */
    const float c45 = 0.70710678f;
    ASSERT_FLOAT_EQ(c45 * 1.5f, r[0][0], 1e-5f);
    ASSERT_FLOAT_EQ(-c45 * 1.5f, r[0][2], 1e-5f);
    ASSERT_FLOAT_EQ(1.5f, r[1][1], 1e-5f);
    ASSERT_FLOAT_EQ(5.0f, r[3][0], 0.0f);
    ASSERT_FLOAT_EQ(1.0f, r[3][3], 0.0f);
    /* Orientations only: unit rows, no translation */
    read_value(&f, BLOCK_POUT(&f, orientation, 0), r, sizeof(r));
    ASSERT_FLOAT_EQ(c45, r[0][0], 1e-5f);
    ASSERT_FLOAT_EQ(-c45, r[0][2], 1e-5f);
    ASSERT_FLOAT_EQ(1.0f, r[1][1], 1e-5f);
    ASSERT_FLOAT_EQ(0.0f, r[3][0], 0.0f);
    int32_t value = 0;
    read_value(&f, BLOCK_POUT(&f, integer, 0), &value, sizeof(value));
    ASSERT_EQ(15, value); /* truncated */
    fixture_end(&f);
}

TEST(engine_logics, message_data_carries_the_senders_parameters)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    const char *const names[] = {"OnClick"};
    fixture_install_messages(&f, names, 1);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t wait = block(&f, GUID_WAIT_MESSAGE, "Wait Message");
    target_owner(&f, wait);
    fixture_input_int(&f, BLOCK_PIN(&f, wait, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, wait, 0), 0);
    nmo_object_id_t send = block(&f, GUID_SEND_MESSAGE, "Send Message");
    target_owner(&f, send);
    fixture_input_int(&f, BLOCK_PIN(&f, send, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, send, 1), (int32_t)f.owner_id);
    nmo_object_id_t data_in = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, send, NMO_SCRIPT_EDIT_PARAM_IN,
                                                    CKPGUID_INT, "Data", &data_in));
    fixture_input_int(&f, data_in, 42);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, send, 0), 0);
    nmo_object_id_t data = block(&f, GUID_GET_MESSAGE_DATA, "Get Message Data");
    target_owner(&f, data);
    fixture_input_int(&f, BLOCK_PIN(&f, data, 0), 0);
    nmo_object_id_t data_out = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, data, NMO_SCRIPT_EDIT_PARAM_OUT,
                                                    CKPGUID_INT, "Data", &data_out));
    fixture_link(&f, BLOCK_OUT(&f, wait, 0), BLOCK_IN(&f, data, 0), 0);
    nmo_object_id_t check = block(&f, GUID_CHECK_FOR_MESSAGE, "Check For Message");
    fixture_input_int(&f, BLOCK_PIN(&f, check, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, wait, 0), BLOCK_IN(&f, check, 0), 0);
    nmo_object_id_t arrived = nop_after(&f, BLOCK_OUT(&f, check, 1), 0);
    start(&f);

    step(&f, 3);
    ASSERT_EQ(1u, runs(&f, data));
    uint32_t sender = 0;
    int32_t value = 0, count = 0;
    read_value(&f, BLOCK_POUT(&f, data, 0), &sender, sizeof(sender));
    read_value(&f, data_out, &value, sizeof(value));
    ASSERT_EQ((uint32_t)f.owner_id, sender);
    ASSERT_EQ(42, value);
    ASSERT_EQ(1u, runs(&f, arrived));
    read_value(&f, BLOCK_POUT(&f, check, 0), &count, sizeof(count));
    ASSERT_EQ(1, count);
    fixture_end(&f);
}

/* A group of `count` objects */
static nmo_object_id_t make_group(engine_fixture_t *f, const char *name, const nmo_object_id_t *objects,
                                  size_t count)
{
    nmo_object_id_t group = fixture_create(f, NMO_CID_GROUP, name);
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(f, group);
    nmo_ref_t *members = NULL;
    if (state == NULL || (count > 0 && nmo_array_extend(&state->object_ids, count, (void **)&members) != NMO_OK)) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        members[i] = nmo_ref_from_id(objects[i]);
    }
    return group;
}

/* A block's target set to `object` */
static void target_of(engine_fixture_t *f, nmo_object_id_t behavior, nmo_object_id_t object)
{
    nmo_object_id_t target = fixture_target(f, behavior);
    ASSERT_TRUE(target != 0);
    fixture_input_int(f, target, (int32_t)object);
}

static bool in_group(engine_fixture_t *f, nmo_object_id_t group, nmo_object_id_t object)
{
    for (int32_t i = 0; i < nmo_engine_group_count(f->engine, group); i++) {
        if (nmo_engine_group_object(f->engine, group, i) == object) {
            return true;
        }
    }
    return false;
}

TEST(engine_logics, group_blocks_add_remove_combine_and_clear)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t third = fixture_create(&f, NMO_CID_3DENTITY, "Third");
    const nmo_object_id_t both[2] = {f.owner_id, third};
    nmo_object_id_t g = make_group(&f, "G", NULL, 0);
    nmo_object_id_t h = make_group(&f, "H", both, 2);
    nmo_object_id_t k = make_group(&f, "K", both, 1);
    ASSERT_TRUE(other != 0 && third != 0 && g != 0 && h != 0 && k != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t add = block(&f, GUID_ADD_OBJECT_TO_GROUP, "Add Object To Group");
    nmo_object_id_t unite = block(&f, GUID_GROUP_OPERATOR, "Union");
    nmo_object_id_t remove = block(&f, GUID_REMOVE_FROM_GROUP, "Remove From Group");
    nmo_object_id_t remove_object = block(&f, GUID_REMOVE_OBJECT_FROM_GROUP, "Remove Object");
    nmo_object_id_t clear = block(&f, GUID_GROUP_CLEAR, "Group Clear");
    ASSERT_TRUE(add != 0 && unite != 0 && remove != 0 && remove_object != 0 && clear != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, add, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, add, 0), BLOCK_IN(&f, unite, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, unite, 0), BLOCK_IN(&f, remove, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, remove, 0), BLOCK_IN(&f, remove_object, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, remove_object, 0), BLOCK_IN(&f, clear, 0), 0);
    /* G gets Other; G union H: Other, Owner, Third; the owner leaves; then Third */
    fixture_input_int(&f, BLOCK_PIN(&f, add, 0), (int32_t)g);
    fixture_input_int(&f, BLOCK_PIN(&f, add, 1), (int32_t)other);
    target_of(&f, unite, g);
    fixture_input_int(&f, BLOCK_PIN(&f, unite, 0), 1); /* CKUNION */
    nmo_parameterin_state_t *set = (nmo_parameterin_state_t *)fixture_state(&f, BLOCK_PIN(&f, unite, 1));
    ASSERT_TRUE(set != NULL);
    set->type_guid = CKPGUID_GROUP;
    fixture_input_int(&f, BLOCK_PIN(&f, unite, 1), (int32_t)h);
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 0), (int32_t)g);
    target_of(&f, remove, f.owner_id);
    fixture_input_int(&f, BLOCK_PIN(&f, remove_object, 0), (int32_t)g);
    fixture_input_int(&f, BLOCK_PIN(&f, remove_object, 1), (int32_t)third);
    target_of(&f, clear, k);
    start(&f);

    step(&f, 1);
    ASSERT_EQ(1, nmo_engine_group_count(f.engine, g));
    ASSERT_TRUE(in_group(&f, g, other));
    ASSERT_EQ(2, nmo_engine_group_count(f.engine, h));
    ASSERT_EQ(0, nmo_engine_group_count(f.engine, k));
    fixture_end(&f);
}

TEST(engine_logics, rendezvous_and_critical_sections_hold_objects_back)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t meeting = fixture_create(&f, NMO_CID_SYNCHRO, "Meeting");
    nmo_object_id_t alone = fixture_create(&f, NMO_CID_SYNCHRO, "Alone");
    nmo_object_id_t section = fixture_create(&f, NMO_CID_CRITICALSECTION, "Section");
    nmo_synchro_state_t *meeting_state = (nmo_synchro_state_t *)fixture_state(&f, meeting);
    nmo_synchro_state_t *alone_state = (nmo_synchro_state_t *)fixture_state(&f, alone);
    ASSERT_TRUE(other != 0 && section != 0 && meeting_state != NULL && alone_state != NULL);
    meeting_state->max_waiters = 2;
    alone_state->max_waiters = 1;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* A rendezvous of one passes at once; of two, the owner waits for Other a frame later */
    nmo_object_id_t solo = block(&f, GUID_RENDEZVOUS, "Solo");
    target_of(&f, solo, f.owner_id);
    fixture_input_int(&f, BLOCK_PIN(&f, solo, 0), (int32_t)alone);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, solo, 0), 0);
    nmo_object_id_t solo_out = nop_after(&f, BLOCK_OUT(&f, solo, 0), 0);
    nmo_object_id_t first = block(&f, GUID_RENDEZVOUS, "First");
    nmo_object_id_t second = block(&f, GUID_RENDEZVOUS, "Second");
    target_of(&f, first, f.owner_id);
    target_of(&f, second, other);
    fixture_input_int(&f, BLOCK_PIN(&f, first, 0), (int32_t)meeting);
    fixture_input_int(&f, BLOCK_PIN(&f, second, 0), (int32_t)meeting);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, first, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, second, 0), 2);
    nmo_object_id_t first_out = nop_after(&f, BLOCK_OUT(&f, first, 0), 0);
    nmo_object_id_t second_out = nop_after(&f, BLOCK_OUT(&f, second, 0), 0);
    /* The owner holds the section frames 1 to 3; Other asks from frame 2 */
    nmo_object_id_t enter = block(&f, GUID_ENTER_CRITICAL_SECTION, "Enter");
    nmo_object_id_t leave = block(&f, GUID_LEAVE_CRITICAL_SECTION, "Leave");
    nmo_object_id_t wait = block(&f, GUID_ENTER_CRITICAL_SECTION, "Enter Other");
    target_of(&f, enter, f.owner_id);
    target_of(&f, leave, f.owner_id);
    target_of(&f, wait, other);
    fixture_input_int(&f, BLOCK_PIN(&f, enter, 0), (int32_t)section);
    fixture_input_int(&f, BLOCK_PIN(&f, leave, 0), (int32_t)section);
    fixture_input_int(&f, BLOCK_PIN(&f, wait, 0), (int32_t)section);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, enter, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, enter, 0), BLOCK_IN(&f, leave, 0), 2);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, wait, 0), 1);
    nmo_object_id_t entered = nop_after(&f, BLOCK_OUT(&f, enter, 0), 0);
    nmo_object_id_t waited = nop_after(&f, BLOCK_OUT(&f, wait, 0), 0);
    start(&f);

    step(&f, 2);
    ASSERT_EQ(1u, runs(&f, solo_out));
    ASSERT_EQ(0u, runs(&f, first_out));
    ASSERT_EQ(1u, runs(&f, entered));
    ASSERT_EQ(0u, runs(&f, waited));
    step(&f, 3);
    ASSERT_EQ(1u, runs(&f, first_out));
    ASSERT_EQ(1u, runs(&f, second_out));
    ASSERT_EQ(1u, runs(&f, waited));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_logics, while_and_bool_events_follow_their_conditions);
    REGISTER_TEST(engine_logics, fifo_and_lifo_release_their_inputs_in_order);
    REGISTER_TEST(engine_logics, priority_locks_lower_inputs_until_reset);
    REGISTER_TEST(engine_logics, trigger_event_stop_and_go_and_chrono_follow_activity);
    REGISTER_TEST(engine_logics, hierarchy_parser_walks_the_target_depth_first);
    REGISTER_TEST(engine_logics, interpolators_blend_matrices_orientations_and_integers);
    REGISTER_TEST(engine_logics, message_data_carries_the_senders_parameters);
    REGISTER_TEST(engine_logics, group_blocks_add_remove_combine_and_clear);
    REGISTER_TEST(engine_logics, rendezvous_and_critical_sections_hold_objects_back);
TEST_MAIN_END()
