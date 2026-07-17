/**
 * @file test_cli_behavior_graph.c
 * @brief CLI behavior graph smoke tests
 */

#include "test_framework.h"
#include "write_semantic_probe.h"

#include "../../tools/nmo_cli_common.h"
#include "document/nmo_document_save.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "core/nmo_array.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "yyjson.h"

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

#define NMO_INTERFACE_EDIT_FIXTURE NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo")
#define NMO_INTERFACE_EDIT_TARGET_ID 253u
#define NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID 250u
#define NMO_INTERFACE_EDIT_LINK_ID 107u
#define NMO_INTERFACE_EDIT_LINK_WITH_POINT_ID 247u
#define NMO_INTERFACE_EDIT_SUB_BEHAVIOR_ID 86u
#define NMO_INTERFACE_EDIT_MISSING_OP_ID 999u

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

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void assert_probe_open(write_semantic_probe_t *probe, const char *path) {
    nmo_status_t status = write_probe_open(probe, path);
    if (status != NMO_OK) {
        fprintf(stderr, "Failed to open semantic probe for %s: %d\n", path, status);
    }
    ASSERT_EQ(NMO_OK, status);
}

static const nmo_behavior_state_t *probe_behavior_state(
    write_semantic_probe_t *probe,
    const char *path,
    nmo_object_id_t behavior_id) {
    assert_probe_open(probe, path);
    return (const nmo_behavior_state_t *)write_probe_state(
        probe, behavior_id, CKPGUID_BEHAVIOR);
}

static bool create_behavior_link_fixture(
    const char *path,
    nmo_object_id_t *out_behavior_id,
    nmo_object_id_t *out_from_id,
    nmo_object_id_t *out_to_id)
{
    if (out_behavior_id == NULL || out_from_id == NULL || out_to_id == NULL) {
        return false;
    }
    *out_behavior_id = 0;
    *out_from_id = 0;
    *out_to_id = 0;

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    if (ctx == NULL) {
        return false;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_runtime_report_t report = {0};
    nmo_object_id_t owner_id = 0;
    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t source_behavior_id = 0;
    nmo_object_id_t target_behavior_id = 0;
    nmo_object_id_t from_id = 0;
    nmo_object_id_t to_id = 0;
    bool ok =
        nmo_session_create_object(session, NMO_CID_3DENTITY, "Owner",
                                  (nmo_guid_t){0, 0}, &owner_id, &report) == NMO_OK &&
        nmo_session_create_object(session, NMO_CID_BEHAVIOR, "Graph",
                                  (nmo_guid_t){0, 0}, &behavior_id, &report) == NMO_OK &&
        nmo_session_create_object(session, NMO_CID_BEHAVIOR, "Source",
                                  (nmo_guid_t){0, 0}, &source_behavior_id, &report) == NMO_OK &&
        nmo_session_create_object(session, NMO_CID_BEHAVIOR, "Target",
                                  (nmo_guid_t){0, 0}, &target_behavior_id, &report) == NMO_OK &&
        nmo_session_create_object(session, NMO_CID_BEHAVIORIO, "Out",
                                  (nmo_guid_t){0, 0}, &from_id, &report) == NMO_OK &&
        nmo_session_create_object(session, NMO_CID_BEHAVIORIO, "In",
                                  (nmo_guid_t){0, 0}, &to_id, &report) == NMO_OK;

    if (ok) {
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        nmo_object_t *owner_obj =
            repo ? nmo_object_repository_find_by_id(repo, owner_id) : NULL;
        nmo_object_t *behavior_obj =
            repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
        nmo_object_t *source_behavior_obj =
            repo ? nmo_object_repository_find_by_id(repo, source_behavior_id) : NULL;
        nmo_object_t *target_behavior_obj =
            repo ? nmo_object_repository_find_by_id(repo, target_behavior_id) : NULL;
        nmo_beobject_state_t *owner_state = owner_obj
            ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
            : NULL;
        nmo_behavior_state_t *behavior_state = behavior_obj
            ? (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj)
            : NULL;
        nmo_behavior_state_t *source_behavior_state = source_behavior_obj
            ? (nmo_behavior_state_t *)nmo_object_get_state(source_behavior_obj)
            : NULL;
        nmo_behavior_state_t *target_behavior_state = target_behavior_obj
            ? (nmo_behavior_state_t *)nmo_object_get_state(target_behavior_obj)
            : NULL;
        ok = owner_state != NULL &&
             behavior_state != NULL &&
             source_behavior_state != NULL &&
             target_behavior_state != NULL &&
             nmo_beobject_script_array_append(&owner_state->scripts, behavior_id) == NMO_OK &&
             nmo_behavior_ref_array_append(&behavior_state->sub_behaviors, source_behavior_id, NULL) == NMO_OK &&
             nmo_behavior_ref_array_append(&behavior_state->sub_behaviors, target_behavior_id, NULL) == NMO_OK &&
             nmo_behavior_ref_array_append(&source_behavior_state->outputs, from_id, NULL) == NMO_OK &&
             nmo_behavior_ref_array_append(&target_behavior_state->inputs, to_id, NULL) == NMO_OK;
        if (ok) {
            behavior_state->flags |= 0x00000002u;
            nmo_behavior_set_owner_id(behavior_state, owner_id);
            behavior_state->has_save_flags = true;
            behavior_state->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV;
            nmo_behavior_set_owner_id(source_behavior_state, behavior_id);
            source_behavior_state->has_save_flags = true;
            source_behavior_state->save_flags |= CK_STATESAVE_BEHAVIOROUTPUTS;
            nmo_behavior_set_owner_id(target_behavior_state, behavior_id);
            target_behavior_state->has_save_flags = true;
            target_behavior_state->save_flags |= CK_STATESAVE_BEHAVIORINPUTS;
        }
    }

    if (ok) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        ok = nmo_save_file(session, path, &save_opts) == NMO_OK;
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    if (ok) {
        *out_behavior_id = behavior_id;
        *out_from_id = from_id;
        *out_to_id = to_id;
    }
    return ok;
}

static bool create_interface_comment_fixture(const char *path) {
    remove(path);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface add-comment %u --text \"Debt note\" --rect 1,2,3,4 \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE,
             path);
    cli_run_result_t result = run_cli_capture(args);
    bool ok = result.output != NULL &&
              result.exit_code == NMO_CLI_EXIT_SUCCESS &&
              file_exists(path);
    free(result.output);
    return ok;
}

