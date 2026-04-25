#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
#include "document/nmo_document_save.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#else
#include <direct.h>
#endif

#define NMO_SCRIPT_INTERFACE_FIXTURE NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo")
#define NMO_SCRIPT_INTERFACE_TARGET_ID 253u

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

typedef struct rewrite_manifest {
    uint32_t root_behavior_id;
    uint32_t replace_parent_id;
    uint32_t replace_node_id;
    char replace_guid[64];
    char replace_name[128];
    uint32_t fold_parent_id;
    uint32_t fold_anchor_id;
    uint32_t fold_node_ids[32];
    size_t fold_node_count;
} rewrite_manifest_t;

static yyjson_val *find_array_object_by_name(yyjson_val *arr, const char *name);

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

static uint32_t get_array_uint_at(yyjson_val *arr, size_t index)
{
    yyjson_val *val = yyjson_arr_get(arr, index);
    if (val && yyjson_is_uint(val)) {
        return (uint32_t)yyjson_get_uint(val);
    }
    if (val && yyjson_is_obj(val)) {
        return (uint32_t)get_uint_field(val, "id");
    }
    return 0u;
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void make_dir(const char *path)
{
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static void assert_validate_ok(const char *path)
{
    char args[1024];
    cli_run_result_t result;

    snprintf(args, sizeof(args), "validate all \"%s\"", path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Result: VALID");
    free(result.output);
}

static void load_behavior_first_ios(const char *path,
                                    uint32_t behavior_id,
                                    uint32_t *out_input_id,
                                    uint32_t *out_output_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *inputs = NULL;
    yyjson_val *outputs = NULL;

    if (out_input_id) {
        *out_input_id = 0u;
    }
    if (out_output_id) {
        *out_output_id = 0u;
    }

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    inputs = get_array_field(data, "inputs");
    outputs = get_array_field(data, "outputs");
    ASSERT_NOT_NULL(inputs);
    ASSERT_NOT_NULL(outputs);
    ASSERT_TRUE(yyjson_arr_size(inputs) > 0u);
    ASSERT_TRUE(yyjson_arr_size(outputs) > 0u);

    if (out_input_id) {
        *out_input_id = get_array_uint_at(inputs, 0u);
        ASSERT_TRUE(*out_input_id != 0u);
    }
    if (out_output_id) {
        *out_output_id = get_array_uint_at(outputs, 0u);
        ASSERT_TRUE(*out_output_id != 0u);
    }

    yyjson_doc_free(doc);
}

static void load_behavior_io_by_name(const char *path,
                                     uint32_t behavior_id,
                                     const char *field_name,
                                     const char *io_name,
                                     uint32_t *out_io_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *arr = NULL;
    yyjson_val *item = NULL;

    if (out_io_id) {
        *out_io_id = 0u;
    }

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    arr = get_array_field(data, field_name);
    ASSERT_NOT_NULL(arr);
    item = find_array_object_by_name(arr, io_name);
    ASSERT_NOT_NULL(item);

    if (out_io_id) {
        *out_io_id = (uint32_t)get_uint_field(item, "id");
    }

    yyjson_doc_free(doc);
}

static void load_behavior_io_entry_by_name(const char *path,
                                           uint32_t behavior_id,
                                           const char *field_name,
                                           const char *io_name,
                                           uint32_t *out_io_id,
                                           uint32_t *out_index)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *arr = NULL;
    yyjson_val *item = NULL;

    if (out_io_id) {
        *out_io_id = 0u;
    }
    if (out_index) {
        *out_index = 0u;
    }

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    arr = get_array_field(data, field_name);
    ASSERT_NOT_NULL(arr);
    item = find_array_object_by_name(arr, io_name);
    ASSERT_NOT_NULL(item);

    if (out_io_id) {
        *out_io_id = (uint32_t)get_uint_field(item, "id");
    }
    if (out_index) {
        *out_index = (uint32_t)get_uint_field(item, "index");
    }

    yyjson_doc_free(doc);
}

static yyjson_val *find_control_edge_by_link_id(yyjson_val *edges, uint32_t link_id)
{
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *edge = NULL;

    if (!edges) {
        return NULL;
    }

    yyjson_arr_foreach(edges, idx, max, edge) {
        if (get_uint_field(edge, "link_id") == link_id) {
            return edge;
        }
    }

    return NULL;
}

static yyjson_val *find_array_object_by_id(yyjson_val *arr, uint32_t id)
{
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *item = NULL;

    if (!arr) {
        return NULL;
    }

    yyjson_arr_foreach(arr, idx, max, item) {
        if (get_uint_field(item, "id") == id) {
            return item;
        }
    }

    return NULL;
}

static yyjson_val *find_array_object_by_behavior_id(yyjson_val *arr,
                                                    uint32_t behavior_id)
{
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *item = NULL;

    if (!arr) {
        return NULL;
    }

    yyjson_arr_foreach(arr, idx, max, item) {
        if (get_uint_field(item, "behavior_id") == behavior_id) {
            return item;
        }
    }

    return NULL;
}

static yyjson_val *find_array_object_by_name(yyjson_val *arr, const char *name)
{
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *item = NULL;

    if (!arr || !name) {
        return NULL;
    }

    yyjson_arr_foreach(arr, idx, max, item) {
        const char *item_name = get_string_field(item, "name");
        if (item_name && strcmp(item_name, name) == 0) {
            return item;
        }
    }

    return NULL;
}

static yyjson_val *find_data_edge(yyjson_val *edges,
                                  uint32_t source_parameter_id,
                                  uint32_t target_parameter_id)
{
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *edge = NULL;

    if (!edges) {
        return NULL;
    }

    yyjson_arr_foreach(edges, idx, max, edge) {
        if (get_uint_field(edge, "source_parameter_id") == source_parameter_id &&
            get_uint_field(edge, "target_parameter_id") == target_parameter_id) {
            return edge;
        }
    }

    return NULL;
}

static void load_behavior_parameter_entry(const char *path,
                                          uint32_t behavior_id,
                                          const char *field_name,
                                          uint32_t parameter_id,
                                          uint32_t *out_index,
                                          yyjson_doc **out_doc,
                                          yyjson_val **out_item)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *arr = NULL;
    yyjson_val *item = NULL;

    if (out_index) {
        *out_index = 0u;
    }
    if (out_doc) {
        *out_doc = NULL;
    }
    if (out_item) {
        *out_item = NULL;
    }

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    arr = get_array_field(data, field_name);
    ASSERT_NOT_NULL(arr);
    item = find_array_object_by_id(arr, parameter_id);
    ASSERT_NOT_NULL(item);

    if (out_index) {
        *out_index = (uint32_t)get_uint_field(item, "index");
    }
    if (out_item) {
        *out_item = item;
    }
    if (out_doc) {
        *out_doc = doc;
    } else {
        yyjson_doc_free(doc);
    }
}

static void load_behavior_parameter_entry_by_name(const char *path,
                                                  uint32_t behavior_id,
                                                  const char *field_name,
                                                  const char *parameter_name,
                                                  uint32_t *out_parameter_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *arr = NULL;
    yyjson_val *item = NULL;

    if (out_parameter_id) {
        *out_parameter_id = 0u;
    }

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             behavior_id,
             path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    arr = get_array_field(data, field_name);
    ASSERT_NOT_NULL(arr);
    item = find_array_object_by_name(arr, parameter_name);
    ASSERT_NOT_NULL(item);

    if (out_parameter_id) {
        *out_parameter_id = (uint32_t)get_uint_field(item, "id");
    }

    yyjson_doc_free(doc);
}

static void assert_parameter_show_value(const char *path,
                                        uint32_t parameter_id,
                                        const char *expected_value,
                                        uint32_t expected_destination_count)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;

    snprintf(args, sizeof(args),
             "-f json parameter show %u \"%s\"",
             parameter_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(expected_value, get_string_field(data, "value"));
    ASSERT_EQ(expected_destination_count,
              (uint32_t)get_uint_field(data, "destination_count"));
    yyjson_doc_free(doc);
}

static void assert_parameter_show_source(const char *path,
                                         uint32_t parameter_id,
                                         uint32_t expected_source_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;

    snprintf(args, sizeof(args),
             "-f json parameter show %u \"%s\"",
             parameter_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(expected_source_id,
              (uint32_t)get_uint_field(data, "source_id"));
    yyjson_doc_free(doc);
}

static void assert_script_graph_data_edge_present(const char *path,
                                                  uint32_t root_behavior_id,
                                                  uint32_t source_parameter_id,
                                                  uint32_t target_parameter_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *edges = NULL;

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             root_behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    edges = get_array_field(data, "data_edges");
    ASSERT_NOT_NULL(edges);
    ASSERT_NOT_NULL(find_data_edge(edges, source_parameter_id, target_parameter_id));
    yyjson_doc_free(doc);
}

static void assert_script_graph_data_edge_missing(const char *path,
                                                  uint32_t root_behavior_id,
                                                  uint32_t source_parameter_id,
                                                  uint32_t target_parameter_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *edges = NULL;

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             root_behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    edges = get_array_field(data, "data_edges");
    ASSERT_NOT_NULL(edges);
    ASSERT_NULL(find_data_edge(edges, source_parameter_id, target_parameter_id));
    yyjson_doc_free(doc);
}

static void assert_script_graph_link(const char *path,
                                     uint32_t root_behavior_id,
                                     uint32_t link_id,
                                     uint32_t source_owner_id,
                                     uint32_t target_owner_id,
                                     int32_t activation_delay)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *edges = NULL;
    yyjson_val *edge = NULL;
    yyjson_val *source = NULL;
    yyjson_val *target = NULL;

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             root_behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    edges = get_array_field(data, "control_edges");
    ASSERT_NOT_NULL(edges);

    edge = find_control_edge_by_link_id(edges, link_id);
    ASSERT_NOT_NULL(edge);
    source = get_object_field(edge, "source");
    target = get_object_field(edge, "target");
    ASSERT_NOT_NULL(source);
    ASSERT_NOT_NULL(target);
    ASSERT_EQ(source_owner_id, (uint32_t)get_uint_field(source, "owner_behavior_id"));
    ASSERT_EQ(target_owner_id, (uint32_t)get_uint_field(target, "owner_behavior_id"));
    ASSERT_EQ(activation_delay,
              (int32_t)yyjson_get_int(yyjson_obj_get(edge, "activation_delay")));

    yyjson_doc_free(doc);
}

static void assert_script_graph_link_missing(const char *path,
                                             uint32_t root_behavior_id,
                                             uint32_t link_id)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *edges = NULL;

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             root_behavior_id, path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    edges = get_array_field(data, "control_edges");
    ASSERT_NOT_NULL(edges);
    ASSERT_NULL(find_control_edge_by_link_id(edges, link_id));

    yyjson_doc_free(doc);
}

