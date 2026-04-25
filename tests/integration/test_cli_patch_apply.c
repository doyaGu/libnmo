/**
 * @file test_cli_patch_apply.c
 * @brief CLI patch apply integration tests.
 */

#include "test_framework.h"

#include "ballance_rewrite_fixture.h"
#include "../../tools/nmo_cli_common.h"
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

static void load_ballance_manifest_or_die(rewrite_manifest_t *manifest) {
    ASSERT_NOT_NULL(manifest);
    ASSERT_TRUE(load_rewrite_manifest(manifest));
}

static int normalize_cli_exit_code(int status) {
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

static cli_run_result_t run_cli_capture(const char *args) {
    cli_run_result_t result = {NULL, -1};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) {
        return result;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1 > cap) {
            size_t next_cap = cap * 2;
            while (next_cap < len + chunk_len + 1) {
                next_cap *= 2;
            }
            char *next = (char *)realloc(buf, next_cap);
            if (!next) {
                free(buf);
                result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
                return result;
            }
            buf = next;
            cap = next_cap;
        }
        memcpy(buf + len, chunk, chunk_len);
        len += chunk_len;
    }
    buf[len] = '\0';
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    result.output = buf;
    return result;
}

static void make_dir(const char *path) {
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

static const char *get_string_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return yyjson_get_str(val);
}

static yyjson_val *get_object_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_obj(val)) ? val : NULL;
}

static yyjson_val *get_array_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_arr(val)) ? val : NULL;
}

static uint64_t get_uint_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_uint(val)) ? yyjson_get_uint(val) : 0;
}

static bool get_bool_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_bool(val) && yyjson_get_bool(val);
}

static void assert_cli_success(const char *args, const char *contains) {
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code != NMO_CLI_EXIT_SUCCESS ||
        (contains && (!result.output || !strstr(result.output, contains)))) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args, result.exit_code,
                result.output ? result.output : "(null)");
    }
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    if (contains) {
        ASSERT_STR_CONTAINS(result.output, contains);
    }
    free(result.output);
}

static void assert_validate_ok(const char *path) {
    char args[1024];
    snprintf(args, sizeof(args), "validate all \"%s\"", path);
    assert_cli_success(args, "Result: VALID");
}

static void run_json_command(const char *args,
                             const char *expected_command,
                             yyjson_doc **out_doc) {
    ASSERT_NOT_NULL(out_doc);
    *out_doc = NULL;

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ(expected_command, get_string_field(root, "command"));
    ASSERT_NOT_NULL(get_object_field(root, "data"));
    *out_doc = doc;
}