static bool create_sectioned_graph_interface_fixture(const char *path) {
    remove(path);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    if (ctx == NULL) {
        return false;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        nmo_context_release(ctx);
        return false;
    }

    bool ok = nmo_session_load_file(session, NMO_INTERFACE_EDIT_FIXTURE,
                                    NULL, NULL) == NMO_OK;
    if (ok) {
        ok = nmo_session_ensure_behavior_acceleration(session) == NMO_OK;
    }
    if (ok) {
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        nmo_object_t *obj = repo
            ? nmo_object_repository_find_by_id(repo, NMO_INTERFACE_EDIT_TARGET_ID)
            : NULL;
        nmo_behavior_state_t *state = obj
            ? (nmo_behavior_state_t *)nmo_object_get_state(obj)
            : NULL;
        if (!state || !state->interface_data) {
            ok = false;
        } else {
            state->interface_data->format_flags |=
                NMO_INTERFACE_FORMAT_SECTIONED |
                NMO_INTERFACE_FORMAT_ROOT_GRAPH;
        }
    }
    if (ok) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        ok = nmo_save_file(session, path, &save_opts) == NMO_OK;
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return ok && file_exists(path);
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

static bool json_has_nonempty_string(yyjson_val *obj, const char *key) {
    yyjson_val *val = yyjson_obj_get(obj, key);
    const char *str = yyjson_get_str(val);
    return str && str[0] != '\0';
}

static yyjson_val *find_array_object_by_string(yyjson_val *arr,
                                               const char *key,
                                               const char *value) {
    if (!arr || !key || !value) {
        return NULL;
    }

    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(arr, idx, max, item) {
        yyjson_val *field = yyjson_obj_get(item, key);
        const char *str = yyjson_get_str(field);
        if (str && strcmp(str, value) == 0) {
            return item;
        }
    }
    return NULL;
}

static void run_json_command(const char *args, const char *expected_command, yyjson_doc **out_doc) {
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
    const char *command = get_string_field(root, "command");
    ASSERT_TRUE(command && strcmp(command, expected_command) == 0);
    ASSERT_NOT_NULL(get_object_field(root, "data"));
    *out_doc = doc;
}

TEST(cli, behavior_graph_json) {
    const char *file_path = NMO_TEST_DATA_FILE("Ballance/base.cmo");

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

TEST(cli, behavior_graph_json_parity_metadata) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior graph --max-nodes 500 --max-edges 800 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.graph", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *graph = get_object_field(data, "graph");
    ASSERT_NOT_NULL(graph);
    yyjson_val *nodes = get_array_field(graph, "nodes");
    yyjson_val *edges = get_array_field(graph, "edges");
    ASSERT_NOT_NULL(nodes);
    ASSERT_NOT_NULL(edges);

    yyjson_val *behavior_node = find_array_object_by_string(nodes, "kind", "behavior");
    ASSERT_NOT_NULL(behavior_node);
    ASSERT_TRUE(json_has_nonempty_string(behavior_node, "display_name"));
    ASSERT_TRUE(json_has_nonempty_string(behavior_node, "behavior_type"));

    yyjson_val *operation_node = find_array_object_by_string(nodes, "kind", "operation");
    ASSERT_NOT_NULL(operation_node);
    ASSERT_TRUE(json_has_nonempty_string(operation_node, "display_name"));
    ASSERT_TRUE(json_has_nonempty_string(operation_node, "operation_name"));
    ASSERT_TRUE(json_has_nonempty_string(operation_node, "operation_guid"));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(operation_node, "in1_id")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(operation_node, "in2_id")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(operation_node, "out_id")));

    yyjson_val *behavior_link = find_array_object_by_string(edges, "kind", "behavior_link");
    ASSERT_NOT_NULL(behavior_link);
    ASSERT_TRUE(json_has_nonempty_string(behavior_link, "from_name"));
    ASSERT_TRUE(json_has_nonempty_string(behavior_link, "to_name"));
    ASSERT_TRUE(json_has_nonempty_string(behavior_link, "source_io_name"));
    ASSERT_TRUE(json_has_nonempty_string(behavior_link, "target_io_name"));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(behavior_link, "source_owner_id")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(behavior_link, "target_owner_id")));
    ASSERT_TRUE(json_has_nonempty_string(behavior_link, "source_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(behavior_link, "target_owner_name"));

    yyjson_val *parameter_edge = find_array_object_by_string(edges, "kind", "param_source");
    if (!parameter_edge) {
        parameter_edge = find_array_object_by_string(edges, "kind", "param_local");
    }
    ASSERT_NOT_NULL(parameter_edge);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(parameter_edge, "parameter_id")));
    ASSERT_TRUE(json_has_nonempty_string(parameter_edge, "parameter_name"));
    ASSERT_TRUE(json_has_nonempty_string(parameter_edge, "type_name"));
    ASSERT_TRUE(json_has_nonempty_string(parameter_edge, "type_guid"));

    yyjson_val *param_in_edge = find_array_object_by_string(edges, "kind", "param_in");
    ASSERT_NOT_NULL(param_in_edge);
    yyjson_val *param_in_id = yyjson_obj_get(param_in_edge, "parameter_id");
    yyjson_val *param_in_to = yyjson_obj_get(param_in_edge, "to");
    ASSERT_TRUE(yyjson_is_uint(param_in_id));
    ASSERT_TRUE(yyjson_is_uint(param_in_to));
    ASSERT_EQ(yyjson_get_uint(param_in_to), yyjson_get_uint(param_in_id));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_graph_json_truncation_counts) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior graph --max-nodes 5 --max-edges 5 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.graph", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *graph = get_object_field(data, "graph");
    ASSERT_NOT_NULL(graph);
    yyjson_val *truncated = get_object_field(graph, "truncated");
    ASSERT_NOT_NULL(truncated);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(truncated, "nodes_emitted")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(truncated, "edges_emitted")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(truncated, "nodes_dropped")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(truncated, "edges_dropped")));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_graph_dot_labels_behavior_delay) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior graph --dot 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "delay=");
    free(result.output);
}

TEST(cli, behavior_stats_json_distributions) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior stats \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.stats", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *param_types = get_array_field(data, "parameter_types_top");
    ASSERT_NOT_NULL(param_types);
    ASSERT_TRUE(yyjson_arr_size(param_types) > 0);
    yyjson_val *param_type = yyjson_arr_get(param_types, 0);
    ASSERT_TRUE(json_has_nonempty_string(param_type, "type_name"));
    ASSERT_TRUE(json_has_nonempty_string(param_type, "type_guid"));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(param_type, "count")));

    yyjson_val *op_types = get_array_field(data, "operation_types_top");
    ASSERT_NOT_NULL(op_types);
    ASSERT_TRUE(yyjson_arr_size(op_types) > 0);
    yyjson_val *op_type = yyjson_arr_get(op_types, 0);
    ASSERT_TRUE(json_has_nonempty_string(op_type, "operation_name"));
    ASSERT_TRUE(json_has_nonempty_string(op_type, "operation_guid"));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(op_type, "count")));

    yyjson_val *tree_depth = get_object_field(data, "tree_depth");
    ASSERT_NOT_NULL(tree_depth);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(tree_depth, "max")));
    ASSERT_TRUE(yyjson_is_real(yyjson_obj_get(tree_depth, "avg")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(tree_depth, "p95")));

    yyjson_val *script_sub_counts = get_object_field(data, "script_sub_behavior_counts");
    ASSERT_NOT_NULL(script_sub_counts);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(script_sub_counts, "max")));
    ASSERT_TRUE(yyjson_is_real(yyjson_obj_get(script_sub_counts, "avg")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(script_sub_counts, "p95")));

    yyjson_val *link_delays = get_object_field(data, "link_delay_distribution");
    ASSERT_NOT_NULL(link_delays);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(link_delays, "zero_delay")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(link_delays, "next_frame")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(link_delays, "multi_frame")));

    yyjson_val *broken_refs = get_object_field(data, "broken_references");
    ASSERT_NOT_NULL(broken_refs);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(broken_refs, "behavior_links")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(broken_refs, "sub_behaviors")));

    yyjson_val *interface_parse = get_object_field(data, "interface_parse");
    ASSERT_NOT_NULL(interface_parse);

    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json behavior stats \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Menu.nmo"));
    yyjson_doc *menu_doc = NULL;
    run_json_command(args, "behavior.stats", &menu_doc);
    ASSERT_NOT_NULL(menu_doc);
    yyjson_val *menu_root = yyjson_doc_get_root(menu_doc);
    ASSERT_NOT_NULL(menu_root);
    yyjson_val *menu_data = get_object_field(menu_root, "data");
    ASSERT_NOT_NULL(menu_data);
    ASSERT_NOT_NULL(get_array_field(menu_data, "parameter_types_top"));
    ASSERT_NOT_NULL(get_array_field(menu_data, "operation_types_top"));
    ASSERT_NOT_NULL(get_object_field(menu_data, "tree_depth"));
    yyjson_doc_free(menu_doc);
}

