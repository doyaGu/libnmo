#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
#include "app/nmo_load.h"
#include "app/nmo_save.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_script_view.h"
#include "core/nmo_guid.h"
#include "format/nmo_interface_chunk.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_runtime.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
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

typedef struct placeholder_value {
    const char *token;
    const char *value;
} placeholder_value_t;

static void assert_public_lua_ok(const char *script)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    nmo_status_t status = NMO_OK;

    ASSERT_NOT_NULL(runtime);
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    status = nmo_lua_runtime_execute_string(runtime, script);
    if (status != NMO_OK && nmo_last_error_message() != NULL) {
        fprintf(stderr, "Public Lua check failed: %s\n", nmo_last_error_message());
        fprintf(stderr, "Script was:\n%s\n", script);
    }
    ASSERT_EQ(NMO_OK, status);
    nmo_lua_runtime_destroy(runtime);
}

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
    if (pipe == NULL) {
        result.exit_code = -1;
        return result;
    }

    size_t cap = 4096u;
    size_t len = 0u;
    char *buffer = (char *)malloc(cap);
    if (buffer == NULL) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    while (!feof(pipe)) {
        char chunk[1024];
        if (fgets(chunk, sizeof(chunk), pipe) == NULL) {
            break;
        }

        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1u > cap) {
            size_t new_cap = cap * 2u;
            while (new_cap < len + chunk_len + 1u) {
                new_cap *= 2u;
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

static int save_session_to_path(nmo_session_t *session, const char *path)
{
    nmo_save_options_t options = nmo_save_options_default();
    remove_if_exists(path);
    return nmo_save_file(session, path, &options) == NMO_OK;
}

static nmo_behavior_state_t *find_behavior_state(nmo_session_t *session,
                                                 nmo_object_id_t behavior_id,
                                                 nmo_object_t **out_object)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (out_object != NULL) {
        *out_object = NULL;
    }
    repo = nmo_session_get_repository(session);
    object = repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (object == NULL) {
        return NULL;
    }
    if (out_object != NULL) {
        *out_object = object;
    }
    return (nmo_behavior_state_t *)nmo_object_get_state(object);
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

static char *read_text_file(const char *path)
{
    FILE *fp = NULL;
    long size = 0;
    char *buffer = NULL;

    if (path == NULL) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
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
    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }

    if ((long)fread(buffer, 1, (size_t)size, fp) != size) {
        fclose(fp);
        free(buffer);
        return NULL;
    }

    fclose(fp);
    buffer[size] = '\0';
    return buffer;
}

static char *replace_token(const char *source, const char *token, const char *value)
{
    const char *cursor = NULL;
    const char *match = NULL;
    size_t token_len = 0u;
    size_t value_len = 0u;
    size_t result_len = 0u;
    char *result = NULL;
    char *out = NULL;

    if (source == NULL || token == NULL || value == NULL) {
        return NULL;
    }

    token_len = strlen(token);
    value_len = strlen(value);
    cursor = source;

    while ((match = strstr(cursor, token)) != NULL) {
        result_len += (size_t)(match - cursor);
        result_len += value_len;
        cursor = match + token_len;
    }
    result_len += strlen(cursor);

    result = (char *)malloc(result_len + 1u);
    if (result == NULL) {
        return NULL;
    }

    out = result;
    cursor = source;
    while ((match = strstr(cursor, token)) != NULL) {
        size_t prefix_len = (size_t)(match - cursor);
        memcpy(out, cursor, prefix_len);
        out += prefix_len;
        memcpy(out, value, value_len);
        out += value_len;
        cursor = match + token_len;
    }

    strcpy(out, cursor);
    return result;
}

static char *lua_escape_string(const char *text)
{
    size_t i = 0u;
    size_t len = 0u;
    char *escaped = NULL;
    char *out = NULL;

    if (text == NULL) {
        return NULL;
    }

    for (i = 0u; text[i] != '\0'; ++i) {
        switch (text[i]) {
        case '\\':
        case '"':
        case '\n':
        case '\r':
        case '\t':
            len += 2u;
            break;
        default:
            len += 1u;
            break;
        }
    }

    escaped = (char *)malloc(len + 1u);
    if (escaped == NULL) {
        return NULL;
    }

    out = escaped;
    for (i = 0u; text[i] != '\0'; ++i) {
        switch (text[i]) {
        case '\\':
            *out++ = '\\';
            *out++ = '\\';
            break;
        case '"':
            *out++ = '\\';
            *out++ = '"';
            break;
        case '\n':
            *out++ = '\\';
            *out++ = 'n';
            break;
        case '\r':
            *out++ = '\\';
            *out++ = 'r';
            break;
        case '\t':
            *out++ = '\\';
            *out++ = 't';
            break;
        default:
            *out++ = text[i];
            break;
        }
    }

    *out = '\0';
    return escaped;
}

static char *materialize_fixture_script(const char *fixture_path,
                                        const placeholder_value_t *pairs,
                                        size_t pair_count)
{
    char *script = read_text_file(fixture_path);
    size_t i = 0u;

    if (script == NULL) {
        return NULL;
    }

    for (i = 0u; i < pair_count; ++i) {
        char *next = replace_token(script, pairs[i].token, pairs[i].value);
        free(script);
        if (next == NULL) {
            return NULL;
        }
        script = next;
    }

    return script;
}

static void assert_validate_ok(const char *path)
{
    char args[2048];
    cli_run_result_t result = {0};

    snprintf(args, sizeof(args), "validate all \"%s\"", path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Result: VALID");
    free(result.output);
}

static const char *get_string_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return yyjson_get_str(val);
}

static yyjson_val *get_object_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val != NULL && yyjson_is_obj(val)) ? val : NULL;
}

