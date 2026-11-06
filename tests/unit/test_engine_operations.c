/**
 * @file test_engine_operations.c
 * @brief The parameter operations the engine runs itself because they read
 *        its state: Per Second (the frame's delta) and Random (its random
 *        sequence), and the math operations of the registry reached through
 *        a derived type (Angle finds the Float function, as CK does).
 */

#include "engine_fixture.h"

#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <math.h>

#define GUID_IDENTITY NMO_GUID(0x15151652, 0xaeefffd5)

/* A local parameter of the script of `type` holding `size` bytes of `data` */
static nmo_object_id_t local_value(engine_fixture_t *f, nmo_guid_t type, const char *name,
                                   const void *data, size_t size)
{
    nmo_object_id_t id = 0;
    if (nmo_script_edit_add_parameter(f->tx, f->script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, type,
                                      name, &id) != NMO_OK ||
        nmo_script_edit_set_parameter_bytes(f->tx, id, (const uint8_t *)data, size) != NMO_OK) {
        return 0;
    }
    return id;
}

static nmo_object_id_t local_float(engine_fixture_t *f, nmo_guid_t type, const char *name,
                                   float value)
{
    return local_value(f, type, name, &value, sizeof(value));
}

static nmo_object_id_t local_int(engine_fixture_t *f, const char *name, int32_t value)
{
    return local_value(f, CKPGUID_INT, name, &value, sizeof(value));
}

/*
 * An operation on `in1` and `in2` (either may be 0: that input reads nothing
 * and is of type None), its output of `type`; the output's ID.
 */
static nmo_object_id_t operation(engine_fixture_t *f, nmo_guid_t guid, nmo_object_id_t in1,
                                 nmo_object_id_t in2, nmo_guid_t type)
{
    nmo_object_id_t op = 0;
    if (nmo_script_edit_add_operation(f->tx, f->script_id, guid, in1, in2, 0, &op) != NMO_OK) {
        return 0;
    }
    const nmo_parameteroperation_state_t *state =
        (const nmo_parameteroperation_state_t *)fixture_state(f, op);
    nmo_object_id_t out = state != NULL ? nmo_parameteroperation_out_id(state) : 0;
    nmo_parameter_state_t *out_state = (nmo_parameter_state_t *)fixture_state(f, out);
    if (out_state == NULL) {
        return 0;
    }
    const nmo_object_id_t inputs[2] = {in1, in2};
    const nmo_object_id_t pins[2] = {nmo_parameteroperation_in1_id(state),
                                     nmo_parameteroperation_in2_id(state)};
    for (int i = 0; i < 2; i++) {
        nmo_parameterin_state_t *pin = (nmo_parameterin_state_t *)fixture_state(f, pins[i]);
        const nmo_parameter_state_t *source =
            inputs[i] != 0 ? (const nmo_parameter_state_t *)fixture_state(f, inputs[i]) : NULL;
        if (pin == NULL) {
            return 0;
        }
        /* The operation takes its operands' types, as the operation that matches them does */
        pin->type_guid = source != NULL ? source->type_guid : CKPGUID_NONE;
    }
    out_state->type_guid = type;
    return out;
}

/* An Identity reading `source` as `type`, activated after `after` */
static nmo_object_id_t identity_of(engine_fixture_t *f, nmo_object_id_t source, nmo_guid_t type,
                                   nmo_object_id_t after)
{
    nmo_object_id_t identity = fixture_block(f, GUID_IDENTITY, "Identity");
    nmo_parameterin_state_t *pin =
        (nmo_parameterin_state_t *)fixture_state(f, BLOCK_PIN(f, identity, 0));
    nmo_parameter_state_t *pout = (nmo_parameter_state_t *)fixture_state(f, BLOCK_POUT(f, identity, 0));
    if (pin == NULL || pout == NULL) {
        return 0;
    }
    pin->type_guid = type;
    pout->type_guid = type;
    if (nmo_script_edit_connect_parameter(f->tx, source, BLOCK_PIN(f, identity, 0)) != NMO_OK) {
        return 0;
    }
    fixture_link(f, after, BLOCK_IN(f, identity, 0), 0);
    return identity;
}

static void run_frame(engine_fixture_t *f, float delta_ms)
{
    ASSERT_EQ(NMO_OK, nmo_engine_start(f->engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, delta_ms));
}

