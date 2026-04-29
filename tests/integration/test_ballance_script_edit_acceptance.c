/**
 * @file test_ballance_script_edit_acceptance.c
 * @brief Ballance fixture acceptance checks for unified script edits.
 */

#include "test_framework.h"

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
    cli_run_result_t result = {NULL, -1};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (pipe == NULL) {
        return result;
    }

    size_t cap = 4096u;
    size_t len = 0u;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe) != NULL) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1u > cap) {
            size_t next_cap = cap * 2u;
            while (next_cap < len + chunk_len + 1u) {
                next_cap *= 2u;
            }
            char *next = (char *)realloc(buf, next_cap);
            if (next == NULL) {
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

static void make_dir(const char *path)
{
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
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

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return 0;
    }
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

static yyjson_val *get_object_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_obj(val) ? val : NULL;
}

static yyjson_val *get_array_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_arr(val) ? val : NULL;
}

static const char *get_string_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return yyjson_get_str(val);
}

static bool get_bool_field(yyjson_val *obj, const char *key)
{
    yyjson_val *val = yyjson_obj_get(obj, key);
    return val && yyjson_is_bool(val) && yyjson_get_bool(val);
}

static void run_json_command(
    const char *args,
    const char *expected_command,
    yyjson_doc **out_doc)
{
    ASSERT_NOT_NULL(out_doc);
    *out_doc = NULL;
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(0, result.exit_code);

    yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
    if (doc == NULL) {
        fprintf(stderr, "\nExpected JSON output, got:\n%s\n", result.output);
    }
    free(result.output);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);
    ASSERT_STR_EQ(expected_command, get_string_field(root, "command"));
    ASSERT_NOT_NULL(get_object_field(root, "data"));
    *out_doc = doc;
}

TEST(ballance_acceptance, debug_probe_2d_text_dry_run)
{
    char args[1024];
    snprintf(args, sizeof(args),
             "-f json debug probe 2d-text --behavior 237 "
             "--text \"loading trace\" --dry-run \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));

    yyjson_doc *doc = NULL;
    run_json_command(args, "debug.probe", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    ASSERT_NOT_NULL(get_array_field(data, "operations"));
    ASSERT_NOT_NULL(get_array_field(data, "created_objects"));
    yyjson_doc_free(doc);
}

TEST(ballance_acceptance, patch_replay_dry_run)
{
    make_dir("test_ballance_acceptance_tmp");
    const char *patch = "test_ballance_acceptance_tmp/patch_replay.json";
    const char *output = "test_ballance_acceptance_tmp/patch_replay.cmo";
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_node\",\n"
             "      \"behavior_id\": 237,\n"
             "      \"guid\": \"055B29FE-662D5CA0\",\n"
             "      \"name\": \"Acceptance 2D Text Probe\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);

    remove(patch);
    remove(output);
    ASSERT_TRUE(write_text_file(patch, json));

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run", patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_STR_EQ("add_node", get_string_field(op, "op"));
    ASSERT_NOT_NULL(get_array_field(data, "created_objects"));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);

    remove(patch);
}

TEST(ballance_acceptance, validate_base)
{
    char args[1024];
    snprintf(args, sizeof(args), "validate all \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(0, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Result: VALID");
    free(result.output);
}

TEST(ballance_acceptance, accepted_patch_save_load_validates)
{
    make_dir("test_ballance_acceptance_tmp");
    const char *patch = "test_ballance_acceptance_tmp/accepted_patch.json";
    const char *output = "test_ballance_acceptance_tmp/accepted_patch.cmo";
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_node\",\n"
             "      \"behavior_id\": 237,\n"
             "      \"guid\": \"055B29FE-662D5CA0\",\n"
             "      \"name\": \"Acceptance Saved 2D Text\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);

    remove(patch);
    remove(output);
    ASSERT_TRUE(write_text_file(patch, json));

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\"", patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));
    ASSERT_NOT_NULL(get_array_field(data, "operations"));
    ASSERT_NOT_NULL(get_array_field(data, "created_objects"));
    ASSERT_TRUE(file_exists(output));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "validate all \"%s\"", output);
    cli_run_result_t validate = run_cli_capture(args);
    ASSERT_NOT_NULL(validate.output);
    ASSERT_EQ(0, validate.exit_code);
    ASSERT_STR_CONTAINS(validate.output, "Result: VALID");
    free(validate.output);

    remove(patch);
    remove(output);
}

