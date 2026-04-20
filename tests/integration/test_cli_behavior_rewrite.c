/**
 * @file test_cli_behavior_rewrite.c
 * @brief CLI behavior graph rewrite smoke tests
 */

#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
    cli_run_result_t result;
    result.output = NULL;
    result.exit_code = -1;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) {
        return result;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buffer = (char *)malloc(cap);
    if (!buffer) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1 > cap) {
            size_t new_cap = cap * 2;
            while (new_cap < len + chunk_len + 1) {
                new_cap *= 2;
            }
            char *new_buf = (char *)realloc(buffer, new_cap);
            if (!new_buf) {
                free(buffer);
                result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
                return result;
            }
            buffer = new_buf;
            cap = new_cap;
        }
        memcpy(buffer + len, chunk, chunk_len);
        len += chunk_len;
    }

    buffer[len] = '\0';
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    result.output = buffer;
    return result;
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

static bool get_bool_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_bool(val) && yyjson_get_bool(val);
}

static uint64_t get_uint_field(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val && yyjson_is_uint(val)) ? yyjson_get_uint(val) : 0;
}

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void make_dir(const char *path) {
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
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

static bool array_contains_uint(yyjson_val *arr, uint64_t needle) {
    size_t idx;
    size_t max;
    yyjson_val *item;

    if (!arr) {
        return false;
    }
    yyjson_arr_foreach(arr, idx, max, item) {
        if (yyjson_is_uint(item) && yyjson_get_uint(item) == needle) {
            return true;
        }
    }
    return false;
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
            get_uint_field(item, "id") == needle) {
            return true;
        }
    }
    return false;
}

static yyjson_val *find_object_by_uint_field(yyjson_val *arr,
                                             const char *key,
                                             uint64_t needle) {
    size_t idx;
    size_t max;
    yyjson_val *item;

    if (!arr) {
        return NULL;
    }
    yyjson_arr_foreach(arr, idx, max, item) {
        if (yyjson_is_obj(item) &&
            get_uint_field(item, key) == needle) {
            return item;
        }
    }
    return NULL;
}

TEST(cli, behavior_graph_boundary_json_smoke) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior graph-boundary 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.graph-boundary", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *behavior_id = yyjson_obj_get(data, "behavior_id");
    ASSERT_TRUE(behavior_id && yyjson_is_uint(behavior_id));
    ASSERT_EQ(237u, (uint32_t)yyjson_get_uint(behavior_id));

    yyjson_val *internal_nodes = get_array_field(data, "internal_nodes");
    ASSERT_NOT_NULL(internal_nodes);
    ASSERT_TRUE(array_contains_uint(internal_nodes, 237u));

    ASSERT_NOT_NULL(get_array_field(data, "control_in"));
    ASSERT_NOT_NULL(get_array_field(data, "control_out"));
    ASSERT_NOT_NULL(get_array_field(data, "parameter_in"));
    ASSERT_NOT_NULL(get_array_field(data, "parameter_out"));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_replace_bb_dry_run_reports_leaf_preservation) {
    remove("test_behavior_rewrite_tmp/replace_bb_dry.cmo");
    make_dir("test_behavior_rewrite_tmp");

    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior replace-bb 343 "
             "--guid 42414C02-10000002 "
             "--name \"Ballance Load NMO Range\" "
             "--preserve-links --preserve-params --dry-run \"%s\" "
             "-o \"test_behavior_rewrite_tmp/replace_bb_dry.cmo\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.replace-bb", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(get_bool_field(data, "changed"));
    ASSERT_EQ(343u, (uint32_t)get_uint_field(data, "behavior_id"));

    yyjson_val *before = get_object_field(data, "before");
    yyjson_val *after = get_object_field(data, "after");
    ASSERT_NOT_NULL(before);
    ASSERT_NOT_NULL(after);
    ASSERT_STR_EQ("7BD977D7-26396C0C", get_string_field(before, "guid"));
    ASSERT_STR_EQ("42414C02-10000002", get_string_field(after, "guid"));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(before, "flags")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(after, "flags")));

    yyjson_val *eligibility = get_object_field(data, "eligibility");
    ASSERT_NOT_NULL(eligibility);
    ASSERT_TRUE(get_bool_field(eligibility, "leaf"));
    ASSERT_EQ(0u, (uint32_t)get_uint_field(eligibility, "sub_behaviors"));
    ASSERT_EQ(0u, (uint32_t)get_uint_field(eligibility, "sub_behavior_links"));
    ASSERT_EQ(0u, (uint32_t)get_uint_field(eligibility, "operations"));

    yyjson_val *preserved = get_object_field(data, "preserved");
    ASSERT_NOT_NULL(preserved);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(preserved, "control_in")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(preserved, "control_out")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(preserved, "parameter_in")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(preserved, "parameter_out")));

    ASSERT_FALSE(file_exists("test_behavior_rewrite_tmp/replace_bb_dry.cmo"));
    yyjson_doc_free(doc);
}

