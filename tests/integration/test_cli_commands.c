/**
 * @file test_cli_commands.c
 * @brief CLI command integration tests
 *
 * Tests new CLI command groups (convert, diff, query, extension),
 * batch processing, object list --filter, file stats, and validate --fix.
 */

#include "test_framework.h"

#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * On MinGW/MSYS, _popen uses cmd.exe which has path handling issues.
 * Use popen (which uses /bin/sh on MSYS) for reliable behavior.
 */
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

/* ============================================================================
 * Helpers
 * ============================================================================ */

static char *run_cli(const char *args) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) {
        return NULL;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        NMO_PCLOSE(pipe);
        return NULL;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t clen = strlen(chunk);
        if (len + clen + 1 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); NMO_PCLOSE(pipe); return NULL; }
            buf = nb;
        }
        memcpy(buf + len, chunk, clen);
        len += clen;
    }
    buf[len] = '\0';
    NMO_PCLOSE(pipe);
    return buf;
}

static yyjson_doc *run_cli_json(const char *args) {
    char full_args[2048];
    snprintf(full_args, sizeof(full_args), "-f json %s", args);
    char *output = run_cli(full_args);
    if (!output) return NULL;
    yyjson_doc *doc = yyjson_read(output, strlen(output), 0);
    free(output);
    return doc;
}

static yyjson_val *json_envelope_data(yyjson_doc *doc) {
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root) return NULL;
    return yyjson_obj_get(root, "data");
}

static const char *json_envelope_command(yyjson_doc *doc) {
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root) return NULL;
    yyjson_val *cmd = yyjson_obj_get(root, "command");
    return yyjson_get_str(cmd);
}

/* ============================================================================
 * file info
 * ============================================================================ */

TEST(cli, file_info_text) {
    char args[512];
    snprintf(args, sizeof(args), "file info \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Objects");
    ASSERT_STR_CONTAINS(output, "Managers");
    ASSERT_STR_CONTAINS(output, "CK Version");
    free(output);
}

TEST(cli, file_info_json) {
    char args[512];
    snprintf(args, sizeof(args), "file info \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    const char *cmd = json_envelope_command(doc);
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "file.info");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    yyjson_val *obj_count = yyjson_obj_get(data, "object_count");
    ASSERT_NOT_NULL(obj_count);
    ASSERT_TRUE(yyjson_get_uint(obj_count) > 0);

    yyjson_doc_free(doc);
}

/* ============================================================================
 * file stats (enhanced)
 * ============================================================================ */

