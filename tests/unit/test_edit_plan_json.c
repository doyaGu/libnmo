#include "test_framework.h"

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_edit_plan_json.h"
#include "core/nmo_error.h"
#include "object/nmo_manager_guids.h"
#include "type/nmo_type_guids.h"
#include "yyjson.h"

#include <stdio.h>
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

static void assert_plan_op_kind(const nmo_edit_plan_t *plan,
                                size_t index,
                                nmo_edit_op_kind_t expected)
{
    const nmo_edit_op_t *op = nmo_edit_plan_get(plan, index);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(expected, op->kind);
}

static void assert_manifest_invalid_contains(const char *operations_json,
                                             const char *expected_message)
{
    char json[2048];
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    snprintf(json, sizeof(json),
             "{"
             "\"version\":2,"
             "\"input\":\"in.cmo\","
             "\"output\":\"out.cmo\","
             "\"operations\":[%s]"
             "}",
             operations_json);

    nmo_last_error_clear();
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  json, strlen(json), &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(), expected_message);
    nmo_edit_plan_manifest_dispose(&manifest);
}

static void assert_plan_invalid_contains(const char *operations_json,
                                         const char *expected_message)
{
    char json[2048];
    nmo_edit_plan_t *plan = NULL;
    snprintf(json, sizeof(json),
             "{"
             "\"version\":2,"
             "\"operations\":[%s]"
             "}",
             operations_json);

    nmo_last_error_clear();
    ASSERT_NE(NMO_OK, nmo_edit_plan_json_read(json, strlen(json), &plan));
    ASSERT_STR_CONTAINS(nmo_last_error_message(), expected_message);
    nmo_edit_plan_destroy(plan);
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

TEST(edit_plan_json, reads_manifest_with_operation_handle_refs) {
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));

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

    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  json, strlen(json), &manifest));
    ASSERT_NOT_NULL(manifest.plan);
    ASSERT_STR_EQ("input.cmo", manifest.input_path);
    ASSERT_STR_EQ("output.cmo", manifest.output_path);
    ASSERT_EQ(3u, nmo_edit_plan_count(manifest.plan));

    const nmo_edit_op_t *set_op = nmo_edit_plan_get(manifest.plan, 1u);
    ASSERT_NOT_NULL(set_op);
    ASSERT_EQ(NMO_EDIT_OP_SET_PARAMETER_VALUE, set_op->kind);
    ASSERT_TRUE(set_op->data.set_value.has_parameter_ref);
    ASSERT_EQ(0u, set_op->data.set_value.parameter_ref_operation_index);
    ASSERT_STR_EQ("parameter", set_op->data.set_value.parameter_ref_handle);
    ASSERT_STR_EQ("hello", set_op->data.set_value.value);

    const nmo_edit_op_t *operation_op = nmo_edit_plan_get(manifest.plan, 2u);
    ASSERT_NOT_NULL(operation_op);
    ASSERT_EQ(NMO_EDIT_OP_ADD_OPERATION, operation_op->kind);
    ASSERT_TRUE(operation_op->data.add_operation.has_in1_parameter_ref);
    ASSERT_EQ(0u,
              operation_op->data.add_operation
                  .in1_parameter_ref_operation_index);
    ASSERT_STR_EQ("parameter",
                  operation_op->data.add_operation
                      .in1_parameter_ref_handle);
    ASSERT_FALSE(operation_op->data.add_operation.has_in2_parameter_ref);
    ASSERT_FALSE(operation_op->data.add_operation.has_out_parameter_ref);

    nmo_edit_plan_manifest_dispose(&manifest);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, writes_structured_manager_entry_options)
{
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    yyjson_doc *doc = NULL;

    nmo_parameter_write_options_t options = {
        .manager_entry = {
            .policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
            .manager = NMO_MANAGER_ENTRY_MANAGER_MESSAGE,
            .manager_guid = NMO_MANAGER_GUID_MESSAGE,
        },
    };

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan, 7u, "CreatedMessage", &options));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_json_write(plan, &json));
    ASSERT_NOT_NULL(json);

    doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    ASSERT_NOT_NULL(ops);
    yyjson_val *op = yyjson_arr_get(ops, 0);
    ASSERT_NOT_NULL(op);
    yyjson_val *manager_entry = yyjson_obj_get(op, "manager_entry");
    ASSERT_NOT_NULL(manager_entry);
    ASSERT_TRUE(yyjson_is_obj(manager_entry));
    assert_json_string(manager_entry, "policy", "create_missing");
    assert_json_string(manager_entry, "manager", "message");
    assert_json_string(manager_entry, "manager_guid",
                       "{466A0FAC-00000000}");
    ASSERT_TRUE(yyjson_obj_get(op, "manager_entry_policy") == NULL);

    yyjson_doc_free(doc);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, reads_structured_manager_entry_options)
{
    const char json[] =
        "{"
        "\"version\":2,"
        "\"operations\":[{"
        "\"op\":\"set_parameter_value\","
        "\"parameter_id\":7,"
        "\"value\":\"CreatedMessage\","
        "\"manager_entry\":{"
        "\"policy\":\"create_missing\","
        "\"manager\":\"message\","
        "\"manager_guid\":\"{466A0FAC-00000000}\""
        "}"
        "}]"
        "}";
    nmo_edit_plan_t *plan = NULL;

    ASSERT_EQ(NMO_OK, nmo_edit_plan_json_read(json, strlen(json), &plan));
    ASSERT_EQ(1u, nmo_edit_plan_count(plan));
    const nmo_edit_op_t *op = nmo_edit_plan_get(plan, 0);
    ASSERT_NOT_NULL(op);
    ASSERT_TRUE(op->data.set_value.has_options);
    ASSERT_EQ(NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
              op->data.set_value.options.manager_entry.policy);
    ASSERT_EQ(NMO_MANAGER_ENTRY_MANAGER_MESSAGE,
              op->data.set_value.options.manager_entry.manager);
    ASSERT_TRUE(nmo_guid_equals(
        NMO_MANAGER_GUID_MESSAGE,
        op->data.set_value.options.manager_entry.manager_guid));

    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, rejects_legacy_manager_entry_policy)
{
    assert_plan_invalid_contains(
        "{\"op\":\"set_parameter_value\",\"parameter_id\":7,"
        "\"value\":\"CreatedMessage\","
        "\"manager_entry_policy\":\"create_missing\"}",
        "Unknown field");
}

