/**
 * @file test_repl_read_commands.c
 * @brief Semantic regression coverage for grouped REPL read commands.
 */

#include "test_framework.h"

#include "../../tools/nmo_cmd_ctx.h"
#include "../../tools/nmo_command_registry.h"
#include "../../tools/nmo_repl_commands.h"
#include "../../tools/nmo_repl_session.h"
#include "../../tools/nmo_repl_util.h"
#include "../../tools/commands/nmo_cmd_chunk.h"
#include "../../tools/commands/nmo_cmd_diff.h"
#include "../../tools/commands/nmo_cmd_file.h"
#include "../../tools/commands/nmo_cmd_object.h"
#include "../../tools/commands/nmo_cmd_parameter.h"
#include "../../tools/commands/nmo_cmd_validate.h"
#include "../../tools/nmo_tool_session.h"
#include "document/nmo_document_file_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define NMO_TEST_DUP _dup
#define NMO_TEST_DUP2 _dup2
#define NMO_TEST_CLOSE _close
#define NMO_TEST_FILENO _fileno
#define NMO_TEST_RMDIR _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#define NMO_TEST_DUP dup
#define NMO_TEST_DUP2 dup2
#define NMO_TEST_CLOSE close
#define NMO_TEST_FILENO fileno
#define NMO_TEST_RMDIR rmdir
#endif

#ifndef NMO_SOURCE_DIR
#define NMO_SOURCE_DIR "."
#endif

static int run_repl_command(nmo_repl_context_t *repl, const char *line) {
    char copy[NMO_REPL_MAX_CMD_LEN];
    char *argv[NMO_REPL_MAX_ARGS];

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    int argc = nmo_repl_parse_command(copy, argv, NMO_REPL_MAX_ARGS);
    if (argc <= 0) {
        return -1;
    }
    return nmo_repl_dispatch_command(repl, argc, argv);
}

static int run_repl_command_capture(nmo_repl_context_t *repl,
                                    const char *line,
                                    const char *path)
{
    fflush(stdout);
    int saved_stdout = NMO_TEST_DUP(NMO_TEST_FILENO(stdout));
    if (saved_stdout < 0) {
        return -1;
    }

    FILE *capture = fopen(path, "wb");
    if (!capture) {
        NMO_TEST_CLOSE(saved_stdout);
        return -1;
    }
    if (NMO_TEST_DUP2(NMO_TEST_FILENO(capture), NMO_TEST_FILENO(stdout)) != 0) {
        NMO_TEST_CLOSE(saved_stdout);
        fclose(capture);
        return -1;
    }

    int rc = run_repl_command(repl, line);

    fflush(stdout);
    if (NMO_TEST_DUP2(saved_stdout, NMO_TEST_FILENO(stdout)) != 0) {
        rc = -1;
    }
    NMO_TEST_CLOSE(saved_stdout);
    fclose(capture);
    return rc;
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

static char *read_source_text(const char *relative_path)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", NMO_SOURCE_DIR, relative_path);
    return read_text_file(path);
}

static void assert_source_not_contains(const char *relative_path,
                                       const char *needle)
{
    char *source = read_source_text(relative_path);
    ASSERT_NOT_NULL(source);
    ASSERT_FALSE(strstr(source, needle) != NULL);
    free(source);
}

static void assert_source_contains(const char *relative_path,
                                   const char *needle)
{
    char *source = read_source_text(relative_path);
    ASSERT_NOT_NULL(source);
    ASSERT_TRUE(strstr(source, needle) != NULL);
    free(source);
}

static void assert_source_missing(const char *relative_path)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", NMO_SOURCE_DIR, relative_path);
    ASSERT_FALSE(file_exists(path));
}

static void close_repl(nmo_repl_context_t *repl) {
    if (!repl) {
        return;
    }
    nmo_tool_close_document(repl->ctx, repl->document, repl->workspace);
    repl->ctx = NULL;
    repl->document = NULL;
    repl->workspace = NULL;
}

static void open_repl(nmo_repl_context_t *repl, const char *path) {
    memset(repl, 0, sizeof(*repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(repl, path, errbuf, sizeof(errbuf)));
    ASSERT_FALSE(repl->dirty);
}

static void assert_read_ok(nmo_repl_context_t *repl, const char *line) {
    int rc = run_repl_command(repl, line);
    if (rc != 0) {
        fprintf(stderr, "REPL read command failed: %s\n", line);
    }
    ASSERT_EQ(0, rc);
    ASSERT_FALSE(repl->dirty);
}

static void assert_read_fails_clean(nmo_repl_context_t *repl, const char *line) {
    ASSERT_NE(0, run_repl_command(repl, line));
    ASSERT_FALSE(repl->dirty);
}

static void assert_read_ok_preserves_dirty(nmo_repl_context_t *repl, const char *line) {
    bool was_dirty = repl->dirty;
    ASSERT_EQ(0, run_repl_command(repl, line));
    ASSERT_EQ(was_dirty, repl->dirty);
}

static void assert_in_session_ok(nmo_repl_context_t *repl,
                                 int (*handler)(nmo_cmd_ctx_t *, int, char **),
                                 int argc,
                                 char **argv)
{
    nmo_cli_global_opts_t global;
    nmo_cli_global_opts_init(&global);

    nmo_cmd_ctx_t cmd;
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS,
              nmo_cmd_ctx_init_with_document(&cmd, repl->ctx, repl->document,
                                             repl->workspace,
                                             "(test document)", &global));
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, handler(&cmd, argc, argv));
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, nmo_cmd_ctx_done(&cmd, NMO_CLI_EXIT_SUCCESS));
    ASSERT_FALSE(repl->dirty);
}

static void assert_registry_in_session_ok(nmo_repl_context_t *repl,
                                          const char *group_name,
                                          int argc,
                                          char **argv)
{
    const nmo_cli_group_t *group =
        nmo_command_registry_find_group(group_name, false);
    ASSERT_NOT_NULL(group);
    ASSERT_TRUE(argc > 0);
    const nmo_cli_action_t *action =
        nmo_command_registry_find_action(group, argv[0], true);
    ASSERT_NOT_NULL(action);

    nmo_cli_global_opts_t global;
    nmo_cli_global_opts_init(&global);

    nmo_cmd_ctx_t cmd;
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS,
              nmo_cmd_ctx_init_with_document(&cmd, repl->ctx, repl->document,
                                             repl->workspace,
                                             "(test document)", &global));
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS,
              nmo_command_registry_dispatch_read_in_session(group, action,
                                                            &cmd, argc, argv));
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, nmo_cmd_ctx_done(&cmd, NMO_CLI_EXIT_SUCCESS));
    ASSERT_FALSE(repl->dirty);
}

static void assert_captured_read_ok_not_contains(nmo_repl_context_t *repl,
                                                 const char *line,
                                                 const char *path,
                                                 const char *forbidden)
{
    remove(path);
    ASSERT_EQ(0, run_repl_command_capture(repl, line, path));
    ASSERT_FALSE(repl->dirty);
    char *output = read_text_file(path);
    ASSERT_NOT_NULL(output);
    ASSERT_FALSE(strstr(output, forbidden) != NULL);
    free(output);
    remove(path);
}

