#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static cli_run_result_t run_cli_capture(const char *args)
{
    cli_run_result_t result = {0};
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
        result.exit_code = NMO_PCLOSE(pipe);
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
            char *new_buffer = (char *)realloc(buffer, new_cap);
            if (!new_buffer) {
                free(buffer);
                result.exit_code = NMO_PCLOSE(pipe);
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
    result.exit_code = NMO_PCLOSE(pipe);
    return result;
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

TEST(cli, script_graph_json_smoke)
{
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json script graph 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ("nmo", get_string_field(root, "tool"));
    ASSERT_STR_EQ("script.graph", get_string_field(root, "command"));

    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_is_bool(yyjson_obj_get(data, "edit_ready")));
    ASSERT_TRUE(yyjson_is_bool(yyjson_obj_get(data, "owner_index_available")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(data, "root_behavior_id")));

    yyjson_val *reference_validation =
        get_object_field(data, "reference_validation");
    ASSERT_NOT_NULL(reference_validation);
    ASSERT_TRUE(yyjson_is_int(yyjson_obj_get(reference_validation, "status")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(reference_validation,
                                              "broken_count")));

    yyjson_val *control_edges = get_array_field(data, "control_edges");
    yyjson_val *data_edges = get_array_field(data, "data_edges");
    ASSERT_NOT_NULL(control_edges);
    ASSERT_NOT_NULL(data_edges);
    ASSERT_TRUE(yyjson_arr_size(control_edges) > 0u);
    ASSERT_TRUE(yyjson_arr_size(data_edges) > 0u);

    yyjson_doc_free(doc);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, script_graph_json_smoke);
TEST_MAIN_END()