TEST(cli, behavior_stats_json_survives_interface_parse_failures) {
    char args[1024];

    snprintf(args, sizeof(args),
             "-f json behavior stats \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
    yyjson_doc *balls_doc = NULL;
    run_json_command(args, "behavior.stats", &balls_doc);
    ASSERT_NOT_NULL(balls_doc);
    yyjson_doc_free(balls_doc);

    snprintf(args, sizeof(args),
             "-f json behavior stats \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Menu.nmo"));
    yyjson_doc *menu_doc = NULL;
    run_json_command(args, "behavior.stats", &menu_doc);
    ASSERT_NOT_NULL(menu_doc);
    yyjson_doc_free(menu_doc);
}

TEST(cli, behavior_stats_json_marks_interface_unavailable_without_data) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior stats \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.stats", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *interface_available = yyjson_obj_get(data, "interface_available");
    ASSERT_TRUE(interface_available && yyjson_is_bool(interface_available));
    ASSERT_FALSE(yyjson_get_bool(interface_available));

    yyjson_val *interface_parse = get_object_field(data, "interface_parse");
    ASSERT_NOT_NULL(interface_parse);
    yyjson_val *parsed_count = yyjson_obj_get(interface_parse, "parsed_count");
    ASSERT_TRUE(parsed_count && yyjson_is_uint(parsed_count));
    ASSERT_EQ((uint64_t)0, yyjson_get_uint(parsed_count));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_show_json_survives_interface_parse_failures) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior show 11 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.show", &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *id = yyjson_obj_get(data, "id");
    ASSERT_TRUE(id && yyjson_is_uint(id));
    ASSERT_EQ((uint64_t)11, yyjson_get_uint(id));
    yyjson_doc_free(doc);
}

TEST(cli, behavior_show_json_p2_parity) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior show 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.show", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *inputs = get_array_field(data, "input_parameters");
    ASSERT_NOT_NULL(inputs);
    ASSERT_TRUE(yyjson_arr_size(inputs) > 0);
    yyjson_val *first_input = yyjson_arr_get(inputs, 0);
    ASSERT_NOT_NULL(first_input);
    yyjson_val *source_chain = get_array_field(first_input, "source_chain");
    ASSERT_NOT_NULL(source_chain);
    ASSERT_TRUE(yyjson_arr_size(source_chain) >= 2);
    yyjson_val *chain_step = yyjson_arr_get(source_chain, 0);
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(chain_step, "id")));
    ASSERT_TRUE(json_has_nonempty_string(chain_step, "name"));
    ASSERT_TRUE(json_has_nonempty_string(chain_step, "type_name"));
    ASSERT_TRUE(yyjson_is_bool(yyjson_obj_get(chain_step, "is_shared")));
    ASSERT_TRUE(yyjson_is_uint(yyjson_obj_get(chain_step, "owner_id")));
    ASSERT_TRUE(json_has_nonempty_string(chain_step, "owner_name"));

    yyjson_val *locals = get_array_field(data, "local_parameters");
    ASSERT_NOT_NULL(locals);
    bool saw_decoded = false;
    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(locals, idx, max, item) {
        if (json_has_nonempty_string(item, "decoded_value")) {
            saw_decoded = true;
            break;
        }
    }
    ASSERT_TRUE(saw_decoded);

    yyjson_val *ops = get_array_field(data, "operations");
    ASSERT_NOT_NULL(ops);
    ASSERT_TRUE(yyjson_arr_size(ops) > 0);
    yyjson_val *op = yyjson_arr_get(ops, 0);
    ASSERT_TRUE(json_has_nonempty_string(op, "operation_guid"));
    ASSERT_TRUE(json_has_nonempty_string(op, "operation_name"));
    ASSERT_TRUE(json_has_nonempty_string(op, "in1_name"));
    ASSERT_TRUE(json_has_nonempty_string(op, "in1_type_name"));
    ASSERT_TRUE(json_has_nonempty_string(op, "out_name"));
    ASSERT_TRUE(json_has_nonempty_string(op, "out_type_name"));

    yyjson_val *data_flow = get_array_field(data, "data_flow");
    ASSERT_NOT_NULL(data_flow);
    ASSERT_TRUE(yyjson_arr_size(data_flow) > 0);
    yyjson_val *flow = yyjson_arr_get(data_flow, 0);
    ASSERT_TRUE(json_has_nonempty_string(flow, "source_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "source_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "target_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "target_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "type_name"));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_show_text_formats_non_add_operations) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior show 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_FALSE(strstr(result.output, "[Get Length] Pin 0 + Pin 1") != NULL);
    ASSERT_STR_CONTAINS(result.output, "[Get Length]");
    free(result.output);
}

TEST(cli, behavior_trace_json_p2_semantics) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior trace 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.trace", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *entries = get_array_field(data, "entries");
    ASSERT_NOT_NULL(entries);
    ASSERT_TRUE(yyjson_arr_size(entries) > 0);
    yyjson_val *entry = yyjson_arr_get(entries, 0);
    ASSERT_NOT_NULL(entry);
    yyjson_val *steps = get_array_field(entry, "steps");
    ASSERT_NOT_NULL(steps);
    ASSERT_TRUE(yyjson_arr_size(steps) > 0);

    bool saw_bb_proto = false;
    bool saw_exit = false;
    size_t idx, max;
    yyjson_val *step;
    yyjson_arr_foreach(steps, idx, max, step) {
        ASSERT_TRUE(json_has_nonempty_string(step, "target_behavior_type"));
        ASSERT_TRUE(json_has_nonempty_string(step, "transition"));
        const char *transition = get_string_field(step, "transition");
        if (transition && strcmp(transition, "exit_to_parent") == 0) {
            saw_exit = true;
        }
        if (json_has_nonempty_string(step, "target_bb_proto_name")) {
            saw_bb_proto = true;
        }
    }
    ASSERT_TRUE(saw_bb_proto);
    ASSERT_TRUE(saw_exit);

    yyjson_doc_free(doc);
}

