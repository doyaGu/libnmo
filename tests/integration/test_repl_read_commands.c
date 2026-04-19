/**
 * @file test_repl_read_commands.c
 * @brief Semantic regression coverage for grouped REPL read commands.
 */

#include "test_framework.h"

#include "../../tools/nmo_repl_commands.h"
#include "../../tools/nmo_repl_session.h"
#include "../../tools/nmo_repl_util.h"
#include "../../tools/nmo_tool_session.h"

#include <string.h>

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

static void open_repl(nmo_repl_context_t *repl, const char *path) {
    memset(repl, 0, sizeof(*repl));
    char errbuf[256];
    ASSERT_TRUE(nmo_repl_load_file(repl, path, errbuf, sizeof(errbuf)));
    ASSERT_FALSE(repl->dirty);
}

TEST(repl_read, object_grouped_read_commands_use_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/Camera.nmo"));

    ASSERT_EQ(0, run_repl_command(&repl, "object show 2"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "object show --id 2"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "object show --name Cam_Pos"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "object refs 2"));
    ASSERT_FALSE(repl.dirty);

    close_repl(&repl);
}

TEST(repl_read, parameter_grouped_read_commands_use_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    ASSERT_EQ(0, run_repl_command(&repl, "parameter show 46"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "parameter dump 46"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "parameter dump --all"));
    ASSERT_FALSE(repl.dirty);

    close_repl(&repl);
}

TEST(repl_read, grouped_read_commands_reject_invalid_cli_shape) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    ASSERT_NE(0, run_repl_command(&repl, "object show id:2"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_NE(0, run_repl_command(&repl, "object show"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_NE(0, run_repl_command(&repl, "object refs --name MissingName"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_NE(0, run_repl_command(&repl, "parameter show id:46"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_NE(0, run_repl_command(&repl, "parameter dump --type bad-guid --all"));
    ASSERT_FALSE(repl.dirty);

    close_repl(&repl);
}

TEST(repl_read, legacy_read_shortcuts_still_work) {
    nmo_repl_context_t repl;
    open_repl(&repl, NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo"));

    ASSERT_EQ(0, run_repl_command(&repl, "show 0"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "refs 0"));
    ASSERT_FALSE(repl.dirty);
    ASSERT_EQ(0, run_repl_command(&repl, "param id:46"));
    ASSERT_FALSE(repl.dirty);

    close_repl(&repl);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(repl_read, object_grouped_read_commands_use_cli_shape);
    REGISTER_TEST(repl_read, parameter_grouped_read_commands_use_cli_shape);
    REGISTER_TEST(repl_read, grouped_read_commands_reject_invalid_cli_shape);
    REGISTER_TEST(repl_read, legacy_read_shortcuts_still_work);
TEST_MAIN_END()