static int build_repo_fixture_path(const char *relative_path,
                                   char *buffer,
                                   size_t buffer_size)
{
    const char *source_path = __FILE__;
    const char *tests_dir = NULL;

    if (!relative_path || !buffer || buffer_size == 0u) {
        return 0;
    }

    tests_dir = strstr(source_path, "tests");
    if (!tests_dir) {
        return 0;
    }

    snprintf(buffer, buffer_size, "%.*s%s",
             (int)(tests_dir - source_path), source_path, relative_path);
    return 1;
}

static char *read_text_file(const char *path)
{
    FILE *fp = NULL;
    long size = 0;
    char *buffer = NULL;

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (size > 0) {
        size_t read_size = fread(buffer, 1u, (size_t)size, fp);
        if (read_size != (size_t)size) {
            free(buffer);
            fclose(fp);
            return NULL;
        }
    }
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static int load_rewrite_manifest(rewrite_manifest_t *manifest)
{
    char path[1024];
    char *json = NULL;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *replace_bb = NULL;
    yyjson_val *fold = NULL;
    yyjson_val *node_ids = NULL;
    size_t idx = 0;
    size_t max = 0;
    yyjson_val *item = NULL;

    if (!manifest) {
        return 0;
    }
    memset(manifest, 0, sizeof(*manifest));

    if (!build_repo_fixture_path("tests/fixtures/ballance_rewrite_manifest.json",
                                 path, sizeof(path))) {
        return 0;
    }
    json = read_text_file(path);
    if (!json) {
        return 0;
    }

    doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        free(json);
        return 0;
    }
    root = yyjson_doc_get_root(doc);
    if (!root ||
        !yyjson_is_uint(yyjson_obj_get(root, "root_behavior_id"))) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->root_behavior_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(root, "root_behavior_id"));

    replace_bb = get_object_field(root, "replace_bb");
    if (!replace_bb) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->replace_parent_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(replace_bb, "parent_id"));
    manifest->replace_node_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(replace_bb, "node_id"));
    snprintf(manifest->replace_guid, sizeof(manifest->replace_guid), "%s",
             get_string_field(replace_bb, "guid"));
    snprintf(manifest->replace_name, sizeof(manifest->replace_name), "%s",
             get_string_field(replace_bb, "name"));

    fold = get_object_field(root, "fold");
    if (!fold) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    manifest->fold_parent_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(fold, "parent_id"));
    manifest->fold_anchor_id =
        (uint32_t)yyjson_get_uint(yyjson_obj_get(fold, "anchor_id"));

    node_ids = get_array_field(fold, "node_ids");
    if (!node_ids) {
        yyjson_doc_free(doc);
        free(json);
        return 0;
    }
    yyjson_arr_foreach(node_ids, idx, max, item) {
        if (manifest->fold_node_count >=
                sizeof(manifest->fold_node_ids) /
                    sizeof(manifest->fold_node_ids[0]) ||
            !yyjson_is_uint(item)) {
            yyjson_doc_free(doc);
            free(json);
            return 0;
        }
        manifest->fold_node_ids[manifest->fold_node_count++] =
            (uint32_t)yyjson_get_uint(item);
    }

    yyjson_doc_free(doc);
    free(json);
    return 1;
}

static char *load_report_contract(void)
{
    char path[1024];

    if (!build_repo_fixture_path("tests/fixtures/script_edit_reports.md",
                                 path, sizeof(path))) {
        return NULL;
    }
    return read_text_file(path);
}

static bool create_interface_sub_fixture(const char *input_path,
                                         const char *output_path,
                                         uint32_t behavior_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_behavior_t *subs = NULL;
    nmo_save_options_t save_opts;
    nmo_arena_t *arena = NULL;
    bool ok = false;
    remove(output_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }

    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    repo = nmo_session_get_repository(session);
    obj = repo ? nmo_object_repository_find_by_id(repo, NMO_SCRIPT_INTERFACE_TARGET_ID) : NULL;
    state = obj ? (nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
    if (!state || !state->interface_data || state->interface_data->sub_count == 0u) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
        arena,
        (state->interface_data->sub_count + 1u) * sizeof(*subs),
        alignof(nmo_interface_behavior_t));
    if (!subs) {
        goto cleanup;
    }

    memcpy(subs, state->interface_data->subs,
           state->interface_data->sub_count * sizeof(*subs));
    subs[state->interface_data->sub_count] = state->interface_data->subs[0];
    subs[state->interface_data->sub_count].behavior_id = behavior_id;
    state->interface_data->subs = subs;
    state->interface_data->sub_count += 1u;

    save_opts = nmo_save_options_default();
    ok = nmo_save_file(session, output_path, &save_opts) == NMO_OK;

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok && file_exists(output_path);
}

