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

static void write_raw_v2_patch_operation(
    const char *path,
    const char *output_path,
    const char *operation_json)
{
    char json[4096];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "%s\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path,
             operation_json);
    ASSERT_TRUE(write_text_file(path, json));
}

static void assert_patch_apply_fails_with(
    const char *patch_path,
    const char *expected_text)
{
    char args[1024];
    snprintf(args, sizeof(args), "patch apply \"%s\"", patch_path);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, expected_text);
    free(result.output);
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

static void write_legacy_v1_patch(const char *path, const char *output_path) {
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

static void write_add_behavior_link_patch_v2(const char *path,
                                             const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_behavior_link\",\n"
             "      \"parent_id\": 6,\n"
             "      \"from_io_id\": 5,\n"
             "      \"to_io_id\": 2,\n"
             "      \"activation_delay\": 3\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_add_parameter_patch_v2(const char *path,
                                         const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_parameter\",\n"
             "      \"owner_id\": 6,\n"
             "      \"kind\": \"local\",\n"
             "      \"type_guid\": \"5A5716FD-44E276D7\",\n"
             "      \"name\": \"Patch V2 Int\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_add_operation_patch_v2(const char *path,
                                         const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_operation\",\n"
             "      \"parent_id\": 6,\n"
             "      \"operation_guid\": \"33CC6B49-3589282B\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_add_remove_operation_patch_v2(const char *path,
                                                const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_operation\",\n"
             "      \"parent_id\": 6,\n"
             "      \"operation_guid\": \"33CC6B49-3589282B\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"remove_operation\",\n"
             "      \"operation_id\": 16\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_rewire_operation_patch_v2(const char *path,
                                            const char *output_path) {
    char json[3072];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_parameter\",\n"
             "      \"owner_id\": 6,\n"
             "      \"kind\": \"local\",\n"
             "      \"type_guid\": \"5A5716FD-44E276D7\",\n"
             "      \"name\": \"Patch V2 Op In\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"add_operation\",\n"
             "      \"parent_id\": 6,\n"
             "      \"operation_guid\": \"33CC6B49-3589282B\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"rewire_operation\",\n"
             "      \"operation_id\": 17,\n"
             "      \"in1_id\": 16\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_disconnect_parameter_patch_v2(const char *path,
                                                const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"disconnect_parameter\",\n"
             "      \"target_id\": 8\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_remove_parameter_patch_v2(const char *path,
                                            const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"remove_parameter\",\n"
             "      \"parameter_id\": 18,\n"
             "      \"detach\": false\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_set_parameter_value_patch_v2(const char *path,
                                               const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"set_parameter_value\",\n"
             "      \"parameter_id\": 18,\n"
             "      \"value\": \"3\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_set_parameter_value_handle_patch_v2(
    const char *path,
    const char *output_path) {
    char json[3072];
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
             "      \"name\": \"Patch 2D Text Logger\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"set_parameter_value\",\n"
             "      \"parameter_operation\": 1,\n"
             "      \"parameter_handle\": \"input_param:Text\",\n"
             "      \"value\": \"Patch trace\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_add_operation_handle_patch_v2(const char *path,
                                                const char *output_path) {
    char json[4096];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_parameter\",\n"
             "      \"owner_id\": 6,\n"
             "      \"kind\": \"local\",\n"
             "      \"type_guid\": \"47884C3F-432C2C20\",\n"
             "      \"name\": \"Patch Op A\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"add_parameter\",\n"
             "      \"owner_id\": 6,\n"
             "      \"kind\": \"local\",\n"
             "      \"type_guid\": \"47884C3F-432C2C20\",\n"
             "      \"name\": \"Patch Op B\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"add_parameter\",\n"
             "      \"owner_id\": 6,\n"
             "      \"kind\": \"local\",\n"
             "      \"type_guid\": \"47884C3F-432C2C20\",\n"
             "      \"name\": \"Patch Op Out\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"add_operation\",\n"
             "      \"parent_id\": 6,\n"
             "      \"operation_guid\": \"33CC6B49-3589282B\",\n"
             "      \"in1_operation\": 1,\n"
             "      \"in1_handle\": \"parameter\",\n"
             "      \"in2_operation\": 2,\n"
             "      \"in2_handle\": \"parameter\",\n"
             "      \"out_operation\": 3,\n"
             "      \"out_handle\": \"parameter\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_set_parameter_bytes_patch_v2(const char *path,
                                               const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"set_parameter_bytes\",\n"
             "      \"parameter_id\": 64,\n"
             "      \"hex\": \"2A000000\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_set_parameter_bytes_handle_patch_v2(
    const char *path,
    const char *output_path) {
    char json[3072];
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
             "      \"name\": \"Patch 2D Text Raw Logger\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"set_parameter_bytes\",\n"
             "      \"parameter_operation\": 1,\n"
             "      \"parameter_handle\": \"input_param:Text\",\n"
             "      \"hex\": \"52617720747261636500\",\n"
             "      \"resize\": true\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_set_data_cell_patch_v2(const char *path,
                                         const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"set_data_cell\",\n"
             "      \"dataarray_id\": 2261,\n"
             "      \"row\": 0,\n"
             "      \"col\": 1,\n"
             "      \"value\": \"0.75\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/Balls.nmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_connect_parameter_patch_v2(const char *path,
                                             const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"connect_parameter\",\n"
             "      \"source_id\": 7,\n"
             "      \"target_id\": 8\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_connect_parameter_handle_patch_v2(const char *path,
                                                    const char *output_path) {
    char json[3072];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"add_node\",\n"
             "      \"behavior_id\": 237,\n"
             "      \"guid\": \"18655B3F-68291DC3\",\n"
             "      \"name\": \"Patch Parameter Logger\"\n"
             "    },\n"
             "    {\n"
             "      \"op\": \"connect_parameter\",\n"
             "      \"source_id\": 234,\n"
             "      \"target_operation\": 1,\n"
             "      \"target_handle\": \"input_param:String\"\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Ballance/base.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_rewire_behavior_link_patch_v2(const char *path,
                                                const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"rewire_behavior_link\",\n"
             "      \"link_id\": 75,\n"
             "      \"from_io_id\": 78,\n"
             "      \"to_io_id\": 25\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_set_behavior_link_delay_patch_v2(
    const char *path,
    const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"set_behavior_link_delay\",\n"
             "      \"link_id\": 75,\n"
             "      \"activation_delay\": 5\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_remove_behavior_link_patch_v2(const char *path,
                                                const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"remove_behavior_link\",\n"
             "      \"parent_id\": 79,\n"
             "      \"link_id\": 75\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"),
             output_path);
    ASSERT_TRUE(write_text_file(path, json));
}