TEST(edit_plan_json, reports_manager_entry_policy_path)
{
    assert_plan_invalid_contains(
        "{\"op\":\"set_parameter_value\",\"parameter_id\":7,"
        "\"value\":\"CreatedMessage\","
        "\"manager_entry\":{\"policy\":42}}",
        "Invalid manager_entry.policy");
}

TEST(edit_plan_json, roundtrips_rewire_operation_handle_refs)
{
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    yyjson_doc *doc = NULL;
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(plan,
                                          42u,
                                          NMO_SCRIPT_EDIT_PARAM_IN,
                                          CKPGUID_STRING,
                                          "Runtime Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_operation_with_refs(
                  plan,
                  55u,
                  NMO_SCRIPT_EDIT_OP_SLOT_IN1 |
                      NMO_SCRIPT_EDIT_OP_SLOT_OUT,
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
    yyjson_val *ops = yyjson_obj_get(yyjson_doc_get_root(doc), "operations");
    ASSERT_NOT_NULL(ops);
    yyjson_val *rewire = yyjson_arr_get(ops, 1u);
    ASSERT_NOT_NULL(rewire);
    assert_json_string(rewire, "op", "rewire_operation");
    assert_json_uint(rewire, "operation_id", 55u);
    assert_json_uint(rewire, "in1_operation", 1u);
    assert_json_string(rewire, "in1_handle", "parameter");
    assert_json_uint(rewire, "out_id", 0u);
    yyjson_doc_free(doc);
    doc = NULL;

    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  json, strlen(json), &manifest));
    ASSERT_NOT_NULL(manifest.plan);
    ASSERT_EQ(2u, nmo_edit_plan_count(manifest.plan));
    const nmo_edit_op_t *op = nmo_edit_plan_get(manifest.plan, 1u);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_REWIRE_OPERATION, op->kind);
    ASSERT_TRUE((op->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u);
    ASSERT_TRUE((op->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u);
    ASSERT_TRUE(op->data.rewire_operation.has_in1_parameter_ref);
    ASSERT_EQ(0u,
              op->data.rewire_operation
                  .in1_parameter_ref_operation_index);
    ASSERT_STR_EQ("parameter",
                  op->data.rewire_operation.in1_parameter_ref_handle);
    ASSERT_EQ(0u, op->data.rewire_operation.out_parameter_id);

    nmo_edit_plan_manifest_dispose(&manifest);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, roundtrips_plan_without_manifest_paths) {
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_plan_t *parsed = NULL;
    char *json = NULL;
    yyjson_doc *doc = NULL;

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan, 42u, NMO_SCRIPT_EDIT_IO_INPUT, "Entry"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_json_write(plan, &json));
    ASSERT_NOT_NULL(json);

    doc = yyjson_read(json, strlen(json), 0);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_TRUE(yyjson_is_obj(root));
    assert_json_uint(root, "version", 2u);
    ASSERT_TRUE(yyjson_obj_get(root, "input") == NULL);
    ASSERT_TRUE(yyjson_obj_get(root, "output") == NULL);
    ASSERT_NOT_NULL(yyjson_obj_get(root, "operations"));
    yyjson_doc_free(doc);
    doc = NULL;

    ASSERT_EQ(NMO_OK, nmo_edit_plan_json_read(json, strlen(json), &parsed));
    ASSERT_NOT_NULL(parsed);
    ASSERT_EQ(1u, nmo_edit_plan_count(parsed));
    assert_plan_op_kind(parsed, 0u, NMO_EDIT_OP_ADD_IO);

    nmo_edit_plan_destroy(parsed);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, roundtrips_absent_parameter_bytes_options) {
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_plan_t *parsed = NULL;
    char *json = NULL;
    uint8_t bytes[] = {0x01u, 0x02u, 0x03u};

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_bytes(
                  plan, 42u, bytes, sizeof(bytes), NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_plan_json_write(plan, &json));
    ASSERT_NOT_NULL(json);
    ASSERT_EQ(NMO_OK, nmo_edit_plan_json_read(json, strlen(json), &parsed));
    ASSERT_NOT_NULL(parsed);
    ASSERT_EQ(1u, nmo_edit_plan_count(parsed));

    const nmo_edit_op_t *op = nmo_edit_plan_get(parsed, 0u);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_SET_PARAMETER_BYTES, op->kind);
    ASSERT_FALSE(op->data.set_bytes.has_options);
    ASSERT_EQ(3u, op->data.set_bytes.byte_count);

    nmo_edit_plan_destroy(parsed);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, roundtrips_all_current_v2_ops) {
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    nmo_edit_plan_manifest_t manifest;
    uint8_t bytes[] = {0xCAu, 0xFEu};
    nmo_parameter_write_options_t resize_options = {
        .resize = true,
        .manager_entry = {
            .policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
            .manager = NMO_MANAGER_ENTRY_MANAGER_MESSAGE,
        },
    };
    nmo_object_id_t fold_nodes[] = {101u, 102u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 1u,
            .old_id = 11u,
            .new_id = 22u,
            .label = "In",
        },
    };
    nmo_behavior_fold_map_t output_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 2u,
            .new_index = 3u,
            .old_id = 33u,
            .new_id = 44u,
            .label = "Out",
        },
    };
    nmo_behavior_fold_map_t parameter_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_PARAMETER,
            .old_index = 4u,
            .new_index = 5u,
            .old_id = 55u,
            .new_id = 66u,
            .label = "Param",
        },
    };
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 500u,
        .node_ids = fold_nodes,
        .node_count = 2u,
        .anchor_id = 101u,
        .block_guid = nmo_guid_parse("11111111-22222222"),
        .name = "Folded",
        .block_version = 7u,
        .preserve_boundary = true,
        .preserve_links = true,
        .preserve_params = true,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE,
        .input_maps = input_maps,
        .input_map_count = 1u,
        .output_maps = output_maps,
        .output_map_count = 1u,
        .parameter_maps = parameter_maps,
        .parameter_map_count = 1u,
    };
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 600u,
        .block_guid = nmo_guid_parse("33333333-44444444"),
        .name = "Replacement",
        .block_version = 9u,
        .preserve_links = true,
        .preserve_params = true,
    };
    memset(&manifest, 0, sizeof(manifest));

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_parameter_value(plan, 1u, "value", &resize_options));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_parameter_bytes(plan, 2u, bytes, sizeof(bytes), &resize_options));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node_ex(
                  plan,
                  3u,
                  nmo_guid_parse("AAAA0001-BBBB0002"),
                  "Node",
                  &(nmo_add_node_options_t){
                      .manager_entry = {
                          .policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
                          .manager = NMO_MANAGER_ENTRY_MANAGER_MESSAGE,
                      },
                  }));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_node(plan, 4u, 5u, 6u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_io(plan, 7u, NMO_SCRIPT_EDIT_IO_OUTPUT, "Out"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rename_io(plan, 8u, "Renamed"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_io(plan, 9u, true));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_behavior_link_from_handles(plan, 10u, 2u, "out", 4u, "in", 11u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rewire_behavior_link(plan, 12u, 13u, 14u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_behavior_link_delay(plan, 15u, 16u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_behavior_link(plan, 17u, 18u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_parameter(plan, 19u, NMO_SCRIPT_EDIT_PARAM_SHARED, CKPGUID_STRING, "Param"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_connect_parameter_to_handle(plan, 20u, 11u, "parameter"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_disconnect_parameter(plan, 21u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_parameter(plan, 22u, true));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_operation_with_refs(plan, 23u, nmo_guid_parse("33CC6B49-3589282B"), 0u, 11u, "parameter", 24u, 0u, NULL, 25u, 0u, NULL));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rewire_operation(plan, 26u, NMO_SCRIPT_EDIT_OP_SLOT_IN1 | NMO_SCRIPT_EDIT_OP_SLOT_OUT, 27u, 0u, 28u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_operation(plan, 29u));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_interface_policy(plan, 30u, NMO_SCRIPT_EDIT_INTERFACE_REMOVE));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_data_cell(plan, 31u, 32u, 33u, "cell"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(NMO_OK, nmo_edit_plan_manifest_json_write(plan, "in.cmo", "out.cmo", &json));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_manifest_json_read(json, strlen(json), &manifest));
    ASSERT_NOT_NULL(manifest.plan);
    ASSERT_EQ(nmo_edit_plan_count(plan), nmo_edit_plan_count(manifest.plan));
    for (size_t i = 0; i < nmo_edit_plan_count(plan); ++i) {
        const nmo_edit_op_t *expected = nmo_edit_plan_get(plan, i);
        ASSERT_NOT_NULL(expected);
        assert_plan_op_kind(manifest.plan, i, expected->kind);
    }
    const nmo_edit_op_t *set_bytes = nmo_edit_plan_get(manifest.plan, 1u);
    ASSERT_NOT_NULL(set_bytes);
    ASSERT_EQ(2u, set_bytes->data.set_bytes.byte_count);
    ASSERT_TRUE(set_bytes->data.set_bytes.has_options);
    ASSERT_TRUE(set_bytes->data.set_bytes.options.resize);
    const nmo_edit_op_t *set_value = nmo_edit_plan_get(manifest.plan, 0u);
    ASSERT_NOT_NULL(set_value);
    ASSERT_EQ(1u, set_value->primary_id);
    ASSERT_STR_EQ("value", set_value->data.set_value.value);
    ASSERT_TRUE(set_value->data.set_value.has_options);
    ASSERT_TRUE(set_value->data.set_value.options.resize);
    ASSERT_EQ(NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
              set_value->data.set_value.options.manager_entry.policy);
    ASSERT_EQ(NMO_MANAGER_ENTRY_MANAGER_MESSAGE,
              set_value->data.set_value.options.manager_entry.manager);
    const nmo_edit_op_t *add_node = nmo_edit_plan_get(manifest.plan, 2u);
    ASSERT_NOT_NULL(add_node);
    ASSERT_EQ(3u, add_node->data.add_node.parent_behavior_id);
    ASSERT_TRUE(nmo_guid_equals(nmo_guid_parse("AAAA0001-BBBB0002"),
                                add_node->data.add_node.bb_guid));
    ASSERT_STR_EQ("Node", add_node->data.add_node.name);
    ASSERT_TRUE(add_node->data.add_node.has_options);
    ASSERT_EQ(NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING,
              add_node->data.add_node.options.manager_entry.policy);
    ASSERT_EQ(NMO_MANAGER_ENTRY_MANAGER_MESSAGE,
              add_node->data.add_node.options.manager_entry.manager);
    const nmo_edit_op_t *remove_node = nmo_edit_plan_get(manifest.plan, 3u);
    ASSERT_NOT_NULL(remove_node);
    ASSERT_EQ(4u, remove_node->data.remove_node.parent_behavior_id);
    ASSERT_EQ(5u, remove_node->data.remove_node.node_id);
    ASSERT_EQ(6u, remove_node->data.remove_node.delete_flags);
    const nmo_edit_op_t *link = nmo_edit_plan_get(manifest.plan, 7u);
    ASSERT_NOT_NULL(link);
    ASSERT_TRUE(link->data.add_link.has_from_io_ref);
    ASSERT_EQ(2u, link->data.add_link.from_io_ref_operation_index);
    ASSERT_STR_EQ("out", link->data.add_link.from_io_ref_handle);
    ASSERT_TRUE(link->data.add_link.has_to_io_ref);
    ASSERT_EQ(4u, link->data.add_link.to_io_ref_operation_index);
    ASSERT_STR_EQ("in", link->data.add_link.to_io_ref_handle);
    ASSERT_EQ(11u, link->data.add_link.activation_delay);
    const nmo_edit_op_t *connect = nmo_edit_plan_get(manifest.plan, 12u);
    ASSERT_NOT_NULL(connect);
    ASSERT_EQ(20u, connect->data.connect_parameter.source_parameter_id);
    ASSERT_TRUE(connect->data.connect_parameter.has_target_parameter_ref);
    ASSERT_EQ(11u,
              connect->data.connect_parameter
                  .target_parameter_ref_operation_index);
    ASSERT_STR_EQ("parameter",
                  connect->data.connect_parameter
                      .target_parameter_ref_handle);
    const nmo_edit_op_t *add_operation = nmo_edit_plan_get(manifest.plan, 15u);
    ASSERT_NOT_NULL(add_operation);
    ASSERT_TRUE(add_operation->data.add_operation.has_in1_parameter_ref);
    ASSERT_EQ(11u,
              add_operation->data.add_operation
                  .in1_parameter_ref_operation_index);
    ASSERT_STR_EQ("parameter",
                  add_operation->data.add_operation
                      .in1_parameter_ref_handle);
    ASSERT_EQ(24u, add_operation->data.add_operation.in2_parameter_id);
    ASSERT_EQ(25u, add_operation->data.add_operation.out_parameter_id);
    const nmo_edit_op_t *rewire_operation =
        nmo_edit_plan_get(manifest.plan, 16u);
    ASSERT_NOT_NULL(rewire_operation);
    ASSERT_EQ(26u, rewire_operation->data.rewire_operation.operation_id);
    ASSERT_TRUE((rewire_operation->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u);
    ASSERT_TRUE((rewire_operation->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u);
    ASSERT_EQ(27u, rewire_operation->data.rewire_operation.in1_parameter_id);
    ASSERT_EQ(28u, rewire_operation->data.rewire_operation.out_parameter_id);
    const nmo_edit_op_t *interface_policy =
        nmo_edit_plan_get(manifest.plan, 18u);
    ASSERT_NOT_NULL(interface_policy);
    ASSERT_EQ(30u, interface_policy->data.interface_policy.behavior_id);
    ASSERT_EQ(NMO_SCRIPT_EDIT_INTERFACE_REMOVE,
              interface_policy->data.interface_policy.mode);
    const nmo_edit_op_t *data_cell = nmo_edit_plan_get(manifest.plan, 19u);
    ASSERT_NOT_NULL(data_cell);
    ASSERT_EQ(31u, data_cell->data.data_cell.dataarray_id);
    ASSERT_EQ(32u, data_cell->data.data_cell.row);
    ASSERT_EQ(33u, data_cell->data.data_cell.col);
    ASSERT_STR_EQ("cell", data_cell->data.data_cell.value);
    const nmo_edit_op_t *fold_op = nmo_edit_plan_get(manifest.plan, 20u);
    ASSERT_NOT_NULL(fold_op);
    ASSERT_EQ(500u, fold_op->data.fold.desc.parent_id);
    ASSERT_EQ(2u, fold_op->data.fold.desc.node_count);
    ASSERT_EQ(102u, fold_op->data.fold.node_ids[1u]);
    ASSERT_EQ(101u, fold_op->data.fold.desc.anchor_id);
    ASSERT_TRUE(nmo_guid_equals(nmo_guid_parse("11111111-22222222"),
                                fold_op->data.fold.desc.block_guid));
    ASSERT_STR_EQ("Folded", fold_op->data.fold.desc.name);
    ASSERT_EQ(7u, fold_op->data.fold.desc.block_version);
    ASSERT_TRUE(fold_op->data.fold.desc.preserve_boundary);
    ASSERT_TRUE(fold_op->data.fold.desc.preserve_links);
    ASSERT_TRUE(fold_op->data.fold.desc.preserve_params);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE,
              fold_op->data.fold.desc.interface_mode);
    ASSERT_EQ(1u, fold_op->data.fold.desc.input_map_count);
    ASSERT_EQ(22u, fold_op->data.fold.input_maps[0].new_id);
    ASSERT_STR_EQ("In", fold_op->data.fold.input_maps[0].label);
    ASSERT_EQ(1u, fold_op->data.fold.desc.output_map_count);
    ASSERT_EQ(44u, fold_op->data.fold.output_maps[0].new_id);
    ASSERT_STR_EQ("Out", fold_op->data.fold.output_maps[0].label);
    ASSERT_EQ(1u, fold_op->data.fold.desc.parameter_map_count);
    ASSERT_EQ(66u, fold_op->data.fold.parameter_maps[0].new_id);
    ASSERT_STR_EQ("Param", fold_op->data.fold.parameter_maps[0].label);
    const nmo_edit_op_t *replace_op = nmo_edit_plan_get(manifest.plan, 21u);
    ASSERT_NOT_NULL(replace_op);
    ASSERT_EQ(600u, replace_op->data.replace_bb.desc.behavior_id);
    ASSERT_TRUE(nmo_guid_equals(nmo_guid_parse("33333333-44444444"),
                                replace_op->data.replace_bb.desc.block_guid));
    ASSERT_STR_EQ("Replacement", replace_op->data.replace_bb.desc.name);
    ASSERT_EQ(9u, replace_op->data.replace_bb.desc.block_version);
    ASSERT_TRUE(replace_op->data.replace_bb.desc.preserve_links);
    ASSERT_TRUE(replace_op->data.replace_bb.desc.preserve_params);

    nmo_edit_plan_manifest_dispose(&manifest);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan_json, reads_manifest_from_file) {
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    nmo_edit_plan_manifest_t manifest;
    const char *path = "test_edit_plan_json_manifest.json";
    FILE *fp = NULL;
    memset(&manifest, 0, sizeof(manifest));
    remove(path);

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan, 42u, NMO_SCRIPT_EDIT_IO_INPUT, "Entry"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_manifest_json_write(
                  plan, "input.cmo", "output.cmo", &json));
    ASSERT_NOT_NULL(json);

    fp = fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    ASSERT_EQ(strlen(json), fwrite(json, 1u, strlen(json), fp));
    ASSERT_EQ(0, fclose(fp));
    fp = NULL;

    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_manifest_json_read_file(path, &manifest));
    ASSERT_STR_EQ("input.cmo", manifest.input_path);
    ASSERT_STR_EQ("output.cmo", manifest.output_path);
    ASSERT_NOT_NULL(manifest.plan);
    ASSERT_EQ(1u, nmo_edit_plan_count(manifest.plan));
    assert_plan_op_kind(manifest.plan, 0u, NMO_EDIT_OP_ADD_IO);

    nmo_edit_plan_manifest_dispose(&manifest);
    nmo_edit_plan_manifest_json_free(json);
    nmo_edit_plan_destroy(plan);
    remove(path);
}

TEST(edit_plan_json, rejects_missing_manifest_file_with_generic_diagnostic) {
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));

    nmo_last_error_clear();
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read_file(
                  "test_edit_plan_json_missing_manifest.json", &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Failed to open edit plan manifest file");

    nmo_edit_plan_manifest_dispose(&manifest);
}

TEST(edit_plan_json, rejects_incomplete_manifest_roots) {
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));

    const char *missing_input =
        "{"
        "\"version\":2,"
        "\"output\":\"out.cmo\","
        "\"operations\":[{\"op\":\"add_io\",\"behavior_id\":1,"
        "\"kind\":\"input\",\"name\":\"In\"}]"
        "}";
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  missing_input, strlen(missing_input), &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Edit plan manifest requires input");
    nmo_edit_plan_manifest_dispose(&manifest);

    const char *missing_output =
        "{"
        "\"version\":2,"
        "\"input\":\"in.cmo\","
        "\"operations\":[{\"op\":\"add_io\",\"behavior_id\":1,"
        "\"kind\":\"input\",\"name\":\"In\"}]"
        "}";
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  missing_output, strlen(missing_output), &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Edit plan manifest requires output");
    nmo_edit_plan_manifest_dispose(&manifest);

    const char *empty_operations =
        "{"
        "\"version\":2,"
        "\"input\":\"in.cmo\","
        "\"output\":\"out.cmo\","
        "\"operations\":[]"
        "}";
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  empty_operations, strlen(empty_operations), &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Edit plan manifest operations must be a non-empty array");
    nmo_edit_plan_manifest_dispose(&manifest);

    const char *unknown_root_field =
        "{"
        "\"version\":2,"
        "\"input\":\"in.cmo\","
        "\"output\":\"out.cmo\","
        "\"operations\":[{\"op\":\"add_io\",\"behavior_id\":1,"
        "\"kind\":\"input\",\"name\":\"In\"}],"
        "\"extra\":true"
        "}";
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  unknown_root_field, strlen(unknown_root_field), &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Unknown field 'extra' in edit plan manifest root");
    nmo_edit_plan_manifest_dispose(&manifest);
}

TEST(edit_plan_json, rejects_manifest_version_with_generic_diagnostic) {
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));

    const char *legacy =
        "{"
        "\"version\":1,"
        "\"input\":\"in.cmo\","
        "\"output\":\"out.cmo\","
        "\"operations\":[{\"op\":\"add_io\",\"behavior_id\":1,"
        "\"kind\":\"input\",\"name\":\"In\"}]"
        "}";

    nmo_last_error_clear();
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read(
                  legacy, strlen(legacy), &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Edit plan manifest version 2 is required");

    nmo_edit_plan_manifest_dispose(&manifest);
}