TEST(cli, behavior_trace_json_reports_depth_truncation) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior trace --depth 0 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.trace", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *entries = get_array_field(data, "entries");
    ASSERT_NOT_NULL(entries);
    ASSERT_TRUE(yyjson_arr_size(entries) > 0);
    yyjson_val *entry = yyjson_arr_get(entries, 0);
    ASSERT_NOT_NULL(entry);
    yyjson_val *steps = get_array_field(entry, "steps");
    ASSERT_NOT_NULL(steps);

    bool saw_max_depth = false;
    size_t idx, max;
    yyjson_val *step;
    yyjson_arr_foreach(steps, idx, max, step) {
        const char *reason = get_string_field(step, "truncated_reason");
        if (reason && strcmp(reason, "max_depth") == 0) {
            saw_max_depth = true;
            break;
        }
    }
    ASSERT_TRUE(saw_max_depth);

    yyjson_doc_free(doc);
}

TEST(cli, behavior_trace_text_mentions_current_graph) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior trace 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Current graph:");
    ASSERT_STR_CONTAINS(result.output, "transition:");
    free(result.output);
}

TEST(cli, behavior_read_commands_accept_exact_name_selectors) {
    char args[1024];

    snprintf(args, sizeof(args),
             "-f json behavior show --name \"Register & Activate Init_Script\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    yyjson_doc *show_doc = NULL;
    run_json_command(args, "behavior.show", &show_doc);
    ASSERT_NOT_NULL(show_doc);
    yyjson_val *show_data = get_object_field(yyjson_doc_get_root(show_doc), "data");
    ASSERT_NOT_NULL(show_data);
    ASSERT_EQ(237, yyjson_get_uint(yyjson_obj_get(show_data, "id")));
    yyjson_doc_free(show_doc);

    snprintf(args, sizeof(args),
             "-f json behavior graph --max-nodes 5 --max-edges 5 "
             "--name \"Register & Activate Init_Script\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    yyjson_doc *graph_doc = NULL;
    run_json_command(args, "behavior.graph", &graph_doc);
    ASSERT_NOT_NULL(graph_doc);
    yyjson_doc_free(graph_doc);

    snprintf(args, sizeof(args),
             "-f json behavior trace --name \"Register & Activate Init_Script\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    yyjson_doc *trace_doc = NULL;
    run_json_command(args, "behavior.trace", &trace_doc);
    ASSERT_NOT_NULL(trace_doc);
    yyjson_doc_free(trace_doc);

    snprintf(args, sizeof(args),
             "-f json behavior dump --name \"Register & Activate Init_Script\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    yyjson_doc *dump_doc = NULL;
    run_json_command(args, "behavior.dump", &dump_doc);
    ASSERT_NOT_NULL(dump_doc);
    yyjson_doc_free(dump_doc);

    snprintf(args, sizeof(args),
             "-f json behavior interface show --name \"Topic - Prevent Collision\" \"%s\"",
             NMO_INTERFACE_EDIT_FIXTURE);
    yyjson_doc *iface_doc = NULL;
    run_json_command(args, "behavior.interface", &iface_doc);
    ASSERT_NOT_NULL(iface_doc);
    yyjson_doc_free(iface_doc);
}

TEST(cli, behavior_dump_help_describes_tree_overview_options) {
    cli_run_result_t result = run_cli_capture("behavior dump --help");
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "tree overview");
    ASSERT_STR_CONTAINS(result.output, "--flows");
    ASSERT_STR_CONTAINS(result.output, "--values");
    free(result.output);
}

TEST(cli, behavior_dump_text_flows_show_owner_endpoints) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior dump --flows 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Execution Flow");
    ASSERT_STR_CONTAINS(result.output, "Data Flow");
    ASSERT_STR_CONTAINS(result.output, "Register & Activate Init_Script.In 0 -> Op.In");
    free(result.output);
}

TEST(cli, behavior_dump_all_rejects_flows) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior dump --all --flows \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "--flows cannot be used with --all");
    free(result.output);
}

TEST(cli, behavior_dump_text_flows_show_empty_execution_section) {
    const char *fixture = "test_behavior_dump_empty_flows.nmo";
    remove(fixture);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_id = 0;
    nmo_object_id_t to_id = 0;
    ASSERT_TRUE(create_behavior_link_fixture(fixture, &behavior_id, &from_id, &to_id));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior dump --flows %u \"%s\"",
             (unsigned)behavior_id, fixture);

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Execution Flow");
    ASSERT_STR_CONTAINS(result.output, "(no execution links)");
    free(result.output);

    remove(fixture);
}

TEST(cli, behavior_dump_text_values_show_decoded_values) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior dump --values 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Decoded Values");
    ASSERT_STR_CONTAINS(result.output, "Start [int] = 0");
    free(result.output);
}

TEST(cli, behavior_dump_json_flows_include_owner_names) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior dump --flows 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.dump", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *execution_flow = get_array_field(data, "execution_flow");
    ASSERT_NOT_NULL(execution_flow);
    ASSERT_TRUE(yyjson_arr_size(execution_flow) > 0);
    yyjson_val *exec = yyjson_arr_get(execution_flow, 0);
    ASSERT_TRUE(json_has_nonempty_string(exec, "source_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(exec, "source_io_name"));
    ASSERT_TRUE(json_has_nonempty_string(exec, "target_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(exec, "target_io_name"));

    yyjson_val *data_flow = get_array_field(data, "data_flow");
    ASSERT_NOT_NULL(data_flow);
    ASSERT_TRUE(yyjson_arr_size(data_flow) > 0);
    yyjson_val *flow = yyjson_arr_get(data_flow, 0);
    ASSERT_TRUE(json_has_nonempty_string(flow, "source_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "source_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "target_owner_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "target_name"));
    ASSERT_TRUE(json_has_nonempty_string(flow, "type_name"));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_dump_json_values_include_decoded_values) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior dump --values 237 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.dump", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *tree = get_array_field(data, "tree");
    ASSERT_NOT_NULL(tree);
    ASSERT_TRUE(yyjson_arr_size(tree) > 0);
    yyjson_val *root_node = yyjson_arr_get(tree, 0);
    ASSERT_NOT_NULL(root_node);
    yyjson_val *decoded = get_array_field(root_node, "decoded_values");
    ASSERT_NOT_NULL(decoded);
    ASSERT_TRUE(yyjson_arr_size(decoded) > 0);

    bool saw_decoded = false;
    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(decoded, idx, max, item) {
        if (json_has_nonempty_string(item, "decoded_value")) {
            saw_decoded = true;
            break;
        }
    }
    ASSERT_TRUE(saw_decoded);

    yyjson_doc_free(doc);
}

TEST(cli, behavior_list_json_sanitizes_gameplay_names) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior list \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.list", &doc);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *objects = get_array_field(data, "objects");
    ASSERT_NOT_NULL(objects);
    ASSERT_TRUE(yyjson_arr_size(objects) > 0);
    yyjson_doc_free(doc);
}

TEST(cli, behavior_json_smoke_real_samples) {
    char args[1024];

    snprintf(args, sizeof(args),
             "-f json behavior show 49 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/2D Text.nmo"));
    yyjson_doc *show_doc = NULL;
    run_json_command(args, "behavior.show", &show_doc);
    ASSERT_NOT_NULL(show_doc);
    yyjson_doc_free(show_doc);

    snprintf(args, sizeof(args),
             "-f json behavior find --name Op \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"));
    yyjson_doc *find_doc = NULL;
    run_json_command(args, "behavior.find", &find_doc);
    ASSERT_NOT_NULL(find_doc);
    yyjson_doc_free(find_doc);

    snprintf(args, sizeof(args),
             "-f json behavior trace 49 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/2D Text.nmo"));
    yyjson_doc *trace_doc = NULL;
    run_json_command(args, "behavior.trace", &trace_doc);
    ASSERT_NOT_NULL(trace_doc);
    yyjson_doc_free(trace_doc);

    snprintf(args, sizeof(args),
             "-f json behavior dump 49 \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/2D Text.nmo"));
    yyjson_doc *dump_doc = NULL;
    run_json_command(args, "behavior.dump", &dump_doc);
    ASSERT_NOT_NULL(dump_doc);
    yyjson_doc_free(dump_doc);
}