TEST(cli, behavior_replace_bb_rejects_non_leaf_script) {
    remove("test_behavior_rewrite_tmp/replace_bb_reject.cmo");
    make_dir("test_behavior_rewrite_tmp");

    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior replace-bb 363 "
             "--guid 42414C02-10000002 "
             "--name \"Ballance Load NMO Range\" "
             "--preserve-links --preserve-params --dry-run \"%s\" "
             "-o \"test_behavior_rewrite_tmp/replace_bb_reject.cmo\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "not leaf-replaceable");
    ASSERT_STR_CONTAINS(result.output, "sub_behaviors");
    ASSERT_STR_CONTAINS(result.output, "sub_behavior_links");
    ASSERT_STR_CONTAINS(result.output, "operations");
    ASSERT_FALSE(file_exists("test_behavior_rewrite_tmp/replace_bb_reject.cmo"));
    free(result.output);
}

TEST(cli, behavior_replace_bb_saves_output) {
    const char *output = "test_behavior_rewrite_tmp/replace_bb_save.cmo";
    remove(output);
    make_dir("test_behavior_rewrite_tmp");

    char args[2048];
    snprintf(args, sizeof(args),
             "behavior replace-bb 343 "
             "--guid 42414C02-10000002 "
             "--name \"Ballance Load NMO Range\" "
             "--preserve-links --preserve-params \"%s\" -o \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);

    assert_cli_success(args, "Saved to");
    ASSERT_TRUE(file_exists(output));
    assert_validate_ok(output);

    char show_args[1024];
    snprintf(show_args, sizeof(show_args), "-f json behavior show 343 \"%s\"",
             output);
    yyjson_doc *doc = NULL;
    run_json_command(show_args, "behavior.show", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    ASSERT_STR_EQ("BB", get_string_field(data, "behavior_type"));
    ASSERT_STR_EQ("42414C02-10000002", get_string_field(data, "bb_guid"));

    yyjson_val *inputs = get_array_field(data, "inputs");
    yyjson_val *outputs = get_array_field(data, "outputs");
    ASSERT_NOT_NULL(inputs);
    ASSERT_NOT_NULL(outputs);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(inputs));
    ASSERT_EQ(3u, (uint32_t)yyjson_arr_size(outputs));
    ASSERT_TRUE(array_contains_object_id(inputs, 324u));
    ASSERT_TRUE(array_contains_object_id(inputs, 325u));
    ASSERT_TRUE(array_contains_object_id(outputs, 326u));
    ASSERT_TRUE(array_contains_object_id(outputs, 327u));
    ASSERT_TRUE(array_contains_object_id(outputs, 328u));

    yyjson_doc_free(doc);
    remove(output);
}

TEST(cli, behavior_fold_candidates_reports_parent_boundary) {
    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold-candidates --parent 363 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.fold-candidates", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(363u, (uint32_t)get_uint_field(data, "parent_id"));

    yyjson_val *parent = get_object_field(data, "parent");
    ASSERT_NOT_NULL(parent);
    ASSERT_STR_EQ("Script", get_string_field(parent, "behavior_type"));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(parent, "flags")));

    yyjson_val *groups = get_array_field(data, "candidate_groups");
    ASSERT_NOT_NULL(groups);
    ASSERT_TRUE(yyjson_arr_size(groups) >= 1);
    yyjson_val *group = yyjson_arr_get(groups, 0);
    ASSERT_TRUE(group && yyjson_is_obj(group));
    ASSERT_NOT_NULL(get_array_field(group, "nodes"));
    ASSERT_NOT_NULL(get_array_field(group, "control_in"));
    ASSERT_NOT_NULL(get_array_field(group, "control_out"));
    ASSERT_NOT_NULL(get_array_field(group, "parameter_in"));
    ASSERT_NOT_NULL(get_array_field(group, "parameter_out"));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_fold_candidates_reports_direct_child_groups) {
    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold-candidates --parent 4692 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.fold-candidates", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(4692u, (uint32_t)get_uint_field(data, "parent_id"));

    yyjson_val *groups = get_array_field(data, "candidate_groups");
    ASSERT_NOT_NULL(groups);
    ASSERT_TRUE(yyjson_arr_size(groups) > 1);

    bool saw_direct_child = false;
    size_t idx;
    size_t max;
    yyjson_val *group;
    yyjson_arr_foreach(groups, idx, max, group) {
        if (!yyjson_is_obj(group) ||
            strcmp("direct_child", get_string_field(group, "kind")) != 0) {
            continue;
        }

        uint64_t root_id = get_uint_field(group, "root_id");
        yyjson_val *nodes = get_array_field(group, "nodes");
        yyjson_val *interface_obj = get_object_field(group, "interface");
        ASSERT_TRUE(root_id > 0);
        ASSERT_NOT_NULL(get_string_field(group, "root_behavior_type"));
        ASSERT_NOT_NULL(nodes);
        ASSERT_TRUE(array_contains_uint(nodes, root_id));
        ASSERT_NOT_NULL(get_array_field(group, "control_in"));
        ASSERT_NOT_NULL(get_array_field(group, "control_out"));
        ASSERT_NOT_NULL(get_array_field(group, "parameter_in"));
        ASSERT_NOT_NULL(get_array_field(group, "parameter_out"));
        ASSERT_NOT_NULL(interface_obj);
        ASSERT_TRUE(yyjson_is_bool(yyjson_obj_get(interface_obj, "available")));
        ASSERT_NOT_NULL(get_string_field(interface_obj, "action"));
        saw_direct_child = true;
        break;
    }

    ASSERT_TRUE(saw_direct_child);
    yyjson_doc_free(doc);
}

