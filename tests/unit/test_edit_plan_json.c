#include "test_framework.h"

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_edit_plan_json.h"
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

TEST(edit_plan_json, roundtrips_all_current_v2_ops) {
    nmo_edit_plan_t *plan = NULL;
    char *json = NULL;
    nmo_edit_plan_manifest_t manifest;
    uint8_t bytes[] = {0xCAu, 0xFEu};
    nmo_parameter_write_options_t resize_options = {
        .resize = true,
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
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_parameter_value(plan, 1u, "value", NULL));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_parameter_bytes(plan, 2u, bytes, sizeof(bytes), &resize_options));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_node(plan, 3u, nmo_guid_parse("AAAA0001-BBBB0002"), "Node"));
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
    const nmo_edit_op_t *link = nmo_edit_plan_get(manifest.plan, 7u);
    ASSERT_NOT_NULL(link);
    ASSERT_TRUE(link->data.add_link.has_from_io_ref);
    ASSERT_EQ(2u, link->data.add_link.from_io_ref_operation_index);
    ASSERT_STR_EQ("out", link->data.add_link.from_io_ref_handle);
    const nmo_edit_op_t *fold_op = nmo_edit_plan_get(manifest.plan, 20u);
    ASSERT_NOT_NULL(fold_op);
    ASSERT_EQ(1u, fold_op->data.fold.desc.input_map_count);
    ASSERT_EQ(22u, fold_op->data.fold.input_maps[0].new_id);
    ASSERT_STR_EQ("In", fold_op->data.fold.input_maps[0].label);

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
    nmo_edit_plan_manifest_dispose(&manifest);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(edit_plan_json, writes_manifest_with_operation_handle_refs);
REGISTER_TEST(edit_plan_json, reads_manifest_with_operation_handle_refs);
REGISTER_TEST(edit_plan_json, roundtrips_all_current_v2_ops);
REGISTER_TEST(edit_plan_json, reads_manifest_from_file);
REGISTER_TEST(edit_plan_json, rejects_incomplete_manifest_roots);
TEST_MAIN_END()
