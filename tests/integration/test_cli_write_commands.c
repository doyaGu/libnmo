/**
 * @file test_cli_write_commands.c
 * @brief End-to-end regression coverage for high-risk CLI write commands.
 */

#include "test_framework.h"

#include "../../tools/nmo_cli_common.h"

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

static int write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

static char *read_text_file_alloc(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
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

TEST(cli_write, resource_import_replace_dry_run_does_not_write_output) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("BBSamples/3D Transformations/Activate Link.cmo"),
        "test_cli_write_tmp/resource_input.cmo"));
    ASSERT_TRUE(write_text_file("test_cli_write_tmp/payload.txt", "dry run payload\n"));
    remove("test_cli_write_tmp/resource_dry.cmo");

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
}

TEST(cli_write, resource_import_replace_remove_save_and_validate) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("BBSamples/3D Transformations/Activate Link.cmo"),
        "test_cli_write_tmp/resource_input.cmo"));
    ASSERT_TRUE(write_text_file("test_cli_write_tmp/payload.txt", "hello resource\n"));

    assert_cli_success(
        "resource import --name hello.txt "
        "\"test_cli_write_tmp/payload.txt\" \"test_cli_write_tmp/resource_input.cmo\" "
        "-o \"test_cli_write_tmp/resource_import.cmo\"",
        "Imported resource");
    assert_validate_ok("test_cli_write_tmp/resource_import.cmo");
    assert_cli_success("resource list \"test_cli_write_tmp/resource_import.cmo\"", "hello.txt");

    assert_cli_success(
        "resource replace --index 0 "
        "\"test_cli_write_tmp/payload.txt\" \"test_cli_write_tmp/resource_import.cmo\" "
        "-o \"test_cli_write_tmp/resource_replace.cmo\"",
        "Replaced resource");
    assert_validate_ok("test_cli_write_tmp/resource_replace.cmo");

    assert_cli_success(
        "resource remove --index 0 \"test_cli_write_tmp/resource_replace.cmo\" "
        "-o \"test_cli_write_tmp/resource_remove.cmo\"",
        "Removed resource");
    assert_validate_ok("test_cli_write_tmp/resource_remove.cmo");
}