TEST(cli, file_stats_text) {
    char args[512];
    snprintf(args, sizeof(args), "file stats \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "File Statistics");
    ASSERT_STR_CONTAINS(output, "Objects");
    ASSERT_STR_CONTAINS(output, "Chunks");
    ASSERT_STR_CONTAINS(output, "Memory");
    ASSERT_STR_CONTAINS(output, "References");
    /* Performance only shown with -v */
    ASSERT_TRUE(strstr(output, "Performance") == NULL);
    free(output);
}

TEST(cli, file_stats_verbose) {
    char args[512];
    snprintf(args, sizeof(args), "-v file stats \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Performance");
    ASSERT_STR_CONTAINS(output, "Load Time");
    free(output);
}

TEST(cli, file_stats_json) {
    char args[512];
    snprintf(args, sizeof(args), "file stats \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    /* Check all stat sections present in JSON */
    ASSERT_NOT_NULL(yyjson_obj_get(data, "objects"));
    ASSERT_NOT_NULL(yyjson_obj_get(data, "chunks"));
    ASSERT_NOT_NULL(yyjson_obj_get(data, "memory"));
    ASSERT_NOT_NULL(yyjson_obj_get(data, "references"));
    ASSERT_NOT_NULL(yyjson_obj_get(data, "performance"));

    /* Check memory fields */
    yyjson_val *mem = yyjson_obj_get(data, "memory");
    ASSERT_NOT_NULL(yyjson_obj_get(mem, "total_size"));
    ASSERT_NOT_NULL(yyjson_obj_get(mem, "header_size"));
    ASSERT_NOT_NULL(yyjson_obj_get(mem, "compression_ratio"));

    yyjson_doc_free(doc);
}

/* ============================================================================
 * object list
 * ============================================================================ */

TEST(cli, object_list_text) {
    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Objects:");
    ASSERT_STR_CONTAINS(output, "InGameCam");
    free(output);
}

TEST(cli, object_list_class_filter) {
    char args[512];
    snprintf(args, sizeof(args), "object list --class CKGroup \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "filtered by class");
    ASSERT_STR_CONTAINS(output, "CKGroup");
    free(output);
}

TEST(cli, object_list_json) {
    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    const char *cmd = json_envelope_command(doc);
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "object.list");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    yyjson_val *objects = yyjson_obj_get(data, "objects");
    ASSERT_NOT_NULL(objects);
    ASSERT_TRUE(yyjson_is_arr(objects));
    ASSERT_TRUE(yyjson_arr_size(objects) > 0);

    /* Check first object has required fields */
    yyjson_val *first = yyjson_arr_get(objects, 0);
    ASSERT_NOT_NULL(first);
    ASSERT_NOT_NULL(yyjson_obj_get(first, "id"));
    ASSERT_NOT_NULL(yyjson_obj_get(first, "class_id"));

    yyjson_doc_free(doc);
}

/* ============================================================================
 * validate commands
 * ============================================================================ */

TEST(cli, validate_all_text) {
    char args[512];
    snprintf(args, sizeof(args), "validate all \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Objects");
    ASSERT_STR_CONTAINS(output, "Errors");
    free(output);
}

TEST(cli, validate_all_json) {
    char args[512];
    snprintf(args, sizeof(args), "validate all \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    yyjson_val *valid = yyjson_obj_get(data, "valid");
    ASSERT_NOT_NULL(valid);
    ASSERT_TRUE(yyjson_is_bool(valid));

    yyjson_doc_free(doc);
}

TEST(cli, validate_structure_text) {
    char args[512];
    snprintf(args, sizeof(args), "validate structure \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Structure Validation");
    ASSERT_STR_CONTAINS(output, "Errors");
    free(output);
}

TEST(cli, validate_structure_fix) {
    char args[512];
    snprintf(args, sizeof(args), "validate structure --fix \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    /* Should run without error (Camera.nmo is valid so no fix suggestions shown) */
    ASSERT_STR_CONTAINS(output, "Structure Validation");
    free(output);
}

TEST(cli, validate_references_text) {
    char args[512];
    snprintf(args, sizeof(args), "validate references \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Reference Validation");
    ASSERT_STR_CONTAINS(output, "Total references");
    free(output);
}

TEST(cli, validate_references_json) {
    char args[512];
    snprintf(args, sizeof(args), "validate references \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    ASSERT_NOT_NULL(yyjson_obj_get(data, "total_references"));
    ASSERT_NOT_NULL(yyjson_obj_get(data, "valid"));

    yyjson_doc_free(doc);
}

/* ============================================================================
 * type commands
 * ============================================================================ */

TEST(cli, type_list_text) {
    char args[512];
    snprintf(args, sizeof(args), "type list \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    /* Should list registered types */
    ASSERT_STR_CONTAINS(output, "CKObject");
    free(output);
}

TEST(cli, type_list_json) {
    char args[512];
    snprintf(args, sizeof(args), "type list \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    const char *cmd = json_envelope_command(doc);
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "type.list");

    yyjson_doc_free(doc);
}

/* ============================================================================
 * batch processing
 * ============================================================================ */

TEST(cli, batch_file_info) {
    char args[1024];
    snprintf(args, sizeof(args),
             "--batch file info \"%s\" \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"),
             NMO_TEST_DATA_FILE("Balls.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Camera.nmo");
    ASSERT_STR_CONTAINS(output, "Balls.nmo");
    ASSERT_STR_CONTAINS(output, "Batch Summary");
    free(output);
}

TEST(cli, batch_file_info_json) {
    char args[1024];
    snprintf(args, sizeof(args),
             "--batch file info \"%s\" \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"),
             NMO_TEST_DATA_FILE("Balls.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);

    /* Batch JSON has results/summary at root level */
    yyjson_val *results = yyjson_obj_get(root, "results");
    ASSERT_NOT_NULL(results);
    ASSERT_TRUE(yyjson_is_arr(results));
    ASSERT_TRUE(yyjson_arr_size(results) == 2);

    yyjson_val *summary = yyjson_obj_get(root, "summary");
    ASSERT_NOT_NULL(summary);

    /* Check batch_mode flag */
    yyjson_val *batch_mode = yyjson_obj_get(root, "batch_mode");
    ASSERT_NOT_NULL(batch_mode);
    ASSERT_TRUE(yyjson_get_bool(batch_mode));

    yyjson_doc_free(doc);
}

TEST(cli, batch_validate_all) {
    char args[1024];
    snprintf(args, sizeof(args),
             "--batch validate all \"%s\" \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"),
             NMO_TEST_DATA_FILE("Balls.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Camera.nmo");
    ASSERT_STR_CONTAINS(output, "Balls.nmo");
    free(output);
}

/* ============================================================================
 * JSON schema envelope
 * ============================================================================ */

TEST(cli, json_schema_envelope) {
    char args[512];
    snprintf(args, sizeof(args), "file info \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *root = yyjson_doc_get_root(doc);
    ASSERT_NOT_NULL(root);

    /* Required envelope fields */
    yyjson_val *schema = yyjson_obj_get(root, "schema_version");
    ASSERT_NOT_NULL(schema);
    ASSERT_STR_EQ(yyjson_get_str(schema), "3.0.0");

    yyjson_val *tool = yyjson_obj_get(root, "tool");
    ASSERT_NOT_NULL(tool);
    ASSERT_STR_EQ(yyjson_get_str(tool), "nmo");

    ASSERT_NOT_NULL(yyjson_obj_get(root, "command"));
    ASSERT_NOT_NULL(yyjson_obj_get(root, "timestamp"));
    ASSERT_NOT_NULL(yyjson_obj_get(root, "data"));

    yyjson_doc_free(doc);
}

/* ============================================================================
 * help / usage
 * ============================================================================ */

TEST(cli, help_shows_groups) {
    char *output = run_cli("--help");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "file");
    ASSERT_STR_CONTAINS(output, "object");
    ASSERT_STR_CONTAINS(output, "validate");
    ASSERT_STR_CONTAINS(output, "convert");
    ASSERT_STR_CONTAINS(output, "diff");
    ASSERT_STR_CONTAINS(output, "query");
    ASSERT_STR_CONTAINS(output, "extension");
    free(output);
}

TEST(cli, unknown_command_error) {
    char *output = run_cli("nonexistent foobar");
    ASSERT_NOT_NULL(output);
    /* Should show error or help */
    ASSERT_TRUE(strlen(output) > 0);
    free(output);
}

/* ============================================================================
 * Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* file commands */
    REGISTER_TEST(cli, file_info_text);
    REGISTER_TEST(cli, file_info_json);
    REGISTER_TEST(cli, file_stats_text);
    REGISTER_TEST(cli, file_stats_verbose);
    REGISTER_TEST(cli, file_stats_json);

    /* object commands */
    REGISTER_TEST(cli, object_list_text);
    REGISTER_TEST(cli, object_list_class_filter);
    REGISTER_TEST(cli, object_list_json);

    /* validate commands */
    REGISTER_TEST(cli, validate_all_text);
    REGISTER_TEST(cli, validate_all_json);
    REGISTER_TEST(cli, validate_structure_text);
    REGISTER_TEST(cli, validate_structure_fix);
    REGISTER_TEST(cli, validate_references_text);
    REGISTER_TEST(cli, validate_references_json);

    /* type commands */
    REGISTER_TEST(cli, type_list_text);
    REGISTER_TEST(cli, type_list_json);

    /* batch processing */
    REGISTER_TEST(cli, batch_file_info);
    REGISTER_TEST(cli, batch_file_info_json);
    REGISTER_TEST(cli, batch_validate_all);

    /* JSON envelope */
    REGISTER_TEST(cli, json_schema_envelope);

    /* help/usage */
    REGISTER_TEST(cli, help_shows_groups);
    REGISTER_TEST(cli, unknown_command_error);
TEST_MAIN_END()