TEST(edit_plan_json, rejects_non_object_manifest_with_generic_diagnostic) {
    nmo_edit_plan_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));

    nmo_last_error_clear();
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_manifest_json_read("[]", 2u, &manifest));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Edit plan manifest root must be an object");

    nmo_edit_plan_manifest_dispose(&manifest);
}

TEST(edit_plan_json, rejects_invalid_operations_with_stable_diagnostics) {
    assert_manifest_invalid_contains(
        "{\"op\":\"add_io\",\"kind\":\"input\",\"name\":\"In\"}",
        "Missing or invalid behavior_id");

    assert_manifest_invalid_contains(
        "{\"op\":\"add_io\",\"behavior_id\":1,\"kind\":\"input\","
        "\"name\":\"In\",\"extra\":true}",
        "Unknown field 'extra' in add_io operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"add_behavior_link\",\"parent_id\":1,"
        "\"from_io_id\":2,\"from_operation\":1,\"from_handle\":\"out\","
        "\"to_io_id\":3}",
        "add_behavior_link requires either from_io_id or from_operation plus from_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"set_parameter_value\",\"parameter_id\":2,"
        "\"parameter_operation\":1,\"parameter_handle\":\"parameter\","
        "\"value\":\"hello\"}",
        "set_parameter_value requires either parameter_id or parameter_operation plus parameter_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"set_parameter_bytes\",\"parameter_id\":2,"
        "\"parameter_operation\":1,\"parameter_handle\":\"parameter\","
        "\"hex\":\"CAFE\"}",
        "set_parameter_bytes requires either parameter_id or parameter_operation plus parameter_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"set_parameter_bytes\",\"parameter_operation\":1,"
        "\"hex\":\"CAFE\"}",
        "Missing or invalid parameter_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"set_parameter_bytes\",\"parameter_operation\":0,"
        "\"parameter_handle\":\"parameter\",\"hex\":\"CAFE\"}",
        "Missing or invalid parameter_operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"add_behavior_link\",\"parent_id\":1,"
        "\"from_operation\":1,\"to_io_id\":3}",
        "Missing or invalid from_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"add_operation\",\"parent_id\":1,"
        "\"operation_guid\":\"33CC6B49-3589282B\","
        "\"in1_operation\":0,\"in1_handle\":\"parameter\"}",
        "Missing or invalid in1_operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"add_parameter\",\"owner_id\":1,\"kind\":\"in\","
        "\"type_guid\":\"6BD010E2-115617EA\",\"name\":\"Text\"},"
        "{\"op\":\"set_parameter_value\",\"parameter_operation\":3,"
        "\"parameter_handle\":\"parameter\",\"value\":\"hello\"}",
        "parameter_operation must reference an earlier operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"rewire_operation\",\"operation_id\":1}",
        "rewire_operation requires in1_id");

    assert_manifest_invalid_contains(
        "{\"op\":\"rewire_operation\",\"operation_id\":1,"
        "\"in1_id\":2,\"in1_operation\":1,\"in1_handle\":\"parameter\"}",
        "rewire_operation operation requires either in1_id or in1_operation plus in1_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"unknown_edit\"}",
        "Unsupported edit plan op 'unknown_edit'");
}