static void assert_captured_read_ok_contains(nmo_repl_context_t *repl,
                                             const char *line,
                                             const char *path,
                                             const char *expected)
{
    remove(path);
    ASSERT_EQ(0, run_repl_command_capture(repl, line, path));
    ASSERT_FALSE(repl->dirty);
    char *output = read_text_file(path);
    ASSERT_NOT_NULL(output);
    ASSERT_TRUE(strstr(output, expected) != NULL);
    free(output);
    remove(path);
}

TEST(repl_read, object_grouped_read_commands_use_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_read_ok(&repl, "object show 2");
    assert_read_ok(&repl, "object show --id 2");
    assert_read_ok(&repl, "object show --name Cam_Pos");
    assert_read_ok(&repl, "object refs 2");

    close_repl(&repl);
}

TEST(repl_read, object_list_fields_uses_full_session_core) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_captured_read_ok_contains(
        &repl,
        "object list-fields 2",
        "test_repl_object_fields.txt",
        "world_matrix");
    assert_captured_read_ok_contains(
        &repl,
        "object list-fields 2",
        "test_repl_object_fields.txt",
        "parent_id");

    close_repl(&repl);
}

TEST(repl_read, object_refgraph_actions_use_session_core) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_captured_read_ok_contains(
        &repl,
        "object impact 46",
        "test_repl_object_impact.txt",
        "Cascade deletion would remove");
    assert_captured_read_ok_contains(
        &repl,
        "object orphans",
        "test_repl_object_orphans.txt",
        "Orphan Analysis");
    assert_captured_read_ok_contains(
        &repl,
        "object cycles",
        "test_repl_object_cycles.txt",
        "Cycle Detection");
    assert_captured_read_ok_contains(
        &repl,
        "object graph",
        "test_repl_object_graph.txt",
        "Reference Graph:");

    close_repl(&repl);
}

TEST(repl_read, behavior_read_actions_use_session_core) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_captured_read_ok_contains(
        &repl,
        "behavior graph 517",
        "test_repl_behavior_graph.txt",
        "Behavior Graph");
    assert_captured_read_ok_contains(
        &repl,
        "behavior graph 517",
        "test_repl_behavior_graph.txt",
        "Nodes:");
    close_repl(&repl);

    open_repl(&repl, NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"));
    assert_captured_read_ok_contains(
        &repl,
        "behavior interface show --name \"Topic - Prevent Collision\"",
        "test_repl_behavior_interface.txt",
        "Interface:");
    assert_captured_read_ok_contains(
        &repl,
        "behavior interface show --name \"Topic - Prevent Collision\"",
        "test_repl_behavior_interface.txt",
        "Sub-behaviors");
    assert_captured_read_ok_contains(
        &repl,
        "behavior interface --name \"Topic - Prevent Collision\"",
        "test_repl_behavior_interface_default.txt",
        "Interface:");

    close_repl(&repl);
}

TEST(repl_read, cli_read_option_values_with_file_suffix_are_not_file_operands) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    ASSERT_EQ(0, run_repl_command(&repl, "object rename 2 ReplSuffixProbe.nmo"));
    ASSERT_TRUE(repl.dirty);
    repl.dirty = false;
    assert_read_ok(&repl, "object show --name ReplSuffixProbe.nmo");

    const char payload[] = "resource suffix probe";
    ASSERT_EQ(NMO_OK,
              nmo_document_add_included_file(repl.document,
                                            "repl_suffix_probe.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    repl.dirty = false;

    remove("test_repl_resource_extract.nmo/repl_suffix_probe.bin");
    NMO_TEST_RMDIR("test_repl_resource_extract.nmo");
    ASSERT_EQ(0, run_repl_command(
                     &repl,
                     "resource extract --out-dir test_repl_resource_extract.nmo --index 0"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_TRUE(file_exists("test_repl_resource_extract.nmo/repl_suffix_probe.bin"));
    remove("test_repl_resource_extract.nmo/repl_suffix_probe.bin");
    NMO_TEST_RMDIR("test_repl_resource_extract.nmo");

    close_repl(&repl);
}

TEST(repl_read, domain_list_class_filter_cannot_be_overridden) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));

    assert_captured_read_ok_contains(
        &repl,
        "mesh list --class CKTexture",
        "test_repl_mesh_list_class_override.txt",
        "CKMesh");

    close_repl(&repl);
}