static yyjson_val *get_array_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return (val != NULL && yyjson_is_arr(val)) ? val : NULL;
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

static size_t count_nodes_with_kind(yyjson_val *nodes, const char *kind)
{
    size_t index = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;
    size_t count = 0u;

    if (nodes == NULL || kind == NULL) {
        return 0u;
    }

    yyjson_arr_foreach(nodes, index, max, item) {
        const char *node_kind = get_string_field(item, "kind");
        if (node_kind != NULL && strcmp(node_kind, kind) == 0) {
            count += 1u;
        }
    }

    return count;
}

static void load_first_interface_script_ids(const char *path,
                                            uint32_t *out_root_id,
                                            uint32_t *out_first_sub_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    size_t script_count = 0u;

    ASSERT_NOT_NULL(out_root_id);
    *out_root_id = 0u;
    if (out_first_sub_id != NULL) {
        *out_first_sub_id = 0u;
    }

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_load_file(session, path, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));
    ASSERT_EQ(NMO_OK, nmo_script_view_count(session, &script_count));
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    for (size_t i = 0u; i < script_count; ++i) {
        nmo_script_view_t script = {0};
        nmo_object_t *root_object = NULL;
        nmo_behavior_state_t *root_state = NULL;

        ASSERT_EQ(NMO_OK, nmo_script_view_at(session, i, &script));
        root_object = nmo_object_repository_find_by_id(repo, script.script_id);
        ASSERT_NOT_NULL(root_object);
        root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_object);
        ASSERT_NOT_NULL(root_state);

        if (root_state->interface_data == NULL) {
            continue;
        }

        *out_root_id = script.script_id;
        if (out_first_sub_id != NULL &&
            root_state->interface_data->sub_count > 0u &&
            root_state->interface_data->subs != NULL) {
            *out_first_sub_id = root_state->interface_data->subs[0].behavior_id;
        }
        break;
    }

    ASSERT_TRUE(*out_root_id != 0u);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

static void execute_public_lua_fixture(const char *fixture_path,
                                       const placeholder_value_t *pairs,
                                       size_t pair_count)
{
    nmo_lua_runtime_t *runtime = NULL;
    char *script = materialize_fixture_script(fixture_path, pairs, pair_count);
    nmo_status_t status = NMO_OK;

    ASSERT_NOT_NULL(script);
    runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    status = nmo_lua_runtime_execute_string(runtime, script);
    if (status != NMO_OK && nmo_last_error_message() != NULL) {
        fprintf(stderr, "Lua fixture failed: %s\n", nmo_last_error_message());
        fprintf(stderr, "Lua fixture script:\n%s\n", script);
    }
    ASSERT_EQ(NMO_OK, status);

    free(script);
    nmo_lua_runtime_destroy(runtime);
}

