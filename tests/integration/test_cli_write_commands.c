/**
 * @file test_cli_write_commands.c
 * @brief End-to-end regression coverage for high-risk CLI write commands.
 */

#include "test_framework.h"
#include "write_semantic_probe.h"

#include "../../tools/nmo_cli_common.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
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

static int normalize_cli_exit_code(int status) {
    if (status < 0) return status;
#if !defined(_WIN32)
    if (WIFEXITED(status)) return WEXITSTATUS(status);
#endif
    return status;
}

static cli_run_result_t run_cli_capture(const char *args) {
    cli_run_result_t result = { NULL, -1 };
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", NMO_CLI_PATH, args);

    FILE *pipe = NMO_POPEN(cmd, "r");
    if (!pipe) return result;

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
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
                return result;
            }
            buf = next;
        }
        memcpy(buf + len, chunk, chunk_len);
        len += chunk_len;
    }
    buf[len] = '\0';
    result.exit_code = normalize_cli_exit_code(NMO_PCLOSE(pipe));
    result.output = buf;
    return result;
}

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static void make_dir(const char *path) {
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static int copy_file_binary(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

static bool create_duplicate_camera_name_fixture(const char *path) {
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

    nmo_object_id_t plain_id = 0;
    nmo_object_id_t camera_id = 0;
    bool ok =
        nmo_session_create_object(session, NMO_CID_OBJECT,
                                  "SharedTypedName", (nmo_guid_t){0, 0},
                                  &plain_id, NULL) == NMO_OK &&
        nmo_session_create_object(session, NMO_CID_CAMERA,
                                  "SharedTypedName", (nmo_guid_t){0, 0},
                                  &camera_id, NULL) == NMO_OK &&
        plain_id != 0 && camera_id != 0 &&
        nmo_session_save_file(session, path, NULL, NULL) == NMO_OK;

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return ok;
}

static int write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

static void assert_cli_success(const char *args, const char *contains) {
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code != NMO_CLI_EXIT_SUCCESS ||
        (contains && (!result.output || !strstr(result.output, contains)))) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args, result.exit_code, result.output ? result.output : "(null)");
    }
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    if (contains) {
        ASSERT_STR_CONTAINS(result.output, contains);
    }
    free(result.output);
}