TEST(repl_read, mesh_and_animation_exports_use_session_core) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));

    remove("test_repl_mesh_export/P_Box_Mesh_3.obj");
    remove("test_repl_mesh_export/P_Box_Mesh_3.mtl");
    NMO_TEST_RMDIR("test_repl_mesh_export");

    ASSERT_EQ(0, run_repl_command(&repl, "mesh export --out-dir test_repl_mesh_export --all"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_TRUE(file_exists("test_repl_mesh_export/P_Box_Mesh_3.obj"));
    ASSERT_TRUE(file_exists("test_repl_mesh_export/P_Box_Mesh_3.mtl"));

    remove("test_repl_mesh_export/P_Box_Mesh_3.obj");
    remove("test_repl_mesh_export/P_Box_Mesh_3.mtl");
    NMO_TEST_RMDIR("test_repl_mesh_export");
    close_repl(&repl);

    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    remove("test_repl_anim_export/Kamera02_519.anim.json");
    remove("test_repl_anim_export/Record Anim_1082.anim.json");
    NMO_TEST_RMDIR("test_repl_anim_export");

    ASSERT_EQ(0, run_repl_command(&repl, "animation export --out-dir test_repl_anim_export --all"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_TRUE(file_exists("test_repl_anim_export/Kamera02_519.anim.json"));
    ASSERT_TRUE(file_exists("test_repl_anim_export/Record Anim_1082.anim.json"));

    remove("test_repl_anim_export/Kamera02_519.anim.json");
    remove("test_repl_anim_export/Record Anim_1082.anim.json");
    NMO_TEST_RMDIR("test_repl_anim_export");
    close_repl(&repl);
}

TEST(repl_read, resource_and_texture_extract_use_session_core) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    const char payload[] = "resource extract probe";
    ASSERT_EQ(NMO_OK,
              nmo_document_add_included_file(repl.document,
                                            "repl_extract_probe.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    repl.dirty = false;

    remove("test_repl_resource_extract/repl_extract_probe.bin");
    NMO_TEST_RMDIR("test_repl_resource_extract");

    ASSERT_EQ(0, run_repl_command(&repl, "resource extract --out-dir test_repl_resource_extract --index 0"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_TRUE(file_exists("test_repl_resource_extract/repl_extract_probe.bin"));

    remove("test_repl_resource_extract/repl_extract_probe.bin");
    NMO_TEST_RMDIR("test_repl_resource_extract");
    close_repl(&repl);

    open_repl(&repl, NMO_TEST_DATA_FILE("Demo/Tunnel.cmo"));
    remove("test_repl_texture_extract/simple lit pink_1061.png");
    NMO_TEST_RMDIR("test_repl_texture_extract");

    ASSERT_EQ(0, run_repl_command(&repl, "texture extract --out-dir test_repl_texture_extract --id 1061 --format png"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_TRUE(file_exists("test_repl_texture_extract/simple lit pink_1061.png"));

    remove("test_repl_texture_extract/simple lit pink_1061.png");
    NMO_TEST_RMDIR("test_repl_texture_extract");
    close_repl(&repl);
}

TEST(repl_read, export_reads_reject_missing_output_directory) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/P_Box.nmo"));

    assert_read_fails_clean(&repl, "mesh export --all");
    close_repl(&repl);

    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    assert_read_fails_clean(&repl, "animation export --all");
    close_repl(&repl);

    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));
    assert_read_fails_clean(&repl, "resource extract");
    close_repl(&repl);

    open_repl(&repl, NMO_TEST_DATA_FILE("Demo/Tunnel.cmo"));
    assert_read_fails_clean(&repl, "texture extract --id 1061");

    close_repl(&repl);
}

TEST(repl_read, domain_show_commands_enforce_family_type) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_read_fails_clean(&repl, "mesh show 2");
    assert_read_fails_clean(&repl, "animation show 2");
    assert_read_fails_clean(&repl, "texture show 2");
    assert_read_fails_clean(&repl, "data show 2");
    assert_read_fails_clean(&repl, "behavior show 2");

    close_repl(&repl);
}

TEST(repl_read, entity_show_uses_entity_session_core) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    remove("test_repl_entity_show_capture.txt");
    ASSERT_EQ(0, run_repl_command_capture(
                     &repl,
                     "entity show 518",
                     "test_repl_entity_show_capture.txt"));
    ASSERT_FALSE(repl.dirty);
    char *output = read_text_file("test_repl_entity_show_capture.txt");
    ASSERT_NOT_NULL(output);
    ASSERT_TRUE(strstr(output, "3D Entity Details") != NULL);
    ASSERT_TRUE(strstr(output, "Current Mesh") != NULL);
    ASSERT_FALSE(strstr(output, "Object Details") != NULL);
    free(output);
    remove("test_repl_entity_show_capture.txt");

    close_repl(&repl);
}

TEST(repl_read, parameter_grouped_read_commands_use_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_read_ok(&repl, "parameter show 46");
    assert_read_ok(&repl, "parameter dump 46");
    assert_read_ok(&repl, "parameter dump --all");

    close_repl(&repl);
}

TEST(repl_read, grouped_read_commands_reject_invalid_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_read_fails_clean(&repl, "object show id:2");
    assert_read_fails_clean(&repl, "object show");
    assert_read_fails_clean(&repl, "object refs --name MissingName");
    assert_read_fails_clean(&repl, "parameter show id:46");
    assert_read_fails_clean(&repl, "parameter dump --type bad-guid --all");
    assert_read_fails_clean(&repl, "file info ../data/Ballance/MenuLevel.nmo");
    assert_read_fails_clean(&repl, "object list ../data/Ballance/MenuLevel.nmo");
    assert_read_fails_clean(&repl, "query eval has_target ../data/Ballance/MenuLevel.nmo");
    assert_read_fails_clean(&repl, "resource info");
    assert_read_fails_clean(&repl, "query script script.nmodsl");
    assert_read_fails_clean(&repl, "query module module.nmodsl");
    assert_read_fails_clean(&repl, "query schema schema.nmodsl");

    close_repl(&repl);
}

TEST(repl_read, mirrored_cli_read_groups_are_available) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));
    char diff_cmd[512];
    snprintf(diff_cmd, sizeof(diff_cmd), "diff summary %s", NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_read_ok(&repl, "file info");
    assert_read_ok(&repl, "file header");
    assert_read_ok(&repl, "chunk list --top 3");
    assert_read_ok(&repl, "chunk tree");
    assert_read_ok(&repl, "object list --top 3");
    assert_read_ok(&repl, "object tree");
    assert_read_ok(&repl, "object find --name Object");
    assert_read_ok(&repl, "object impact 46");
    assert_read_ok(&repl, "object orphans");
    assert_read_ok(&repl, "object cycles");
    assert_read_ok(&repl, "object graph");
    assert_read_ok(&repl, "object list-fields 520");
    assert_read_ok(&repl, "parameter list");
    assert_read_ok(&repl, "behavior list");
    assert_read_ok(&repl, "behavior stats");
    assert_read_ok(&repl, "validate structure");
    assert_read_ok(&repl, diff_cmd);
    assert_read_fails_clean(&repl, "query eval --object 520 has_target");
    assert_read_ok(&repl, "type list");
    assert_read_ok(&repl, "extension list");
    assert_captured_read_ok_contains(
        &repl,
        "extension info",
        "test_repl_extension_info.txt",
        "Extension Metadata");
    assert_captured_read_ok_contains(
        &repl,
        "extension check",
        "test_repl_extension_check.txt",
        "Plugin Dependency Check");
    assert_read_ok(&repl, "completion bash");

    close_repl(&repl);
}

TEST(repl_read, cli_read_mirror_does_not_expose_snapshot_paths) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_captured_read_ok_not_contains(
        &repl,
        "file info",
        "test_repl_file_info_capture.txt",
        "nmo_repl_cli_read");
    assert_captured_read_ok_not_contains(
        &repl,
        "debug load-phases",
        "test_repl_debug_capture.txt",
        "snapshot_");

    close_repl(&repl);
}

TEST(repl_read, cli_wrapper_supports_global_options) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    remove("test_repl_cli_json_capture.txt");
    ASSERT_EQ(0, run_repl_command_capture(
        &repl,
        "cli -f json object list --top 3",
        "test_repl_cli_json_capture.txt"));
    ASSERT_FALSE(repl.dirty);
    char *output = read_text_file("test_repl_cli_json_capture.txt");
    ASSERT_NOT_NULL(output);
    ASSERT_TRUE(strstr(output, "\"command\":\"object.list\"") != NULL);
    free(output);
    remove("test_repl_cli_json_capture.txt");

    remove("test_repl_debug_export.json");
    ASSERT_EQ(0, run_repl_command(&repl, "cli -o test_repl_debug_export.json debug export"));
    ASSERT_FALSE(repl.dirty);
    FILE *f = fopen("test_repl_debug_export.json", "rb");
    ASSERT_NOT_NULL(f);
    fclose(f);
    remove("test_repl_debug_export.json");

    close_repl(&repl);
}