TEST(cli, behavior_help_mentions_json_output) {
    static const char *commands[] = {
        "behavior list --help",
        "behavior stats --help",
        "behavior show --help",
        "behavior graph --help",
        "behavior find --help",
        "behavior trace --help",
        "behavior dump --help",
        "behavior interface --help",
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        cli_run_result_t result = run_cli_capture(commands[i]);
        ASSERT_NOT_NULL(result.output);
        ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
        ASSERT_STR_CONTAINS(result.output, "-f json");
        free(result.output);
    }

    cli_run_result_t graph = run_cli_capture("behavior graph --help");
    ASSERT_NOT_NULL(graph.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, graph.exit_code);
    ASSERT_STR_CONTAINS(graph.output, "--dot");
    free(graph.output);

    cli_run_result_t iface = run_cli_capture("behavior interface --help");
    ASSERT_NOT_NULL(iface.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, iface.exit_code);
    ASSERT_STR_CONTAINS(iface.output, "set-viewport");
    ASSERT_STR_CONTAINS(iface.output, "translate");
    ASSERT_STR_CONTAINS(iface.output, "canonicalize");
    free(iface.output);
}

TEST(cli, behavior_graph_dot) {
    const char *file_path = NMO_TEST_DATA_FILE("Ballance/base.cmo");

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

TEST(cli, behavior_add_link_requires_output_unless_dry_run) {
    const char *fixture = "test_behavior_add_link_missing_output.nmo";
    remove(fixture);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_id = 0;
    nmo_object_id_t to_id = 0;
    ASSERT_TRUE(create_behavior_link_fixture(fixture, &behavior_id, &from_id, &to_id));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior add-link --parent %u --from %u --to %u \"%s\"",
             (unsigned)behavior_id, (unsigned)from_id, (unsigned)to_id, fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "output");
    free(result.output);

    remove(fixture);
}

TEST(cli, behavior_add_link_dry_run_does_not_write_output) {
    const char *fixture = "test_behavior_add_link_dry_fixture.nmo";
    const char *output = "test_behavior_add_link_dry_output.nmo";
    remove(fixture);
    remove(output);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_id = 0;
    nmo_object_id_t to_id = 0;
    ASSERT_TRUE(create_behavior_link_fixture(fixture, &behavior_id, &from_id, &to_id));

    write_semantic_probe_t before;
    assert_probe_open(&before, fixture);
    const nmo_behavior_state_t *before_behavior =
        (const nmo_behavior_state_t *)write_probe_state(&before, behavior_id, CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(before_behavior);
    size_t before_links = before_behavior->sub_behavior_links.count;
    size_t before_objects = write_probe_object_count(&before);
    write_probe_close(&before);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior add-link --dry-run --parent %u --from %u --to %u -o \"%s\" \"%s\"",
             (unsigned)behavior_id, (unsigned)from_id, (unsigned)to_id, output, fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    write_semantic_probe_t after;
    assert_probe_open(&after, fixture);
    const nmo_behavior_state_t *after_behavior =
        (const nmo_behavior_state_t *)write_probe_state(&after, behavior_id, CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(after_behavior);
    ASSERT_EQ(before_links, after_behavior->sub_behavior_links.count);
    ASSERT_EQ(before_objects, write_probe_object_count(&after));
    write_probe_close(&after);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_add_link_saves_output) {
    const char *fixture = "test_behavior_add_link_save_fixture.nmo";
    const char *output = "test_behavior_add_link_save_output.nmo";
    remove(fixture);
    remove(output);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_id = 0;
    nmo_object_id_t to_id = 0;
    ASSERT_TRUE(create_behavior_link_fixture(fixture, &behavior_id, &from_id, &to_id));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior add-link --parent %u --from %u --to %u -o \"%s\" \"%s\"",
             (unsigned)behavior_id, (unsigned)from_id, (unsigned)to_id, output, fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Saved to");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, output);
    const nmo_behavior_state_t *behavior =
        (const nmo_behavior_state_t *)write_probe_state(&probe, behavior_id, CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(behavior);
    ASSERT_EQ(1u, behavior->sub_behavior_links.count);
    nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
        &behavior->sub_behavior_links, 0);
    ASSERT_TRUE(link_id != 0);
    nmo_object_t *link_obj = write_probe_object_by_id(&probe, link_id);
    ASSERT_NOT_NULL(link_obj);
    ASSERT_EQ(NMO_CID_BEHAVIORLINK, link_obj->class_id);
    const nmo_behaviorlink_state_t *link =
        (const nmo_behaviorlink_state_t *)write_probe_state(&probe, link_id, CKPGUID_BEHAVIORLINK);
    ASSERT_NOT_NULL(link);
    ASSERT_EQ(from_id, nmo_behaviorlink_in_io_id(link));
    ASSERT_EQ(to_id, nmo_behaviorlink_out_io_id(link));
    write_probe_close(&probe);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_add_link_json_dry_run) {
    const char *fixture = "test_behavior_add_link_json_dry_fixture.nmo";
    const char *output = "test_behavior_add_link_json_dry_output.nmo";
    remove(fixture);
    remove(output);

    nmo_object_id_t behavior_id = 0;
    nmo_object_id_t from_id = 0;
    nmo_object_id_t to_id = 0;
    ASSERT_TRUE(create_behavior_link_fixture(fixture, &behavior_id, &from_id, &to_id));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior add-link --dry-run --parent %u --from %u --to %u -o \"%s\" \"%s\"",
             (unsigned)behavior_id, (unsigned)from_id, (unsigned)to_id, output, fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_FALSE(file_exists(output));

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ(get_string_field(root, "command"), "behavior.add-link");
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *dry_run = yyjson_obj_get(data, "dry_run");
    ASSERT_TRUE(dry_run && yyjson_is_bool(dry_run));
    ASSERT_TRUE(yyjson_get_bool(dry_run));
    yyjson_doc_free(doc);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_interface_set_pos_requires_output_unless_dry_run) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-pos %u %u 11 22 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "output");
    free(result.output);
}