static void assert_cli_failure(const char *args, const char *contains) {
    cli_run_result_t result = run_cli_capture(args);
    if (result.exit_code == NMO_CLI_EXIT_SUCCESS ||
        (contains && (!result.output || !strstr(result.output, contains)))) {
        fprintf(stderr, "\nCommand: %s\nExit: %d\nOutput:\n%s\n",
                args, result.exit_code, result.output ? result.output : "(null)");
    }
    ASSERT_NE(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    if (contains) {
        ASSERT_STR_CONTAINS(result.output, contains);
    }
    free(result.output);
}

static void assert_validate_ok(const char *path) {
    char args[2048];
    snprintf(args, sizeof(args), "validate all \"%s\"", path);
    assert_cli_success(args, "Result: VALID");
}

static void assert_probe_open(write_semantic_probe_t *probe, const char *path) {
    nmo_status_t status = write_probe_open(probe, path);
    if (status != NMO_OK) {
        fprintf(stderr, "Failed to open semantic probe for %s: %d\n", path, status);
    }
    ASSERT_EQ(NMO_OK, status);
}

static void assert_bytes_eq(const void *actual, size_t actual_size,
                            const void *expected, size_t expected_size) {
    ASSERT_NOT_NULL(actual);
    ASSERT_EQ(expected_size, actual_size);
    ASSERT_EQ(0, memcmp(actual, expected, expected_size));
}

static void assert_file_payload_eq(const nmo_included_file_t *file, const char *path) {
    ASSERT_NOT_NULL(file);
    FILE *fp = fopen(path, "rb");
    ASSERT_NOT_NULL(fp);
    ASSERT_EQ(0, fseek(fp, 0, SEEK_END));
    long size = ftell(fp);
    ASSERT_TRUE(size >= 0);
    ASSERT_EQ(0, fseek(fp, 0, SEEK_SET));
    char *buf = (char *)malloc((size_t)size);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ((size_t)size, fread(buf, 1, (size_t)size, fp));
    fclose(fp);
    assert_bytes_eq(file->data, file->size, buf, (size_t)size);
    free(buf);
}

TEST(cli_write, object_data_entity_material_texture_parameter_dry_run_preserves_inputs) {
    make_dir("test_cli_write_tmp");
    remove("test_cli_write_tmp/object_create_dry.nmo");
    remove("test_cli_write_tmp/object_copy_dry.nmo");
    remove("test_cli_write_tmp/object_delete_dry.nmo");
    remove("test_cli_write_tmp/object_set_field_dry.nmo");
    remove("test_cli_write_tmp/data_dry.nmo");
    remove("test_cli_write_tmp/entity_dry.nmo");
    remove("test_cli_write_tmp/material_dry.nmo");
    remove("test_cli_write_tmp/texture_dry.nmo");
    remove("test_cli_write_tmp/parameter_dry.nmo");

    write_semantic_probe_t camera_before;
    assert_probe_open(&camera_before, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    size_t camera_objects = write_probe_object_count(&camera_before);
    const nmo_3dentity_state_t *entity_before =
        (const nmo_3dentity_state_t *)write_probe_state(&camera_before, 2, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(entity_before);
    float entity_pos[3] = {
        entity_before->world_matrix[12],
        entity_before->world_matrix[13],
        entity_before->world_matrix[14],
    };
    const nmo_material_state_t *material_before =
        (const nmo_material_state_t *)write_probe_state(&camera_before, 8, CKPGUID_MATERIAL);
    ASSERT_NOT_NULL(material_before);
    uint32_t diffuse_before = material_before->diffuse_color;
    uint32_t texture_id_before = material_before->texture_ids[0];
    const nmo_texture_state_t *texture_before =
        (const nmo_texture_state_t *)write_probe_state(&camera_before, 7, CKPGUID_TEXTURE);
    ASSERT_NOT_NULL(texture_before);
    int texture_w_before = texture_before->reader_width;
    int texture_h_before = texture_before->reader_height;
    uint32_t camera_resources = write_probe_included_count(&camera_before);
    write_probe_close(&camera_before);

    assert_cli_success(
        "object create --dry-run --class CKGroup --name DryProbe "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/object_create_dry.nmo\"",
        "[dry-run]");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/object_create_dry.nmo"));
    assert_cli_success(
        "object copy --dry-run 1 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/object_copy_dry.nmo\"",
        "[dry-run]");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/object_copy_dry.nmo"));
    assert_cli_success(
        "object delete --dry-run 1 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/object_delete_dry.nmo\"",
        "Dry Run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/object_delete_dry.nmo"));
    assert_cli_success(
        "object set-field --name Cam_Pos parent_id 3 --dry-run "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/object_set_field_dry.nmo\"",
        "parent_id:");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/object_set_field_dry.nmo"));

    assert_cli_success(
        "entity set-position --name Cam_Pos 9 8 7 --dry-run "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/entity_dry.nmo\"",
        "dry-run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/entity_dry.nmo"));

    assert_cli_success(
        "material set --name Interface_Life --diffuse \"(1,0,1,1)\" --dry-run "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/material_dry.nmo\"",
        "dry-run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/material_dry.nmo"));

    assert_cli_success(
        "texture replace --name Button01_special --file "
        "\"" NMO_TEST_DATA_FILE("BBSamples/Narratives/Open File Image.png") "\" --dry-run "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/texture_dry.nmo\"",
        "dry run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/texture_dry.nmo"));

    write_semantic_probe_t camera_after;
    assert_probe_open(&camera_after, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    ASSERT_EQ(camera_objects, write_probe_object_count(&camera_after));
    ASSERT_NULL(write_probe_object_by_name(&camera_after, "DryProbe"));
    const nmo_3dentity_state_t *entity_after =
        (const nmo_3dentity_state_t *)write_probe_state(&camera_after, 2, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(entity_after);
    ASSERT_FLOAT_EQ(entity_pos[0], entity_after->world_matrix[12], 0.0001f);
    ASSERT_FLOAT_EQ(entity_pos[1], entity_after->world_matrix[13], 0.0001f);
    ASSERT_FLOAT_EQ(entity_pos[2], entity_after->world_matrix[14], 0.0001f);
    const nmo_material_state_t *material_after =
        (const nmo_material_state_t *)write_probe_state(&camera_after, 8, CKPGUID_MATERIAL);
    ASSERT_NOT_NULL(material_after);
    ASSERT_EQ(diffuse_before, material_after->diffuse_color);
    ASSERT_EQ(texture_id_before, material_after->texture_ids[0]);
    const nmo_texture_state_t *texture_after =
        (const nmo_texture_state_t *)write_probe_state(&camera_after, 7, CKPGUID_TEXTURE);
    ASSERT_NOT_NULL(texture_after);
    ASSERT_EQ(texture_w_before, texture_after->reader_width);
    ASSERT_EQ(texture_h_before, texture_after->reader_height);
    ASSERT_EQ(camera_resources, write_probe_included_count(&camera_after));
    write_probe_close(&camera_after);

    write_semantic_probe_t data_before_probe;
    assert_probe_open(&data_before_probe, NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
    const nmo_dataarray_state_t *data_before =
        (const nmo_dataarray_state_t *)write_probe_state(&data_before_probe, 2261, CKPGUID_DATAARRAY);
    ASSERT_NOT_NULL(data_before);
    ASSERT_TRUE(data_before->row_count > 0u);
    ASSERT_TRUE(data_before->rows[0].column_count > 1u);
    float data_value_before = data_before->rows[0].cells[1].float_value;
    write_probe_close(&data_before_probe);

    write_semantic_probe_t param_before_probe;
    assert_probe_open(&param_before_probe, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    const nmo_parameter_state_t *param_before = write_probe_parameter_state(&param_before_probe, 5);
    ASSERT_NOT_NULL(param_before);
    ASSERT_TRUE(param_before->buffer_data.count <= 16u);
    uint8_t param_bytes_before[16] = {0};
    uint32_t param_size_before = param_before->buffer_data.count;
    if (param_size_before > 0u) {
        ASSERT_NOT_NULL(param_before->buffer_data.data);
        memcpy(param_bytes_before, param_before->buffer_data.data, param_size_before);
    }
    write_probe_close(&param_before_probe);

    assert_cli_success(
        "data set-cell --name Physicalize_GameBall --row 0 --col 1 --value 0.75 --dry-run "
        "\"" NMO_TEST_DATA_FILE("Ballance/Balls.nmo") "\" "
        "-o \"test_cli_write_tmp/data_dry.nmo\"",
        "dry run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/data_dry.nmo"));

    assert_cli_success(
        "parameter set --dry-run --id 5 1.25 "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/parameter_dry.nmo\"",
        "dry run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/parameter_dry.nmo"));

    write_semantic_probe_t data_after_probe;
    assert_probe_open(&data_after_probe, NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));
    const nmo_dataarray_state_t *data_after =
        (const nmo_dataarray_state_t *)write_probe_state(&data_after_probe, 2261, CKPGUID_DATAARRAY);
    ASSERT_NOT_NULL(data_after);
    ASSERT_FLOAT_EQ(data_value_before, data_after->rows[0].cells[1].float_value, 0.0001f);
    write_probe_close(&data_after_probe);

    write_semantic_probe_t param_after_probe;
    assert_probe_open(&param_after_probe, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    const nmo_parameter_state_t *param_after = write_probe_parameter_state(&param_after_probe, 5);
    ASSERT_NOT_NULL(param_after);
    assert_bytes_eq(param_after->buffer_data.data,
                    param_after->buffer_data.count,
                    param_bytes_before,
                    param_size_before);
    write_probe_close(&param_after_probe);
}

TEST(cli_write, resource_import_replace_dry_run_does_not_write_output) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("BBSamples/3D Transformations/Activate Link.cmo"),
        "test_cli_write_tmp/resource_input.cmo"));
    ASSERT_TRUE(write_text_file("test_cli_write_tmp/payload.txt", "dry run payload\n"));
    remove("test_cli_write_tmp/resource_dry.cmo");

    write_semantic_probe_t before;
    assert_probe_open(&before, "test_cli_write_tmp/resource_input.cmo");
    uint32_t before_count = write_probe_included_count(&before);
    write_probe_close(&before);

    assert_cli_success(
        "resource import --dry-run --name dry.txt "
        "\"test_cli_write_tmp/payload.txt\" \"test_cli_write_tmp/resource_input.cmo\"",
        "dry run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/resource_dry.cmo"));

    assert_cli_success(
        "resource replace --dry-run --index 0 "
        "\"test_cli_write_tmp/payload.txt\" \"test_cli_write_tmp/resource_input.cmo\"",
        "dry run");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/resource_dry.cmo"));

    write_semantic_probe_t after;
    assert_probe_open(&after, "test_cli_write_tmp/resource_input.cmo");
    ASSERT_EQ(before_count, write_probe_included_count(&after));
    ASSERT_NULL(write_probe_included_by_name(&after, "dry.txt"));
    write_probe_close(&after);
}