TEST(repl_read, domain_cli_read_groups_are_available) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    const char payload[] = "resource probe";
    ASSERT_EQ(NMO_OK,
              nmo_document_add_included_file(repl.document,
                                            "repl_probe.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    repl.dirty = false;

    assert_read_ok(&repl, "resource list");
    assert_read_ok(&repl, "resource info --index 0");
    assert_read_ok(&repl, "texture list");
    assert_read_ok(&repl, "data list");
    assert_read_ok(&repl, "scene list");
    assert_read_ok(&repl, "entity list");
    assert_read_ok(&repl, "material list");
    assert_read_ok(&repl, "mesh list");
    assert_read_ok(&repl, "animation list");
    assert_read_ok(&repl, "debug load-phases");
    assert_read_ok(&repl, "debug chunks");
    assert_read_ok(&repl, "debug objects");

    close_repl(&repl);
}

TEST(repl_read, cli_read_mirror_uses_current_session_snapshot) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    ASSERT_EQ(0, run_repl_command(&repl, "object rename 2 ReplSnapshotName"));
    ASSERT_TRUE(repl.dirty);
    repl.dirty = false;

    assert_read_ok(&repl, "object show --name ReplSnapshotName");
    assert_read_ok(&repl, "object find --name ReplSnapshotName");
    assert_read_ok(&repl, "file info");
    assert_read_ok(&repl, "validate structure");

    close_repl(&repl);
}

TEST(repl_read, validate_all_reads_dirty_current_session) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    ASSERT_EQ(0, run_repl_command(&repl, "object rename 2 ReplValidateAllName"));
    ASSERT_TRUE(repl.dirty);
    char diff_cmd[512];
    snprintf(diff_cmd, sizeof(diff_cmd), "diff summary %s", NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_read_ok_preserves_dirty(&repl, "validate all");
    assert_read_ok_preserves_dirty(&repl, "cli --strict validate all");
    assert_read_ok_preserves_dirty(&repl, "validate orphans");
    assert_read_ok_preserves_dirty(&repl, "chunk list --top 1");
    assert_read_ok_preserves_dirty(&repl, diff_cmd);

    remove("test_repl_dirty_debug_export.json");
    ASSERT_EQ(0, run_repl_command(&repl, "cli -o test_repl_dirty_debug_export.json debug export"));
    ASSERT_TRUE(repl.dirty);
    FILE *f = fopen("test_repl_dirty_debug_export.json", "rb");
    ASSERT_NOT_NULL(f);
    fclose(f);
    remove("test_repl_dirty_debug_export.json");

    close_repl(&repl);
}

TEST(repl_read, mutating_cli_actions_are_rejected_by_read_mirror) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_read_fails_clean(&repl, "object import -f json missing.json");
    assert_read_fails_clean(&repl, "object set-field 1 name value");
    assert_read_fails_clean(&repl, "resource import file.bin");
    assert_read_fails_clean(&repl, "resource replace 1 file.bin");
    assert_read_fails_clean(&repl, "texture replace 1 image.png");
    assert_read_fails_clean(&repl, "data set-cell 1 0 0 value");
    assert_read_fails_clean(&repl, "scene set 1 --active");
    assert_read_fails_clean(&repl, "entity set-position 1 0 0 0");
    assert_read_fails_clean(&repl, "material set 1 --diffuse 1,1,1");
    assert_read_fails_clean(&repl, "mesh import model.obj");
    assert_read_fails_clean(&repl, "animation import anim.json");
    assert_read_fails_clean(&repl, "extension load plugin.dll");
    assert_read_fails_clean(&repl, "convert copy -o out.nmo");
    assert_read_fails_clean(&repl, "validate orphans --strip -o out.nmo");

    close_repl(&repl);
}

TEST(repl_read, cli_batch_mode_is_rejected) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_read_fails_clean(&repl, "cli --batch object list");
    assert_read_fails_clean(&repl, "cli --batch file info");
    assert_read_fails_clean(&repl, "cli --batch validate all");

    close_repl(&repl);
}

TEST(repl_read, family_repl_read_cores_are_directly_callable) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    char *file_info[] = {"info"};
    char *file_header[] = {"header"};
    char *file_stats[] = {"stats"};
    char *file_classes[] = {"classes"};
    char *file_plugins[] = {"plugins"};
    char *file_space[] = {"space"};
    char *validate_all[] = {"all"};
    char *validate_structure[] = {"structure"};
    char *validate_references[] = {"references"};
    char *validate_resources[] = {"resources"};
    char *validate_orphans[] = {"orphans"};
    char *diff_summary[2] = {"summary", NMO_TEST_DATA_FILE("Ballance/Camera.nmo")};
    char *diff_objects[2] = {"objects", NMO_TEST_DATA_FILE("Ballance/Camera.nmo")};
    char *diff_chunks[2] = {"chunks", NMO_TEST_DATA_FILE("Ballance/Camera.nmo")};
    char *diff_full[2] = {"full", NMO_TEST_DATA_FILE("Ballance/Camera.nmo")};
    char *chunk_list[] = {"list", "--top", "3"};
    char *chunk_tree[] = {"tree"};
    char *chunk_show[] = {"show", "520"};
    char *chunk_find[] = {"find", "--class", "CKGroup"};
    char *object_show[] = {"show", "520"};
    char *object_refs[] = {"refs", "520"};
    char *parameter_show[] = {"show", "46"};
    char *parameter_dump[] = {"dump", "46"};

    assert_in_session_ok(&repl, nmo_cmd_file_in_session, 1, file_info);
    assert_in_session_ok(&repl, nmo_cmd_file_in_session, 1, file_header);
    assert_in_session_ok(&repl, nmo_cmd_file_in_session, 1, file_stats);
    assert_in_session_ok(&repl, nmo_cmd_file_in_session, 1, file_classes);
    assert_in_session_ok(&repl, nmo_cmd_file_in_session, 1, file_plugins);
    assert_in_session_ok(&repl, nmo_cmd_file_in_session, 1, file_space);
    assert_in_session_ok(&repl, nmo_cmd_validate_in_session, 1, validate_all);
    assert_in_session_ok(&repl, nmo_cmd_validate_in_session, 1, validate_structure);
    assert_in_session_ok(&repl, nmo_cmd_validate_in_session, 1, validate_references);
    assert_in_session_ok(&repl, nmo_cmd_validate_in_session, 1, validate_resources);
    assert_in_session_ok(&repl, nmo_cmd_validate_in_session, 1, validate_orphans);
    assert_in_session_ok(&repl, nmo_cmd_diff_in_session, 2, diff_summary);
    assert_in_session_ok(&repl, nmo_cmd_diff_in_session, 2, diff_objects);
    assert_in_session_ok(&repl, nmo_cmd_diff_in_session, 2, diff_chunks);
    assert_in_session_ok(&repl, nmo_cmd_diff_in_session, 2, diff_full);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 3, chunk_list);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 1, chunk_tree);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 2, chunk_show);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 3, chunk_find);
    assert_in_session_ok(&repl, nmo_cmd_object_in_session, 2, object_show);
    assert_in_session_ok(&repl, nmo_cmd_object_in_session, 2, object_refs);
    assert_in_session_ok(&repl, nmo_cmd_parameter_in_session, 2, parameter_show);
    assert_in_session_ok(&repl, nmo_cmd_parameter_in_session, 2, parameter_dump);

    close_repl(&repl);
}