static void create_interface_graph_io_fixture(const char *input_path,
                                              const char *output_path,
                                              uint32_t owner_behavior_id,
                                              int32_t input_index)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_t *obj = NULL;
    nmo_behavior_state_t *state = NULL;
    nmo_interface_behavior_t *subs = NULL;
    nmo_interface_behavior_t *target_sub = NULL;
    const nmo_interface_behavior_t *template_sub = NULL;
    int32_t *inputs = NULL;
    nmo_arena_t *arena = NULL;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = NMO_TEST_DATA_DIR });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_session_load_file(session, input_path, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_session_ensure_behavior_acceleration(session));

    state = find_behavior_state(session, 253u, &obj);
    ASSERT_NOT_NULL(state);
    ASSERT_NOT_NULL(obj);
    ASSERT_NOT_NULL(state->interface_data);

    for (size_t i = 0; i < state->interface_data->sub_count; ++i) {
        if (state->interface_data->subs[i].body.has_graph_io &&
            state->interface_data->subs[i].body.graph_io) {
            template_sub = &state->interface_data->subs[i];
            break;
        }
    }
    ASSERT_NOT_NULL(template_sub);

    arena = nmo_object_get_storage_arena(obj);
    ASSERT_NOT_NULL(arena);
    subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
        arena,
        (state->interface_data->sub_count + 1u) * sizeof(*subs),
        alignof(nmo_interface_behavior_t));
    ASSERT_NOT_NULL(subs);

    memcpy(subs,
           state->interface_data->subs,
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
    ASSERT_NOT_NULL(inputs);
    memcpy(inputs,
           target_sub->body.graph_io->outward_inputs,
           target_sub->body.graph_io->outward_input_count * sizeof(*inputs));
    inputs[target_sub->body.graph_io->outward_input_count] = input_index;
    target_sub->body.graph_io->outward_inputs = inputs;
    target_sub->body.graph_io->outward_input_count += 1u;

    ASSERT_TRUE(save_session_to_path(session, output_path));
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

static void create_public_validation_input(const char *seed_path,
                                           const char *output_path,
                                           uint32_t *out_io_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_object_t *owner_object = NULL;
    nmo_behavior_state_t *owner_state = NULL;
    nmo_object_id_t io_id = 0u;
    uint32_t io_index = 0u;

    ASSERT_NOT_NULL(seed_path);
    ASSERT_NOT_NULL(output_path);
    ASSERT_NOT_NULL(out_io_id);
    *out_io_id = 0u;
    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(
        ctx, NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"));
    ASSERT_NOT_NULL(session);

    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(ctx, session, "public lua validation input", &tx));
    ASSERT_NOT_NULL(tx);
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_io(tx, 229u, NMO_SCRIPT_EDIT_IO_INPUT, "Lua Validation Io", &io_id));
    ASSERT_TRUE(io_id != 0u);
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(tx));
    ASSERT_TRUE(save_session_to_path(session, seed_path));

    owner_state = find_behavior_state(session, 229u, &owner_object);
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(owner_object);
    ASSERT_TRUE(owner_state->inputs.count > 0u);
    {
        nmo_object_id_t *ids = (nmo_object_id_t *)owner_state->inputs.data;
        for (size_t i = 0u; i < owner_state->inputs.count; ++i) {
            if (ids[i] == io_id) {
                io_index = (uint32_t)i;
                break;
            }
        }
    }
    ASSERT_TRUE(io_index != 0u);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    create_interface_graph_io_fixture(seed_path, output_path, 229u, (int32_t)io_index);
    *out_io_id = io_id;
}