TEST(cli_write, resource_import_replace_remove_save_and_validate) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("BBSamples/3D Transformations/Activate Link.cmo"),
        "test_cli_write_tmp/resource_input.cmo"));
    ASSERT_TRUE(write_text_file("test_cli_write_tmp/payload.txt", "hello resource\n"));

    write_semantic_probe_t baseline;
    assert_probe_open(&baseline, "test_cli_write_tmp/resource_input.cmo");
    uint32_t baseline_count = write_probe_included_count(&baseline);
    write_probe_close(&baseline);

    assert_cli_success(
        "resource import --name hello.txt "
        "\"test_cli_write_tmp/payload.txt\" \"test_cli_write_tmp/resource_input.cmo\" "
        "-o \"test_cli_write_tmp/resource_import.cmo\"",
        "Imported resource");
    assert_validate_ok("test_cli_write_tmp/resource_import.cmo");
    write_semantic_probe_t imported;
    assert_probe_open(&imported, "test_cli_write_tmp/resource_import.cmo");
    ASSERT_EQ(baseline_count + 1u, write_probe_included_count(&imported));
    const nmo_included_file_t *hello_file =
        write_probe_included_by_name(&imported, "hello.txt");
    ASSERT_NOT_NULL(hello_file);
    ASSERT_STR_EQ("hello.txt", hello_file->name);
    assert_file_payload_eq(hello_file, "test_cli_write_tmp/payload.txt");
    write_probe_close(&imported);

    assert_cli_success(
        "resource replace --index 0 "
        "\"test_cli_write_tmp/payload.txt\" \"test_cli_write_tmp/resource_import.cmo\" "
        "-o \"test_cli_write_tmp/resource_replace.cmo\"",
        "Replaced resource");
    assert_validate_ok("test_cli_write_tmp/resource_replace.cmo");
    write_semantic_probe_t replaced;
    assert_probe_open(&replaced, "test_cli_write_tmp/resource_replace.cmo");
    ASSERT_EQ(baseline_count + 1u, write_probe_included_count(&replaced));
    assert_file_payload_eq(write_probe_included_by_index(&replaced, 0),
                           "test_cli_write_tmp/payload.txt");
    write_probe_close(&replaced);

    assert_cli_success(
        "resource remove --index 0 \"test_cli_write_tmp/resource_replace.cmo\" "
        "-o \"test_cli_write_tmp/resource_remove.cmo\"",
        "Removed resource");
    assert_validate_ok("test_cli_write_tmp/resource_remove.cmo");
    write_semantic_probe_t removed;
    assert_probe_open(&removed, "test_cli_write_tmp/resource_remove.cmo");
    ASSERT_EQ(baseline_count, write_probe_included_count(&removed));
    write_probe_close(&removed);
}

TEST(cli_write, resource_replace_warns_for_texture_named_payload) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Balls.nmo"),
        "test_cli_write_tmp/texture_resource_input.nmo"));

    assert_cli_success(
        "resource import --name BallWood.bmp "
        "\"" NMO_TEST_DATA_FILE("BBSamples/Narratives/Open File Image.png") "\" "
        "\"test_cli_write_tmp/texture_resource_input.nmo\" "
        "-o \"test_cli_write_tmp/texture_resource_import.nmo\"",
        "Imported resource");

    cli_run_result_t result = run_cli_capture(
        "resource replace --index 0 "
        "\"" NMO_TEST_DATA_FILE("BBSamples/Narratives/Open File Image.png") "\" "
        "\"test_cli_write_tmp/texture_resource_import.nmo\" "
        "-o \"test_cli_write_tmp/texture_resource_replace.nmo\"");
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_NOT_NULL(result.output);
    ASSERT_STR_CONTAINS(result.output, "Warning:");
    ASSERT_STR_CONTAINS(result.output, "texture replace");
    free(result.output);

    assert_validate_ok("test_cli_write_tmp/texture_resource_replace.nmo");
}