TEST(cli, behavior_fold_dry_run_reports_boundary_plan) {
    const char *output = "test_behavior_rewrite_tmp/fold_dry.cmo";
    remove(output);
    make_dir("test_behavior_rewrite_tmp");

    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold --parent 4692 --nodes 2364 "
             "--guid 42414C07-10000007 "
             "--name \"Ballance Event Handler\" "
             "--preserve-links --preserve-params --dry-run \"%s\" -o \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.fold", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    yyjson_val *can_write = yyjson_obj_get(data, "can_write");
    ASSERT_TRUE(can_write && yyjson_is_bool(can_write));
    ASSERT_FALSE(yyjson_get_bool(can_write));
    yyjson_val *write_supported = yyjson_obj_get(data, "write_supported");
    ASSERT_TRUE(write_supported && yyjson_is_bool(write_supported));
    ASSERT_FALSE(get_bool_field(data, "write_supported"));
    yyjson_val *write_blockers = get_array_field(data, "write_blockers");
    ASSERT_NOT_NULL(write_blockers);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(write_blockers));
    yyjson_val *blocker = yyjson_arr_get(write_blockers, 0);
    ASSERT_TRUE(blocker && yyjson_is_obj(blocker));
    ASSERT_STR_EQ("analysis_only", get_string_field(blocker, "code"));
    ASSERT_NOT_NULL(get_string_field(blocker, "message"));
    ASSERT_EQ(4692u, (uint32_t)get_uint_field(data, "parent_id"));
    ASSERT_EQ(2364u, (uint32_t)get_uint_field(data, "representative_id"));

    yyjson_val *target = get_object_field(data, "target");
    ASSERT_NOT_NULL(target);
    ASSERT_STR_EQ("42414C07-10000007", get_string_field(target, "guid"));
    ASSERT_STR_EQ("Ballance Event Handler", get_string_field(target, "name"));

    yyjson_val *selected_nodes = get_array_field(data, "selected_nodes");
    ASSERT_NOT_NULL(selected_nodes);
    ASSERT_TRUE(array_contains_uint(selected_nodes, 2364u));

    yyjson_val *planned = get_object_field(data, "planned");
    ASSERT_NOT_NULL(planned);
    ASSERT_NOT_NULL(get_array_field(planned, "internal_nodes"));
    ASSERT_TRUE(array_contains_uint(get_array_field(planned, "internal_nodes"),
                                    2364u));
    ASSERT_NOT_NULL(get_array_field(planned, "nodes_to_delete"));

    yyjson_val *links_to_move = get_object_field(planned, "links_to_move");
    ASSERT_NOT_NULL(links_to_move);
    ASSERT_NOT_NULL(get_array_field(links_to_move, "control_in"));
    ASSERT_NOT_NULL(get_array_field(links_to_move, "control_out"));

    yyjson_val *parameters = get_object_field(planned, "parameters_to_preserve");
    ASSERT_NOT_NULL(parameters);
    ASSERT_NOT_NULL(get_array_field(parameters, "parameter_in"));
    ASSERT_NOT_NULL(get_array_field(parameters, "parameter_out"));

    yyjson_val *interface_obj = get_object_field(planned, "interface");
    ASSERT_NOT_NULL(interface_obj);
    ASSERT_NOT_NULL(get_string_field(interface_obj, "action"));

    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
}