static void write_leaf_patch(const char *path, const char *output_path) {
    rewrite_manifest_t manifest;
    char replace_guid[64];
    char json[2048];

    load_ballance_manifest_or_die(&manifest);
    rewrite_manifest_cli_guid(manifest.replace_guid, replace_guid,
                              sizeof(replace_guid));

    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 1,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"replace_bb\",\n"
             "      \"behavior_id\": 343,\n"
             "      \"name\": \"%s\",\n"
             "      \"guid\": \"%s\",\n"
             "      \"version\": 65536,\n"
             "      \"preserve_links\": true,\n"
             "      \"preserve_params\": true\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path,
             manifest.replace_name,
             replace_guid);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_leaf_patch_v2(const char *path, const char *output_path) {
    rewrite_manifest_t manifest;
    char replace_guid[64];
    char json[2048];

    load_ballance_manifest_or_die(&manifest);
    rewrite_manifest_cli_guid(manifest.replace_guid, replace_guid,
                              sizeof(replace_guid));

    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"replace_bb\",\n"
             "      \"behavior_id\": 343,\n"
             "      \"name\": \"%s\",\n"
             "      \"guid\": \"%s\",\n"
             "      \"version\": 65536,\n"
             "      \"preserve_links\": true,\n"
             "      \"preserve_params\": true\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path,
             manifest.replace_name,
             replace_guid);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_add_io_patch_v2(const char *path, const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_io\",\n"
             "      \"behavior_id\": 3,\n"
             "      \"kind\": \"input\",\n"
             "      \"name\": \"Patch V2 In\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_add_node_patch_v2(const char *path,
                                    const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_node\",\n"
             "      \"behavior_id\": 3,\n"
             "      \"guid\": \"055B29FE-662D5CA0\",\n"
             "      \"name\": \"Patch V2 2D Text\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_remove_io_patch_v2(const char *path,
                                     const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"remove_io\",\n"
             "      \"io_id\": 2,\n"
             "      \"detach_links\": false\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_rename_io_patch_v2(const char *path,
                                     const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"rename_io\",\n"
             "      \"io_id\": 2,\n"
             "      \"name\": \"Patch V2 Start\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_interface_policy_patch_v2(const char *path,
                                            const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"interface_policy\",\n"
             "      \"behavior_id\": 3,\n"
             "      \"mode\": \"preserve\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_non_leaf_patch(const char *path, const char *output_path) {
    rewrite_manifest_t manifest;
    char replace_guid[64];
    char json[2048];

    load_ballance_manifest_or_die(&manifest);
    rewrite_manifest_cli_guid(manifest.replace_guid, replace_guid,
                              sizeof(replace_guid));

    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 1,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"replace_bb\",\n"
             "      \"behavior_id\": %u,\n"
             "      \"name\": \"%s\",\n"
             "      \"guid\": \"%s\",\n"
             "      \"version\": 65536,\n"
             "      \"preserve_links\": true,\n"
             "      \"preserve_params\": true\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path,
             manifest.replace_node_id,
             manifest.replace_name,
             replace_guid);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_fold_patch(const char *path, const char *output_path) {
    char json[4096];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 1,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"fold\",\n"
             "      \"parent\": 4692,\n"
             "      \"nodes\": [2364, 2208],\n"
             "      \"anchor\": 2364,\n"
             "      \"name\": \"Ballance Event Handler\",\n"
             "      \"guid\": \"42414C07-10000007\",\n"
             "      \"version\": 65536,\n"
             "      \"preserve_boundary\": true,\n"
             "      \"inputs\": [\n"
             "        {\"old_index\": 0, \"new_index\": 0},\n"
             "        {\"old_index\": 1, \"new_index\": 1}\n"
             "      ],\n"
             "      \"outputs\": [\n"
             "        {\"old_index\": 0, \"new_index\": 1}\n"
             "      ],\n"
             "      \"interface\": \"preserve\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_risky_fold_patch(const char *path, const char *output_path) {
    char json[4096];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 1,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"fold\",\n"
             "      \"parent\": 363,\n"
             "      \"nodes\": [237, 358],\n"
             "      \"anchor\": 358,\n"
             "      \"name\": \"Ballance Load NMO Range\",\n"
             "      \"guid\": \"42414C02-10000002\",\n"
             "      \"version\": 65536,\n"
             "      \"preserve_boundary\": false,\n"
             "      \"interface\": \"preserve\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_closed_graph_fold_patch(const char *path,
                                          const char *output_path) {
    rewrite_manifest_t manifest;
    char fold_nodes[256];
    char json[4096];

    load_ballance_manifest_or_die(&manifest);
    ASSERT_TRUE(rewrite_manifest_fold_nodes_csv(&manifest, fold_nodes,
                                                sizeof(fold_nodes)));

    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 1,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"fold\",\n"
             "      \"parent\": %u,\n"
             "      \"nodes\": [%s],\n"
             "      \"anchor\": %u,\n"
             "      \"name\": \"Patch Fold Small Graph\",\n"
             "      \"guid\": \"42414C07-10000007\",\n"
             "      \"version\": 65536,\n"
             "      \"preserve_boundary\": true,\n"
             "      \"interface\": \"preserve\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path,
             manifest.fold_parent_id,
             fold_nodes,
             manifest.fold_anchor_id);
    ASSERT_TRUE(write_text_file(path, json));
}

static bool array_contains_object_id(yyjson_val *arr, uint64_t needle) {
    size_t idx;
    size_t max;
    yyjson_val *item;

    if (!arr) {
        return false;
    }
    yyjson_arr_foreach(arr, idx, max, item) {
        if (yyjson_is_obj(item) &&
            get_uint_field(item, "object_id") == needle) {
            return true;
        }
    }
    return false;
}