TEST(cli, behavior_interface_set_pos_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_pos_dry_output.cmo";
    remove(output);

    write_semantic_probe_t before;
    assert_probe_open(&before, NMO_INTERFACE_EDIT_FIXTURE);
    const nmo_behavior_state_t *before_behavior =
        (const nmo_behavior_state_t *)write_probe_state(
            &before, NMO_INTERFACE_EDIT_TARGET_ID, CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(before_behavior);
    ASSERT_NOT_NULL(before_behavior->interface_data);
    float before_h = before_behavior->interface_data->script.h_pos;
    float before_v = before_behavior->interface_data->script.v_pos;
    write_probe_close(&before);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-pos --dry-run %u %u 11 22 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, NMO_INTERFACE_EDIT_FIXTURE);
    const nmo_behavior_state_t *behavior =
        (const nmo_behavior_state_t *)write_probe_state(
            &probe, NMO_INTERFACE_EDIT_TARGET_ID, CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(behavior->interface_data);
    ASSERT_FLOAT_EQ(before_h, behavior->interface_data->script.h_pos, 0.0001f);
    ASSERT_FLOAT_EQ(before_v, behavior->interface_data->script.v_pos, 0.0001f);
    write_probe_close(&probe);

    remove(output);
}

TEST(cli, behavior_interface_set_pos_saves_output) {
    const char *output = "test_behavior_interface_set_pos_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-pos %u %u 11 22 \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE,
             output);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Saved to");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, output);
    const nmo_behavior_state_t *behavior =
        (const nmo_behavior_state_t *)write_probe_state(
            &probe, NMO_INTERFACE_EDIT_TARGET_ID, CKPGUID_BEHAVIOR);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(behavior->interface_data);
    ASSERT_FLOAT_EQ(11.0f, behavior->interface_data->script.h_pos, 0.0001f);
    ASSERT_FLOAT_EQ(22.0f, behavior->interface_data->script.v_pos, 0.0001f);
    write_probe_close(&probe);

    remove(output);
}

TEST(cli, behavior_interface_set_pos_json_dry_run) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior interface set-pos --dry-run %u %u 11 22 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ(get_string_field(root, "command"), "behavior.interface.set-pos");
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *dry_run = yyjson_obj_get(data, "dry_run");
    ASSERT_TRUE(dry_run && yyjson_is_bool(dry_run));
    ASSERT_TRUE(yyjson_get_bool(dry_run));
    yyjson_doc_free(doc);
}