TEST(cli, behavior_fold_dry_run_uses_explicit_node_set) {
    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold --parent 4692 --nodes 2364,2178 "
             "--guid 42414C07-10000007 "
             "--name \"Ballance Event Handler\" "
             "--preserve-links --preserve-params --dry-run \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.fold", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *selected_nodes = get_array_field(data, "selected_nodes");
    ASSERT_NOT_NULL(selected_nodes);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(selected_nodes));
    ASSERT_TRUE(array_contains_uint(selected_nodes, 2364u));
    ASSERT_TRUE(array_contains_uint(selected_nodes, 2178u));

    yyjson_val *planned = get_object_field(data, "planned");
    ASSERT_NOT_NULL(planned);
    ASSERT_EQ(2u, (uint32_t)get_uint_field(planned, "node_count"));

    yyjson_val *internal_nodes = get_array_field(planned, "internal_nodes");
    ASSERT_NOT_NULL(internal_nodes);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(internal_nodes));
    ASSERT_TRUE(array_contains_uint(internal_nodes, 2364u));
    ASSERT_TRUE(array_contains_uint(internal_nodes, 2178u));

    yyjson_val *nodes_to_delete = get_array_field(planned, "nodes_to_delete");
    ASSERT_NOT_NULL(nodes_to_delete);
    ASSERT_TRUE(array_contains_uint(nodes_to_delete, 2178u));
    ASSERT_FALSE(array_contains_uint(nodes_to_delete, 2364u));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_fold_dry_run_uses_explicit_anchor) {
    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold --parent 4692 --nodes 2364,2208 "
             "--anchor 2208 "
             "--guid 42414C07-10000007 "
             "--name \"Ballance Event Handler\" "
             "--preserve-links --preserve-params --dry-run \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.fold", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(2208u, (uint32_t)get_uint_field(data, "anchor_id"));
    ASSERT_EQ(2208u, (uint32_t)get_uint_field(data, "representative_id"));

    yyjson_val *planned = get_object_field(data, "planned");
    ASSERT_NOT_NULL(planned);
    yyjson_val *nodes_to_delete = get_array_field(planned, "nodes_to_delete");
    ASSERT_NOT_NULL(nodes_to_delete);
    ASSERT_TRUE(array_contains_uint(nodes_to_delete, 2364u));
    ASSERT_FALSE(array_contains_uint(nodes_to_delete, 2208u));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_fold_dry_run_reports_control_rewire_plan) {
    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold --parent 4692 --nodes 2364,2208 "
             "--guid 42414C07-10000007 "
             "--name \"Ballance Event Handler\" "
             "--preserve-links --preserve-params --dry-run \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.fold", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *planned = get_object_field(data, "planned");
    ASSERT_NOT_NULL(planned);

    yyjson_val *links_to_delete = get_object_field(planned, "links_to_delete");
    ASSERT_NOT_NULL(links_to_delete);
    yyjson_val *delete_control = get_array_field(links_to_delete, "control");
    ASSERT_NOT_NULL(delete_control);
    ASSERT_NOT_NULL(find_object_by_uint_field(delete_control, "link_id",
                                              2357u));

    yyjson_val *links_to_retarget =
        get_object_field(planned, "links_to_retarget");
    ASSERT_NOT_NULL(links_to_retarget);
    yyjson_val *retarget_in = get_array_field(links_to_retarget, "control_in");
    yyjson_val *retarget_out =
        get_array_field(links_to_retarget, "control_out");
    ASSERT_NOT_NULL(retarget_in);
    ASSERT_NOT_NULL(retarget_out);

    yyjson_val *out_link = find_object_by_uint_field(retarget_out,
                                                     "link_id", 2345u);
    ASSERT_NOT_NULL(out_link);
    ASSERT_EQ(2208u, (uint32_t)get_uint_field(out_link,
                                              "old_source_owner_id"));
    ASSERT_EQ(2364u, (uint32_t)get_uint_field(out_link,
                                              "new_source_owner_id"));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_fold_dry_run_rejects_parent_in_selected_nodes) {
    char args[2048];
    snprintf(args, sizeof(args),
             "-f json behavior fold --parent 2378 --nodes 2378,2374 "
             "--guid 42414C07-10000007 "
             "--name \"Param Fold\" "
             "--preserve-links --preserve-params --dry-run \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "parent_selected");
    ASSERT_STR_CONTAINS(result.output,
                        "must not include the parent behavior");
    free(result.output);
}

TEST(cli, behavior_fold_write_rejects_with_analysis_blocker) {
    const char *output = "test_behavior_rewrite_tmp/fold_write_reject.cmo";
    remove(output);
    make_dir("test_behavior_rewrite_tmp");

    char args[2048];
    snprintf(args, sizeof(args),
             "behavior fold --parent 4692 --nodes 2364,2208 "
             "--guid 42414C07-10000007 "
             "--name \"Ballance Event Handler\" "
             "--preserve-links --preserve-params \"%s\" -o \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "analysis_only");
    ASSERT_STR_CONTAINS(result.output, "write mode is not implemented");
    ASSERT_FALSE(file_exists(output));
    free(result.output);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, behavior_graph_boundary_json_smoke);
    REGISTER_TEST(cli, behavior_replace_bb_dry_run_reports_leaf_preservation);
    REGISTER_TEST(cli, behavior_replace_bb_rejects_non_leaf_script);
    REGISTER_TEST(cli, behavior_replace_bb_saves_output);
    REGISTER_TEST(cli, behavior_fold_candidates_reports_parent_boundary);
    REGISTER_TEST(cli, behavior_fold_candidates_reports_direct_child_groups);
    REGISTER_TEST(cli, behavior_fold_dry_run_reports_boundary_plan);
    REGISTER_TEST(cli, behavior_fold_dry_run_uses_explicit_node_set);
    REGISTER_TEST(cli, behavior_fold_dry_run_uses_explicit_anchor);
    REGISTER_TEST(cli, behavior_fold_dry_run_reports_control_rewire_plan);
    REGISTER_TEST(cli, behavior_fold_dry_run_rejects_parent_in_selected_nodes);
    REGISTER_TEST(cli, behavior_fold_write_rejects_with_analysis_blocker);
TEST_MAIN_END()
