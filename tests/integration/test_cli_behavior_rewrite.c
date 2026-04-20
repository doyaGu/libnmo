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
#include <sys/wait.h>
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

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, behavior_graph_boundary_json_smoke);
TEST_MAIN_END()