TEST(repl_read, all_read_session_groups_have_family_dispatchers) {
    size_t group_count = 0;
    const nmo_cli_group_t *groups =
        nmo_command_registry_get_groups(&group_count);
    ASSERT_NOT_NULL(groups);

    for (size_t i = 0; i < group_count; i++) {
        const nmo_cli_group_t *group = &groups[i];
        bool has_session_read = false;
        for (size_t j = 0; j < group->action_count; j++) {
            if (group->actions[j].repl_policy == NMO_REPL_ACTION_READ_SESSION) {
                has_session_read = true;
                break;
            }
        }
        if (has_session_read) {
            ASSERT_NOT_NULL(group->repl_session_handler);
        }
    }
}

TEST(repl_read, repl_does_not_export_read_fallback_probes) {
    assert_source_not_contains("tools/nmo_repl_commands.h",
                               "nmo_repl_cli_read_session_public_fallback_count");
    assert_source_not_contains("tools/nmo_repl_commands.c",
                               "nmo_repl_cli_read_session_public_fallback_count");
    assert_source_not_contains("tools/nmo_repl_commands.h",
                               "nmo_repl_cli_read_generic_session_count_for_group");
    assert_source_not_contains("tools/nmo_repl_commands.c",
                               "nmo_repl_cli_read_generic_session_count_for_group");
}

TEST(repl_read, no_borrowed_session_adapter_symbols_remain) {
    assert_source_not_contains("tools/nmo_cli_common.h", "borrowed_session");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_in_session_dispatch_with_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.c", "borrowed_session");
    assert_source_not_contains("tools/nmo_command_registry.c",
                               "nmo_cmd_in_session_dispatch_with_source");
}

TEST(repl_read, no_remaining_repl_read_placeholder_strings) {
    assert_source_not_contains("tools/commands/nmo_cmd_behavior.c",
                               "Behavior Interface");
    assert_source_not_contains("tools/commands/nmo_cmd_mesh.c",
                               "Mesh export from current session");
    assert_source_not_contains("tools/commands/nmo_cmd_animation.c",
                               "Animation export from current session");
    assert_source_not_contains("tools/commands/nmo_cmd_object.c",
                               "digraph nmo_refs");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior.c",
                               "digraph behavior");
    assert_source_not_contains("tools/commands/nmo_cmd_resource.c",
                               "Resource extraction from current session");
    assert_source_not_contains("tools/commands/nmo_cmd_texture.c",
                               "Texture extraction from current session");
}

TEST(repl_read, script_run_uses_executor_handles_only) {
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "result_handles");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "result_handle_count");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_add_operation_json");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_count_report_impacts");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_operation_t");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_append_operation");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "operation_capacity");
}

TEST(repl_read, script_run_does_not_keep_script_edit_report) {
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "nmo_script_edit_report_t report");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "&args->report");
}

TEST(repl_read, script_run_does_not_keep_private_validation) {
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "args->validation");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_collect_validation");
}

TEST(repl_read, script_run_uses_executor_report_directly) {
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_accumulate_edit_report");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_copy_operation_result");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_accumulate_semantic_risks");
}

TEST(repl_read, script_write_commands_use_edit_report_validation_only) {
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_validation_t");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "common->validation");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_collect_validation");
}

TEST(repl_read, edit_report_json_does_not_emit_legacy_risk_level) {
    assert_source_not_contains("tools/nmo_edit_report_json.h",
                               "include_risk_level");
    assert_source_not_contains("tools/nmo_edit_report_json.c",
                               "risk_level");
}

TEST(repl_read, edit_report_schema_v2_has_single_json_helper) {
    assert_source_contains("tools/nmo_edit_report_json.h",
                           "nmo_cli_edit_report_add_schema_v2_json");
    assert_source_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                           "nmo_cli_edit_report_add_schema_v2_json");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "nmo_cli_edit_report_add_schema_v2_json");
    assert_source_contains("tools/commands/nmo_cmd_script.c",
                           "nmo_cli_edit_report_add_schema_v2_json");
    assert_source_contains("tools/commands/nmo_cmd_debug.c",
                           "nmo_cli_edit_report_add_schema_v2_json");
}

TEST(repl_read, behavior_rewrite_reuses_semantic_risk_json_helper) {
    assert_source_contains("tools/nmo_edit_report_json.h",
                           "nmo_cli_edit_report_add_semantic_risk_array_json");
    assert_source_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                           "nmo_cli_edit_report_add_semantic_risk_array_json");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                               "static const char *semantic_risk_severity_string");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                               "static void add_semantic_risks_json");
}

TEST(repl_read, behavior_rewrite_does_not_emit_private_write_reports) {
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                               "static void add_common_write_report_json");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                               "static void add_changed_object_ids_json");
}

TEST(repl_read, patch_manifest_does_not_keep_parallel_operation_array) {
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_operation_t");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "PATCH_OP_");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_operation_add_to_edit_plan");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_operation_t *operations");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "plan->operations");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "plan->operation_count");
}

TEST(repl_read, patch_interface_policy_parses_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_interface_policy(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "nmo_edit_plan_add_interface_policy(");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "edit_plan, (nmo_object_id_t)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_interface_policy(op_obj, &operation)");
}

TEST(repl_read, patch_set_data_cell_parses_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_set_data_cell(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "nmo_edit_plan_add_data_cell(");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_set_data_cell(op_obj, &operation)");
}

