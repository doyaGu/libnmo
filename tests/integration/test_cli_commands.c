/**
 * @file test_cli_commands.c
 * @brief CLI command integration tests
 *
 * Tests new CLI command groups (convert, diff, query, extension),
 * batch processing, object list --filter, file stats, and validate --fix.
 */

#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

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
    char *buf = (char *)malloc(cap);
    if (!buf) {
        result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
        return result;
    }

    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t clen = strlen(chunk);
        if (len + clen + 1 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
                return result;
            }
            buf = nb;
        }
        memcpy(buf + len, chunk, clen);
        len += clen;
    }
    buf[len] = '\0';
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    result.output = buf;
    return result;
}

static char *run_cli(const char *args) {
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code < 0) {
        free(result.output);
        return NULL;
    }
    return result.output;
}

static char *read_file_text(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read_size != (size_t)size) {
        free(buf);
        return NULL;
    }

    buf[size] = '\0';
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

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
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

TEST(cli, file_info_output_file_text) {
    const char *report_path = "test_cli_file_info_output.txt";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" file info \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "File Info");
    ASSERT_STR_CONTAINS(report, "Objects");
    ASSERT_STR_CONTAINS(report, "Managers");
    ASSERT_STR_CONTAINS(report, "CK Version");

    free(report);
    free(result.output);
    remove(report_path);
}