static yyjson_val *find_object_by_string_field(yyjson_val *arr,
                                               const char *key,
                                               const char *needle) {
    size_t idx;
    size_t max;
    yyjson_val *item;

    if (!arr || !needle) {
        return NULL;
    }
    yyjson_arr_foreach(arr, idx, max, item) {
        if (yyjson_is_obj(item) &&
            strcmp(needle, get_string_field(item, key)) == 0) {
            return item;
        }
    }
    return NULL;
}

TEST(cli, patch_apply_leaf_replace_bb_dry_run_and_apply) {
    rewrite_manifest_t manifest;
    char replace_guid[64];

    load_ballance_manifest_or_die(&manifest);
    rewrite_manifest_cli_guid(manifest.replace_guid, replace_guid,
                              sizeof(replace_guid));
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/replace_bb.json";
    const char *output = "test_patch_tmp/replace_bb.cmo";
    remove(patch);
    remove(output);
    write_leaf_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    ASSERT_FALSE(file_exists(output));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "patch diff \"%s\"", patch);
    assert_cli_success(args, "replace_bb #343");

    snprintf(args, sizeof(args), "patch apply \"%s\"", patch);
    assert_cli_success(args, "Saved to");
    ASSERT_TRUE(file_exists(output));
    assert_validate_ok(output);

    snprintf(args, sizeof(args), "-f json behavior show 343 \"%s\"", output);
    run_json_command(args, "behavior.show", &doc);
    root = yyjson_doc_get_root(doc);
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(replace_guid, get_string_field(data, "bb_guid"));
    yyjson_doc_free(doc);

    remove(output);
    remove(patch);
}

TEST(cli, patch_apply_rejects_non_leaf_replace_bb) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/reject_non_leaf.json";
    const char *output = "test_patch_tmp/reject_non_leaf.cmo";
    remove(patch);
    remove(output);
    write_non_leaf_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "patch apply \"%s\"", patch);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "not_leaf_replaceable");
    ASSERT_STR_CONTAINS(result.output, "Behavior is not leaf-replaceable");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(patch);
}

TEST(cli, patch_apply_json_failure_reports_edit_report) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/reject_non_leaf_json.json";
    const char *output = "test_patch_tmp/reject_non_leaf_json.cmo";
    remove(patch);
    remove(output);
    write_non_leaf_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    if (!doc) {
        fprintf(stderr, "\nExpected JSON output, got:\n%s\n", result.output);
    }
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("patch.apply", get_string_field(root, "command"));
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_FALSE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("replace_bb", get_string_field(op, "op"));
    ASSERT_STR_EQ("not_leaf_replaceable",
                  get_string_field(op, "diagnostic_code"));
    ASSERT_STR_CONTAINS(get_string_field(op, "diagnostic_message"),
                        "not leaf-replaceable");
    ASSERT_NOT_NULL(get_object_field(data, "validation"));
    ASSERT_NOT_NULL(get_object_field(data, "diff"));
    ASSERT_FALSE(file_exists(output));

    yyjson_doc_free(doc);
    free(result.output);
    remove(patch);
}

TEST(cli, patch_apply_v2_replace_bb_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/replace_bb_v2.json";
    const char *output = "test_patch_tmp/replace_bb_v2.cmo";
    remove(patch);
    remove(output);
    write_leaf_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);

    remove(patch);
}