TEST(cli, behavior_interface_edit_commands_accept_exact_name_target_selector) {
    char args[1024];

    snprintf(args, sizeof(args),
             "behavior interface set-pos --dry-run --name \"Topic - Prevent Collision\" "
             "%u 11 22 \"%s\"",
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t set_pos = run_cli_capture(args);
    ASSERT_NOT_NULL(set_pos.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, set_pos.exit_code);
    ASSERT_STR_CONTAINS(set_pos.output, "[dry-run]");
    free(set_pos.output);

    snprintf(args, sizeof(args),
             "behavior interface canonicalize --dry-run --name \"Topic - Prevent Collision\" "
             "\"%s\"",
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t canonicalize = run_cli_capture(args);
    ASSERT_NOT_NULL(canonicalize.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, canonicalize.exit_code);
    ASSERT_STR_CONTAINS(canonicalize.output, "[dry-run]");
    free(canonicalize.output);

    snprintf(args, sizeof(args),
             "behavior interface add-comment --dry-run --name \"Topic - Prevent Collision\" "
             "--text \"Selector note\" --rect 1,2,3,4 \"%s\"",
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t add_comment = run_cli_capture(args);
    ASSERT_NOT_NULL(add_comment.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, add_comment.exit_code);
    ASSERT_STR_CONTAINS(add_comment.output, "[dry-run]");
    free(add_comment.output);

    snprintf(args, sizeof(args),
             "behavior interface add-comment --dry-run --name \"Topic - Prevent Collision\" "
             "--text \"--name\" --rect 1,2,3,4 \"%s\"",
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t selector_like_text = run_cli_capture(args);
    ASSERT_NOT_NULL(selector_like_text.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, selector_like_text.exit_code);
    ASSERT_STR_CONTAINS(selector_like_text.output, "[dry-run]");
    free(selector_like_text.output);

    snprintf(args, sizeof(args),
             "behavior interface add-point --dry-run --name \"Topic - Prevent Collision\" "
             "%u 1 2 \"%s\"",
             NMO_INTERFACE_EDIT_LINK_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t add_point = run_cli_capture(args);
    ASSERT_NOT_NULL(add_point.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, add_point.exit_code);
    ASSERT_STR_CONTAINS(add_point.output, "[dry-run]");
    free(add_point.output);

    snprintf(args, sizeof(args),
             "behavior interface move-param --dry-run --name \"Topic - Prevent Collision\" "
             "5 6 \"%s\" --param-index 0",
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t move_param = run_cli_capture(args);
    ASSERT_NOT_NULL(move_param.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, move_param.exit_code);
    ASSERT_STR_CONTAINS(move_param.output, "[dry-run]");
    free(move_param.output);

    snprintf(args, sizeof(args),
             "behavior interface set-graph-io --dry-run --name \"Topic - Prevent Collision\" "
             "--body %u --in-in 1,2 \"%s\"",
             NMO_INTERFACE_EDIT_SUB_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t graph_io = run_cli_capture(args);
    ASSERT_NOT_NULL(graph_io.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, graph_io.exit_code);
    ASSERT_STR_CONTAINS(graph_io.output, "[dry-run]");
    free(graph_io.output);
}

TEST(cli, behavior_interface_show_json_reports_format_root) {
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.interface", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *root_kind = yyjson_obj_get(data, "root_kind");
    ASSERT_TRUE(root_kind && yyjson_is_str(root_kind));
    ASSERT_STR_EQ("script", yyjson_get_str(root_kind));

    yyjson_val *is_graph = yyjson_obj_get(data, "sectioned_root_is_graph");
    ASSERT_TRUE(is_graph && yyjson_is_bool(is_graph));
    ASSERT_FALSE(yyjson_get_bool(is_graph));

    yyjson_val *script = get_object_field(data, "script");
    ASSERT_NOT_NULL(script);
    yyjson_val *color_defaulted = yyjson_obj_get(script, "color_defaulted");
    ASSERT_TRUE(color_defaulted && yyjson_is_bool(color_defaulted));
    ASSERT_FALSE(yyjson_get_bool(color_defaulted));

    yyjson_doc_free(doc);
}

TEST(cli, behavior_interface_show_brief_reports_root_kind) {
    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface show --brief %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Root kind");
    ASSERT_STR_CONTAINS(result.output, "script");
    free(result.output);
}

TEST(cli, behavior_interface_show_json_reports_sectioned_graph_root) {
    const char *fixture = "test_behavior_interface_sectioned_graph_fixture.cmo";
    remove(fixture);
    ASSERT_TRUE(create_sectioned_graph_interface_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             fixture);

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.interface", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *sectioned = yyjson_obj_get(data, "sectioned_layout");
    ASSERT_TRUE(sectioned && yyjson_is_bool(sectioned));
    ASSERT_TRUE(yyjson_get_bool(sectioned));

    yyjson_val *is_graph = yyjson_obj_get(data, "sectioned_root_is_graph");
    ASSERT_TRUE(is_graph && yyjson_is_bool(is_graph));
    ASSERT_TRUE(yyjson_get_bool(is_graph));

    yyjson_val *root_kind = yyjson_obj_get(data, "root_kind");
    ASSERT_TRUE(root_kind && yyjson_is_str(root_kind));
    ASSERT_STR_EQ("graph", yyjson_get_str(root_kind));

    yyjson_val *script = get_object_field(data, "script");
    ASSERT_NOT_NULL(script);
    yyjson_val *color_defaulted = yyjson_obj_get(script, "color_defaulted");
    ASSERT_TRUE(color_defaulted && yyjson_is_bool(color_defaulted));
    ASSERT_TRUE(yyjson_get_bool(color_defaulted));

    yyjson_doc_free(doc);
    remove(fixture);
}

TEST(cli, behavior_interface_canonicalize_json_saves_output) {
    const char *output = "test_behavior_interface_canonicalize_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior interface canonicalize %u \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE,
             output);

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.interface.canonicalize", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *target_id = yyjson_obj_get(data, "target_id");
    ASSERT_TRUE(target_id && yyjson_is_uint(target_id));
    ASSERT_EQ((uint64_t)NMO_INTERFACE_EDIT_TARGET_ID, yyjson_get_uint(target_id));

    yyjson_val *canonicalized = yyjson_obj_get(data, "canonicalized");
    ASSERT_TRUE(canonicalized && yyjson_is_bool(canonicalized));
    ASSERT_TRUE(yyjson_get_bool(canonicalized));

    yyjson_val *sectioned = yyjson_obj_get(data, "sectioned_layout");
    ASSERT_TRUE(sectioned && yyjson_is_bool(sectioned));
    ASSERT_FALSE(yyjson_get_bool(sectioned));

    yyjson_val *root_kind = yyjson_obj_get(data, "root_kind");
    ASSERT_TRUE(root_kind && yyjson_is_str(root_kind));
    ASSERT_STR_EQ("script", yyjson_get_str(root_kind));

    ASSERT_TRUE(file_exists(output));
    yyjson_doc_free(doc);

    write_semantic_probe_t probe;
    const nmo_behavior_state_t *behavior =
        probe_behavior_state(&probe, output, NMO_INTERFACE_EDIT_TARGET_ID);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(behavior->interface_data);
    ASSERT_EQ(NMO_INTERFACE_EDIT_TARGET_ID,
              behavior->interface_data->script.behavior_id);
    ASSERT_TRUE(behavior->interface_data->script.body.has_body);
    write_probe_close(&probe);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             output);
    yyjson_doc *show_doc = NULL;
    run_json_command(args, "behavior.interface", &show_doc);
    ASSERT_NOT_NULL(show_doc);
    yyjson_doc_free(show_doc);

    remove(output);
}

TEST(cli, behavior_interface_set_color_json_does_not_persist_sectioned_color) {
    const char *fixture = "test_behavior_interface_set_color_sectioned_fixture.cmo";
    const char *output = "test_behavior_interface_set_color_sectioned_output.cmo";
    remove(fixture);
    remove(output);
    ASSERT_TRUE(create_sectioned_graph_interface_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json behavior interface set-color %u FF00AA \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             fixture,
             output);

    yyjson_doc *doc = NULL;
    run_json_command(args, "behavior.interface.set-color", &doc);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);

    yyjson_val *persisted = yyjson_obj_get(data, "color_persisted");
    ASSERT_TRUE(persisted && yyjson_is_bool(persisted));
    ASSERT_FALSE(yyjson_get_bool(persisted));

    yyjson_val *warning = yyjson_obj_get(data, "warning");
    ASSERT_TRUE(warning && yyjson_is_str(warning));
    ASSERT_TRUE(file_exists(output));

    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             output);
    doc = NULL;
    run_json_command(args, "behavior.interface", &doc);
    ASSERT_NOT_NULL(doc);

    root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    yyjson_val *script = get_object_field(data, "script");
    ASSERT_NOT_NULL(script);

    yyjson_val *color = yyjson_obj_get(script, "color");
    ASSERT_TRUE(color && yyjson_is_uint(color));
    ASSERT_EQ(NMO_INTERFACE_DEFAULT_HEADER_COLOR, (unsigned)yyjson_get_uint(color));

    yyjson_val *color_defaulted = yyjson_obj_get(script, "color_defaulted");
    ASSERT_TRUE(color_defaulted && yyjson_is_bool(color_defaulted));
    ASSERT_TRUE(yyjson_get_bool(color_defaulted));

    yyjson_doc_free(doc);
    remove(fixture);
    remove(output);
}

TEST(cli, behavior_interface_fold_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_fold_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface fold --dry-run %u %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_unfold_saves_output) {
    const char *output = "test_behavior_interface_unfold_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface unfold %u %u \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SCRIPT_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE,
             output);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Saved to");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    write_semantic_probe_t probe;
    const nmo_behavior_state_t *behavior =
        probe_behavior_state(&probe, output, NMO_INTERFACE_EDIT_TARGET_ID);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(behavior->interface_data);
    ASSERT_EQ(0u, behavior->interface_data->script.flags & NMO_INTERFACE_FLAG_FOLDED);
    write_probe_close(&probe);

    remove(output);
}

TEST(cli, behavior_interface_set_color_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_color_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-color --dry-run %u FF00AA \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_set_color_saves_output) {
    const char *output = "test_behavior_interface_set_color_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-color %u FF00AA \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE,
             output);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Saved to");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    write_semantic_probe_t probe;
    const nmo_behavior_state_t *behavior =
        probe_behavior_state(&probe, output, NMO_INTERFACE_EDIT_TARGET_ID);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(behavior->interface_data);
    ASSERT_EQ(0x00FF00AAu, behavior->interface_data->script.color);
    write_probe_close(&probe);

    remove(output);
}

TEST(cli, behavior_interface_add_comment_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_add_comment_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface add-comment --dry-run %u --text \"Debt note\" --rect 1,2,3,4 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_add_comment_saves_comment_state) {
    const char *output = "test_behavior_interface_add_comment_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface add-comment %u --text \"Debt note\" --rect 1,2,3,4 \"%s\" -o \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE,
             output);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Saved to");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    write_semantic_probe_t probe;
    const nmo_behavior_state_t *behavior =
        probe_behavior_state(&probe, output, NMO_INTERFACE_EDIT_TARGET_ID);
    ASSERT_NOT_NULL(behavior);
    ASSERT_NOT_NULL(behavior->interface_data);
    const nmo_interface_body_t *body = &behavior->interface_data->script.body;
    ASSERT_TRUE(body->comment_count > 0u);
    const nmo_interface_comment_t *comment = &body->comments[body->comment_count - 1u];
    ASSERT_FLOAT_EQ(1.0f, comment->left, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, comment->top, 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, comment->right, 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, comment->bottom, 0.0001f);
    ASSERT_STR_EQ("Debt note", comment->text);
    write_probe_close(&probe);

    remove(output);
}

