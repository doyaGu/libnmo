/**
 * @file test_engine_operation_functions.c
 * @brief Every parameter operation function the original plugins register
 *        (data/virtools_operation_functions.json) has one in the engine for
 *        exactly its types: a missing one only shows as an output that never
 *        changes, so it is checked here, signature by signature.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "yyjson.h"

#include <stdio.h>
#include <string.h>

/* "0xD1-0xD2" into a GUID; a JSON null is no GUID */
static bool parse_guid(yyjson_val *value, nmo_guid_t *out)
{
    *out = NMO_GUID(0u, 0u);
    if (value == NULL || yyjson_is_null(value)) {
        return true;
    }
    unsigned d1 = 0, d2 = 0;
    const char *text = yyjson_get_str(value);
    if (text == NULL || sscanf(text, "0x%x-0x%x", &d1, &d2) != 2) {
        return false;
    }
    *out = NMO_GUID(d1, d2);
    return true;
}

TEST(engine_operation_functions, every_registered_function_is_there)
{
    yyjson_doc *doc = yyjson_read_file(NMO_TEST_DATA_DIR "/virtools_operation_functions.json", 0, NULL, NULL);
    ASSERT_NOT_NULL(doc);
    yyjson_val *functions = yyjson_obj_get(yyjson_doc_get_root(doc), "functions");
    ASSERT_TRUE(yyjson_is_arr(functions));
    ASSERT_TRUE(yyjson_arr_size(functions) > 400u);

    engine_fixture_t f;
    fixture_begin(&f);
    fixture_commit(&f, NULL);
    size_t index, count, missing = 0;
    yyjson_val *entry;
    yyjson_arr_foreach(functions, index, count, entry) {
        nmo_guid_t operation, result, input1, input2;
        ASSERT_TRUE(parse_guid(yyjson_obj_get(entry, "operation_guid"), &operation));
        ASSERT_TRUE(parse_guid(yyjson_obj_get(entry, "result_guid"), &result));
        ASSERT_TRUE(parse_guid(yyjson_obj_get(entry, "input1_guid"), &input1));
        ASSERT_TRUE(parse_guid(yyjson_obj_get(entry, "input2_guid"), &input2));
        if (!eng_operation_available(f.engine, operation, result, input1, input2)) {
            missing++;
            fprintf(stderr, "  missing: %s (%s)\n", yyjson_get_str(yyjson_obj_get(entry, "function")),
                    yyjson_get_str(yyjson_obj_get(entry, "plugin")));
        }
    }
    ASSERT_EQ(0u, missing);
    fixture_end(&f);
    yyjson_doc_free(doc);
}

TEST(engine_operation_functions, a_missing_function_is_noted_once_with_its_count)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* Get Radius of a Vector: no plugin registers one */
    const float v[3] = {1.0f, 2.0f, 3.0f};
    nmo_object_id_t vector = 0, op = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, f.script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_VECTOR, "V", &vector));
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_bytes(f.tx, vector, (const uint8_t *)v, sizeof(v)));
    const nmo_guid_t get_radius = NMO_GUID(0x0ae164ff, 0x3a735fe9);
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_operation(f.tx, f.script_id, get_radius, vector, 0, 0, &op));
    const nmo_parameteroperation_state_t *op_state =
        (const nmo_parameteroperation_state_t *)fixture_state(&f, op);
    ASSERT_NOT_NULL(op_state);
    nmo_parameterin_state_t *pin =
        (nmo_parameterin_state_t *)fixture_state(&f, nmo_parameteroperation_in1_id(op_state));
    nmo_parameterin_state_t *pin2 =
        (nmo_parameterin_state_t *)fixture_state(&f, nmo_parameteroperation_in2_id(op_state));
    nmo_parameter_state_t *out = (nmo_parameter_state_t *)fixture_state(&f, nmo_parameteroperation_out_id(op_state));
    ASSERT_TRUE(pin != NULL && pin2 != NULL && out != NULL);
    pin->type_guid = CKPGUID_VECTOR;
    pin2->type_guid = CKPGUID_NONE;
    out->type_guid = CKPGUID_FLOAT;
    /* An Identity reads the output each frame, which runs the operation */
    nmo_object_id_t identity = fixture_block(&f, NMO_GUID(0x15151652, 0xaeefffd5), "Identity");
    ASSERT_TRUE(identity != 0);
    nmo_parameterin_state_t *read = (nmo_parameterin_state_t *)fixture_state(&f, BLOCK_PIN(&f, identity, 0));
    nmo_parameter_state_t *copy = (nmo_parameter_state_t *)fixture_state(&f, BLOCK_POUT(&f, identity, 0));
    ASSERT_TRUE(read != NULL && copy != NULL);
    read->type_guid = CKPGUID_FLOAT;
    copy->type_guid = CKPGUID_FLOAT;
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(f.tx, nmo_parameteroperation_out_id(op_state),
                                                        BLOCK_PIN(&f, identity, 0)));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, identity, 0), 0);
    fixture_commit(&f, NULL);

    size_t count = 9;
    nmo_engine_missing_operations(f.engine, &count);
    ASSERT_EQ(0u, count);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    const nmo_engine_missing_operation_t *missing = nmo_engine_missing_operations(f.engine, &count);
    ASSERT_EQ(1u, count);
    ASSERT_TRUE(nmo_guid_equals(missing[0].operation, get_radius));
    ASSERT_TRUE(nmo_guid_equals(missing[0].result, CKPGUID_FLOAT));
    ASSERT_TRUE(nmo_guid_equals(missing[0].input1, CKPGUID_VECTOR));
    ASSERT_TRUE(nmo_guid_is_null(missing[0].input2));
    ASSERT_NULL(missing[0].input2_name);
    ASSERT_TRUE(missing[0].count >= 1u);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_operation_functions, every_registered_function_is_there);
    REGISTER_TEST(engine_operation_functions, a_missing_function_is_noted_once_with_its_count);
TEST_MAIN_END()