TEST(cli, patch_apply_v2_add_io_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/add_io_v2.json";
    const char *output = "test_patch_tmp/add_io_v2.cmo";
    remove(patch);
    remove(output);
    write_add_io_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("add_io", get_string_field(op, "op"));
    ASSERT_EQ(3u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_add_node_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/add_node_v2.json";
    const char *output = "test_patch_tmp/add_node_v2.cmo";
    remove(patch);
    remove(output);
    write_add_node_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("add_node", get_string_field(op, "op"));
    ASSERT_EQ(3u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    yyjson_val *created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_TRUE(yyjson_arr_size(created_objects) > 1u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_remove_io_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/remove_io_v2.json";
    const char *output = "test_patch_tmp/remove_io_v2.cmo";
    remove(patch);
    remove(output);
    write_remove_io_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("remove_io", get_string_field(op, "op"));
    ASSERT_EQ(2u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_TRUE(array_contains_object_id(deleted_objects, 2u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_rename_io_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/rename_io_v2.json";
    const char *output = "test_patch_tmp/rename_io_v2.cmo";
    remove(patch);
    remove(output);
    write_rename_io_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("rename_io", get_string_field(op, "op"));
    ASSERT_EQ(2u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 2u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_interface_policy_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/interface_policy_v2.json";
    const char *output = "test_patch_tmp/interface_policy_v2.cmo";
    remove(patch);
    remove(output);
    write_interface_policy_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("interface_policy", get_string_field(op, "op"));
    ASSERT_EQ(3u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_fold_dry_run_reports_analysis) {
    rewrite_manifest_t manifest;

    load_ballance_manifest_or_die(&manifest);
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/fold.json";
    const char *output = "test_patch_tmp/fold.cmo";
    remove(patch);
    remove(output);
    write_fold_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("fold", get_string_field(op, "op"));
    ASSERT_EQ(manifest.fold_parent_id,
              (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_EQ(0u, (uint32_t)get_uint_field(op, "status"));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "patch diff \"%s\"", patch);
    assert_cli_success(args, "fold #4692");

    remove(patch);
}

TEST(cli, patch_apply_fold_dry_run_reports_semantic_risks) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/fold_risky.json";
    const char *output = "test_patch_tmp/fold_risky.cmo";
    remove(patch);
    remove(output);
    write_risky_fold_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_STR_EQ(output, get_string_field(data, "output_path"));
    ASSERT_NOT_NULL(get_array_field(data, "errors"));
    ASSERT_NOT_NULL(get_array_field(data, "warnings"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 363u));
    ASSERT_TRUE(array_contains_object_id(changed_objects, 237u));
    ASSERT_TRUE(array_contains_object_id(changed_objects, 358u));
    ASSERT_TRUE(yyjson_obj_get(data, "risk_level") == NULL);
    yyjson_val *root_risks = get_array_field(data, "semantic_risks");
    ASSERT_NOT_NULL(root_risks);
    ASSERT_NOT_NULL(find_object_by_string_field(
        root_risks, "code", "shared_parameter"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("fold", get_string_field(op, "op"));
    ASSERT_EQ(0u, (uint32_t)get_uint_field(op, "status"));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);

    remove(patch);
}

TEST(cli, patch_diff_json_reports_fold_delete_plan) {
    rewrite_manifest_t manifest;

    load_ballance_manifest_or_die(&manifest);
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/fold_closed_graph.json";
    const char *output = "test_patch_tmp/fold_closed_graph.cmo";
    remove(patch);
    remove(output);
    write_closed_graph_fold_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch diff \"%s\"", patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.diff", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(1u, (uint32_t)get_uint_field(data, "operation_count"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("fold", get_string_field(op, "op"));
    ASSERT_EQ(manifest.fold_parent_id,
              (uint32_t)get_uint_field(op, "primary_id"));

    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_TRUE(array_contains_object_id(deleted_objects, 4140u));
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_TRUE(get_uint_field(diff, "deleted_object_count") > 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);

    remove(patch);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, patch_apply_rejects_non_leaf_replace_bb);
    REGISTER_TEST(cli, patch_apply_json_failure_reports_edit_report);
    REGISTER_TEST(cli, patch_apply_v2_replace_bb_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_node_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_io_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_remove_io_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_rename_io_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_interface_policy_dry_run);
    REGISTER_TEST(cli, patch_apply_fold_dry_run_reports_analysis);
    REGISTER_TEST(cli, patch_apply_fold_dry_run_reports_semantic_risks);
    REGISTER_TEST(cli, patch_diff_json_reports_fold_delete_plan);
    REGISTER_TEST(cli, patch_apply_leaf_replace_bb_dry_run_and_apply);
TEST_MAIN_END()
