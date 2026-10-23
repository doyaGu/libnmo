#include "engine_fixture.h"

#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "type/nmo_type_guids.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_MINI_CALCULATOR NMO_GUID(0x55bc3115, 0x1dfe1e40)
#define GUID_GET_DELTA_TIME NMO_GUID(0x47dc3232, 0x64bf203a)
#define GUID_GET_COMPONENT NMO_GUID(0x2689057b, 0x1a1a446a)
#define GUID_VARIATION NMO_GUID(0x3e7335d9, 0x29687f36)
#define GUID_BEZIER_TRANSFORM NMO_GUID(0x6d433e1b, 0x191954b4)

static void input_floats(engine_fixture_t *f, nmo_object_id_t pin, const float *values, size_t n)
{
    fixture_input_value(f, pin, values, n * sizeof(float));
}

/* Give an input parameter (before its source is made) or an output another type */
static void retype_input(engine_fixture_t *f, nmo_object_id_t pin, nmo_guid_t type)
{
    nmo_parameterin_state_t *state = (nmo_parameterin_state_t *)fixture_state(f, pin);
    ASSERT_NOT_NULL(state);
    state->type_guid = type;
}

static void retype_output(engine_fixture_t *f, nmo_object_id_t pout, nmo_guid_t type)
{
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, pout);
    ASSERT_NOT_NULL(state);
    state->type_guid = type;
}

static float output_float(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    float value = 0.0f;
    read_value(f, BLOCK_POUT(f, block, pos), &value, sizeof(value));
    return value;
}

TEST(engine_calculator, mini_calculator_and_delta_time)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t divide = fixture_block(&f, GUID_MINI_CALCULATOR, "Divide");
    nmo_object_id_t delta = fixture_block(&f, GUID_GET_DELTA_TIME, "Delta");
    ASSERT_TRUE(divide != 0 && delta != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, divide, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, delta, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, divide, 0), 4); /* "/" */
    const float a = 6.0f, b = 3.0f;
    input_floats(&f, BLOCK_PIN(&f, divide, 1), &a, 1);
    input_floats(&f, BLOCK_PIN(&f, divide, 2), &b, 1);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 25.0f));
    ASSERT_FLOAT_EQ(2.0f, output_float(&f, divide, 0), 0.0001f);
    ASSERT_FLOAT_EQ(25.0f, output_float(&f, delta, 0), 0.0001f);
    fixture_end(&f);
}

TEST(engine_calculator, get_component_of_a_vector_and_a_rectangle)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t vector = fixture_block(&f, GUID_GET_COMPONENT, "Vector");
    nmo_object_id_t rect = fixture_block(&f, GUID_GET_COMPONENT, "Rectangle");
    ASSERT_TRUE(vector != 0 && rect != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, vector, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, rect, 0), 0);
    const float xyz[3] = {1.0f, 2.0f, 3.0f};
    input_floats(&f, BLOCK_PIN(&f, vector, 0), xyz, 3);
    /* A rectangle in, 2D vectors out, as the block's callback makes them */
    retype_input(&f, BLOCK_PIN(&f, rect, 0), CKPGUID_RECT);
    retype_output(&f, BLOCK_POUT(&f, rect, 0), CKPGUID_2DVECTOR);
    retype_output(&f, BLOCK_POUT(&f, rect, 1), CKPGUID_2DVECTOR);
    const float ltrb[4] = {10.0f, 20.0f, 30.0f, 60.0f};
    input_floats(&f, BLOCK_PIN(&f, rect, 0), ltrb, 4);
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, rect);
    ASSERT_TRUE(state != NULL && state->local_parameters.count > 0);
    nmo_object_id_t mode = nmo_behavior_ref_array_get_id(&state->local_parameters, 0);
    fixture_commit(&f, NULL);
    int32_t center = 3; /* Center/HalfSizes */
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, mode, &center, sizeof(center)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    for (int i = 0; i < 3; i++) {
        ASSERT_FLOAT_EQ(xyz[i], output_float(&f, vector, (size_t)i), 0.0001f);
    }
    float position[2] = {0}, size[2] = {0};
    read_value(&f, BLOCK_POUT(&f, rect, 0), position, sizeof(position));
    read_value(&f, BLOCK_POUT(&f, rect, 1), size, sizeof(size));
    ASSERT_FLOAT_EQ(20.0f, position[0], 0.0001f);
    ASSERT_FLOAT_EQ(40.0f, position[1], 0.0001f);
    ASSERT_FLOAT_EQ(10.0f, size[0], 0.0001f);
    ASSERT_FLOAT_EQ(20.0f, size[1], 0.0001f);
    fixture_end(&f);
}