TEST(repl_read, patch_operation_edits_parse_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_add_operation(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_remove_operation(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_rewire_operation(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_add_operation(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_remove_operation(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_rewire_operation(op_obj, &operation)");
}

TEST(repl_read, patch_io_edits_parse_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_add_io(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_remove_io(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_rename_io(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_add_io(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_remove_io(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_rename_io(op_obj, &operation)");
}

TEST(repl_read, patch_behavior_link_edits_parse_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_add_behavior_link(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_rewire_behavior_link(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_set_behavior_link_delay(");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "op_obj, out_plan->edit_plan");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_remove_behavior_link(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_add_behavior_link(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_rewire_behavior_link(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_set_behavior_link_delay(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_remove_behavior_link(op_obj, &operation)");
}

TEST(repl_read, patch_parameter_edits_parse_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_add_parameter(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_disconnect_parameter(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_connect_parameter(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_remove_parameter(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_set_parameter_value(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_set_parameter_bytes(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_add_parameter(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_disconnect_parameter(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_connect_parameter(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_remove_parameter(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_set_parameter_value(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_set_parameter_bytes(op_obj, &operation)");
}

TEST(repl_read, patch_node_edits_parse_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_add_node(op_obj, out_plan->edit_plan)");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_remove_node(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_add_node(op_obj, &operation)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_remove_node(op_obj, &operation)");
}

TEST(repl_read, patch_replace_bb_parses_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_replace_bb(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_replace_bb(op_obj, &operation)");
}

TEST(repl_read, patch_fold_parses_into_edit_plan) {
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "patch_parse_fold(op_obj, out_plan->edit_plan)");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "patch_parse_fold(op_obj, &operation)");
}

TEST(repl_read, lua_fold_maps_accept_patch_id_aliases) {
    assert_source_contains("include/lua/nmo_lua_fold_map_parser.h",
                           "nmo_lua_fold_map_parse");
    assert_source_contains("src/lua/lua_fold_map_parser.c",
                           "\"old_io_id\"");
    assert_source_contains("src/lua/lua_fold_map_parser.c",
                           "\"new_io_id\"");
    assert_source_contains("src/lua/lua_fold_map_parser.c",
                           "\"old_parameter_id\"");
    assert_source_contains("src/lua/lua_fold_map_parser.c",
                           "\"new_parameter_id\"");
    assert_source_contains("src/lua/lua_bindings_plan.c",
                           "nmo_lua_fold_map_parse(");
    assert_source_contains("tools/commands/nmo_cmd_script.c",
                           "nmo_lua_fold_map_parse(");
    assert_source_not_contains("src/lua/lua_bindings_plan.c",
                               "nmo_lua_plan_parse_fold_maps");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "script_run_lua_parse_fold_maps");
}

TEST(repl_read, replace_bb_semantic_risks_merge_into_edit_report) {
    assert_source_contains("src/behavior/behavior_rewrite.c",
                           "nmo_behavior_edit_collect_semantic_risks(");
    assert_source_contains("src/behavior/edit_plan.c",
                           "replace_report.semantic_risks");
    assert_source_contains("src/behavior/edit_plan.c",
                           "edit_report_note_semantic_risks(");
}

TEST(repl_read, patch_uses_schema_v2_output_path) {
    assert_source_contains("include/behavior/nmo_edit_plan.h",
                           "output_path");
    assert_source_contains("tools/nmo_edit_report_json.c",
                           "\"output_path\"");
    assert_source_contains("tools/commands/nmo_cmd_patch.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_patch.c",
                               "\"output_path\"");
}

TEST(repl_read, script_uses_schema_v2_output_path) {
    assert_source_contains("tools/commands/nmo_cmd_script.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_script.c",
                               "\"output_path\"");
}

TEST(repl_read, behavior_rewrite_uses_schema_v2_output_path) {
    assert_source_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_rewrite.c",
                               "\"output_path\"");
}

TEST(repl_read, behavior_link_uses_schema_v2_output_path) {
    assert_source_contains("tools/commands/nmo_cmd_behavior_link.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_link.c",
                               "\"output_path\"");
}

TEST(repl_read, parameter_set_uses_schema_v2_output_path) {
    assert_source_contains("tools/commands/nmo_cmd_parameter.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "\"output_path\"");
}

TEST(repl_read, data_set_cell_uses_schema_v2_output_path) {
    assert_source_contains("tools/commands/nmo_cmd_data.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_data.c",
                               "\"output_path\"");
}

TEST(repl_read, debug_probe_uses_schema_v2_output_path) {
    assert_source_contains("tools/commands/nmo_cmd_debug.c",
                           "nmo_edit_report_set_output_path");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.c",
                               "\"output_path\"");
}

TEST(repl_read, debug_probe_uses_kind_spec_table) {
    assert_source_contains("tools/commands/nmo_cmd_debug.c",
                           "debug_probe_kind_specs");
    assert_source_contains("tools/commands/nmo_cmd_debug.c",
                           "debug_probe_find_kind");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.c",
                               "static const char *debug_probe_input_handle");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.c",
                               "static const char *debug_probe_output_handle");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.c",
                               "static const char *debug_probe_text_handle");
}

TEST(repl_read, behavior_link_commands_use_edit_executor) {
    assert_source_contains("tools/commands/nmo_cmd_behavior_link.c",
                           "nmo_edit_executor_execute");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_link.c",
                               "nmo_script_edit_begin");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_link.c",
                               "nmo_script_edit_add_behavior_link");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior_link.c",
                               "nmo_script_edit_remove_behavior_link");
}

TEST(repl_read, parameter_set_uses_edit_executor) {
    assert_source_contains("tools/commands/nmo_cmd_parameter.c",
                           "nmo_edit_executor_execute");
    assert_source_contains("tools/commands/nmo_cmd_parameter.c",
                           "nmo_cli_edit_report_add_schema_v2_json");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "nmo_script_edit_begin");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "nmo_script_edit_set_parameter_value");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "nmo_script_edit_set_parameter_bytes");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "nmo_workspace_edit_begin(c->workspace, \"parameter set\"");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "nmo_object_edit_set_parameter_value");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.c",
                               "nmo_object_edit_set_parameter_bytes");
}

TEST(repl_read, data_set_cell_uses_edit_executor) {
    assert_source_contains("tools/commands/nmo_cmd_data.c",
                           "nmo_edit_executor_execute");
    assert_source_contains("tools/commands/nmo_cmd_data.c",
                           "nmo_cli_edit_report_add_schema_v2_json");
    assert_source_not_contains("tools/commands/nmo_cmd_data.c",
                               "nmo_workspace_edit_begin(c->workspace, \"data set-cell\"");
    assert_source_not_contains("tools/commands/nmo_cmd_data.c",
                               "nmo_object_edit_set_dataarray_cell");
}

TEST(repl_read, domain_session_dispatchers_do_not_construct_object_argv) {
    assert_source_not_contains("tools/commands/nmo_cmd_entity.c",
                               "nmo_cmd_object_show_in_session(ctx");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior.c",
                               "nmo_cmd_object_in_session(ctx");
    assert_source_not_contains("tools/commands/nmo_cmd_mesh.c",
                               "nmo_cmd_object_in_session(ctx");
    assert_source_not_contains("tools/commands/nmo_cmd_animation.c",
                               "nmo_cmd_object_in_session(ctx");
    assert_source_not_contains("tools/commands/nmo_cmd_texture.c",
                               "nmo_cmd_object_in_session(ctx");
    assert_source_not_contains("tools/commands/nmo_cmd_data.c",
                               "nmo_cmd_object_in_session(ctx");
}

TEST(repl_read, read_family_headers_only_export_family_session_entrypoints) {
    assert_source_not_contains("tools/commands/nmo_cmd_file.h",
                               "nmo_cmd_file_info_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_file.h",
                               "nmo_cmd_file_header_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_file.h",
                               "nmo_cmd_file_stats_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_file.h",
                               "nmo_cmd_file_classes_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_file.h",
                               "nmo_cmd_file_plugins_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_file.h",
                               "nmo_cmd_file_space_in_session");

    assert_source_not_contains("tools/commands/nmo_cmd_debug.h",
                               "nmo_cmd_debug_load_phases_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.h",
                               "nmo_cmd_debug_chunks_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.h",
                               "nmo_cmd_debug_objects_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_debug.h",
                               "nmo_cmd_debug_export_in_session");

    assert_source_not_contains("tools/commands/nmo_cmd_validate.h",
                               "nmo_cmd_validate_all_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_validate.h",
                               "nmo_cmd_validate_structure_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_validate.h",
                               "nmo_cmd_validate_references_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_validate.h",
                               "nmo_cmd_validate_resources_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_validate.h",
                               "nmo_cmd_validate_orphans_in_session");

    assert_source_not_contains("tools/commands/nmo_cmd_diff.h",
                               "nmo_cmd_diff_summary_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_diff.h",
                               "nmo_cmd_diff_objects_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_diff.h",
                               "nmo_cmd_diff_chunks_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_diff.h",
                               "nmo_cmd_diff_full_in_session");

    assert_source_missing("tools/commands/nmo_cmd_query.h");
    assert_source_not_contains("tools/commands/nmo_cmd_object.h",
                               "nmo_cmd_object_show_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_object.h",
                               "nmo_cmd_object_show_class_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_object.h",
                               "nmo_cmd_object_list_class_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_object.h",
                               "nmo_cmd_object_find_class_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_object.h",
                               "nmo_cmd_object_refs_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_object.h",
                               "nmo_cmd_object_refgraph_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.h",
                               "nmo_cmd_parameter_show_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_parameter.h",
                               "nmo_cmd_parameter_dump_in_session");

    assert_source_not_contains("tools/commands/nmo_cmd_resource.h",
                               "nmo_cmd_resource_extract_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_texture.h",
                               "nmo_cmd_texture_extract_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_mesh.h",
                               "nmo_cmd_mesh_export_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_animation.h",
                               "nmo_cmd_animation_export_in_session");

    assert_source_not_contains("tools/commands/nmo_cmd_behavior.h",
                               "nmo_cmd_behavior_graph_in_session");
    assert_source_not_contains("tools/commands/nmo_cmd_behavior.h",
                               "nmo_cmd_behavior_interface_in_session");
}

TEST(repl_read, no_active_session_adapter_symbols_remain) {
    assert_source_not_contains("tools/nmo_cmd_ctx.c", "g_active_session_ctx");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_ctx_dispatch_with_session");
    assert_source_not_contains("tools/nmo_cmd_ctx.c",
                               "nmo_cmd_ctx_dispatch_with_session");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_ctx_resolve_active_session");
    assert_source_not_contains("tools/nmo_cmd_ctx.c",
                               "nmo_cmd_ctx_resolve_active_session");
}

TEST(repl_read, command_source_is_not_a_global_cli_option) {
    assert_source_not_contains("tools/nmo_cli_common.h", "command_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.c", "global->command_source");
    assert_source_not_contains("tools/commands/nmo_cmd_chunk.c", "global->command_source");
    assert_source_not_contains("tools/commands/nmo_cmd_diff.c", "global->command_source");
    assert_source_not_contains("tools/commands/nmo_cmd_object.c", "global->command_source");
    assert_source_not_contains("tools/commands/nmo_cmd_object_refs.c", "global->command_source");
}

TEST(repl_read, chunk_session_dispatch_does_not_use_ctx_dispatch_helper) {
    assert_source_not_contains("tools/commands/nmo_cmd_chunk.c",
                               "nmo_cmd_ctx_dispatch_from_source");
    assert_source_not_contains("tools/commands/nmo_cmd_chunk.c",
                               "nmo_cmd_public_handler_t");
    assert_source_not_contains("tools/commands/nmo_cmd_chunk.c",
                               "nmo_cmd_invocation_t");
}

TEST(repl_read, command_source_dispatch_bridge_symbols_are_removed) {
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_ctx_dispatch_from_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.c",
                               "nmo_cmd_ctx_dispatch_from_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_public_handler_t");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_invocation_t");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_global_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.c",
                               "nmo_cmd_global_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.h",
                               "nmo_cmd_global_uses_session_source");
    assert_source_not_contains("tools/nmo_cmd_ctx.c",
                               "nmo_cmd_global_uses_session_source");
}

TEST(repl_read, diff_session_dispatch_does_not_use_ctx_dispatch_helper) {
    assert_source_not_contains("tools/commands/nmo_cmd_diff.c",
                               "nmo_cmd_ctx_dispatch_from_source");
    assert_source_not_contains("tools/commands/nmo_cmd_diff.c",
                               "nmo_cmd_global_source");
    assert_source_not_contains("tools/commands/nmo_cmd_diff.c",
                               "NMO_CMD_SOURCE_SESSION");
}

TEST(repl_read, explicit_document_source_initializes_command_context) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    nmo_cli_global_opts_t global;
    nmo_cli_global_opts_init(&global);
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_DOCUMENT,
        .ctx = repl.ctx,
        .document = repl.document,
        .workspace = repl.workspace,
        .source_label = "(test explicit document)",
    };

    nmo_cmd_ctx_t cmd;
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS,
              nmo_cmd_ctx_init_from_source(&cmd, 0, NULL, &global, &source));
    char *argv[] = {"info"};
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, nmo_cmd_file_in_session(&cmd, 1, argv));
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS,
              nmo_cmd_ctx_done(&cmd, NMO_CLI_EXIT_SUCCESS));
    ASSERT_FALSE(repl.dirty);

    close_repl(&repl);
}

