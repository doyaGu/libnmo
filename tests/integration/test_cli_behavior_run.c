#include "../unit/engine_fixture.h"

#include "../../tools/nmo_cli_common.h"
#include "yyjson.h"

#ifndef _WIN32
#include <sys/wait.h>
#define RUN_POPEN popen
#define RUN_PCLOSE pclose
#else
#define RUN_POPEN _popen
#define RUN_PCLOSE _pclose
#endif

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_TRANSLATE NMO_GUID(0x000d000d, 0x000d000d)
#define GUID_OP NMO_GUID(0x2d5d6d01, 0x6a353eb0)

typedef enum run_fixture_kind {
    RUN_UNKNOWN, RUN_LOOP, RUN_OPERATION, RUN_OK
} run_fixture_kind_t;

static void write_run_fixture(const char *path, run_fixture_kind_t kind,
                              nmo_object_id_t *out_behavior)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_guid_t guid = kind == RUN_UNKNOWN ? GUID_TRANSLATE : kind == RUN_OPERATION ? GUID_OP : GUID_NOP;
    nmo_object_id_t block = fixture_block(&f, guid, "First");
    nmo_object_id_t after = fixture_block(&f, GUID_NOP, "After");
    ASSERT_TRUE(block != 0 && after != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, block, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, block, 0),
                  BLOCK_IN(&f, kind == RUN_LOOP ? block : after, 0), 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_session_save_file(f.session, path, NULL, NULL));
    *out_behavior = kind == RUN_LOOP ? f.script_id : block;
    fixture_end(&f);
}

typedef struct run_result {
    char output[32768];
    int exit_code;
} run_result_t;

static run_result_t run_cli(const char *args)
{
    run_result_t result = {{0}, -1};
    char command[2048];
    snprintf(command, sizeof(command), "\"%s\" %s 2>&1", NMO_CLI_PATH, args);
    FILE *pipe = RUN_POPEN(command, "r");
    if (pipe == NULL) {
        return result;
    }
    size_t length = fread(result.output, 1, sizeof(result.output) - 1, pipe);
    result.output[length] = '\0';
    bool complete = feof(pipe) && !ferror(pipe);
    int status = RUN_PCLOSE(pipe);
    if (!complete) {
        return result;
    }
    result.exit_code = status;
#ifndef _WIN32
    if (status >= 0 && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
#endif
    return result;
}

TEST(cli_behavior_run, strict_reports_failures_in_json_even_when_quiet)
{
    const char *path = NMO_TEST_SCRATCH_FILE("cli_behavior_run_failure.cmo");
    const char *kinds[] = {"unknown block", "infinite loop", "operation"};
    int results[] = {0, 4, NMO_ERR_NOT_FOUND};
    const char *commands[] = {"--strict behavior run", "behavior run --strict"};
    for (int kind = RUN_UNKNOWN; kind <= RUN_OPERATION; kind++) {
        nmo_object_id_t behavior = 0;
        write_run_fixture(path, (run_fixture_kind_t)kind, &behavior);
        for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            char args[1024];
            snprintf(args, sizeof(args),
                     "-f json %s --quiet --unknown pass --start all --frames 5 "
                     "--max-iterations 3 \"%s\"", commands[i], path);
            run_result_t result = run_cli(args);
            ASSERT_EQ(NMO_CLI_EXIT_STRICT_FAILURE, result.exit_code);
            yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
            ASSERT_NOT_NULL(doc);
            yyjson_val *data = yyjson_obj_get(yyjson_doc_get_root(doc), "data");
            ASSERT_TRUE(yyjson_get_bool(yyjson_obj_get(data, "failed")));
            ASSERT_EQ(1u, yyjson_get_uint(yyjson_obj_get(data, "frames")));
            ASSERT_EQ(0u, yyjson_arr_size(yyjson_obj_get(data, "events")));
            yyjson_val *failure = yyjson_obj_get(data, "failure");
            ASSERT_NOT_NULL(failure);
            ASSERT_STR_EQ(kinds[kind], yyjson_get_str(yyjson_obj_get(failure, "kind")));
            ASSERT_EQ(behavior, yyjson_get_uint(yyjson_obj_get(failure, "behavior_id")));
            ASSERT_EQ(0u, yyjson_get_uint(yyjson_obj_get(failure, "frame")));
            ASSERT_EQ(results[kind], yyjson_get_sint(yyjson_obj_get(failure, "result")));
            yyjson_doc_free(doc);
        }
    }
    remove(path);
}

TEST(cli_behavior_run, default_unknown_policy_and_successful_strict_runs_still_succeed)
{
    const char *path = NMO_TEST_SCRATCH_FILE("cli_behavior_run_compatible.cmo");
    const char *options[] = {"--unknown stop", "--unknown pass", "--strict"};
    for (int i = 0; i < 3; i++) {
        nmo_object_id_t behavior = 0;
        write_run_fixture(path, i == 2 ? RUN_OK : RUN_UNKNOWN, &behavior);
        char args[1024];
        snprintf(args, sizeof(args), "-f json behavior run %s --start all --frames 2 \"%s\"",
                 options[i], path);
        run_result_t result = run_cli(args);
        ASSERT_EQ(NMO_CLI_EXIT_SUCCESS, result.exit_code);
        yyjson_doc *doc = yyjson_read(result.output, strlen(result.output), 0);
        ASSERT_NOT_NULL(doc);
        yyjson_val *data = yyjson_obj_get(yyjson_doc_get_root(doc), "data");
        ASSERT_FALSE(yyjson_get_bool(yyjson_obj_get(data, "failed")));
        ASSERT_NULL(yyjson_obj_get(data, "failure"));
        ASSERT_EQ(2u, yyjson_get_uint(yyjson_obj_get(data, "frames")));
        ASSERT_EQ(i == 0 ? 1u : 2u, yyjson_get_uint(yyjson_obj_get(data, "block_executions")));
        yyjson_doc_free(doc);
    }
    remove(path);
}

TEST(cli_behavior_run, strict_text_names_the_failure)
{
    const char *path = NMO_TEST_SCRATCH_FILE("cli_behavior_run_text.cmo");
    nmo_object_id_t behavior = 0;
    write_run_fixture(path, RUN_UNKNOWN, &behavior);
    char args[1024];
    snprintf(args, sizeof(args), "behavior run --strict --quiet --start all \"%s\"", path);
    run_result_t result = run_cli(args);
    ASSERT_EQ(NMO_CLI_EXIT_STRICT_FAILURE, result.exit_code);
    ASSERT_STR_CONTAINS(result.output, "Failure kind");
    ASSERT_STR_CONTAINS(result.output, "unknown block");
    ASSERT_STR_CONTAINS(result.output, "Failure behavior");
    ASSERT_STR_CONTAINS(result.output, "First#");
    remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(cli_behavior_run, strict_reports_failures_in_json_even_when_quiet);
REGISTER_TEST(cli_behavior_run, default_unknown_policy_and_successful_strict_runs_still_succeed);
REGISTER_TEST(cli_behavior_run, strict_text_names_the_failure);
TEST_MAIN_END()
