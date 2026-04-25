#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
#include "behavior/nmo_behavior_query.h"
#include "document/nmo_document_load.h"
#include "behavior/nmo_behavior_view.h"
#include "format/nmo_interface_view.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/wait.h>
#else
#include <direct.h>
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
#define NMO_POPEN popen
#define NMO_PCLOSE pclose
#elif defined(_WIN32)
#define NMO_POPEN _popen
#define NMO_PCLOSE _pclose
#else
#define NMO_POPEN popen
#define NMO_PCLOSE pclose
#endif

#ifndef NMO_CLI_PATH
#define NMO_CLI_PATH "nmo"
#endif

typedef struct cli_run_result {
    char *output;
    int exit_code;
} cli_run_result_t;

static int normalize_cli_exit_code(int status)
{
    if (status < 0) {
        return status;
    }
#if !defined(_WIN32)
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
#endif
    return status;
}

static cli_run_result_t run_cli_capture(const char *args)
{
    cli_run_result_t result = {0};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buffer = (char *)malloc(cap);
    if (buffer == NULL) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe) != NULL) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1 > cap) {
            size_t new_cap = cap * 2;
            while (new_cap < len + chunk_len + 1) {
                new_cap *= 2;
            }
            char *new_buffer = (char *)realloc(buffer, new_cap);
            if (new_buffer == NULL) {
                free(buffer);
                result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
                return result;
            }
            buffer = new_buffer;
            cap = new_cap;
        }
        memcpy(buffer + len, chunk, chunk_len);
        len += chunk_len;
    }

    buffer[len] = '\0';
    result.output = buffer;
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    return result;
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void remove_if_exists(const char *path)
{
    if (file_exists(path)) {
        remove(path);
    }
}

static int build_repo_fixture_path(const char *relative_path,
                                   char *buffer,
                                   size_t buffer_size)
{
    const char *source_path = __FILE__;
    const char *tests_dir = strstr(source_path, "tests");

    if (relative_path == NULL || buffer == NULL || buffer_size == 0u || tests_dir == NULL) {
        return 0;
    }

    snprintf(buffer, buffer_size, "%.*s%s",
             (int)(tests_dir - source_path), source_path, relative_path);
    return 1;
}

static const char *get_string_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return yyjson_get_str(val);
}

static yyjson_val *get_object_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_obj(val)) ? val : NULL;
}

static yyjson_val *get_array_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_arr(val)) ? val : NULL;
}

static uint64_t get_uint_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_uint(val)) ? yyjson_get_uint(val) : 0u;
}

static bool get_bool_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_bool(val) && yyjson_get_bool(val);
}

static yyjson_val *find_array_object_by_name(yyjson_val *arr, const char *name)
{
    size_t index = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;

    if (arr == NULL || name == NULL) {
        return NULL;
    }

    yyjson_arr_foreach(arr, index, max, item) {
        const char *item_name = get_string_field(item, "name");
        if (item_name != NULL && strcmp(item_name, name) == 0) {
            return item;
        }
    }

    return NULL;
}

