/**
 * @file test_repl_read_commands.c
 * @brief Semantic regression coverage for grouped REPL read commands.
 */

#include "test_framework.h"

#include "../../tools/nmo_cmd_ctx.h"
#include "../../tools/nmo_repl_commands.h"
#include "../../tools/nmo_repl_session.h"
#include "../../tools/nmo_repl_util.h"
#include "../../tools/commands/nmo_cmd_chunk.h"
#include "../../tools/commands/nmo_cmd_diff.h"
#include "../../tools/commands/nmo_cmd_file.h"
#include "../../tools/commands/nmo_cmd_object.h"
#include "../../tools/commands/nmo_cmd_parameter.h"
#include "../../tools/commands/nmo_cmd_query.h"
#include "../../tools/commands/nmo_cmd_validate.h"
#include "../../tools/nmo_tool_session.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#define NMO_TEST_DUP _dup
#define NMO_TEST_DUP2 _dup2
#define NMO_TEST_CLOSE _close
#define NMO_TEST_FILENO _fileno
#else
#include <unistd.h>
#define NMO_TEST_DUP dup
#define NMO_TEST_DUP2 dup2
#define NMO_TEST_CLOSE close
#define NMO_TEST_FILENO fileno
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

static void close_repl(nmo_repl_context_t *repl) {
    if (!repl) {
        return;
    }
    nmo_tool_close_session(repl->ctx, repl->session);
    repl->ctx = NULL;
    repl->session = NULL;
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
              nmo_cmd_ctx_init_with_session(&cmd, repl->ctx, repl->session,
                                            "(test session)", &global));
    ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, handler(&cmd, argc, argv));
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

TEST(repl_read, object_grouped_read_commands_use_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    assert_read_ok(&repl, "object show 2");
    assert_read_ok(&repl, "object show --id 2");
    assert_read_ok(&repl, "object show --name Cam_Pos");
    assert_read_ok(&repl, "object refs 2");

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
    assert_read_ok(&repl, "query eval --object 520 has_target");
    assert_read_ok(&repl, "type list");
    assert_read_ok(&repl, "extension list");
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
              nmo_session_add_included_file(repl.session,
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

TEST(repl_read, specialized_repl_read_cores_are_directly_callable) {
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
    char *query_eval[] = {"eval", "--object", "520", "has_target"};
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

    assert_in_session_ok(&repl, nmo_cmd_file_info_in_session, 1, file_info);
    assert_in_session_ok(&repl, nmo_cmd_file_header_in_session, 1, file_header);
    assert_in_session_ok(&repl, nmo_cmd_file_stats_in_session, 1, file_stats);
    assert_in_session_ok(&repl, nmo_cmd_file_classes_in_session, 1, file_classes);
    assert_in_session_ok(&repl, nmo_cmd_file_plugins_in_session, 1, file_plugins);
    assert_in_session_ok(&repl, nmo_cmd_file_space_in_session, 1, file_space);
    assert_in_session_ok(&repl, nmo_cmd_validate_all_in_session, 1, validate_all);
    assert_in_session_ok(&repl, nmo_cmd_validate_structure_in_session, 1, validate_structure);
    assert_in_session_ok(&repl, nmo_cmd_validate_references_in_session, 1, validate_references);
    assert_in_session_ok(&repl, nmo_cmd_validate_resources_in_session, 1, validate_resources);
    assert_in_session_ok(&repl, nmo_cmd_validate_orphans_in_session, 1, validate_orphans);
    assert_in_session_ok(&repl, nmo_cmd_query_eval_in_session, 4, query_eval);
    assert_in_session_ok(&repl, nmo_cmd_diff_summary_in_session, 2, diff_summary);
    assert_in_session_ok(&repl, nmo_cmd_diff_objects_in_session, 2, diff_objects);
    assert_in_session_ok(&repl, nmo_cmd_diff_chunks_in_session, 2, diff_chunks);
    assert_in_session_ok(&repl, nmo_cmd_diff_full_in_session, 2, diff_full);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 3, chunk_list);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 1, chunk_tree);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 2, chunk_show);
    assert_in_session_ok(&repl, nmo_cmd_chunk_in_session, 3, chunk_find);
    assert_in_session_ok(&repl, nmo_cmd_object_show_in_session, 2, object_show);
    assert_in_session_ok(&repl, nmo_cmd_object_refs_in_session, 2, object_refs);
    assert_in_session_ok(&repl, nmo_cmd_parameter_show_in_session, 2, parameter_show);
    assert_in_session_ok(&repl, nmo_cmd_parameter_dump_in_session, 2, parameter_dump);

    close_repl(&repl);
}

TEST(repl_read, cli_read_table_has_no_session_public_fallbacks) {
    ASSERT_EQ(0u, nmo_repl_cli_read_session_public_fallback_count());
    ASSERT_EQ(0u, nmo_repl_cli_read_generic_session_count_for_group("chunk"));
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
    assert_read_ok(&repl, "query id");

    close_repl(&repl);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(repl_read, object_grouped_read_commands_use_cli_shape);
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
    REGISTER_TEST(repl_read, specialized_repl_read_cores_are_directly_callable);
    REGISTER_TEST(repl_read, cli_read_table_has_no_session_public_fallbacks);
    REGISTER_TEST(repl_read, completion_group_does_not_require_session);
    REGISTER_TEST(repl_read, legacy_read_shortcuts_still_work);
TEST_MAIN_END()
