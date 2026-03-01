/**
 * @file test_cli_behavior_graph.c
 * @brief CLI behavior graph smoke tests
 */

#include "test_framework.h"

#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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

static char *read_command_output(const char *command) {
    if (!command) {
        return NULL;
    }

    FILE *pipe = NMO_POPEN(command, "r");
    if (!pipe) {
        return NULL;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buffer = (char *)malloc(cap);
    if (!buffer) {
        NMO_PCLOSE(pipe);
        return NULL;
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
                NMO_PCLOSE(pipe);
                return NULL;
            }
            buffer = new_buf;
            cap = new_cap;
        }
        memcpy(buffer + len, chunk, chunk_len);
        len += chunk_len;
    }

    buffer[len] = '\0';
    NMO_PCLOSE(pipe);
    return buffer;
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

TEST(cli, behavior_graph_json) {
    const char *file_path = NMO_TEST_DATA_FILE("base.cmo");

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s -f json behavior list \"%s\"", NMO_CLI_PATH, file_path);
    char *list_output = read_command_output(cmd);
    ASSERT_NOT_NULL(list_output);

    yyjson_doc *list_doc = yyjson_read(list_output, strlen(list_output), 0);
    free(list_output);
    ASSERT_NOT_NULL(list_doc);

    yyjson_val *list_root = yyjson_doc_get_root(list_doc);
    ASSERT_NOT_NULL(list_root);

    const char *tool = get_string_field(list_root, "tool");
    ASSERT_TRUE(tool && strcmp(tool, "nmo") == 0);

    const char *command = get_string_field(list_root, "command");
    ASSERT_TRUE(command && strcmp(command, "behavior.list") == 0);

    yyjson_val *list_data = get_object_field(list_root, "data");
    ASSERT_NOT_NULL(list_data);

    yyjson_val *objects = get_array_field(list_data, "objects");
    ASSERT_NOT_NULL(objects);

    size_t obj_count = yyjson_arr_size(objects);
    ASSERT_TRUE(obj_count > 0);

    yyjson_val *first_obj = yyjson_arr_get(objects, 0);
    ASSERT_NOT_NULL(first_obj);

    yyjson_val *id_val = yyjson_obj_get(first_obj, "id");
    ASSERT_TRUE(id_val && yyjson_is_uint(id_val));
    uint32_t behavior_id = (uint32_t)yyjson_get_uint(id_val);

    yyjson_doc_free(list_doc);

    snprintf(cmd, sizeof(cmd),
             "%s -f json behavior graph --max-nodes 5 --max-edges 5 %u \"%s\"",
             NMO_CLI_PATH, behavior_id, file_path);
    char *graph_output = read_command_output(cmd);
    ASSERT_NOT_NULL(graph_output);

    yyjson_doc *graph_doc = yyjson_read(graph_output, strlen(graph_output), 0);
    free(graph_output);
    ASSERT_NOT_NULL(graph_doc);

    yyjson_val *graph_root = yyjson_doc_get_root(graph_doc);
    ASSERT_NOT_NULL(graph_root);

    command = get_string_field(graph_root, "command");
    ASSERT_TRUE(command && strcmp(command, "behavior.graph") == 0);

    yyjson_val *graph_data = get_object_field(graph_root, "data");
    ASSERT_NOT_NULL(graph_data);

    yyjson_val *graph = get_object_field(graph_data, "graph");
    ASSERT_NOT_NULL(graph);

    yyjson_val *nodes = get_array_field(graph, "nodes");
    yyjson_val *edges = get_array_field(graph, "edges");
    ASSERT_NOT_NULL(nodes);
    ASSERT_NOT_NULL(edges);

    ASSERT_TRUE(yyjson_arr_size(nodes) <= 5);
    ASSERT_TRUE(yyjson_arr_size(edges) <= 5);

    yyjson_val *truncated = get_object_field(graph, "truncated");
    if (truncated) {
        yyjson_val *trunc_nodes = yyjson_obj_get(truncated, "nodes");
        yyjson_val *trunc_edges = yyjson_obj_get(truncated, "edges");
        ASSERT_TRUE(trunc_nodes && yyjson_is_bool(trunc_nodes));
        ASSERT_TRUE(trunc_edges && yyjson_is_bool(trunc_edges));
    }

    yyjson_doc_free(graph_doc);
}

TEST(cli, behavior_graph_dot) {
    const char *file_path = NMO_TEST_DATA_FILE("base.cmo");

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s -f json behavior list \"%s\"", NMO_CLI_PATH, file_path);
    char *list_output = read_command_output(cmd);
    ASSERT_NOT_NULL(list_output);

    yyjson_doc *list_doc = yyjson_read(list_output, strlen(list_output), 0);
    free(list_output);
    ASSERT_NOT_NULL(list_doc);

    yyjson_val *list_root = yyjson_doc_get_root(list_doc);
    ASSERT_NOT_NULL(list_root);

    yyjson_val *list_data = get_object_field(list_root, "data");
    ASSERT_NOT_NULL(list_data);

    yyjson_val *objects = get_array_field(list_data, "objects");
    ASSERT_NOT_NULL(objects);

    size_t obj_count = yyjson_arr_size(objects);
    ASSERT_TRUE(obj_count > 0);

    yyjson_val *first_obj = yyjson_arr_get(objects, 0);
    ASSERT_NOT_NULL(first_obj);

    yyjson_val *id_val = yyjson_obj_get(first_obj, "id");
    ASSERT_TRUE(id_val && yyjson_is_uint(id_val));
    uint32_t behavior_id = (uint32_t)yyjson_get_uint(id_val);

    yyjson_doc_free(list_doc);

    snprintf(cmd, sizeof(cmd), "%s behavior graph --dot %u \"%s\"", NMO_CLI_PATH, behavior_id, file_path);
    char *dot_output = read_command_output(cmd);
    ASSERT_NOT_NULL(dot_output);

    ASSERT_TRUE(strstr(dot_output, "digraph behavior_graph") != NULL);
    free(dot_output);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, behavior_graph_json);
    REGISTER_TEST(cli, behavior_graph_dot);
TEST_MAIN_END()
