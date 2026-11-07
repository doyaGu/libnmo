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
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
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
#define GUID_PROXIMITY NMO_GUID(0x5321cacb, 0xdcdc5213)
#define GUID_MODIFY_COMPONENT NMO_GUID(0xe5b234f3, 0x35fbacf7)
#define GUID_RAY_BOX_INTERSECTION NMO_GUID(0x671e5a87, 0x383b2372)
#define GUID_OBJECT_BETWEEN NMO_GUID(0x45484b53, 0x08eb1fab)
#define GUID_IS_IN_VIEW_FRUSTUM NMO_GUID(0x45542a7f, 0x4c2f01a6)

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

/* A 3D object at `x`, `z` with a unit cube mesh (vertices only: boxes, no faces) */
static nmo_object_id_t cube_at(engine_fixture_t *f, const char *name, float x, float z)
{
    nmo_object_id_t object = fixture_create(f, NMO_CID_3DOBJECT, name);
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, object);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (entity == NULL || state == NULL) {
        return 0;
    }
    memset(entity->world_matrix, 0, sizeof(entity->world_matrix));
    entity->world_matrix[0] = entity->world_matrix[5] = entity->world_matrix[10] = 1.0f;
    entity->world_matrix[15] = 1.0f;
    entity->world_matrix[12] = x;
    entity->world_matrix[14] = z;
    entity->has_entityndata_chunk = 1;
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f->session),
                                                      8u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    if (state->vertices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 8u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 8; i++) {
        state->vertices[i].position =
            (nmo_vector_t){(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f};
    }
    state->vertex_count = 8;
    entity->current_mesh = nmo_ref_from_id(mesh);
    return object;
}

/* A local parameter of a block, by position */
static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t behavior, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, behavior);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

TEST(engine_logics, proximity_and_modify_component)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t near_cube = cube_at(&f, "Near", 3.0f, 0.0f);
    ASSERT_TRUE(near_cube != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    /* The owner (at the origin) and Near, 3 apart: within 5, not within 2 */
    nmo_object_id_t within = block(&f, GUID_PROXIMITY, "Within 5");
    nmo_object_id_t beyond = block(&f, GUID_PROXIMITY, "Within 2");
    const float five = 5.0f, two = 2.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, within, 0), &five, sizeof(five));
    fixture_input_value(&f, BLOCK_PIN(&f, beyond, 0), &two, sizeof(two));
    for (int i = 0; i < 2; i++) {
        nmo_object_id_t p = i == 0 ? within : beyond;
        fixture_input_int(&f, BLOCK_PIN(&f, p, 1), (int32_t)f.owner_id);
        fixture_input_int(&f, BLOCK_PIN(&f, p, 2), (int32_t)near_cube);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, p, 0), 0);
    }
    nmo_object_id_t in_range = nop_after(&f, BLOCK_OUT(&f, within, 0), 0);
    nmo_object_id_t out_range = nop_after(&f, BLOCK_OUT(&f, beyond, 1), 0);
    /* Modify Component of a vector: Y only */
    nmo_object_id_t modify = block(&f, GUID_MODIFY_COMPONENT, "Modify Component");
    const float vin[3] = {1.0f, 2.0f, 3.0f}, nine = 9.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, modify, 0), vin, sizeof(vin));
    nmo_object_id_t y = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, modify, NMO_SCRIPT_EDIT_PARAM_IN,
                                                    CKPGUID_FLOAT, "Y", &y));
    fixture_input_value(&f, y, &nine, sizeof(nine));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, modify, 0), 0);
    nmo_object_id_t settings = local_of(&f, modify, 0);
    ASSERT_TRUE(settings != 0);
    start(&f);
    const uint32_t flags = 0x70000004u; /* a vector, Modify Y */
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, settings, &flags, sizeof(flags)));

    step(&f, 1);
    ASSERT_EQ(1u, runs(&f, in_range));
    ASSERT_EQ(1u, runs(&f, out_range));
    float distance = 0.0f;
    read_value(&f, BLOCK_POUT(&f, within, 0), &distance, sizeof(distance));
    ASSERT_FLOAT_EQ(3.0f, distance, 1e-6f);
    float v[3] = {0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, modify, 0), v, sizeof(v));
    ASSERT_FLOAT_EQ(1.0f, v[0], 0.0f);
    ASSERT_FLOAT_EQ(9.0f, v[1], 0.0f);
    ASSERT_FLOAT_EQ(3.0f, v[2], 0.0f);
    fixture_end(&f);
}