TEST(engine_operations, per_second_scales_by_the_frame_delta)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t speed = local_float(&f, CKPGUID_FLOAT, "Speed", 100.0f);
    const float velocity[3] = {10.0f, 20.0f, 30.0f};
    nmo_object_id_t motion = local_value(&f, CKPGUID_VECTOR, "Motion", velocity, sizeof(velocity));
    ASSERT_TRUE(speed != 0 && motion != 0);
    nmo_object_id_t per_frame = operation(&f, NMO_OP_GUID_PER_SECOND, speed, 0, CKPGUID_FLOAT);
    nmo_object_id_t step = operation(&f, NMO_OP_GUID_PER_SECOND, motion, 0, CKPGUID_VECTOR);
    ASSERT_TRUE(per_frame != 0 && step != 0);
    nmo_object_id_t read_speed = identity_of(&f, per_frame, CKPGUID_FLOAT, f.script_in);
    ASSERT_TRUE(read_speed != 0);
    nmo_object_id_t read_motion =
        identity_of(&f, step, CKPGUID_VECTOR, BLOCK_OUT(&f, read_speed, 0));
    ASSERT_TRUE(read_motion != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 20.0f);
    /* 100 per second over 20 ms; the plugin's vector version scales by a tenth of that */
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, read_speed, 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(2.0f, value, 1e-6f);
    float scaled[3] = {0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, read_motion, 0), scaled, sizeof(scaled));
    ASSERT_FLOAT_EQ(0.02f, scaled[0], 1e-6f);
    ASSERT_FLOAT_EQ(0.04f, scaled[1], 1e-6f);
    ASSERT_FLOAT_EQ(0.06f, scaled[2], 1e-6f);
    fixture_end(&f);
}

typedef struct random_run {
    int32_t whole;
    float real;
    uint32_t flag;
    float color[4];
} random_run_t;

/* One engine's Random values of a frame */
static void random_frame(random_run_t *out)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t low = local_int(&f, "Low", 5), high = local_int(&f, "High", 7);
    nmo_object_id_t from = local_float(&f, CKPGUID_FLOAT, "From", 1.0f);
    nmo_object_id_t to = local_float(&f, CKPGUID_FLOAT, "To", 2.0f);
    ASSERT_TRUE(low != 0 && high != 0 && from != 0 && to != 0);
    nmo_object_id_t whole = operation(&f, NMO_OP_GUID_RANDOM, low, high, CKPGUID_INT);
    nmo_object_id_t real = operation(&f, NMO_OP_GUID_RANDOM, from, to, CKPGUID_FLOAT);
    nmo_object_id_t flag = operation(&f, NMO_OP_GUID_RANDOM, 0, 0, CKPGUID_BOOL);
    nmo_object_id_t color = operation(&f, NMO_OP_GUID_RANDOM, 0, 0, CKPGUID_COLOR);
    ASSERT_TRUE(whole != 0 && real != 0 && flag != 0 && color != 0);
    nmo_object_id_t read_whole = identity_of(&f, whole, CKPGUID_INT, f.script_in);
    nmo_object_id_t read_real = identity_of(&f, real, CKPGUID_FLOAT, BLOCK_OUT(&f, read_whole, 0));
    nmo_object_id_t read_flag = identity_of(&f, flag, CKPGUID_BOOL, BLOCK_OUT(&f, read_real, 0));
    nmo_object_id_t read_color = identity_of(&f, color, CKPGUID_COLOR, BLOCK_OUT(&f, read_flag, 0));
    ASSERT_TRUE(read_whole != 0 && read_real != 0 && read_flag != 0 && read_color != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 16.0f);
    read_value(&f, BLOCK_POUT(&f, read_whole, 0), &out->whole, sizeof(out->whole));
    read_value(&f, BLOCK_POUT(&f, read_real, 0), &out->real, sizeof(out->real));
    read_value(&f, BLOCK_POUT(&f, read_flag, 0), &out->flag, sizeof(out->flag));
    read_value(&f, BLOCK_POUT(&f, read_color, 0), out->color, sizeof(out->color));
    fixture_end(&f);
}

TEST(engine_operations, random_stays_in_its_bounds_and_repeats_with_the_seed)
{
    random_run_t first, second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0xff, sizeof(second));
    random_frame(&first);
    random_frame(&second);
    /* Between the bounds, the integer ones inclusive; the color is opaque */
    ASSERT_TRUE(first.whole >= 5 && first.whole <= 7);
    ASSERT_TRUE(first.real >= 1.0f && first.real < 2.0f);
    ASSERT_TRUE(first.flag == 0u || first.flag == 1u);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(first.color[i] >= 0.0f && first.color[i] < 1.0f);
    }
    ASSERT_FLOAT_EQ(1.0f, first.color[3], 0.0f);
    /* The engine's random sequence, seeded the same, gives the same values */
    ASSERT_EQ(0, memcmp(&first, &second, sizeof(first)));
}

TEST(engine_operations, derived_types_find_the_float_functions)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* Square Root is registered for Float; an Angle operand finds it through its base type */
    nmo_object_id_t square = local_float(&f, CKPGUID_ANGLE, "Square", 9.0f);
    ASSERT_TRUE(square != 0);
    nmo_object_id_t root = operation(&f, NMO_OP_GUID_SQRT, square, 0, CKPGUID_ANGLE);
    ASSERT_TRUE(root != 0);
    nmo_object_id_t read_root = identity_of(&f, root, CKPGUID_ANGLE, f.script_in);
    ASSERT_TRUE(read_root != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 16.0f);
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, read_root, 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(3.0f, value, 0.0f);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_operations, per_second_scales_by_the_frame_delta);
    REGISTER_TEST(engine_operations, random_stays_in_its_bounds_and_repeats_with_the_seed);
    REGISTER_TEST(engine_operations, derived_types_find_the_float_functions);
TEST_MAIN_END()