TEST(engine_calculator, variation_reports_changes)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t variation = fixture_block(&f, GUID_VARIATION, "Variation");
    nmo_object_id_t changed = fixture_block(&f, GUID_NOP, "Changed");
    nmo_object_id_t same = fixture_block(&f, GUID_NOP, "Same");
    ASSERT_TRUE(variation != 0 && changed != 0 && same != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, variation, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, variation, 0), BLOCK_IN(&f, changed, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, variation, 1), BLOCK_IN(&f, same, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, changed, 0), BLOCK_IN(&f, variation, 0), 1);
    fixture_link(&f, BLOCK_OUT(&f, same, 0), BLOCK_IN(&f, variation, 0), 1);
    nmo_object_id_t x = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                          f.tx, BLOCK_PIN(&f, variation, 0), &x));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const float values[3] = {5.0f, 5.0f, 7.0f};
    const float deltas[3] = {5.0f, 0.0f, 2.0f};
    const uint64_t changes[3] = {1u, 1u, 2u};
    for (int frame = 0; frame < 3; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, x, &values[frame], sizeof(float)));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        ASSERT_FLOAT_EQ(deltas[frame], output_float(&f, variation, 0), 0.0001f);
        ASSERT_EQ(changes[frame], runs(&f, changed));
    }
    ASSERT_EQ(1u, runs(&f, same));
    /* OldX: the value before the last change */
    ASSERT_FLOAT_EQ(5.0f, output_float(&f, variation, 1), 0.0001f);
    fixture_end(&f);
}

TEST(engine_calculator, bezier_transform_maps_between_boundaries)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t unit = fixture_block(&f, GUID_BEZIER_TRANSFORM, "Unit");
    nmo_object_id_t bounded = fixture_block(&f, GUID_BEZIER_TRANSFORM, "Bounded");
    ASSERT_TRUE(unit != 0 && bounded != 0);
    nmo_object_id_t curve = 0;
    nmo_object_id_t blocks[2] = {unit, bounded};
    for (int i = 0; i < 2; i++) {
        fixture_link(&f, f.script_in, BLOCK_IN(&f, blocks[i], 0), 0);
        /* No curve saved: CK2dCurve's diagonal */
        ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(
                              f.tx, BLOCK_PIN(&f, blocks[i], 1), &curve));
    }
    const float quarter = 0.25f, half_way = 50.0f;
    input_floats(&f, BLOCK_PIN(&f, unit, 0), &quarter, 1);
    input_floats(&f, BLOCK_PIN(&f, bounded, 0), &half_way, 1);
    const float boundaries[4] = {0.0f, 10.0f, 100.0f, 20.0f}; /* X 0..100 to Y 20..10 */
    input_floats(&f, BLOCK_PIN(&f, bounded, 2), boundaries, 4);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(0.25f, output_float(&f, unit, 0), 0.0001f);
    ASSERT_FLOAT_EQ(15.0f, output_float(&f, bounded, 0), 0.0001f);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_calculator, mini_calculator_and_delta_time);
REGISTER_TEST(engine_calculator, get_component_of_a_vector_and_a_rectangle);
REGISTER_TEST(engine_calculator, variation_reports_changes);
REGISTER_TEST(engine_calculator, bezier_transform_maps_between_boundaries);
TEST_MAIN_END()