TEST(ballance_acceptance, accepted_message_probe_save_load_validates)
{
    make_dir("test_ballance_acceptance_tmp");
    const char *output = "test_ballance_acceptance_tmp/message_probe.cmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "-f json debug probe message-logger --behavior 2172 "
             "--text \"message trace\" \"%s\" -o \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);
    yyjson_doc *doc = NULL;
    run_json_command(args, "debug.probe", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(file_exists(output));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(5u, (uint32_t)yyjson_arr_size(operations));
    ASSERT_NOT_NULL(get_array_field(data, "created_objects"));
    yyjson_val *diagnostics =
        get_object_field(data, "probe_selector_diagnostics");
    ASSERT_NOT_NULL(diagnostics);
    ASSERT_STR_EQ("auto", get_string_field(diagnostics, "mode"));
    ASSERT_STR_EQ("selected", get_string_field(diagnostics, "status"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "validate all \"%s\"", output);
    cli_run_result_t validate = run_cli_capture(args);
    ASSERT_NOT_NULL(validate.output);
    ASSERT_EQ(0, validate.exit_code);
    ASSERT_STR_CONTAINS(validate.output, "Result: VALID");
    free(validate.output);

    remove(output);
}

TEST(ballance_acceptance, accepted_data_probe_save_load_validates)
{
    make_dir("test_ballance_acceptance_tmp");
    const char *output = "test_ballance_acceptance_tmp/data_probe.cmo";
    remove(output);

    char args[1536];
    snprintf(args, sizeof(args),
             "-f json debug probe data-cell-logger --behavior 3880 "
             "--dataarray 6067 --row 0 --col 1 "
             "--write-node 3871 --remove-link 3874 "
             "\"%s\" -o \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);
    yyjson_doc *doc = NULL;
    run_json_command(args, "debug.probe", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(file_exists(output));
    ASSERT_STR_EQ("data_cell_write", get_string_field(data, "probe_selector"));
    yyjson_val *diagnostics =
        get_object_field(data, "probe_selector_diagnostics");
    ASSERT_NOT_NULL(diagnostics);
    ASSERT_STR_EQ("explicit_node", get_string_field(diagnostics, "mode"));
    ASSERT_STR_EQ("selected", get_string_field(diagnostics, "status"));
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(5u, (uint32_t)yyjson_arr_size(operations));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "validate all \"%s\"", output);
    cli_run_result_t validate = run_cli_capture(args);
    ASSERT_NOT_NULL(validate.output);
    ASSERT_EQ(0, validate.exit_code);
    ASSERT_STR_CONTAINS(validate.output, "Result: VALID");
    free(validate.output);

    remove(output);
}

TEST(ballance_acceptance, accepted_manager_entry_save_load_validates)
{
    make_dir("test_ballance_acceptance_tmp");
    const char *output = "test_ballance_acceptance_tmp/manager_entry.cmo";
    remove(output);

    char args[1536];
    snprintf(args, sizeof(args),
             "-f json script node add --parent 237 "
             "--bb-guid A20E8D5B-DF002150 "
             "--name AcceptanceSendMessage "
             "--manager-entry create-missing "
             "--manager-entry-schema message "
             "--manager-entry-guid {466A0FAC-00000000} "
             "--manager-entry-key AcceptanceMessage "
             "\"%s\" -o \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);
    yyjson_doc *doc = NULL;
    run_json_command(args, "script.node.add", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(file_exists(output));
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    yyjson_val *manager_diff = get_object_field(diff, "manager_entry_diff");
    ASSERT_NOT_NULL(manager_diff);
    yyjson_val *changed = get_array_field(manager_diff, "changed");
    ASSERT_NOT_NULL(changed);
    ASSERT_TRUE(yyjson_arr_size(changed) > 0u);
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "validate all \"%s\"", output);
    cli_run_result_t validate = run_cli_capture(args);
    ASSERT_NOT_NULL(validate.output);
    ASSERT_EQ(0, validate.exit_code);
    ASSERT_STR_CONTAINS(validate.output, "Result: VALID");
    free(validate.output);

    remove(output);
}

TEST(ballance_acceptance, accepted_attribute_manager_entry_save_load_validates)
{
    make_dir("test_ballance_acceptance_tmp");
    const char *patch = "test_ballance_acceptance_tmp/attribute_manager_entry.json";
    const char *output = "test_ballance_acceptance_tmp/attribute_manager_entry.cmo";
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"set_parameter_value\",\n"
             "      \"parameter_id\": 2606,\n"
             "      \"value\": \"ignored\",\n"
             "      \"manager_entry\": {\n"
             "        \"policy\": \"create_missing\",\n"
             "        \"schema\": \"attribute\",\n"
             "        \"manager_guid\": \"{3D242466-00000000}\",\n"
             "        \"key\": \"AcceptanceAttribute\",\n"
             "        \"create\": {\n"
             "          \"attribute_type_guid\": \"{5A54D2BD-44E28357}\",\n"
             "          \"category\": \"Acceptance\",\n"
             "          \"compatible_class_id\": 19,\n"
             "          \"flags\": 5\n"
             "        }\n"
             "      }\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output);

    remove(patch);
    remove(output);
    ASSERT_TRUE(write_text_file(patch, json));

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch apply \"%s\"", patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.apply", &doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_FALSE(get_bool_field(data, "dry_run"));
    ASSERT_TRUE(file_exists(output));
    ASSERT_NOT_NULL(get_array_field(data, "operations"));
    ASSERT_NOT_NULL(get_object_field(data, "manifest"));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "validate all \"%s\"", output);
    cli_run_result_t validate = run_cli_capture(args);
    ASSERT_NOT_NULL(validate.output);
    ASSERT_EQ(0, validate.exit_code);
    ASSERT_STR_CONTAINS(validate.output, "Result: VALID");
    free(validate.output);

    remove(patch);
    remove(output);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(ballance_acceptance, debug_probe_2d_text_dry_run);
REGISTER_TEST(ballance_acceptance, patch_replay_dry_run);
REGISTER_TEST(ballance_acceptance, validate_base);
REGISTER_TEST(ballance_acceptance, accepted_patch_save_load_validates);
REGISTER_TEST(ballance_acceptance, accepted_message_probe_save_load_validates);
REGISTER_TEST(ballance_acceptance, accepted_data_probe_save_load_validates);
REGISTER_TEST(ballance_acceptance, accepted_manager_entry_save_load_validates);
REGISTER_TEST(ballance_acceptance, accepted_attribute_manager_entry_save_load_validates);
TEST_MAIN_END()
