/**
 * @file test_repl_write_commands.c
 * @brief Semantic regression coverage for REPL write commands.
 */

#include "test_framework.h"
#include "write_semantic_probe.h"

#include "../../tools/nmo_repl_commands.h"
#include "../../tools/nmo_repl_session.h"
#include "../../tools/nmo_repl_util.h"
#include "../../tools/nmo_tool_session.h"

#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_repository.h"

#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

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

static void close_repl(nmo_repl_context_t *repl) {
    if (!repl) {
        return;
    }
    nmo_tool_close_session(repl->ctx, repl->session);
    repl->ctx = NULL;
    repl->session = NULL;
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
    ASSERT_EQ(expected_size, actual_size);
    ASSERT_TRUE(memcmp(actual, expected, expected_size) == 0);
}

static const nmo_parameter_state_t *repl_parameter_state(
    nmo_repl_context_t *repl,
    nmo_object_id_t id)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(repl->session);
    nmo_object_t *obj = repo ? nmo_object_repository_find_by_id(repo, id) : NULL;
    return obj ? nmo_parameter_get_state(obj) : NULL;
}

TEST(repl_write, legacy_mutation_commands_are_removed) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/legacy_removed_input.nmo"));

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/legacy_removed_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_NE(0, run_repl_command(&repl, "rename 2 ReplCamPos"));
    ASSERT_NE(0, run_repl_command(&repl, "set-param --id 46 520"));
    ASSERT_FALSE(repl.dirty);
    close_repl(&repl);
}

TEST(repl_write, object_rename_uses_cli_shape) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/rename_input.nmo"));
    remove("test_repl_write_tmp/rename_out.nmo");

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/rename_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_NE(0, run_repl_command(&repl, "object rename id:2 ReplCamPos"));
    ASSERT_FALSE(repl.dirty);

    ASSERT_EQ(0, run_repl_command(&repl, "object rename 2 ReplCamPos"));
    ASSERT_EQ(0, run_repl_command(&repl, "object ren 2 ReplCamAlias"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/rename_out.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/rename_out.nmo");
    nmo_object_t *renamed = write_probe_object_by_id(&probe, 2);
    ASSERT_NOT_NULL(renamed);
    ASSERT_STR_EQ("ReplCamAlias", nmo_object_get_name(renamed));
    ASSERT_NULL(write_probe_object_by_name(&probe, "Cam_Pos"));
    write_probe_close(&probe);
}

TEST(repl_write, object_create_delete_and_copy_use_cli_filters) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/object_mutation_input.nmo"));
    remove("test_repl_write_tmp/object_mutation_out.nmo");

    write_semantic_probe_t baseline;
    assert_probe_open(&baseline, "test_repl_write_tmp/object_mutation_input.nmo");
    size_t baseline_count = write_probe_object_count(&baseline);
    write_probe_close(&baseline);

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/object_mutation_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "object create --class CKGroup --name ReplDeleteMe"));
    ASSERT_EQ(0, run_repl_command(&repl, "object delete --name ReplDeleteMe"));
    ASSERT_EQ(0, run_repl_command(&repl, "object copy 2"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/object_mutation_out.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/object_mutation_out.nmo");
    ASSERT_EQ(baseline_count + 1u, write_probe_object_count(&probe));
    ASSERT_NULL(write_probe_object_by_name(&probe, "ReplDeleteMe"));
    ASSERT_NOT_NULL(write_probe_object_by_id(&probe, 2));
    write_probe_close(&probe);
}

TEST(repl_write, parameter_set_uses_cli_shape) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"),
        "test_repl_write_tmp/parameter_input.nmo"));
    remove("test_repl_write_tmp/parameter_out.nmo");

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/parameter_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "parameter set --id 46 520"));
    ASSERT_EQ(0, run_repl_command(&repl, "parameter set --hex --id 64 2A000000"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/parameter_out.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/parameter_out.nmo");
    const nmo_parameter_state_t *object_state = write_probe_parameter_state(&probe, 46);
    ASSERT_NOT_NULL(object_state);
    ASSERT_EQ(520u, object_state->object_id);
    const nmo_parameter_state_t *hex_state = write_probe_parameter_state(&probe, 64);
    ASSERT_NOT_NULL(hex_state);
    const unsigned char expected[] = {0x2A, 0x00, 0x00, 0x00};
    assert_bytes_eq(hex_state->buffer_data.data, hex_state->buffer_data.count,
                    expected, sizeof(expected));
    write_probe_close(&probe);
}

TEST(repl_write, mutation_options_reject_output_and_unknown_options) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/reject_options_input.nmo"));

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/reject_options_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_NE(0, run_repl_command(&repl, "object delete 1 --unknown"));
    ASSERT_NE(0, run_repl_command(&repl, "object copy 2 -o test_repl_write_tmp/nope.nmo"));
    ASSERT_NE(0, run_repl_command(&repl, "parameter set --id 46 520 --output test_repl_write_tmp/nope.nmo"));
    ASSERT_FALSE(repl.dirty);
    close_repl(&repl);
}

TEST(repl_write, dry_run_mutations_do_not_change_session_or_dirty_flag) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"),
        "test_repl_write_tmp/dry_run_input.nmo"));

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/dry_run_input.nmo",
                                   errbuf, sizeof(errbuf)));

    size_t before_count = nmo_repl_object_count(&repl);
    const nmo_parameter_state_t *before_param = repl_parameter_state(&repl, 46);
    ASSERT_NOT_NULL(before_param);
    ASSERT_NE(520u, before_param->object_id);

    ASSERT_EQ(0, run_repl_command(&repl, "object create --dry-run --class CKGroup --name DryProbe"));
    ASSERT_EQ(before_count, nmo_repl_object_count(&repl));
    ASSERT_FALSE(repl.dirty);

    ASSERT_EQ(0, run_repl_command(&repl, "object copy --dry-run 1"));
    ASSERT_EQ(before_count, nmo_repl_object_count(&repl));
    ASSERT_FALSE(repl.dirty);

    ASSERT_EQ(0, run_repl_command(&repl, "object delete --dry-run 1"));
    ASSERT_EQ(before_count, nmo_repl_object_count(&repl));
    ASSERT_FALSE(repl.dirty);

    ASSERT_EQ(0, run_repl_command(&repl, "parameter set --dry-run --id 46 520"));
    const nmo_parameter_state_t *after_param = repl_parameter_state(&repl, 46);
    ASSERT_NOT_NULL(after_param);
    ASSERT_EQ(before_param->object_id, after_param->object_id);
    ASSERT_FALSE(repl.dirty);

    ASSERT_NE(0, run_repl_command(&repl, "parameter set --dry-run --owner 1 --index 0 1"));
    ASSERT_FALSE(repl.dirty);

    ASSERT_EQ(0, run_repl_command(&repl, "parameter set --dry-run --owner 84 --index 0 1"));
    ASSERT_FALSE(repl.dirty);
    close_repl(&repl);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(repl_write, legacy_mutation_commands_are_removed);
    REGISTER_TEST(repl_write, object_rename_uses_cli_shape);
    REGISTER_TEST(repl_write, object_create_delete_and_copy_use_cli_filters);
    REGISTER_TEST(repl_write, parameter_set_uses_cli_shape);
    REGISTER_TEST(repl_write, mutation_options_reject_output_and_unknown_options);
    REGISTER_TEST(repl_write, dry_run_mutations_do_not_change_session_or_dirty_flag);
TEST_MAIN_END()