static void assert_validate_ok(const char *path)
{
    char args[1024];
    cli_run_result_t result = {0};

    snprintf(args, sizeof(args), "validate all \"%s\"", path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Result: VALID");
    free(result.output);
}

static void load_root_behavior_counts(const char *path,
                                      uint32_t *out_behavior_id,
                                      uint32_t *out_inputs,
                                      uint32_t *out_outputs)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, path, NULL, NULL));

    nmo_behavior_script_view_t script = {0};
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_behavior_query_script_at(document, 0u, &script));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_behavior_view_t behavior = {0};
    ASSERT_EQ(NMO_OK,
              nmo_behavior_view_from_behavior(workspace, script.script_id, &behavior));

    if (out_behavior_id != NULL) {
        *out_behavior_id = script.script_id;
    }
    if (out_inputs != NULL) {
        *out_inputs = behavior.input_count;
    }
    if (out_outputs != NULL) {
        *out_outputs = behavior.output_count;
    }

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(cli, script_run_dry_run_emits_frozen_json_contract) {
    char script_path[1024];
    const char *input_path = NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo");
    const char *output_path = "test_cli_script_run_dry_out.cmo";
    char args[2048];
    cli_run_result_t result = {0};
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *validation = NULL;
    yyjson_val *references = NULL;
    yyjson_val *behavior_index = NULL;
    yyjson_val *interface_obj = NULL;
    yyjson_val *changed_objects = NULL;
    yyjson_val *created_objects = NULL;
    yyjson_val *first_changed = NULL;
    yyjson_val *first_created = NULL;
    yyjson_val *diff = NULL;
    yyjson_val *first_operation = NULL;
    yyjson_val *first_handles = NULL;

    remove_if_exists(output_path);
    ASSERT_TRUE(build_repo_fixture_path("tests/fixtures/lua/script_run_multi_action.lua",
                                        script_path,
                                        sizeof(script_path)));

    snprintf(args, sizeof(args),
             "-f json script run --dry-run \"%s\" \"%s\"",
             script_path,
             input_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ("nmo", get_string_field(root, "tool"));
    ASSERT_STR_EQ("script.run", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_NOT_NULL(get_array_field(data, "errors"));
    ASSERT_NOT_NULL(get_array_field(data, "warnings"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_EQ(1u, yyjson_arr_size(changed_objects));
    first_changed = yyjson_arr_get(changed_objects, 0);
    ASSERT_NOT_NULL(first_changed);
    ASSERT_STR_EQ("add_io", get_string_field(first_changed, "cause"));
    ASSERT_TRUE(yyjson_obj_get(data, "risk_level") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "semantic_risks") == NULL);
    ASSERT_STR_EQ(script_path, get_string_field(data, "script_file"));
    ASSERT_EQ(3u, get_uint_field(data, "op_count"));
    ASSERT_EQ(3u, get_uint_field(data, "operation_count"));
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(3u, yyjson_arr_size(operations));
    first_operation = yyjson_arr_get(operations, 0);
    ASSERT_NOT_NULL(first_operation);
    ASSERT_STR_EQ("add_io", get_string_field(first_operation, "op"));
    ASSERT_STR_EQ("add_io", get_string_field(first_operation, "kind"));
    ASSERT_EQ(3u, get_uint_field(first_operation, "primary_id"));
    ASSERT_EQ(276u, get_uint_field(first_operation, "result_id"));
    ASSERT_EQ(0u, get_uint_field(first_operation, "status"));
    ASSERT_STR_EQ("Success", get_string_field(first_operation, "status_name"));
    ASSERT_TRUE(yyjson_obj_get(first_operation, "result_handles") == NULL);
    first_handles = get_array_field(first_operation, "handles");
    ASSERT_NOT_NULL(first_handles);
    ASSERT_EQ(1u, yyjson_arr_size(first_handles));
    ASSERT_STR_EQ("io", get_string_field(yyjson_arr_get(first_handles, 0), "name"));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    references = get_object_field(validation, "references");
    ASSERT_NOT_NULL(references);
    ASSERT_NOT_NULL(get_string_field(references, "status_name"));
    behavior_index = get_object_field(validation, "behavior_index");
    ASSERT_NOT_NULL(behavior_index);
    interface_obj = get_object_field(validation, "interface");
    ASSERT_NOT_NULL(interface_obj);
    ASSERT_EQ(NMO_OK, (nmo_status_t)get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "roundtrip_status") != NULL);
    ASSERT_EQ(NMO_OK, (nmo_status_t)get_uint_field(validation, "roundtrip_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "reference_status") != NULL);
    ASSERT_EQ(NMO_OK, (nmo_status_t)get_uint_field(validation, "reference_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "behavior_index_status") != NULL);
    ASSERT_EQ(NMO_OK, (nmo_status_t)get_uint_field(validation, "behavior_index_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "interface_status") != NULL);
    ASSERT_EQ(NMO_OK, (nmo_status_t)get_uint_field(validation, "interface_status"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_EQ(3u, yyjson_arr_size(created_objects));
    first_created = yyjson_arr_get(created_objects, 0);
    ASSERT_NOT_NULL(first_created);
    ASSERT_STR_EQ("add_io", get_string_field(first_created, "cause"));
    diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_EQ(1u, get_uint_field(diff, "changed_object_count"));
    ASSERT_EQ(3u, get_uint_field(diff, "created_object_count"));
    ASSERT_EQ(0u, get_uint_field(diff, "deleted_object_count"));
    ASSERT_EQ(0u, get_uint_field(diff, "semantic_risk_count"));
    ASSERT_FALSE(file_exists(output_path));

    yyjson_doc_free(doc);
}

TEST(cli, script_run_applies_changes_through_executor) {
    char script_path[1024];
    const char *input_path = NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo");
    const char *output_path = "test_cli_script_run_apply_out.cmo";
    uint32_t baseline_behavior_id = 0;
    uint32_t baseline_inputs = 0;
    uint32_t baseline_outputs = 0;
    uint32_t new_behavior_id = 0;
    uint32_t new_inputs = 0;
    uint32_t new_outputs = 0;
    char args[2048];
    cli_run_result_t result = {0};
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *inputs = NULL;
    yyjson_val *outputs = NULL;
    yyjson_val *iface_parse = NULL;
    yyjson_val *graph = NULL;
    yyjson_val *control_edges = NULL;
    yyjson_val *data_edges = NULL;

    remove_if_exists(output_path);
    ASSERT_TRUE(build_repo_fixture_path("tests/fixtures/lua/script_run_multi_action.lua",
                                        script_path,
                                        sizeof(script_path)));
    load_root_behavior_counts(input_path,
                              &baseline_behavior_id,
                              &baseline_inputs,
                              &baseline_outputs);

    snprintf(args, sizeof(args),
             "-f json script run \"%s\" \"%s\" -o \"%s\"",
             script_path,
             input_path,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.run", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(3u, get_uint_field(data, "operation_count"));
    ASSERT_STR_EQ(output_path, get_string_field(data, "output_path"));
    yyjson_doc_free(doc);

    ASSERT_TRUE(file_exists(output_path));
    load_root_behavior_counts(output_path, &new_behavior_id, &new_inputs, &new_outputs);
    ASSERT_EQ(baseline_behavior_id, new_behavior_id);
    ASSERT_EQ(baseline_inputs + 2u, new_inputs);
    ASSERT_EQ(baseline_outputs + 1u, new_outputs);
    assert_validate_ok(output_path);

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             new_behavior_id,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    inputs = get_array_field(data, "inputs");
    outputs = get_array_field(data, "outputs");
    ASSERT_NOT_NULL(inputs);
    ASSERT_NOT_NULL(outputs);
    ASSERT_NOT_NULL(find_array_object_by_name(inputs, "Lua Multi In A"));
    ASSERT_NOT_NULL(find_array_object_by_name(inputs, "Lua Multi In B"));
    ASSERT_NOT_NULL(find_array_object_by_name(outputs, "Lua Multi Out A"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             new_behavior_id,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    iface_parse = get_object_field(data, "interface_parse");
    ASSERT_NOT_NULL(iface_parse);
    ASSERT_TRUE(get_bool_field(iface_parse, "available"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             new_behavior_id,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    graph = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(graph);
    control_edges = get_array_field(graph, "control_edges");
    data_edges = get_array_field(graph, "data_edges");
    ASSERT_NOT_NULL(control_edges);
    ASSERT_NOT_NULL(data_edges);
    yyjson_doc_free(doc);

    remove_if_exists(output_path);
}

TEST(cli, script_run_runtime_error_does_not_write_output)
{
    char script_path[1024];
    const char *input_path = NMO_TEST_DATA_FILE("Nop.cmo");
    const char *output_path = "test_cli_script_run_runtime_error_out.cmo";
    char args[2048];
    cli_run_result_t result = {0};

    ASSERT_TRUE(build_repo_fixture_path("tests/fixtures/lua/script_run_runtime_error.lua",
                                        script_path,
                                        sizeof(script_path)));
    remove_if_exists(output_path);

    snprintf(args, sizeof(args),
             "script run \"%s\" \"%s\" -o \"%s\"",
             script_path,
             input_path,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "lua runtime acceptance failure");
    free(result.output);
    ASSERT_FALSE(file_exists(output_path));
}

TEST(cli, script_run_validation_failure_does_not_write_output)
{
    char script_path[1024];
    const char *input_path = NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo");
    const char *output_path = "test_cli_script_run_validation_error_out.cmo";
    char args[2048];
    cli_run_result_t result = {0};

    ASSERT_TRUE(build_repo_fixture_path(
        "tests/fixtures/lua/script_run_validation_failure.lua",
        script_path,
        sizeof(script_path)));
    remove_if_exists(output_path);

    snprintf(args, sizeof(args),
             "script run \"%s\" \"%s\" -o \"%s\"",
             script_path,
             input_path,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "interface");
    free(result.output);
    ASSERT_FALSE(file_exists(output_path));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, script_run_dry_run_emits_frozen_json_contract);
    REGISTER_TEST(cli, script_run_applies_changes_through_executor);
    REGISTER_TEST(cli, script_run_runtime_error_does_not_write_output);
    REGISTER_TEST(cli, script_run_validation_failure_does_not_write_output);
TEST_MAIN_END()