TEST(cli_write, mesh_animation_import_dry_run_does_not_write_output) {
    make_dir("test_cli_write_tmp");
    make_dir("test_cli_write_tmp/mesh_dry_export");
    make_dir("test_cli_write_tmp/anim_dry_export");

    assert_cli_success(
        "mesh export --id 3 --out-dir \"test_cli_write_tmp/mesh_dry_export\" "
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

    assert_cli_success(
        "animation export --id 519 --out-dir \"test_cli_write_tmp/anim_dry_export\" "
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
    ASSERT_FALSE(file_exists("test_cli_write_tmp/animation_dry_out.nmo"));
}

TEST(cli_write, object_create_copy_import_delete_save_and_validate) {
    make_dir("test_cli_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_cli_write_tmp/object_input.nmo"));

    assert_cli_success(
        "object create --class CKGroup --name ProbeGroup "
        "\"test_cli_write_tmp/object_input.nmo\" -o \"test_cli_write_tmp/object_create.nmo\"",
        "Created object");
    assert_validate_ok("test_cli_write_tmp/object_create.nmo");
    assert_cli_success("object find --name ProbeGroup \"test_cli_write_tmp/object_create.nmo\"", "ProbeGroup");

    assert_cli_success(
        "object copy 1 \"test_cli_write_tmp/object_input.nmo\" "
        "-o \"test_cli_write_tmp/object_copy.nmo\"",
        "Copied 1 object");
    assert_validate_ok("test_cli_write_tmp/object_copy.nmo");

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
    assert_cli_success("object list-fields 1 \"test_cli_write_tmp/object_import.nmo\"", "z_order");

    assert_cli_success(
        "object delete --name ProbeGroup \"test_cli_write_tmp/object_create.nmo\" "
        "-o \"test_cli_write_tmp/object_delete.nmo\"",
        "Deleted");
    assert_validate_ok("test_cli_write_tmp/object_delete.nmo");
}

TEST(cli_write, object_export_import_snapshot_round_trips_mesh_and_matrix) {
    make_dir("test_cli_write_tmp");

    assert_cli_success(
        "-f json object export --id 3 "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "> \"test_cli_write_tmp/object_mesh_snapshot.json\"",
        NULL);
    ASSERT_TRUE(file_exists("test_cli_write_tmp/object_mesh_snapshot.json"));
    char *mesh_snapshot = read_text_file_alloc("test_cli_write_tmp/object_mesh_snapshot.json");
    ASSERT_NOT_NULL(mesh_snapshot);
    ASSERT_STR_CONTAINS(mesh_snapshot, "\"name\":\"faces\",\"type_guid\":\"{F5E61C19-3DBB4E19}\",\"kind\":\"array\",\"count\":12");
    ASSERT_STR_CONTAINS(mesh_snapshot, "\"name\":\"face_vertex_indices\",\"type_guid\":\"{4E4D4F03-00100000}\",\"kind\":\"array\",\"count\":36");
    ASSERT_STR_CONTAINS(mesh_snapshot, "\"name\":\"vertices\",\"type_guid\":\"{8A1FD901-61A8F154}\",\"kind\":\"array\",\"count\":24");
    ASSERT_STR_CONTAINS(mesh_snapshot, "\"raw_hex\"");
    free(mesh_snapshot);
    assert_cli_success(
        "object import -f json \"test_cli_write_tmp/object_mesh_snapshot.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "-o \"test_cli_write_tmp/object_mesh_roundtrip.nmo\"",
        "Fields written");
    assert_validate_ok("test_cli_write_tmp/object_mesh_roundtrip.nmo");
    assert_cli_success(
        "-f json object export --id 3 \"test_cli_write_tmp/object_mesh_roundtrip.nmo\"",
        "\"items\"");

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
    assert_cli_success(
        "-f json object export --id 2 \"test_cli_write_tmp/object_entity_roundtrip.nmo\"",
        "\"world_matrix\"");

    assert_cli_failure(
        "object import-json \"test_cli_write_tmp/object_mesh_snapshot.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" --dry-run",
        "Unknown action");
}

TEST(cli_write, data_entity_material_texture_animation_save_and_validate) {
    make_dir("test_cli_write_tmp");

    assert_cli_success(
        "data set-cell 2261 --row 0 --col 1 --value 0.75 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Balls.nmo") "\" "
        "-o \"test_cli_write_tmp/data_out.nmo\"",
        "New:");
    assert_validate_ok("test_cli_write_tmp/data_out.nmo");
    assert_cli_success("data dump 2261 \"test_cli_write_tmp/data_out.nmo\"", "0.75");

    assert_cli_success(
        "entity set-position 2 1 2 3 "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/entity_out.nmo\"",
        "position:");
    assert_validate_ok("test_cli_write_tmp/entity_out.nmo");
    assert_cli_success("entity show 2 \"test_cli_write_tmp/entity_out.nmo\"", "(1.00, 2.00, 3.00)");

    assert_cli_success(
        "material set 8 --diffuse \"(1,0,1,1)\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/material_out.nmo\"",
        "diffuse_color:");
    assert_validate_ok("test_cli_write_tmp/material_out.nmo");
    assert_cli_success("material show 8 \"test_cli_write_tmp/material_out.nmo\"", "Diffuse");

    assert_cli_success(
        "texture replace 7 --file "
        "\"" NMO_TEST_DATA_FILE("BBSamples/Narratives/Open File Image.png") "\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "\" "
        "-o \"test_cli_write_tmp/texture_out.nmo\"",
        "New dims: 256x128");
    assert_validate_ok("test_cli_write_tmp/texture_out.nmo");
    assert_cli_success("texture show 7 \"test_cli_write_tmp/texture_out.nmo\"", "256x128");

    make_dir("test_cli_write_tmp/mesh_export");
    assert_cli_success(
        "mesh export --id 3 --out-dir \"test_cli_write_tmp/mesh_export\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\"",
        "Exported");
    assert_cli_success(
        "mesh import \"test_cli_write_tmp/mesh_export/P_Box_Mesh_3.obj\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/P_Box.nmo") "\" "
        "-o \"test_cli_write_tmp/mesh_out.nmo\"",
        "Imported");
    assert_validate_ok("test_cli_write_tmp/mesh_out.nmo");
    assert_cli_success("mesh list \"test_cli_write_tmp/mesh_out.nmo\"", "P_Box_Mesh");

    make_dir("test_cli_write_tmp/anim_export");
    assert_cli_success(
        "animation export --id 519 --out-dir \"test_cli_write_tmp/anim_export\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\"",
        NULL);
    assert_cli_success(
        "animation import \"test_cli_write_tmp/anim_export/Kamera02_519.anim.json\" "
        "\"" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "\" "
        "-o \"test_cli_write_tmp/animation_out.nmo\"",
        "Created CKObjectAnimation");
    assert_validate_ok("test_cli_write_tmp/animation_out.nmo");
    assert_cli_success("animation list \"test_cli_write_tmp/animation_out.nmo\"", "imported_anim");
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli_write, resource_import_replace_dry_run_does_not_write_output);
    REGISTER_TEST(cli_write, resource_import_replace_remove_save_and_validate);
    REGISTER_TEST(cli_write, mesh_animation_import_dry_run_does_not_write_output);
    REGISTER_TEST(cli_write, object_create_copy_import_delete_save_and_validate);
    REGISTER_TEST(cli_write, object_export_import_snapshot_round_trips_mesh_and_matrix);
    REGISTER_TEST(cli_write, data_entity_material_texture_animation_save_and_validate);
TEST_MAIN_END()