TEST(cli, behavior_interface_remove_comment_dry_run_does_not_write_output) {
    const char *fixture = "test_behavior_interface_remove_comment_fixture.cmo";
    const char *output = "test_behavior_interface_remove_comment_dry_output.cmo";
    remove(output);
    ASSERT_TRUE(create_interface_comment_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface remove-comment --dry-run %u 0 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_interface_set_comment_text_dry_run_does_not_write_output) {
    const char *fixture = "test_behavior_interface_set_comment_text_fixture.cmo";
    const char *output = "test_behavior_interface_set_comment_text_dry_output.cmo";
    remove(output);
    ASSERT_TRUE(create_interface_comment_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-comment-text --dry-run %u 0 --text \"Changed\" \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_interface_move_comment_dry_run_does_not_write_output) {
    const char *fixture = "test_behavior_interface_move_comment_fixture.cmo";
    const char *output = "test_behavior_interface_move_comment_dry_output.cmo";
    remove(output);
    ASSERT_TRUE(create_interface_comment_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface move-comment --dry-run %u 0 --rect 5,6,7,8 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_interface_set_comment_style_dry_run_does_not_write_output) {
    const char *fixture = "test_behavior_interface_set_comment_style_fixture.cmo";
    const char *output = "test_behavior_interface_set_comment_style_dry_output.cmo";
    remove(output);
    ASSERT_TRUE(create_interface_comment_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-comment-style --dry-run %u 0 --style 3 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(fixture);
    remove(output);
}

TEST(cli, behavior_interface_add_point_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_add_point_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface add-point --dry-run %u %u 1 2 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_LINK_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_clear_points_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_clear_points_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface clear-points --dry-run %u %u \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_LINK_WITH_POINT_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_remove_point_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_remove_point_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface remove-point --dry-run %u %u 0 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_LINK_WITH_POINT_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_move_point_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_move_point_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface move-point --dry-run %u %u 0 5 6 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_LINK_WITH_POINT_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_set_link_highlight_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_link_highlight_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-link-highlight --dry-run %u %u on \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_LINK_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_move_op_dry_run_reaches_operation_validation) {
    const char *output = "test_behavior_interface_move_op_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface move-op --dry-run %u %u 12 34 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_MISSING_OP_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Operation 999 not found");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_move_param_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_move_param_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface move-param --dry-run %u 5 6 \"%s\" --param-index 0",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_set_param_style_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_param_style_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-param-style --dry-run %u \"%s\" --param-index 0 --style 1",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_resize_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_resize_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface resize --dry-run %u %u 10 20 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SUB_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_set_expand_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_expand_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-expand --dry-run %u %u 10 20 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SUB_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_set_viewport_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_viewport_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-viewport --dry-run %u 1 2 3 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_translate_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_translate_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface translate --dry-run %u 1 2 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST(cli, behavior_interface_set_graph_io_dry_run_does_not_write_output) {
    const char *output = "test_behavior_interface_set_graph_io_dry_output.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "behavior interface set-graph-io --dry-run %u --body %u --in-in 1,2 --out-out 3 \"%s\"",
             NMO_INTERFACE_EDIT_TARGET_ID,
             NMO_INTERFACE_EDIT_SUB_BEHAVIOR_ID,
             NMO_INTERFACE_EDIT_FIXTURE);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "[dry-run]");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(output);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, behavior_graph_json);
    REGISTER_TEST(cli, behavior_graph_json_parity_metadata);
    REGISTER_TEST(cli, behavior_graph_json_truncation_counts);
    REGISTER_TEST(cli, behavior_graph_dot_labels_behavior_delay);
    REGISTER_TEST(cli, behavior_stats_json_distributions);
    REGISTER_TEST(cli, behavior_stats_json_survives_interface_parse_failures);
    REGISTER_TEST(cli, behavior_stats_json_marks_interface_unavailable_without_data);
    REGISTER_TEST(cli, behavior_show_json_survives_interface_parse_failures);
    REGISTER_TEST(cli, behavior_show_json_p2_parity);
    REGISTER_TEST(cli, behavior_show_text_formats_non_add_operations);
    REGISTER_TEST(cli, behavior_trace_json_p2_semantics);
    REGISTER_TEST(cli, behavior_trace_json_reports_depth_truncation);
    REGISTER_TEST(cli, behavior_trace_text_mentions_current_graph);
    REGISTER_TEST(cli, behavior_read_commands_accept_exact_name_selectors);
    REGISTER_TEST(cli, behavior_dump_help_describes_tree_overview_options);
    REGISTER_TEST(cli, behavior_dump_text_flows_show_owner_endpoints);
    REGISTER_TEST(cli, behavior_dump_all_rejects_flows);
    REGISTER_TEST(cli, behavior_dump_text_flows_show_empty_execution_section);
    REGISTER_TEST(cli, behavior_dump_text_values_show_decoded_values);
    REGISTER_TEST(cli, behavior_dump_json_flows_include_owner_names);
    REGISTER_TEST(cli, behavior_dump_json_values_include_decoded_values);
    REGISTER_TEST(cli, behavior_list_json_sanitizes_gameplay_names);
    REGISTER_TEST(cli, behavior_json_smoke_real_samples);
    REGISTER_TEST(cli, behavior_help_mentions_json_output);
    REGISTER_TEST(cli, behavior_graph_dot);
    REGISTER_TEST(cli, behavior_add_link_requires_output_unless_dry_run);
    REGISTER_TEST(cli, behavior_add_link_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_add_link_saves_output);
    REGISTER_TEST(cli, behavior_add_link_json_dry_run);
    REGISTER_TEST(cli, behavior_interface_set_pos_requires_output_unless_dry_run);
    REGISTER_TEST(cli, behavior_interface_set_pos_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_pos_saves_output);
    REGISTER_TEST(cli, behavior_interface_set_pos_json_dry_run);
    REGISTER_TEST(cli, behavior_interface_edit_commands_accept_exact_name_target_selector);
    REGISTER_TEST(cli, behavior_interface_show_json_reports_format_root);
    REGISTER_TEST(cli, behavior_interface_show_brief_reports_root_kind);
    REGISTER_TEST(cli, behavior_interface_show_json_reports_sectioned_graph_root);
    REGISTER_TEST(cli, behavior_interface_canonicalize_json_saves_output);
    REGISTER_TEST(cli, behavior_interface_set_color_json_does_not_persist_sectioned_color);
    REGISTER_TEST(cli, behavior_interface_fold_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_unfold_saves_output);
    REGISTER_TEST(cli, behavior_interface_set_color_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_color_saves_output);
    REGISTER_TEST(cli, behavior_interface_add_comment_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_add_comment_saves_comment_state);
    REGISTER_TEST(cli, behavior_interface_remove_comment_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_comment_text_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_move_comment_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_comment_style_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_add_point_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_clear_points_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_remove_point_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_move_point_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_link_highlight_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_move_op_dry_run_reaches_operation_validation);
    REGISTER_TEST(cli, behavior_interface_move_param_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_param_style_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_resize_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_expand_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_viewport_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_translate_dry_run_does_not_write_output);
    REGISTER_TEST(cli, behavior_interface_set_graph_io_dry_run_does_not_write_output);
TEST_MAIN_END()