TEST(lua_public_workflow, public_lua_workflow_applies_and_saves_valid_output)
{
    char fixture_path[1024];
    char float_guid[32];
    char output_path[1024];
    char marker_path[1024];
    char *escaped_input = NULL;
    char *escaped_output = NULL;
    char *escaped_marker = NULL;
    const char *input_path = NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo");
    uint32_t behavior_root_id = 0u;
    uint32_t interface_root_id = 0u;
    placeholder_value_t pairs[3];
    char args[2048];
    cli_run_result_t result = {0};
    yyjson_doc *doc = NULL;
    yyjson_val *root = NULL;
    yyjson_val *data = NULL;
    yyjson_val *inputs = NULL;
    yyjson_val *outputs = NULL;
    yyjson_val *iface_parse = NULL;
    yyjson_val *nodes = NULL;
    char *escaped_compare_input = NULL;
    char *escaped_compare_output = NULL;
    char lua_check[3072];

    ASSERT_TRUE(build_repo_fixture_path("test_lua_public_workflow_out.cmo",
                                        output_path,
                                        sizeof(output_path)));
    ASSERT_TRUE(build_repo_fixture_path("test_lua_public_workflow_marker.txt",
                                        marker_path,
                                        sizeof(marker_path)));
    remove_if_exists(output_path);
    remove_if_exists(marker_path);
    ASSERT_TRUE(build_repo_fixture_path("tests/fixtures/lua/public_behavior_workflow.lua",
                                        fixture_path,
                                        sizeof(fixture_path)));
    ASSERT_TRUE(nmo_guid_format(CKPGUID_FLOAT, float_guid, sizeof(float_guid)) > 0);
    escaped_input = lua_escape_string(input_path);
    escaped_output = lua_escape_string(output_path);
    escaped_marker = lua_escape_string(marker_path);
    ASSERT_NOT_NULL(escaped_input);
    ASSERT_NOT_NULL(escaped_output);
    ASSERT_NOT_NULL(escaped_marker);

    pairs[0] = (placeholder_value_t){ "__INPUT_PATH__", escaped_input };
    pairs[1] = (placeholder_value_t){ "__OUTPUT_PATH__", escaped_output };
    pairs[2] = (placeholder_value_t){ "__FLOAT_GUID__", float_guid };
    {
        placeholder_value_t extended_pairs[4];
        extended_pairs[0] = pairs[0];
        extended_pairs[1] = pairs[1];
        extended_pairs[2] = pairs[2];
        extended_pairs[3] = (placeholder_value_t){ "__MARKER_PATH__", escaped_marker };
        execute_public_lua_fixture(fixture_path, extended_pairs, 4u);
    }
    free(escaped_input);
    free(escaped_output);
    free(escaped_marker);

    ASSERT_TRUE(file_exists(marker_path));
    if (!file_exists(output_path)) {
        char *marker_contents = read_text_file(marker_path);
        fprintf(stderr, "Expected output path: %s\n", output_path);
        fprintf(stderr, "Marker path: %s\n", marker_path);
        fprintf(stderr, "Marker contents: %s\n",
                marker_contents != NULL ? marker_contents : "(null)");
        free(marker_contents);
    }
    ASSERT_TRUE(file_exists(output_path));
    assert_validate_ok(output_path);
    load_first_interface_script_ids(input_path, &behavior_root_id, NULL);
    ASSERT_TRUE(behavior_root_id != 0u);
    interface_root_id = behavior_root_id;

    snprintf(args, sizeof(args),
             "-f json behavior show %u \"%s\"",
             behavior_root_id,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    if (result.exit_code != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "behavior.interface.show failed: %s\n", result.output);
    }
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
    ASSERT_NOT_NULL(find_array_object_by_name(inputs, "Lua Public In"));
    ASSERT_NOT_NULL(find_array_object_by_name(outputs, "Lua Public Out"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json behavior interface show %u \"%s\"",
             interface_root_id,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    if (result.exit_code != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "script.graph failed: %s\n", result.output);
    }
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    iface_parse = get_object_field(data, "interface_parse");
    ASSERT_NOT_NULL(iface_parse);
    ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(iface_parse, "available")));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args),
             "-f json script graph %u \"%s\"",
             interface_root_id,
             output_path);
    result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    doc = yyjson_read(result.output, strlen(result.output), 0);
    if (doc == NULL) {
        fprintf(stderr, "behavior.interface.show output: %s\n", result.output);
    }
    free(result.output);
    ASSERT_NOT_NULL(doc);
    data = get_object_field(yyjson_doc_get_root(doc), "data");
    ASSERT_NOT_NULL(data);
    nodes = get_array_field(data, "nodes");
    ASSERT_NOT_NULL(nodes);
    ASSERT_NOT_NULL(find_array_object_by_name(nodes, "Lua Public Node"));
    ASSERT_TRUE(count_nodes_with_kind(nodes, "operation") > 0u);
    yyjson_doc_free(doc);

    escaped_compare_input = lua_escape_string(input_path);
    escaped_compare_output = lua_escape_string(output_path);
    ASSERT_NOT_NULL(escaped_compare_input);
    ASSERT_NOT_NULL(escaped_compare_output);
    snprintf(
        lua_check,
        sizeof(lua_check),
        "local session = require('nmo.session')\n"
        "local behavior = require('nmo.behavior')\n"
        "local app = require('nmo.app')\n"
        "local before = session.load_file(session.create_context(), \"%s\")\n"
        "local after = session.load_file(session.create_context(), \"%s\")\n"
        "local diff = app.diff(before, after)\n"
        "assert(type(diff.changed) == 'table')\n"
        "assert(type(diff.added) == 'table')\n"
        "assert(#diff.changed + #diff.renamed + #diff.added + #diff.removed > 0)\n"
        "local tree = behavior.script_tree(after, %u)\n"
        "assert(type(tree) == 'table')\n"
        "assert(#tree > 0)\n"
        "local found = false\n"
        "for i = 1, #tree do\n"
        "  if tree[i].name == 'Lua Public Node' then found = true break end\n"
        "end\n"
        "assert(found)\n",
        escaped_compare_input,
        escaped_compare_output,
        behavior_root_id);
    assert_public_lua_ok(lua_check);
    free(escaped_compare_input);
    free(escaped_compare_output);

    remove_if_exists(output_path);
    remove_if_exists(marker_path);
}