TEST(repl_read, registry_dispatches_document_reads) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    char *file_info[] = {"info"};
    char *object_list[] = {"list", "--top", "3"};
    char *parameter_show[] = {"show", "46"};

    assert_registry_in_session_ok(&repl, "file", 1, file_info);
    assert_registry_in_session_ok(&repl, "object", 3, object_list);
    assert_registry_in_session_ok(&repl, "parameter", 2, parameter_show);

    close_repl(&repl);
}

TEST(repl_read, command_registry_is_shared_repl_policy_source) {
    const nmo_cli_group_t *object =
        nmo_command_registry_find_group("object", false);
    ASSERT_NOT_NULL(object);
    ASSERT_NULL(nmo_command_registry_find_group("obj", false));
    ASSERT_NOT_NULL(nmo_command_registry_find_group("obj", true));

    const nmo_cli_action_t *list =
        nmo_command_registry_find_action(object, "list", true);
    const nmo_cli_action_t *rename =
        nmo_command_registry_find_action(object, "rename", true);
    const nmo_cli_action_t *import =
        nmo_command_registry_find_action(object, "import", true);
    ASSERT_NOT_NULL(list);
    ASSERT_NOT_NULL(rename);
    ASSERT_NOT_NULL(import);
    ASSERT_EQ(NMO_REPL_ACTION_READ_SESSION, list->repl_policy);
    ASSERT_EQ(NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED, rename->repl_policy);
    ASSERT_EQ(NMO_REPL_ACTION_MUTATE_FILE_ONLY, import->repl_policy);

    ASSERT_NULL(nmo_command_registry_find_group("query", false));

    const nmo_cli_group_t *type =
        nmo_command_registry_find_group("type", false);
    const nmo_cli_action_t *type_list =
        nmo_command_registry_find_action(type, "list", true);
    ASSERT_NOT_NULL(type_list);
    ASSERT_EQ(NMO_REPL_ACTION_READ_NO_SESSION, type_list->repl_policy);

    const nmo_cli_group_t *completion =
        nmo_command_registry_find_group("completion", false);
    const nmo_cli_action_t *bash =
        nmo_command_registry_find_action(completion, "bash", true);
    ASSERT_NOT_NULL(bash);
    ASSERT_EQ(NMO_REPL_ACTION_READ_NO_SESSION, bash->repl_policy);
}