static void write_remove_node_patch_v2(const char *path,
                                       const char *output_path) {
    char json[2048];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"remove_node\",\n"
             "      \"parent_id\": 6,\n"
             "      \"node_id\": 4,\n"
             "      \"delete_flags\": 0\n"
             "    }\n"
             "  ]\n"
             "}\n",
             NMO_TEST_DATA_FILE("Nop.cmo"),
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
             "  \"version\": 2,\n"
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
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"fold\",\n"
             "      \"parent_id\": 4692,\n"
             "      \"nodes\": [2364, 2208],\n"
             "      \"anchor_id\": 2364,\n"
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
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"fold\",\n"
             "      \"parent_id\": 363,\n"
             "      \"nodes\": [237, 358],\n"
             "      \"anchor_id\": 358,\n"
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
             "  \"version\": 2,\n"
             "  \"input\": \"%s\",\n"
             "  \"output\": \"%s\",\n"
             "  \"operations\": [\n"
             "    {\n"
             "      \"op\": \"fold\",\n"
             "      \"parent_id\": %u,\n"
             "      \"nodes\": [%s],\n"
             "      \"anchor_id\": %u,\n"
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

static yyjson_val *find_object_by_id_and_role(yyjson_val *arr,
                                              uint64_t object_id,
                                              const char *role) {
    size_t idx;
    size_t max;
    yyjson_val *item;

    if (!arr || !role) {
        return NULL;
    }
    yyjson_arr_foreach(arr, idx, max, item) {
        if (yyjson_is_obj(item) &&
            get_uint_field(item, "object_id") == object_id &&
            strcmp(role, get_string_field(item, "role")) == 0) {
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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

TEST(cli, patch_apply_rejects_legacy_v1_manifest) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/legacy_v1.json";
    const char *output = "test_patch_tmp/legacy_v1.cmo";
    remove(patch);
    remove(output);
    write_legacy_v1_patch(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "patch apply \"%s\"", patch);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "version 2");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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

TEST(cli, patch_apply_v2_add_behavior_link_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/add_behavior_link_v2.json";
    const char *output = "test_patch_tmp/add_behavior_link_v2.cmo";
    remove(patch);
    remove(output);
    write_add_behavior_link_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("add_behavior_link", get_string_field(op, "op"));
    ASSERT_EQ(6u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    yyjson_val *created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_TRUE(yyjson_arr_size(created_objects) > 0u);
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    yyjson_val *graph_edge_diff = get_object_field(diff, "graph_edge_diff");
    ASSERT_NOT_NULL(graph_edge_diff);
    yyjson_val *created_edges = get_array_field(graph_edge_diff, "created");
    ASSERT_NOT_NULL(created_edges);
    ASSERT_TRUE(yyjson_arr_size(created_edges) > 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_add_parameter_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/add_parameter_v2.json";
    const char *output = "test_patch_tmp/add_parameter_v2.cmo";
    remove(patch);
    remove(output);
    write_add_parameter_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("add_parameter", get_string_field(op, "op"));
    ASSERT_EQ(6u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    yyjson_val *created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_TRUE(yyjson_arr_size(created_objects) > 0u);
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 6u));
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    yyjson_val *parameter_edge_diff =
        get_object_field(diff, "parameter_edge_diff");
    ASSERT_NOT_NULL(parameter_edge_diff);
    yyjson_val *created_parameters =
        get_array_field(parameter_edge_diff, "created");
    ASSERT_NOT_NULL(created_parameters);
    ASSERT_TRUE(yyjson_arr_size(created_parameters) > 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_set_parameter_value_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/set_parameter_value_v2.json";
    const char *output = "test_patch_tmp/set_parameter_value_v2.cmo";
    remove(patch);
    remove(output);
    write_set_parameter_value_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("set_parameter_value", get_string_field(op, "op"));
    ASSERT_EQ(18u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 18u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_set_parameter_value_to_handle_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/set_parameter_value_handle_v2.json";
    const char *output = "test_patch_tmp/set_parameter_value_handle_v2.cmo";
    remove(patch);
    remove(output);
    write_set_parameter_value_handle_patch_v2(patch, output);

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
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(operations));
    ASSERT_STR_EQ("add_node",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    yyjson_val *op = yyjson_arr_get(operations, 1);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("set_parameter_value", get_string_field(op, "op"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_diff_json_emits_normalized_v2_manifest) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/normalized_manifest_v2.json";
    const char *output = "test_patch_tmp/normalized_manifest_v2.cmo";
    remove(patch);
    remove(output);
    write_set_parameter_value_handle_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch diff \"%s\"", patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.diff", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    ASSERT_TRUE(get_bool_field(data, "dry_run"));

    yyjson_val *manifest = get_object_field(data, "manifest");
    ASSERT_NOT_NULL(manifest);
    ASSERT_EQ(2u, (uint32_t)get_uint_field(manifest, "version"));
    ASSERT_STR_EQ(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                  get_string_field(manifest, "input"));
    ASSERT_STR_EQ(output, get_string_field(manifest, "output"));

    yyjson_val *manifest_ops = get_array_field(manifest, "operations");
    ASSERT_NOT_NULL(manifest_ops);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(manifest_ops));
    yyjson_val *add_op = yyjson_arr_get(manifest_ops, 0);
    ASSERT_TRUE(add_op && yyjson_is_obj(add_op));
    ASSERT_STR_EQ("add_node", get_string_field(add_op, "op"));
    ASSERT_EQ(237u, (uint32_t)get_uint_field(add_op, "behavior_id"));
    ASSERT_STR_EQ("Patch 2D Text Logger", get_string_field(add_op, "name"));

    yyjson_val *set_op = yyjson_arr_get(manifest_ops, 1);
    ASSERT_TRUE(set_op && yyjson_is_obj(set_op));
    ASSERT_STR_EQ("set_parameter_value", get_string_field(set_op, "op"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(set_op, "parameter_operation"));
    ASSERT_STR_EQ("input_param:Text",
                  get_string_field(set_op, "parameter_handle"));
    ASSERT_STR_EQ("Patch trace", get_string_field(set_op, "value"));
    ASSERT_TRUE(yyjson_obj_get(set_op, "parameter_id") == NULL);
    ASSERT_FALSE(file_exists(output));

    const char *replay = "test_patch_tmp/normalized_manifest_replay_v2.json";
    remove(replay);
    ASSERT_TRUE(yyjson_val_write_file(replay, manifest,
                                      YYJSON_WRITE_PRETTY, NULL, NULL));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             replay);
    yyjson_doc *replay_doc = NULL;
    run_json_command(args, "patch.apply", &replay_doc);
    yyjson_val *replay_data =
        get_object_field(yyjson_doc_get_root(replay_doc), "data");
    ASSERT_NOT_NULL(replay_data);
    ASSERT_TRUE(get_bool_field(replay_data, "ok"));
    ASSERT_TRUE(get_bool_field(replay_data, "dry_run"));
    yyjson_doc_free(replay_doc);

    remove(replay);
    remove(patch);
}

TEST(cli, patch_diff_json_roundtrips_operation_handle_refs) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/operation_handle_manifest_v2.json";
    const char *output = "test_patch_tmp/operation_handle_manifest_v2.cmo";
    remove(patch);
    remove(output);
    write_add_operation_handle_patch_v2(patch, output);

    char args[1024];
    snprintf(args, sizeof(args), "-f json patch diff \"%s\"", patch);
    yyjson_doc *doc = NULL;
    run_json_command(args, "patch.diff", &doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *data = get_object_field(root, "data");
    ASSERT_NOT_NULL(data);
    ASSERT_TRUE(get_bool_field(data, "ok"));
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    yyjson_val *object_diff = get_object_field(diff, "object_diff");
    ASSERT_NOT_NULL(object_diff);
    yyjson_val *created_diff = get_array_field(object_diff, "created");
    ASSERT_NOT_NULL(created_diff);
    ASSERT_TRUE(yyjson_arr_size(created_diff) >= 4u);
    yyjson_val *manifest = get_object_field(data, "manifest");
    ASSERT_NOT_NULL(manifest);
    yyjson_val *ops = get_array_field(manifest, "operations");
    ASSERT_NOT_NULL(ops);
    ASSERT_EQ(4u, (uint32_t)yyjson_arr_size(ops));

    yyjson_val *op = yyjson_arr_get(ops, 3);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("add_operation", get_string_field(op, "op"));
    ASSERT_EQ(1u, (uint32_t)get_uint_field(op, "in1_operation"));
    ASSERT_STR_EQ("parameter", get_string_field(op, "in1_handle"));
    ASSERT_EQ(2u, (uint32_t)get_uint_field(op, "in2_operation"));
    ASSERT_STR_EQ("parameter", get_string_field(op, "in2_handle"));
    ASSERT_EQ(3u, (uint32_t)get_uint_field(op, "out_operation"));
    ASSERT_STR_EQ("parameter", get_string_field(op, "out_handle"));

    const char *replay = "test_patch_tmp/operation_handle_manifest_replay_v2.json";
    remove(replay);
    ASSERT_TRUE(yyjson_val_write_file(replay, manifest,
                                      YYJSON_WRITE_PRETTY, NULL, NULL));
    yyjson_doc_free(doc);

    snprintf(args, sizeof(args), "-f json patch apply \"%s\" --dry-run",
             replay);
    yyjson_doc *replay_doc = NULL;
    run_json_command(args, "patch.apply", &replay_doc);
    yyjson_val *replay_data =
        get_object_field(yyjson_doc_get_root(replay_doc), "data");
    ASSERT_NOT_NULL(replay_data);
    ASSERT_TRUE(get_bool_field(replay_data, "ok"));
    ASSERT_TRUE(get_bool_field(replay_data, "dry_run"));
    yyjson_val *replay_ops = get_array_field(replay_data, "operations");
    ASSERT_NOT_NULL(replay_ops);
    ASSERT_EQ(4u, (uint32_t)yyjson_arr_size(replay_ops));
    ASSERT_STR_EQ("add_operation",
                  get_string_field(yyjson_arr_get(replay_ops, 3), "op"));
    yyjson_doc_free(replay_doc);

    ASSERT_FALSE(file_exists(output));
    remove(replay);
    remove(patch);
}

TEST(cli, patch_apply_v2_set_parameter_bytes_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/set_parameter_bytes_v2.json";
    const char *output = "test_patch_tmp/set_parameter_bytes_v2.nmo";
    remove(patch);
    remove(output);
    write_set_parameter_bytes_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("set_parameter_bytes", get_string_field(op, "op"));
    ASSERT_EQ(64u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 64u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_set_parameter_bytes_to_handle_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/set_parameter_bytes_handle_v2.json";
    const char *output = "test_patch_tmp/set_parameter_bytes_handle_v2.cmo";
    remove(patch);
    remove(output);
    write_set_parameter_bytes_handle_patch_v2(patch, output);

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
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(operations));
    ASSERT_STR_EQ("add_node",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    yyjson_val *op = yyjson_arr_get(operations, 1);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("set_parameter_bytes", get_string_field(op, "op"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_set_data_cell_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/set_data_cell_v2.json";
    const char *output = "test_patch_tmp/set_data_cell_v2.cmo";
    remove(patch);
    remove(output);
    write_set_data_cell_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("set_data_cell", get_string_field(op, "op"));
    ASSERT_EQ(2261u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 2261u));
    ASSERT_NOT_NULL(find_object_by_id_and_role(changed_objects, 2261u, "data_cell"));
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    yyjson_val *data_cell_diff = get_object_field(diff, "data_cell_diff");
    ASSERT_NOT_NULL(data_cell_diff);
    yyjson_val *changed_cells = get_array_field(data_cell_diff, "changed");
    ASSERT_NOT_NULL(changed_cells);
    ASSERT_NOT_NULL(find_object_by_id_and_role(changed_cells, 2261u, "data_cell"));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_remove_parameter_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/remove_parameter_v2.json";
    const char *output = "test_patch_tmp/remove_parameter_v2.cmo";
    remove(patch);
    remove(output);
    write_remove_parameter_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("remove_parameter", get_string_field(op, "op"));
    ASSERT_EQ(18u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_TRUE(array_contains_object_id(deleted_objects, 18u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_add_operation_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/add_operation_v2.json";
    const char *output = "test_patch_tmp/add_operation_v2.cmo";
    remove(patch);
    remove(output);
    write_add_operation_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("add_operation", get_string_field(op, "op"));
    ASSERT_EQ(6u, (uint32_t)get_uint_field(op, "primary_id"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    yyjson_val *created_objects = get_array_field(data, "created_objects");
    ASSERT_NOT_NULL(created_objects);
    ASSERT_TRUE(yyjson_arr_size(created_objects) > 0u);
    yyjson_val *diff = get_object_field(data, "diff");
    ASSERT_NOT_NULL(diff);
    yyjson_val *operation_graph_diff =
        get_object_field(diff, "operation_graph_diff");
    ASSERT_NOT_NULL(operation_graph_diff);
    yyjson_val *created_operations =
        get_array_field(operation_graph_diff, "created");
    ASSERT_NOT_NULL(created_operations);
    ASSERT_TRUE(yyjson_arr_size(created_operations) > 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_rewire_operation_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/rewire_operation_v2.json";
    const char *output = "test_patch_tmp/rewire_operation_v2.cmo";
    remove(patch);
    remove(output);
    write_rewire_operation_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(3u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 2);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("rewire_operation", get_string_field(op, "op"));
    ASSERT_EQ(17u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 17u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_remove_operation_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/remove_operation_v2.json";
    const char *output = "test_patch_tmp/remove_operation_v2.cmo";
    remove(patch);
    remove(output);
    write_add_remove_operation_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 1);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("remove_operation", get_string_field(op, "op"));
    ASSERT_EQ(16u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_TRUE(array_contains_object_id(deleted_objects, 16u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_connect_parameter_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/connect_parameter_v2.json";
    const char *output = "test_patch_tmp/connect_parameter_v2.cmo";
    remove(patch);
    remove(output);
    write_connect_parameter_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("connect_parameter", get_string_field(op, "op"));
    ASSERT_EQ(8u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 8u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_connect_parameter_to_handle_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/connect_parameter_handle_v2.json";
    const char *output = "test_patch_tmp/connect_parameter_handle_v2.cmo";
    remove(patch);
    remove(output);
    write_connect_parameter_handle_patch_v2(patch, output);

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
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(2u, (uint32_t)yyjson_arr_size(operations));
    ASSERT_STR_EQ("add_node",
                  get_string_field(yyjson_arr_get(operations, 0), "op"));
    yyjson_val *op = yyjson_arr_get(operations, 1);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("connect_parameter", get_string_field(op, "op"));
    ASSERT_TRUE(get_uint_field(op, "result_id") != 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_disconnect_parameter_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/disconnect_parameter_v2.json";
    const char *output = "test_patch_tmp/disconnect_parameter_v2.cmo";
    remove(patch);
    remove(output);
    write_disconnect_parameter_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("disconnect_parameter", get_string_field(op, "op"));
    ASSERT_EQ(8u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 8u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_rewire_behavior_link_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/rewire_behavior_link_v2.json";
    const char *output = "test_patch_tmp/rewire_behavior_link_v2.cmo";
    remove(patch);
    remove(output);
    write_rewire_behavior_link_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("rewire_behavior_link", get_string_field(op, "op"));
    ASSERT_EQ(75u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 75u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_set_behavior_link_delay_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/set_behavior_link_delay_v2.json";
    const char *output = "test_patch_tmp/set_behavior_link_delay_v2.cmo";
    remove(patch);
    remove(output);
    write_set_behavior_link_delay_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("set_behavior_link_delay", get_string_field(op, "op"));
    ASSERT_EQ(75u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *changed_objects = get_array_field(data, "changed_objects");
    ASSERT_NOT_NULL(changed_objects);
    ASSERT_TRUE(array_contains_object_id(changed_objects, 75u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_remove_behavior_link_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/remove_behavior_link_v2.json";
    const char *output = "test_patch_tmp/remove_behavior_link_v2.cmo";
    remove(patch);
    remove(output);
    write_remove_behavior_link_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("remove_behavior_link", get_string_field(op, "op"));
    ASSERT_EQ(79u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_TRUE(array_contains_object_id(deleted_objects, 75u));
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);
    remove(patch);
}

TEST(cli, patch_apply_v2_remove_node_dry_run) {
    make_dir("test_patch_tmp");
    const char *patch = "test_patch_tmp/remove_node_v2.json";
    const char *output = "test_patch_tmp/remove_node_v2.cmo";
    remove(patch);
    remove(output);
    write_remove_node_patch_v2(patch, output);

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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
    yyjson_val *operations = get_array_field(data, "operations");
    ASSERT_NOT_NULL(operations);
    ASSERT_EQ(1u, (uint32_t)yyjson_arr_size(operations));
    yyjson_val *op = yyjson_arr_get(operations, 0);
    ASSERT_TRUE(op && yyjson_is_obj(op));
    ASSERT_STR_EQ("remove_node", get_string_field(op, "op"));
    ASSERT_EQ(6u, (uint32_t)get_uint_field(op, "primary_id"));
    yyjson_val *deleted_objects = get_array_field(data, "deleted_objects");
    ASSERT_NOT_NULL(deleted_objects);
    ASSERT_TRUE(array_contains_object_id(deleted_objects, 4u));
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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

TEST(cli, patch_apply_v2_rejects_strict_manifest_edges) {
    struct invalid_case {
        const char *name;
        const char *operation_json;
        const char *expected_text;
    } cases[] = {
        {
            "operation_zero",
            "    {\n"
            "      \"op\": \"set_parameter_value\",\n"
            "      \"parameter_operation\": 0,\n"
            "      \"parameter_handle\": \"parameter\",\n"
            "      \"value\": \"x\"\n"
            "    }",
            "Missing or invalid parameter_operation",
        },
        {
            "missing_handle",
            "    {\n"
            "      \"op\": \"set_parameter_value\",\n"
            "      \"parameter_operation\": 1,\n"
            "      \"value\": \"x\"\n"
            "    }",
            "Missing or invalid parameter_handle",
        },
        {
            "id_and_handle",
            "    {\n"
            "      \"op\": \"connect_parameter\",\n"
            "      \"source_id\": 1,\n"
            "      \"target_id\": 2,\n"
            "      \"target_operation\": 1,\n"
            "      \"target_handle\": \"input_param:String\"\n"
            "    }",
            "connect_parameter requires either target_id or target_operation plus target_handle",
        },
        {
            "operation_slot_zero",
            "    {\n"
            "      \"op\": \"add_operation\",\n"
            "      \"parent_id\": 3,\n"
            "      \"operation_guid\": \"00000002-00000000\",\n"
            "      \"in1_operation\": 0,\n"
            "      \"in1_handle\": \"parameter\"\n"
            "    }",
            "Missing or invalid in1_operation",
        },
        {
            "operation_slot_missing_handle",
            "    {\n"
            "      \"op\": \"add_operation\",\n"
            "      \"parent_id\": 3,\n"
            "      \"operation_guid\": \"00000002-00000000\",\n"
            "      \"in1_operation\": 1\n"
            "    }",
            "Missing or invalid in1_handle",
        },
        {
            "operation_slot_id_and_handle",
            "    {\n"
            "      \"op\": \"add_operation\",\n"
            "      \"parent_id\": 3,\n"
            "      \"operation_guid\": \"00000002-00000000\",\n"
            "      \"in1_id\": 1,\n"
            "      \"in1_operation\": 1,\n"
            "      \"in1_handle\": \"parameter\"\n"
            "    }",
            "add_operation operation requires either in1_id or in1_operation plus in1_handle",
        },
        {
            "unknown_op",
            "    {\n"
            "      \"op\": \"not_a_real_op\"\n"
            "    }",
            "Unsupported edit plan op 'not_a_real_op'",
        },
        {
            "unknown_field",
            "    {\n"
            "      \"op\": \"add_io\",\n"
            "      \"behavior_id\": 3,\n"
            "      \"kind\": \"input\",\n"
            "      \"name\": \"Patch V2 In\",\n"
            "      \"unexpected\": true\n"
            "    }",
            "Unknown field 'unexpected' in add_io operation",
        },
    };

    make_dir("test_patch_tmp");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char patch[256];
        char output[256];
        snprintf(patch, sizeof(patch),
                 "test_patch_tmp/invalid_%s.json", cases[i].name);
        snprintf(output, sizeof(output),
                 "test_patch_tmp/invalid_%s.cmo", cases[i].name);
        remove(patch);
        remove(output);
        write_raw_v2_patch_operation(
            patch, output, cases[i].operation_json);
        assert_patch_apply_fails_with(patch, cases[i].expected_text);
        ASSERT_FALSE(file_exists(output));
        remove(patch);
    }
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
    ASSERT_TRUE(yyjson_obj_get(data, "operation_count") == NULL);
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
    yyjson_val *replay_summary = get_object_field(diff, "replay_summary");
    ASSERT_NOT_NULL(replay_summary);
    ASSERT_EQ(1u, (uint32_t)get_uint_field(replay_summary, "operation_count"));
    ASSERT_TRUE(get_uint_field(replay_summary, "deleted_object_count") > 0u);
    ASSERT_FALSE(file_exists(output));
    yyjson_doc_free(doc);

    remove(patch);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli, patch_apply_rejects_non_leaf_replace_bb);
    REGISTER_TEST(cli, patch_apply_json_failure_reports_edit_report);
    REGISTER_TEST(cli, patch_apply_v2_replace_bb_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_node_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_remove_node_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_behavior_link_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_rewire_behavior_link_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_set_behavior_link_delay_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_remove_behavior_link_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_parameter_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_operation_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_remove_operation_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_rewire_operation_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_connect_parameter_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_connect_parameter_to_handle_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_disconnect_parameter_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_remove_parameter_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_set_parameter_value_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_set_parameter_value_to_handle_dry_run);
    REGISTER_TEST(cli, patch_diff_json_emits_normalized_v2_manifest);
    REGISTER_TEST(cli, patch_diff_json_roundtrips_operation_handle_refs);
    REGISTER_TEST(cli, patch_apply_v2_set_parameter_bytes_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_set_parameter_bytes_to_handle_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_set_data_cell_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_add_io_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_remove_io_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_rename_io_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_interface_policy_dry_run);
    REGISTER_TEST(cli, patch_apply_v2_rejects_strict_manifest_edges);
    REGISTER_TEST(cli, patch_apply_fold_dry_run_reports_analysis);
    REGISTER_TEST(cli, patch_apply_fold_dry_run_reports_semantic_risks);
    REGISTER_TEST(cli, patch_diff_json_reports_fold_delete_plan);
    REGISTER_TEST(cli, patch_apply_leaf_replace_bb_dry_run_and_apply);
    REGISTER_TEST(cli, patch_apply_rejects_legacy_v1_manifest);
TEST_MAIN_END()