TEST(cli, file_info_output_file_json) {
    const char *report_path = "test_cli_file_info_output.json";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" -f json file info \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    yyjson_doc *doc = yyjson_read(report, strlen(report), 0);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "file.info");
    ASSERT_NOT_NULL(json_envelope_data(doc));

    yyjson_doc_free(doc);
    free(report);
    free(result.output);
    remove(report_path);
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

TEST(cli, validate_all_output_file_text) {
    const char *report_path = "test_cli_validate_all_output.txt";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" validate all \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "Validation Results");
    ASSERT_STR_CONTAINS(report, "Objects");
    ASSERT_STR_CONTAINS(report, "Errors");
    ASSERT_STR_CONTAINS(report, "Warnings");
    ASSERT_STR_CONTAINS(report, "Result:");

    free(report);
    free(result.output);
    remove(report_path);
}

TEST(cli, validate_all_output_file_json) {
    const char *report_path = "test_cli_validate_all_output.json";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" -f json validate all \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    yyjson_doc *doc = yyjson_read(report, strlen(report), 0);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "validate.all");
    ASSERT_NOT_NULL(json_envelope_data(doc));

    yyjson_doc_free(doc);
    free(report);
    free(result.output);
    remove(report_path);
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

TEST(cli, batch_file_info_output_file_text) {
    const char *report_path = "test_cli_batch_file_info_output.txt";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" --batch file info \"%s\" \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"), NMO_TEST_DATA_FILE("Menu.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "--- [1/2]");
    ASSERT_STR_CONTAINS(report, "--- [2/2]");
    ASSERT_STR_CONTAINS(report, "Objects");
    ASSERT_STR_CONTAINS(report, "=== Batch Summary ===");

    free(report);
    free(result.output);
    remove(report_path);
}

TEST(cli, batch_validate_all_output_file_text) {
    const char *report_path = "test_cli_batch_validate_all_output.txt";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" --batch validate all \"%s\" \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"), NMO_TEST_DATA_FILE("Menu.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "--- [1/2]");
    ASSERT_STR_CONTAINS(report, "--- [2/2]");
    ASSERT_STR_CONTAINS(report, "Result:");
    ASSERT_STR_CONTAINS(report, "=== Batch Summary ===");

    free(report);
    free(result.output);
    remove(report_path);
}

TEST(cli, convert_version_output_file_text) {
    const char *report_path = "test_cli_convert_version_output.txt";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" convert version \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "File version:");
    ASSERT_STR_CONTAINS(report, "CK version:");
    ASSERT_STR_CONTAINS(report, "Manager count:");

    free(report);
    free(result.output);
    remove(report_path);
}

TEST(cli, convert_version_output_file_json) {
    const char *report_path = "test_cli_convert_version_output.json";
    remove(report_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" -f json convert version \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    yyjson_doc *doc = yyjson_read(report, strlen(report), 0);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "convert.version");
    ASSERT_NOT_NULL(json_envelope_data(doc));

    yyjson_doc_free(doc);
    free(report);
    free(result.output);
    remove(report_path);
}

TEST(cli, convert_copy_output_and_report_files) {
    const char *report_path = "test_cli_convert_copy_report.txt";
    const char *save_path = "test_cli_convert_copy_output.nmo";
    remove(report_path);
    remove(save_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" convert copy -o \"%s\" \"%s\"",
             report_path, save_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");
    ASSERT_TRUE(file_exists(save_path));

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "Saved to");
    ASSERT_STR_CONTAINS(report, save_path);

    free(report);
    free(result.output);
    remove(report_path);
    remove(save_path);
}

TEST(cli, convert_merge_option_values_not_treated_as_files) {
    const char *report_path = "test_cli_convert_merge_report.txt";
    const char *save_path = "test_cli_convert_merge_output.nmo";
    remove(report_path);
    remove(save_path);

    char args[1024];
    snprintf(args, sizeof(args),
             "--output \"%s\" convert merge -o \"%s\" \"%s\" \"%s\"",
             report_path, save_path,
             NMO_TEST_DATA_FILE("Camera.nmo"),
             NMO_TEST_DATA_FILE("Menu.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");
    ASSERT_TRUE(file_exists(save_path));

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "Copied");
    ASSERT_STR_CONTAINS(report, "Saved to");

    free(report);
    free(result.output);
    remove(report_path);
    remove(save_path);
}

TEST(cli, diff_chunks_object_option_value_not_treated_as_file) {
    char args[1024];
    snprintf(args, sizeof(args),
             "diff chunks --object 1 \"%s\" \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"),
             NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Chunk Comparison");
    ASSERT_STR_CONTAINS(result.output, "File 1");
    ASSERT_STR_CONTAINS(result.output, "File 2");
    free(result.output);
}

TEST(cli, file_info_output_open_failure) {
    const char *report_path = "test_cli_missing_dir_a8d74e/report.txt";
    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" file info \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_IO_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Cannot open");
    free(result.output);
}

TEST(cli, validate_all_output_open_failure) {
    const char *report_path = "test_cli_missing_dir_b19c52/report.txt";
    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" validate all \"%s\"",
             report_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_IO_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Cannot open");
    free(result.output);
}

TEST(cli, convert_copy_output_open_failure) {
    const char *report_path = "test_cli_missing_dir_c27d1f/report.txt";
    const char *save_path = "test_cli_convert_copy_fail_output.nmo";
    remove(save_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" convert copy -o \"%s\" \"%s\"",
             report_path, save_path, NMO_TEST_DATA_FILE("Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_IO_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Cannot open");
    ASSERT_FALSE(file_exists(save_path));

    free(result.output);
    remove(save_path);
}

/* ============================================================================
 * object list SIZE column
 * ============================================================================ */

TEST(cli, object_list_has_size_json) {
    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *objects = yyjson_obj_get(data, "objects");
    ASSERT_NOT_NULL(objects);
    ASSERT_TRUE(yyjson_arr_size(objects) > 0);

    yyjson_val *first = yyjson_arr_get_first(objects);
    ASSERT_NOT_NULL(first);
    yyjson_val *size_val = yyjson_obj_get(first, "size");
    ASSERT_NOT_NULL(size_val);
    ASSERT_TRUE(yyjson_is_uint(size_val));

    yyjson_doc_free(doc);
}

TEST(cli, object_list_has_size_text) {
    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);

    ASSERT_STR_CONTAINS(output, "SIZE");
    ASSERT_STR_CONTAINS(output, "ID");
    ASSERT_STR_CONTAINS(output, "CLASS");
    ASSERT_STR_CONTAINS(output, "NAME");

    free(output);
}

/* ============================================================================
 * object list --sort / --top / --reverse
 * ============================================================================ */

TEST(cli, object_list_sort_by_size_json) {
    char args[512];
    snprintf(args, sizeof(args), "object list --sort=size --reverse \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    yyjson_val *objects = yyjson_obj_get(data, "objects");
    size_t count = yyjson_arr_size(objects);
    ASSERT_TRUE(count >= 2);

    uint64_t prev_size = UINT64_MAX;
    size_t idx, max;
    yyjson_val *obj;
    yyjson_arr_foreach(objects, idx, max, obj) {
        uint64_t sz = yyjson_get_uint(yyjson_obj_get(obj, "size"));
        ASSERT_TRUE(sz <= prev_size);
        prev_size = sz;
    }

    yyjson_doc_free(doc);
}

TEST(cli, object_list_top_limits_output) {
    char args[512];
    snprintf(args, sizeof(args), "object list --top 3 \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    yyjson_val *objects = yyjson_obj_get(data, "objects");
    ASSERT_TRUE(yyjson_arr_size(objects) == 3);

    yyjson_doc_free(doc);
}

TEST(cli, object_list_sort_and_top_combined) {
    char args[512];
    snprintf(args, sizeof(args), "object list --sort=size --reverse --top 5 \"%s\"",
             NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    yyjson_val *objects = yyjson_obj_get(data, "objects");
    ASSERT_TRUE(yyjson_arr_size(objects) <= 5);

    uint64_t prev_size = UINT64_MAX;
    size_t idx, max;
    yyjson_val *obj;
    yyjson_arr_foreach(objects, idx, max, obj) {
        uint64_t sz = yyjson_get_uint(yyjson_obj_get(obj, "size"));
        ASSERT_TRUE(sz <= prev_size);
        prev_size = sz;
    }

    yyjson_doc_free(doc);
}

/* ============================================================================
 * file classes SIZE columns and --sort
 * ============================================================================ */

TEST(cli, file_classes_has_size_json) {
    char args[512];
    snprintf(args, sizeof(args), "file classes \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = json_envelope_data(doc);
    yyjson_val *classes = yyjson_obj_get(data, "classes");
    ASSERT_NOT_NULL(classes);
    ASSERT_TRUE(yyjson_arr_size(classes) > 0);
    yyjson_val *first = yyjson_arr_get_first(classes);
    ASSERT_NOT_NULL(yyjson_obj_get(first, "total_size"));
    ASSERT_NOT_NULL(yyjson_obj_get(first, "avg_size"));
    ASSERT_NOT_NULL(yyjson_obj_get(first, "percentage"));
    yyjson_doc_free(doc);
}

TEST(cli, file_classes_sort_by_size) {
    char args[512];
    snprintf(args, sizeof(args), "file classes --sort=size \"%s\"", NMO_TEST_DATA_FILE("Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = json_envelope_data(doc);
    yyjson_val *classes = yyjson_obj_get(data, "classes");
    size_t count = yyjson_arr_size(classes);
    ASSERT_TRUE(count >= 2);
    uint64_t prev = UINT64_MAX;
    size_t idx, max;
    yyjson_val *cls;
    yyjson_arr_foreach(classes, idx, max, cls) {
        uint64_t sz = yyjson_get_uint(yyjson_obj_get(cls, "total_size"));
        ASSERT_TRUE(sz <= prev);
        prev = sz;
    }
    yyjson_doc_free(doc);
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
    REGISTER_TEST(cli, file_info_output_file_text);
    REGISTER_TEST(cli, file_info_output_file_json);
    REGISTER_TEST(cli, file_stats_text);
    REGISTER_TEST(cli, file_stats_verbose);
    REGISTER_TEST(cli, file_stats_json);
    REGISTER_TEST(cli, file_classes_has_size_json);
    REGISTER_TEST(cli, file_classes_sort_by_size);

    /* object commands */
    REGISTER_TEST(cli, object_list_text);
    REGISTER_TEST(cli, object_list_class_filter);
    REGISTER_TEST(cli, object_list_json);
    REGISTER_TEST(cli, object_list_has_size_json);
    REGISTER_TEST(cli, object_list_has_size_text);
    REGISTER_TEST(cli, object_list_sort_by_size_json);
    REGISTER_TEST(cli, object_list_top_limits_output);
    REGISTER_TEST(cli, object_list_sort_and_top_combined);

    /* validate commands */
    REGISTER_TEST(cli, validate_all_text);
    REGISTER_TEST(cli, validate_all_json);
    REGISTER_TEST(cli, validate_all_output_file_text);
    REGISTER_TEST(cli, validate_all_output_file_json);
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
    REGISTER_TEST(cli, batch_file_info_output_file_text);
    REGISTER_TEST(cli, batch_validate_all);
    REGISTER_TEST(cli, batch_validate_all_output_file_text);

    /* convert command output redirection */
    REGISTER_TEST(cli, convert_version_output_file_text);
    REGISTER_TEST(cli, convert_version_output_file_json);
    REGISTER_TEST(cli, convert_copy_output_and_report_files);
    REGISTER_TEST(cli, convert_merge_option_values_not_treated_as_files);
    REGISTER_TEST(cli, diff_chunks_object_option_value_not_treated_as_file);

    /* output-open failure handling */
    REGISTER_TEST(cli, file_info_output_open_failure);
    REGISTER_TEST(cli, validate_all_output_open_failure);
    REGISTER_TEST(cli, convert_copy_output_open_failure);

    /* JSON envelope */
    REGISTER_TEST(cli, json_schema_envelope);

    /* help/usage */
    REGISTER_TEST(cli, help_shows_groups);
    REGISTER_TEST(cli, unknown_command_error);
TEST_MAIN_END()
