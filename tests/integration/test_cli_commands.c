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

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "app/nmo_save.h"
#include "core/nmo_array.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "type/nmo_type_guids.h"

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

#ifndef NMO_SOURCE_DIR
#define NMO_SOURCE_DIR "."
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

static uint64_t file_size_bytes(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    long size = ftell(fp);
    fclose(fp);
    return size > 0 ? (uint64_t)size : 0;
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
 * completion
 * ============================================================================ */

TEST(cli, completion_bash_matches_generated_file) {
    cli_run_result_t result = run_cli_capture("completion bash");
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_NOT_NULL(result.output);

    char path[1024];
    snprintf(path, sizeof(path), "%s/completions/nmo.bash", NMO_SOURCE_DIR);
    char *expected = read_file_text(path);
    ASSERT_NOT_NULL(expected);
    ASSERT_STR_EQ(expected, result.output);

    free(expected);
    free(result.output);
}

TEST(cli, completion_powershell_alias_matches_generated_file) {
    cli_run_result_t result = run_cli_capture("completion ps1");
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_NOT_NULL(result.output);

    char path[1024];
    snprintf(path, sizeof(path), "%s/completions/nmo.ps1", NMO_SOURCE_DIR);
    char *expected = read_file_text(path);
    ASSERT_NOT_NULL(expected);
    ASSERT_STR_EQ(expected, result.output);

    free(expected);
    free(result.output);
}

/* ============================================================================
 * file info
 * ============================================================================ */

TEST(cli, file_info_text) {
    char args[512];
    snprintf(args, sizeof(args), "file info \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Objects");
    ASSERT_STR_CONTAINS(output, "Managers");
    ASSERT_STR_CONTAINS(output, "CK Version");
    free(output);
}

TEST(cli, file_info_json) {
    char args[512];
    snprintf(args, sizeof(args), "file info \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "file stats \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "-v file stats \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Performance");
    ASSERT_STR_CONTAINS(output, "Load Time");
    free(output);
}

TEST(cli, file_stats_json) {
    char args[512];
    snprintf(args, sizeof(args), "file stats \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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

TEST(cli, global_yaml_format_is_rejected) {
    char args[512];
    snprintf(args, sizeof(args), "-f yaml file info \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Invalid format 'yaml'");
    free(result.output);
}

TEST(cli, file_space_reports_file_and_packed_sizes) {
    const char *path = NMO_TEST_DATA_FILE("Nop.cmo");
    char args[512];
    snprintf(args, sizeof(args), "file space \"%s\"", path);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    uint64_t expected_size = file_size_bytes(path);
    ASSERT_TRUE(expected_size > 0);
    ASSERT_EQ(expected_size, yyjson_get_uint(yyjson_obj_get(data, "file_size")));
    ASSERT_TRUE(yyjson_get_uint(yyjson_obj_get(data, "total_data_size")) > 0);
    ASSERT_TRUE(yyjson_get_uint(yyjson_obj_get(data, "total_pack_size")) > 0);

    yyjson_val *classes = yyjson_obj_get(data, "classes");
    ASSERT_NOT_NULL(classes);
    ASSERT_TRUE(yyjson_arr_size(classes) > 0);
    yyjson_val *first_class = yyjson_arr_get_first(classes);
    ASSERT_TRUE(yyjson_get_uint(yyjson_obj_get(first_class, "pack_size")) > 0);

    yyjson_val *top_objects = yyjson_obj_get(data, "top_objects");
    ASSERT_NOT_NULL(top_objects);
    ASSERT_TRUE(yyjson_arr_size(top_objects) > 0);
    yyjson_val *first_object = yyjson_arr_get_first(top_objects);
    ASSERT_TRUE(yyjson_get_uint(yyjson_obj_get(first_object, "pack_size")) > 0);

    yyjson_doc_free(doc);
}

TEST(cli, file_plugins_resolves_known_behavior_dependencies) {
    char args[512];
    snprintf(args, sizeof(args), "file plugins \"%s\"", NMO_TEST_DATA_FILE("Nop.cmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    uint64_t entry_count = yyjson_get_uint(yyjson_obj_get(data, "entry_count"));
    uint64_t missing_count = yyjson_get_uint(yyjson_obj_get(data, "missing_count"));
    ASSERT_TRUE(entry_count >= 2);
    ASSERT_TRUE(missing_count < entry_count);

    yyjson_val *entries = yyjson_obj_get(data, "entries");
    ASSERT_NOT_NULL(entries);

    int found_nop = 0;
    size_t idx, max;
    yyjson_val *entry;
    yyjson_arr_foreach(entries, idx, max, entry) {
        const char *guid = yyjson_get_str(yyjson_obj_get(entry, "guid"));
        if (guid != NULL && strcmp(guid, "{302561C4-0D282980}") == 0) {
            const char *name = yyjson_get_str(yyjson_obj_get(entry, "name"));
            ASSERT_NOT_NULL(name);
            ASSERT_STR_EQ("Nop", name);
            ASSERT_EQ(0, yyjson_get_uint(yyjson_obj_get(entry, "status_flags")));
            found_nop = 1;
            break;
        }
    }
    ASSERT_TRUE(found_nop);

    yyjson_doc_free(doc);
}

TEST(cli, file_plugins_resolves_known_manager_dependencies) {
    char args[512];
    snprintf(args, sizeof(args), "file plugins \"%s\"", NMO_TEST_DATA_FILE("Demo/Tunnel.cmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *entries = yyjson_obj_get(data, "entries");
    ASSERT_NOT_NULL(entries);

    int found_input = 0;
    int found_collision = 0;
    size_t idx, max;
    yyjson_val *entry;
    yyjson_arr_foreach(entries, idx, max, entry) {
        const char *guid = yyjson_get_str(yyjson_obj_get(entry, "guid"));
        if (guid == NULL) {
            continue;
        }

        if (strcmp(guid, "{F787C904-00000000}") == 0 ||
            strcmp(guid, "{38244712-00000000}") == 0) {
            const char *name = yyjson_get_str(yyjson_obj_get(entry, "name"));
            const char *category_name = yyjson_get_str(yyjson_obj_get(entry, "category_name"));
            ASSERT_NOT_NULL(name);
            ASSERT_NOT_NULL(category_name);
            ASSERT_NOT_NULL(yyjson_obj_get(entry, "category"));
            ASSERT_NOT_NULL(yyjson_obj_get(entry, "required_version"));
            ASSERT_EQ(0, yyjson_get_uint(yyjson_obj_get(entry, "status_flags")));

            if (strcmp(guid, "{F787C904-00000000}") == 0) {
                ASSERT_STR_EQ("DirectX Keyboard/Mouse/Joystick Manager", name);
                ASSERT_STR_EQ("manager", category_name);
                found_input = 1;
            } else {
                ASSERT_STR_EQ("Collision Manager", name);
                ASSERT_STR_EQ("manager", category_name);
                found_collision = 1;
            }
        }
    }
    ASSERT_TRUE(found_input);
    ASSERT_TRUE(found_collision);

    yyjson_doc_free(doc);
}

TEST(cli, file_plugins_resolves_exported_plugin_dependencies) {
    char args[512];
    snprintf(args, sizeof(args), "file plugins \"%s\"", NMO_TEST_DATA_FILE("Demo/Tunnel.cmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(0, yyjson_get_uint(yyjson_obj_get(data, "missing_count")));

    yyjson_val *entries = yyjson_obj_get(data, "entries");
    ASSERT_NOT_NULL(entries);

    int found_wav_reader = 0;
    int found_lod_manager = 0;
    int found_mesh_modifiers = 0;
    int found_lod_options = 0;
    size_t idx, max;
    yyjson_val *entry;
    yyjson_arr_foreach(entries, idx, max, entry) {
        const char *guid = yyjson_get_str(yyjson_obj_get(entry, "guid"));
        if (guid == NULL) {
            continue;
        }
        const char *category_name = yyjson_get_str(yyjson_obj_get(entry, "category_name"));
        const char *name = yyjson_get_str(yyjson_obj_get(entry, "name"));

        if (strcmp(guid, "{61ABC44F-E1233343}") == 0) {
            ASSERT_STR_EQ("sound_reader", category_name);
            ASSERT_STR_EQ("Wav Sound Files", name);
            found_wav_reader = 1;
        } else if (strcmp(guid, "{314F3F83-006F0E30}") == 0) {
            ASSERT_STR_EQ("manager", category_name);
            ASSERT_STR_EQ("Level Of Detail Manager", name);
            found_lod_manager = 1;
        } else if (strcmp(guid, "{68CA037D-6BEF5E1A}") == 0) {
            ASSERT_STR_EQ("behavior", category_name);
            ASSERT_STR_EQ("Mesh Modification building blocks", name);
            found_mesh_modifiers = 1;
        } else if (strcmp(guid, "{2B557187-02027BAF}") == 0) {
            ASSERT_STR_EQ("behavior", category_name);
            ASSERT_STR_EQ("LOD Manager Options", name);
            found_lod_options = 1;
        }
    }

    ASSERT_TRUE(found_wav_reader);
    ASSERT_TRUE(found_lod_manager);
    ASSERT_TRUE(found_mesh_modifiers);
    ASSERT_TRUE(found_lod_options);

    yyjson_doc_free(doc);
}

TEST(cli, file_plugins_resolves_bbsample_plugin_dependencies) {
    char args[512];
    snprintf(args, sizeof(args), "file plugins \"%s\"",
             NMO_TEST_DATA_FILE("BBSamples/3D Transformations/Activate Link.cmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(0, yyjson_get_uint(yyjson_obj_get(data, "missing_count")));

    yyjson_doc_free(doc);
}

TEST(cli, file_plugins_ignores_null_guid_placeholder_dependencies) {
    char args[512];
    snprintf(args, sizeof(args), "file plugins \"%s\"", NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *entries = yyjson_obj_get(data, "entries");
    ASSERT_NOT_NULL(entries);

    size_t idx, max;
    yyjson_val *entry;
    yyjson_arr_foreach(entries, idx, max, entry) {
        const char *guid = yyjson_get_str(yyjson_obj_get(entry, "guid"));
        ASSERT_TRUE(guid == NULL || strcmp(guid, "{00000000-00000000}") != 0);
    }

    yyjson_doc_free(doc);
}

TEST(cli, object_list_fields_json_outputs_envelope) {
    char args[512];
    snprintf(args, sizeof(args), "object list-fields 1 \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "object.list-fields");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(1, yyjson_get_uint(yyjson_obj_get(data, "id")));
    ASSERT_NOT_NULL(yyjson_get_str(yyjson_obj_get(data, "class_name")));
    yyjson_val *fields = yyjson_obj_get(data, "fields");
    ASSERT_NOT_NULL(fields);
    ASSERT_TRUE(yyjson_arr_size(fields) > 0);

    yyjson_val *field = yyjson_arr_get_first(fields);
    ASSERT_NOT_NULL(yyjson_get_str(yyjson_obj_get(field, "name")));
    ASSERT_NOT_NULL(yyjson_get_str(yyjson_obj_get(field, "type")));
    ASSERT_NOT_NULL(yyjson_obj_get(field, "value"));

    yyjson_doc_free(doc);
}

/* ============================================================================
 * object list
 * ============================================================================ */

TEST(cli, object_list_text) {
    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Objects:");
    ASSERT_STR_CONTAINS(output, "InGameCam");
    free(output);
}

TEST(cli, object_list_class_filter) {
    char args[512];
    snprintf(args, sizeof(args), "object list --class CKGroup \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "filtered by class");
    ASSERT_STR_CONTAINS(output, "CKGroup");
    free(output);
}

TEST(cli, object_list_json) {
    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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

/* ============================================================================
 * query and entity commands
 * ============================================================================ */

static bool find_object_id_by_name(
    const char *file_path,
    const char *target_name,
    uint64_t *out_id)
{
    if (out_id == NULL) {
        return false;
    }
    *out_id = 0;

    char args[512];
    snprintf(args, sizeof(args), "object list \"%s\"", file_path);
    yyjson_doc *doc = run_cli_json(args);
    if (doc == NULL) {
        return false;
    }

    yyjson_val *data = json_envelope_data(doc);
    if (data == NULL) {
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_val *objects = yyjson_obj_get(data, "objects");
    if (objects == NULL) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t idx, max;
    yyjson_val *obj;
    yyjson_arr_foreach(objects, idx, max, obj) {
        yyjson_val *name = yyjson_obj_get(obj, "name");
        if (name != NULL && strcmp(yyjson_get_str(name), target_name) == 0) {
            *out_id = yyjson_get_uint(yyjson_obj_get(obj, "id"));
            break;
        }
    }
    yyjson_doc_free(doc);
    return *out_id != 0;
}

static bool create_numeric_name_query_fixture(const char *path) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) {
        return false;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_runtime_report_t report = {0};
    nmo_object_id_t id = 0;
    bool ok = nmo_session_create_object(
        session,
        NMO_CID_OBJECT,
        "424242",
        (nmo_guid_t){0, 0},
        &id,
        &report) == NMO_OK;
    if (ok) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        ok = nmo_save_file(session, path, &save_opts) == NMO_OK;
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return ok;
}

TEST(cli, query_eval_object_id_uses_object_query_lookup) {
    const char *file_path = NMO_TEST_DATA_FILE("Ballance/Camera.nmo");
    uint64_t object_id = 0;
    ASSERT_TRUE(find_object_id_by_name(file_path, "InGameCam", &object_id));

    char args[512];
    snprintf(args, sizeof(args),
             "query eval --object %u has_target \"%s\"",
             (unsigned)object_id,
             file_path);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "query.eval");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *result = yyjson_obj_get(data, "result");
    ASSERT_NOT_NULL(result);
    ASSERT_NOT_NULL(yyjson_get_str(result));
    yyjson_doc_free(doc);
}

TEST(cli, query_eval_numeric_object_selector_does_not_fall_back_to_name) {
    const char *fixture = "test_query_numeric_name_fixture.nmo";
    remove(fixture);
    ASSERT_TRUE(create_numeric_name_query_fixture(fixture));

    char args[512];
    snprintf(args, sizeof(args),
             "query eval --object 424242 has_target \"%s\"",
             fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_EQ(NMO_CLI_EXIT_NOT_FOUND, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    ASSERT_STR_CONTAINS(result.output, "Object not found: 424242");
    ASSERT_FALSE(strstr(result.output, "has no reflection data") != NULL);
    free(result.output);
    remove(fixture);
}

TEST(cli, query_eval_missing_object_name_returns_not_found) {
    const char *file_path = NMO_TEST_DATA_FILE("Ballance/Camera.nmo");
    char args[512];
    snprintf(args, sizeof(args),
             "query eval --object DefinitelyMissingObject has_target \"%s\"",
             file_path);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_EQ(NMO_CLI_EXIT_NOT_FOUND, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    ASSERT_STR_CONTAINS(result.output, "Object not found: DefinitelyMissingObject");
    free(result.output);
}

TEST(cli, query_eval_object_name_uses_object_query_lookup) {
    const char *file_path = NMO_TEST_DATA_FILE("Ballance/Camera.nmo");
    char args[512];
    snprintf(args, sizeof(args),
             "query eval --object InGameCam has_target \"%s\"",
             file_path);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "query.eval");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *result = yyjson_obj_get(data, "result");
    ASSERT_NOT_NULL(result);
    ASSERT_NOT_NULL(yyjson_get_str(result));
    yyjson_doc_free(doc);
}

TEST(cli, entity_list_class_filter_accepts_entity_derived_class) {
    char args[512];
    snprintf(args, sizeof(args),
             "entity list --class CKCamera \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "entity.list");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *count = yyjson_obj_get(data, "count");
    yyjson_val *entities = yyjson_obj_get(data, "entities");
    ASSERT_NOT_NULL(count);
    ASSERT_NOT_NULL(entities);
    ASSERT_TRUE(yyjson_get_uint(count) > 0);
    ASSERT_TRUE(yyjson_arr_size(entities) > 0);
    yyjson_doc_free(doc);
}

TEST(cli, entity_list_class_filter_non_entity_class_returns_empty_result) {
    char args[512];
    snprintf(args, sizeof(args),
             "entity list --class CKMaterial \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "entity.list");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *count = yyjson_obj_get(data, "count");
    yyjson_val *entities = yyjson_obj_get(data, "entities");
    ASSERT_NOT_NULL(count);
    ASSERT_NOT_NULL(entities);
    ASSERT_TRUE(yyjson_get_uint(count) == 0);
    ASSERT_TRUE(yyjson_arr_size(entities) == 0);
    yyjson_doc_free(doc);
}

TEST(cli, validate_all_text) {
    char args[512];
    snprintf(args, sizeof(args), "validate all \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Objects");
    ASSERT_STR_CONTAINS(output, "Errors");
    free(output);
}

TEST(cli, validate_all_json) {
    char args[512];
    snprintf(args, sizeof(args), "validate all \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "validate structure \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Structure Validation");
    ASSERT_STR_CONTAINS(output, "Errors");
    free(output);
}

TEST(cli, validate_structure_fix) {
    char args[512];
    snprintf(args, sizeof(args), "validate structure --fix \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    /* Should run without error (Camera.nmo is valid so no fix suggestions shown) */
    ASSERT_STR_CONTAINS(output, "Structure Validation");
    free(output);
}

TEST(cli, validate_references_text) {
    char args[512];
    snprintf(args, sizeof(args), "validate references \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Reference Validation");
    ASSERT_STR_CONTAINS(output, "Total references");
    free(output);
}

TEST(cli, validate_references_json) {
    char args[512];
    snprintf(args, sizeof(args), "validate references \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    ASSERT_NOT_NULL(yyjson_obj_get(data, "total_references"));
    ASSERT_NOT_NULL(yyjson_obj_get(data, "valid"));

    yyjson_doc_free(doc);
}

static bool create_dangling_reference_fixture(const char *path) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) return false;

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_object_id_t group_id = 0;
    bool ok = false;

    if (nmo_session_create_object(session, NMO_CID_GROUP, "dangling-group",
            (nmo_guid_t){0, 0}, &group_id, NULL) != NMO_OK) {
        goto cleanup;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    if (!group_obj || !group_obj->state) {
        goto cleanup;
    }

    nmo_group_state_t *group_state = (nmo_group_state_t *)group_obj->state;
    nmo_array_clear(&group_state->object_ids);
    if (nmo_array_reserve(&group_state->object_ids, 1) != NMO_OK) {
        goto cleanup;
    }
    nmo_object_id_t *ids = NULL;
    if (nmo_array_extend(&group_state->object_ids, 1, (void **)&ids) != NMO_OK) {
        goto cleanup;
    }
    ids[0] = 99999u;

    nmo_save_options_t save_opts = nmo_save_options_default();
    ok = (nmo_save_file(session, path, &save_opts) == NMO_OK);

cleanup:
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return ok;
}

TEST(cli, validate_references_strict_does_not_fail_during_load) {
    const char *fixture = "test_validate_refs_dangling.nmo";
    remove(fixture);

    ASSERT_TRUE(create_dangling_reference_fixture(fixture));

    char args[512];
    snprintf(args, sizeof(args), "--strict validate references \"%s\"", fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Reference Validation");

    free(result.output);
    remove(fixture);
}

/* ============================================================================
 * type commands
 * ============================================================================ */

TEST(cli, type_list_text) {
    char args[512];
    snprintf(args, sizeof(args), "type list \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    /* Should list registered types */
    ASSERT_STR_CONTAINS(output, "CKObject");
    free(output);
}

TEST(cli, type_list_json) {
    char args[512];
    snprintf(args, sizeof(args), "type list \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"), NMO_TEST_DATA_FILE("Ballance/Menu.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"), NMO_TEST_DATA_FILE("Ballance/Menu.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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

TEST(cli, convert_version_fast_save_json_reports_durability) {
    const char *report_path = "test_cli_convert_version_fast_report.json";
    const char *save_path = "test_cli_convert_version_fast_output.nmo";
    remove(report_path);
    remove(save_path);

    char args[1024];
    snprintf(args, sizeof(args),
             "--output \"%s\" -f json convert version --fast-save -o \"%s\" \"%s\"",
             report_path, save_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");
    ASSERT_TRUE(file_exists(save_path));

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    yyjson_doc *doc = yyjson_read(report, strlen(report), 0);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "convert.version");
    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(data, "save_durability")), "fast");

    yyjson_doc_free(doc);
    free(report);
    free(result.output);
    remove(report_path);
    remove(save_path);
}

TEST(cli, convert_copy_output_and_report_files) {
    const char *report_path = "test_cli_convert_copy_report.txt";
    const char *save_path = "test_cli_convert_copy_output.nmo";
    remove(report_path);
    remove(save_path);

    char args[1024];
    snprintf(args, sizeof(args), "--output \"%s\" convert copy -o \"%s\" \"%s\"",
             report_path, save_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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

TEST(cli, convert_copy_fast_save_json_reports_durability) {
    const char *report_path = "test_cli_convert_copy_fast_report.json";
    const char *save_path = "test_cli_convert_copy_fast_output.nmo";
    remove(report_path);
    remove(save_path);

    char args[1024];
    snprintf(args, sizeof(args),
             "--output \"%s\" -f json convert copy --fast-save -o \"%s\" \"%s\"",
             report_path, save_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_EQ(result.output, "");
    ASSERT_TRUE(file_exists(save_path));

    char *report = read_file_text(report_path);
    ASSERT_NOT_NULL(report);
    yyjson_doc *doc = yyjson_read(report, strlen(report), 0);
    ASSERT_NOT_NULL(doc);
    ASSERT_STR_EQ(json_envelope_command(doc), "convert.copy");
    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(data, "save_durability")), "fast");

    yyjson_doc_free(doc);
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             NMO_TEST_DATA_FILE("Ballance/Menu.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             report_path, save_path, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "object list \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "file classes \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
    snprintf(args, sizeof(args), "file classes --sort=size \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
 * resource list --sort
 * ============================================================================ */

TEST(cli, resource_list_sort_by_size) {
    char args[512];
    snprintf(args, sizeof(args), "resource list --sort=size \"%s\"", NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *resources = yyjson_obj_get(data, "resources");
    if (resources && yyjson_arr_size(resources) >= 2) {
        uint64_t prev = UINT64_MAX;
        size_t idx, max;
        yyjson_val *res;
        yyjson_arr_foreach(resources, idx, max, res) {
            uint64_t sz = yyjson_get_uint(yyjson_obj_get(res, "size"));
            ASSERT_TRUE(sz <= prev);
            prev = sz;
        }
    }
    yyjson_doc_free(doc);
}

TEST(cli, texture_list_uses_slot_dimensions_when_reader_dimensions_missing) {
    char args[512];
    snprintf(args, sizeof(args), "texture list \"%s\"", NMO_TEST_DATA_FILE("Demo/Tunnel.cmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *textures = yyjson_obj_get(data, "textures");
    ASSERT_NOT_NULL(textures);

    int found = 0;
    size_t idx, max;
    yyjson_val *tex;
    yyjson_arr_foreach(textures, idx, max, tex) {
        if (yyjson_get_uint(yyjson_obj_get(tex, "id")) == 783) {
            ASSERT_EQ(256, yyjson_get_uint(yyjson_obj_get(tex, "width")));
            ASSERT_EQ(256, yyjson_get_uint(yyjson_obj_get(tex, "height")));
            found = 1;
            break;
        }
    }
    ASSERT_TRUE(found);
    yyjson_doc_free(doc);
}

/* ============================================================================
 * chunk list --top
 * ============================================================================ */

TEST(cli, chunk_list_top_limits_output) {
    char args[512];
    snprintf(args, sizeof(args), "chunk list --top 5 \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);
    yyjson_val *data = json_envelope_data(doc);
    yyjson_val *chunks = yyjson_obj_get(data, "chunks");
    ASSERT_NOT_NULL(chunks);
    ASSERT_TRUE(yyjson_arr_size(chunks) <= 5);
    yyjson_doc_free(doc);
}

/* ============================================================================
 * JSON schema envelope
 * ============================================================================ */

TEST(cli, json_schema_envelope) {
    char args[512];
    snprintf(args, sizeof(args), "file info \"%s\"", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
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
 * object rename
 * ============================================================================ */

/**
 * Create a minimal NMO test fixture with 2 objects for rename tests.
 * Object 1: "Alpha" (CKObject, class_id=1)
 * Object 2: "Beta"  (CKObject, class_id=1)
 */
static bool create_rename_test_fixture(const char *path) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) return false;

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_runtime_report_t report = {0};
    nmo_object_id_t id1 = 0, id2 = 0;

    if (nmo_session_create_object(session, 1, "Alpha", (nmo_guid_t){0, 0}, &id1, &report) != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }
    if (nmo_session_create_object(session, 1, "Beta", (nmo_guid_t){0, 0}, &id2, &report) != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return false;
    }

    nmo_save_options_t save_opts = nmo_save_options_default();
    int rc = nmo_save_file(session, path, &save_opts);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return rc == NMO_OK;
}

static bool create_parameter_hex_fixture(const char *path, nmo_object_id_t *out_param_id) {
    if (out_param_id == NULL) {
        return false;
    }
    *out_param_id = 0;

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (!ctx) return false;

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        return false;
    }

    nmo_runtime_report_t report = {0};
    nmo_object_id_t param_id = 0;
    bool ok = nmo_session_create_object(
        session,
        NMO_CID_PARAMETER,
        "HexParam",
        (nmo_guid_t){0, 0},
        &param_id,
        &report) == NMO_OK;

    if (ok) {
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        nmo_object_t *param = repo ? nmo_object_repository_find_by_id(repo, param_id) : NULL;
        nmo_parameter_state_t *state = param ? nmo_parameter_get_mutable_state(param) : NULL;
        ok = state != NULL;
        if (ok) {
            state->type_guid = CKPGUID_INT;
            state->mode = CKPARAM_MODE_BUFFER;
            state->has_state = true;
            ok = nmo_array_alloc(&state->buffer_data, sizeof(uint8_t), 4, NULL) == NMO_OK;
        }
        if (ok) {
            uint8_t initial[4] = {0xAA, 0xBB, 0xCC, 0xDD};
            memcpy(state->buffer_data.data, initial, sizeof(initial));
        }
    }

    if (ok) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        ok = nmo_save_file(session, path, &save_opts) == NMO_OK;
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    if (ok) {
        *out_param_id = param_id;
    }
    return ok;
}

TEST(cli, object_rename_json) {
    const char *fixture = "test_rename_fixture.nmo";
    const char *output = "test_rename_output.nmo";
    remove(fixture);
    remove(output);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    /* Find the ID of "Alpha" */
    char list_args[512];
    snprintf(list_args, sizeof(list_args), "object list \"%s\"", fixture);
    yyjson_doc *list_doc = run_cli_json(list_args);
    ASSERT_NOT_NULL(list_doc);
    yyjson_val *list_data = json_envelope_data(list_doc);
    yyjson_val *objects = yyjson_obj_get(list_data, "objects");
    ASSERT_NOT_NULL(objects);

    /* Find Alpha's ID */
    uint64_t alpha_id = 0;
    size_t idx, max;
    yyjson_val *obj;
    yyjson_arr_foreach(objects, idx, max, obj) {
        yyjson_val *name = yyjson_obj_get(obj, "name");
        if (name && strcmp(yyjson_get_str(name), "Alpha") == 0) {
            alpha_id = yyjson_get_uint(yyjson_obj_get(obj, "id"));
            break;
        }
    }
    yyjson_doc_free(list_doc);
    ASSERT_TRUE(alpha_id > 0);

    /* Rename Alpha -> NewAlpha */
    char args[1024];
    snprintf(args, sizeof(args), "object rename %u NewAlpha \"%s\" -o \"%s\"",
             (unsigned)alpha_id, fixture, output);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    const char *cmd = json_envelope_command(doc);
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "object.rename");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(data, "old_name")), "Alpha");
    ASSERT_STR_EQ(yyjson_get_str(yyjson_obj_get(data, "new_name")), "NewAlpha");
    ASSERT_TRUE(yyjson_get_uint(yyjson_obj_get(data, "id")) == alpha_id);
    ASSERT_NOT_NULL(yyjson_obj_get(data, "output"));
    yyjson_doc_free(doc);

    /* Reload output and verify rename took effect */
    char verify_args[512];
    snprintf(verify_args, sizeof(verify_args), "object list \"%s\"", output);
    yyjson_doc *verify_doc = run_cli_json(verify_args);
    ASSERT_NOT_NULL(verify_doc);
    yyjson_val *verify_data = json_envelope_data(verify_doc);
    yyjson_val *verify_objects = yyjson_obj_get(verify_data, "objects");
    ASSERT_NOT_NULL(verify_objects);

    bool found_new = false;
    yyjson_arr_foreach(verify_objects, idx, max, obj) {
        yyjson_val *name = yyjson_obj_get(obj, "name");
        if (name && strcmp(yyjson_get_str(name), "NewAlpha") == 0) {
            found_new = true;
            break;
        }
    }
    ASSERT_TRUE(found_new);
    yyjson_doc_free(verify_doc);

    remove(fixture);
    remove(output);
}

TEST(cli, object_rename_text) {
    const char *fixture = "test_rename_text_fixture.nmo";
    const char *output = "test_rename_text_output.nmo";
    remove(fixture);
    remove(output);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    /* Find Alpha's ID */
    char list_args[512];
    snprintf(list_args, sizeof(list_args), "object list \"%s\"", fixture);
    yyjson_doc *list_doc = run_cli_json(list_args);
    ASSERT_NOT_NULL(list_doc);
    yyjson_val *list_data = json_envelope_data(list_doc);
    yyjson_val *objects = yyjson_obj_get(list_data, "objects");
    uint64_t alpha_id = 0;
    size_t idx, max;
    yyjson_val *obj;
    yyjson_arr_foreach(objects, idx, max, obj) {
        yyjson_val *name = yyjson_obj_get(obj, "name");
        if (name && strcmp(yyjson_get_str(name), "Alpha") == 0) {
            alpha_id = yyjson_get_uint(yyjson_obj_get(obj, "id"));
            break;
        }
    }
    yyjson_doc_free(list_doc);
    ASSERT_TRUE(alpha_id > 0);

    char args[1024];
    snprintf(args, sizeof(args), "object rename %u RenamedAlpha \"%s\" -o \"%s\"",
             (unsigned)alpha_id, fixture, output);
    char *text_output = run_cli(args);
    ASSERT_NOT_NULL(text_output);
    ASSERT_STR_CONTAINS(text_output, "Renamed");
    ASSERT_STR_CONTAINS(text_output, "Alpha");
    ASSERT_STR_CONTAINS(text_output, "RenamedAlpha");
    free(text_output);

    remove(fixture);
    remove(output);
}

TEST(cli, object_rename_name_collision) {
    const char *fixture = "test_rename_collision_fixture.nmo";
    const char *output = "test_rename_collision_output.nmo";
    remove(fixture);
    remove(output);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    /* Find Alpha's and Beta's IDs */
    char list_args[512];
    snprintf(list_args, sizeof(list_args), "object list \"%s\"", fixture);
    yyjson_doc *list_doc = run_cli_json(list_args);
    ASSERT_NOT_NULL(list_doc);
    yyjson_val *list_data = json_envelope_data(list_doc);
    yyjson_val *objects = yyjson_obj_get(list_data, "objects");
    uint64_t alpha_id = 0;
    size_t idx, max;
    yyjson_val *obj;
    yyjson_arr_foreach(objects, idx, max, obj) {
        yyjson_val *name = yyjson_obj_get(obj, "name");
        if (name && strcmp(yyjson_get_str(name), "Alpha") == 0) {
            alpha_id = yyjson_get_uint(yyjson_obj_get(obj, "id"));
            break;
        }
    }
    yyjson_doc_free(list_doc);
    ASSERT_TRUE(alpha_id > 0);

    /* Rename Alpha -> Beta (collision with existing Beta) */
    char args[1024];
    snprintf(args, sizeof(args), "object rename %u Beta \"%s\" -o \"%s\"",
             (unsigned)alpha_id, fixture, output);
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);
    yyjson_val *collision = yyjson_obj_get(data, "name_collision");
    ASSERT_NOT_NULL(collision);
    ASSERT_TRUE(yyjson_get_bool(collision));
    yyjson_doc_free(doc);

    /* File should still be saved */
    ASSERT_TRUE(file_exists(output));

    remove(fixture);
    remove(output);
}

TEST(cli, object_rename_nonexistent_id) {
    const char *fixture = "test_rename_notfound_fixture.nmo";
    const char *output = "test_rename_notfound_output.nmo";
    remove(fixture);
    remove(output);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args), "object rename 9999 NewName \"%s\" -o \"%s\"",
             fixture, output);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "not found");
    free(result.output);

    remove(fixture);
    remove(output);
}

TEST(cli, object_rename_missing_output) {
    const char *fixture = "test_rename_noout_fixture.nmo";
    remove(fixture);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args), "object rename 1 NewName \"%s\"", fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "output");
    free(result.output);

    remove(fixture);
}

TEST(cli, object_delete_batch_name_filter_saves) {
    const char *fixture = "test_delete_batch_fixture.nmo";
    const char *output = "test_delete_batch_output.nmo";
    remove(fixture);
    remove(output);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "--batch object delete --name Alpha \"%s\" -o \"%s\"",
             fixture, output);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Deleted 1 object");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    char verify_args[512];
    snprintf(verify_args, sizeof(verify_args), "object list \"%s\"", output);
    char *verify_output = run_cli(verify_args);
    ASSERT_NOT_NULL(verify_output);
    ASSERT_STR_CONTAINS(verify_output, "Beta");
    ASSERT_TRUE(strstr(verify_output, "Alpha") == NULL);
    free(verify_output);

    remove(fixture);
    remove(output);
}

TEST(cli, object_delete_rejects_empty_dsl_filter) {
    const char *fixture = "test_delete_empty_filter_fixture.nmo";
    remove(fixture);

    ASSERT_TRUE(create_rename_test_fixture(fixture));

    char args[1024];
    snprintf(args, sizeof(args),
             "object delete --dry-run --filter \"\" \"%s\"",
             fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_ARG_ERROR, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "filter");
    free(result.output);

    remove(fixture);
}

TEST(cli, parameter_set_hex_dry_run_does_not_write_output) {
    const char *fixture = "test_parameter_hex_dry_fixture.nmo";
    const char *output = "test_parameter_hex_dry_output.nmo";
    remove(fixture);
    remove(output);

    nmo_object_id_t param_id = 0;
    ASSERT_TRUE(create_parameter_hex_fixture(fixture, &param_id));

    char args[1024];
    snprintf(args, sizeof(args),
             "parameter set --hex --dry-run -o \"%s\" %u 0102 \"%s\"",
             output, (unsigned)param_id, fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "dry run");
    ASSERT_FALSE(file_exists(output));
    free(result.output);

    remove(fixture);
    remove(output);
}

TEST(cli, parameter_set_hex_saves_output) {
    const char *fixture = "test_parameter_hex_save_fixture.nmo";
    const char *output = "test_parameter_hex_save_output.nmo";
    remove(fixture);
    remove(output);

    nmo_object_id_t param_id = 0;
    ASSERT_TRUE(create_parameter_hex_fixture(fixture, &param_id));

    char args[1024];
    snprintf(args, sizeof(args),
             "parameter set --hex -o \"%s\" %u 0102 \"%s\"",
             output, (unsigned)param_id, fixture);
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Saved to");
    ASSERT_TRUE(file_exists(output));
    free(result.output);

    remove(fixture);
    remove(output);
}

/* ============================================================================
 * convert strip --dry-run
 * ============================================================================ */

TEST(cli, convert_strip_dry_run_json) {
    char args[1024];
    snprintf(args, sizeof(args),
             "convert strip --dry-run --name \"Cam_OrientRef\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    yyjson_doc *doc = run_cli_json(args);
    ASSERT_NOT_NULL(doc);

    const char *cmd = json_envelope_command(doc);
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "convert.strip");

    yyjson_val *data = json_envelope_data(doc);
    ASSERT_NOT_NULL(data);

    yyjson_val *dry_run_val = yyjson_obj_get(data, "dry_run");
    ASSERT_NOT_NULL(dry_run_val);
    ASSERT_TRUE(yyjson_get_bool(dry_run_val));

    yyjson_val *match_count = yyjson_obj_get(data, "match_count");
    ASSERT_NOT_NULL(match_count);
    ASSERT_TRUE(yyjson_get_uint(match_count) == 1);

    yyjson_val *matches = yyjson_obj_get(data, "matches");
    ASSERT_NOT_NULL(matches);
    ASSERT_TRUE(yyjson_is_arr(matches));
    ASSERT_TRUE(yyjson_arr_size(matches) == 1);

    yyjson_val *first = yyjson_arr_get_first(matches);
    yyjson_val *name = yyjson_obj_get(first, "name");
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ(yyjson_get_str(name), "Cam_OrientRef");

    yyjson_doc_free(doc);
}

TEST(cli, convert_strip_dry_run_no_file_written) {
    const char *output = "test_strip_dry_run_no_write.nmo";
    remove(output);

    char args[1024];
    snprintf(args, sizeof(args),
             "convert strip --dry-run --name \"Cam_*\" -o \"%s\" \"%s\"",
             output, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);

    /* Dry-run should not write output file */
    ASSERT_FALSE(file_exists(output));

    free(result.output);
    remove(output);
}

TEST(cli, convert_strip_dry_run_no_output_required) {
    /* --dry-run should not require -o */
    char args[1024];
    snprintf(args, sizeof(args),
             "convert strip --dry-run --name \"Cam_*\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);
}

TEST(cli, convert_strip_dry_run_text) {
    char args[1024];
    snprintf(args, sizeof(args),
             "convert strip --dry-run --name \"Cam_OrientRef\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    char *output = run_cli(args);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Dry Run");
    ASSERT_STR_CONTAINS(output, "Cam_OrientRef");
    free(output);
}

TEST(cli, convert_strip_dry_run_no_matches_exit_0) {
    /* --dry-run with no matches should still exit 0 */
    char args[1024];
    snprintf(args, sizeof(args),
             "convert strip --dry-run --name \"NonexistentObject_XYZ_12345\" \"%s\"",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    cli_run_result_t result = run_cli_capture(args);
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    free(result.output);
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
    /* completion */
    REGISTER_TEST(cli, completion_bash_matches_generated_file);
    REGISTER_TEST(cli, completion_powershell_alias_matches_generated_file);

    /* file commands */
    REGISTER_TEST(cli, file_info_text);
    REGISTER_TEST(cli, file_info_json);
    REGISTER_TEST(cli, global_yaml_format_is_rejected);
    REGISTER_TEST(cli, file_info_output_file_text);
    REGISTER_TEST(cli, file_info_output_file_json);
    REGISTER_TEST(cli, file_stats_text);
    REGISTER_TEST(cli, file_stats_verbose);
    REGISTER_TEST(cli, file_stats_json);
    REGISTER_TEST(cli, file_space_reports_file_and_packed_sizes);
    REGISTER_TEST(cli, file_plugins_resolves_known_behavior_dependencies);
    REGISTER_TEST(cli, file_plugins_resolves_known_manager_dependencies);
    REGISTER_TEST(cli, file_plugins_resolves_exported_plugin_dependencies);
    REGISTER_TEST(cli, file_plugins_resolves_bbsample_plugin_dependencies);
    REGISTER_TEST(cli, file_plugins_ignores_null_guid_placeholder_dependencies);
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
    REGISTER_TEST(cli, object_list_fields_json_outputs_envelope);
    REGISTER_TEST(cli, query_eval_object_id_uses_object_query_lookup);
    REGISTER_TEST(cli, query_eval_numeric_object_selector_does_not_fall_back_to_name);
    REGISTER_TEST(cli, query_eval_missing_object_name_returns_not_found);
    REGISTER_TEST(cli, query_eval_object_name_uses_object_query_lookup);
    REGISTER_TEST(cli, entity_list_class_filter_accepts_entity_derived_class);
    REGISTER_TEST(cli, entity_list_class_filter_non_entity_class_returns_empty_result);

    /* validate commands */
    REGISTER_TEST(cli, validate_all_text);
    REGISTER_TEST(cli, validate_all_json);
    REGISTER_TEST(cli, validate_all_output_file_text);
    REGISTER_TEST(cli, validate_all_output_file_json);
    REGISTER_TEST(cli, validate_structure_text);
    REGISTER_TEST(cli, validate_structure_fix);
    REGISTER_TEST(cli, validate_references_text);
    REGISTER_TEST(cli, validate_references_json);
    REGISTER_TEST(cli, validate_references_strict_does_not_fail_during_load);

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
    REGISTER_TEST(cli, convert_version_fast_save_json_reports_durability);
    REGISTER_TEST(cli, convert_copy_output_and_report_files);
    REGISTER_TEST(cli, convert_copy_fast_save_json_reports_durability);
    REGISTER_TEST(cli, convert_merge_option_values_not_treated_as_files);
    REGISTER_TEST(cli, diff_chunks_object_option_value_not_treated_as_file);

    /* output-open failure handling */
    REGISTER_TEST(cli, file_info_output_open_failure);
    REGISTER_TEST(cli, validate_all_output_open_failure);
    REGISTER_TEST(cli, convert_copy_output_open_failure);

    /* object rename */
    REGISTER_TEST(cli, object_rename_json);
    REGISTER_TEST(cli, object_rename_text);
    REGISTER_TEST(cli, object_rename_name_collision);
    REGISTER_TEST(cli, object_rename_nonexistent_id);
    REGISTER_TEST(cli, object_rename_missing_output);
    REGISTER_TEST(cli, object_delete_batch_name_filter_saves);
    REGISTER_TEST(cli, object_delete_rejects_empty_dsl_filter);
    REGISTER_TEST(cli, parameter_set_hex_dry_run_does_not_write_output);
    REGISTER_TEST(cli, parameter_set_hex_saves_output);

    /* convert strip --dry-run */
    REGISTER_TEST(cli, convert_strip_dry_run_json);
    REGISTER_TEST(cli, convert_strip_dry_run_no_file_written);
    REGISTER_TEST(cli, convert_strip_dry_run_no_output_required);
    REGISTER_TEST(cli, convert_strip_dry_run_text);
    REGISTER_TEST(cli, convert_strip_dry_run_no_matches_exit_0);

    /* resource list --sort */
    REGISTER_TEST(cli, resource_list_sort_by_size);
    REGISTER_TEST(cli, texture_list_uses_slot_dimensions_when_reader_dimensions_missing);

    /* chunk list --top */
    REGISTER_TEST(cli, chunk_list_top_limits_output);

    /* JSON envelope */
    REGISTER_TEST(cli, json_schema_envelope);

    /* help/usage */
    REGISTER_TEST(cli, help_shows_groups);
    REGISTER_TEST(cli, unknown_command_error);
TEST_MAIN_END()