TEST(edit_plan_json, rejects_plan_roots_with_generic_operation_diagnostics) {
    nmo_edit_plan_t *plan = NULL;
    const char *unknown_root =
        "{"
        "\"version\":2,"
        "\"operations\":[{\"op\":\"add_io\",\"behavior_id\":1,"
        "\"kind\":\"input\",\"name\":\"In\"}],"
        "\"input\":\"not-allowed-here.cmo\""
        "}";
    nmo_last_error_clear();
    ASSERT_NE(NMO_OK,
              nmo_edit_plan_json_read(
                  unknown_root, strlen(unknown_root), &plan));
    ASSERT_STR_CONTAINS(nmo_last_error_message(),
                        "Unknown field 'input' in edit plan root");
    nmo_edit_plan_destroy(plan);

    assert_plan_invalid_contains(
        "{\"op\":\"unknown_edit\"}",
        "Unsupported edit plan op 'unknown_edit'");
}

TEST(edit_plan_json, rejects_strict_replay_manifest_errors) {
    assert_manifest_invalid_contains(
        "{\"op\":\"add_behavior_link\",\"parent_id\":1,"
        "\"from_operation\":2,\"from_handle\":\"io\",\"to_io_id\":3}",
        "from_operation must reference an earlier operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"connect_parameter\",\"source_id\":1,"
        "\"target_operation\":2,\"target_handle\":\"parameter\"}",
        "target_operation must reference an earlier operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"add_parameter\",\"owner_id\":1,\"kind\":\"in\","
        "\"type_guid\":\"6BD010E2-115617EA\",\"name\":\"Text\"},"
        "{\"op\":\"add_operation\",\"parent_id\":1,"
        "\"operation_guid\":\"33CC6B49-3589282B\","
        "\"out_operation\":3,\"out_handle\":\"parameter\"}",
        "out_operation must reference an earlier operation");

    assert_manifest_invalid_contains(
        "{\"op\":\"connect_parameter\",\"source_id\":1,"
        "\"target_operation\":1}",
        "Missing or invalid target_handle");

    assert_manifest_invalid_contains(
        "{\"op\":\"set_data_cell\",\"dataarray_id\":1,"
        "\"row\":\"0\",\"col\":1,\"value\":\"x\"}",
        "Missing or invalid row");
}