static bool create_interface_link_fixture(const char *input_path,
                                          const char *output_path,
                                          uint32_t owner_behavior_id,
                                          uint32_t link_id,
                                          uint32_t from_id,
                                          uint32_t to_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_body_t *body = NULL;
    nmo_interface_link_t *links = NULL;
    nmo_save_options_t save_opts;
    nmo_arena_t *arena = NULL;
    bool ok = false;
    remove(output_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }

    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    repo = nmo_session_get_repository(session);
    obj = repo ? nmo_object_repository_find_by_id(repo, owner_behavior_id) : NULL;
    state = obj ? (nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
    if (!state || !state->interface_data) {
        goto cleanup;
    }

    body = &state->interface_data->script.body;
    if (state->interface_data->script.behavior_id != owner_behavior_id) {
        body = NULL;
        for (size_t i = 0; i < state->interface_data->sub_count; ++i) {
            if (state->interface_data->subs[i].behavior_id == owner_behavior_id) {
                body = &state->interface_data->subs[i].body;
                break;
            }
        }
    }
    if (!body || body->link_count == 0u) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    links = (nmo_interface_link_t *)nmo_arena_alloc(
        arena,
        (body->link_count + 1u) * sizeof(*links),
        alignof(nmo_interface_link_t));
    if (!links) {
        goto cleanup;
    }

    memcpy(links, body->links, body->link_count * sizeof(*links));
    links[body->link_count] = body->links[0];
    links[body->link_count].link_id = link_id;
    links[body->link_count].start.id = from_id;
    links[body->link_count].end.id = to_id;
    body->links = links;
    body->link_count += 1u;

    save_opts = nmo_save_options_default();
    ok = nmo_save_file(session, output_path, &save_opts) == NMO_OK;

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok && file_exists(output_path);
}

static bool create_interface_operation_fixture(const char *input_path,
                                               const char *output_path,
                                               uint32_t owner_behavior_id,
                                               uint32_t op_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_body_t *body = NULL;
    nmo_interface_operation_t *ops = NULL;
    nmo_save_options_t save_opts;
    nmo_arena_t *arena = NULL;
    bool ok = false;
    remove(output_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }

    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    repo = nmo_session_get_repository(session);
    obj = repo ? nmo_object_repository_find_by_id(repo, owner_behavior_id) : NULL;
    state = obj ? (nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
    if (!state || !state->interface_data) {
        goto cleanup;
    }

    body = &state->interface_data->script.body;
    if (state->interface_data->script.behavior_id != owner_behavior_id) {
        body = NULL;
        for (size_t i = 0; i < state->interface_data->sub_count; ++i) {
            if (state->interface_data->subs[i].behavior_id == owner_behavior_id) {
                body = &state->interface_data->subs[i].body;
                break;
            }
        }
    }
    if (!body) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    ops = (nmo_interface_operation_t *)nmo_arena_alloc(
        arena,
        (body->operation_count + 1u) * sizeof(*ops),
        alignof(nmo_interface_operation_t));
    if (!ops) {
        goto cleanup;
    }

    if (body->operation_count > 0u) {
        memcpy(ops, body->operations, body->operation_count * sizeof(*ops));
        ops[body->operation_count] = body->operations[0];
    } else {
        memset(ops, 0, sizeof(*ops));
        ops[0].h_pos = 0.0f;
        ops[0].v_pos = 0.0f;
    }
    ops[body->operation_count].id = op_id;
    body->operations = ops;
    body->operation_count += 1u;

    save_opts = nmo_save_options_default();
    ok = nmo_save_file(session, output_path, &save_opts) == NMO_OK;

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok && file_exists(output_path);
}

static bool create_interface_shared_param_fixture(const char *input_path,
                                                  const char *output_path,
                                                  uint32_t owner_behavior_id,
                                                  uint32_t param_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_body_t *body = NULL;
    nmo_interface_param_t *params = NULL;
    nmo_save_options_t save_opts;
    nmo_arena_t *arena = NULL;
    bool ok = false;
    remove(output_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }

    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    repo = nmo_session_get_repository(session);
    obj = repo ? nmo_object_repository_find_by_id(repo, owner_behavior_id) : NULL;
    state = obj ? (nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
    if (!state || !state->interface_data) {
        goto cleanup;
    }

    body = &state->interface_data->script.body;
    if (state->interface_data->script.behavior_id != owner_behavior_id) {
        body = NULL;
        for (size_t i = 0; i < state->interface_data->sub_count; ++i) {
            if (state->interface_data->subs[i].behavior_id == owner_behavior_id) {
                body = &state->interface_data->subs[i].body;
                break;
            }
        }
    }
    if (!body || !body->has_params) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    params = (nmo_interface_param_t *)nmo_arena_alloc(
        arena,
        (body->params.shared_count + 1u) * sizeof(*params),
        alignof(nmo_interface_param_t));
    if (!params) {
        goto cleanup;
    }

    if (body->params.shared_count > 0u) {
        memcpy(params, body->params.shared,
               body->params.shared_count * sizeof(*params));
        params[body->params.shared_count] = body->params.shared[0];
    } else {
        memset(params, 0, sizeof(*params));
        params[0].h_pos = 0;
        params[0].v_pos = 0;
        params[0].style = 0;
    }
    params[body->params.shared_count].source_id = param_id;
    body->params.shared = params;
    body->params.shared_count += 1u;

    save_opts = nmo_save_options_default();
    ok = nmo_save_file(session, output_path, &save_opts) == NMO_OK;

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok && file_exists(output_path);
}

static bool create_interface_graph_io_fixture(const char *input_path,
                                              const char *output_path,
                                              uint32_t owner_behavior_id,
                                              int32_t input_index)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_behavior_t *subs = NULL;
    nmo_interface_behavior_t *target_sub = NULL;
    const nmo_interface_behavior_t *template_sub = NULL;
    int32_t *inputs = NULL;
    nmo_save_options_t save_opts;
    nmo_arena_t *arena = NULL;
    bool ok = false;
    remove(output_path);

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    if (!ctx) {
        return false;
    }

    session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    if (nmo_session_load_file(session, input_path, NULL, NULL) != NMO_OK ||
        nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        goto cleanup;
    }

    repo = nmo_session_get_repository(session);
    obj = repo ? nmo_object_repository_find_by_id(repo, NMO_SCRIPT_INTERFACE_TARGET_ID) : NULL;
    state = obj ? (nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
    if (!state || !state->interface_data) {
        goto cleanup;
    }

    for (size_t i = 0; i < state->interface_data->sub_count; ++i) {
        if (state->interface_data->subs[i].body.has_graph_io &&
            state->interface_data->subs[i].body.graph_io) {
            template_sub = &state->interface_data->subs[i];
            break;
        }
    }
    if (!template_sub) {
        goto cleanup;
    }

    arena = nmo_object_get_storage_arena(obj);
    if (!arena) {
        goto cleanup;
    }

    subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
        arena,
        (state->interface_data->sub_count + 1u) * sizeof(*subs),
        alignof(nmo_interface_behavior_t));
    if (!subs) {
        goto cleanup;
    }
    memcpy(subs, state->interface_data->subs,
           state->interface_data->sub_count * sizeof(*subs));
    subs[state->interface_data->sub_count] = *template_sub;
    subs[state->interface_data->sub_count].behavior_id = owner_behavior_id;
    state->interface_data->subs = subs;
    state->interface_data->sub_count += 1u;
    target_sub = &state->interface_data->subs[state->interface_data->sub_count - 1u];

    inputs = (int32_t *)nmo_arena_alloc(
        arena,
        (target_sub->body.graph_io->outward_input_count + 1u) * sizeof(*inputs),
        alignof(int32_t));
    if (!inputs) {
        goto cleanup;
    }

    if (target_sub->body.graph_io->outward_input_count > 0u) {
        memcpy(inputs, target_sub->body.graph_io->outward_inputs,
               target_sub->body.graph_io->outward_input_count * sizeof(*inputs));
    }
    inputs[target_sub->body.graph_io->outward_input_count] = input_index;
    target_sub->body.graph_io->outward_inputs = inputs;
    target_sub->body.graph_io->outward_input_count += 1u;

    save_opts = nmo_save_options_default();
    ok = nmo_save_file(session, output_path, &save_opts) == NMO_OK;

cleanup:
    if (session) {
        nmo_session_destroy(session);
    }
    if (ctx) {
        nmo_context_release(ctx);
    }
    return ok && file_exists(output_path);
}

TEST(cli, script_edit_fixture_manifest_contains_locked_ballance_ids)
{
    static const uint32_t expected_fold_nodes[] = {
        4166u, 4140u, 4147u, 4157u, 4165u,
        4153u, 4151u, 4155u, 4143u, 4145u
    };
    rewrite_manifest_t manifest;

    ASSERT_TRUE(load_rewrite_manifest(&manifest));

    ASSERT_EQ(237u, manifest.root_behavior_id);
    ASSERT_EQ(237u, manifest.replace_parent_id);
    ASSERT_EQ(363u, manifest.replace_node_id);
    ASSERT_STR_EQ("{42414C02-10000002}", manifest.replace_guid);
    ASSERT_STR_EQ("Ballance Load NMO Range", manifest.replace_name);
    ASSERT_EQ(4692u, manifest.fold_parent_id);
    ASSERT_EQ(4166u, manifest.fold_anchor_id);
    ASSERT_EQ(sizeof(expected_fold_nodes) / sizeof(expected_fold_nodes[0]),
              manifest.fold_node_count);
    ASSERT_ARRAY_EQ(manifest.fold_node_ids, expected_fold_nodes,
                    manifest.fold_node_count);
}

TEST(cli, script_edit_report_contract_is_checked_in)
{
    char *markdown = load_report_contract();

    ASSERT_NOT_NULL(markdown);
    ASSERT_STR_CONTAINS(markdown, "## `nmo script graph ... -f json`");
    ASSERT_STR_CONTAINS(markdown, "## `nmo script boundary ... -f json`");
    ASSERT_STR_CONTAINS(markdown, "## `nmo script fold ... --dry-run -f json`");
    ASSERT_STR_CONTAINS(markdown, "## `nmo script run ... --dry-run -f json`");
    ASSERT_STR_CONTAINS(markdown, "## CLI To Lua Option Mapping");
    ASSERT_STR_CONTAINS(markdown, "`--parent` -> `{ parent = <id> }`");
    ASSERT_STR_CONTAINS(markdown, "`--node` -> `{ node = <id> }`");
    ASSERT_STR_CONTAINS(markdown, "`--bb-guid` -> `{ guid = \"<guid>\" }`");
    ASSERT_STR_CONTAINS(markdown, "`--name` -> `{ name = \"<name>\" }`");
    ASSERT_STR_CONTAINS(markdown, "`--interface preserve|canonicalize|remove` -> `{ interface = \"...\" }`");
    free(markdown);
}

TEST(cli, script_graph_json_smoke)
{
    rewrite_manifest_t manifest;
    char args[1024];

    ASSERT_TRUE(load_rewrite_manifest(&manifest));

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             manifest.root_behavior_id,
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
    ASSERT_EQ(manifest.root_behavior_id,
              (uint32_t)yyjson_get_uint(yyjson_obj_get(data,
                                                       "root_behavior_id")));
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

TEST(cli, script_node_and_io_crud_roundtrip)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *changed_objects = NULL;
    yyjson_val *deleted_objects = NULL;
    yyjson_val *validation = NULL;
    uint32_t node_id = 0;
    uint32_t io_id = 0;
    char args[1024];
    const char *node_add = "test_script_edit_tmp/node_add.cmo";
    const char *node_remove = "test_script_edit_tmp/node_remove.cmo";
    const char *io_add = "test_script_edit_tmp/io_add.cmo";
    const char *io_rename = "test_script_edit_tmp/io_rename.cmo";
    const char *io_remove = "test_script_edit_tmp/io_remove.cmo";

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    make_dir("test_script_edit_tmp");
    remove(node_add);
    remove(node_remove);
    remove(io_add);
    remove(io_rename);
    remove(io_remove);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Test BB\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             node_add);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.node.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    node_id = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_id != 0u);
    ASSERT_TRUE(file_exists(node_add));
    yyjson_doc_free(doc);
    assert_validate_ok(node_add);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name In "
             "\"%s\" -o \"%s\"",
             node_id,
             node_add,
             io_add);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.io.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    io_id = (uint32_t)get_uint_field(data, "io_id");
    ASSERT_TRUE(io_id != 0u);
    ASSERT_TRUE(file_exists(io_add));
    yyjson_doc_free(doc);
    assert_validate_ok(io_add);

    snprintf(args, sizeof(args),
             "-f json script io rename --io %u --name Start "
             "\"%s\" -o \"%s\"",
             io_id,
             io_add,
             io_rename);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.io.rename", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    ASSERT_STR_EQ("rename_io",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, io_id));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    yyjson_doc_free(doc);
    assert_validate_ok(io_rename);

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             node_id,
             io_rename);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    ASSERT_NOT_NULL(get_array_field(data, "inputs"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script io remove --io %u \"%s\" -o \"%s\"",
             io_id,
             io_rename,
             io_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.io.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    ASSERT_STR_EQ("remove_io",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(deleted_objects, io_id));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    yyjson_doc_free(doc);
    assert_validate_ok(io_remove);

    snprintf(args, sizeof(args),
             "-f json script node remove --parent %u --node %u "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             node_id,
             io_remove,
             node_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.node.remove", get_string_field(root, "command"));
    yyjson_doc_free(doc);
    assert_validate_ok(node_remove);
}

TEST(cli, script_node_add_dry_run_reports_schema_v2)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *created_objects = NULL;
    yyjson_val *semantic_risks = NULL;
    yyjson_val *validation = NULL;
    yyjson_val *diff = NULL;
    yyjson_val *op = NULL;
    char args[1024];

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid 055B29FE-662D5CA0 --name \"Dry Node\" "
             "--dry-run \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.node.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(get_uint_field(data, "node_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("add_node", get_string_field(op, "op"));
    ASSERT_EQ(manifest.root_behavior_id, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(op, "result_handles") == NULL);
    ASSERT_NOT_NULL(get_array_field(op, "handles"));
    created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_TRUE(yyjson_arr_size(created_objects) > 1u);
    semantic_risks = get_array_field(data, "semantic_risks");
    ASSERT_NOT_NULL(semantic_risks);
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(NMO_OK, (nmo_status_t)get_uint_field(validation, "final_status"));
    diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_TRUE(get_uint_field(diff, "created_object_count") > 1u);

    yyjson_doc_free(doc);
}

TEST(cli, script_io_add_dry_run_exposes_executor_validation_parity)
{
    char args[1024];
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *validation = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *created_objects = NULL;
    yyjson_val *semantic_risks = NULL;
    yyjson_val *diff = NULL;
    yyjson_val *op = NULL;

    snprintf(args, sizeof(args),
             "-f json script io add --behavior 6 --kind input --name DryParity "
             "--dry-run \"%s\"",
             NMO_TEST_DATA_FILE("Nop.cmo"));
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    ASSERT_STR_EQ("script.io.add",
                  get_string_field(yyjson_doc_get_root(doc), "command"));
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(get_uint_field(data, "io_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("add_io", get_string_field(op, "op"));
    ASSERT_EQ(6u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(op, "result_handles") == NULL);
    ASSERT_NOT_NULL(get_array_field(op, "handles"));
    created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_EQ(1u, yyjson_arr_size(created_objects));
    semantic_risks = get_array_field(data, "semantic_risks");
    ASSERT_NOT_NULL(semantic_risks);

    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    ASSERT_TRUE(yyjson_obj_get(validation, "behavior_index") == NULL);
    ASSERT_TRUE(yyjson_obj_get(validation, "interface") == NULL);
    diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_EQ(1u, get_uint_field(diff, "created_object_count"));

    yyjson_doc_free(doc);
}

TEST(cli, script_node_remove_canonicalizes_interface_refs)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *subs = NULL;
    uint32_t node_id = 0;
    char args[1024];
    const char *node_add = "test_script_edit_tmp/interface_stale_add.cmo";
    const char *iface_node = "test_script_edit_tmp/interface_node_present.cmo";
    const char *node_remove = "test_script_edit_tmp/interface_node_remove.cmo";

    make_dir("test_script_edit_tmp");
    remove(node_add);
    remove(iface_node);
    remove(node_remove);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Iface Canon\" "
             "\"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             node_add);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_id = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_id != 0u);
    yyjson_doc_free(doc);

    ASSERT_TRUE(create_interface_sub_fixture(node_add, iface_node, node_id));

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             iface_node);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    subs = get_array_field(data, "subs");
    ASSERT_NOT_NULL(subs);
    ASSERT_NOT_NULL(find_array_object_by_behavior_id(subs, node_id));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script node remove --parent %u --node %u "
             "--interface canonicalize \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             node_id,
             iface_node,
             node_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.node.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("canonicalize", get_string_field(data, "interface_mode"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, yyjson_arr_size(operations));
    ASSERT_STR_EQ("remove_node",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    ASSERT_STR_EQ("interface_policy",
                  get_string_field(yyjson_arr_get(operations, 1), "op"));
    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(deleted_objects, node_id));
    yyjson_val *validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    yyjson_doc_free(doc);
    assert_validate_ok(node_remove);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             node_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    subs = get_array_field(data, "subs");
    ASSERT_NOT_NULL(subs);
    ASSERT_NULL(find_array_object_by_behavior_id(subs, node_id));
    {
        yyjson_val *diag = get_object_field(data, "interface_parse");
        yyjson_val *available = diag ? yyjson_obj_get(diag, "available") : NULL;
        ASSERT_TRUE(available && yyjson_is_bool(available));
        ASSERT_TRUE(yyjson_get_bool(available));
    }
    yyjson_doc_free(doc);
}

TEST(cli, script_node_remove_preserve_rejects_stale_interface_refs)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    uint32_t node_id = 0;
    char args[1024];
    const char *node_add = "test_script_edit_tmp/interface_preserve_add.cmo";
    const char *iface_node = "test_script_edit_tmp/interface_preserve_present.cmo";
    const char *node_remove = "test_script_edit_tmp/interface_preserve_remove.cmo";

    make_dir("test_script_edit_tmp");
    remove(node_add);
    remove(iface_node);
    remove(node_remove);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Iface Preserve\" "
             "\"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             node_add);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_id = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_id != 0u);
    yyjson_doc_free(doc);

    ASSERT_TRUE(create_interface_sub_fixture(node_add, iface_node, node_id));

    snprintf(args, sizeof(args),
             "-f json script node remove --parent %u --node %u "
             "--interface preserve \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             node_id,
             iface_node,
             node_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "interface");
    free(result.output);
    ASSERT_FALSE(file_exists(node_remove));
}

TEST(cli, script_node_remove_remove_strips_interface_data)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    uint32_t node_id = 0;
    char args[1024];
    const char *node_add = "test_script_edit_tmp/interface_remove_add.cmo";
    const char *iface_node = "test_script_edit_tmp/interface_remove_present.cmo";
    const char *node_remove = "test_script_edit_tmp/interface_remove_output.cmo";

    make_dir("test_script_edit_tmp");
    remove(node_add);
    remove(iface_node);
    remove(node_remove);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Iface Remove\" "
             "\"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             node_add);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_id = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_id != 0u);
    yyjson_doc_free(doc);

    ASSERT_TRUE(create_interface_sub_fixture(node_add, iface_node, node_id));

    snprintf(args, sizeof(args),
             "-f json script node remove --parent %u --node %u "
             "--interface remove \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             node_id,
             iface_node,
             node_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.node.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("remove", get_string_field(data, "interface_mode"));
    yyjson_doc_free(doc);
    assert_validate_ok(node_remove);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             node_remove);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "has no interface data");
    free(result.output);
}