TEST(lua_public_workflow, public_lua_rollback_leaves_no_saved_output)
{
    char fixture_path[1024];
    char output_path[1024];
    char *escaped_input = NULL;
    const char *input_path = NMO_TEST_DATA_FILE("Ballance/base.cmo");
    placeholder_value_t pairs[1];

    ASSERT_TRUE(build_repo_fixture_path("test_lua_public_rollback_out.cmo",
                                        output_path,
                                        sizeof(output_path)));
    remove_if_exists(output_path);
    ASSERT_TRUE(build_repo_fixture_path("tests/fixtures/lua/public_behavior_rollback.lua",
                                        fixture_path,
                                        sizeof(fixture_path)));
    escaped_input = lua_escape_string(input_path);
    ASSERT_NOT_NULL(escaped_input);

    pairs[0] = (placeholder_value_t){ "__INPUT_PATH__", escaped_input };
    execute_public_lua_fixture(fixture_path, pairs, 1u);
    free(escaped_input);
    ASSERT_FALSE(file_exists(output_path));
}

TEST(lua_public_workflow, public_lua_validation_failure_leaves_no_saved_output)
{
    char fixture_path[1024];
    char output_path[1024];
    char prepared_seed_path[1024];
    char prepared_input_path[1024];
    char *escaped_input = NULL;
    char owner_id_text[32];
    char doomed_id_text[32];
    uint32_t owner_id = 253u;
    uint32_t doomed_id = 0u;
    placeholder_value_t pairs[3];

    ASSERT_TRUE(build_repo_fixture_path("test_lua_public_validation_failure_out.cmo",
                                        output_path,
                                        sizeof(output_path)));
    ASSERT_TRUE(build_repo_fixture_path("test_lua_public_validation_failure_seed.cmo",
                                        prepared_seed_path,
                                        sizeof(prepared_seed_path)));
    ASSERT_TRUE(build_repo_fixture_path("test_lua_public_validation_failure_input.cmo",
                                        prepared_input_path,
                                        sizeof(prepared_input_path)));
    remove_if_exists(output_path);
    remove_if_exists(prepared_seed_path);
    remove_if_exists(prepared_input_path);
    ASSERT_TRUE(build_repo_fixture_path(
        "tests/fixtures/lua/public_behavior_validation_failure.lua",
        fixture_path,
        sizeof(fixture_path)));
    create_public_validation_input(prepared_seed_path, prepared_input_path, &doomed_id);
    ASSERT_TRUE(doomed_id != 0u);
    escaped_input = lua_escape_string(prepared_input_path);
    ASSERT_NOT_NULL(escaped_input);
    snprintf(owner_id_text, sizeof(owner_id_text), "%u", owner_id);
    snprintf(doomed_id_text, sizeof(doomed_id_text), "%u", doomed_id);

    pairs[0] = (placeholder_value_t){ "__INPUT_PATH__", escaped_input };
    pairs[1] = (placeholder_value_t){ "__OWNER_ID__", owner_id_text };
    pairs[2] = (placeholder_value_t){ "__DOOMED_ID__", doomed_id_text };
    execute_public_lua_fixture(fixture_path, pairs, 3u);
    free(escaped_input);
    ASSERT_FALSE(file_exists(output_path));
    remove_if_exists(prepared_seed_path);
    remove_if_exists(prepared_input_path);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_public_workflow,
                  public_lua_workflow_applies_and_saves_valid_output);
    REGISTER_TEST(lua_public_workflow,
                  public_lua_rollback_leaves_no_saved_output);
    REGISTER_TEST(lua_public_workflow,
                  public_lua_validation_failure_leaves_no_saved_output);
TEST_MAIN_END()