TEST(engine_logics, ray_boxes_obstacles_and_the_view_frustum)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t cube = cube_at(&f, "Cube", 10.0f, 0.0f);
    nmo_object_id_t far_end = cube_at(&f, "Far End", 20.0f, 0.0f);
    nmo_object_id_t ahead = cube_at(&f, "Ahead", 0.0f, 10.0f);
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    ASSERT_TRUE(cube != 0 && far_end != 0 && ahead != 0 && lens != NULL);
    memset(lens->entity.world_matrix, 0, sizeof(lens->entity.world_matrix));
    lens->entity.world_matrix[0] = lens->entity.world_matrix[5] = lens->entity.world_matrix[10] = 1.0f;
    lens->entity.world_matrix[15] = 1.0f;
    lens->entity.has_entityndata_chunk = 1;
    lens->projection_type = 1; /* perspective */
    lens->fov = 1.0f;
    lens->aspect_width = 4;
    lens->aspect_height = 3;
    lens->near_plane = 1.0f;
    lens->far_plane = 100.0f;
    const nmo_object_id_t members[1] = {cube};
    nmo_object_id_t group = make_group(&f, "Boxes", members, 1);
    ASSERT_TRUE(group != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t ray = block(&f, GUID_RAY_BOX_INTERSECTION, "Ray Box");
    target_of(&f, ray, f.owner_id);
    const float origin[3] = {0, 0, 0}, along_x[3] = {1, 0, 0};
    fixture_input_value(&f, BLOCK_PIN(&f, ray, 0), origin, sizeof(origin));
    fixture_input_value(&f, BLOCK_PIN(&f, ray, 1), along_x, sizeof(along_x));
    fixture_input_int(&f, BLOCK_PIN(&f, ray, 4), (int32_t)group);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, ray, 0), 0);
    nmo_object_id_t hit = nop_after(&f, BLOCK_OUT(&f, ray, 0), 0);
    nmo_object_id_t between = block(&f, GUID_OBJECT_BETWEEN, "Object Between");
    fixture_input_int(&f, BLOCK_PIN(&f, between, 0), (int32_t)f.owner_id);
    fixture_input_int(&f, BLOCK_PIN(&f, between, 1), (int32_t)far_end);
    fixture_input_int(&f, BLOCK_PIN(&f, between, 2), 1); /* boxes only */
    fixture_input_int(&f, BLOCK_PIN(&f, between, 3), 0); /* every 3D object */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, between, 0), 0);
    nmo_object_id_t blocked = nop_after(&f, BLOCK_OUT(&f, between, 0), 0);
    nmo_object_id_t seen = block(&f, GUID_IS_IN_VIEW_FRUSTUM, "Ahead In View");
    nmo_object_id_t unseen = block(&f, GUID_IS_IN_VIEW_FRUSTUM, "Side In View");
    fixture_input_int(&f, BLOCK_PIN(&f, seen, 0), (int32_t)ahead);
    fixture_input_int(&f, BLOCK_PIN(&f, unseen, 0), (int32_t)cube);
    fixture_input_int(&f, BLOCK_PIN(&f, seen, 1), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, unseen, 1), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, seen, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, unseen, 0), 0);
    nmo_object_id_t inside = nop_after(&f, BLOCK_OUT(&f, seen, 0), 0);
    nmo_object_id_t outside = nop_after(&f, BLOCK_OUT(&f, unseen, 1), 0);
    start(&f);
    f.engine->render.camera = camera;

    step(&f, 1);
    ASSERT_EQ(1u, runs(&f, hit));
    uint32_t object = 0;
    float point[3] = {0, 0, 0}, normal[3] = {0, 0, 0}, distance = 0.0f;
    read_value(&f, BLOCK_POUT(&f, ray, 0), &object, sizeof(object));
    read_value(&f, BLOCK_POUT(&f, ray, 1), point, sizeof(point));
    read_value(&f, BLOCK_POUT(&f, ray, 2), normal, sizeof(normal));
    read_value(&f, BLOCK_POUT(&f, ray, 3), &distance, sizeof(distance));
    ASSERT_EQ((uint32_t)cube, object);
    ASSERT_FLOAT_EQ(9.5f, point[0], 1e-5f);
    ASSERT_FLOAT_EQ(-1.0f, normal[0], 0.0f);
    ASSERT_FLOAT_EQ(9.5f, distance, 1e-5f);
    ASSERT_EQ(1u, runs(&f, blocked));
    read_value(&f, BLOCK_POUT(&f, between, 0), &object, sizeof(object));
    ASSERT_EQ((uint32_t)cube, object);
    ASSERT_EQ(1u, runs(&f, inside));
    ASSERT_EQ(1u, runs(&f, outside));
    float extents[4] = {0, 0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, seen, 0), extents, sizeof(extents));
    /* A unit cube 10 ahead sits around the middle of the 640 by 480 view */
    ASSERT_TRUE(extents[0] < 320.0f && extents[2] > 320.0f);
    ASSERT_TRUE(extents[1] < 240.0f && extents[3] > 240.0f);
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
    REGISTER_TEST(engine_logics, proximity_and_modify_component);
    REGISTER_TEST(engine_logics, ray_boxes_obstacles_and_the_view_frustum);
TEST_MAIN_END()