TEST_MAIN_BEGIN()
REGISTER_TEST(edit_plan_json, writes_manifest_with_operation_handle_refs);
REGISTER_TEST(edit_plan_json, reads_manifest_with_operation_handle_refs);
REGISTER_TEST(edit_plan_json, writes_structured_manager_entry_options);
REGISTER_TEST(edit_plan_json, reads_structured_manager_entry_options);
REGISTER_TEST(edit_plan_json, rejects_legacy_manager_entry_policy);
REGISTER_TEST(edit_plan_json, reports_manager_entry_policy_path);
REGISTER_TEST(edit_plan_json, roundtrips_rewire_operation_handle_refs);
REGISTER_TEST(edit_plan_json, roundtrips_plan_without_manifest_paths);
REGISTER_TEST(edit_plan_json, roundtrips_absent_parameter_bytes_options);
REGISTER_TEST(edit_plan_json, roundtrips_all_current_v2_ops);
REGISTER_TEST(edit_plan_json, reads_manifest_from_file);
REGISTER_TEST(edit_plan_json,
              rejects_missing_manifest_file_with_generic_diagnostic);
REGISTER_TEST(edit_plan_json, rejects_incomplete_manifest_roots);
REGISTER_TEST(edit_plan_json,
              rejects_manifest_version_with_generic_diagnostic);
REGISTER_TEST(edit_plan_json,
              rejects_non_object_manifest_with_generic_diagnostic);
REGISTER_TEST(edit_plan_json, rejects_invalid_operations_with_stable_diagnostics);
REGISTER_TEST(edit_plan_json,
              rejects_plan_roots_with_generic_operation_diagnostics);
REGISTER_TEST(edit_plan_json, rejects_strict_replay_manifest_errors);
TEST_MAIN_END()