TEST(repl_read, completion_group_does_not_require_session) {
    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));

    assert_read_ok(&repl, "completion bash");
}

TEST(repl_read, legacy_read_shortcuts_still_work) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    assert_read_ok(&repl, "show 0");
    assert_read_ok(&repl, "refs 0");
    assert_read_ok(&repl, "param id:46");
    assert_read_fails_clean(&repl, "query id");

    close_repl(&repl);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(repl_read, object_grouped_read_commands_use_cli_shape);
    REGISTER_TEST(repl_read, object_list_fields_uses_full_session_core);
    REGISTER_TEST(repl_read, object_refgraph_actions_use_session_core);
    REGISTER_TEST(repl_read, behavior_read_actions_use_session_core);
    REGISTER_TEST(repl_read, cli_read_option_values_with_file_suffix_are_not_file_operands);
    REGISTER_TEST(repl_read, domain_list_class_filter_cannot_be_overridden);
    REGISTER_TEST(repl_read, mesh_and_animation_exports_use_session_core);
    REGISTER_TEST(repl_read, resource_and_texture_extract_use_session_core);
    REGISTER_TEST(repl_read, export_reads_reject_missing_output_directory);
    REGISTER_TEST(repl_read, domain_show_commands_enforce_family_type);
    REGISTER_TEST(repl_read, entity_show_uses_entity_session_core);
    REGISTER_TEST(repl_read, parameter_grouped_read_commands_use_cli_shape);
    REGISTER_TEST(repl_read, grouped_read_commands_reject_invalid_cli_shape);
    REGISTER_TEST(repl_read, mirrored_cli_read_groups_are_available);
    REGISTER_TEST(repl_read, cli_read_mirror_does_not_expose_snapshot_paths);
    REGISTER_TEST(repl_read, cli_wrapper_supports_global_options);
    REGISTER_TEST(repl_read, domain_cli_read_groups_are_available);
    REGISTER_TEST(repl_read, cli_read_mirror_uses_current_session_snapshot);
    REGISTER_TEST(repl_read, validate_all_reads_dirty_current_session);
    REGISTER_TEST(repl_read, mutating_cli_actions_are_rejected_by_read_mirror);
    REGISTER_TEST(repl_read, cli_batch_mode_is_rejected);
    REGISTER_TEST(repl_read, family_repl_read_cores_are_directly_callable);
    REGISTER_TEST(repl_read, all_read_session_groups_have_family_dispatchers);
    REGISTER_TEST(repl_read, repl_does_not_export_read_fallback_probes);
    REGISTER_TEST(repl_read, no_borrowed_session_adapter_symbols_remain);
    REGISTER_TEST(repl_read, no_remaining_repl_read_placeholder_strings);
    REGISTER_TEST(repl_read, script_run_uses_executor_handles_only);
    REGISTER_TEST(repl_read, script_run_does_not_keep_script_edit_report);
    REGISTER_TEST(repl_read, script_run_does_not_keep_private_validation);
    REGISTER_TEST(repl_read, script_run_uses_executor_report_directly);
    REGISTER_TEST(repl_read, script_write_commands_use_edit_report_validation_only);
    REGISTER_TEST(repl_read, edit_report_json_does_not_emit_legacy_risk_level);
    REGISTER_TEST(repl_read, edit_report_schema_v2_has_single_json_helper);
    REGISTER_TEST(repl_read, behavior_rewrite_reuses_semantic_risk_json_helper);
    REGISTER_TEST(repl_read, behavior_rewrite_does_not_emit_private_write_reports);
    REGISTER_TEST(repl_read, patch_manifest_does_not_keep_parallel_operation_array);
    REGISTER_TEST(repl_read, patch_interface_policy_parses_into_edit_plan);
    REGISTER_TEST(repl_read, patch_set_data_cell_parses_into_edit_plan);
    REGISTER_TEST(repl_read, patch_operation_edits_parse_into_edit_plan);
    REGISTER_TEST(repl_read, patch_io_edits_parse_into_edit_plan);
    REGISTER_TEST(repl_read, patch_behavior_link_edits_parse_into_edit_plan);
    REGISTER_TEST(repl_read, patch_parameter_edits_parse_into_edit_plan);
    REGISTER_TEST(repl_read, patch_node_edits_parse_into_edit_plan);
    REGISTER_TEST(repl_read, patch_replace_bb_parses_into_edit_plan);
    REGISTER_TEST(repl_read, patch_fold_parses_into_edit_plan);
    REGISTER_TEST(repl_read, lua_fold_maps_accept_patch_id_aliases);
    REGISTER_TEST(repl_read, replace_bb_semantic_risks_merge_into_edit_report);
    REGISTER_TEST(repl_read, patch_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, script_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, behavior_rewrite_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, behavior_link_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, parameter_set_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, data_set_cell_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, debug_probe_uses_schema_v2_output_path);
    REGISTER_TEST(repl_read, debug_probe_uses_kind_spec_table);
    REGISTER_TEST(repl_read, behavior_link_commands_use_edit_executor);
    REGISTER_TEST(repl_read, parameter_set_uses_edit_executor);
    REGISTER_TEST(repl_read, data_set_cell_uses_edit_executor);
    REGISTER_TEST(repl_read, domain_session_dispatchers_do_not_construct_object_argv);
    REGISTER_TEST(repl_read, read_family_headers_only_export_family_session_entrypoints);
    REGISTER_TEST(repl_read, no_active_session_adapter_symbols_remain);
    REGISTER_TEST(repl_read, command_source_is_not_a_global_cli_option);
    REGISTER_TEST(repl_read, chunk_session_dispatch_does_not_use_ctx_dispatch_helper);
    REGISTER_TEST(repl_read, command_source_dispatch_bridge_symbols_are_removed);
    REGISTER_TEST(repl_read, diff_session_dispatch_does_not_use_ctx_dispatch_helper);
    REGISTER_TEST(repl_read, explicit_document_source_initializes_command_context);
    REGISTER_TEST(repl_read, registry_dispatches_document_reads);
    REGISTER_TEST(repl_read, command_registry_is_shared_repl_policy_source);
    REGISTER_TEST(repl_read, completion_group_does_not_require_session);
    REGISTER_TEST(repl_read, legacy_read_shortcuts_still_work);
TEST_MAIN_END()