TEST(cli, script_control_flow_crud_roundtrip)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *op_item = NULL;
    yyjson_val *changed_objects = NULL;
    uint32_t node_a = 0;
    uint32_t node_b = 0;
    uint32_t node_c = 0;
    uint32_t node_d = 0;
    uint32_t a_in = 0;
    uint32_t a_out = 0;
    uint32_t b_in = 0;
    uint32_t b_out = 0;
    uint32_t c_in = 0;
    uint32_t c_out = 0;
    uint32_t d_in = 0;
    uint32_t d_out = 0;
    uint32_t link_id = 0;
    char args[1024];
    const char *node_a_path = "test_script_edit_tmp/link_node_a.cmo";
    const char *node_b_path = "test_script_edit_tmp/link_node_b.cmo";
    const char *node_c_path = "test_script_edit_tmp/link_node_c.cmo";
    const char *node_d_path = "test_script_edit_tmp/link_node_d.cmo";
    const char *io_a_in_path = "test_script_edit_tmp/link_io_a_in.cmo";
    const char *io_a_out_path = "test_script_edit_tmp/link_io_a_out.cmo";
    const char *io_b_in_path = "test_script_edit_tmp/link_io_b_in.cmo";
    const char *io_b_out_path = "test_script_edit_tmp/link_io_b_out.cmo";
    const char *io_c_in_path = "test_script_edit_tmp/link_io_c_in.cmo";
    const char *io_c_out_path = "test_script_edit_tmp/link_io_c_out.cmo";
    const char *io_d_in_path = "test_script_edit_tmp/link_io_d_in.cmo";
    const char *io_d_out_path = "test_script_edit_tmp/link_io_d_out.cmo";
    const char *link_add_path = "test_script_edit_tmp/link_add.cmo";
    const char *link_rewire_source_path = "test_script_edit_tmp/link_rewire_source.cmo";
    const char *link_rewire_target_path = "test_script_edit_tmp/link_rewire_target.cmo";
    const char *link_delay_path = "test_script_edit_tmp/link_delay.cmo";
    const char *link_remove_path = "test_script_edit_tmp/link_remove.cmo";

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    make_dir("test_script_edit_tmp");
    remove(node_a_path);
    remove(node_b_path);
    remove(node_c_path);
    remove(node_d_path);
    remove(io_a_in_path);
    remove(io_a_out_path);
    remove(io_b_in_path);
    remove(io_b_out_path);
    remove(io_c_in_path);
    remove(io_c_out_path);
    remove(io_d_in_path);
    remove(io_d_out_path);
    remove(link_add_path);
    remove(link_rewire_source_path);
    remove(link_rewire_target_path);
    remove(link_delay_path);
    remove(link_remove_path);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Link A\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             node_a_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_a = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_a != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Link B\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             node_a_path,
             node_b_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_b = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_b != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Link C\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             node_b_path,
             node_c_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_c = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_c != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Link D\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             node_c_path,
             node_d_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_d = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_d != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name AIn "
             "\"%s\" -o \"%s\"",
             node_a,
             node_d_path,
             io_a_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind output --name AOut "
             "\"%s\" -o \"%s\"",
             node_a,
             io_a_in_path,
             io_a_out_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name BIn "
             "\"%s\" -o \"%s\"",
             node_b,
             io_a_out_path,
             io_b_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind output --name BOut "
             "\"%s\" -o \"%s\"",
             node_b,
             io_b_in_path,
             io_b_out_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name CIn "
             "\"%s\" -o \"%s\"",
             node_c,
             io_b_out_path,
             io_c_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind output --name COut "
             "\"%s\" -o \"%s\"",
             node_c,
             io_c_in_path,
             io_c_out_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name DIn "
             "\"%s\" -o \"%s\"",
             node_d,
             io_c_out_path,
             io_d_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind output --name DOut "
             "\"%s\" -o \"%s\"",
             node_d,
             io_d_in_path,
             io_d_out_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    load_behavior_first_ios(io_d_out_path, node_a, &a_in, &a_out);
    load_behavior_first_ios(io_d_out_path, node_b, &b_in, &b_out);
    load_behavior_first_ios(io_d_out_path, node_c, &c_in, &c_out);
    load_behavior_first_ios(io_d_out_path, node_d, &d_in, &d_out);
    (void)a_in;
    (void)b_out;
    (void)c_in;
    (void)d_out;

    snprintf(args, sizeof(args),
             "-f json script link add --parent %u --from %u --to %u --delay 0 "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             a_out,
             b_in,
             io_d_out_path,
             link_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    link_id = (uint32_t)get_uint_field(data, "link_id");
    ASSERT_TRUE(link_id != 0u);
    yyjson_doc_free(doc);
    assert_validate_ok(link_add_path);
    assert_script_graph_link(link_add_path, manifest.root_behavior_id,
                             link_id, node_a, node_b, 0);

    snprintf(args, sizeof(args),
             "-f json script link rewire --link %u --from %u "
             "\"%s\" -o \"%s\"",
             link_id,
             c_out,
             link_add_path,
             link_rewire_source_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.rewire", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("rewire_behavior_link", get_string_field(op_item, "op"));
    ASSERT_EQ(link_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, link_id));
    yyjson_doc_free(doc);
    assert_validate_ok(link_rewire_source_path);
    assert_script_graph_link(link_rewire_source_path, manifest.root_behavior_id,
                             link_id, node_c, node_b, 0);

    snprintf(args, sizeof(args),
             "-f json script link rewire --link %u --to %u "
             "\"%s\" -o \"%s\"",
             link_id,
             d_in,
             link_rewire_source_path,
             link_rewire_target_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.rewire", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("rewire_behavior_link", get_string_field(op_item, "op"));
    ASSERT_EQ(link_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, link_id));
    yyjson_doc_free(doc);
    assert_validate_ok(link_rewire_target_path);
    assert_script_graph_link(link_rewire_target_path, manifest.root_behavior_id,
                             link_id, node_c, node_d, 0);

    snprintf(args, sizeof(args),
             "-f json script link set-delay --link %u --delay 2 "
             "\"%s\" -o \"%s\"",
             link_id,
             link_rewire_target_path,
             link_delay_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.set-delay", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("set_behavior_link_delay", get_string_field(op_item, "op"));
    ASSERT_EQ(link_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, link_id));
    yyjson_doc_free(doc);
    assert_validate_ok(link_delay_path);
    assert_script_graph_link(link_delay_path, manifest.root_behavior_id,
                             link_id, node_c, node_d, 2);

    snprintf(args, sizeof(args),
             "-f json script link remove --parent %u --link %u "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             link_id,
             link_delay_path,
             link_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.remove", get_string_field(root, "command"));
    yyjson_doc_free(doc);
    assert_validate_ok(link_remove_path);
    assert_script_graph_link_missing(link_remove_path,
                                     manifest.root_behavior_id,
                                     link_id);
}

TEST(cli, script_link_add_dry_run_reports_executor_validation)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *validation = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *created_objects = NULL;
    yyjson_val *diff = NULL;
    yyjson_val *op = NULL;
    uint32_t node_a = 0;
    uint32_t node_b = 0;
    uint32_t a_out = 0;
    uint32_t b_in = 0;
    char args[1024];
    const char *node_a_path = "test_script_edit_tmp/link_dry_run_node_a.cmo";
    const char *node_b_path = "test_script_edit_tmp/link_dry_run_node_b.cmo";
    const char *io_a_out_path = "test_script_edit_tmp/link_dry_run_io_a_out.cmo";
    const char *io_b_in_path = "test_script_edit_tmp/link_dry_run_io_b_in.cmo";

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    make_dir("test_script_edit_tmp");
    remove(node_a_path);
    remove(node_b_path);
    remove(io_a_out_path);
    remove(io_b_in_path);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Dry Link A\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             node_a_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_a = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_a != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Dry Link B\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             node_a_path,
             node_b_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_b = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_b != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind output --name DryAOut "
             "\"%s\" -o \"%s\"",
             node_a,
             node_b_path,
             io_a_out_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name DryBIn "
             "\"%s\" -o \"%s\"",
             node_b,
             io_a_out_path,
             io_b_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    load_behavior_io_by_name(io_b_in_path, node_a, "outputs", "DryAOut", &a_out);
    load_behavior_io_by_name(io_b_in_path, node_b, "inputs", "DryBIn", &b_in);

    snprintf(args, sizeof(args),
             "-f json script link add --parent %u --from %u --to %u --delay 0 "
             "--dry-run \"%s\"",
             manifest.root_behavior_id,
             a_out,
             b_in,
             io_b_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("add_behavior_link", get_string_field(op, "op"));
    ASSERT_EQ(manifest.root_behavior_id, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(op, "result_handles") == NULL);
    ASSERT_NOT_NULL(get_array_field(op, "handles"));
    created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_EQ(1u, yyjson_arr_size(created_objects));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_EQ(1u, get_uint_field(diff, "created_object_count"));
    ASSERT_NULL(yyjson_obj_get(data, "output"));
    yyjson_doc_free(doc);
}

TEST(cli, script_link_remove_canonicalizes_interface_refs)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *script = NULL;
    yyjson_val *body = NULL;
    yyjson_val *links = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *deleted_objects = NULL;
    yyjson_val *validation = NULL;
    uint32_t node_a = 0;
    uint32_t node_b = 0;
    uint32_t a_out = 0;
    uint32_t b_in = 0;
    uint32_t link_id = 0;
    char args[1024];
    const char *node_a_path = "test_script_edit_tmp/interface_link_node_a.cmo";
    const char *node_b_path = "test_script_edit_tmp/interface_link_node_b.cmo";
    const char *io_a_out_path = "test_script_edit_tmp/interface_link_io_a_out.cmo";
    const char *io_b_in_path = "test_script_edit_tmp/interface_link_io_b_in.cmo";
    const char *link_add_path = "test_script_edit_tmp/interface_link_add.cmo";
    const char *iface_link_path = "test_script_edit_tmp/interface_link_present.cmo";
    const char *link_remove_path = "test_script_edit_tmp/interface_link_remove.cmo";

    make_dir("test_script_edit_tmp");
    remove(node_a_path);
    remove(node_b_path);
    remove(io_a_out_path);
    remove(io_b_in_path);
    remove(link_add_path);
    remove(iface_link_path);
    remove(link_remove_path);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Iface Link A\" "
             "\"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             node_a_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_a = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_a != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Iface Link B\" "
             "\"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             node_a_path,
             node_b_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    node_b = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(node_b != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind output --name AOut "
             "\"%s\" -o \"%s\"",
             node_a,
             node_b_path,
             io_a_out_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name BIn "
             "\"%s\" -o \"%s\"",
             node_b,
             io_a_out_path,
             io_b_in_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    load_behavior_io_by_name(io_b_in_path, node_a, "outputs", "AOut", &a_out);
    load_behavior_io_by_name(io_b_in_path, node_b, "inputs", "BIn", &b_in);

    snprintf(args, sizeof(args),
             "-f json script link add --parent %u --from %u --to %u "
             "\"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             a_out,
             b_in,
             io_b_in_path,
             link_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    link_id = (uint32_t)get_uint_field(data, "link_id");
    ASSERT_TRUE(link_id != 0u);
    yyjson_doc_free(doc);

    ASSERT_TRUE(create_interface_link_fixture(link_add_path, iface_link_path,
                                              NMO_SCRIPT_INTERFACE_TARGET_ID, link_id,
                                              a_out, b_in));

    snprintf(args, sizeof(args),
             "-f json script link remove --parent %u --link %u "
             "--interface canonicalize \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             link_id,
             iface_link_path,
             link_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.link.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("canonicalize", get_string_field(data, "interface_mode"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, yyjson_arr_size(operations));
    ASSERT_STR_EQ("remove_behavior_link",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    ASSERT_STR_EQ("interface_policy",
                  get_string_field(yyjson_arr_get(operations, 1), "op"));
    deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(deleted_objects, link_id));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    yyjson_doc_free(doc);

    assert_validate_ok(link_remove_path);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             link_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    script = get_object_field(data, "script");
    ASSERT_NOT_NULL(script);
    body = get_object_field(script, "body");
    ASSERT_NOT_NULL(body);
    links = get_array_field(body, "links");
    ASSERT_NOT_NULL(links);
    ASSERT_NULL(find_control_edge_by_link_id(links, link_id));
    yyjson_doc_free(doc);
}

TEST(cli, script_io_remove_canonicalizes_interface_refs)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *subs = NULL;
    yyjson_val *sub = NULL;
    yyjson_val *body = NULL;
    yyjson_val *graph_io = NULL;
    yyjson_val *outward_inputs = NULL;
    uint32_t io_id = 0;
    uint32_t io_index = 0;
    char args[1024];
    const uint32_t owner_behavior_id = 229u;
    const char *io_add_path = "test_script_edit_tmp/interface_io_add.cmo";
    const char *iface_io_path = "test_script_edit_tmp/interface_io_present.cmo";
    const char *io_remove_path = "test_script_edit_tmp/interface_io_remove.cmo";

    make_dir("test_script_edit_tmp");
    remove(io_add_path);
    remove(iface_io_path);
    remove(io_remove_path);

    snprintf(args, sizeof(args),
             "-f json script io add --behavior %u --kind input --name IfaceIo "
             "\"%s\" -o \"%s\"",
             owner_behavior_id,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             io_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    load_behavior_io_entry_by_name(io_add_path, owner_behavior_id,
                                   "inputs", "IfaceIo",
                                   &io_id, &io_index);
    ASSERT_TRUE(io_id != 0u);

    ASSERT_TRUE(create_interface_graph_io_fixture(io_add_path, iface_io_path,
                                                  owner_behavior_id,
                                                  (int32_t)io_index));

    snprintf(args, sizeof(args),
             "-f json script io remove --io %u --interface canonicalize "
             "\"%s\" -o \"%s\"",
             io_id,
             iface_io_path,
             io_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.io.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("canonicalize", get_string_field(data, "interface_mode"));
    yyjson_doc_free(doc);

    assert_validate_ok(io_remove_path);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             io_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    subs = get_array_field(data, "subs");
    ASSERT_NOT_NULL(subs);
    sub = find_array_object_by_behavior_id(subs, owner_behavior_id);
    ASSERT_NOT_NULL(sub);
    body = get_object_field(sub, "body");
    ASSERT_NOT_NULL(body);
    graph_io = get_object_field(body, "graph_io");
    ASSERT_NOT_NULL(graph_io);
    outward_inputs = get_array_field(graph_io, "outward_inputs");
    ASSERT_NOT_NULL(outward_inputs);
    for (size_t i = 0; i < yyjson_arr_size(outward_inputs); ++i) {
        ASSERT_TRUE((uint32_t)yyjson_get_int(yyjson_arr_get(outward_inputs, i)) != io_index);
    }
    yyjson_doc_free(doc);
}

TEST(cli, script_param_remove_canonicalizes_interface_refs)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *script = NULL;
    yyjson_val *body = NULL;
    yyjson_val *params = NULL;
    yyjson_val *shared = NULL;
    uint32_t param_id = 0;
    char args[1024];
    const char *param_add_path = "test_script_edit_tmp/interface_param_add.cmo";
    const char *iface_param_path = "test_script_edit_tmp/interface_param_present.cmo";
    const char *param_remove_path = "test_script_edit_tmp/interface_param_remove.cmo";

    make_dir("test_script_edit_tmp");
    remove(param_add_path);
    remove(iface_param_path);
    remove(param_remove_path);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind shared "
             "--type int --name IfaceParam \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(param_id != 0u);
    yyjson_doc_free(doc);

    ASSERT_TRUE(create_interface_shared_param_fixture(param_add_path, iface_param_path,
                                                      NMO_SCRIPT_INTERFACE_TARGET_ID,
                                                      param_id));

    snprintf(args, sizeof(args),
             "-f json script param remove --param %u --detach "
             "--interface canonicalize \"%s\" -o \"%s\"",
             param_id,
             iface_param_path,
             param_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("canonicalize", get_string_field(data, "interface_mode"));
    yyjson_doc_free(doc);

    assert_validate_ok(param_remove_path);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             param_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    script = get_object_field(data, "script");
    ASSERT_NOT_NULL(script);
    body = get_object_field(script, "body");
    ASSERT_NOT_NULL(body);
    params = get_object_field(body, "params");
    ASSERT_NOT_NULL(params);
    shared = get_array_field(params, "shared");
    ASSERT_NOT_NULL(shared);
    ASSERT_NULL(find_array_object_by_id(shared, param_id));
    yyjson_doc_free(doc);
}

TEST(cli, script_param_add_dry_run_reports_executor_validation)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *validation = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *created_objects = NULL;
    yyjson_val *diff = NULL;
    yyjson_val *op = NULL;
    char args[1024];

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type int --name DryParam --dry-run \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("add_parameter", get_string_field(op, "op"));
    ASSERT_EQ(NMO_SCRIPT_INTERFACE_TARGET_ID,
              (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(op, "result_handles") == NULL);
    ASSERT_NOT_NULL(get_array_field(op, "handles"));
    created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_EQ(1u, yyjson_arr_size(created_objects));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_EQ(1u, get_uint_field(diff, "created_object_count"));
    ASSERT_NULL(yyjson_obj_get(data, "output"));
    yyjson_doc_free(doc);
}

TEST(cli, script_op_remove_canonicalizes_interface_refs)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *script = NULL;
    yyjson_val *body = NULL;
    yyjson_val *ops = NULL;
    uint32_t in1_param_id = 0;
    uint32_t in2_param_id = 0;
    uint32_t out_param_id = 0;
    uint32_t op_id = 0;
    char args[1024];
    const char *in_param_add_path = "test_script_edit_tmp/interface_op_in_param_add.cmo";
    const char *in2_param_add_path = "test_script_edit_tmp/interface_op_in2_param_add.cmo";
    const char *param_add_path = "test_script_edit_tmp/interface_op_param_add.cmo";
    const char *op_add_path = "test_script_edit_tmp/interface_op_add.cmo";
    const char *iface_op_path = "test_script_edit_tmp/interface_op_present.cmo";
    const char *op_remove_path = "test_script_edit_tmp/interface_op_remove.cmo";

    make_dir("test_script_edit_tmp");
    remove(in_param_add_path);
    remove(in2_param_add_path);
    remove(param_add_path);
    remove(op_add_path);
    remove(iface_op_path);
    remove(op_remove_path);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type int --name IfaceOpIn \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             in_param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    in1_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(in1_param_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type int --name IfaceOpIn2 \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             in_param_add_path,
             in2_param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    in2_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(in2_param_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind out "
             "--type int --name IfaceOpOut \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             in2_param_add_path,
             param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    out_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(out_param_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script op add --parent %u "
             "--op-guid 33CC6B49-3589282B --in1 %u --in2 %u --out %u \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             in1_param_id,
             in2_param_id,
             out_param_id,
             param_add_path,
             op_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    op_id = (uint32_t)get_uint_field(data, "op_id");
    ASSERT_TRUE(op_id != 0u);
    yyjson_doc_free(doc);

    ASSERT_TRUE(create_interface_operation_fixture(op_add_path, iface_op_path,
                                                   NMO_SCRIPT_INTERFACE_TARGET_ID,
                                                   op_id));

    snprintf(args, sizeof(args),
             "-f json script op remove --op %u "
             "--interface canonicalize \"%s\" -o \"%s\"",
             op_id,
             iface_op_path,
             op_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.op.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("canonicalize", get_string_field(data, "interface_mode"));
    yyjson_doc_free(doc);

    assert_validate_ok(op_remove_path);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             op_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    script = get_object_field(data, "script");
    ASSERT_NOT_NULL(script);
    body = get_object_field(script, "body");
    ASSERT_NOT_NULL(body);
    ops = get_array_field(body, "operations");
    ASSERT_NOT_NULL(ops);
    ASSERT_NULL(find_array_object_by_id(ops, op_id));
    yyjson_doc_free(doc);
}

TEST(cli, script_op_add_dry_run_reports_executor_validation)
{
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *validation = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *created_objects = NULL;
    yyjson_val *diff = NULL;
    yyjson_val *op = NULL;
    uint32_t in1_param_id = 0;
    uint32_t in2_param_id = 0;
    uint32_t out_param_id = 0;
    char args[1024];
    const char *in_param_add_path = "test_script_edit_tmp/op_dry_run_in_param_add.cmo";
    const char *in2_param_add_path = "test_script_edit_tmp/op_dry_run_in2_param_add.cmo";
    const char *param_add_path = "test_script_edit_tmp/op_dry_run_param_add.cmo";

    make_dir("test_script_edit_tmp");
    remove(in_param_add_path);
    remove(in2_param_add_path);
    remove(param_add_path);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type int --name DryOpIn \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             NMO_SCRIPT_INTERFACE_FIXTURE,
             in_param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    in1_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(in1_param_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type int --name DryOpIn2 \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             in_param_add_path,
             in2_param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    in2_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(in2_param_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind out "
             "--type int --name DryOpOut \"%s\" -o \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             in2_param_add_path,
             param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    out_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(out_param_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script op add --parent %u "
             "--op-guid 33CC6B49-3589282B --in1 %u --in2 %u --out %u "
             "--dry-run \"%s\"",
             NMO_SCRIPT_INTERFACE_TARGET_ID,
             in1_param_id,
             in2_param_id,
             out_param_id,
             param_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);

    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.op.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("add_operation", get_string_field(op, "op"));
    ASSERT_EQ(NMO_SCRIPT_INTERFACE_TARGET_ID,
              (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_TRUE(yyjson_obj_get(op, "result_handles") == NULL);
    ASSERT_NOT_NULL(get_array_field(op, "handles"));
    created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_EQ(1u, yyjson_arr_size(created_objects));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    ASSERT_EQ(1u, get_uint_field(diff, "created_object_count"));
    ASSERT_NULL(yyjson_obj_get(data, "output"));
    yyjson_doc_free(doc);
}

TEST(cli, script_parameter_crud_roundtrip)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *root = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *op_item = NULL;
    yyjson_val *changed_objects = NULL;
    yyjson_val *deleted_objects = NULL;
    yyjson_val *validation = NULL;
    uint32_t source_param_id = 0;
    uint32_t target_node_id = 0;
    uint32_t target_param_id = 0;
    uint32_t removed_target_param_id = 0;
    uint32_t source_param_index = 0;
    char args[1024];
    const char *source_add_path = "test_script_edit_tmp/param_source_add.cmo";
    const char *source_set_path = "test_script_edit_tmp/param_source_set.cmo";
    const char *target_node_path = "test_script_edit_tmp/param_target_node.cmo";
    const char *target_add_path = "test_script_edit_tmp/param_target_add.cmo";
    const char *connect_path = "test_script_edit_tmp/param_connect.cmo";
    const char *disconnect_path = "test_script_edit_tmp/param_disconnect.cmo";
    const char *remove_path = "test_script_edit_tmp/param_remove.cmo";

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    make_dir("test_script_edit_tmp");
    remove(source_add_path);
    remove(source_set_path);
    remove(target_node_path);
    remove(target_add_path);
    remove(connect_path);
    remove(disconnect_path);
    remove(remove_path);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind out "
             "--type CKPGUID_INT --name Level "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             source_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    source_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(source_param_id != 0u);
    yyjson_doc_free(doc);
    assert_validate_ok(source_add_path);

    snprintf(args, sizeof(args),
             "-f json script param set --param %u --value 3 "
             "\"%s\" -o \"%s\"",
             source_param_id,
             source_add_path,
             source_set_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.set", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ("3", get_string_field(data, "new_value"));
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("set_parameter_value", get_string_field(op_item, "op"));
    ASSERT_EQ(source_param_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    ASSERT_TRUE(yyjson_obj_get(op_item, "result_handles") == NULL);
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, source_param_id));
    validation = get_object_field(data, "validation");
    ASSERT_NOT_NULL(validation);
    ASSERT_EQ(0u, get_uint_field(validation, "final_status"));
    ASSERT_TRUE(yyjson_obj_get(validation, "references") == NULL);
    yyjson_doc_free(doc);
    assert_validate_ok(source_set_path);
    assert_parameter_show_value(source_set_path, source_param_id, "3", 0u);

    load_behavior_parameter_entry(source_set_path,
                                  manifest.root_behavior_id,
                                  "output_parameters",
                                  source_param_id,
                                  &source_param_index,
                                  NULL,
                                  NULL);

    snprintf(args, sizeof(args),
             "-f json script node add --parent %u "
             "--bb-guid D0B7ADF3-D3FF3CF6 --name \"Param Target\" "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             source_set_path,
             target_node_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    target_node_id = (uint32_t)get_uint_field(data, "node_id");
    ASSERT_TRUE(target_node_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind in "
             "--type CKPGUID_INT --name LevelIn "
             "\"%s\" -o \"%s\"",
             target_node_id,
             target_node_path,
             target_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    target_param_id = (uint32_t)get_uint_field(data, "param_id");
    ASSERT_TRUE(target_param_id != 0u);
    yyjson_doc_free(doc);
    assert_validate_ok(target_add_path);

    snprintf(args, sizeof(args),
             "-f json script param connect --from %u --to %u "
             "\"%s\" -o \"%s\"",
             source_param_id,
             target_param_id,
             target_add_path,
             connect_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.connect", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("connect_parameter", get_string_field(op_item, "op"));
    ASSERT_EQ(target_param_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, target_param_id));
    yyjson_doc_free(doc);
    assert_validate_ok(connect_path);
    assert_parameter_show_value(connect_path, source_param_id, "3", 1u);
    assert_parameter_show_source(connect_path, target_param_id, source_param_id);
    assert_script_graph_data_edge_present(connect_path,
                                          manifest.root_behavior_id,
                                          source_param_id,
                                          target_param_id);

    snprintf(args, sizeof(args),
             "-f json script param remove --param %u "
             "\"%s\" -o \"%s\"",
             source_param_id,
             connect_path,
             remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);
    ASSERT_FALSE(file_exists(remove_path));

    snprintf(args, sizeof(args),
             "-f json script param disconnect --to %u "
             "\"%s\" -o \"%s\"",
             target_param_id,
             connect_path,
             disconnect_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.disconnect", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("disconnect_parameter", get_string_field(op_item, "op"));
    ASSERT_EQ(target_param_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, target_param_id));
    yyjson_doc_free(doc);
    assert_validate_ok(disconnect_path);
    assert_parameter_show_value(disconnect_path, source_param_id, "3", 0u);
    assert_parameter_show_source(disconnect_path, target_param_id, 0u);
    assert_script_graph_data_edge_missing(disconnect_path,
                                          manifest.root_behavior_id,
                                          source_param_id,
                                          target_param_id);

    snprintf(args, sizeof(args),
             "-f json script param remove --param %u "
             "\"%s\" -o \"%s\"",
             source_param_id,
             disconnect_path,
             remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.param.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("remove_parameter", get_string_field(op_item, "op"));
    ASSERT_EQ(source_param_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    op_item = yyjson_arr_get(operations, 1);
    ASSERT_STR_EQ("interface_policy", get_string_field(op_item, "op"));
    deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(deleted_objects, source_param_id));
    yyjson_doc_free(doc);
    assert_validate_ok(remove_path);

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             manifest.root_behavior_id,
             remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    ASSERT_NULL(find_array_object_by_id(get_array_field(data, "output_parameters"),
                                        source_param_id));
    yyjson_doc_free(doc);
    load_behavior_parameter_entry_by_name(remove_path,
                                          target_node_id,
                                          "input_parameters",
                                          "LevelIn",
                                          &removed_target_param_id);
    assert_parameter_show_source(remove_path, removed_target_param_id, 0u);
    assert_script_graph_data_edge_missing(remove_path,
                                          manifest.root_behavior_id,
                                          source_param_id,
                                          removed_target_param_id);
    (void)source_param_index;
}

TEST(cli, script_operation_crud_roundtrip)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    yyjson_val *root = NULL;
    yyjson_val *operations = NULL;
    yyjson_val *op_item = NULL;
    yyjson_val *changed_objects = NULL;
    yyjson_val *deleted_objects = NULL;
    uint32_t lhs_id = 0;
    uint32_t rhs_id = 0;
    uint32_t alt_id = 0;
    uint32_t out_id = 0;
    uint32_t op_id = 0;
    char args[1024];
    const char *lhs_add_path = "test_script_edit_tmp/op_lhs_add.cmo";
    const char *rhs_add_path = "test_script_edit_tmp/op_rhs_add.cmo";
    const char *out_add_path = "test_script_edit_tmp/op_out_add.cmo";
    const char *alt_add_path = "test_script_edit_tmp/op_alt_add.cmo";
    const char *lhs_set_path = "test_script_edit_tmp/op_lhs_set.cmo";
    const char *rhs_set_path = "test_script_edit_tmp/op_rhs_set.cmo";
    const char *alt_set_path = "test_script_edit_tmp/op_alt_set.cmo";
    const char *op_add_path = "test_script_edit_tmp/op_add.cmo";
    const char *op_rewire_path = "test_script_edit_tmp/op_rewire.cmo";
    const char *op_remove_path = "test_script_edit_tmp/op_remove.cmo";

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    make_dir("test_script_edit_tmp");
    remove(lhs_add_path);
    remove(rhs_add_path);
    remove(out_add_path);
    remove(alt_add_path);
    remove(lhs_set_path);
    remove(rhs_set_path);
    remove(alt_set_path);
    remove(op_add_path);
    remove(op_rewire_path);
    remove(op_remove_path);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_INT --name Lhs "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             lhs_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    lhs_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_INT --name Rhs "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             lhs_add_path,
             rhs_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    rhs_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind out "
             "--type CKPGUID_INT --name Result "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             rhs_add_path,
             out_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    out_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_INT --name Alt "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             out_add_path,
             alt_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    alt_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param set --param %u --value 3 "
             "\"%s\" -o \"%s\"",
             lhs_id,
             alt_add_path,
             lhs_set_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script param set --param %u --value 4 "
             "\"%s\" -o \"%s\"",
             rhs_id,
             lhs_set_path,
             rhs_set_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script param set --param %u --value 7 "
             "\"%s\" -o \"%s\"",
             alt_id,
             rhs_set_path,
             alt_set_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);

    snprintf(args, sizeof(args),
             "-f json script op add --parent %u "
             "--op-guid 33CC6B49-3589282B --in1 %u --in2 %u --out %u "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             lhs_id,
             rhs_id,
             out_id,
             alt_set_path,
             op_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.op.add", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    op_id = (uint32_t)get_uint_field(data, "op_id");
    ASSERT_TRUE(op_id != 0u);
    yyjson_doc_free(doc);
    assert_validate_ok(op_add_path);

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             manifest.root_behavior_id,
             op_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    op_item = find_array_object_by_id(operations, op_id);
    ASSERT_NOT_NULL(op_item);
    ASSERT_STR_EQ("33CC6B49-3589282B", get_string_field(op_item, "operation_guid"));
    ASSERT_EQ(lhs_id, (uint32_t)get_uint_field(op_item, "in1_id"));
    ASSERT_EQ(rhs_id, (uint32_t)get_uint_field(op_item, "in2_id"));
    ASSERT_EQ(out_id, (uint32_t)get_uint_field(op_item, "out_id"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script op rewire --op %u --in1 %u "
             "\"%s\" -o \"%s\"",
             op_id,
             alt_id,
             op_add_path,
             op_rewire_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.op.rewire", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("rewire_operation", get_string_field(op_item, "op"));
    ASSERT_EQ(op_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(changed_objects, op_id));
    yyjson_doc_free(doc);
    assert_validate_ok(op_rewire_path);

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             manifest.root_behavior_id,
             op_rewire_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    op_item = find_array_object_by_id(operations, op_id);
    ASSERT_NOT_NULL(op_item);
    ASSERT_EQ(alt_id, (uint32_t)get_uint_field(op_item, "in1_id"));
    ASSERT_EQ(rhs_id, (uint32_t)get_uint_field(op_item, "in2_id"));
    ASSERT_EQ(out_id, (uint32_t)get_uint_field(op_item, "out_id"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script op remove --op %u "
             "\"%s\" -o \"%s\"",
             op_id,
             op_rewire_path,
             op_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    root = yyjson_doc_get_root(doc);
    ASSERT_STR_EQ("script.op.remove", get_string_field(root, "command"));
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(yyjson_obj_get(data, "result_handles") == NULL);
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, yyjson_arr_size(operations));
    op_item = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("remove_operation", get_string_field(op_item, "op"));
    ASSERT_EQ(op_id, (uint32_t)get_uint_field(op_item, "primary_id"));
    op_item = yyjson_arr_get(operations, 1);
    ASSERT_STR_EQ("interface_policy", get_string_field(op_item, "op"));
    deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_NOT_NULL(find_array_object_by_id(deleted_objects, op_id));
    yyjson_doc_free(doc);
    assert_validate_ok(op_remove_path);

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             manifest.root_behavior_id,
             op_remove_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_NULL(find_array_object_by_id(operations, op_id));
    yyjson_doc_free(doc);
}

TEST(cli, script_operation_rejects_invalid_signature)
{
    rewrite_manifest_t manifest;
    cli_run_result_t result;
    yyjson_doc *doc = NULL;
    yyjson_val *data = NULL;
    uint32_t lhs_id = 0;
    uint32_t rhs_id = 0;
    uint32_t out_id = 0;
    uint32_t text_id = 0;
    uint32_t bool_in_id = 0;
    uint32_t bool_out_id = 0;
    uint32_t op_id = 0;
    char args[1024];
    const char *lhs_add_path = "test_script_edit_tmp/op_invalid_lhs_add.cmo";
    const char *rhs_add_path = "test_script_edit_tmp/op_invalid_rhs_add.cmo";
    const char *out_add_path = "test_script_edit_tmp/op_invalid_out_add.cmo";
    const char *text_add_path = "test_script_edit_tmp/op_invalid_text_add.cmo";
    const char *bool_in_add_path = "test_script_edit_tmp/op_invalid_bool_in_add.cmo";
    const char *bool_out_add_path = "test_script_edit_tmp/op_invalid_bool_out_add.cmo";
    const char *valid_add_path = "test_script_edit_tmp/op_invalid_valid_add.cmo";
    const char *invalid_add_path = "test_script_edit_tmp/op_invalid_add_fail.cmo";
    const char *invalid_rewire_path = "test_script_edit_tmp/op_invalid_rewire_fail.cmo";
    const char *invalid_unary_add_path = "test_script_edit_tmp/op_invalid_unary_add_fail.cmo";

    ASSERT_TRUE(load_rewrite_manifest(&manifest));
    make_dir("test_script_edit_tmp");
    remove(lhs_add_path);
    remove(rhs_add_path);
    remove(out_add_path);
    remove(text_add_path);
    remove(bool_in_add_path);
    remove(bool_out_add_path);
    remove(valid_add_path);
    remove(invalid_add_path);
    remove(invalid_rewire_path);
    remove(invalid_unary_add_path);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_INT --name Lhs "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             lhs_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    lhs_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_INT --name Rhs "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             lhs_add_path,
             rhs_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    rhs_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind out "
             "--type CKPGUID_INT --name Result "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             rhs_add_path,
             out_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    out_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_STRING --name Text "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             out_add_path,
             text_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    text_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind local "
             "--type CKPGUID_BOOL --name Flag "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             text_add_path,
             bool_in_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    bool_in_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script param add --owner %u --kind out "
             "--type CKPGUID_BOOL --name FlagOut "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             bool_in_add_path,
             bool_out_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    bool_out_id = (uint32_t)get_uint_field(data, "param_id");
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script op add --parent %u "
             "--op-guid 33CC6B49-3589282B --in1 %u --in2 %u --out %u "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             lhs_id,
             text_id,
             out_id,
             text_add_path,
             invalid_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);
    ASSERT_FALSE(file_exists(invalid_add_path));

    snprintf(args, sizeof(args),
             "-f json script op add --parent %u "
             "--op-guid 0E5C02E8-3AAD7BB8 --in1 %u --in2 %u --out %u "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             bool_in_id,
             lhs_id,
             bool_out_id,
             bool_out_add_path,
             invalid_unary_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);
    ASSERT_FALSE(file_exists(invalid_unary_add_path));

    snprintf(args, sizeof(args),
             "-f json script op add --parent %u "
             "--op-guid 33CC6B49-3589282B --in1 %u --in2 %u --out %u "
             "\"%s\" -o \"%s\"",
             manifest.root_behavior_id,
             lhs_id,
             rhs_id,
             out_id,
             bool_out_add_path,
             valid_add_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    op_id = (uint32_t)get_uint_field(data, "op_id");
    ASSERT_TRUE(op_id != 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script op rewire --op %u --in2 %u "
             "\"%s\" -o \"%s\"",
             op_id,
             text_id,
             valid_add_path,
             invalid_rewire_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);
    ASSERT_FALSE(file_exists(invalid_rewire_path));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, script_edit_fixture_manifest_contains_locked_ballance_ids);
    REGISTER_TEST(cli, script_edit_report_contract_is_checked_in);
    REGISTER_TEST(cli, script_graph_json_smoke);
    REGISTER_TEST(cli, script_node_and_io_crud_roundtrip);
    REGISTER_TEST(cli, script_node_add_dry_run_reports_schema_v2);
    REGISTER_TEST(cli, script_io_add_dry_run_exposes_executor_validation_parity);
    REGISTER_TEST(cli, script_node_remove_canonicalizes_interface_refs);
    REGISTER_TEST(cli, script_node_remove_preserve_rejects_stale_interface_refs);
    REGISTER_TEST(cli, script_node_remove_remove_strips_interface_data);
    REGISTER_TEST(cli, script_control_flow_crud_roundtrip);
    REGISTER_TEST(cli, script_link_add_dry_run_reports_executor_validation);
    REGISTER_TEST(cli, script_link_remove_canonicalizes_interface_refs);
    REGISTER_TEST(cli, script_io_remove_canonicalizes_interface_refs);
    REGISTER_TEST(cli, script_param_remove_canonicalizes_interface_refs);
    REGISTER_TEST(cli, script_param_add_dry_run_reports_executor_validation);
    REGISTER_TEST(cli, script_op_remove_canonicalizes_interface_refs);
    REGISTER_TEST(cli, script_op_add_dry_run_reports_executor_validation);
    REGISTER_TEST(cli, script_parameter_crud_roundtrip);
    REGISTER_TEST(cli, script_operation_crud_roundtrip);
    REGISTER_TEST(cli, script_operation_rejects_invalid_signature);
TEST_MAIN_END()