TEST(cli_write, mesh_animation_import_dry_run_does_not_write_output) {
    make_dir("test_cli_write_tmp");
    make_dir("test_cli_write_tmp/mesh_dry_export");
    make_dir("test_cli_write_tmp/anim_dry_export");

    write_semantic_probe_t mesh_before;
    assert_probe_open(&mesh_before, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));
    const nmo_mesh_state_t *mesh_before_state =
        (const nmo_mesh_state_t *)write_probe_state(&mesh_before, 3, CKPGUID_MESH);
    ASSERT_NOT_NULL(mesh_before_state);
    uint32_t mesh_vertex_count = mesh_before_state->vertex_count;
    uint32_t mesh_face_count = mesh_before_state->face_count;
    size_t mesh_object_count = write_probe_object_count(&mesh_before);
    write_probe_close(&mesh_before);

    assert_cli_success(
        "mesh export --name P_Box_Mesh --out-dir \"test_cli_write_tmp/mesh_dry_export\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\"",
        "Exported");
    remove("test_cli_write_tmp/mesh_dry_out.nmo");
    assert_cli_success(
        "mesh import --dry-run "
        "\"test_cli_write_tmp/mesh_dry_export/P_Box_Mesh_3.obj\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\"",
        "[dry-run]");
    assert_cli_success(
        "mesh import --dry-run "
        "\"test_cli_write_tmp/mesh_dry_export/P_Box_Mesh_3.obj\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "-o \"test_cli_write_tmp/mesh_dry_out.nmo\"",
        "[dry-run]");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/mesh_dry_out.nmo"));

    write_semantic_probe_t mesh_after;
    assert_probe_open(&mesh_after, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));
    const nmo_mesh_state_t *mesh_after_state =
        (const nmo_mesh_state_t *)write_probe_state(&mesh_after, 3, CKPGUID_MESH);
    ASSERT_NOT_NULL(mesh_after_state);
    ASSERT_EQ(mesh_object_count, write_probe_object_count(&mesh_after));
    ASSERT_EQ(mesh_vertex_count, mesh_after_state->vertex_count);
    ASSERT_EQ(mesh_face_count, mesh_after_state->face_count);
    write_probe_close(&mesh_after);

    write_semantic_probe_t anim_before;
    assert_probe_open(&anim_before, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    size_t anim_object_count = write_probe_object_count(&anim_before);
    write_probe_close(&anim_before);

    assert_cli_success(
        "animation export --name Kamera02 --out-dir \"test_cli_write_tmp/anim_dry_export\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\"",
        NULL);
    remove("test_cli_write_tmp/animation_dry_out.nmo");
    assert_cli_success(
        "animation import --dry-run "
        "\"test_cli_write_tmp/anim_dry_export/Kamera02_519.anim.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\"",
        "[dry-run]");
    assert_cli_success(
        "animation import --dry-run "
        "\"test_cli_write_tmp/anim_dry_export/Kamera02_519.anim.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/animation_dry_out.nmo\"",
        "[dry-run]");
    assert_cli_success(
        "animation import --replace-name Kamera02 --dry-run "
        "\"test_cli_write_tmp/anim_dry_export/Kamera02_519.anim.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/animation_dry_out.nmo\"",
        "[dry-run]");
    ASSERT_FALSE(file_exists("test_cli_write_tmp/animation_dry_out.nmo"));

    write_semantic_probe_t anim_after;
    assert_probe_open(&anim_after, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    ASSERT_EQ(anim_object_count, write_probe_object_count(&anim_after));
    ASSERT_NULL(write_probe_object_by_name(&anim_after, "imported_anim"));
    write_probe_close(&anim_after);
}

TEST(cli_write, type_specific_name_selector_skips_duplicate_wrong_class_name) {
    make_dir("test_cli_write_tmp");
    const char *fixture = "test_cli_write_tmp/duplicate_camera_name.nmo";
    remove(fixture);
    ASSERT_TRUE(create_duplicate_camera_name_fixture(fixture));

    cli_run_result_t result = run_cli_capture(
        "entity set-camera --name SharedTypedName --fov 0.75 --dry-run "
        "\"test_cli_write_tmp/duplicate_camera_name.nmo\"");
    ASSERT_NOT_NULL(result.output);
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Camera #");
    free(result.output);

    remove(fixture);
}

TEST(cli_write, object_create_copy_import_delete_save_and_validate) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_cli_write_tmp/object_input.nmo"));

    write_semantic_probe_t baseline;
    assert_probe_open(&baseline, "test_cli_write_tmp/object_input.nmo");
    size_t baseline_count = write_probe_object_count(&baseline);
    write_probe_close(&baseline);

    assert_cli_success(
        "object create --class CKGroup --name ProbeGroup "
        "\"test_cli_write_tmp/object_input.nmo\" -o \"test_cli_write_tmp/object_create.nmo\"",
        "Created object");
    assert_validate_ok("test_cli_write_tmp/object_create.nmo");
    write_semantic_probe_t created;
    assert_probe_open(&created, "test_cli_write_tmp/object_create.nmo");
    nmo_object_t *probe_group = write_probe_object_by_name(&created, "ProbeGroup");
    ASSERT_NOT_NULL(probe_group);
    ASSERT_EQ(NMO_CID_GROUP, probe_group->class_id);
    ASSERT_EQ(baseline_count + 1u, write_probe_object_count(&created));
    write_probe_close(&created);

    assert_cli_success(
        "object copy 1 \"test_cli_write_tmp/object_input.nmo\" "
        "-o \"test_cli_write_tmp/object_copy.nmo\"",
        "Copied 1 object");
    assert_validate_ok("test_cli_write_tmp/object_copy.nmo");
    write_semantic_probe_t copied;
    assert_probe_open(&copied, "test_cli_write_tmp/object_copy.nmo");
    ASSERT_EQ(baseline_count + 1u, write_probe_object_count(&copied));
    ASSERT_EQ(2u, write_probe_count_class_name(&copied, NMO_CID_3DENTITY, "Cam_OrientRef"));
    write_probe_close(&copied);

    ASSERT_TRUE(write_text_file(
        "test_cli_write_tmp/object_import.json",
        "{\"objects\":[{\"id\":1,\"fields\":["
        "{\"name\":\"z_order\",\"kind\":\"scalar\","
        "\"type_guid\":\"{5A5716FD-44E276D7}\",\"value\":7}"
        "]}]}\n"));
    assert_cli_success(
        "object import -f json \"test_cli_write_tmp/object_import.json\" "
        "\"test_cli_write_tmp/object_input.nmo\" -o \"test_cli_write_tmp/object_import.nmo\"",
        "Fields written");
    assert_validate_ok("test_cli_write_tmp/object_import.nmo");
    write_semantic_probe_t imported;
    assert_probe_open(&imported, "test_cli_write_tmp/object_import.nmo");
    const nmo_3dentity_state_t *entity_state =
        (const nmo_3dentity_state_t *)write_probe_state(&imported, 1, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(entity_state);
    ASSERT_EQ(7, entity_state->z_order);
    ASSERT_EQ(baseline_count, write_probe_object_count(&imported));
    write_probe_close(&imported);

    assert_cli_success(
        "object delete --name ProbeGroup \"test_cli_write_tmp/object_create.nmo\" "
        "-o \"test_cli_write_tmp/object_delete.nmo\"",
        "Deleted");
    assert_validate_ok("test_cli_write_tmp/object_delete.nmo");
    write_semantic_probe_t deleted;
    assert_probe_open(&deleted, "test_cli_write_tmp/object_delete.nmo");
    ASSERT_NULL(write_probe_object_by_name(&deleted, "ProbeGroup"));
    ASSERT_EQ(baseline_count, write_probe_object_count(&deleted));
    write_probe_close(&deleted);
}

TEST(cli_write, object_export_import_snapshot_round_trips_mesh_and_matrix) {
    make_dir("test_cli_write_tmp");

    write_semantic_probe_t mesh_baseline;
    assert_probe_open(&mesh_baseline, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));
    const nmo_mesh_state_t *baseline_mesh =
        (const nmo_mesh_state_t *)write_probe_state(&mesh_baseline, 3, CKPGUID_MESH);
    ASSERT_NOT_NULL(baseline_mesh);
    uint32_t baseline_vertices = baseline_mesh->vertex_count;
    uint32_t baseline_faces = baseline_mesh->face_count;
    write_probe_close(&mesh_baseline);

    assert_cli_success(
        "-f json object export --id 3 "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "> \"test_cli_write_tmp/object_mesh_snapshot.json\"",
        NULL);
    ASSERT_TRUE(file_exists("test_cli_write_tmp/object_mesh_snapshot.json"));
    assert_cli_success(
        "object import -f json \"test_cli_write_tmp/object_mesh_snapshot.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "-o \"test_cli_write_tmp/object_mesh_roundtrip.nmo\"",
        "Fields written");
    assert_validate_ok("test_cli_write_tmp/object_mesh_roundtrip.nmo");
    write_semantic_probe_t mesh_probe;
    assert_probe_open(&mesh_probe, "test_cli_write_tmp/object_mesh_roundtrip.nmo");
    const nmo_mesh_state_t *mesh_state =
        (const nmo_mesh_state_t *)write_probe_state(&mesh_probe, 3, CKPGUID_MESH);
    ASSERT_NOT_NULL(mesh_state);
    ASSERT_EQ(baseline_vertices, mesh_state->vertex_count);
    ASSERT_EQ(baseline_faces, mesh_state->face_count);
    ASSERT_EQ(baseline_faces * 3u, mesh_state->face_count * 3u);
    ASSERT_NOT_NULL(mesh_state->face_vertex_indices);
    write_probe_close(&mesh_probe);

    assert_cli_success(
        "-f json object export --id 2 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "> \"test_cli_write_tmp/object_entity_snapshot.json\"",
        NULL);
    ASSERT_TRUE(file_exists("test_cli_write_tmp/object_entity_snapshot.json"));
    assert_cli_success(
        "object import -f json \"test_cli_write_tmp/object_entity_snapshot.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/object_entity_roundtrip.nmo\"",
        "Fields written");
    assert_validate_ok("test_cli_write_tmp/object_entity_roundtrip.nmo");
    write_semantic_probe_t entity_probe;
    assert_probe_open(&entity_probe, "test_cli_write_tmp/object_entity_roundtrip.nmo");
    const nmo_3dentity_state_t *roundtrip_entity =
        (const nmo_3dentity_state_t *)write_probe_state(&entity_probe, 2, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(roundtrip_entity);
    ASSERT_FLOAT_EQ(21.9998798f, roundtrip_entity->world_matrix[12], 0.001f);
    ASSERT_FLOAT_EQ(34.999729f, roundtrip_entity->world_matrix[13], 0.001f);
    ASSERT_FLOAT_EQ(0.00003596f, roundtrip_entity->world_matrix[14], 0.001f);
    write_probe_close(&entity_probe);

    assert_cli_failure(
        "object import-json \"test_cli_write_tmp/object_mesh_snapshot.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" --dry-run",
        "Unknown action");
}

TEST(cli_write, data_entity_material_texture_animation_save_and_validate) {
    make_dir("test_cli_write_tmp");

    assert_cli_success(
        "data set-cell --name Physicalize_GameBall --row 0 --col 1 --value 0.75 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Balls.nmo") "\" "
        "-o \"test_cli_write_tmp/data_out.nmo\"",
        "New:");
    assert_validate_ok("test_cli_write_tmp/data_out.nmo");
    write_semantic_probe_t data_probe;
    assert_probe_open(&data_probe, "test_cli_write_tmp/data_out.nmo");
    const nmo_dataarray_state_t *data_state =
        (const nmo_dataarray_state_t *)write_probe_state(&data_probe, 2261, CKPGUID_DATAARRAY);
    ASSERT_NOT_NULL(data_state);
    ASSERT_TRUE(data_state->row_count > 0u);
    ASSERT_TRUE(data_state->rows[0].column_count > 1u);
    ASSERT_FLOAT_EQ(0.75f, data_state->rows[0].cells[1].float_value, 0.0001f);
    write_probe_close(&data_probe);

    assert_cli_success(
        "entity set-position --name Cam_Pos 1 2 3 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/entity_out.nmo\"",
        "position:");
    assert_validate_ok("test_cli_write_tmp/entity_out.nmo");
    write_semantic_probe_t entity_probe;
    assert_probe_open(&entity_probe, "test_cli_write_tmp/entity_out.nmo");
    const nmo_3dentity_state_t *entity_state =
        (const nmo_3dentity_state_t *)write_probe_state(&entity_probe, 2, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(entity_state);
    ASSERT_FLOAT_EQ(1.0f, entity_state->world_matrix[12], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, entity_state->world_matrix[13], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, entity_state->world_matrix[14], 0.0001f);
    write_probe_close(&entity_probe);

    assert_cli_success(
        "entity set-parent --name Cam_Pos 3 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/entity_parent_out.nmo\"",
        "Saved to");
    assert_validate_ok("test_cli_write_tmp/entity_parent_out.nmo");
    write_semantic_probe_t parent_probe;
    assert_probe_open(&parent_probe, "test_cli_write_tmp/entity_parent_out.nmo");
    const nmo_3dentity_state_t *parent_state =
        (const nmo_3dentity_state_t *)write_probe_state(&parent_probe, 2, CKPGUID_3DENTITY);
    ASSERT_NOT_NULL(parent_state);
    ASSERT_EQ(3u, parent_state->parent_id);
    write_probe_close(&parent_probe);

    assert_cli_success(
        "entity set-camera --name InGameCam --fov 0.875 --near 2.5 --far 3333 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/camera_out.nmo\"",
        "far_plane:");
    assert_validate_ok("test_cli_write_tmp/camera_out.nmo");
    write_semantic_probe_t camera_probe;
    assert_probe_open(&camera_probe, "test_cli_write_tmp/camera_out.nmo");
    const nmo_camera_state_t *camera_state =
        (const nmo_camera_state_t *)write_probe_state(&camera_probe, 5, CKPGUID_CAMERA);
    ASSERT_NOT_NULL(camera_state);
    ASSERT_FLOAT_EQ(0.875f, camera_state->fov, 0.0001f);
    ASSERT_FLOAT_EQ(2.5f, camera_state->near_plane, 0.0001f);
    ASSERT_FLOAT_EQ(3333.0f, camera_state->far_plane, 0.0001f);
    write_probe_close(&camera_probe);

    assert_cli_success(
        "entity set-light 2151 --diffuse \"(0.25,0.5,0.75,1)\" --range 1234.5 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Balls.nmo") "\" "
        "-o \"test_cli_write_tmp/light_out.nmo\"",
        "range:");
    assert_validate_ok("test_cli_write_tmp/light_out.nmo");
    write_semantic_probe_t light_probe;
    assert_probe_open(&light_probe, "test_cli_write_tmp/light_out.nmo");
    const nmo_light_state_t *light_state =
        (const nmo_light_state_t *)write_probe_state(&light_probe, 2151, CKPGUID_LIGHT);
    ASSERT_NOT_NULL(light_state);
    ASSERT_FLOAT_EQ(0.25f, light_state->light_data.diffuse.r, 0.01f);
    ASSERT_FLOAT_EQ(0.5f, light_state->light_data.diffuse.g, 0.01f);
    ASSERT_FLOAT_EQ(0.75f, light_state->light_data.diffuse.b, 0.01f);
    ASSERT_FLOAT_EQ(1234.5f, light_state->light_data.range, 0.001f);
    write_probe_close(&light_probe);

    assert_cli_success(
        "scene set --name \"Scene 1\" --bg-color \"(0.1,0.2,0.3,1)\" "
        "--ambient \"(0.4,0.5,0.6,1)\" --fog-color \"(0.7,0.8,0.9,1)\" "
        "\"" NMO_TEST_DATA_FILE("BBSamples/Narratives/Add To Scene-Remove From Scene.cmo") "\" "
        "-o \"test_cli_write_tmp/scene_out.cmo\"",
        "fog_color:");
    assert_validate_ok("test_cli_write_tmp/scene_out.cmo");
    write_semantic_probe_t scene_probe;
    assert_probe_open(&scene_probe, "test_cli_write_tmp/scene_out.cmo");
    const nmo_scene_state_t *scene_state =
        (const nmo_scene_state_t *)write_probe_state(&scene_probe, 32, CKPGUID_SCENE);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(0xFF1A334Du, scene_state->background_color);
    ASSERT_EQ(0xFF668099u, scene_state->ambient_light_color);
    ASSERT_EQ(0xFFB3CCE6u, scene_state->fog_color);
    write_probe_close(&scene_probe);

    assert_cli_success(
        "material set --name Interface_Life --diffuse \"(1,0,1,1)\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/material_out.nmo\"",
        "diffuse_color:");
    assert_validate_ok("test_cli_write_tmp/material_out.nmo");
    write_semantic_probe_t material_probe;
    assert_probe_open(&material_probe, "test_cli_write_tmp/material_out.nmo");
    const nmo_material_state_t *material_state =
        (const nmo_material_state_t *)write_probe_state(&material_probe, 8, CKPGUID_MATERIAL);
    ASSERT_NOT_NULL(material_state);
    ASSERT_EQ(0xFFFF00FFu, material_state->diffuse_color);
    ASSERT_EQ(7u, material_state->texture_ids[0]);
    write_probe_close(&material_probe);

    write_semantic_probe_t texture_baseline;
    assert_probe_open(&texture_baseline, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    uint32_t texture_baseline_resources = write_probe_included_count(&texture_baseline);
    write_probe_close(&texture_baseline);

    assert_cli_success(
        "texture replace --name Button01_special --file "
        "\"" NMO_TEST_DATA_FILE("BBSamples/Narratives/Open File Image.png") "\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/texture_out.nmo\"",
        "New dims: 256x128");
    assert_validate_ok("test_cli_write_tmp/texture_out.nmo");
    write_semantic_probe_t texture_probe;
    assert_probe_open(&texture_probe, "test_cli_write_tmp/texture_out.nmo");
    const nmo_texture_state_t *texture_state =
        (const nmo_texture_state_t *)write_probe_state(&texture_probe, 7, CKPGUID_TEXTURE);
    ASSERT_NOT_NULL(texture_state);
    ASSERT_EQ(256, texture_state->reader_width);
    ASSERT_EQ(128, texture_state->reader_height);
    ASSERT_TRUE(texture_state->slot_count > 0u);
    ASSERT_NOT_NULL(texture_state->reader_slots);
    ASSERT_EQ(texture_baseline_resources, write_probe_included_count(&texture_probe));
    write_probe_close(&texture_probe);

    make_dir("test_cli_write_tmp/mesh_export");
    write_semantic_probe_t mesh_baseline;
    assert_probe_open(&mesh_baseline, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));
    size_t mesh_baseline_count = write_probe_object_count(&mesh_baseline);
    size_t mesh_baseline_meshes =
        write_probe_count_class_name(&mesh_baseline, NMO_CID_MESH, NULL);
    write_probe_close(&mesh_baseline);
    assert_cli_success(
        "mesh export --name P_Box_Mesh --out-dir \"test_cli_write_tmp/mesh_export\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\"",
        "Exported");
    assert_cli_success(
        "mesh import \"test_cli_write_tmp/mesh_export/P_Box_Mesh_3.obj\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "-o \"test_cli_write_tmp/mesh_out.nmo\"",
        "Imported");
    assert_validate_ok("test_cli_write_tmp/mesh_out.nmo");
    write_semantic_probe_t mesh_probe;
    assert_probe_open(&mesh_probe, "test_cli_write_tmp/mesh_out.nmo");
    ASSERT_EQ(mesh_baseline_count + 1u, write_probe_object_count(&mesh_probe));
    ASSERT_EQ(mesh_baseline_meshes + 1u,
              write_probe_count_class_name(&mesh_probe, NMO_CID_MESH, NULL));
    write_probe_close(&mesh_probe);

    ASSERT_TRUE(write_text_file(
        "test_cli_write_tmp/tiny_replace.obj",
        "o TinyReplace\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n"));
    assert_cli_success(
        "mesh import --replace-name P_Box_Mesh --name TinyReplace "
        "\"test_cli_write_tmp/tiny_replace.obj\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "-o \"test_cli_write_tmp/mesh_replace_out.nmo\"",
        "Imported 3 vertices");
    assert_validate_ok("test_cli_write_tmp/mesh_replace_out.nmo");
    write_semantic_probe_t mesh_replace_probe;
    assert_probe_open(&mesh_replace_probe, "test_cli_write_tmp/mesh_replace_out.nmo");
    const nmo_mesh_state_t *replaced_mesh =
        (const nmo_mesh_state_t *)write_probe_state(&mesh_replace_probe, 3, CKPGUID_MESH);
    ASSERT_NOT_NULL(replaced_mesh);
    ASSERT_EQ(3u, replaced_mesh->vertex_count);
    ASSERT_EQ(1u, replaced_mesh->face_count);
    ASSERT_EQ(1u, replaced_mesh->material_group_count);
    ASSERT_NOT_NULL(replaced_mesh->face_vertex_indices);
    ASSERT_EQ(0u, replaced_mesh->face_vertex_indices[0]);
    ASSERT_EQ(1u, replaced_mesh->face_vertex_indices[1]);
    ASSERT_EQ(2u, replaced_mesh->face_vertex_indices[2]);
    write_probe_close(&mesh_replace_probe);

    write_semantic_probe_t anim_baseline;
    assert_probe_open(&anim_baseline, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    size_t anim_baseline_count = write_probe_object_count(&anim_baseline);
    size_t anim_baseline_anims =
        write_probe_count_class_name(&anim_baseline, NMO_CID_OBJECTANIMATION, NULL);
    write_probe_close(&anim_baseline);
    ASSERT_TRUE(write_text_file(
        "test_cli_write_tmp/import_anim.anim.json",
        "{\n"
        "  \"format\":\"NEWDATA\",\n"
        "  \"entity_id\":520,\n"
        "  \"length\":12.5,\n"
        "  \"flags\":0,\n"
        "  \"controllers\":[{\n"
        "    \"type\":\"0x637c4301\",\n"
        "    \"key_count\":1,\n"
        "    \"key_size\":16,\n"
        "    \"keys\":[{\"time\":0,\"values\":[1,2,3]}]\n"
        "  }]\n"
        "}\n"));
    assert_cli_success(
        "animation import \"test_cli_write_tmp/import_anim.anim.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/animation_out.nmo\"",
        "Created CKObjectAnimation");
    assert_validate_ok("test_cli_write_tmp/animation_out.nmo");
    write_semantic_probe_t anim_probe;
    assert_probe_open(&anim_probe, "test_cli_write_tmp/animation_out.nmo");
    ASSERT_EQ(anim_baseline_count + 1u, write_probe_object_count(&anim_probe));
    ASSERT_EQ(anim_baseline_anims + 1u,
              write_probe_count_class_name(&anim_probe, NMO_CID_OBJECTANIMATION, NULL));
    nmo_object_t *imported_anim = write_probe_object_by_name(&anim_probe, "imported_anim");
    ASSERT_NOT_NULL(imported_anim);
    ASSERT_EQ(NMO_CID_OBJECTANIMATION, imported_anim->class_id);
    const nmo_objectanimation_state_t *anim_state =
        (const nmo_objectanimation_state_t *)write_probe_state(
            &anim_probe, imported_anim->id, CKPGUID_OBJECTANIMATION);
    ASSERT_NOT_NULL(anim_state);
    ASSERT_EQ(CKOBJANIM_FORMAT_NEWDATA, anim_state->format);
    ASSERT_TRUE(anim_state->has_length);
    ASSERT_FLOAT_EQ(12.5f, anim_state->length, 0.0001f);
    ASSERT_EQ(1u, anim_state->controller_count);
    ASSERT_EQ(1u, anim_state->controllers[0].key_count);
    ASSERT_EQ(16u, anim_state->controllers[0].data_size);
    ASSERT_NOT_NULL(anim_state->controllers[0].data);
    write_probe_close(&anim_probe);
}

TEST(cli_write, parameter_set_persists_typed_object_and_raw_values) {
    make_dir("test_cli_write_tmp");

    assert_cli_success(
        "parameter set --id 5 1.25 "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/parameter_float.nmo\"",
        "Parameter #5");
    assert_validate_ok("test_cli_write_tmp/parameter_float.nmo");
    write_semantic_probe_t float_probe;
    assert_probe_open(&float_probe, "test_cli_write_tmp/parameter_float.nmo");
    const nmo_parameter_state_t *float_state = write_probe_parameter_state(&float_probe, 5);
    ASSERT_NOT_NULL(float_state);
    ASSERT_EQ(4u, float_state->buffer_data.count);
    float float_value = 0.0f;
    memcpy(&float_value, float_state->buffer_data.data, sizeof(float_value));
    ASSERT_FLOAT_EQ(1.25f, float_value, 0.0001f);
    char value_buf[64];
    ASSERT_EQ(NMO_OK, write_probe_parameter_value(
        &float_probe, 5, value_buf, sizeof(value_buf)));
    ASSERT_STR_CONTAINS(value_buf, "1.25");
    write_probe_close(&float_probe);

    assert_cli_success(
        "parameter set 46 520 "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/parameter_object.nmo\"",
        "Parameter #46");
    assert_validate_ok("test_cli_write_tmp/parameter_object.nmo");
    write_semantic_probe_t object_probe;
    assert_probe_open(&object_probe, "test_cli_write_tmp/parameter_object.nmo");
    const nmo_parameter_state_t *object_state = write_probe_parameter_state(&object_probe, 46);
    ASSERT_NOT_NULL(object_state);
    ASSERT_EQ(520u, object_state->object_id);
    write_probe_close(&object_probe);

    assert_cli_success(
        "parameter set --hex 64 2A000000 "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/parameter_hex.nmo\"",
        "Parameter #64");
    assert_validate_ok("test_cli_write_tmp/parameter_hex.nmo");
    write_semantic_probe_t hex_probe;
    assert_probe_open(&hex_probe, "test_cli_write_tmp/parameter_hex.nmo");
    const nmo_parameter_state_t *hex_state = write_probe_parameter_state(&hex_probe, 64);
    ASSERT_NOT_NULL(hex_state);
    const unsigned char expected[] = { 0x2A, 0x00, 0x00, 0x00 };
    assert_bytes_eq(hex_state->buffer_data.data, hex_state->buffer_data.count,
                    expected, sizeof(expected));
    write_probe_close(&hex_probe);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli_write, object_data_entity_material_texture_parameter_dry_run_preserves_inputs);
    REGISTER_TEST(cli_write, resource_import_replace_dry_run_does_not_write_output);
    REGISTER_TEST(cli_write, resource_import_replace_remove_save_and_validate);
    REGISTER_TEST(cli_write, resource_replace_warns_for_texture_named_payload);
    REGISTER_TEST(cli_write, mesh_animation_import_dry_run_does_not_write_output);
    REGISTER_TEST(cli_write, type_specific_name_selector_skips_duplicate_wrong_class_name);
    REGISTER_TEST(cli_write, object_create_copy_import_delete_save_and_validate);
    REGISTER_TEST(cli_write, object_export_import_snapshot_round_trips_mesh_and_matrix);
    REGISTER_TEST(cli_write, data_entity_material_texture_animation_save_and_validate);
    REGISTER_TEST(cli_write, parameter_set_persists_typed_object_and_raw_values);
TEST_MAIN_END()
