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

TEST(repl_write, set_param_persists_object_reference_values) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"),
        "test_repl_write_tmp/parameter_input.nmo"));
    remove("test_repl_write_tmp/parameter_object.nmo");

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/parameter_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "set-param id:46 520"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/parameter_object.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/parameter_object.nmo");
    const nmo_parameter_state_t *state = write_probe_parameter_state(&probe, 46);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(520u, state->object_id);
    write_probe_close(&probe);
}

TEST(repl_write, set_param_accepts_cli_style_id_selector) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"),
        "test_repl_write_tmp/parameter_id_input.nmo"));
    remove("test_repl_write_tmp/parameter_id_object.nmo");

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/parameter_id_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "set-param --id 46 520"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/parameter_id_object.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/parameter_id_object.nmo");
    const nmo_parameter_state_t *state = write_probe_parameter_state(&probe, 46);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(520u, state->object_id);
    write_probe_close(&probe);
}

TEST(repl_write, rename_accepts_cli_style_exact_name_selector) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/rename_name_input.nmo"));
    remove("test_repl_write_tmp/rename_name_out.nmo");

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/rename_name_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "rename --name Cam_Pos ReplCamPos"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/rename_name_out.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/rename_name_out.nmo");
    nmo_object_t *renamed = write_probe_object_by_id(&probe, 2);
    ASSERT_NOT_NULL(renamed);
    ASSERT_STR_EQ("ReplCamPos", nmo_object_get_name(renamed));
    ASSERT_NULL(write_probe_object_by_name(&probe, "Cam_Pos"));
    write_probe_close(&probe);
}

TEST(repl_write, copy_accepts_cli_style_id_selector) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/copy_id_input.nmo"));
    remove("test_repl_write_tmp/copy_id_out.nmo");

    write_semantic_probe_t baseline;
    assert_probe_open(&baseline, "test_repl_write_tmp/copy_id_input.nmo");
    size_t baseline_count = write_probe_object_count(&baseline);
    write_probe_close(&baseline);

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/copy_id_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "copy --id 2"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/copy_id_out.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/copy_id_out.nmo");
    ASSERT_EQ(baseline_count + 1u, write_probe_object_count(&probe));
    ASSERT_NOT_NULL(write_probe_object_by_id(&probe, 2));
    write_probe_close(&probe);
}

TEST(repl_write, create_and_delete_persist_name_selected_objects) {
    make_dir("test_repl_write_tmp");
    ASSERT_TRUE(copy_file_binary(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        "test_repl_write_tmp/delete_name_input.nmo"));
    remove("test_repl_write_tmp/delete_name_out.nmo");

    write_semantic_probe_t baseline;
    assert_probe_open(&baseline, "test_repl_write_tmp/delete_name_input.nmo");
    size_t baseline_count = write_probe_object_count(&baseline);
    write_probe_close(&baseline);

    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(&repl, "test_repl_write_tmp/delete_name_input.nmo",
                                   errbuf, sizeof(errbuf)));

    ASSERT_EQ(0, run_repl_command(&repl, "create CKGroup ReplDeleteMe"));
    ASSERT_EQ(0, run_repl_command(&repl, "delete --name ReplDeleteMe"));
    ASSERT_TRUE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "save test_repl_write_tmp/delete_name_out.nmo"));
    close_repl(&repl);

    write_semantic_probe_t probe;
    assert_probe_open(&probe, "test_repl_write_tmp/delete_name_out.nmo");
    ASSERT_EQ(baseline_count, write_probe_object_count(&probe));
    ASSERT_NULL(write_probe_object_by_name(&probe, "ReplDeleteMe"));
    write_probe_close(&probe);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(repl_write, set_param_persists_object_reference_values);
    REGISTER_TEST(repl_write, set_param_accepts_cli_style_id_selector);
    REGISTER_TEST(repl_write, rename_accepts_cli_style_exact_name_selector);
    REGISTER_TEST(repl_write, copy_accepts_cli_style_id_selector);
    REGISTER_TEST(repl_write, create_and_delete_persist_name_selected_objects);
TEST_MAIN_END()
