#include "test_framework.h"

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_edit_plan_json.h"
#include "type/nmo_type_guids.h"
#include "yyjson.h"

#include <string.h>

static void assert_json_string(yyjson_val *obj,
                               const char *key,
                               const char *expected)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    ASSERT_NOT_NULL(value);
    ASSERT_TRUE(yyjson_is_str(value));
    ASSERT_STR_EQ(expected, yyjson_get_str(value));
}

static void assert_json_uint(yyjson_val *obj,
                             const char *key,
                             uint64_t expected)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    ASSERT_NOT_NULL(value);
    ASSERT_TRUE(yyjson_is_uint(value));
    ASSERT_EQ(expected, yyjson_get_uint(value));
}

TEST(edit_plan_json, writes_manifest_with_operation_handle_refs) {
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    yyjson_doc *doc = NULL;

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(plan,
                                          42u,
                                          NMO_SCRIPT_EDIT_PARAM_IN,
                                          CKPGUID_STRING,
                                          "Runtime Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value_from_handle(
                  plan, 0u, "parameter", "hello", NULL));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation_with_refs(
                  plan,
                  42u,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  0u,
                  0u,
                  "parameter",
                  0u,
                  0u,
                  NULL,
                  0u,
                  0u,
                  NULL));

    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_manifest_json_write(
                  plan, "input.cmo", "output.cmo", &json));
    ASSERT_NOT_NULL(json);

    doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_TRUE(yyjson_is_obj(root));
    assert_json_uint(root, "version", 2u);
    assert_json_string(root, "input", "input.cmo");
    assert_json_string(root, "output", "output.cmo");

    yyjson_val *ops = yyjson_obj_get(root, "operations");
    ASSERT_NOT_NULL(ops);
    ASSERT_TRUE(yyjson_is_arr(ops));
    ASSERT_EQ(3u, yyjson_arr_size(ops));

    yyjson_val *add_param = yyjson_arr_get(ops, 0);
    ASSERT_NOT_NULL(add_param);
    assert_json_string(add_param, "op", "add_parameter");
    assert_json_uint(add_param, "owner_id", 42u);
    assert_json_string(add_param, "kind", "in");
    assert_json_string(add_param, "type_guid", "{6BD010E2-115617EA}");
    assert_json_string(add_param, "name", "Runtime Text");

    yyjson_val *set_value = yyjson_arr_get(ops, 1);
    ASSERT_NOT_NULL(set_value);
    assert_json_string(set_value, "op", "set_parameter_value");
    assert_json_uint(set_value, "parameter_operation", 1u);
    assert_json_string(set_value, "parameter_handle", "parameter");
    assert_json_string(set_value, "value", "hello");
    ASSERT_TRUE(yyjson_obj_get(set_value, "parameter_id") == NULL);

    yyjson_val *add_operation = yyjson_arr_get(ops, 2);
    ASSERT_NOT_NULL(add_operation);
    assert_json_string(add_operation, "op", "add_operation");
    assert_json_uint(add_operation, "parent_id", 42u);
    assert_json_string(add_operation, "operation_guid",
                       "{33CC6B49-3589282B}");
    assert_json_uint(add_operation, "in1_operation", 1u);
    assert_json_string(add_operation, "in1_handle", "parameter");
    ASSERT_TRUE(yyjson_obj_get(add_operation, "in1_id") == NULL);

    yyjson_doc_free(doc);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(edit_plan_json, writes_manifest_with_operation_handle_refs);
TEST_MAIN_END()
